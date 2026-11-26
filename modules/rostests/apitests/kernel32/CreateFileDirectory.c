/*
 * PROJECT:     LiberNT API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test CreateFileW directory creation with BACKUP/POSIX/DIRECTORY flags
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "precomp.h"

START_TEST(CreateFileDirectory)
{
    static const DWORD Flags[] =
    {
        FILE_FLAG_BACKUP_SEMANTICS,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_ATTRIBUTE_DIRECTORY,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_POSIX_SEMANTICS | FILE_ATTRIBUTE_DIRECTORY,
        FILE_FLAG_POSIX_SEMANTICS | FILE_ATTRIBUTE_DIRECTORY
    };
    static const DWORD Dispositions[] =
    {
        CREATE_NEW,
        OPEN_ALWAYS,
        TRUNCATE_EXISTING,
        CREATE_ALWAYS
    };
    WCHAR Path[MAX_PATH], Temp[MAX_PATH];
    BY_HANDLE_FILE_INFORMATION Info;
    HANDLE File;
    UINT i, j;

    GetTempPathW(ARRAYSIZE(Temp), Temp);
    GetTempFileNameW(Temp, L"psx", 0, Path);
    DeleteFileW(Path);

    for (i = 0; i < ARRAYSIZE(Dispositions); i++)
    {
        for (j = 0; j < ARRAYSIZE(Flags); j++)
        {
            File = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                               Dispositions[i], Flags[j], NULL);
            if (TRUNCATE_EXISTING == Dispositions[i])
            {
                ok(INVALID_HANDLE_VALUE == File && ERROR_FILE_NOT_FOUND == GetLastError(),
                   "CreateFileW(%lu, %#lx) returned %p with %lu\n",
                   Dispositions[i], Flags[j], File, GetLastError());
                continue;
            }
            ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%lu, %#lx) failed with %lu\n",
               Dispositions[i], Flags[j], GetLastError());
            if (INVALID_HANDLE_VALUE == File)
                continue;

            ok(GetFileInformationByHandle(File, &Info), "GetFileInformationByHandle failed with %lu\n",
               GetLastError());
            if (CREATE_NEW == Dispositions[i] &&
                (FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_POSIX_SEMANTICS | FILE_ATTRIBUTE_DIRECTORY) == Flags[j])
                ok(0 != (Info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), "created file is not a directory\n");
            else
                ok(0 == (Info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), "created file is a directory\n");

            CloseHandle(File);
            if (0 != (Info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                RemoveDirectoryW(Path);
            else
                DeleteFileW(Path);
        }
    }
}
