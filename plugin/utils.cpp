#include "7zip.h"


LPCTSTR BIN(LPCTSTR fileName)
{
	static TCHAR szModule1[_MAX_PATH];
	static TCHAR szModule2[_MAX_PATH];
	TCHAR* pszFileName;

	::GetModuleFileName(g_hInstance, szModule1, _MAX_PATH);
	::GetFullPathName(szModule1, _MAX_PATH, szModule2, &pszFileName);
	lstrcpy(pszFileName, fileName);
	return szModule2;
}

HRESULT CopyBlock(ISequentialInStream *inStream, ISequentialOutStream *outStream)
{
	CMyComPtr<ICompressCoder> copyCoder = new NCompress::CCopyCoder;
	return copyCoder->Code(inStream, outStream, NULL, NULL, NULL);
}

HRESULT CopyBlock(ISequentialInStream *inStream, ISequentialOutStream *outStream, UInt64 size)
{
	CMyComPtr<ICompressCoder> copyCoder = new NCompress::CCopyCoder;
	return copyCoder->Code(inStream, outStream, NULL, &size, NULL);
}

extern "C" {
////////////////////////////////////////////////////////////////////////////////////////////
// Constants

const UInt32 kNoSolidBlockSize = 0;
const UInt32 kSolidBlockSize = 64;

////////////////////////////////////////////////////////////////////////////////////////////
// Variables

static const CMethodId k_LZMA    = 0x030101;
static const CMethodId k_LZMA2    = 0x21;
static const CMethodId k_PPMd    = 0x030401;
static const CMethodId k_Deflate = 0x040108;
static const CMethodId k_Deflate64 = 0x040109;
static const CMethodId k_Copy    = 0x0;
static const CMethodId k_BZip2   = 0x040202;

COMPMETHOD g_compMethods[] = {
	{L"LZMA", k_LZMA, kLZMA},
	{L"LZMA2", k_LZMA2, kLZMA2},
	{L"PPMd", k_PPMd, kPPMd},
	{L"BZip2", k_BZip2, kBZip2},
	{L"Deflate", k_Deflate, kDeflate},
	{L"Deflate64", k_Deflate64, kDeflate64},
	{NULL, k_Copy, kCopy}
};

COMPLEVEL g_compLevels[] = {
	{L"Store", 0x02000D81},
	{L"Fastest", 0x02000D85},
	{ 0, 0 },
	{L"Fast", 0x02000D84},
	{ 0, 0 },
	{L"Normal", 0x02000D82},
	{ 0, 0 },
	{L"Maximum", 0x02000D83},
	{ 0, 0 },
	{L"Ultra", 0x02000D86},
	{ 0, 0 },{ 0, 0 }
};

StringVector g_knownSFXModules;

UInt64 GetMaxRamSizeForProgram()
{
    UInt64 physSize = NWindows::NSystem::GetRamSize();
    const UInt64 kMinSysSize = (1 << 24);
    if (physSize <= kMinSysSize)
        physSize = 0;
    else
        physSize -= kMinSysSize;
    const UInt64 kMinUseSize = (1 << 25);
    if (physSize < kMinUseSize)
        physSize = kMinUseSize;
    return physSize;
}

UInt64 GetMemoryUsage2 (HWND hwndDlg, UInt32 dictionary, UInt64 &decompressMemory)
{
    decompressMemory = UInt64 (Int64 (-1));
    UInt32 level = GetLevel2 (hwndDlg);
    if (level == 0) {
        decompressMemory = (1 << 20);
        return decompressMemory;
    }
    UInt64 size = 0;

    UInt32 methodID = GetMethodID (hwndDlg);

    if (methodID != kDeflate && methodID != kDeflate64 && level >= 9)
        size += (12 << 20) * 2 + (5 << 20);

    UInt32 numThreads = GetNumThreads (hwndDlg);
  
    switch (methodID)
    {
		case kLZMA2:
        case kLZMA:
        {
            UInt32 hs = dictionary - 1;
            hs |= (hs >> 1);
            hs |= (hs >> 2);
            hs |= (hs >> 4);
            hs |= (hs >> 8);
            hs >>= 1;
            hs |= 0xFFFF;
            if (hs > (1 << 24))
                hs >>= 1;
            hs++;
            UInt64 size1 = (UInt64)hs * 4;
            size1 += (UInt64)dictionary * 11 / 2;
            if (level >= 5)
                size1 += (UInt64)dictionary * 4;
            size1 += (2 << 20);

            UInt32 numThreads1 = 1;
            if (numThreads > 1 && level >= 5)
            {
                size1 += (2 << 20) + (4 << 20);
                numThreads1 = 2;
            }
            size += size1 * numThreads / numThreads1;

            decompressMemory = dictionary + (2 << 20);
            return size;
        }
        case kPPMd:
        {
            decompressMemory = dictionary + (2 << 20);
            return size + decompressMemory;
        }
        case kDeflate:
        case kDeflate64:
        {
            UInt32 order = GetOrder (hwndDlg);
            if (order == UInt32(-1))
                order = 32;
            if (level >= 7)
                size += (1 << 20);
            size += 3 << 20;
            decompressMemory = (2 << 20);
            return size;
        }
        case kBZip2:
        {
            decompressMemory = (7 << 20);
            UInt64 memForOneThread = (10 << 20);
            return size + memForOneThread * numThreads;
        }
    }
    return UInt64(Int64(-1));
}

UInt64 GetMemoryUsage(HWND hwndDlg, UInt64 &decompressMemory)
{
    return GetMemoryUsage2 (hwndDlg, GetDictionary (hwndDlg), decompressMemory);
}

void UpdateSettings(CMethodId methodID, int level, COMPSETTINGS *cs)
{
	if (methodID == k_LZMA)
	{
		cs->method = k_LZMA;
		cs->methodID = kLZMA;
	}

	// Dictionary
	switch (methodID)
	{
		case k_LZMA2:
			cs->method = k_LZMA2;
			cs->methodID = kLZMA2;
		case k_LZMA:
			if(level >= 9)		cs->dict = (1 << 26);
			else if(level >= 7)	cs->dict = (1 << 25);
			else if(level >= 5)	cs->dict = (1 << 24);
			else if(level >= 3) cs->dict = (1 << 20);
			else                cs->dict = (1 << 16);

			if (level >= 7)
				cs->word = 64;
			else
				cs->word = 32;
			
			break;
		case k_PPMd:
			cs->method = k_PPMd;
			cs->methodID = kPPMd;
			if (level >= 9)      cs->dict = (192 << 20);
			else if (level >= 7) cs->dict = ( 64 << 20);
			else if (level >= 5) cs->dict = ( 16 << 20);
			else                 cs->dict = (  4 << 20);

			if (level >= 9)      cs->word = 32;
			else if (level >= 7) cs->word = 16;
			else if (level >= 5) cs->word = 6;
			else                 cs->word = 4;
			break;
	}
}


CMethodId FindSFXCompatibleMethodId(NArchive::N7z::CArchiveDatabaseEx db, CMethodId method)
{
	bool LZMAused = false;
	bool LZMA2used = false;
	bool PPMdused = false;

	for (int i = 0; i < db.Folders.Size(); i++)
	{
		for (int j = 0; j < db.Folders[i].Coders.Size(); j++)
		{
			if (db.Folders[i].Coders[j].MethodID == method)
			{
				return method;
			}

			CMethodId q = db.Folders[i].Coders[j].MethodID;

			switch (db.Folders[i].Coders[j].MethodID)
			{
				case k_LZMA : LZMAused  = true; break;
				case k_LZMA2: LZMA2used = true; break;
				case k_PPMd : PPMdused  = true; break;
			}
		}
	}

	if (LZMA2used)
		return k_LZMA2;
	else if (LZMAused)
		return k_LZMA;
	else if (PPMdused)
		return k_PPMd;

	if ((db.Folders.Size() > 0) && (db.Folders[0].Coders.Size() > 0))
		return db.Folders[0].Coders[0].MethodID;
	else
		return method;
}


int FindSFXModuleSize(IInStream *inFS)
{
	#define MAXSFXSIZE 524288

	BYTE *buff = new BYTE[MAXSFXSIZE];
	int SFXSize = 0;
	UInt32 actualSize = 0;
	inFS->Seek (0, FILE_BEGIN, NULL);
	inFS->Read(buff, MAXSFXSIZE, &actualSize);

	UInt32 PeHdrOfs = *((UInt32*)(buff + 0x3C));
	UInt16 NumOfObj = *((UInt16*)(buff + PeHdrOfs + 6));
	UInt16 NtHdrSize = *((UInt16*)(buff + PeHdrOfs + 0x14)) + 0x18;
	UInt32 ObjTblOfs = PeHdrOfs + NtHdrSize;
	UInt32 ObjTblEntSize = 0x28;

	UInt32 PhisSize = 0;
	UInt32 PhisOfs = 0;
	UInt32 max = 0;

	for (UInt32 i = 0; i < NumOfObj; i++)
	{
		PhisSize = *((UInt32*)(buff + ObjTblOfs + i * ObjTblEntSize + 0x10));
		PhisOfs  = *((UInt32*)(buff + ObjTblOfs + i * ObjTblEntSize + 0x14));
		if ((PhisSize + PhisOfs) > max)
			max = PhisSize + PhisOfs;
	}

	for (UInt32 i = max; i < actualSize; i++)
	{
		if ((buff[i] == '7') && (buff[i+1] == 'z'))
		{
			if ((buff[i+2] == 0xBC) && (buff[i+3] == 0xAF) &&
				(buff[i+4] == 0x27) && (buff[i+5] == 0x1C))
			{
				SFXSize = i;
				break;
			}
		}
	}

	delete []buff;
	return SFXSize;
}

void FindSFXModules ()
{
    NWindows::NFile::NFind::CFileInfo fileInfo;
    NWindows::NFile::NFind::CFindFile finder;

    g_knownSFXModules.clear ();

    bool ok = finder.FindFirst (BIN (L"*.sfx"), fileInfo);
    while (ok) {
        g_knownSFXModules.push_back (fileInfo.Name.GetBuffer (2));
        ok = finder.FindNext (fileInfo);
    }
}

// 7zip password size is 64
char szEncryptStr64[PASSWORD_SIZE*4 + 1];
char UShortToOneChar(unsigned short V)
{
	V = V & 0x000F;
	if(V>=10) return 'A'+V-10;
	return '0'+V;
}
unsigned short OneCharToUShort(char V)
{
	if(V>='A')return V-'A'+10;
	return V-'0';
}
void UShortToChar(char *T,unsigned short V)
{
	T[0]=UShortToOneChar(V);
	T[1]=UShortToOneChar(V>>4);
	T[2]=UShortToOneChar(V>>8);
	T[3]=UShortToOneChar(V>>12);
}
void CharToUShort(char *T,unsigned short *V)
{
	*V =  OneCharToUShort(T[0])
		+ (OneCharToUShort(T[1])<<4)
		+ (OneCharToUShort(T[2])<<8)
		+ (OneCharToUShort(T[3])<<12);
}
void myEncryptStr64(TCHAR *Src)
{
	bool IsEnd=false;
	unsigned short Base=0x5678;
	unsigned short Inc=0x1234;
	unsigned short V;
	wchar_t T;
	for(int i=0;i<PASSWORD_SIZE;i++)
	{
		if(IsEnd)
			T = 0;
		else
		{
			T = Src[i];
			if(T==0) IsEnd = true;
		}
		V = T;
		V += Base;
		Base += Inc;
		UShortToChar(szEncryptStr64+i*4,V);		
	}
	szEncryptStr64[PASSWORD_SIZE*4]=0;
}
void myDecryptStr64(TCHAR *Dst)
{
	unsigned short Base=0x5678;
	unsigned short Inc=0x1234;
	unsigned short V;
	wchar_t T;
	for(int i=0;i<PASSWORD_SIZE;i++)
	{
		CharToUShort(szEncryptStr64+i*4,&V);
		V -= Base;
		Base += Inc;
		T = V;
		Dst[i] = T;
	}
}

BOOL WriteSettings(COMPSETTINGS *cs)
{
    TCHAR szVal[64];
	LPCTSTR szFile = BIN (L"7zip.ini");

	_ltow (cs->methodID, szVal, 10);
    WritePrivateProfileString (L"settings", L"CompMethod", szVal, szFile);

	_ltow (cs->level, szVal, 10);
    WritePrivateProfileString (L"settings", L"CompLevel", szVal, szFile);

	_ltow (cs->dict, szVal, 10);
    WritePrivateProfileString (L"settings", L"DictSize", szVal, szFile);

	_ltow (cs->word, szVal, 10);
    WritePrivateProfileString (L"settings", L"WordSize", szVal, szFile);

    _ltow (cs->solidBlock, szVal, 10);
    WritePrivateProfileString (L"settings", L"SolidBlock", szVal, szFile);

	_ltow (cs->flags, szVal, 10);
    WritePrivateProfileString (L"settings", L"Flags", szVal, szFile);

    _ltow (cs->numThreads > 32 ? 1 : cs->numThreads, szVal, 10);
    WritePrivateProfileString (L"settings", L"NumThreads", szVal, szFile);

    _ltow (cs->deflateNumPasses, szVal, 10);
    WritePrivateProfileString (L"settings", L"DeflateNumPasses", szVal, szFile);

	if(wcslen(cs->password)==0) // no password
	{
		WritePrivateProfileString (L"settings", L"Password", cs->password, szFile);
	}
	else	// <2011-06-28> Encrypt password before save to ini
	{
		TCHAR tzEncryptStr64[PASSWORD_SIZE*4 + 1];
		myEncryptStr64(cs->password);
		mbstowcs(tzEncryptStr64, szEncryptStr64, PASSWORD_SIZE*4 + 1);
		WritePrivateProfileString (L"settings", L"Password", tzEncryptStr64, szFile);
	}

    WritePrivateProfileString (L"settings", L"SFXModule", cs->SFXModule, szFile);
    WritePrivateProfileString (L"settings", L"SFXIconFile", cs->SFXIconFile, szFile);
    _ltow (cs->SFXIconIndex, szVal, 10);
    WritePrivateProfileString (L"settings", L"SFXIconIndex", szVal, szFile);

	return TRUE;
}

BOOL ReadSettings (COMPSETTINGS *cs)
{
	LPCTSTR szFile = BIN (L"7zip.ini");

	cs->methodID = GetPrivateProfileInt (L"settings", L"CompMethod", kLZMA, szFile);
	for(int i = 0; g_compMethods[i].name; i++) 
        if (g_compMethods[i].num == cs->methodID)
            cs->method = g_compMethods[i].id;

	cs->level = GetPrivateProfileInt (L"settings", L"CompLevel", 5, szFile);

	cs->dict = GetPrivateProfileInt (L"settings", L"DictSize", (2 << 20), szFile);

	cs->word = GetPrivateProfileInt (L"settings", L"WordSize", 32, szFile);

    cs->solidBlock = GetPrivateProfileInt (L"settings", L"SolidBlock", kNoSolidBlockSize, szFile);

	cs->flags = GetPrivateProfileInt (L"settings", L"Flags", DEFAULT_CONFIG_FLAGS, szFile);
	cs->flags = cs->flags | FLAG_SAVE_ATTR_MODIFY_TIME; // <2011-04-13> Always save Modify Time

	// <2011-06-28> Decrypt password after read from ini
	TCHAR tzEncryptStr64[PASSWORD_SIZE*4 + 1];
	GetPrivateProfileString (L"settings", L"Password", L"", tzEncryptStr64, sizeof(tzEncryptStr64), szFile);
	int encryptPasswordLen=wcslen(tzEncryptStr64);
	if(encryptPasswordLen==0) // no password
	{
		wcscpy(cs->password,L"");
	}
	else if(encryptPasswordLen<=PASSWORD_SIZE) // old no encrypt password
	{
		wcscpy(cs->password,tzEncryptStr64);
	}
	else if(encryptPasswordLen==PASSWORD_SIZE*4)
	{
		wcstombs(szEncryptStr64, tzEncryptStr64, PASSWORD_SIZE*4+1);
		myDecryptStr64(cs->password);
	}
	else // password data error, reset password
	{
		DebugString(L"Password Error, Reset Password to Empty.");
		wcscpy(cs->password,L"");
	}
	// GetPrivateProfileString (L"settings", L"Password", L"", cs->password, PASSWORD_SIZE, szFile);
	if(!(cs->flags & FLAG_USE_NEXT))
	    WritePrivateProfileString (L"settings", L"Password", L"", szFile);

    cs->deflateNumPasses = GetPrivateProfileInt (L"settings", L"DeflateNumPasses", 1, szFile);

    cs->numThreads = GetPrivateProfileInt (L"settings", L"NumThreads", 1, szFile);
    if (cs->numThreads > 32)
        cs->numThreads = 1;

    GetPrivateProfileString (L"settings", L"SFXModule", L"", cs->SFXModule, SFX_MODULE_SIZE, szFile);
    GetPrivateProfileString (L"settings", L"SFXIconFile", L"", cs->SFXIconFile, SFX_ICON_FILE_SIZE, szFile);
    cs->SFXIconIndex = GetPrivateProfileInt (L"settings", L"SFXIconIndex", 0, szFile);

	return TRUE;
}

void ConvertUInt64ToString(UInt64 value, TCHAR *s, UInt32 base)
{
  if (base < 2 || base > 36)
  {
    *s = L'\0';
    return;
  }
  TCHAR temp[72];
  int pos = 0;
  do
  {
    int delta = (int)(value % base);
    temp[pos++] = (TCHAR)((delta < 10) ? (L'0' + delta) : (L'a' + (delta - 10)));
    value /= base;
  }
  while (value != 0);
  do
    *s++ = temp[--pos];
  while (pos > 0);
  *s = L'\0';
}

////////////////////////////////////////////////////////////////////////////////
// Language settings

// Default language (group)
TCHAR szLang[64] = L"lang";
UINT cp = CP_ACP;

#ifndef _UNICODE
char *TranslateDefA(int id, LPWSTR wDef)
{
	static char szTrans[255], szKey[32];
    _ltow(id, szKey, 10);
	if(GetPrivateProfileString(szLang, szKey, NULL, szTrans,
		sizeof(szTrans), BIN("7zip.lng"))
		return szTrans;
	if(wDef)
	{
		//WideCharToMultiByte(cp, 0, wDef, (int)wcslen(wDef), szTrans, sizeof(szTrans), NULL, NULL);
        wcstombs_s (NULL, szTrans, sizeof (szTrans), wDef, (int)wcslen(wDef));
		return szTrans;
	}
	return NULL;
}
#endif

LPWSTR TranslateDefW(int id, LPWSTR wDef)
{
	static TCHAR szTrans[255], szKey[32];
#ifndef _UNICODE
	static WCHAR wTrans[255];
	ZeroMemory(wTrans, sizeof(wTrans));
#endif
	_ltow(id, szKey, 10);
#ifdef _UNICODE
	if(GetPrivateProfileString(szLang, szKey, NULL, szTrans,
		sizeof(szTrans), BIN(L"7zip.lng")))
		return szTrans;
#else
	if(GetPrivateProfileString(szLang, szKey, NULL, szTrans,
		sizeof(szTrans), BIN("7zip.lng")
		&& MultiByteToWideChar(cp, 0, szTrans, (int)strlen(szTrans), wTrans, sizeof(wTrans)))
		return wTrans;
#endif
	return wDef;
}

#ifdef _UNICODE
#define TranslateDef TranslateDefW
#else
#define TranslateDef TranslateDefA
#endif

#ifndef _UNICODE
inline char *TranslateA(int id)
{
	DebugString("Translate(%d) %s", id, TranslateDefA(id, NULL));
	return TranslateDefA(id, NULL);
}
#endif

inline LPWSTR TranslateW(int id)
{
	DebugString(L"Translate(%d) %s", id, TranslateDef(id, NULL));
	return TranslateDefW(id, NULL);
}

static BOOL CALLBACK TranslateDialogEnumProc(HWND hwnd, LPARAM lParam)
{
	int id = (int)lParam;
	LPWSTR wTrans;
#ifdef _UNICODE
	if(wTrans = TranslateW(GetDlgCtrlID(hwnd) + id))
		SetWindowText(hwnd, wTrans);
#else
	char *szTrans;
	if(IsWindowUnicode(hwnd) && (wTrans = TranslateW(GetDlgCtrlID(hwnd) + id)))
		SetWindowTextW(hwnd, wTrans);
	else if(szTrans = TranslateA(GetDlgCtrlID(hwnd) + id))
		SetWindowTextA(hwnd, szTrans);
#endif
	return TRUE;
}

BOOL TranslateDialog(HWND hwndDlg, int id)
{
	cp = GetPrivateProfileInt(szLang, L"codepage", CP_ACP, BIN(L"7zip.lng"));
	LPWSTR wTrans;
#ifdef _UNICODE
	if(wTrans = TranslateW(id))
		SetWindowText(hwndDlg, wTrans);
#else
	char *szTrans;
	if(IsWindowUnicode(hwndDlg) && (wTrans = TranslateW(id)))
		SetWindowTextW(hwndDlg, wTrans);
	else if(szTrans = TranslateA(id))
		SetWindowTextA(hwndDlg, szTrans);
#endif
	EnumChildWindows(hwndDlg, TranslateDialogEnumProc, (LPARAM)id);
	return TRUE;
}
}
