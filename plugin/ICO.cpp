#include "7zip.h"

bool ExtractIconFromIcoFile (const wchar_t* filename, 
                             LPMEMICONDIR *newIconDir, DWORD &newIconDirSize,
                             std::vector<LPICONIMAGE> &iconImages)
{
	HANDLE       hFile;
	DWORD        dwBytesRead;
    LPMEMICONDIR myIconDir;
    ICONDIRENTRY iconEntry;
    LPMEMICONDIRENTRY ptr;
    LPICONIMAGE  pIconImage;
	int          i;
    WORD         idReserved, idType, idCount;
    std::vector<DWORD> offsets;

	if ((hFile = CreateFile (filename, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
        return false;
	
	ReadFile (hFile, &idReserved, sizeof (WORD), &dwBytesRead, NULL);
	ReadFile (hFile, &idType, sizeof (WORD), &dwBytesRead, NULL);
	ReadFile (hFile, &idCount, sizeof (WORD), &dwBytesRead, NULL);

    if (idType != 1){
        CloseHandle (hFile);
        return false;
    }
    
    newIconDirSize = sizeof (MEMICONDIR) + sizeof (MEMICONDIRENTRY) * idCount;
    *newIconDir = myIconDir = (LPMEMICONDIR)malloc (newIconDirSize);
    if (myIconDir == NULL) {
        CloseHandle (hFile);
        return false;
    }

    myIconDir->idReserved = idReserved;
    myIconDir->idType     = idType;
    myIconDir->idCount    = idCount;

    ptr = &myIconDir->idEntries[0];
	// Read the ICONDIRENTRY elements
    offsets.clear ();
    for (i = 0; i < idCount; i++, ptr++) {
        ReadFile (hFile, &iconEntry, sizeof (ICONDIRENTRY), &dwBytesRead, NULL);
        ptr->bWidth = iconEntry.bWidth;
        ptr->bHeight = iconEntry.bHeight;
        ptr->bColorCount = iconEntry.bColorCount;
        ptr->bReserved = iconEntry.bReserved;
        ptr->wPlanes = iconEntry.wPlanes;
        ptr->wBitCount = iconEntry.wBitCount;
        ptr->dwBytesInRes = iconEntry.dwBytesInRes;
        ptr->nID = i + 1;
        offsets.push_back (iconEntry.dwImageOffset);
    }

    iconImages.clear ();
    for (i = 0; i < idCount; i++) {
        pIconImage = (LPICONIMAGE)malloc (myIconDir->idEntries[i].dwBytesInRes);
        SetFilePointer (hFile, offsets[i], NULL, FILE_BEGIN);
        ReadFile (hFile, pIconImage, myIconDir->idEntries[i].dwBytesInRes, &dwBytesRead, NULL);
        iconImages.push_back (pIconImage);
    }

    CloseHandle (hFile);
    return true;
}

struct IconID_t
{
    ULONG  ulID;
	std::wstring sID;
    bool   isULONG;
};

struct IconCallbackData
{
    std::vector<IconID_t> m_iconIDs;
};

BOOL CALLBACK EnumResNameProc(
    HMODULE hModule,   // module handle
    const wchar_t* lpszType,  // resource type
    wchar_t* lpszName,   // resource name
    LONG_PTR lParam)   // application-defined parameter
{
    if (lpszType == RT_GROUP_ICON) {
        IconCallbackData *iconData = (IconCallbackData*)lParam;
        IconID_t newID;

        if ((ULONG)lpszName < 65536) {
            newID.isULONG = true;
            newID.ulID = (ULONG)lpszName;
        }
        else {
            newID.isULONG = false;
            newID.sID = lpszName;
        }
        iconData->m_iconIDs.push_back (newID);
	}

	return TRUE;
}

bool ExtractIconFromPEFile (const wchar_t* filename, DWORD iconIndex, 
                            LPMEMICONDIR *newIconDir, DWORD &newIconDirSize, 
                            std::vector<LPICONIMAGE> &iconImages)
{
    HMODULE	hLibrary;
    HRSRC   hRsrc   = NULL;
    HGLOBAL hGlobal = NULL;
    IconCallbackData iconData;
    LPMEMICONDIR lpIcon = NULL;
    LPMEMICONDIR myIconDir;
    LPICONIMAGE lpIconImage, myIconImage;

    if ((hLibrary = LoadLibraryEx (filename, NULL, DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE)) == NULL)
		return false;

    EnumResourceNames (hLibrary, RT_GROUP_ICON, EnumResNameProc, (LONG)&iconData);

    if (iconIndex >= iconData.m_iconIDs.size ()) {
		FreeLibrary (hLibrary);
		return false;
	}
    IconID_t iconID = iconData.m_iconIDs[iconIndex];
    hRsrc = FindResource (hLibrary, iconID.isULONG ? MAKEINTRESOURCE (iconID.ulID) : iconID.sID.c_str (), RT_GROUP_ICON);
	if (hRsrc == NULL) {
		FreeLibrary (hLibrary);
		return false;
	}

    if ((hGlobal = LoadResource (hLibrary, hRsrc)) == NULL) {
        FreeLibrary (hLibrary);
		return false;
    }
    if ((lpIcon = (LPMEMICONDIR)LockResource (hGlobal)) == NULL) {
        FreeLibrary (hLibrary);
		return false;
    }
    
    DWORD rcSize = SizeofResource (hLibrary, hRsrc);
    *newIconDir = myIconDir = (LPMEMICONDIR)malloc (rcSize);
    if (myIconDir == NULL) {
        FreeLibrary (hLibrary);
        return false;
    }
    newIconDirSize = rcSize;
    memcpy (myIconDir, lpIcon, rcSize);

    iconImages.clear ();
    for (WORD i = 0; i < lpIcon->idCount; i++) {
        myIconDir->idEntries[i].nID = i + 1;

        if ((hRsrc = FindResource (hLibrary, 
            MAKEINTRESOURCE (lpIcon->idEntries[i].nID), RT_ICON)) == NULL) {
		    FreeLibrary (hLibrary);
            free (myIconDir);
		    return false;
        }
        if ((hGlobal = LoadResource (hLibrary, hRsrc)) == NULL) {
		    FreeLibrary (hLibrary);
		    free (myIconDir);
		    return false;
        }

        if ((lpIconImage = (LPICONIMAGE)LockResource (hGlobal)) == NULL) {
            FreeLibrary (hLibrary);
            free (myIconDir);
		    return false;
        }

        rcSize = SizeofResource (hLibrary, hRsrc);
        if ((myIconImage = (LPICONIMAGE)malloc (rcSize)) == NULL) {
            FreeLibrary (hLibrary);
            free (myIconDir);
		    return false;
        }
        memcpy (myIconImage, lpIconImage, rcSize);

        iconImages.push_back (myIconImage);
    }

    FreeLibrary (hLibrary);
    return true;
}

void ReplaceIconResourceInPE (const wchar_t* fileName, const wchar_t* iconFile, DWORD iconIndex)
{
    LPMEMICONDIR pIconDir = NULL;
    HMODULE	hLibrary;
    IconCallbackData iconData;
    HANDLE hUpdate = NULL;
    std::vector<LPICONIMAGE> iconImages;
    std::vector<WORD> initalIconIDs;
    size_t i, numIconIDs = 0;
    TCHAR realIconFileName[MAX_PATH];
    DWORD newIconDirSize;
    IconID_t replacedIconID;

    if ((hLibrary = LoadLibraryEx (fileName, NULL, DONT_RESOLVE_DLL_REFERENCES | LOAD_LIBRARY_AS_DATAFILE)) == NULL)
		return;

    // default value, if exe contains no icons
    replacedIconID.isULONG = true;
    replacedIconID.ulID = 100;

    EnumResourceNames (hLibrary, RT_GROUP_ICON, EnumResNameProc, (LONG)&iconData); 

    if (iconData.m_iconIDs.size () > 0) {
        HRSRC   hRsrc   = NULL;
        HGLOBAL hGlobal = NULL;

        IconID_t iconID = iconData.m_iconIDs[0];
        hRsrc = FindResource (hLibrary, iconID.isULONG ? MAKEINTRESOURCE (iconID.ulID) : iconID.sID.c_str (), RT_GROUP_ICON);
        if (hRsrc != NULL) {
		    if ((hGlobal = LoadResource (hLibrary, hRsrc)) == NULL) {
                FreeLibrary (hLibrary);
		        return;
            }
            if ((pIconDir = (LPMEMICONDIR)LockResource (hGlobal)) == NULL) {
                FreeLibrary (hLibrary);
		        return;
            }

            replacedIconID = iconID;

            numIconIDs = pIconDir->idCount;
            for (i = 0; i < pIconDir->idCount; i++)
                initalIconIDs.push_back (pIconDir->idEntries[i].nID);
	    }
    }
    FreeLibrary (hLibrary);

    ExpandEnvironmentStrings (iconFile, realIconFileName, sizeof (realIconFileName) / sizeof (realIconFileName[0]));
    if (!ExtractIconFromPEFile (realIconFileName, iconIndex, &pIconDir, newIconDirSize, iconImages) &&
        !ExtractIconFromIcoFile (realIconFileName, &pIconDir, newIconDirSize, iconImages))
        return;

    hUpdate = BeginUpdateResource (fileName, FALSE);
    if (hUpdate == NULL)
        goto free_mem;

    if (UpdateResource (hUpdate, 
        RT_GROUP_ICON, 
        replacedIconID.isULONG ? MAKEINTRESOURCE (replacedIconID.ulID) : replacedIconID.sID.c_str (),
        MAKELANGID (LANG_ENGLISH, SUBLANG_ENGLISH_US),
        pIconDir, newIconDirSize) == FALSE) {
        EndUpdateResource (hUpdate, TRUE);
        goto free_mem;
    }

    for (i = 0; i < pIconDir->idCount; i++) {
        if (UpdateResource (hUpdate, 
            RT_ICON, 
            MAKEINTRESOURCE (pIconDir->idEntries[i].nID),
            MAKELANGID (LANG_ENGLISH, SUBLANG_ENGLISH_US),
            (LPVOID)iconImages[i], 
            pIconDir->idEntries[i].dwBytesInRes) == FALSE) {
            EndUpdateResource (hUpdate, TRUE);
            goto free_mem;
        }
    }

    for (i = pIconDir->idCount; i < numIconIDs; i++) {
        if (UpdateResource (hUpdate, 
            RT_ICON, 
            MAKEINTRESOURCE (initalIconIDs[i]),
            MAKELANGID (LANG_ENGLISH, SUBLANG_ENGLISH_US),
            NULL, 0) == FALSE) {
            EndUpdateResource (hUpdate, TRUE);
            goto free_mem;
        }
    }

    EndUpdateResource (hUpdate, FALSE);

free_mem:
    free (pIconDir);
    for (i = 0; i < iconImages.size (); i++)
        free (iconImages[i]);
}