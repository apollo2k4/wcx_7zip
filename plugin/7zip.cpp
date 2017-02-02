////////////////////////////////////////////////////////////////////////////////
// 7zip Plugin for Total Commander
// Copyright (c) 2004-2007 Adam Strzelecki <ono@java.pl>
//			 (c) 2009 Dmitry Efimenko <tbbcmrjytv@gmail.com>
//			 (c) 2009 Alexandr Popov <Sax0n0xaS@gmail.com>
//			 (c) 2010 Cristian Adam <cristian.adam@gmail.com>
//			 (c) 2011-2016 dllee <dllee.tw@gmail.com>
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

#include <algorithm>
#include "7zip.h"

#pragma comment(lib, "Wintrust.lib")  // Link with the Wintrust.lib file for WinVerifyTrust()

LPWSTR wAuthors=L"\xA9 2004-2007 Adam Strzelecki <ono@java.pl>\r\n"		// \xA9 is for (c)
L"    2009 Dmitry (Duny) Efimenko <tbbcmrjytv@gmail.com>\r\n"
L"    2009 Alexandr (Sax0n) Popov <Sax0n0xaS@gmail.com>\r\n"
L"    2010 Cristian Adam <cristian.adam@gmail.com>\r\n"
L"    2011-2016 dllee <dllee.tw@gmail.com>\r\n"
L"This software is published under LGPL license.";

HINSTANCE g_hInstance = NULL;
tProcessDataProcW procProcess = NULL;

const UInt64 gMaxCheckStartPosition = 1 << 20;

class C7zPassword;

typedef struct _7ZHANDLE
{
	NArchive::N7z::CArchiveDatabaseEx *db;
	CMyComPtr<IInStream> inStream;
	CMyComPtr<C7zPassword> password;
	UInt32 iItem;
	wchar_t* fileName;
	wchar_t* archiveName;
	UInt64 written;
	HANDLE hThread, hPushEvent, hPullEvent;
	tProcessDataProcW procProcess;
	int iOperation;
	int iResult;
} ZHANDLE, *PZHANDLE;

typedef struct _TmpDIRITEM
{
	UInt32 index;
	NArchive::N7z::CFileItem fileitem;
	UInt64 cTime;
	UInt64 aTime;
	UInt64 mTime;
	bool   cTimeDefined;
	bool   aTimeDefined;
	bool   mTimeDefined;
} TmpDIRITEM, *PTmpDIRITEM;

bool TmpDirItemSortFx(const TmpDIRITEM& d1, const TmpDIRITEM& d2)
{
	return (d1.fileitem.Name.Compare(d2.fileitem.Name)<0);
}

////////////////////////////////////////////////////////////////////////////////

class C7zPassword:
	public ICryptoGetTextPassword,
	public CMyUnknownImp
{
public:
	MY_UNKNOWN_IMP

	bool _passwordIsDefined;
	UString _password;
	CSysString _arcName;
	NWindows::NFile::NFind::CFileInfo _fileInfo;
	unsigned long _asked;

	C7zPassword(const wchar_t* arcName)
	{
		_asked = 0;
		_passwordIsDefined = false;
		_password = L"";
		_arcName = arcName;
		DebugString(L"C7zPassword::C7zPassword(%s)", arcName);
		_fileInfo.Find(_arcName);
	}

	~C7zPassword()
	{
		// Cleanup password from the memory
		_password = L"";
		DebugString(L"C7zPassword::~C7zPassword()");
	}

	inline unsigned long Asked() { return _asked; }

	bool Matches(const wchar_t* arcName, bool updateOnly = false)
	{
		bool bResult = false;
		if(_arcName.CompareNoCase(arcName) == 0)
		{
			NWindows::NFile::NFind::CFileInfo fileInfo;
			fileInfo.Find(_arcName);
			if(fileInfo.CTime.dwHighDateTime != _fileInfo.CTime.dwHighDateTime
				|| fileInfo.CTime.dwLowDateTime != _fileInfo.CTime.dwLowDateTime
				|| fileInfo.MTime.dwHighDateTime != _fileInfo.MTime.dwHighDateTime
				|| fileInfo.MTime.dwLowDateTime != _fileInfo.MTime.dwLowDateTime
				|| fileInfo.Size != _fileInfo.Size)
			{
				if(!updateOnly)
				{
					_passwordIsDefined = false;
					_password = L"";
				}
				_fileInfo = fileInfo;
			}
			bResult = true;
		}
		DebugString(L"C7zPassword::Matches(%s) %s", arcName, bResult ? L"TRUE" : L"FALSE");
		return bResult;
	}

	STDMETHOD(CryptoGetTextPassword)(BSTR *password)
	{
		_asked ++;
		const wchar_t* lpszPass = NULL;
		if(!_passwordIsDefined && (lpszPass = GetPassword()))
		{
			_password = lpszPass;
			_passwordIsDefined = true;
		}
		if(!_passwordIsDefined) return E_ABORT;
		CMyComBSTR tempName(_password);
		*password = tempName.MyCopy();

		return S_OK;
	}
};

////////////////////////////////////////////////////////////////////////////////

class C7zOutStream:
	public ISequentialOutStream,
	public ICompressProgressInfo,
	public CMyUnknownImp
{
public:
	MY_UNKNOWN_IMP2(ISequentialOutStream, ICompressProgressInfo)

		PZHANDLE _handle;
	UInt32 _curIndex, _startIndex;
	bool _isOpen;
	COutFileStream *_streamSpec;
	COutStreamWithCRC _hash;
	UInt64 _pos, _curSize;

	C7zOutStream(PZHANDLE handle, UInt32 startIndex)
	{
		_handle = handle;
		_curIndex = _startIndex = startIndex;
		_pos = 0;
		_isOpen = false;
		_curSize = handle->db->Files[_curIndex].Size;
		DebugString(L"C7zOutStream::constructor start: %d, size: %d", _startIndex, _curSize);
	}

	STDMETHOD(Write)(const void *data, UInt32 size, UInt32 *processedSize)
	{
		UInt64 bytes = 0;

		DebugString(L"(0x%X) C7zOutStream::Write size: %d", GetCurrentThreadId(), size); // <2011-06-21> add ThreadID

		while(bytes < size)
		{
			UInt64 chunk = size - bytes;

			// Check if we should abort the file extraction
			if(_handle->iResult == E_EABORTED)
			{
				if(_isOpen && _handle->iOperation != PK_TEST)
				{
					DebugString(L"(0x%X) C7zOutStream::Write Aborted by user", GetCurrentThreadId()); // <2011-06-21> add ThreadID
					_hash.SetStream(NULL);
					_hash.Init(false);

					::DeleteFile(_handle->fileName);
				}
				if(processedSize) *processedSize = (UInt32)bytes;
				return E_EWRITE;
			}

			while(_curIndex > _handle->iItem)
			{
				SetEvent(_handle->hPullEvent);
				DebugString(L"(0x%X) C7zOutStream::Write SetEvent(_handle->hPullEvent) & WaitForSingleObject(_handle->hPushEvent, INFINITE)", GetCurrentThreadId()); // <2011-06-21> add ThreadID
				WaitForSingleObject(_handle->hPushEvent, INFINITE);
				ResetEvent(_handle->hPushEvent);
				DebugString(L"(0x%X) C7zOutStream::Write ResetEvent(_handle->hPushEvent)", GetCurrentThreadId()); // <2011-06-21> add ThreadID
			}

			// We creating new file
			if(_handle->iItem == _curIndex && !_isOpen && _handle->iOperation != PK_SKIP)
			{
				if(_handle->iOperation == PK_TEST)
				{
					DebugString(L"(0x%X) C7zOutStream::Write(test) Close(\"%s\"", GetCurrentThreadId(), _handle->fileName); // <2011-06-21> add ThreadID
					_hash.Init (true);
					_isOpen = true;
				}
				else if(_streamSpec = new COutFileStream())
				{
					CMyComPtr<ISequentialOutStream> streamLoc(_streamSpec);
					if(_isOpen = _streamSpec->Create(_handle->fileName, true))
					{
						DebugString(L"(0x%X) C7zOutStream::Write Open(\"%s\"", GetCurrentThreadId(), _handle->fileName); // <2011-06-21> add ThreadID
						_hash.SetStream(streamLoc);
						_hash.Init(true);
					}
					else
					{
						DebugString(L"(0x%X) C7zOutStream::Write Open failed \"%s\"", GetCurrentThreadId(), _handle->fileName); // <2011-06-21> add ThreadID
						_handle->iResult = E_ECREATE;
						return E_EWRITE;
					}
				}
			}

			if(_pos + chunk > _curSize) chunk = _curSize - _pos;
			if(_isOpen)
			{
				DebugString(L"(0x%X) C7zOutStream::Write Write(%d, %d)", GetCurrentThreadId(), chunk, _pos); // <2011-06-21> add ThreadID

				UInt32 localSize;
				_hash.Write((const Byte *)data + bytes, (UInt32)chunk, &localSize);
				_pos += localSize; bytes += localSize;
			}
			else
			{
				bytes += chunk;
				_pos += chunk;
			}

			// Check if we skipped to next file
			if(_pos >= _curSize)
			{
				if(_isOpen) // Setup file attributes
				{
					// Mark we have CRC error
					if(_handle->db->Files[_curIndex].CrcDefined)
						if(_handle->db->Files[_curIndex].Crc != _hash.GetCRC())
						{
							_handle->iResult = E_BAD_ARCHIVE;
							DebugString(L"(0x%X) C7zOutStream::Write CRC Error(\"%s\"", GetCurrentThreadId(), _handle->fileName); // <2011-06-21> add ThreadID
						}
					// Write attributes / if not just testing
					if(_handle->iResult != E_BAD_ARCHIVE // <2011-06-21>
					&& _handle->iOperation != PK_TEST)
					{
						_streamSpec->Close();	// <2011-04-14>
						_hash.SetStream(NULL);	// <2011-04-14>
						_hash.Init(false);		// <2011-04-14>

						FILETIME *atp, *ctp, *mtp;
						UInt32 sz;

						sz = _handle->db->CTime.Defined.Size ();
						ctp = NULL;
						if (sz > 0 && _curIndex < sz && _handle->db->CTime.Defined[_curIndex])
							ctp = (FILETIME *)&_handle->db->CTime.Values[_curIndex];

						sz = _handle->db->ATime.Defined.Size ();
						atp = NULL;
						if (sz > 0 && _curIndex < sz && _handle->db->ATime.Defined[_curIndex])
							atp = (FILETIME *)&_handle->db->ATime.Values[_curIndex];

						sz = _handle->db->MTime.Defined.Size ();
						mtp = NULL;
						if (sz > 0 && _curIndex < sz && _handle->db->MTime.Defined[_curIndex])
							mtp = (FILETIME *)&_handle->db->MTime.Values[_curIndex];

						// _streamSpec->SetTime (ctp, atp, mtp);
						NWindows::NFile::NDirectory::SetDirTime(_handle->fileName, ctp, atp, mtp); // <2011-04-14>

						if (_handle->db->Files[_curIndex].AttribDefined)
							NWindows::NFile::NDirectory::MySetFileAttributes(_handle->fileName,
							_handle->db->Files[_curIndex].Attrib);
					}
					DebugString(L"(0x%X) C7zOutStream::Write Close(\"%s\"", GetCurrentThreadId(), _handle->fileName);  // <2011-06-21> add ThreadID
				}
				_curIndex ++; _pos = 0;
				if(_curIndex < (UInt32)_handle->db->Files.Size()) _curSize = _handle->db->Files[_curIndex].Size;
				_isOpen = false;
			}
		}

		if(processedSize) *processedSize = (UInt32)bytes;
		DebugString(L"(0x%X) C7zOutStream::Write return S_OK", GetCurrentThreadId()); // <2011-06-21> add ThreadID
		return S_OK;
	}
	STDMETHOD(WritePart)(const void *data, UInt32 size, UInt32 *processedSize)
	{ return Write(data, size, processedSize); }

	STDMETHOD(SetRatioInfo)(const UInt64 *inSize, const UInt64 *outSize)
	{ 
		if (_handle->iResult == E_EABORTED)
			return E_ABORT;

		_handle->written = *outSize;
		return S_OK; 
	}
};

////////////////////////////////////////////////////////////////////////////////
// Main DLL functions

extern "C" {

// <2011-06-28> Check If the ArcFile has digital signature
// http://msdn.microsoft.com/en-us/library/aa382384(v=vs.85).aspx
HRESULT __stdcall CheckFileSignature(LPCWSTR pcwszFilePath)
{
	GUID gAction = WINTRUST_ACTION_GENERIC_VERIFY_V2;
	WINTRUST_FILE_INFO	wFileInfo;
	WINTRUST_DATA		wData;
	HRESULT				result;

	memset(&wFileInfo, 0, sizeof(WINTRUST_FILE_INFO));
	memset(&wData, 0, sizeof(WINTRUST_DATA));

	wFileInfo.cbStruct = sizeof(WINTRUST_FILE_INFO);
	wFileInfo.pcwszFilePath = pcwszFilePath;

	wData.cbStruct            = sizeof(WINTRUST_DATA);
	wData.dwUIChoice          = WTD_UI_NONE;
	wData.fdwRevocationChecks = WTD_REVOKE_NONE;
	wData.dwUnionChoice       = WTD_CHOICE_FILE;
	wData.pFile               = &wFileInfo;
	wData.dwStateAction       = 0; // Default verification.

	result = WinVerifyTrust((HWND)INVALID_HANDLE_VALUE, &gAction, &wData);
	return result;
}

HRESULT __stdcall TestIfArcFileHasSignature(LPCWSTR pcwszArcFilePath)
{
	WIN32_FIND_DATA FindFileData;
	HANDLE hFind = FindFirstFile(pcwszArcFilePath, &FindFileData);
	if (hFind == INVALID_HANDLE_VALUE) 
		return S_OK; // New File, don't have to check signature
	FindClose(hFind);

	DWORD dwLastError;
	bool NeedToAskUser=false;
	HRESULT rFileSignature=CheckFileSignature(pcwszArcFilePath);

    switch (rFileSignature) 
    {
		case 0x800B0003: // normal .7z file
			break;

        case ERROR_SUCCESS:
            /*
            Signed file:
                - Hash that represents the subject is trusted.

                - Trusted publisher without any verification errors.

                - UI was disabled in dwUIChoice. No publisher or 
                    time stamp chain errors.

                - UI was enabled in dwUIChoice and the user clicked 
                    "Yes" when asked to install and run the signed 
                    subject.
            */
            DebugString(L"The file \"%s\" is signed and the signature was verified.",pcwszArcFilePath);
			NeedToAskUser=true;
            break;
        
        case TRUST_E_NOSIGNATURE:
            // The file was not signed or had a signature 
            // that was not valid.
            // Get the reason for no signature.
            dwLastError = GetLastError();
            if (TRUST_E_NOSIGNATURE == dwLastError ||
                    TRUST_E_SUBJECT_FORM_UNKNOWN == dwLastError ||
                    TRUST_E_PROVIDER_UNKNOWN == dwLastError) 
            {
                // The file was not signed.
				DebugString(L"The file \"%s\" is not signed.",pcwszArcFilePath);
            } 
            else 
            {
                // The signature was not valid or there was an error 
                // opening the file.
				DebugString(L"An unknown error occurred trying to verify the signature of the \"%s\" file.",pcwszArcFilePath);
				NeedToAskUser=true;
            }
            break;

        case TRUST_E_EXPLICIT_DISTRUST:
            // The hash that represents the subject or the publisher 
            // is not allowed by the admin or user.
			DebugString(L"The signature is present in this file \"%s\", but specifically disallowed.",pcwszArcFilePath);
			NeedToAskUser=true;
            break;

        case TRUST_E_SUBJECT_NOT_TRUSTED:
            // The user clicked "No" when asked to install and run.
			DebugString(L"The signature is present in this file \"%s\", but not trusted.",pcwszArcFilePath);
			NeedToAskUser=true;
            break;

        case CRYPT_E_SECURITY_SETTINGS:
            /*
            The hash that represents the subject or the publisher 
            was not explicitly trusted by the admin and the 
            admin policy has disabled user trust. No signature, 
            publisher or time stamp errors.
            */
			DebugString(L"CRYPT_E_SECURITY_SETTINGS - The hash "
                L"representing the subject or the publisher wasn't "
                L"explicitly trusted by the admin and admin policy "
                L"has disabled user trust. No signature, publisher "
                L"or timestamp errors.\n");
			NeedToAskUser=true;
            break;

        default:
            // The UI was disabled in dwUIChoice or the admin policy 
            // has disabled user trust. lStatus contains the 
            // publisher or time stamp chain error.
            DebugString(L"The signature may present in this file \"%s\", but verify error 0x%x.", pcwszArcFilePath,rFileSignature);
			NeedToAskUser=true;
            break;
    }

	if(NeedToAskUser)
	{
		CSysString t=CSysString(TranslateDef (0, L"7Zip plugins")); 
		CSysString s=CSysString(pcwszArcFilePath);
		s+=L"\n\n";
		s+=CSysString(TranslateDef (104,			// <2011-06-28> new ID 104
			L"The packed file may contain digital signature!\n"
			L"Update it will destroy signature information.\n"
			L"Do you still want to update this packed file?")); 
		s.Replace(L"\\n",L"\n"); // let translater add \n for new line.
		int r=MessageBox(NULL/*GetConsoleWindow()*/,s,t,MB_TASKMODAL/*MB_APPLMODAL*/|MB_YESNO|MB_ICONWARNING);
		if(r==IDYES) return S_OK;	// continue update the packed file.
		return E_ABORT;
	}
	return S_OK;
}

DWORD __stdcall ExtractThread(LPVOID empty)
{
	PZHANDLE handle = (PZHANDLE)empty;
	int folderIndex = handle->db->FileIndexToFolderIndexMap[handle->iItem];
	if(folderIndex != -1) {
		UInt32 packStreamIndex = handle->db->FolderStartPackStreamIndex[folderIndex];
		UInt64 folderStartPackPos = handle->db->GetFolderStreamPos(folderIndex, 0);
		const NArchive::N7z::CFolder &folderInfo = handle->db->Folders[folderIndex];
		UInt32 startIndex = (UInt32)handle->db->FolderStartFileIndex[folderIndex];

		C7zOutStream *outStreamSpec = new C7zOutStream(handle, startIndex);
		CMyComPtr<ISequentialOutStream> outStream(outStreamSpec);
		CMyComPtr<ICompressProgressInfo> progressInfo(outStreamSpec);
		DebugString(L"(0x%X) ExtractThread start: %d, item: %d", GetCurrentThreadId(), startIndex, handle->iItem); // <2011-06-21> add ThreadID

		NArchive::N7z::CDecoder decoder(true);
		bool passwordIsDefined;
		HRESULT result = decoder.Decode(
			handle->inStream,
			folderStartPackPos,
			&handle->db->PackSizes[packStreamIndex],
			folderInfo,
			outStream,
			progressInfo, 
			handle->password, passwordIsDefined,
			true, NWindows::NSystem::GetNumberOfProcessors ()
			);
		DebugString(L"(0x%X) Decode Result %d/OK=%d", GetCurrentThreadId(),result,S_OK); // <2011-06-21> hint: happen once for each block (not each file) because it wait and stop at .Write() for each file, or happen when the Decode() is error!!
		// Mark we had a problem with this stream / file to read error / if not marked earlier
		if(result != S_OK && !handle->iResult)
		{
			if (result == E_ABORT)  
				handle->iResult = E_EABORTED;
			else if (result == E_OUTOFMEMORY)
				handle->iResult = E_NO_MEMORY;
			else
				handle->iResult = E_EREAD;

			handle->password->_passwordIsDefined = false;
		}
	}
	DebugString(L"(0x%X) ExtractThread SetEvent(handle->hPullEvent)", GetCurrentThreadId()); // <2011-06-21> add ThreadID
	handle->hThread = NULL; 	  // ^^
	SetEvent(handle->hPullEvent); // This order is very important

	return 0;
}

BOOL Extract(PZHANDLE handle)
{
	int folderIndex = handle->db->FileIndexToFolderIndexMap[handle->iItem];
	if(folderIndex == -1) // This might happen when filesize is 0
	{
		COutFileStream zeroStream;
		zeroStream.Create(handle->fileName, true);
		// if(handle->db->MTime.Defined[handle->iItem])
		// 	zeroStream.SetMTime((const FILETIME *)&handle->db->MTime.Values[handle->iItem]);
		zeroStream.Close();
		// <2011-04-14> restore all time info for empty file
		FILETIME *atp=NULL, *ctp=NULL, *mtp=NULL;
		if((handle->db->CTime.Defined.Size() > (int)handle->iItem) && handle->db->CTime.Defined[handle->iItem])
			ctp = (FILETIME *)&handle->db->CTime.Values[handle->iItem];
		if((handle->db->ATime.Defined.Size() > (int)handle->iItem) && handle->db->ATime.Defined[handle->iItem])
			atp = (FILETIME *)&handle->db->ATime.Values[handle->iItem];
		if((handle->db->MTime.Defined.Size() > (int)handle->iItem) && handle->db->MTime.Defined[handle->iItem])
			mtp = (FILETIME *)&handle->db->MTime.Values[handle->iItem];
		NWindows::NFile::NDirectory::SetDirTime(handle->fileName, ctp, atp, mtp);
		NWindows::NFile::NDirectory::MySetFileAttributes(handle->fileName,
			handle->db->Files[handle->iItem].Attrib);
		return TRUE;
	}

	UInt32 startIndex = (UInt32)handle->db->FolderStartFileIndex[folderIndex];
	UInt64 written = handle->written;

	if(!handle->hThread)
	{
		ResetEvent(handle->hPushEvent); ResetEvent(handle->hPullEvent);
		DWORD dwThreadId = 0;
		DebugString(L"Extract CreateThread");
		written = handle->written = 0;
		// if(!(handle->hThread = CreateThread(NULL, 0, ExtractThread, (LPVOID)handle, 0, &dwThreadId)))
		if(!(handle->hThread = CreateThread(NULL, 0, ExtractThread, (LPVOID)handle, CREATE_SUSPENDED, &dwThreadId)))
		{
			DebugString(L"Extract Cannot create new thread. Exiting extraction...");
			return FALSE;
		}
		DebugString(L"Extract CreateThread ThreadID=0x%X",dwThreadId);
		ResumeThread(handle->hThread); // <2011-06-21> 
		CloseHandle(handle->hThread); // <2011-06-20> close thread handle right after CreateThread?!
	}
	else
	{
		DebugString(L"Extract SetEvent(handle->hPushEvent) (thread is running)");
		SetEvent(handle->hPushEvent);
	}
	DebugString(L"Extract WaitForSingleObject(handle->hPullEvent, 200)");
	while(WaitForSingleObject(handle->hPullEvent, 200) == WAIT_TIMEOUT) {
		if((handle->written - written) && handle->procProcess)
		{
			// Break if user pressed cancel
			if(!handle->procProcess(handle->fileName, (int)(handle->written - written)))
				handle->iResult = E_EABORTED;
			written = handle->written;
		}
		else if(!handle->written && handle->procProcess && !handle->procProcess(NULL, 0))
			handle->iResult = E_EABORTED;
		if(handle->iResult == E_EABORTED) // <2011-06-21> break waiting status
		{
			return FALSE;
		}
	}

	// Show final 100%
	if(!handle->iResult && handle->procProcess) 
		handle->procProcess(handle->fileName, (int)(handle->written - written));
	DebugString(L"Extract ResetEvent(_handle->hPullEvent)");
	ResetEvent(handle->hPullEvent);
	return TRUE;
}

CObjectVector<C7zPassword *> g_passwords;

BOOL APIENTRY DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved)
{
	switch(reason)
	{
	case DLL_PROCESS_ATTACH:
		g_hInstance = hInst;
		break;
	case DLL_PROCESS_DETACH:
		while(g_passwords.Size() > 0)
		{
			g_passwords[0]->Release();
			g_passwords.Delete(0);
		}
		break;
	default:
		break;
	}
	return TRUE;
}

void ZDeleteHandle(PZHANDLE handle);

C7zPassword *ZNewPassword(const wchar_t* arcName, bool bLookupOnly = false)
{
	// Are we really thread safe here ?
	while(g_passwords.Size() > MAX_PASSWORDS)
	{
		// Last process should have still the handle there
		g_passwords[0]->Release();
		g_passwords.Delete(0);
	}
	for(int i = 0; i < g_passwords.Size(); i++)
		if(g_passwords[i]->Matches(arcName))
		{
			g_passwords[i]->AddRef();
			return g_passwords[i];
		}

		if(bLookupOnly)	return NULL;

		C7zPassword *passwordSpec = new C7zPassword(arcName);
		// Add reference so the password is not killed on destroy
		passwordSpec->AddRef();
		g_passwords.Add(passwordSpec);
		return passwordSpec;
}

void ZDeletePassword(C7zPassword *passwordSpec)
{
	if(passwordSpec->Release())
	{
		int pos = g_passwords.Find(passwordSpec);
		if(pos >= 0)
			g_passwords.Delete(pos);
	}
}

PZHANDLE ZNewHandle(const wchar_t* arcName)
{
	PZHANDLE handle = new ZHANDLE;
	ZeroMemory(handle, sizeof(ZHANDLE));
	// if(!(handle->hPushEvent = CreateEvent(NULL, TRUE, FALSE, NULL)) ||
	// 	!(handle->hPushEvent = CreateEvent(NULL, TRUE, FALSE, NULL)))
	handle->hPushEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	handle->hPullEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if(handle->hPushEvent == NULL
	|| handle->hPullEvent == NULL)
	{
		DebugString(L"ZNewHandle Cannot create event handles.");
		ZDeleteHandle(handle);
		return NULL;
	}
	C7zPassword *passwordSpec = ZNewPassword(arcName);
	handle->password = passwordSpec;
	// handle->hPullEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	return handle;
}

void ZDeleteHandle(PZHANDLE handle)
{
	if(!handle) return;
	if(handle->inStream) handle->inStream.Release();
	if(handle->db) delete handle->db;
	if(handle->archiveName) free(handle->archiveName);
	if(handle->hPushEvent) CloseHandle(handle->hPushEvent);
	if(handle->hPullEvent) CloseHandle(handle->hPullEvent);
	delete handle;
}

HANDLE __stdcall OpenArchive(tOpenArchiveData *archiveData)
{
	DebugString(L"Only Unicode version supported.");
	return 0;
}

HANDLE __stdcall OpenArchiveW(tOpenArchiveDataW *archiveData)
{
	if(!archiveData) return NULL;

	// Check if we have valid filename
	if(!archiveData->ArcName)
	{
		archiveData->OpenResult = E_BAD_DATA;
		return NULL;
	}

	PZHANDLE handle = ZNewHandle(archiveData->ArcName);

	// Check if we got valid handle
	if(!handle)
	{
		archiveData->OpenResult = E_NO_MEMORY;
		return NULL;
	}

	try
	{
		// Try to open archive file name
		CInFileStream *inStream = new CInFileStream();
		if(!inStream || !inStream->Open(archiveData->ArcName))
		{
			archiveData->OpenResult = E_EOPEN;
			delete inStream;
			ZDeleteHandle(handle);
			return NULL;
		}

		handle->inStream = inStream;
		NArchive::N7z::CInArchive archive;
		HRESULT result;
		if((result = archive.Open(handle->inStream, &gMaxCheckStartPosition)))
		{
			archiveData->OpenResult = E_UNKNOWN_FORMAT;
			ZDeleteHandle(handle);
			return NULL;
		}
		handle->db = new NArchive::N7z::CArchiveDatabaseEx;
		bool passwordIsDefined;
		if((result = archive.ReadDatabase(*handle->db, handle->password, passwordIsDefined)) != S_OK)
		{
			handle->password->_passwordIsDefined = false;
			archiveData->OpenResult = E_UNKNOWN_FORMAT;
			ZDeleteHandle(handle);
			return NULL;
		}
		handle->db->Fill();
		// <2011-04-12> by dllee, read settings : reorder directory list , to fix dir datetime error in TC (when SortDirsByName=1)
		COMPSETTINGS cs;
		ReadSettings(&cs);
		DebugString(L"Reorder Directory List : %s", !!(cs.flags & FLAG_REORDER_DIRECTORY_LIST) ? L"YES" : L"NO");
		if(!!(cs.flags & FLAG_REORDER_DIRECTORY_LIST))
		{
			int numFiles=handle->db->Files.Size();
			UInt64 tTime;
			std::vector<TmpDIRITEM> vDir;
			std::vector<int> vIndex;
			TmpDIRITEM tDir;
			vDir.reserve(numFiles);
			vIndex.reserve(numFiles);
			for(int i=0;i<numFiles;i++)
			{
				if(handle->db->Files[i].IsDir)
				{
					tDir.index=i;
					tDir.fileitem=handle->db->Files[i];
					DebugString(L"Item[%d] : %s", i, tDir.fileitem.Name);
					if(handle->db->CTime.Defined.Size()>i)
					{
						tDir.cTimeDefined=handle->db->CTime.GetItem(i,tTime);
						tDir.cTime=tTime;
					}
					else
						tDir.cTimeDefined=false;
					if(handle->db->ATime.Defined.Size()>i)
					{
						tDir.aTimeDefined=handle->db->ATime.GetItem(i,tTime);
						tDir.aTime=tTime;
					}
					else
						tDir.aTimeDefined=false;
					if(handle->db->MTime.Defined.Size()>i)
					{
						tDir.mTimeDefined=handle->db->MTime.GetItem(i,tTime);
						tDir.mTime=tTime;
					}
					else
						tDir.mTimeDefined=false;
					vDir.push_back(tDir);
					vIndex.push_back(i);
				}
			}
			sort(vDir.begin(),vDir.end(),TmpDirItemSortFx);
			for(UInt32 i=0;i<vIndex.size();i++)
			{
				handle->db->Files[vIndex[i]]=vDir[i].fileitem;
				DebugString(L"Item[%d] : %s", vIndex[i], vDir[i].fileitem.Name);
				if(handle->db->CTime.Defined.Size()>vIndex[i])
					handle->db->CTime.SetItem(vIndex[i],vDir[i].cTimeDefined,vDir[i].cTime);
				if(handle->db->ATime.Defined.Size()>vIndex[i])
					handle->db->ATime.SetItem(vIndex[i],vDir[i].aTimeDefined,vDir[i].aTime);
				if(handle->db->MTime.Defined.Size()>vIndex[i])
					handle->db->MTime.SetItem(vIndex[i],vDir[i].mTimeDefined,vDir[i].mTime);
			}
			vDir.clear();
			vIndex.clear();
		}
	}
	catch(...)
	{
		archiveData->OpenResult = E_UNKNOWN_FORMAT;
		ZDeleteHandle(handle);
		return NULL;
	}

	handle->archiveName = wcsdup(archiveData->ArcName);
	archiveData->OpenResult = 0;
	DebugString(L"OpenArchive return handle");
	return (HANDLE) handle;
}

int __stdcall ReadHeader(HANDLE hArcData, tHeaderData *headerData)
{
	PZHANDLE handle = (PZHANDLE)hArcData;
	if(!handle || !handle->db) return E_BAD_DATA;

	if(handle->iItem >= (UInt32)handle->db->Files.Size())
	{
		DebugString(L"ReadHeader END");
		handle->iItem = 0;
		return E_END_ARCHIVE;
	}

	// There is no tHeaderDataW so we should not care about Unicode

	// Lookup name
	strncpy(headerData->FileName, UnicodeStringToMultiByte(NArchive::NItemName::GetOSName(handle->db->Files[handle->iItem].Name)), 260);
	strncpy(headerData->ArcName, UnicodeStringToMultiByte(handle->archiveName), 260);

	// Attributes
	headerData->FileAttr = handle->db->Files[handle->iItem].IsDir ? 0x10 : 0;
	if (handle->db->Files[handle->iItem].AttribDefined)
		headerData->FileAttr |= ((handle->db->Files[handle->iItem].Attrib & FILE_ATTRIBUTE_READONLY) ? 0x1 : 0)
		| ((handle->db->Files[handle->iItem].Attrib & FILE_ATTRIBUTE_HIDDEN) ? 0x2 : 0)
		| ((handle->db->Files[handle->iItem].Attrib & FILE_ATTRIBUTE_SYSTEM) ? 0x4 : 0)
		| ((handle->db->Files[handle->iItem].Attrib & FILE_ATTRIBUTE_ARCHIVE) ? 0x20 : 0);

	// Packed size
	int folderIndex = handle->db->FileIndexToFolderIndexMap[handle->iItem];
	if(folderIndex >= 0)
		headerData->PackSize = (int)handle->db->GetFolderFullPackSize(folderIndex);
	else
		headerData->PackSize = 0;

	// File size
	headerData->UnpSize = (int)handle->db->Files[handle->iItem].Size;
	headerData->FileCRC = handle->db->Files[handle->iItem].CrcDefined ? handle->db->Files[handle->iItem].Crc : 0;

	// File time
	if (handle->iItem < (UInt32)handle->db->MTime.Defined.Size () &&
		handle->db->MTime.Defined[handle->iItem]) {
			FILETIME ft;
			SYSTEMTIME st;
			FileTimeToLocalFileTime((const FILETIME *)&handle->db->MTime.Values[handle->iItem], &ft);
			FileTimeToSystemTime(&ft, &st);
			headerData->FileTime = (st.wYear - 1980) << 25 | st.wMonth << 21 | st.wDay << 16 | st.wHour << 11 | st.wMinute << 5 | st.wSecond / 2;
	}
	else
		headerData->FileTime = 1 << 21 | 1 << 16; // <2011-04-13> 1980-01-01 00:00:00

	DebugString(L"ReadHeader return 0");
	return 0;
}

int __stdcall ReadHeaderEx(HANDLE hArcData, tHeaderDataEx *headerDataEx)
{
	DebugString(L"Only Unicode version supported.");
	return 0;
}

int __stdcall ReadHeaderExW(HANDLE hArcData, tHeaderDataExW *headerDataEx)
{
	UInt64 val64;
	PZHANDLE handle = (PZHANDLE)hArcData;
	if(!handle || !handle->db) return E_BAD_DATA;

	if(handle->iItem >= (UInt32)handle->db->Files.Size())
	{
		DebugString(L"ReadHeaderEx END");
		handle->iItem = 0;
		return E_END_ARCHIVE;
	}

	memset (headerDataEx->Reserved, 0, 1024);
	// Lookup name
	wcsncpy(headerDataEx->FileName, NArchive::NItemName::GetOSName(handle->db->Files[handle->iItem].Name), 260);
	wcsncpy(headerDataEx->ArcName, handle->archiveName, 1024);

	// Attributes
	headerDataEx->FileAttr = handle->db->Files[handle->iItem].IsDir ? 0x10 : 0;
	if (handle->db->Files[handle->iItem].AttribDefined)
		headerDataEx->FileAttr |= ((handle->db->Files[handle->iItem].Attrib & FILE_ATTRIBUTE_READONLY) ? 0x1 : 0)
		| ((handle->db->Files[handle->iItem].Attrib & FILE_ATTRIBUTE_HIDDEN) ? 0x2 : 0)
		| ((handle->db->Files[handle->iItem].Attrib & FILE_ATTRIBUTE_SYSTEM) ? 0x4 : 0)
		| ((handle->db->Files[handle->iItem].Attrib & FILE_ATTRIBUTE_ARCHIVE) ? 0x20 : 0);

	// Packed size
	int folderIndex = handle->db->FileIndexToFolderIndexMap[handle->iItem];
	if(folderIndex >= 0)
		val64 = handle->db->GetFolderFullPackSize (folderIndex);
	else
		val64 = 0;

	headerDataEx->PackSize = (unsigned int)val64;
	headerDataEx->PackSizeHigh = (unsigned int)(val64 >> 32);

	// File size
	val64 = handle->db->Files[handle->iItem].Size;
	headerDataEx->UnpSize = (unsigned int)val64;
	headerDataEx->UnpSizeHigh = (unsigned int)(val64 >> 32);
	headerDataEx->FileCRC = handle->db->Files[handle->iItem].CrcDefined ? handle->db->Files[handle->iItem].Crc : 0;

	// File time
	if (handle->iItem < (UInt32)handle->db->MTime.Defined.Size () && 
		handle->db->MTime.Defined[handle->iItem]) {
			FILETIME ft;
			SYSTEMTIME st;
			FileTimeToLocalFileTime((const FILETIME *)&handle->db->MTime.Values[handle->iItem], &ft);
			FileTimeToSystemTime(&ft, &st);
			headerDataEx->FileTime = (st.wYear - 1980) << 25 | st.wMonth << 21 | st.wDay << 16 | st.wHour << 11 | st.wMinute << 5 | st.wSecond / 2;
	}
	else
		headerDataEx->FileTime = 1 << 21 | 1 << 16; // <2011-04-13> 1980-01-01 00:00:00

	DebugString(L"ReadHeaderEx return 0");
	return 0;   
}

int __stdcall ProcessFile(HANDLE hArcData, int operation, LPSTR destPath, LPSTR destName)
{
	DebugString(L"Only Unicode version supported.");
	return 0;
}

int __stdcall ProcessFileW(HANDLE hArcData, int operation, wchar_t* destPath, wchar_t* destName)
{
	PZHANDLE handle = (PZHANDLE)hArcData;
	if(!handle || !handle->db) return E_BAD_DATA;

	handle->fileName = destName;
	handle->iResult = E_NOT_SUPPORTED;
	handle->iOperation = operation;
	switch(operation)
	{
	case PK_TEST:
		handle->iResult = 0;
		DebugString(L"ProcessFile TEST, \"%s\", \"%s\"", destPath, destName);
		Extract(handle);
		break;
	case PK_EXTRACT:
		handle->iResult = 0;
		DebugString(L"ProcessFile EXTRACT, \"%s\", \"%s\"", destPath, destName);
		Extract(handle);
		break;
	case PK_SKIP:
		handle->iResult = 0;
		DebugString(L"ProcessFile SKIP, \"%s\", \"%s\"", destPath, destName);
		if(handle->hThread && handle->hPullEvent)
		{
			SetEvent(handle->hPushEvent);
			DebugString(L"ProcessFile WaitForSingleObject(handle->hPullEvent, INFINITE)");
			WaitForSingleObject(handle->hPullEvent, INFINITE);
			ResetEvent(handle->hPullEvent);
		}
		break;
	}

	handle->iItem ++;
	return handle->iResult;
}

int __stdcall CloseArchive(HANDLE hArcData)
{
	PZHANDLE handle = (PZHANDLE)hArcData;
	handle->iItem = handle->db->Files.Size();

	if(handle->hPushEvent)
	{
		DebugString(L"CloseArchive SetEvent(handle->hPushEvent)");
		SetEvent(handle->hPushEvent);
	}
	// If thread is running stop it !
	if(handle->hThread && handle->hPullEvent)
	{
		// Set we want to abort rest of the extraction
		handle->iResult = E_EABORTED;
		DebugString(L"CloseArchive WaitForSingleObject(handle->hPullEvent, INFINITE)");
		WaitForSingleObject(handle->hPullEvent, INFINITE);
	}
	DebugString(L"CloseArchive return 0");

	ZDeleteHandle((PZHANDLE)hArcData);
	return 0;
}

int __stdcall GetPackerCaps()
{
	return PK_CAPS_MULTIPLE | PK_CAPS_NEW | PK_CAPS_MODIFY | PK_CAPS_BY_CONTENT | PK_CAPS_OPTIONS | PK_CAPS_DELETE | PK_CAPS_SEARCHTEXT;
}

void __stdcall SetChangeVolProc(HANDLE hArcData, tChangeVolProc pChangeVolProc)
{
	DebugString(L"Only Unicode version supported.");
}

void __stdcall SetChangeVolProcW(HANDLE hArcData, tChangeVolProcW pChangeVolProc)
{
}

void __stdcall SetProcessDataProc(HANDLE hArcData, tProcessDataProc pProcessDataProc)
{
	DebugString(L"Only Unicode version supported.");
}

void __stdcall SetProcessDataProcW(HANDLE hArcData, tProcessDataProcW pProcessDataProc)
{
	PZHANDLE handle = (PZHANDLE)hArcData;
	if(hArcData == (HANDLE)-1 || !handle)
		procProcess = pProcessDataProc;
	else
		handle->procProcess = pProcessDataProc;
}

////////////////////////////////////////////////////////////////////////////////

class C7zUpdateCallback:
	public IArchiveUpdateCallback,
	public ICryptoGetTextPassword2,
	public CMyUnknownImp
{
public:
#if !(MY_VER_MAJOR == 4 && MY_VER_MINOR >= 47 || MY_VER_MAJOR > 4)
	MY_UNKNOWN_IMP
#else
	MY_UNKNOWN_IMP2(
		IArchiveUpdateCallback,
		ICryptoGetTextPassword2
		);
#endif

	// IProgress
	STDMETHOD(SetTotal)(UInt64 size)
	{
		_total = size;
		_total_inv100=(1.f / (float)(_total + 1)) * 100.f; // <2011-05-17>
		return S_OK;
	}

	STDMETHOD(SetCompleted)(const UInt64 *completeValue)
	{
		// float percent = (*completeValue / (float)(_total + 1)) * 100.f;
		float percent = (*completeValue) * _total_inv100; // <2011-05-17>

		wchar_t* fileName = NULL;
		if (!_lastFile.IsEmpty ())
			fileName = _lastFile.GetBuffer (1);
		else
			fileName = _packedFile.GetBuffer (1);

		if(!procProcess(fileName, -(int)percent))
		{
			if(_pbAbort) *_pbAbort = TRUE;
			return E_ABORT;
		}

		return S_OK;
	}

	// IUpdateCallback
	STDMETHOD(EnumProperties)(IEnumSTATPROPSTG **enumerator) { return S_OK; }
	STDMETHOD(GetUpdateItemInfo)(UInt32 index,
		Int32 *newData, Int32 *newProperties, UInt32 *indexInArchive) { return S_OK; }
	STDMETHOD(GetProperty)(UInt32 index, PROPID propID, PROPVARIANT *value) { return S_OK; }
	STDMETHOD(GetStream)(UInt32 index, ISequentialInStream **inStream)
	{
		DebugString(L"C7zUpdateCallback::GetStream start.");
		CInFileStream *inStreamSpec = new CInFileStream;
		CMyComPtr<ISequentialInStream> inStreamLoc(inStreamSpec);
		_lastFile = _srcPath + (*_files)[index];
		DebugString(L"C7zUpdateCallback::GetStream index: %d, fileName: \"%s\"", index, (const wchar_t*)_lastFile);
		// if(!inStreamSpec->Open(_lastFile))
		while(!inStreamSpec->OpenShared(_lastFile,true)) // <2011-05-05> If a file is open by another app, the file is still can be packed by 7zip.
		{	// <2011-05-06> add retry dialog
			CSysString t=CSysString(TranslateDef (0, L"7Zip plugins")); // <2011-05-17>
			CSysString s=CSysString(TranslateDef (103, L"The file is using by another application."));
			s+=L"\n\n";
			s+=_lastFile;
			int r=MessageBox(NULL/*GetConsoleWindow()*/,s,t,MB_TASKMODAL/*MB_APPLMODAL*/|MB_RETRYCANCEL|MB_ICONWARNING); // MB_ABORTRETRYIGNORE
			if(r==IDRETRY) continue; // retry
			// if(r==IDABORT)
			*inStream = NULL;
			// Abort
			*_pbAbort = TRUE;
			return E_ABORT;
			// return E_FAIL;
		}
		_inFileStream = inStreamLoc;
		*inStream = inStreamLoc.Detach();
		return S_OK;
	}
	STDMETHOD(SetOperationResult)(Int32 operationResult)
	{
		_inFileStream.Release();
		DebugString(L"C7zUpdateCallback::SetOperationResult released.");
		return S_OK;
	}

	STDMETHOD(CryptoGetTextPassword2)(INT32 *passwordIsDefined, BSTR *password)
	{
		DebugString(L"C7zUpdateCallback::CryptoGetTextPassword2");
		if(!_password) return E_FAIL;
		HRESULT hResult = _password->CryptoGetTextPassword(password);
		if(hResult == S_OK)
			*passwordIsDefined = BoolToInt(true);
		DebugString(L"C7zUpdateCallback::CryptoGetTextPassword2 %s.", (hResult == S_OK) ? L"S_OK" : L"FAILED");
		return hResult;
	}

public:
	CObjectVector<CSysString> *_files;
	CMyComPtr<ISequentialInStream> _inFileStream;
	CSysString _srcPath;
	CSysString _lastFile;
	CSysString _packedFile;
	UInt64 _total;
	float _total_inv100; // <2011-05-17>
	BOOL *_pbAbort;
	C7zPassword *_password;

	C7zUpdateCallback(CObjectVector<CSysString> *files,
		wchar_t* srcPath, wchar_t* packedFile, BOOL *pbAbort, C7zPassword *password)
	{
		_password = password;
		_files = files;
		_srcPath = srcPath ? srcPath : L"";
		_packedFile = packedFile;
		DebugString(L"C7zUpdateCallback::constructor srcPath: %s", _srcPath);
		_pbAbort = pbAbort;
	}
	~C7zUpdateCallback()
	{
		DebugString(L"C7zUpdateCallback::destructor");
	}
};

int Update(wchar_t* packedFile, wchar_t* subPath, wchar_t* srcPath, wchar_t* addList, wchar_t* deleteList, int flags)
{
	TCHAR tempName[MAX_PATH];

	// Read settings
	COMPSETTINGS cs;
	ReadSettings(&cs);

	// Try to open archive file name
	tOpenArchiveDataW arcData = {0};
	arcData.ArcName = packedFile;

	CObjectVector<NArchive::N7z::CUpdateItem> updateItems;
	CObjectVector<CSysString> files;
	CObjectVector<CSysString> delPatterns;
	wchar_t* iName = addList;
	int iNum = 0;
	while(iName && *iName)
	{
		int nameLen = (int)wcslen(iName);
		// Check if it's folder and then delete empty folder
		if(iName[nameLen - 1] == L'\\') iName[nameLen - 1] = 0;
		// Create new file name
		CSysString fileName(srcPath); fileName += iName;
		NWindows::NFile::NFind::CFileInfo fileInfo;
		if(fileInfo.Find(fileName))
		{
			UString newName;

			// Create valid archive filename
			if(subPath)
			{
				newName = subPath;
				newName += L'/';
				newName += iName;
			}
			else
				newName = iName;

			// Add file to file list
			files.Add(iName);

			// Create new update item
			NArchive::N7z::CUpdateItem updateItem;
			updateItem.NewProps = true;
			updateItem.NewData = fileInfo.IsDir() ? false : true;
			updateItem.IndexInArchive = -1;
			updateItem.IsAnti = false;
			updateItem.Size = fileInfo.Size;
			updateItem.Name = NArchive::NItemName::MakeLegalName(newName);
			updateItem.IsDir = fileInfo.IsDir();

			updateItem.Attrib = fileInfo.Attrib;
			if (!(cs.flags & FLAG_SAVE_ATTR_READONLY))
				updateItem.Attrib &= ~FILE_ATTRIBUTE_READONLY;
			if (!(cs.flags & FLAG_SAVE_ATTR_HIDDEN))
				updateItem.Attrib &= ~FILE_ATTRIBUTE_HIDDEN;
			if (!(cs.flags & FLAG_SAVE_ATTR_SYSTEM))
				updateItem.Attrib &= ~FILE_ATTRIBUTE_SYSTEM;
			if (!(cs.flags & FLAG_SAVE_ATTR_ARCHIVE))
				updateItem.Attrib &= ~FILE_ATTRIBUTE_ARCHIVE;
			updateItem.AttribDefined = updateItem.Attrib > 0;

			updateItem.CTimeDefined = !!(cs.flags & FLAG_SAVE_ATTR_CREATE_TIME);
			if (updateItem.CTimeDefined)
				updateItem.CTime = *((UInt64*)&fileInfo.CTime);
			updateItem.MTimeDefined = !!(cs.flags & FLAG_SAVE_ATTR_MODIFY_TIME);
			if (updateItem.MTimeDefined)
				updateItem.MTime = *((UInt64*)&fileInfo.MTime);
			updateItem.ATimeDefined = !!(cs.flags & FLAG_SAVE_ATTR_ACCESS_TIME);
			if (updateItem.ATimeDefined)
				updateItem.ATime = *((UInt64*)&fileInfo.ATime);

			DebugString(L"UpdateItem: Index: %d, Name: %s, Size: %d, Attibutes: %d",
				iNum, (wchar_t*)(const wchar_t*)fileName, fileInfo.Size, fileInfo.Attrib);

			updateItem.IndexInClient = iNum ++;
			updateItems.Add(updateItem);

		}
		else if(flags & PK_PACK_SAVE_PATHS) // dont now know what to do in this case...creating empty directories in archive
		{
			NArchive::N7z::CUpdateItem updateItem;
			UString askedPath, dirName;
			SYSTEMTIME st;
			FILETIME ft;

			GetSystemTime (&st); 
			SystemTimeToFileTime (&st, &ft);

			askedPath = NArchive::NItemName::MakeLegalName(iName);
			if (askedPath[askedPath.Length() - 1] != L'/')
				askedPath += L'/';

			updateItem.NewProps = true;
			updateItem.NewData = false;
			updateItem.IsDir = true;
			updateItem.IndexInArchive = -1;
			updateItem.IsAnti = false;
			updateItem.Size = 0;

			updateItem.Attrib = 0;
			updateItem.AttribDefined = false;
			updateItem.CTimeDefined = !!(cs.flags & FLAG_SAVE_ATTR_CREATE_TIME);
			if (updateItem.CTimeDefined)
				updateItem.CTime = *(UInt64*)&ft;
			updateItem.MTimeDefined = !!(cs.flags & FLAG_SAVE_ATTR_MODIFY_TIME);
			if (updateItem.MTimeDefined)
				updateItem.MTime = *(UInt64*)&ft;
			updateItem.ATimeDefined = !!(cs.flags & FLAG_SAVE_ATTR_ACCESS_TIME);
			if (updateItem.ATimeDefined)
				updateItem.ATime = *(UInt64*)&ft; 

			int slashPos = -1;
			for(;;)
			{
				slashPos = askedPath.Find (L'/', slashPos + 1);
				if (slashPos < 0) break;

				dirName = askedPath.Left (slashPos);
				updateItem.Name = dirName;

				updateItem.IndexInClient = iNum ++;
				updateItems.Add(updateItem);
			}
		}
		// Skip to the next file
		iName += nameLen + 1;
	}

	wchar_t* delName = deleteList;
	while(delName && *delName)
	{
		UString delPattern = NArchive::NItemName::MakeLegalName (delName);

		// total cmd uses "<dir>\*.*" to indicate that we need to delete entia directory
		// search mask "*.*" does not match files like "<dir>\1", "abracadabra" and etc
		// so we replace "*.*" with "*" to get something like this "<dir>\*"
		// also we need to add directory name to the list of deleted files
		bool matched = delPattern.Replace (L"*.*", L"*") > 0;
		delPatterns.Add (delPattern);
		if (matched)
			delPatterns.Add (delPattern.Left (delPattern.ReverseFind (L'/')));

		delName += wcslen (delName) + 1;
	}

	// Password
	C7zPassword *passwordSpec = ZNewPassword(packedFile);
	CMyComPtr<C7zPassword> password = passwordSpec;
	unsigned long passwordAsked = password->Asked();

	// Init streams
	COutFileStream *outStreamSpec = new COutFileStream;
	CMyComPtr<ISequentialOutStream> outStream(outStreamSpec);
	CInFileStream *inStreamSpec = new CInFileStream;
	CMyComPtr<IInStream> inStream;

	// Try to open existing archive
	NArchive::N7z::CArchiveDatabaseEx db;
	BOOL openOK = FALSE;
	if(inStreamSpec->OpenShared(packedFile, false))
	{
		NArchive::N7z::CInArchive archive;
		DebugString(L"7zUpdate opened file: %s", packedFile);
		if(archive.Open(inStreamSpec, &gMaxCheckStartPosition) == S_OK)
		{
			DebugString(L"7zUpdate opened archive: %s", packedFile);
			bool passwordIsDefined;
			if(archive.ReadDatabase(db, password, passwordIsDefined) == S_OK)
			{
				DebugString(L"7zUpdate reading db: %s", packedFile);
				db.Fill();

				int numFiles = db.Files.Size ();
				DebugString(L"7zUpdate checking %d files:", numFiles);
				for(int i = 0; i < numFiles; i++) {
					UString & FileNameInArchive = db.Files[i].Name;

					int ui;
					for (ui = 0; ui < updateItems.Size (); ui++) {
						// if (updateItems[ui].Name == FileNameInArchive) { // <2011-04-12> BUG Fixed FileName.Ext case problem
						if(MyStringCompareNoCase(updateItems[ui].Name,FileNameInArchive)==0) {
							updateItems[ui].IndexInArchive = i;
							DebugString(L"                  [upd] %s", db.Files[i].Name);
							break;
						}
					}
					if (ui == updateItems.Size ()) {
						int di;
						for (di = 0; di < delPatterns.Size (); di++) {
							if (wildcard_helper::test (FileNameInArchive, delPatterns[di])) {
								DebugString(L"                  [del] %s", FileNameInArchive);
								break;
							}
						}
						if (di == delPatterns.Size ()) {
							NArchive::N7z::CUpdateItem updateItem;
							updateItem.NewProps = false;
							updateItem.NewData = false;
							updateItem.IndexInArchive = i;
							updateItem.IndexInClient = iNum ++;
							updateItem.IsAnti = false;
							updateItems.Add(updateItem);
							DebugString(L"                  [old] %s", db.Files[i].Name);
						}
					}
				}

				openOK = TRUE;
				inStream = inStreamSpec;
			}
		}
	}

	// Check if we couldn't open archive even we have something in the delete list
	if(!openOK && deleteList) return 0;

	// SFX mode / check if the extension is .exe
	bool SFXmode = false;
	CInFileStream *sfxStreamSpec = NULL;
	SFXmode = !wcsicmp (packedFile + wcslen (packedFile) - 4, L".exe");
	int SFXsize = 0;
	bool useOldSFX = false;
	CMethodId SFXMethodId = 0;

	// find SFX-module
	if (SFXmode) {
		if (openOK) {
			useOldSFX = true;
			SFXsize = FindSFXModuleSize(inStream);
			SFXMethodId = FindSFXCompatibleMethodId(db, cs.method);
			UpdateSettings(SFXMethodId, cs.level, &cs);
		}
		else {
			if (*cs.password != 0)
				return E_NOT_SUPPORTED;

			FindSFXModules ();

			const wchar_t* sfxModule = NULL;
			sfxStreamSpec = new CInFileStream;

			if (!sfxStreamSpec->Open (sfxModule = BIN(cs.SFXModule))) {
				size_t i = 0;
				for (; i < g_knownSFXModules.size (); i++) {
					if (sfxStreamSpec->Open (g_knownSFXModules[i].c_str ()))
						break;
				}

				if (i == g_knownSFXModules.size ()) {
					DebugString(L"7zUpdate: cannot load SFX module %s", cs.SFXModule);
					delete sfxStreamSpec;
					return E_FAIL;
				}
				DebugString(L"7zUpdate: loading SFX module %s", sfxModule);
			}
		}
	}

	wcscpy_s (tempName, MAX_PATH, packedFile);
	wcscat_s (tempName, MAX_PATH, L".tmp");
	if (!outStreamSpec->Open (tempName, CREATE_ALWAYS))
		return E_ECREATE;

	if (SFXmode && useOldSFX) {
		UInt64 pos = 0;
		inStream->Seek(0, FILE_CURRENT, &pos);
		inStream->Seek(0, FILE_BEGIN, NULL);
		RINOK(CopyBlock(inStream, outStream, SFXsize));
		inStream->Seek(pos, FILE_BEGIN, NULL);
	}
	else if (SFXmode && sfxStreamSpec) {
		CMyComPtr<ISequentialOutStream> sfxOutStream;
		CMyComPtr<IInStream> sfxStream(sfxStreamSpec);
		sfxOutStream = outStream;
		RINOK(CopyBlock(sfxStream, sfxOutStream));

		// update icon
		RINOK(outStreamSpec->Close ());
		ReplaceIconResourceInPE (tempName, cs.SFXIconFile, cs.SFXIconIndex);
		if (!outStreamSpec->Open (tempName, OPEN_EXISTING))
			return E_ECREATE;
		RINOK(outStreamSpec->Seek (0, FILE_END, NULL));

		{
			CInFileStream *sfxConfigStreamSpec = new CInFileStream;
			if (sfxConfigStreamSpec->Open (BIN (L"config.txt"))) {
				CMyComPtr<IInStream> sfxConfigStream(sfxConfigStreamSpec);
				RINOK(CopyBlock(sfxConfigStream, sfxOutStream));
			}
		}
	}

	NArchive::N7z::CUpdateOptions options;
	NArchive::N7z::CMethodFull methodFull, methodHeader;
	NArchive::N7z::CCompressionMethodMode methodMode, headerMode;

	// Method for compressing header
	{
		static const UInt32 kLzmaAlgorithmX5 = 1;
		static const UInt32 kDictionaryForHeaders = 1 << 20;
		static const UInt32 kNumFastBytesForHeaders = 273;
		static const UInt32 kAlgorithmForHeaders = kLzmaAlgorithmX5;
		static const wchar_t *kLzmaMatchFinderForHeaders = L"BT2";

		methodHeader.NumInStreams = 1;
		methodHeader.NumOutStreams = 1;
		methodHeader.Id = g_compMethods[0].id;

		DECLARE_PROPERTY(property);
		ADD_PROPERTY(methodHeader, property, NCoderPropID::kMatchFinder, kLzmaMatchFinderForHeaders);
		ADD_PROPERTY(methodHeader, property, NCoderPropID::kAlgorithm, kAlgorithmForHeaders);
		ADD_PROPERTY(methodHeader, property, NCoderPropID::kNumFastBytes, UInt32(kNumFastBytesForHeaders));
		ADD_PROPERTY(methodHeader, property, NCoderPropID::kDictionarySize, UInt32(kDictionaryForHeaders));

		headerMode.Methods.Add(methodHeader);

		headerMode.NumThreads = 1;
		headerMode.PasswordIsDefined = false;

		if(cs.flags & FLAG_ENCRYPT_HEADER)
		{
			headerMode.PasswordIsDefined = (*cs.password != 0);
			if(headerMode.PasswordIsDefined)
				headerMode.Password = cs.password;
		}

		if(!headerMode.PasswordIsDefined && password->Asked() > passwordAsked && password->_passwordIsDefined)
		{
			headerMode.PasswordIsDefined = true;
			headerMode.Password = password->_password;
		}
	}

	// Method for compressing files
	{
// <2011-05-17> from 7z_src\CPP\7zip\Archive\Common\HandlerOut.cpp for default MatchFinder,Algo,Npass
static const wchar_t *kLzmaMatchFinderX1 = L"HC4";
static const wchar_t *kLzmaMatchFinderX5 = L"BT4";
static const UInt32 kLzmaAlgoX1 = 0;
static const UInt32 kLzmaAlgoX5 = 1;
static const UInt32 kBZip2NumPassesX1 = 1;
static const UInt32 kBZip2NumPassesX7 = 2;
static const UInt32 kBZip2NumPassesX9 = 7;

		methodFull.NumInStreams = 1;
		methodFull.NumOutStreams = 1;

		// We don't have store profile
		if (useOldSFX) {
			methodFull.Id = SFXMethodId;
		}
		else if(cs.level)
			methodFull.Id = cs.method;
		else {
			int i = 0; while(g_compMethods[i].name) i++;
			methodFull.Id = g_compMethods[i].id; // k_Copy
		}

		// Add LZMA properties
		if(cs.level && (cs.methodID == kLZMA || cs.methodID == kLZMA2)) {
			UInt32 algo = (cs.level >= 5 ? kLzmaAlgoX5 : kLzmaAlgoX1);								// <2011-05-17>
			const wchar_t *matchFinder = (cs.level >= 5 ? kLzmaMatchFinderX5 : kLzmaMatchFinderX1);	// <2011-05-17>
			DECLARE_PROPERTY(property);
			ADD_PROPERTY(methodFull, property, NCoderPropID::kNumFastBytes, UInt32(cs.word));
			ADD_PROPERTY(methodFull, property, NCoderPropID::kDictionarySize, UInt32(cs.dict));
			//ADD_PROPERTY(methodFull, property, NCoderPropID::kMultiThread, true);
			ADD_PROPERTY(methodFull, property, NCoderPropID::kNumThreads, UInt32(cs.numThreads));
			ADD_PROPERTY(methodFull, property, NCoderPropID::kAlgorithm, algo);				// <2011-05-17>
			ADD_PROPERTY(methodFull, property, NCoderPropID::kMatchFinder, matchFinder);	// <2011-05-17>
		}
		// Add PPMd properties
		else if(cs.level && cs.methodID == kPPMd) {
			DECLARE_PROPERTY(property);
			ADD_PROPERTY(methodFull, property, NCoderPropID::kOrder, UInt32(cs.word));
			ADD_PROPERTY(methodFull, property, NCoderPropID::kUsedMemorySize, UInt32(cs.dict));
		}
		// Add kBZip2 properties
		else if(cs.level && cs.methodID == kBZip2) {
			UInt32 numPasses = (cs.level >= 9 ? kBZip2NumPassesX9 :						// <2011-05-17>
					(cs.level >= 7 ? kBZip2NumPassesX7 :
					kBZip2NumPassesX1));
			DECLARE_PROPERTY(property);
			ADD_PROPERTY(methodFull, property, NCoderPropID::kNumPasses, numPasses);	// <2011-05-17>
			ADD_PROPERTY(methodFull, property, NCoderPropID::kDictionarySize, UInt32(cs.dict));
			ADD_PROPERTY(methodFull, property, NCoderPropID::kNumThreads, UInt32(cs.numThreads));
		}
		// Add kDeflate and kDeflate64 properties
		else if(cs.level && ((cs.methodID == kDeflate) || (cs.methodID == kDeflate64))) {
			UInt32 algo = (cs.level >= 5 ? kLzmaAlgoX5 : kLzmaAlgoX1);			// <2011-05-17>
			DECLARE_PROPERTY(property);
			ADD_PROPERTY(methodFull, property, NCoderPropID::kAlgorithm, algo);	// <2011-05-17>
			ADD_PROPERTY(methodFull, property, NCoderPropID::kNumFastBytes, UInt32(cs.word));
			ADD_PROPERTY(methodFull, property, NCoderPropID::kNumPasses, UInt32(cs.deflateNumPasses));
		}

		methodMode.Methods.Add(methodFull);

		methodMode.PasswordIsDefined = (*cs.password != 0);
		if(methodMode.PasswordIsDefined)
			methodMode.Password = cs.password;
		// Use defined password
		if(!methodMode.PasswordIsDefined && password->_passwordIsDefined)
		{
			methodMode.PasswordIsDefined = true;
			methodMode.Password = password->_password;
		}
		DebugString(L"Update: Password: %s, Using: %s", cs.password, methodMode.PasswordIsDefined ? L"yes" : L"no");

#ifdef COMPRESS_MT
		methodMode.NumThreads = cs.numThreads;
#endif
	}

	options.Method = &methodMode;
	options.HeaderMethod = &headerMode;
	options.UseFilters = cs.level != 0;
	options.MaxFilter = cs.level >= 8;
	options.HeaderOptions.CompressMainHeader = methodMode.PasswordIsDefined || updateItems.Size () > 1;
	options.SolidExtension = false;
	options.RemoveSfxBlock = false;
	options.VolumeMode = false; 

	UInt32 solidLogSize = cs.solidBlock;
	options.NumSolidBytes = 0;
	options.NumSolidFiles = 1;
	if (solidLogSize > 0 && solidLogSize != (UInt32)-1) {
		options.NumSolidBytes = (solidLogSize >= 64) ? (UInt64)(Int64)-1 : ((UInt64)1 << solidLogSize);
		options.NumSolidFiles = UInt64(Int64(-1));
	}

	options.HeaderOptions.WriteATime = !!(cs.flags & FLAG_SAVE_ATTR_ACCESS_TIME);
	options.HeaderOptions.WriteCTime = !!(cs.flags & FLAG_SAVE_ATTR_CREATE_TIME);
	options.HeaderOptions.WriteMTime = !!(cs.flags & FLAG_SAVE_ATTR_MODIFY_TIME);

	BOOL bAbort = FALSE;
	C7zUpdateCallback *updateCallbackSpec = new C7zUpdateCallback(&files, srcPath, packedFile, &bAbort, password);
	CMyComPtr<IArchiveUpdateCallback> updateCallback(updateCallbackSpec);
	NArchive::N7z::COutArchive archive;
	NArchive::N7z::CArchiveDatabase newDatabase;

	DebugString(L"NArchive::N7z::Update() start...");

	HRESULT hResult = NArchive::N7z::Update(
		openOK ? inStream : NULL,
		openOK ? &db : NULL,
		updateItems, 
		archive, 
		newDatabase,
		outStream, 
		updateCallback, 
		options,
		password);

	DebugString(L"NArchive::N7z::Update: %x", hResult);

	inStreamSpec->File.Close ();
	inStream.Release();
	DebugString(L"7zUpdate: Releasing stream in");

	updateItems.ClearAndFree ();
	if (hResult == S_OK)
		hResult = archive.WriteDatabase (newDatabase, options.HeaderMethod, options.HeaderOptions);

	outStreamSpec->Close ();
	outStream.Release();
	DebugString(L"7zUpdate: Releasing stream out");

	bool renameOk = true;
	if (hResult == S_OK) {
		::DeleteFile (packedFile);
		::MoveFile (tempName, packedFile);
	}
	else {
		::DeleteFile (tempName);
	}

	// If move files requested
	if(hResult == S_OK && (flags & PK_PACK_MOVE_FILES))
	{
		NWindows::NFile::NFind::CFileInfo fileInfo;
		// First remove files
		for(int i = 0; i < files.Size(); i++)
		{
			CSysString fileName(srcPath); fileName += files[i];
			if(fileInfo.Find(fileName) && !fileInfo.IsDir())
			{
				DebugString(L"7zUpdate: Deleting file %i/%i %s...", i + 1, files.Size(), fileName);
				::DeleteFile(fileName);
			}
		}
		// Then remove directories
		// (descending order needed to delete nested directories)
		for(int i = files.Size() - 1; i >= 0; i--)
		{
			CSysString fileName(srcPath); fileName += files[i];
			if(fileInfo.Find(fileName) && fileInfo.IsDir())
			{
				DebugString(L"7zUpdate: Removing folder %i/%i %s...", i + 1, files.Size(), fileName);
				::RemoveDirectory(fileName);
			}
		}
	}

	if(password)
		password->Matches(packedFile, true);
	DebugString(L"7zUpdate DONE, Result=%d",hResult);

	// <2011-04-12> by dllee, set 7z datetime to newest file
	DebugString(L"Set 7z datetime to newest file : %s", !!(cs.flags & FLAG_SET_7Z_DATE_TO_NEWEST) ? L"YES" : L"NO");
	if(hResult == S_OK && !!(cs.flags & FLAG_SET_7Z_DATE_TO_NEWEST))
	{
		// Try to open existing archive
		NArchive::N7z::CArchiveDatabaseEx db;
		UInt64 newestFileDate=0;
		UInt64 newestDirDate=0;
		UInt64 fDate;
		bool bFileFound=false;
		bool bDirFound=false;
		inStreamSpec = new CInFileStream;
		if(inStreamSpec->OpenShared(packedFile, false))
		{
			NArchive::N7z::CInArchive archive;
			DebugString(L"7zUpdate opened file: %s", packedFile);
			if(archive.Open(inStreamSpec, &gMaxCheckStartPosition) == S_OK)
			{
				DebugString(L"7zUpdate opened archive: %s", packedFile);
				bool passwordIsDefined;
				if(archive.ReadDatabase(db, password, passwordIsDefined) == S_OK)
				{
					DebugString(L"7zUpdate reading db: %s", packedFile);
					db.Fill();

					int numFiles = db.Files.Size ();
					for(int i=0;i<numFiles;i++)
					{
						if(db.Files[i].IsDir)
						{
							if(bFileFound) break;
							if(db.MTime.Defined.Size()>i)
							{
								if(db.MTime.GetItem(i,fDate))
								{
									if(newestDirDate<fDate)
									{
										newestDirDate=fDate;
										bDirFound=true;
									}
								}
							}								
						}
						else
						{
							if(db.MTime.Defined.Size()>i)
							{
								if(db.MTime.GetItem(i,fDate))
								{
									if(newestFileDate<fDate)
									{
										newestFileDate=fDate;
										bFileFound=true;
									}
								}
							}								
						}
					}
				}
			}
			inStream = inStreamSpec;
		}
		inStreamSpec->File.Close();
		inStream.Release();
		FILETIME ft;
		if(bFileFound)
		{
			ft.dwLowDateTime = (DWORD)newestFileDate;
			ft.dwHighDateTime = (DWORD)(newestFileDate >> 32);
			NWindows::NFile::NDirectory::SetDirTime(packedFile,NULL,NULL,&ft);
			DebugString(L"Update 7z file datetime to %08X %08X (newest file)",ft.dwHighDateTime,ft.dwLowDateTime);
		}
		else if(bDirFound)
		{
			ft.dwLowDateTime = (DWORD)newestDirDate;
			ft.dwHighDateTime = (DWORD)(newestDirDate >> 32);
			NWindows::NFile::NDirectory::SetDirTime(packedFile,NULL,NULL,&ft);
			DebugString(L"Update 7z file datetime to %08X %08X (newest dir)",ft.dwHighDateTime,ft.dwLowDateTime);
		}
	}

	if (hResult == E_NOTIMPL) 
		return E_NOT_SUPPORTED;
	else if (hResult == E_OUTOFMEMORY)
		return E_NO_MEMORY;
	else if (renameOk == false)
		return E_EWRITE;

	return hResult != S_OK && !bAbort ? E_EWRITE : 0;
}

int __stdcall PackFiles(LPSTR packedFile, LPSTR subPath, LPSTR srcPath, LPSTR addList, int flags)
{
	DebugString(L"Only Unicode version supported.");
	return 0;
}

int __stdcall PackFilesW(wchar_t* packedFile, wchar_t* subPath, wchar_t* srcPath, wchar_t* addList, int flags)
{
	DebugString(L"PackFiles PackedFile: %s, SubPath: %s, SrcPath: %s, AddList: %s, Flags: %d",
		packedFile, subPath, srcPath, addList, flags);

	if(TestIfArcFileHasSignature(packedFile)!=S_OK) // <2011-06-28> Check Signature
		return E_EABORTED;

	return Update(packedFile, subPath, srcPath, addList, NULL, flags);
}

int __stdcall DeleteFiles(LPSTR packedFile, LPSTR deleteList)
{
	DebugString(L"Only Unicode version supported.");
	return 0;
}

int __stdcall DeleteFilesW(wchar_t* packedFile, wchar_t* deleteList)
{
	DebugString(L"DeleteFiles PackedFile: %s, DeleteList: %s",
		packedFile, deleteList);

	if(TestIfArcFileHasSignature(packedFile)!=S_OK) // <2011-06-28> Check Signature
		return E_EABORTED;

	return Update(packedFile, NULL, NULL, NULL, deleteList, 0);
}

BOOL __stdcall CanYouHandleThisFile(LPSTR fileName)
{
	DebugString(L"Only Unicode version supported.");
	return 0;
}

BOOL __stdcall CanYouHandleThisFileW(wchar_t* fileName)
{
	// Try to open archive file name
	CInFileStream *inStreamSpec = new CInFileStream();
	CMyComPtr<IInStream> inStream(inStreamSpec);
	if(!inStreamSpec->Open(fileName))
		return FALSE;

	NArchive::N7z::CInArchive archive;
	HRESULT result;
	if((result = archive.Open(inStream, &gMaxCheckStartPosition)) != S_OK)
		return FALSE;

	return TRUE;
}

INT_PTR __stdcall ConfigDialog(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam);

void __stdcall ConfigurePacker(HWND parent, HINSTANCE dllInstance)
{
	DialogBox(dllInstance, MAKEINTRESOURCE(IDD_CONFIG), parent, ConfigDialog);
}

void __stdcall GetPackerFileExt(wchar_t* lpszExtension, size_t cchExtension)
{
	wcsncpy(lpszExtension, L"7z", cchExtension);
}

int __stdcall GetBackgroundFlags(void)
{
	return BACKGROUND_UNPACK | BACKGROUND_PACK;
}

}