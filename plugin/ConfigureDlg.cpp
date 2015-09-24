////////////////////////////////////////////////////////////////////////////////
// 7zip Plugin for Total Commander
// Copyright (c) 2004-2006 Adam Strzelecki <ono@java.pl>
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

#include "7zip.h"

extern LPWSTR wAuthors;

#define IDC_NONE_SOLID_TEXT 100
#define IDC_SOLID_TEXT 101
#define IDC_SFX_NOT_FOUND_TEXT 102

extern "C" {
int ComboboxAdd (HWND hwndDlg, int id, UInt32 data)
{
	TCHAR s[40];
	int index;

	_ltow (data, s, 10);
	index = (int)SendDlgItemMessage(hwndDlg, id, CB_ADDSTRING, 0, (LPARAM)s);
	SendDlgItemMessage(hwndDlg, id, CB_SETITEMDATA, (WPARAM)index, data);
	return index;
}

////////////////////////////////////////////////////////////////////////////////
// Misc functions

int AddDictionarySizeL(HWND hwndDlg, UInt32 size, bool kilo, bool mega)
{
	UInt32 sizePrint = size;
	if(kilo)
		sizePrint >>= 10;
	else if(mega)
		sizePrint >>= 20;
	TCHAR s[40];
	_ltow(sizePrint, s, 10);
	if(kilo)
		lstrcat(s, L" K");
	else if(mega)
		lstrcat(s, L" M");
	else
		lstrcat(s, L" ");
	lstrcat(s, L"B");

	int index = (int)SendDlgItemMessage(hwndDlg, IDC_DICT_SIZE, CB_ADDSTRING, 0, (LPARAM)s);
	SendDlgItemMessage(hwndDlg, IDC_DICT_SIZE, CB_SETITEMDATA, (WPARAM)index, size);
	return index;
}
int AddDictionarySize(HWND hwndDlg, UInt32 size)
{
	if(size > 0) {
		if((size & 0xFFFFF) == 0)
			return AddDictionarySizeL(hwndDlg, size, false, true);
		if((size & 0x3FF) == 0)
			return AddDictionarySizeL(hwndDlg, size, true, false);
	}
	return AddDictionarySizeL(hwndDlg, size, false, false);
}

int AddPassesCount(HWND hwndDlg, UInt32 listItem, UInt32 itemData)
{
	TCHAR s[40];
	_ltow(listItem, s, 10);
	int index = (int)SendDlgItemMessage(hwndDlg, IDC_DICT_SIZE, CB_ADDSTRING, 0, (LPARAM)s);
	SendDlgItemMessage(hwndDlg, IDC_DICT_SIZE, CB_SETITEMDATA, (WPARAM)index, (LPARAM)itemData);
	return index;
}
int GetMethodID(HWND hwndDlg)
{
	int index = (int)SendDlgItemMessage(hwndDlg, IDC_COMP_METHOD, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
	if(index == -1) return g_compMethods[0].num;

	return g_compMethods[index].num;
}
UInt32 GetLevel2(HWND hwndDlg)
{
	int index = (int)SendDlgItemMessage(hwndDlg, IDC_COMP_LEVEL, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
	if(index == -1) return 5;
	return (UInt32)SendDlgItemMessage(hwndDlg, IDC_COMP_LEVEL, CB_GETITEMDATA, (WPARAM)index, (LPARAM)0);
}
UInt32 GetNumThreads(HWND hwndDlg)
{
	int index = (int)SendDlgItemMessage(hwndDlg, IDC_NUM_THREADS, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
	if (index < 0) return 1;
	return (UInt32)SendDlgItemMessage(hwndDlg, IDC_NUM_THREADS, CB_GETITEMDATA, (WPARAM)index, (LPARAM)0);
}
UInt32 GetOrder(HWND hwndDlg)
{
	int index = (int)SendDlgItemMessage(hwndDlg, IDC_WORD_SIZE, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
	if (index < 0) return 32;
	return (UInt32)SendDlgItemMessage(hwndDlg, IDC_WORD_SIZE, CB_GETITEMDATA, (WPARAM)index, (LPARAM)0);
}
UInt32 GetDictionary(HWND hwndDlg)
{
	int index = (int)SendDlgItemMessage(hwndDlg, IDC_DICT_SIZE, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
	if (index < 0) return UInt32(-1);
	return (UInt32)SendDlgItemMessage(hwndDlg, IDC_DICT_SIZE, CB_GETITEMDATA, (WPARAM)index, (LPARAM)0);
}
void SetNearestSelectComboBox(HWND hwndDlg, int id, UInt32 value)
{
	for(int i = (int)SendDlgItemMessage(hwndDlg, id, CB_GETCOUNT, (WPARAM)0, (LPARAM)0) - 1; i >= 0; i--)
		if((UInt32)SendDlgItemMessage(hwndDlg, id, CB_GETITEMDATA, (WPARAM)i, (LPARAM)0) <= value)
		{
			SendDlgItemMessage(hwndDlg, id, CB_SETCURSEL, (WPARAM)i, (LPARAM)0);
			return;
		}
		if(SendDlgItemMessage(hwndDlg, id, CB_GETCOUNT, (WPARAM)0, (LPARAM)0) > 0)
			SendDlgItemMessage(hwndDlg, id, CB_SETCURSEL, (WPARAM)0, (LPARAM)0);
}

void FillSolidBlockSize(HWND hwndDlg)
{
	SendDlgItemMessage(hwndDlg, IDC_SOLID_BLOCK_SIZE, CB_RESETCONTENT, (WPARAM)0, (LPARAM)0);

	int index = (int)SendDlgItemMessage (hwndDlg, IDC_SOLID_BLOCK_SIZE, CB_ADDSTRING, 0, 
		(LPARAM)TranslateDef (IDC_NONE_SOLID_TEXT, L"Non-solid"));
	SendDlgItemMessage (hwndDlg, IDC_SOLID_BLOCK_SIZE, CB_SETITEMDATA, (WPARAM)index, (LPARAM)kNoSolidBlockSize);    

	for (int i = 20; i <= 36; i++) {
		TCHAR s[40];
		ConvertUInt64ToString (1 << (i % 10), s, 10);
		if (i < 30) lstrcat (s, L" MB");
		else        lstrcat (s, L" GB");

		index = (int)SendDlgItemMessage (hwndDlg, IDC_SOLID_BLOCK_SIZE, CB_ADDSTRING, (WPARAM)0, (LPARAM)s);
		SendDlgItemMessage (hwndDlg, IDC_SOLID_BLOCK_SIZE, CB_SETITEMDATA, (WPARAM)index, (LPARAM)(UInt32)i);    
	}

	index = (int)SendDlgItemMessage (hwndDlg, IDC_SOLID_BLOCK_SIZE, CB_ADDSTRING, 0, 
		(LPARAM)TranslateDef (IDC_SOLID_TEXT, L"solid"));
	SendDlgItemMessage (hwndDlg, IDC_SOLID_BLOCK_SIZE, CB_SETITEMDATA, (WPARAM)index, (LPARAM)kSolidBlockSize);

	SendDlgItemMessage (hwndDlg, IDC_SOLID_BLOCK_SIZE, CB_SETCURSEL, (WPARAM)0, (LPARAM)0);
}

void UpdateCombos(HWND hwndDlg)
{
	int methodID = GetMethodID(hwndDlg);
	int level    = GetLevel2(hwndDlg);

	UInt32 defaultDictionary = UInt32(-1);
	UInt32 defaultOrder = UInt32(-1);

	const UInt64 maxRamSize = GetMaxRamSizeForProgram();

	int index = (int)SendDlgItemMessage(hwndDlg, IDC_DICT_SIZE, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
	if (index != CB_ERR) {
		LPCTSTR szFile = BIN(L"7zip.ini");
		TCHAR szVal[64];

		UInt32 val = (UInt32)SendDlgItemMessage(hwndDlg, IDC_DICT_SIZE, CB_GETITEMDATA, (WPARAM)index, (LPARAM)0);

		if (methodID == kDeflate || methodID == kDeflate64) {
			SetWindowText(GetDlgItem(hwndDlg, IDC_LBL_DICT_SIZE), // L"kNumPasses");
				TranslateDef (GetDlgCtrlID(hwndDlg) + IDC_LBL_DICT_SIZE , L"kNumPasses:")); // <2011-04-13> Add translation 115 for kNumPasses:
			_ltow(val, szVal, 10); WritePrivateProfileString(L"settings", L"DeflateNumPasses", szVal, szFile);
		}
		else {
			_ltow(val, szVal, 10); WritePrivateProfileString(L"settings", L"DictSize", szVal, szFile);
			SetWindowText(GetDlgItem(hwndDlg, IDC_LBL_DICT_SIZE), 
				TranslateDef (GetDlgCtrlID(hwndDlg) + IDC_LBL_DICT_SIZE + IDD_CONFIG, L"Dictionary size:")); // <2011-04-13> BUG Fixed (id + IDD_CONFIG)
		}
	}

	SendDlgItemMessage(hwndDlg, IDC_DICT_SIZE, CB_RESETCONTENT, (WPARAM)0, (LPARAM)0);
	SendDlgItemMessage(hwndDlg, IDC_WORD_SIZE, CB_RESETCONTENT, (WPARAM)0, (LPARAM)0);
	SendDlgItemMessage(hwndDlg, IDC_NUM_THREADS, CB_RESETCONTENT, (WPARAM)0, (LPARAM)0);

	// Dictionary
	switch (methodID)
	{
	case kLZMA2:
	case kLZMA:
		{ 
			static const UInt32 kMinDicSize = (1 << 16);
			if(defaultDictionary == UInt32(-1))
			{
				if(level >= 9)		defaultDictionary = (1 << 26);
				else if(level >= 7)	defaultDictionary = (1 << 25);
				else if(level >= 5)	defaultDictionary = (1 << 24);
				else if(level >= 3) defaultDictionary = (1 << 20);
				else                defaultDictionary = (kMinDicSize);
			}
			int i;
			AddDictionarySize(hwndDlg, kMinDicSize);
			SendDlgItemMessage (hwndDlg, IDC_DICT_SIZE, CB_SETCURSEL, (WPARAM)0, (LPARAM)0);
			for(i = 20; i <= 30; i++) {
				for(int j = 0; j < 2; j++)
				{
					if(i == 20 && j > 0)
						continue;
					UInt32 dictionary = (1 << i) + (j << (i - 1));
					if (dictionary >
#ifdef _WIN64
						(1 << 30)
#else
						(1 << 26)
#endif
						)
						continue;
					AddDictionarySize(hwndDlg, dictionary);
					UInt64 decomprSize;
					UInt64 requiredComprSize = GetMemoryUsage2 (hwndDlg, dictionary, decomprSize);
					if (dictionary <= defaultDictionary && requiredComprSize <= maxRamSize) {
						SendDlgItemMessage (hwndDlg, IDC_DICT_SIZE, CB_SETCURSEL, 
							(WPARAM)-1 + (int)SendDlgItemMessage (hwndDlg, IDC_DICT_SIZE, CB_GETCOUNT, (WPARAM)0, (LPARAM)0), 
							(LPARAM)0);
					}
				}
			}
			break;
		}
	case kPPMd:
		{
			if (defaultDictionary == UInt32(-1))
			{
				if (level >= 9)      defaultDictionary = (192 << 20);
				else if (level >= 7) defaultDictionary = ( 64 << 20);
				else if (level >= 5) defaultDictionary = ( 16 << 20);
				else                 defaultDictionary = (  4 << 20);
			}
			int i;
			for(i = 20; i < 31; i++) {
				for(int j = 0; j < 2; j++)
				{
					if (i == 20 && j > 0)
						continue;
					UInt32 dictionary = (1 << i) + (j << (i - 1));
					if (dictionary >= (1 << 31))
						continue;
					AddDictionarySize(hwndDlg, dictionary);
					UInt64 decomprSize;
					UInt64 requiredComprSize = GetMemoryUsage2 (hwndDlg, dictionary, decomprSize);
					if (dictionary <= defaultDictionary && requiredComprSize <= maxRamSize || 
						(int)SendDlgItemMessage (hwndDlg, IDC_DICT_SIZE, CB_GETCOUNT, (WPARAM)0, (LPARAM)0) == 0) {
							SendDlgItemMessage (hwndDlg, IDC_DICT_SIZE, CB_SETCURSEL, 
								(WPARAM)-1 + (int)SendDlgItemMessage (hwndDlg, IDC_DICT_SIZE, CB_GETCOUNT, (WPARAM)0, (LPARAM)0), 
								(LPARAM)0);
					}
				}
			}
			SetNearestSelectComboBox(hwndDlg, IDC_DICT_SIZE, defaultDictionary);
			break;
		}
	case kDeflate:
	case kDeflate64:
		{
			AddPassesCount (hwndDlg, 1, 1);
			AddPassesCount (hwndDlg, 2, 2);
			AddPassesCount (hwndDlg, 3, 11);

			SetNearestSelectComboBox(hwndDlg, IDC_DICT_SIZE, (level >= 7 ? 2 : 1));
			break;
		}
	case kBZip2:
		{
			if (defaultDictionary == UInt32(-1))
			{
				if (level >= 5) defaultDictionary = (900 << 10);
				else if (level >= 3) defaultDictionary = (500 << 10);
				else defaultDictionary = (100 << 10);
			}
			for (size_t i = 100; i <= 900; i+=100)
				AddDictionarySize(hwndDlg, (UInt32)i << 10);
			// SendDlgItemMessage(hwndDlg, IDC_DICT_SIZE, CB_SETCURSEL, (WPARAM)0, (LPARAM)0);
			SetNearestSelectComboBox(hwndDlg, IDC_DICT_SIZE, defaultDictionary); // <2011-05-17> set default dict
			break;
		}
	}

	// Word size
	switch (methodID)
	{
	case kLZMA2:
	case kLZMA:
		{
			if (defaultOrder == UInt32(-1)) defaultOrder = (level >= 7) ? 64 : 32;
			int i;
			for(i = 3; i <= 8; i++)
				for(int j = 0; j < 2; j++)
				{
					UInt32 order = (1 << i) + (j << (i - 1));
					if(order <= 256) ComboboxAdd(hwndDlg, IDC_WORD_SIZE, order);
				}
				ComboboxAdd(hwndDlg, IDC_WORD_SIZE, 273);
				SetNearestSelectComboBox(hwndDlg, IDC_WORD_SIZE, defaultOrder);
				break;
		}
	case kPPMd:
		{
			if (defaultOrder == UInt32(-1))
			{
				if (level >= 9)      defaultOrder = 32;
				else if (level >= 7) defaultOrder = 16;
				else if (level >= 5) defaultOrder = 6;
				else                 defaultOrder = 4;
			}
			int i;
			ComboboxAdd(hwndDlg, IDC_WORD_SIZE, 2);
			ComboboxAdd(hwndDlg, IDC_WORD_SIZE, 3);
			for(i = 2; i < 8; i++)
				for(int j = 0; j < 4; j++)
				{
					UInt32 order = (1 << i) + (j << (i - 2));
					if(order < 32)
						ComboboxAdd(hwndDlg, IDC_WORD_SIZE, order);
				}
				ComboboxAdd(hwndDlg, IDC_WORD_SIZE, 32);
				SetNearestSelectComboBox(hwndDlg, IDC_WORD_SIZE, defaultOrder);
				break;
		}
	case kDeflate:
	case kDeflate64:
		{
			if (defaultOrder == UInt32(-1))
			{
				if (level >= 9)      defaultOrder = 128;
				else if (level >= 7) defaultOrder = 64;
				else                 defaultOrder = 32;
			}
			int i;
			for(i = 3; i <= 8; i++)
				for(int j = 0; j < 2; j++)
				{
					UInt32 order = (1 << i) + (j << (i - 1));
					if(order <= 256)
						ComboboxAdd(hwndDlg, IDC_WORD_SIZE, order);
				}
				ComboboxAdd(hwndDlg, IDC_WORD_SIZE, methodID == kDeflate64 ? 257 : 258);
				SetNearestSelectComboBox(hwndDlg, IDC_WORD_SIZE, defaultOrder);
				break;
		}
	case kBZip2:
		{
			break;
		}
	}

	// Num threads
	UInt32 numHardwareThreads = (level > 0 ? NWindows::NSystem::GetNumberOfProcessors() : 1);
	UInt32 defaultValue = numHardwareThreads;
	UInt32 numAlgoThreadsMax = 1;
	switch (methodID)
	{
	case kLZMA2:
	case kLZMA:
		{
			numAlgoThreadsMax = 2;
			break;
		}
	case kBZip2:
		{
			numAlgoThreadsMax = 32;
			break;
		}
	case kDeflate:
		defaultValue = 1;
	case kDeflate64:
		{
			numAlgoThreadsMax = 128;
			break;
		}
	}
	UInt32 maxThread = numHardwareThreads * 2;
	if (maxThread>numAlgoThreadsMax) maxThread = numAlgoThreadsMax;
	for (UInt32 i = 1; i <= maxThread; i++)
		ComboboxAdd(hwndDlg, IDC_NUM_THREADS, i);
	SetNearestSelectComboBox(hwndDlg, IDC_NUM_THREADS, defaultValue);
	// <2011-04-12> update maximum thread
	TCHAR s[40];
	wsprintf(s,L"/ %d",maxThread);
	SendDlgItemMessage (hwndDlg, IDC_MAX_THREADS, WM_SETTEXT, (WPARAM)0, (LPARAM)s);

	// <2011-05-17> select default solid block size
	int defaultSolidBlockSizeIndex=-1;
	switch (methodID)
	{
	case kLZMA2:
	case kLZMA:
		{
			if (level >= 7)      defaultSolidBlockSizeIndex=13;
			else if (level >= 5) defaultSolidBlockSizeIndex=12;
			else if (level >= 3) defaultSolidBlockSizeIndex=8;
			else                 defaultSolidBlockSizeIndex=4;
		}	break;
	case kPPMd:
		{
			if (level >= 7)      defaultSolidBlockSizeIndex=13;
			else if (level >= 5) defaultSolidBlockSizeIndex=12;
			else if (level >= 3) defaultSolidBlockSizeIndex=10;
			else                 defaultSolidBlockSizeIndex=10;
		}	break;
	case kBZip2:
		{
			if (level >= 5)      defaultSolidBlockSizeIndex=7;
			else if (level >= 3) defaultSolidBlockSizeIndex=6;
			else                 defaultSolidBlockSizeIndex=4;
		}	break;
	}
	if(defaultSolidBlockSizeIndex>=0)
		SendDlgItemMessage (hwndDlg, IDC_SOLID_BLOCK_SIZE, CB_SETCURSEL, (WPARAM)defaultSolidBlockSizeIndex, (LPARAM)0);
}

void UpdatePassword(HWND hwndDlg)
{
	SendDlgItemMessage(hwndDlg, IDC_PASSWORD, EM_SETPASSWORDCHAR, (WPARAM)(IsDlgButtonChecked(hwndDlg, IDC_SHOW_PASSWORD) ? 0 : '*'), (LPARAM)0);
	InvalidateRect(GetDlgItem(hwndDlg, IDC_PASSWORD), NULL, TRUE);
}

void SetNearestSFXModule(HWND hwndDlg, LPTSTR sfxModuleFromSettings)
{
	NWindows::NFile::NFind::CFileInfo fileInfo;
	LPCTSTR szFile = NULL;

	SendDlgItemMessage(hwndDlg, IDC_SFX_MODULE, CB_RESETCONTENT, (WPARAM)0, (LPARAM)0);

	int sfxModuleIndex = -1;
	size_t numSFXModules = g_knownSFXModules.size ();

	for (size_t i = 0; i < numSFXModules; i++) {
		int index = (int)SendDlgItemMessage (hwndDlg, IDC_SFX_MODULE, CB_ADDSTRING, 0, (LPARAM)g_knownSFXModules[i].c_str ());
		if (sfxModuleIndex == -1 &&
			!wcscmp (g_knownSFXModules[i].c_str (), sfxModuleFromSettings)) {
				SendDlgItemMessage (hwndDlg, IDC_SFX_MODULE, CB_SETCURSEL, (WPARAM)index, (LPARAM)0);
				sfxModuleIndex = index;
		}
	}

	if (numSFXModules == 0)
		SendDlgItemMessage (hwndDlg, IDC_SFX_MODULE, CB_SETCURSEL, 
		(WPARAM)SendDlgItemMessage (hwndDlg, IDC_SFX_MODULE, CB_ADDSTRING, 0, 
		(LPARAM)TranslateDef (IDC_SFX_NOT_FOUND_TEXT, L"None found")), (LPARAM)0); // <2011-04-13>
	else {
		if (sfxModuleIndex == -1)
			SendDlgItemMessage (hwndDlg, IDC_SFX_MODULE, CB_SETCURSEL, (WPARAM)0, (LPARAM)0);
	}
}

void PrintMemUsage (HWND hwndDlg, UINT res, UInt64 value)
{
	if (value == (UInt64)Int64(-1)) {
		SendDlgItemMessage (hwndDlg, res, WM_SETTEXT, (WPARAM)0, (LPARAM)L"?");
		return;
	}
	value = (value + (1 << 20) - 1) >> 20;
	TCHAR s[40];
	ConvertUInt64ToString (value, s, 10);
	lstrcat (s, L" MB");
	SendDlgItemMessage (hwndDlg, res, WM_SETTEXT, (WPARAM)0, (LPARAM)s);
}

void SetMemoryUsage(HWND hwndDlg)
{
	UInt64 decompressMem;
	UInt64 memUsage = GetMemoryUsage (hwndDlg, decompressMem);
	PrintMemUsage (hwndDlg, IDC_MEMORY_FOR_COMPRESSING, memUsage);
	PrintMemUsage (hwndDlg, IDC_MEMORY_FOR_DECOMPRESSING, decompressMem);
}

////////////////////////////////////////////////////////////////////////////////////////////
// Proc: Config dialog

static void EnableControls(HWND hwndDlg)
{
	BOOL bEnable = (SendDlgItemMessage(hwndDlg, IDC_COMP_LEVEL, CB_GETCURSEL, (WPARAM)0, (LPARAM)0) != 0);
	int MethodID = GetMethodID (hwndDlg);

	EnableWindow(GetDlgItem(hwndDlg, IDC_COMP_METHOD), bEnable);
	EnableWindow(GetDlgItem(hwndDlg, IDC_DICT_SIZE), bEnable);
	EnableWindow(GetDlgItem(hwndDlg, IDC_WORD_SIZE), bEnable && MethodID != kBZip2);
	EnableWindow(GetDlgItem(hwndDlg, IDC_NUM_THREADS), bEnable);
	EnableWindow(GetDlgItem(hwndDlg, IDC_SOLID_BLOCK_SIZE), bEnable && MethodID != kDeflate && MethodID != kDeflate64);
}

void SetDialogState (HWND hwndDlg, DIALOGDATA *dat)
{
	SIZE newSize;

	SetWindowText (GetDlgItem (hwndDlg, IDC_TOGGLE_EXT_SETTINGS), dat->bDialogExpanded ? L"<" : L">");

	if (dat->bDialogExpanded)
		newSize = dat->expandedSize;
	else
		newSize = dat->collapsedSize;
	SetWindowPos (hwndDlg, NULL, 0, 0, newSize.cx, newSize.cy, SWP_NOMOVE | SWP_SHOWWINDOW);
}

BOOL SetSFXIcon (HWND hwndDlg, DIALOGDATA *dat)
{
	HICON hIcon;
	BOOL  ret = TRUE;

	if (!dat) return FALSE;

	hIcon = ExtractIcon (g_hInstance, dat->SFXIconFile, dat->SFXIconIndex);
	if (hIcon == NULL) {
		ret = FALSE;
		hIcon = LoadIcon (g_hInstance, MAKEINTRESOURCE (IDI_ICON));
	}

	SendDlgItemMessage (hwndDlg, IDC_SFX_ICON, STM_SETICON, (WPARAM)hIcon, (LPARAM)0L);
	return ret;
}

void ChooseSFXIcon (HWND hwndDlg, DIALOGDATA *dat)
{
	if (!dat) return;

	if (SelectIcon (hwndDlg, dat->SFXIconFile, MAX_PATH, &dat->SFXIconIndex) != 1)
		return;

	SetSFXIcon (hwndDlg, dat);
}

void SetFileTimesCheckbox (HWND hwndDlg, DWORD flags)
{
	CheckDlgButton (hwndDlg, IDC_LBL_SAVE_CREATE_TIME, flags & FLAG_SAVE_ATTR_CREATE_TIME);
	CheckDlgButton (hwndDlg, IDC_LBL_SAVE_ACCESS_TIME, flags & FLAG_SAVE_ATTR_ACCESS_TIME);
	// CheckDlgButton (hwndDlg, IDC_LBL_SAVE_MODIFY_TIME, flags & FLAG_SAVE_ATTR_MODIFY_TIME); // <2011-04-13> Always save Modify Time
}

void SetFileAttributesCheckbox (HWND hwndDlg, DWORD flags)
{
	CheckDlgButton (hwndDlg, IDC_LBL_SAVE_READONLY, flags & FLAG_SAVE_ATTR_READONLY);
	CheckDlgButton (hwndDlg, IDC_LBL_SAVE_HIDDEN,   flags & FLAG_SAVE_ATTR_HIDDEN);
	CheckDlgButton (hwndDlg, IDC_LBL_SAVE_SYSTEM,   flags & FLAG_SAVE_ATTR_SYSTEM);
	CheckDlgButton (hwndDlg, IDC_LBL_SAVE_ARCHIVE,  flags & FLAG_SAVE_ATTR_ARCHIVE);
}

void Set7zFileDateCheckbox (HWND hwndDlg, DWORD flags) // <2011-04-12>
{
	CheckDlgButton (hwndDlg, IDC_DATE_TO_NEWEST, flags & FLAG_SET_7Z_DATE_TO_NEWEST);
	CheckDlgButton (hwndDlg, IDC_FIX_DIR_DATETIME,   flags & FLAG_REORDER_DIRECTORY_LIST);
}

INT_PTR CALLBACK ConfigDialog(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	DIALOGDATA *dat;
	dat = (DIALOGDATA *)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

	switch (msg) {
	case WM_INITDIALOG:
		{
			COMPSETTINGS cs;
			ReadSettings(&cs);

			TranslateDialog(hwndDlg, IDD_CONFIG);
			SetWindowText(GetDlgItem(hwndDlg, IDC_AUTHORS), wAuthors);
			if(dat = (DIALOGDATA *)malloc(sizeof(DIALOGDATA)))
			{
				SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (LONG_PTR)dat);
				// Make bold title font
				LOGFONT lf;
				HFONT hNormalFont = (HFONT)SendDlgItemMessage(hwndDlg, IDC_TITLE, WM_GETFONT, 0, 0);
				GetObject(hNormalFont, sizeof(lf), &lf);
				lf.lfWeight = FW_BOLD;
				dat->hBoldFont = CreateFontIndirect(&lf);
				SendDlgItemMessage(hwndDlg, IDC_TITLE, WM_SETFONT, (WPARAM)dat->hBoldFont, 0);

				// calc dialog expanded/collapsed sizes
				{
					RECT rectDlg, rect1, rect2;

					// expanded
					GetWindowRect (hwndDlg, &rectDlg); 
					dat->expandedSize.cy = rectDlg.bottom - rectDlg.top;

					dat->collapsedSize.cx = dat->expandedSize.cx = rectDlg.right - rectDlg.left;

					// collapsed
					GetWindowRect (GetDlgItem (hwndDlg, IDOK), &rect1);
					GetWindowRect (GetDlgItem (hwndDlg, IDCANCEL), &rect2);
					if (rect2.bottom > rect1.bottom)
						rect1 = rect2;

					GetWindowRect (GetDlgItem (hwndDlg, IDC_NUM_THREADS), &rect2); 
					if (rect2.bottom > rect1.bottom)
						rect1 = rect2;

					dat->collapsedSize.cy = rect1.bottom + 5 - rectDlg.top;
				}
				dat->bDialogExpanded = cs.flags & FLAG_DIALOG_EXPANDED;

				wcscpy (dat->SFXIconFile, cs.SFXIconFile);
				dat->SFXIconIndex = cs.SFXIconIndex;
			}

			FindSFXModules ();

			FillSolidBlockSize (hwndDlg);

			// Fill in the compression list
			for(int i = 0; g_compMethods[i].name; i ++)
			{
				int index = (int)SendDlgItemMessage(hwndDlg, IDC_COMP_METHOD, CB_ADDSTRING, 0, (LPARAM)g_compMethods[i].name);
				if(g_compMethods[i].num == cs.methodID) SendDlgItemMessage(hwndDlg, IDC_COMP_METHOD, CB_SETCURSEL, (WPARAM)index, (LPARAM)0);
			}

			for(int i = 0; g_compLevels[i].id || g_compLevels[i + 1].id; i ++)
				if(g_compLevels[i].name)
				{
					int count = (int)SendDlgItemMessage(hwndDlg, IDC_COMP_LEVEL, CB_GETCOUNT, (WPARAM)0, (LPARAM)0);
					int index = (int)SendDlgItemMessageW(hwndDlg, IDC_COMP_LEVEL, CB_ADDSTRING, 0, (LPARAM)TranslateDefW(IDS_COMP_LEVEL + i, g_compLevels[i].name));
					SendDlgItemMessage(hwndDlg, IDC_COMP_LEVEL, CB_SETITEMDATA, (WPARAM)index, (LPARAM)i);
					if(i == cs.level) SendDlgItemMessage(hwndDlg, IDC_COMP_LEVEL, CB_SETCURSEL, (WPARAM)index, (LPARAM)0);
				}

				UpdateCombos(hwndDlg);

				int methodID = GetMethodID(hwndDlg);
				if(methodID == kDeflate || methodID == kDeflate64) {
					SetNearestSelectComboBox (hwndDlg, IDC_DICT_SIZE, cs.deflateNumPasses);
					SetWindowText (GetDlgItem (hwndDlg, IDC_LBL_DICT_SIZE), L"kNumPasses");
				}
				else
					SetNearestSelectComboBox(hwndDlg, IDC_DICT_SIZE, cs.dict);

				SetNearestSelectComboBox(hwndDlg, IDC_WORD_SIZE, cs.word);
				SetNearestSelectComboBox(hwndDlg, IDC_NUM_THREADS, cs.numThreads);
				SetNearestSelectComboBox(hwndDlg, IDC_SOLID_BLOCK_SIZE, cs.solidBlock);

				SetNearestSFXModule(hwndDlg, cs.SFXModule);

				SetMemoryUsage (hwndDlg);

				CheckDlgButton(hwndDlg, IDC_SHOW_PASSWORD, cs.flags & FLAG_SHOW_PASSWORD);
				CheckDlgButton(hwndDlg, IDC_USE_NEXT, cs.flags & FLAG_USE_NEXT);
				CheckDlgButton(hwndDlg, IDC_ENCRYPT_HEADER, cs.flags & FLAG_ENCRYPT_HEADER);

				SetFileTimesCheckbox (hwndDlg, cs.flags);
				SetFileAttributesCheckbox (hwndDlg, cs.flags);
				Set7zFileDateCheckbox (hwndDlg, cs.flags); // <2011-04-12>

				SetDlgItemText(hwndDlg, IDC_PASSWORD, cs.password);
				SendDlgItemMessage(hwndDlg, IDC_PASSWORD, EM_SETLIMITTEXT, (WPARAM)(sizeof(cs.password) - 1), (LPARAM)0);
				UpdatePassword(hwndDlg);

				if (dat) { 
					SetDialogState (hwndDlg, dat);
					SetSFXIcon (hwndDlg, dat);
				}

				EnableControls(hwndDlg);
				return TRUE;
		}
	case WM_CTLCOLORSTATIC:
		if((GetDlgItem(hwndDlg, IDC_TITLE) == (HWND)lParam)
			|| (GetDlgItem(hwndDlg, IDC_SUBTITLE) == (HWND)lParam)
			|| (GetDlgItem(hwndDlg, IDI_ICON) == (HWND)lParam))
		{
			SetBkMode((HDC)wParam, TRANSPARENT);
			return (BOOL)GetStockObject(NULL_BRUSH);
		}
		break;
	case WM_COMMAND:
		{
			switch(LOWORD(wParam))
			{
			case IDC_COMP_METHOD: {
				if(HIWORD(wParam) == CBN_SELCHANGE) {
					UpdateCombos(hwndDlg);
					SetMemoryUsage (hwndDlg);
					EnableControls(hwndDlg);
				}
								  } break;

			case IDC_COMP_LEVEL: {
				if(HIWORD(wParam) == CBN_SELCHANGE) {
					UpdateCombos(hwndDlg);
					SetMemoryUsage (hwndDlg);
					EnableControls(hwndDlg);
				}
								 } break;

			case IDC_DICT_SIZE:
			case IDC_WORD_SIZE: 
			case IDC_NUM_THREADS: {
				if(HIWORD(wParam) == CBN_SELCHANGE) {
					SetMemoryUsage (hwndDlg);
				}
								  } break;

			case IDC_SHOW_PASSWORD: {
				UpdatePassword (hwndDlg);
									} break;

			case IDC_SFX_ICON: {
				ChooseSFXIcon (hwndDlg, dat);
							   } break;

			case IDC_TOGGLE_EXT_SETTINGS: {
				if (dat) {
					dat->bDialogExpanded = !dat->bDialogExpanded;
					SetDialogState (hwndDlg, dat);
				}
										  } break;

			case IDOK: {
				COMPSETTINGS cs;
				ZeroMemory(&cs, sizeof(COMPSETTINGS));
				ReadSettings(&cs);

				int index;

				// Method
				index = (int)SendDlgItemMessage(hwndDlg, IDC_COMP_METHOD, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
				cs.methodID = g_compMethods[index].num;
				// Level
				cs.level = GetLevel2 (hwndDlg);
				// word
				cs.word = GetOrder (hwndDlg);
				// Flags
				cs.flags = (IsDlgButtonChecked(hwndDlg, IDC_SHOW_PASSWORD) ? FLAG_SHOW_PASSWORD : 0)
					| (IsDlgButtonChecked(hwndDlg, IDC_USE_NEXT) ? FLAG_USE_NEXT : 0)
					| (IsDlgButtonChecked(hwndDlg, IDC_ENCRYPT_HEADER) ? FLAG_ENCRYPT_HEADER : 0)

					| (IsDlgButtonChecked(hwndDlg, IDC_LBL_SAVE_CREATE_TIME) ? FLAG_SAVE_ATTR_CREATE_TIME : 0)
					| (IsDlgButtonChecked(hwndDlg, IDC_LBL_SAVE_ACCESS_TIME) ? FLAG_SAVE_ATTR_ACCESS_TIME : 0)
					// | (IsDlgButtonChecked(hwndDlg, IDC_LBL_SAVE_MODIFY_TIME) ? FLAG_SAVE_ATTR_MODIFY_TIME : 0)
					| FLAG_SAVE_ATTR_MODIFY_TIME // <2011-04-13> Always save Modify Time

					| (IsDlgButtonChecked(hwndDlg, IDC_LBL_SAVE_READONLY) ? FLAG_SAVE_ATTR_READONLY : 0)
					| (IsDlgButtonChecked(hwndDlg, IDC_LBL_SAVE_HIDDEN)   ? FLAG_SAVE_ATTR_HIDDEN : 0)
					| (IsDlgButtonChecked(hwndDlg, IDC_LBL_SAVE_SYSTEM)   ? FLAG_SAVE_ATTR_SYSTEM : 0)
					| (IsDlgButtonChecked(hwndDlg, IDC_LBL_SAVE_ARCHIVE)  ? FLAG_SAVE_ATTR_ARCHIVE : 0)

					| (IsDlgButtonChecked(hwndDlg, IDC_DATE_TO_NEWEST)   ? FLAG_SET_7Z_DATE_TO_NEWEST : 0)		// <2011-04-12>
					| (IsDlgButtonChecked(hwndDlg, IDC_FIX_DIR_DATETIME) ? FLAG_REORDER_DIRECTORY_LIST : 0)	// <2011-04-12>

					| (dat && dat->bDialogExpanded ? FLAG_DIALOG_EXPANDED : 0);
				// Number of threads
				cs.numThreads = GetNumThreads (hwndDlg);

				// solid block
				index = (int)SendDlgItemMessage(hwndDlg, IDC_SOLID_BLOCK_SIZE, CB_GETCURSEL, (WPARAM)0, (LPARAM)0);
				cs.solidBlock = (UInt32)SendDlgItemMessage(hwndDlg, IDC_SOLID_BLOCK_SIZE, CB_GETITEMDATA, (WPARAM)index, (LPARAM)0);

				// dictionary/num passes
				if (cs.methodID == kDeflate || cs.methodID == kDeflate64)
					cs.deflateNumPasses = GetDictionary (hwndDlg);        
				else
					cs.dict = GetDictionary (hwndDlg);

				// password
				GetDlgItemText(hwndDlg, IDC_PASSWORD, cs.password, sizeof(cs.password));

				// SFX module
				SendDlgItemMessage(hwndDlg, IDC_SFX_MODULE, CB_GETLBTEXT, 
					(WPARAM)SendDlgItemMessage(hwndDlg, IDC_SFX_MODULE, CB_GETCURSEL, (WPARAM)0, (LPARAM)0), 
					(LPARAM)cs.SFXModule);
				if (!wcscmp (cs.SFXModule, L"None found"))
					cs.SFXModule[0] = L'\0';
				else if(!wcscmp (cs.SFXModule, TranslateDef (IDC_SFX_NOT_FOUND_TEXT, L"None found"))) // <2011-04-13>
					cs.SFXModule[0] = L'\0';

				if (dat) {
					wcscpy (cs.SFXIconFile, dat->SFXIconFile);
					cs.SFXIconIndex = dat->SFXIconIndex;
				}

				WriteSettings(&cs);
				EndDialog(hwndDlg, IDOK);
					   } break;

			case IDCANCEL: {
				EndDialog(hwndDlg, IDCANCEL);
						   } break;

			case IDC_MAIL_TO_ADAM: {
				ShellExecute (NULL, L"open", L"mailto:ono@java.pl", NULL, NULL, SW_SHOWDEFAULT);
								   } break;

			case IDC_MAIL_TO_DUNY: {
				ShellExecute (NULL, L"open", L"mailto:tbbcmrjytv@gmail.com", NULL, NULL, SW_SHOWDEFAULT);
								   } break;

			case IDC_MAIL_TO_SAX0N: {
				ShellExecute (NULL, L"open", L"mailto:Sax0n0xaS@gmail.com", NULL, NULL, SW_SHOWDEFAULT);
									} break;

			case IDC_RESET_SFX_ICON: {
				wcscpy (dat->SFXIconFile, L""); dat->SFXIconIndex = 0;
				SetSFXIcon (hwndDlg, dat);
									 } break;

			case IDC_RESET_FILE_TIMES: {
				SetFileTimesCheckbox (hwndDlg, DEFAULT_FILE_TIMES);
									   } break;

			case IDC_RESET_FILE_ATTRIBUTES: {
				SetFileAttributesCheckbox (hwndDlg, DEFAULT_FILE_ATTRIBUTES);
											} break;
			}
			break;
		}
	case WM_NOTIFY:
		{
			break;
		}
	case WM_DESTROY:
		if(dat)
		{
			if(dat->hBoldFont) DeleteObject(dat->hBoldFont);
			free(dat);
		}
		break;
	}
	return FALSE;
}

////////////////////////////////////////////////////////////////////////////////////////////
// Proc: Password

INT_PTR CALLBACK PasswordDialog(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	DIALOGDATA *dat;
	dat = (DIALOGDATA *)GetWindowLongPtr(hwndDlg, GWLP_USERDATA);

	switch (msg) {
	case WM_INITDIALOG:
		{
			LPTSTR szPasswordBuffer = (LPTSTR)lParam;
			TranslateDialog(hwndDlg, IDD_PASSWORD);
			if(dat = (DIALOGDATA *)malloc(sizeof(DIALOGDATA)))
			{
				SetWindowLongPtr(hwndDlg, GWLP_USERDATA, (LONG_PTR)dat);
				// Make bold title font
				LOGFONT lf;
				HFONT hNormalFont = (HFONT)SendDlgItemMessage(hwndDlg, IDC_TITLE, WM_GETFONT, 0, 0);
				GetObject(hNormalFont, sizeof(lf), &lf);
				lf.lfWeight = FW_BOLD;
				dat->hBoldFont = CreateFontIndirect(&lf);
				SendDlgItemMessage(hwndDlg, IDC_TITLE, WM_SETFONT, (WPARAM)dat->hBoldFont, 0);
				// Store password buffer pointer
				dat->szPassword = szPasswordBuffer;
			}

			SendDlgItemMessage(hwndDlg, IDC_PASSWORD, EM_SETLIMITTEXT, (WPARAM)PASSWORD_SIZE, (LPARAM)0);
			//UpdatePassword(hwndDlg);

			return TRUE;
			break;
		}
	case WM_CTLCOLORSTATIC:
		if((GetDlgItem(hwndDlg, IDC_TITLE) == (HWND)lParam)
			|| (GetDlgItem(hwndDlg, IDC_SUBTITLE) == (HWND)lParam)
			|| (GetDlgItem(hwndDlg, IDI_ICON) == (HWND)lParam))
		{
			SetBkMode((HDC)wParam, TRANSPARENT);
			return (BOOL)GetStockObject(NULL_BRUSH);
		}
		break;
	case WM_COMMAND:
		{
			switch(LOWORD(wParam))
			{
			case IDOK:
				if(dat) GetDlgItemText(hwndDlg, IDC_PASSWORD, dat->szPassword, PASSWORD_SIZE);
				EndDialog(hwndDlg, IDOK);
				break;
			case IDCANCEL:
				EndDialog(hwndDlg, IDCANCEL);
				break;
			}
			break;
		}
	case WM_NOTIFY:
		{
			break;
		}
	case WM_DESTROY:
		if(dat)
		{
			if(dat->hBoldFont) DeleteObject(dat->hBoldFont);
			free(dat);
		}
		break;
	}
	return FALSE;
}

LPCTSTR GetPassword()
{   
	static TCHAR szPassword[PASSWORD_SIZE + 1] = L"\0";

	if(DialogBoxParam(g_hInstance, MAKEINTRESOURCE(IDD_PASSWORD), NULL, PasswordDialog, (LPARAM)szPassword) == IDOK)
		return szPassword;
	else
		return NULL;
}
}
