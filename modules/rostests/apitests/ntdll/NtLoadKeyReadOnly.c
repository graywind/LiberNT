/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test NtLoadKey on hive files that carry the read-only attribute
 * COPYRIGHT:   Copyright 2026 LiberNT contributors
 */

#include "precomp.h"

static NTSTATUS
LoadHive(
    PCWSTR FilePath,
    PCWSTR KeyPath)
{
    UNICODE_STRING NtFilePath, KeyName;
    OBJECT_ATTRIBUTES FileAttributes, KeyAttributes;
    NTSTATUS Status;

    if (!RtlDosPathNameToNtPathName_U(FilePath, &NtFilePath, NULL, NULL))
        return STATUS_OBJECT_PATH_SYNTAX_BAD;

    RtlInitUnicodeString(&KeyName, KeyPath);
    InitializeObjectAttributes(&KeyAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    InitializeObjectAttributes(&FileAttributes, &NtFilePath, OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = NtLoadKey(&KeyAttributes, &FileAttributes);
    if (NT_SUCCESS(Status)) NtUnloadKey(&KeyAttributes);

    RtlFreeUnicodeString(&NtFilePath);
    return Status;
}

static BOOLEAN
CopyHive(
    PCWSTR Source,
    PCWSTR Destination,
    DWORD Attributes)
{
    if (!CopyFileW(Source, Destination, FALSE))
    {
        ok(0, "CopyFileW(%ls) failed with %lu\n", Source, GetLastError());
        return FALSE;
    }

    if (!SetFileAttributesW(Destination, Attributes))
    {
        ok(0, "SetFileAttributesW(%ls) failed with %lu\n", Destination, GetLastError());
        return FALSE;
    }

    return TRUE;
}

START_TEST(NtLoadKeyReadOnly)
{
    WCHAR WindowsDir[MAX_PATH], Source[MAX_PATH], ModulePath[MAX_PATH];
    WCHAR HiveArchive[MAX_PATH], HivePlain[MAX_PATH];
    WCHAR *Separator;
    BOOLEAN RestoreEnabled = FALSE, BackupEnabled = FALSE;
    NTSTATUS Status;

    Status = RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, TRUE, FALSE, &RestoreEnabled);
    if (!NT_SUCCESS(Status))
    {
        skip("RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE) failed (Status 0x%08lx)\n", Status);
        return;
    }
    RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, TRUE, FALSE, &BackupEnabled);

    GetWindowsDirectoryW(WindowsDir, ARRAYSIZE(WindowsDir));
    StringCchPrintfW(Source, ARRAYSIZE(Source), L"%ls\\System32\\config\\BCD-Template", WindowsDir);

    GetModuleFileNameW(NULL, ModulePath, ARRAYSIZE(ModulePath));
    Separator = wcsrchr(ModulePath, L'\\');
    if (Separator != NULL) *Separator = UNICODE_NULL;
    StringCchPrintfW(HiveArchive, ARRAYSIZE(HiveArchive), L"%ls\\hiva-%lu.tmp", ModulePath, GetCurrentProcessId());
    StringCchPrintfW(HivePlain, ARRAYSIZE(HivePlain), L"%ls\\hivp-%lu.tmp", ModulePath, GetCurrentProcessId());
    DeleteFileW(HiveArchive);
    DeleteFileW(HivePlain);

    if (CopyHive(Source, HiveArchive, FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_READONLY))
    {
        Status = LoadHive(HiveArchive, L"\\Registry\\Machine\\TestHiveReadOnly");
        ok(NT_SUCCESS(Status), "NtLoadKey on an ARCHIVE|READONLY hive failed with 0x%08lx\n", Status);
    }

    if (CopyHive(Source, HivePlain, FILE_ATTRIBUTE_READONLY))
    {
        Status = LoadHive(HivePlain, L"\\Registry\\Machine\\TestHiveReadOnlyNotArchive");
        ok(Status == STATUS_ACCESS_DENIED,
           "NtLoadKey on a READONLY-only hive returned 0x%08lx, expected 0x%08lx\n",
           Status, (ULONG)STATUS_ACCESS_DENIED);
    }

    SetFileAttributesW(HiveArchive, FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW(HivePlain, FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(HiveArchive);
    DeleteFileW(HivePlain);
    RtlAdjustPrivilege(SE_BACKUP_PRIVILEGE, BackupEnabled, FALSE, &BackupEnabled);
    RtlAdjustPrivilege(SE_RESTORE_PRIVILEGE, RestoreEnabled, FALSE, &RestoreEnabled);
}
