/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     FsRtlRegisterUncProviderEx accepts a device object that carries no name and MUP opens a file through it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define PROVIDER_NAME L"\\Device\\KmtUncProvider"
#define FILE_NAME L"\\Device\\Mup\\KmtServer\\Share\\KmtFile"
#define ACCEPTED_PREFIX L"\\KmtServer\\Share"

static BOOLEAN CreateSeen;

static
BOOLEAN
IsAcceptedPath(
    _In_ PUNICODE_STRING Path)
{
    UNICODE_STRING Prefix = RTL_CONSTANT_STRING(ACCEPTED_PREFIX);

    if (Path->Length < Prefix.Length)
    {
        return FALSE;
    }

    if (!RtlPrefixUnicodeString(&Prefix, Path, TRUE))
    {
        return FALSE;
    }

    return (Path->Length == Prefix.Length) ||
           (Path->Buffer[Prefix.Length / sizeof(WCHAR)] == L'\\');
}

static
NTSTATUS
CreateHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IoStackLocation)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    if (IoStackLocation->FileObject->FileName.Length != 0)
    {
        CreateSeen = TRUE;
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = FILE_OPENED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

static
NTSTATUS
DeviceControlHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IoStackLocation)
{
    PQUERY_PATH_RESPONSE Response = Irp->UserBuffer;
    UNICODE_STRING Path = {0};
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;

    UNREFERENCED_PARAMETER(DeviceObject);

    if ((Irp->RequestorMode != KernelMode) || (Response == NULL))
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else if (IoStackLocation->Parameters.DeviceIoControl.IoControlCode == IOCTL_REDIR_QUERY_PATH_EX)
    {
        PQUERY_PATH_REQUEST_EX Request =
            IoStackLocation->Parameters.DeviceIoControl.Type3InputBuffer;

        if (Request != NULL)
        {
            Path = Request->PathName;
        }
    }
    else if (IoStackLocation->Parameters.DeviceIoControl.IoControlCode == IOCTL_REDIR_QUERY_PATH)
    {
        PQUERY_PATH_REQUEST Request =
            IoStackLocation->Parameters.DeviceIoControl.Type3InputBuffer;

        if (Request != NULL)
        {
            Path.Buffer = Request->FilePathName;
            Path.Length = Path.MaximumLength = Request->PathNameLength;
        }
    }

    if ((Path.Buffer != NULL) && IsAcceptedPath(&Path))
    {
        Response->LengthAccepted = (sizeof(ACCEPTED_PREFIX) / sizeof(WCHAR) - 1) * sizeof(WCHAR);
        Status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

START_TEST(FsRtlUncProviderEx)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(PROVIDER_NAME);
    UNICODE_STRING FileName = RTL_CONSTANT_STRING(FILE_NAME);
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    PDEVICE_OBJECT Device = NULL;
    HANDLE MupHandle = INVALID_HANDLE_VALUE;
    HANDLE FileHandle = INVALID_HANDLE_VALUE;
    NTSTATUS Status;

    CreateSeen = FALSE;

    Status = IoCreateDevice(KmtDriverObject,
                            0,
                            NULL,
                            FILE_DEVICE_NETWORK_FILE_SYSTEM,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Device);
    ok(NT_SUCCESS(Status), "IoCreateDevice failed (0x%lx)\n", Status);
    if (!NT_SUCCESS(Status)) return;

    KmtRegisterIrpHandler(IRP_MJ_CREATE, Device, CreateHandler);
    KmtRegisterIrpHandler(IRP_MJ_DEVICE_CONTROL, Device, DeviceControlHandler);

    Status = FsRtlRegisterUncProviderEx(&MupHandle, &Name, Device, 0);
    ok(NT_SUCCESS(Status), "FsRtlRegisterUncProviderEx failed (0x%lx)\n", Status);
    if (!NT_SUCCESS(Status)) goto Cleanup;

    InitializeObjectAttributes(&ObjectAttributes,
                               &FileName,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = IoCreateFile(&FileHandle,
                          FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          0,
                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                          FILE_OPEN,
                          0,
                          NULL,
                          0,
                          CreateFileTypeNone,
                          NULL,
                          IO_NO_PARAMETER_CHECKING);
    ok(NT_SUCCESS(Status), "Opening %S failed (0x%lx)\n", FILE_NAME, Status);
    ok(CreateSeen, "The provider did not receive the create\n");

    if (NT_SUCCESS(Status))
    {
        ZwClose(FileHandle);
    }

    FsRtlDeregisterUncProvider(MupHandle);

Cleanup:
    KmtUnregisterIrpHandler(IRP_MJ_CREATE, Device, CreateHandler);
    KmtUnregisterIrpHandler(IRP_MJ_DEVICE_CONTROL, Device, DeviceControlHandler);
    IoDeleteDevice(Device);
}
