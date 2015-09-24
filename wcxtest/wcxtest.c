#include <wchar.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "wcxhead.h"

#define PROG_LOGO																\
L"Total Commander WCX plugin Test ver. 0.23 Copyright (c) 2004-2006 Oleg Bondar.\n"	\
L"                                          Copyright (c) 2010 Cristian Adam.\n"	\
L"Implements algorithm described in \"WCX Writer\'s Reference\" to test plugin's\n"	\
L"functionality.\n"

#define PROG_USAGE																\
L"Usage:\n"																   \
L"  wcxtest [-f | -l | | -t | -x] [-q] <wcx_path> [<arc_path>] [<dir_path>]\n" \
L"\n"																			\
L"<wcx_path> - path to WCX plugin\n"												\
L"<arc_path> - path to archive file\n"											\
L"<dir_path> - directory to unpack files, default is current\n"					\
L"  -f - list WCX exported functions\n"											\
L"  -l - List archive contents (default)\n"										\
L"  -t - Test archive contents\n"												\
L"  -x - eXtract files from archive (overwrite existing)\n"						\
L"  -?, -h - this topic\n"														\
L"\n"																			\
L"  -v - Verbose\n"																\
L"\n"																			\
L"ERRORLEVEL: 0 - success, non zero - some (m.b. unknown) error.\n"				\
L"\n"																			\
L"Switches are NOT case sensitive. It\'s order - arbitrary.\n"					\
L"Program NOT tested with file names that contains non-ASCII wchar_ts.\n"


#define TODO_FLIST		0
#define TODO_LIST		1
#define TODO_TEST		2
#define TODO_EXTRACT	3

#define ERR_NO_WCX		1
#define ERR_NO_PROC		2
#define ERR_OPEN_FAIL	3
#define ERR_CLOSE_FAIL	4


/* mandatory functions */
static HANDLE	(__stdcall *pOpenArchive)(tOpenArchiveDataW *ArchiveData) = NULL;
static int		(__stdcall *pReadHeader)(HANDLE hArcData, tHeaderDataExW *HeaderData) = NULL;
static int		(__stdcall *pProcessFile)(HANDLE hArcData, int Operation, wchar_t *DestPath, wchar_t *DestName) = NULL;
static int		(__stdcall *pCloseArchive)(HANDLE hArcData) = NULL;

/* optional functions */
static int		(__stdcall *pPackFiles)(wchar_t *PackedFile, wchar_t *SubPath, wchar_t *SrcPath, wchar_t *AddList, int Flags) = NULL;
static int		(__stdcall *pDeleteFiles)(wchar_t *PackedFile, wchar_t *DeleteList) = NULL;
static int		(__stdcall *pGetPackerCaps)(void) = NULL;
static void		(__stdcall *pConfigurePacker)(HWND Parent, HINSTANCE DllInstance);
static void		(__stdcall *pSetChangeVolProc)(HANDLE hArcData, tChangeVolProcW pChangeVolProc1) = NULL;		 /* NOT quite */
static void		(__stdcall *pSetProcessDataProc)(HANDLE hArcData, tProcessDataProcW pProcessDataProc) = NULL; /* NOT quite */

/* packing into memory */
static int		(__stdcall *pStartMemPack)(int Options, wchar_t *FileName) = NULL;
static int		(__stdcall *pPackToMem)(int hMemPack, wchar_t* BufIn, int InLen, int* Taken,
										wchar_t* BufOut, int OutLen, int* Written, int SeekBy) = NULL;
static int		(__stdcall *pDoneMemPack)(int hMemPack) = NULL;

static BOOL		(__stdcall *pCanYouHandleThisFile)(wchar_t *FileName) = NULL;
static void		(__stdcall *pPackSetDefaultParams)(PackDefaultParamStruct* dps) = NULL;

static wchar_t wcx_path[MAX_PATH] = L"";
static wchar_t arc_path[MAX_PATH] = L"";
static wchar_t dir_path[MAX_PATH] = L".\\";
static wchar_t prog_prefix[] = L"WT:";
static int	open_todo = -1;
static int	verbose = 0;


static wchar_t *
WCX_err_msg(int code)
{
	static wchar_t		buf[256];

	switch(code) {
		case E_END_ARCHIVE:		wcscpy(buf,L"No more files in archive");	break;
		case E_NO_MEMORY:		wcscpy(buf,L"Not enough memory");			break;
		case E_BAD_DATA:		wcscpy(buf,L"Data is bad");					break;
		case E_BAD_ARCHIVE:		wcscpy(buf,L"CRC error in archive data");	break;
		case E_UNKNOWN_FORMAT:	wcscpy(buf,L"Archive format unknown");		break;
		case E_EOPEN:			wcscpy(buf,L"Cannot open existing file");	break;
		case E_ECREATE:			wcscpy(buf,L"Cannot create file");			break;
		case E_ECLOSE:			wcscpy(buf,L"Error closing file");			break;
		case E_EREAD:			wcscpy(buf,L"Error reading from file");		break;
		case E_EWRITE:			wcscpy(buf,L"Error writing to file");		break;
		case E_SMALL_BUF:		wcscpy(buf,L"Buffer too small");			break;
		case E_EABORTED:		wcscpy(buf,L"Function aborted by user");	break;
		case E_NO_FILES:		wcscpy(buf,L"No files found");				break;
		case E_TOO_MANY_FILES:	wcscpy(buf,L"Too many files to pack");		break;
		case E_NOT_SUPPORTED:	wcscpy(buf,L"Function not supported");		break;

		default: swprintf(buf,L"Unknown error code (%d)", code); break;
	}

	return buf;
}

static int __stdcall
ChangeVol(wchar_t *ArcName, int Mode)
{
	wchar_t	buf[32];
	int		rc = 0;

	switch(Mode) {
		case PK_VOL_ASK:
		wprintf(L"%sPlease change disk and enter Y or N to stop:", prog_prefix);
		_getws(buf);
		rc = *buf == L'y' || *buf == L'Y';
		break;

		case PK_VOL_NOTIFY:
		wprintf(L"%sProcessing next volume/diskette\n", prog_prefix);
		rc = 1;
		break;

		default:
		wprintf(L"%sUnknown ChangeVolProc mode\n", prog_prefix);
		rc = 0;
		break;
	}

	return rc;
}

static int __stdcall
ProcessData(wchar_t *FileName, int Size)
{
//	  wchar_t	  buf[MAX_PATH];

//	  wchar_tToOem(FileName, buf);
//	  wprintf(L"%sProcessed %s (%d). Ok.\n", prog_prefix, buf, Size); fflush(stdout);
	wprintf(L".");
	return 1;
}


#define DIR_SEPARATOR L'\\'
#define DRV_SEPARATOR L':'

void
check_fndir(wchar_t *fname)
{
	struct _stat	sb;
	wchar_t			*s, buf[MAX_PATH];

	/* check if dir exists; if not create */
	for(s = fname; *s; ) {
		if(*s == DIR_SEPARATOR) ++s;
		while (*s && *s != DIR_SEPARATOR) ++s;
		if(*s == DIR_SEPARATOR) {
			*s = 0;
			/* there is no difference in speed: check if exists directory */
			if(_wstat(fname, & sb) == -1) {
				if(verbose) wprintf(L"%s-- Making dir %s\n", prog_prefix, fname);
				_wmkdir(fname);
			}
			*s = DIR_SEPARATOR;
		}
	}
}


extern int
wmain(int argc, wchar_t *argv[])
{
	int					i, j, rc = 0;
	wchar_t				*s, buf[1024];
	HINSTANCE			hwcx = NULL;
	HANDLE				arch = NULL;
	tOpenArchiveDataW	arcd;
	tHeaderDataExW		hdrd;

	if(argc < 3) {
		wprintf(PROG_LOGO);
		wprintf(PROG_USAGE);
		return 0;
	}

	/* switches */
	for(i = 1; i < argc; ++i) {
		s = argv[i];
		if(*s != L'-' && *s != L'/') continue;
		++s;
		switch(*s) {
			case L'f':
			case L'F': /* list of functions mode */
			if(open_todo < 0) open_todo = TODO_FLIST;
			else {
				wprintf(L"Syntax error: too many switches.\n");
				wprintf(PROG_USAGE);
				return 0;
			}
			break;

			case L'l':
			case L'L': /* list mode */
			if(open_todo < 0) open_todo = TODO_LIST;
			else {
				wprintf(L"Syntax error: too many switches.\n");
				wprintf(PROG_USAGE);
				return 0;
			}
			break;

			case L't':
			case L'T': /* test mode */
			if(open_todo < 0) open_todo = TODO_TEST;
			else {
				wprintf(L"Syntax error: too many switches.\n");
				wprintf(PROG_USAGE);
				return 0;
			}
			break;

			case L'x':
			case L'X': /* extract mode */
			if(open_todo < 0) open_todo = TODO_EXTRACT;
			else {
				wprintf(L"Syntax error: too many switches.\n");
				wprintf(TODO_FLIST);
				return 0;
			}
			break;

			case L'v':
			case L'V':
			verbose = 1;
			break;

			case L'?':
			case L'h':
			case L'H':
			wprintf(PROG_LOGO);
			wprintf(TODO_FLIST);
			return 0;

			default:
			wprintf(L"Syntax error: invalid switch.\n");
			wprintf(PROG_USAGE);
			return 0;
		}
	}
	if(open_todo < 0) open_todo = TODO_LIST;
	if(!verbose) *prog_prefix = 0;

	/* parameters */
	for(i = 1, j = 0; i < argc; ++i) {
		s = argv[i];
		if(*s == L'-' || *s == L'/') continue;
		switch(j) {
			case 0:
			wcscpy(wcx_path, argv[i]);
			break;

			case 1:
			wcscpy(arc_path, argv[i]);
			break;

			case 2:
			wcscpy(dir_path, argv[i]);
			break;

			default:
			wprintf(L"Syntax error: too many arguments.\n");
			wprintf(PROG_USAGE);
			return 0;
		}
		++j;
	}

	if(!*wcx_path) {
		wprintf(L"Syntax error: no WCX name.\n");
		wprintf(PROG_USAGE);
		return 0;
	}
	if(!*arc_path && (open_todo == TODO_LIST || open_todo == TODO_TEST || open_todo == TODO_EXTRACT)) {
		wprintf(L"Syntax error: no archive name.\n");
		wprintf(PROG_USAGE);
		return 0;
	}
	if(*dir_path && dir_path[wcslen(dir_path)-1] != L'\\') wcscat(dir_path,L"\\");

	if(verbose) {
		switch(open_todo) {
			case TODO_FLIST:
			//wprintf(L"%sExported functions in \"%s\":\n", prog_prefix, wcx_path);
			break;

			case TODO_LIST:
			wprintf(L"%sUsing \"%s\" for list files in \"%s\".\n", prog_prefix, wcx_path, arc_path);
			break;

			case TODO_TEST:
			wprintf(L"%sUsing \"%s\" for test files in \"%s\".\n", prog_prefix, wcx_path, arc_path);
			break;

			case TODO_EXTRACT:
			wprintf(L"%sUsing \"%s\" for extract files from \"%s\" to \"%s\".\n", prog_prefix, wcx_path, arc_path, dir_path);
			break;

			default:
			wprintf(L"unknown to do with");
			break;
		}
	}

	if(verbose) wprintf(L"%sLoading plugin %s...", prog_prefix, wcx_path);
	if(!(hwcx = LoadLibrary(wcx_path))) {
		if(verbose)  {
			wprintf(L"Failed. Error code: 0x%x\n", GetLastError());
		}
		else {
			wprintf(L"Failed loading plugin.\n");
		}
		return ERR_NO_WCX;
	}
	if(verbose) wprintf(L" Ok.\n");

	/* mandatory */
	pOpenArchive =			(void *) GetProcAddress(hwcx,"OpenArchiveW");
	pReadHeader =			(void *) GetProcAddress(hwcx,"ReadHeaderExW");
	pProcessFile =			(void *) GetProcAddress(hwcx,"ProcessFileW");
	pCloseArchive =			(void *) GetProcAddress(hwcx,"CloseArchive");
	/* optional */
	pPackFiles =			(void *) GetProcAddress(hwcx,"PackFilesW");
	pDeleteFiles =			(void *) GetProcAddress(hwcx,"DeleteFilesW");
	pGetPackerCaps =		(void *) GetProcAddress(hwcx,"GetPackerCaps");
	pConfigurePacker =		(void *) GetProcAddress(hwcx,"ConfigurePacker");
	/* NOT optional */
	pSetChangeVolProc =		(void *) GetProcAddress(hwcx,"SetChangeVolProcW");
	pSetProcessDataProc =	(void *) GetProcAddress(hwcx,"SetProcessDataProcW");
	/* optional */
	pStartMemPack =			(void *) GetProcAddress(hwcx,"StartMemPackW");
	pPackToMem =			(void *) GetProcAddress(hwcx,"PackToMem");
	pDoneMemPack =			(void *) GetProcAddress(hwcx,"DoneMemPack");
	pCanYouHandleThisFile = (void *) GetProcAddress(hwcx,"CanYouHandleThisFileW");
	pPackSetDefaultParams = (void *) GetProcAddress(hwcx,"PackSetDefaultParams");

	if(open_todo == TODO_FLIST) {
		wprintf(L"%sExported WCX functions in \"%s\":\n", prog_prefix, wcx_path);
		if(pOpenArchive			) wprintf(L"%s  OpenArchiveW\n", prog_prefix);
		if(pReadHeader			) wprintf(L"%s  ReadHeaderExW\n", prog_prefix);
		if(pProcessFile			) wprintf(L"%s  ProcessFileW\n", prog_prefix);
		if(pCloseArchive		) wprintf(L"%s  CloseArchive\n", prog_prefix);
		if(pPackFiles			) wprintf(L"%s  PackFilesW\n", prog_prefix);
		if(pDeleteFiles			) wprintf(L"%s  DeleteFilesW\n", prog_prefix);
		if(pGetPackerCaps		) wprintf(L"%s  GetPackerCaps\n", prog_prefix);
		if(pConfigurePacker		) wprintf(L"%s  ConfigurePacker\n", prog_prefix);
		if(pSetChangeVolProc	) wprintf(L"%s  SetChangeVolProcW\n", prog_prefix);
		if(pSetProcessDataProc	) wprintf(L"%s  SetProcessDataProcW\n", prog_prefix);
		if(pStartMemPack		) wprintf(L"%s  StartMemPack\n", prog_prefix);
		if(pPackToMem			) wprintf(L"%s  PackToMem\n", prog_prefix);
		if(pDoneMemPack			) wprintf(L"%s  DoneMemPack\n", prog_prefix);
		if(pCanYouHandleThisFile) wprintf(L"%s  CanYouHandleThisFileW\n", prog_prefix);
		if(pPackSetDefaultParams) wprintf(L"%s  PackSetDefaultParams\n", prog_prefix);

		if(pGetPackerCaps) {
			int		pc = pGetPackerCaps(), f = 0;

			wprintf(L"%sPackerCaps: %u =", prog_prefix, pc);
			if(pc & PK_CAPS_NEW			) { wprintf(L"%s PK_CAPS_NEW", f ?L" |" :L""); f = 1; }
			if(pc & PK_CAPS_MODIFY		) { wprintf(L"%s PK_CAPS_MODIFY", f ?L" |" :L""); f = 1; }
			if(pc & PK_CAPS_MULTIPLE	) { wprintf(L"%s PK_CAPS_MULTIPLE", f ?L" |" :L""); f = 1; }
			if(pc & PK_CAPS_DELETE		) { wprintf(L"%s PK_CAPS_DELETE", f ?L" |" :L""); f = 1; }
			if(pc & PK_CAPS_OPTIONS		) { wprintf(L"%s PK_CAPS_OPTIONS", f ?L" |" :L""); f = 1; }
			if(pc & PK_CAPS_MEMPACK		) { wprintf(L"%s PK_CAPS_MEMPACK", f ?L" |" :L""); f = 1; }
			if(pc & PK_CAPS_BY_CONTENT	) { wprintf(L"%s PK_CAPS_BY_CONTENT", f ?L" |" :L""); f = 1; }
			if(pc & PK_CAPS_SEARCHTEXT	) { wprintf(L"%s PK_CAPS_SEARCHTEXT", f ?L" |" :L""); f = 1; }
			if(pc & PK_CAPS_HIDE		) { wprintf(L"%s PK_CAPS_HIDE", f ?L" |" :L""); f = 1; }
			wprintf(L"\n");
		}
		goto stop;
	}

	if(!pOpenArchive || !pReadHeader || !pProcessFile || !pCloseArchive) {
		wprintf(L"%sError: There IS NOT mandatory function(s):", prog_prefix);
		if(!pOpenArchive ) wprintf(L" OpenArchiveW");
		if(!pReadHeader	 ) wprintf(L" ReadHeaderExW");
		if(!pProcessFile ) wprintf(L" ProcessFileW");
		if(!pCloseArchive) wprintf(L" CloseArchive");
		wprintf(L"\n");
		rc = ERR_NO_PROC;
		goto stop;
	}
	if(!pSetChangeVolProc || !pSetProcessDataProc) {
		wprintf(L"%sError: There IS NOT NOT-optional function(s):", prog_prefix);
		if(!pSetChangeVolProc  ) wprintf(L" SetChangeVolProcW");
		if(!pSetProcessDataProc) wprintf(L" SetProcessDataProcW");
		wprintf(L"\n");
		rc = ERR_NO_PROC;
		goto stop;
	}

	if(verbose) wprintf(L"%sOpening archive %s...\n", prog_prefix, arc_path);
	memset(&arcd, 0, sizeof(arcd));
	arcd.ArcName = arc_path;
	switch(open_todo) {
		case TODO_LIST:
		arcd.OpenMode = PK_OM_LIST;
		break;

		case TODO_TEST:
		case TODO_EXTRACT:
		arcd.OpenMode = PK_OM_EXTRACT;
		break;

		default:
		wprintf(L"%sUnknown TODO\n", prog_prefix);
		rc = ERR_OPEN_FAIL;
		goto stop;
	}
	if(!(arch = pOpenArchive(&arcd))) {
		if(verbose) wprintf(L"%sFailed: %s\n", prog_prefix, WCX_err_msg(arcd.OpenResult));
		else wprintf(L"%sFailed opening archive: %s\n", prog_prefix, WCX_err_msg(arcd.OpenResult));
		rc = ERR_OPEN_FAIL;
		goto stop;
	}
	if(verbose) wprintf(L"%sHandle returned by WCX: %X\n", prog_prefix, arch); fflush(stdout);

	if(pSetChangeVolProc) pSetChangeVolProc(arch, ChangeVol);
	if(pSetProcessDataProc) pSetProcessDataProc(arch, ProcessData);

	switch(open_todo) {
		case TODO_LIST:
		if(verbose) wprintf(L"%sList of files in %s\n", prog_prefix, arc_path);
		wprintf(L"%s Length    YYYY/MM/DD HH:MM:SS   Attr   Name\n", prog_prefix);
		wprintf(L"%s---------  ---------- --------  ------  ------------\n", prog_prefix);
		break;

		case TODO_TEST:
		if(verbose) wprintf(L"%sTesting files in %s\n", prog_prefix, arc_path);
		if(verbose) wprintf(L"%s--------\n", prog_prefix);
		break;

		case TODO_EXTRACT:
		if(verbose) wprintf(L"%sExtracting files from %s\n", prog_prefix, arc_path);
		if(verbose) wprintf(L"%s--------\n", prog_prefix);
		break;

		default:
		wprintf(L"%sUnknown TODO\n", prog_prefix);
		rc = ERR_OPEN_FAIL;
		goto stop;
	}

	/* main loop */
	while(!(rc = pReadHeader(arch, &hdrd))) {
		int		pfrc;
		switch(open_todo) {
			case TODO_LIST:
			wprintf(L"%s%9u  %04u/%02u/%02u %02u:%02u:%02u  %c%c%c%c%c%c	%s", prog_prefix, hdrd.UnpSize,
				((hdrd.FileTime >> 25 & 0x7f) + 1980), hdrd.FileTime >> 21 & 0x0f, hdrd.FileTime >> 16 & 0x1f,
				hdrd.FileTime >> 11 & 0x1f, hdrd.FileTime >>  5 & 0x3f, (hdrd.FileTime & 0x1F) * 2,
				hdrd.FileAttr & 0x01 ? L'r' : L'-',
				hdrd.FileAttr & 0x02 ? L'h' : L'-',
				hdrd.FileAttr & 0x04 ? L's' : L'-',
				hdrd.FileAttr & 0x08 ? L'v' : L'-',
				hdrd.FileAttr & 0x10 ? L'd' : L'-',
				hdrd.FileAttr & 0x20 ? L'a' : L'-',
				hdrd.FileName); fflush(stdout);
			pfrc = pProcessFile(arch, PK_SKIP, NULL, NULL);
			if(pfrc) {
				wprintf(L" - Error! %s\n", WCX_err_msg(pfrc));
				goto stop;
			}
			else wprintf(L"\n");
			fflush(stdout);
			break;

			case TODO_TEST:
			if(!(hdrd.FileAttr & 0x10)) {
				wprintf(L"%s%s", prog_prefix, hdrd.FileName);
				pfrc = pProcessFile(arch, PK_TEST, NULL, NULL);
				if(pfrc) {
					wprintf(L"Error! %s\n", WCX_err_msg(pfrc));
					goto stop;
				}
				else wprintf(L" Ok.\n");
				fflush(stdout);
			}
			else {
				pfrc = pProcessFile(arch, PK_SKIP, NULL, NULL);
			}
			break;

			case TODO_EXTRACT:
			if(!(hdrd.FileAttr & 0x10)) {
				wchar_t	outpath[MAX_PATH];

				wsprintf(outpath,L"%s%s", dir_path, hdrd.FileName);
				check_fndir(outpath);
				wprintf(L"%s%s", prog_prefix, hdrd.FileName);
				pfrc = pProcessFile(arch, PK_EXTRACT, NULL, outpath);
				if(pfrc) {
					wprintf(L"\nError! %s (%s)\n", WCX_err_msg(pfrc), outpath);
					goto stop;
				}
				else wprintf(L" Ok.\n");
				fflush(stdout);
			}
			else {
				pfrc = pProcessFile(arch, PK_SKIP, NULL, NULL);
			}
			break;

			default:
			wprintf(L"%sUnknown TODO\n", prog_prefix); fflush(stdout);
			rc = ERR_OPEN_FAIL;
			goto stop;
		}
	}
	if(verbose) wprintf(L"%s--------\n", prog_prefix);
	if(verbose) wprintf(L"%s%s\n", prog_prefix, WCX_err_msg(rc)); fflush(stdout);
	if(rc == E_END_ARCHIVE) rc = 0;

	/* cleanup */
stop:
	if(arch) {
		if(verbose) {
			wprintf(L"%sClosing archive...", prog_prefix);
			fflush(stdout);
		}
		if(pCloseArchive(arch)) {
			if(verbose) wprintf(L"Failed: %s\n", rc);
			else wprintf(L"Failed closing archive: %s\n", rc);
			fflush(stdout);
			rc = ERR_CLOSE_FAIL;
		} else {
			if(verbose) {
				wprintf(L" Ok.\n");
				fflush(stdout);
			}
			arch = NULL;
		}
	}

	if(hwcx) {
		if(verbose) {
			wprintf(L"%sUnloading plugin...", prog_prefix);
			fflush(stdout);
		}
		if(!FreeLibrary(hwcx)) {
			if(verbose) wprintf(L"Failed.\n");
			else wprintf(L"Failed unloading plugin.\n");
			fflush(stdout);
			rc = ERR_NO_WCX;
		}
		else {
			if(verbose) {
				wprintf(L" Ok.\n");
				fflush(stdout);
			}
			hwcx = NULL;
		}
	}

//	  wprintf(L"Press Enter.L");
//	  _getws(buf);

	if(verbose) {
		wprintf(L"%sERRORLEVEL: %d\n", prog_prefix, rc);
		fflush(stdout);
	}
	return rc;
}
