/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test CreateFileW directory creation with BACKUP/POSIX/DIRECTORY flags
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 *              Adapted from Wine dlls/kernel32/tests/file.c (test_posix_semantics), LGPL-2.1-or-later
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
    static const struct
    {
        DWORD Disposition, Cleanup;
    } Dispositions[] =
    {
        {CREATE_NEW, 1},
        {OPEN_ALWAYS, 0},
        {TRUNCATE_EXISTING, 0},
        {CREATE_ALWAYS, 1}
    };
    WCHAR Path[MAX_PATH], Temp[MAX_PATH];
    BY_HANDLE_FILE_INFORMATION Info;
    HANDLE File;
    UINT i, j;

    if (!GetTempPathW(ARRAYSIZE(Temp), Temp))
    {
        ok(0, "GetTempPathW failed with %lu\n", GetLastError());
        return;
    }
    if (!GetTempFileNameW(Temp, L"psx", 0, Path))
    {
        ok(0, "GetTempFileNameW failed with %lu\n", GetLastError());
        return;
    }
    DeleteFileW(Path);

    for (i = 0; i < ARRAYSIZE(Dispositions); i++)
    {
        for (j = 0; j < ARRAYSIZE(Flags); j++)
        {
            File = CreateFileW(Path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
                               Dispositions[i].Disposition, Flags[j], NULL);
            ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%lu, %#lx) failed with %lu\n",
               Dispositions[i].Disposition, Flags[j], GetLastError());
            if (INVALID_HANDLE_VALUE == File)
                continue;

            if (!GetFileInformationByHandle(File, &Info))
            {
                ok(0, "GetFileInformationByHandle failed with %lu\n", GetLastError());
                CloseHandle(File);
                continue;
            }

            if (CREATE_NEW == Dispositions[i].Disposition &&
                (FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_POSIX_SEMANTICS | FILE_ATTRIBUTE_DIRECTORY) == Flags[j])
                ok(0 != (Info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), "created file is not a directory\n");
            else
                ok(0 == (Info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), "created file is a directory\n");

            CloseHandle(File);
            if (!Dispositions[i].Cleanup)
                continue;
            if (0 != (Info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                RemoveDirectoryW(Path);
            else
                DeleteFileW(Path);
        }
    }
}
