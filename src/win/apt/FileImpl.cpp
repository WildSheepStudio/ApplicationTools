#include <apt/File.h>

#include <apt/def.h>
#include <apt/log.h>
#include <apt/platform.h>
#include <apt/win.h>
#include <apt/String.h>
#include <apt/TextParser.h>

#include <cstdlib> // malloc, free
#include <utility> // swap

using namespace apt;
using namespace internal;

FileImpl::FileImpl()
	: m_handle((void*)INVALID_HANDLE_VALUE)
{
}

FileImpl::~FileImpl()
{
	if ((HANDLE)m_handle != INVALID_HANDLE_VALUE) {
		APT_PLATFORM_VERIFY(CloseHandle((HANDLE)m_handle));
	}
}

bool FileImpl::Exists(const char* _path)
{
	return GetFileAttributes(_path) != INVALID_FILE_ATTRIBUTES;
}

bool FileImpl::CreateDir(const char* _path)
{
	/*String<64> mkpath(_path);
	
	for (int i = 0; i < path.getDirectoryCount(); ++i) {
		mkpath.appendf("%s/", path.getDirectory(i));
		if (CreateDirectory(mkpath, NULL) == 0) {
			DWORD err = GetLastError();
			if (err != ERROR_ALREADY_EXISTS) {
				APT_LOG_ERR("CreateDirectory failed: %s", GetPlatformErrorString(err));
				return false;
			}
		}
	}
	return true;*/
	TextParser tp(_path);
	while (tp.advanceToNext("\\/") != 0) {
		String<64> mkdir;
		mkdir.set(_path, tp.getCharCount());
		if (CreateDirectory(mkdir, NULL) == 0) {
			DWORD err = GetLastError();
			if (err != ERROR_ALREADY_EXISTS) {
				APT_LOG_ERR("CreateDirectory failed: %s", GetPlatformErrorString(err));
				return false;
			}
		}
		tp.advance(); // skip the delimiter
	}
	return true;
}

bool FileImpl::Read(FileImpl& file_, const char* _path)
{
	if (!_path) {
		_path = file_.getPath();
	}
	APT_ASSERT(_path);

	bool ret = false;
	const char* err = "";
	char* data = 0;

 	HANDLE h = CreateFile(
		_path,
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		NULL
		);
	if (h == INVALID_HANDLE_VALUE) {
		err = GetPlatformErrorString(GetLastError());
		goto FileImpl_Read_end;
	}

	LARGE_INTEGER li;
	if (!GetFileSizeEx(h, &li)) {
		err = GetPlatformErrorString(GetLastError());
		goto FileImpl_Read_end;
	}
	DWORD dataSize = (DWORD)li.QuadPart; // ReadFile can only read DWORD bytes

	data = (char*)malloc(dataSize + 2); // +2 for null terminator
	APT_ASSERT(data);
	DWORD bytesRead;
	if (!ReadFile(h, data, dataSize, &bytesRead, 0)) {
		err = GetPlatformErrorString(GetLastError());
		goto FileImpl_Read_end;
	}
	data[dataSize] = data[dataSize + 1] = 0;

	ret = true;
	
  // close existing handle/free existing data
	if ((HANDLE)file_.m_handle != INVALID_HANDLE_VALUE) {
		APT_PLATFORM_VERIFY(CloseHandle((HANDLE)file_.m_handle));
	}
	if (file_.m_data) {
		free(file_.m_data);
	}
	
	file_.m_data = data;
	file_.m_dataSize = (uint64)dataSize;
	file_.setPath(_path);

FileImpl_Read_end:
	if (!ret) {
		if (data) {
			free(data);
		}
		APT_LOG_ERR("Error reading '%s':\n\t%s", _path, err);
		APT_ASSERT(false);
	}
	if (h != INVALID_HANDLE_VALUE) {
		APT_PLATFORM_VERIFY(CloseHandle(h));
	}
	return ret;
}

bool FileImpl::Write(const FileImpl& _file, const char* _path)
{
	if (!_path) {
		_path = _file.getPath();
	}
	APT_ASSERT(_path);

	bool ret = false;
	const char* err = "";
	char* data = 0;
	
 	HANDLE h = CreateFile(
		_path,
		GENERIC_WRITE,
		0, // prevent sharing while we write
		NULL,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		NULL
		);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD lastErr = GetLastError();
		if (lastErr == ERROR_PATH_NOT_FOUND) {
			if (CreateDir(_path)) {
				return Write(_file, _path);
			} else {
				return false;
			}
		} else {
			err = GetPlatformErrorString(lastErr);
			goto FileImpl_Write_end;
		}
	}

	DWORD bytesWritten;
	if (!WriteFile(h, _file.getData(), (DWORD)_file.getDataSize(), &bytesWritten, NULL)) {
		err = GetPlatformErrorString(GetLastError());
		goto FileImpl_Write_end;
	}
	APT_ASSERT(bytesWritten == _file.getDataSize());

	ret = true;

FileImpl_Write_end:
	if (!ret) {
		APT_LOG_ERR("Error writing '%s':\n\t%s", _path, err);
		APT_ASSERT(false);
	}
	if (h != INVALID_HANDLE_VALUE) {
		APT_PLATFORM_VERIFY(CloseHandle(h));
	}
	return ret;
}