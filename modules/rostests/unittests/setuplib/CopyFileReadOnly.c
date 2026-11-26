/*
 * PROJECT:     LiberNT Setup Library - Unit-tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test that SetupCopyFile does not make the copy read-only
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "precomp.h"
#include <strsafe.h>

static void
WriteSourceFile(PCWSTR Path)
{
    static const char Data[] = "SetupCopyFile attribute test\r\n";
    HANDLE File;
    DWORD Written;

    File = CreateFileW(Path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    ok(File != INVALID_HANDLE_VALUE, "CreateFileW(%ls) failed with %lu\n", Path, GetLastError());
    if (File == INVALID_HANDLE_VALUE) return;

    ok(WriteFile(File, Data, sizeof(Data), &Written, NULL), "WriteFile failed with %lu\n", GetLastError());
    CloseHandle(File);

    ok(SetFileAttributesW(Path, FILE_ATTRIBUTE_READONLY),
       "SetFileAttributesW(%ls) failed with %lu\n", Path, GetLastError());
}

START_TEST(CopyFileReadOnly)
{
    WCHAR Temp[MAX_PATH], Source[MAX_PATH], Target[MAX_PATH];
    WCHAR NtSource[MAX_PATH], NtTarget[MAX_PATH];
    DWORD TargetAttributes;
    NTSTATUS Status;

    GetTempPathW(ARRAYSIZE(Temp), Temp);
    GetTempFileNameW(Temp, L"cpy", 0, Source);
    GetTempFileNameW(Temp, L"cpy", 1, Target);
    DeleteFileW(Target);

    WriteSourceFile(Source);

    StringCchPrintfW(NtSource, ARRAYSIZE(NtSource), L"\\??\\%ls", Source);
    StringCchPrintfW(NtTarget, ARRAYSIZE(NtTarget), L"\\??\\%ls", Target);

    Status = SetupCopyFile(NtSource, NtTarget, FALSE);
    ok(NT_SUCCESS(Status), "SetupCopyFile failed with 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        TargetAttributes = GetFileAttributesW(Target);
        ok(TargetAttributes != INVALID_FILE_ATTRIBUTES,
           "GetFileAttributesW failed with %lu\n", GetLastError());
        ok(!(TargetAttributes & FILE_ATTRIBUTE_READONLY),
           "the copy is read-only (attributes %#lx)\n", TargetAttributes);
    }

    SetFileAttributesW(Source, FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW(Target, FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(Source);
}
