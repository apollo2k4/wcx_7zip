////////////////////////////////////////////////////////////////////////////////
// 7zip Plugin for Total Commander
// Copyright (c) 2004-2006 Adam Strzelecki <ono@java.pl>
//			 (c) 2009 Dmitry Efimenko <tbbcmrjytv@gmail.com>
//			 (c) 2009 Alexandr Popov <Sax0n0xaS@gmail.com>
//			 (c) 2010 Cristian Adam <cristian.adam@gmail.com>
//			 (c) 2011 dllee <dllee.tw@gmail.com>
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Lesser Public License
// as published by the Free Software Foundation; either version 2.1
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
////////////////////////////////////////////////////////////////////////////////

#define MAX_PASSWORDS	8

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <stdio.h>

#include <initguid.h>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <Winnetwk.h>
#include <time.h>
#include <vector>
#include <Softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

// 7-zip version
#include "7zip/MyVersion.h"

// Method IDs
#include "7zip/Common/MethodId.h"

// Property definition
#define DECLARE_PROPERTY(property) CProp property
#define ADD_PROPERTY(method, property, id, value) \
			property.Id = id; \
			property.Value = value; \
			method.Props.Add(property)


typedef std::vector<std::wstring> StringVector;

// Includes
#include "Common/StringConvert.h"
#include "7zip/Common/FileStreams.h"
#include "7zip/Archive/Common/ItemNameUtils.h"
#include "7zip/Archive/Common/OutStreamWithCRC.h"
#include "7zip/Archive/7z/7zIn.h"
#include "7zip/Archive/7z/7zItem.h"
#include "7zip/Archive/7z/7zDecode.h"
#include "7zip/Archive/7z/7zUpdate.h"
#include "7zip/Archive/7z/7zCompressionMode.h"
#include "7zip/Compress/CopyCoder.h"

#include "Windows/FileDir.h"
#include "Windows/FileFind.h"
#include "Windows/System.h"

#include "wcxhead.h"
#include "PickIconDialog.h"
#include "ICO.h"
#include "resource.h"

namespace wildcard_helper
{
	bool test(UString &str, UString &pattern);
};

HRESULT CopyBlock(ISequentialInStream *inStream, ISequentialOutStream *outStream);
HRESULT CopyBlock(ISequentialInStream *inStream, ISequentialOutStream *outStream, UInt64 size);

void ReplaceIconResourceInPE (LPCTSTR fileName, LPCTSTR iconFile, DWORD iconIndex);

extern "C" {

typedef struct
{
	HFONT hBoldFont;
	LPTSTR szPassword;

    int bDialogExpanded;
    SIZE expandedSize, collapsedSize;

    TCHAR SFXIconFile[MAX_PATH]; 
    DWORD SFXIconIndex;
} DIALOGDATA;

typedef struct
{
	LPTSTR name;
	CMethodId id;
	int num;
} COMPMETHOD;

typedef struct
{
	LPWSTR name;
	int id;
	int langID;
} COMPLEVEL;

enum EMethodID
{
	kCopy,
	kLZMA,
	kLZMA2,
	kPPMd,
	kBZip2,
	kDeflate,
	kDeflate64
};

extern HINSTANCE g_hInstance;
extern COMPMETHOD g_compMethods[];
extern COMPLEVEL g_compLevels[];

extern StringVector g_knownSFXModules;

extern const UInt32 kNoSolidBlockSize;
extern const UInt32 kSolidBlockSize;

#define FLAG_SHOW_PASSWORD		1
#define FLAG_USE_NEXT			2
#define FLAG_ENCRYPT_HEADER 	4

#define FLAG_SAVE_ATTR_READONLY  8
#define FLAG_SAVE_ATTR_HIDDEN   16
#define FLAG_SAVE_ATTR_SYSTEM   32
#define FLAG_SAVE_ATTR_ARCHIVE  64

#define FLAG_SAVE_ATTR_MODIFY_TIME 128
#define FLAG_SAVE_ATTR_CREATE_TIME 256
#define FLAG_SAVE_ATTR_ACCESS_TIME 512

#define FLAG_DIALOG_EXPANDED    1024

#define FLAG_SET_7Z_DATE_TO_NEWEST 2048
#define FLAG_REORDER_DIRECTORY_LIST 4096

#define DEFAULT_FILE_TIMES FLAG_SAVE_ATTR_MODIFY_TIME
#define DEFAULT_FILE_ATTRIBUTES (FLAG_SAVE_ATTR_READONLY|FLAG_SAVE_ATTR_HIDDEN|FLAG_SAVE_ATTR_SYSTEM|FLAG_SAVE_ATTR_ARCHIVE)

#define DEFAULT_CONFIG_FLAGS (FLAG_ENCRYPT_HEADER|DEFAULT_FILE_TIMES|DEFAULT_FILE_ATTRIBUTES)

#define PASSWORD_SIZE			64
#define SFX_MODULE_SIZE         64
#define SFX_ICON_FILE_SIZE      1024
typedef struct
{
	int methodID;
	CMethodId method;
	int level;
	UInt32 dict;
	UInt32 word;
    UInt32 solidBlock;
	DWORD flags;
	TCHAR password[PASSWORD_SIZE + 1];
    UInt32 numThreads;
    UInt32 deflateNumPasses;
    TCHAR SFXModule[SFX_MODULE_SIZE + 1];
    TCHAR SFXIconFile[MAX_PATH]; 
    DWORD SFXIconIndex;
} COMPSETTINGS;

LPCTSTR BIN(LPCTSTR fileName);

BOOL ReadSettings(COMPSETTINGS *cs);
BOOL WriteSettings(COMPSETTINGS *cs);

LPCTSTR GetPassword();

void UpdateSettings(CMethodId methodID, int level, COMPSETTINGS *cs);
void FindSFXModules ();
int FindSFXModuleSize(IInStream *);
CMethodId FindSFXCompatibleMethodId(NArchive::N7z::CArchiveDatabaseEx db, CMethodId method);

void ConvertUInt64ToString(UInt64 value, TCHAR *s, UInt32 base);

UInt64 GetMaxRamSizeForProgram ();
UInt64 GetMemoryUsage (HWND hwndDlg, UInt64 &decompressMemory);
UInt64 GetMemoryUsage2 (HWND hwndDlg, UInt32 dictionary, UInt64 &decompressMemory);

#ifndef _UNICODE
char *TranslateDefA(int id, LPWSTR wDef);
#endif
LPWSTR TranslateDefW(int id, LPWSTR wDef);
#ifdef _UNICODE
#define TranslateDef TranslateDefW
#else
#define TranslateDef TranslateDefA
#endif

BOOL TranslateDialog(HWND hwndDlg, int id);

UInt32 GetLevel2(HWND hwndDlg);
int GetMethodID(HWND hwndDlg);
UInt32 GetNumThreads(HWND hwndDlg);
UInt32 GetOrder(HWND hwndDlg);
UInt32 GetDictionary(HWND hwndDlg);

#if (defined(_DEBUG) || defined(__DEBUG__))
static wchar_t _debugStr[255];
#define STRING_AUX( x ) #x
#define STRING(x) STRING_AUX(x)
#define DebugString(x,...) _snwprintf(_debugStr,255,__FILEW__ L":" _STR2WSTR(STRING( __LINE__ )) L" " x L"\n",##__VA_ARGS__),OutputDebugString(_debugStr)
#else
#define DebugString
#endif
}

