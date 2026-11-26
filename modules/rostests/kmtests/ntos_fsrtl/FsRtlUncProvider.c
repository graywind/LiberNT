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
static PDEVICE_OBJECT ProviderDevice;
static PDRIVER_DISPATCH SavedCreate;
static PDRIVER_DISPATCH SavedDeviceControl;
static PDRIVER_DISPATCH SavedCleanup;
static PDRIVER_DISPATCH SavedClose;

static
ULONG
AcceptedPrefixLength(
    _In_ PUNICODE_STRING Path)
{
    UNICODE_STRING Prefix = RTL_CONSTANT_STRING(ACCEPTED_PREFIX);
    UNICODE_STRING NoSlash = RTL_CONSTANT_STRING(L"KmtServer\\Share");

    if ((Path->Length == 0) || (Path->Buffer == NULL))
    {
        return 0;
    }

    if (Path->Buffer[0] != L'\\')
    {
        Prefix = NoSlash;
    }

    if ((Path->Length < Prefix.Length) ||
        !RtlPrefixUnicodeString(&Prefix, Path, TRUE))
    {
        return 0;
    }

    if ((Path->Length != Prefix.Length) &&
        (Path->Buffer[Prefix.Length / sizeof(WCHAR)] != L'\\'))
    {
        return 0;
    }

    return Prefix.Length;
}

static
NTSTATUS
NTAPI
CreateHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStackLocation = IoGetCurrentIrpStackLocation(Irp);

    if (DeviceObject != ProviderDevice)
    {
        return SavedCreate(DeviceObject, Irp);
    }

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
NTAPI
DeviceControlHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    PQUERY_PATH_RESPONSE Response = Irp->UserBuffer;
    UNICODE_STRING Path = {0};
    ULONG LengthAccepted = 0;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;

    if (DeviceObject != ProviderDevice)
    {
        return SavedDeviceControl(DeviceObject, Irp);
    }

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

    if (Path.Buffer != NULL)
    {
        LengthAccepted = AcceptedPrefixLength(&Path);
    }

    if (LengthAccepted != 0)
    {
        Response->LengthAccepted = LengthAccepted;
        Status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

static
NTSTATUS
NTAPI
CloseHandler(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    if (DeviceObject != ProviderDevice)
    {
        if (IoGetCurrentIrpStackLocation(Irp)->MajorFunction == IRP_MJ_CLEANUP)
        {
            return SavedCleanup(DeviceObject, Irp);
        }

        return SavedClose(DeviceObject, Irp);
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
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

    ProviderDevice = Device;
    SavedCreate = KmtDriverObject->MajorFunction[IRP_MJ_CREATE];
    SavedDeviceControl = KmtDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL];
    SavedCleanup = KmtDriverObject->MajorFunction[IRP_MJ_CLEANUP];
    SavedClose = KmtDriverObject->MajorFunction[IRP_MJ_CLOSE];
    KmtDriverObject->MajorFunction[IRP_MJ_CREATE] = CreateHandler;
    KmtDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControlHandler;
    KmtDriverObject->MajorFunction[IRP_MJ_CLEANUP] = CloseHandler;
    KmtDriverObject->MajorFunction[IRP_MJ_CLOSE] = CloseHandler;

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
    KmtDriverObject->MajorFunction[IRP_MJ_CREATE] = SavedCreate;
    KmtDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SavedDeviceControl;
    KmtDriverObject->MajorFunction[IRP_MJ_CLEANUP] = SavedCleanup;
    KmtDriverObject->MajorFunction[IRP_MJ_CLOSE] = SavedClose;
    IoDeleteDevice(Device);
}
