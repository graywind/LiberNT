/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     FsRtlRegisterUncProviderEx accepts a device object that carries no name and MUP opens a file through it
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define PROVIDER_NAME L"\\Device\\KmtUncProvider"
#define FILE_NAME L"\\??\\UNC\\KmtServer\\Share\\KmtFile"
#define ACCEPTED_PREFIX L"\\KmtServer\\Share"

#define PROVIDER_SHORT_NAME L"KmtUncProvider"
#define PROVIDER_KEY L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\KmtUncProvider"
#define NETWORK_PROVIDER_KEY PROVIDER_KEY L"\\NetworkProvider"
#define PROVIDER_ORDER_KEY L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\NetworkProvider\\Order"
#define KMT_UNC_TAG 'cUnK'

static BOOLEAN CreateSeen;
static ULONG QueryCount;
static ULONG QueryAccepted;
static ULONG LastQueryIoControl;
static ULONG LastQueryLength;
static NTSTATUS LastQueryStatus;
static WCHAR LastQueryPath[128];
static PDEVICE_OBJECT ProviderDevice;
static PDRIVER_DISPATCH SavedCreate;
static PDRIVER_DISPATCH SavedDeviceControl;
static PDRIVER_DISPATCH SavedCleanup;
static PDRIVER_DISPATCH SavedClose;
static PWSTR SavedProviderOrder;
static ULONG SavedProviderOrderLength;

static
NTSTATUS
SetStringValue(
    _In_ HANDLE Key,
    _In_ PCWSTR Name,
    _In_ PCWSTR Data)
{
    UNICODE_STRING Value;

    RtlInitUnicodeString(&Value, Name);
    return ZwSetValueKey(Key,
                         &Value,
                         0,
                         REG_SZ,
                         (PVOID)Data,
                         (ULONG)((wcslen(Data) + 1) * sizeof(WCHAR)));
}

static
NTSTATUS
CreateProviderRegistration(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    HANDLE ServiceKey;
    HANDLE NetworkKey;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, PROVIDER_KEY);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = ZwCreateKey(&ServiceKey,
                         KEY_ALL_ACCESS,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SetStringValue(ServiceKey, L"Group", L"NetworkProvider");
    if (!NT_SUCCESS(Status))
    {
        ZwClose(ServiceKey);
        return Status;
    }

    RtlInitUnicodeString(&Name, L"NetworkProvider");
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               ServiceKey,
                               NULL);
    Status = ZwCreateKey(&NetworkKey,
                         KEY_ALL_ACCESS,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         NULL);
    if (NT_SUCCESS(Status))
    {
        Status = SetStringValue(NetworkKey, L"DeviceName", PROVIDER_NAME);
        ZwClose(NetworkKey);
    }

    ZwClose(ServiceKey);
    return Status;
}

static
BOOLEAN
OrderNamesProvider(
    _In_z_ PCWSTR Order)
{
    SIZE_T Length;

    for (;;)
    {
        Length = 0;
        while ((Order[Length] != L'\0') && (Order[Length] != L','))
        {
            Length++;
        }

        if ((Length == sizeof(PROVIDER_SHORT_NAME) / sizeof(WCHAR) - 1) &&
            RtlEqualMemory(Order, PROVIDER_SHORT_NAME, Length * sizeof(WCHAR)))
        {
            return TRUE;
        }

        if (Order[Length] == L'\0')
        {
            return FALSE;
        }

        Order += Length + 1;
    }
}

static
NTSTATUS
AddProviderToOrder(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    UNICODE_STRING Value;
    PKEY_VALUE_PARTIAL_INFORMATION Info;
    PWSTR Order;
    PWSTR NewOrder;
    SIZE_T OrderLength;
    SIZE_T ProviderLength;
    ULONG Length;
    HANDLE Key;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, PROVIDER_ORDER_KEY);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = ZwOpenKey(&Key, KEY_QUERY_VALUE | KEY_SET_VALUE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlInitUnicodeString(&Value, L"ProviderOrder");
    Status = ZwQueryValueKey(Key, &Value, KeyValuePartialInformation, NULL, 0, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL)
    {
        ZwClose(Key);
        return NT_SUCCESS(Status) ? STATUS_UNSUCCESSFUL : Status;
    }

    Info = ExAllocatePoolWithTag(PagedPool, Length, KMT_UNC_TAG);
    if (Info == NULL)
    {
        ZwClose(Key);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQueryValueKey(Key, &Value, KeyValuePartialInformation, Info, Length, &Length);
    if (!NT_SUCCESS(Status) || (Info->Type != REG_SZ))
    {
        if (NT_SUCCESS(Status))
        {
            Status = STATUS_OBJECT_TYPE_MISMATCH;
        }
        ExFreePoolWithTag(Info, KMT_UNC_TAG);
        ZwClose(Key);
        return Status;
    }

    Order = ExAllocatePoolWithTag(PagedPool,
                                  Info->DataLength + sizeof(WCHAR),
                                  KMT_UNC_TAG);
    if (Order == NULL)
    {
        ExFreePoolWithTag(Info, KMT_UNC_TAG);
        ZwClose(Key);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(Order, Info->Data, Info->DataLength);
    Order[Info->DataLength / sizeof(WCHAR)] = UNICODE_NULL;
    SavedProviderOrderLength = Info->DataLength;
    ExFreePoolWithTag(Info, KMT_UNC_TAG);

    if (!OrderNamesProvider(Order))
    {
        OrderLength = wcslen(Order);
        ProviderLength = sizeof(PROVIDER_SHORT_NAME) / sizeof(WCHAR) - 1;
        NewOrder = ExAllocatePoolWithTag(PagedPool,
                                         (OrderLength + ProviderLength + 2) * sizeof(WCHAR),
                                         KMT_UNC_TAG);
        if (NewOrder == NULL)
        {
            ExFreePoolWithTag(Order, KMT_UNC_TAG);
            ZwClose(Key);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        if (OrderLength == 0)
        {
            RtlCopyMemory(NewOrder,
                          PROVIDER_SHORT_NAME,
                          (ProviderLength + 1) * sizeof(WCHAR));
            OrderLength = ProviderLength + 1;
        }
        else
        {
            RtlCopyMemory(NewOrder, Order, OrderLength * sizeof(WCHAR));
            NewOrder[OrderLength] = L',';
            RtlCopyMemory(NewOrder + OrderLength + 1,
                          PROVIDER_SHORT_NAME,
                          (ProviderLength + 1) * sizeof(WCHAR));
            OrderLength += ProviderLength + 2;
        }

        Status = ZwSetValueKey(Key,
                               &Value,
                               0,
                               REG_SZ,
                               NewOrder,
                               (ULONG)(OrderLength * sizeof(WCHAR)));
        ExFreePoolWithTag(NewOrder, KMT_UNC_TAG);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Order, KMT_UNC_TAG);
            ZwClose(Key);
            return Status;
        }
    }

    SavedProviderOrder = Order;
    ZwClose(Key);
    return STATUS_SUCCESS;
}

static
VOID
RemoveProviderRegistration(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING Name;
    UNICODE_STRING Value;
    HANDLE Key;

    if (SavedProviderOrder != NULL)
    {
        RtlInitUnicodeString(&Name, PROVIDER_ORDER_KEY);
        InitializeObjectAttributes(&ObjectAttributes,
                                   &Name,
                                   OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                                   NULL,
                                   NULL);
        if (NT_SUCCESS(ZwOpenKey(&Key, KEY_SET_VALUE, &ObjectAttributes)))
        {
            RtlInitUnicodeString(&Value, L"ProviderOrder");
            ZwSetValueKey(Key,
                          &Value,
                          0,
                          REG_SZ,
                          SavedProviderOrder,
                          SavedProviderOrderLength);
            ZwClose(Key);
        }

        ExFreePoolWithTag(SavedProviderOrder, KMT_UNC_TAG);
        SavedProviderOrder = NULL;
    }

    RtlInitUnicodeString(&Name, NETWORK_PROVIDER_KEY);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    if (NT_SUCCESS(ZwOpenKey(&Key, KEY_ALL_ACCESS, &ObjectAttributes)))
    {
        ZwDeleteKey(Key);
        ZwClose(Key);
    }

    RtlInitUnicodeString(&Name, PROVIDER_KEY);
    InitializeObjectAttributes(&ObjectAttributes,
                               &Name,
                               OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    if (NT_SUCCESS(ZwOpenKey(&Key, KEY_ALL_ACCESS, &ObjectAttributes)))
    {
        ZwDeleteKey(Key);
        ZwClose(Key);
    }
}

static
ULONG
AcceptedPrefixLength(
    _In_ PUNICODE_STRING Path)
{
    UNICODE_STRING OneSlash = RTL_CONSTANT_STRING(ACCEPTED_PREFIX);
    UNICODE_STRING TwoSlashes = RTL_CONSTANT_STRING(L"\\\\KmtServer\\Share");
    UNICODE_STRING NoSlash = RTL_CONSTANT_STRING(L"KmtServer\\Share");
    UNICODE_STRING Prefix;

    if ((Path->Length == 0) || (Path->Buffer == NULL))
    {
        return 0;
    }

    if (Path->Buffer[0] != L'\\')
    {
        Prefix = NoSlash;
    }
    else if ((Path->Length >= 2 * sizeof(WCHAR)) && (Path->Buffer[1] == L'\\'))
    {
        Prefix = TwoSlashes;
    }
    else
    {
        Prefix = OneSlash;
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
VOID
RecordQueryPath(
    _In_ PUNICODE_STRING Path)
{
    ULONG Chars;

    LastQueryPath[0] = UNICODE_NULL;
    LastQueryLength = 0;

    if ((Path->Buffer == NULL) || (Path->Length == 0))
    {
        return;
    }

    Chars = Path->Length / sizeof(WCHAR);
    if (Chars > (sizeof(LastQueryPath) / sizeof(WCHAR)) - 1)
    {
        Chars = (sizeof(LastQueryPath) / sizeof(WCHAR)) - 1;
    }

    RtlCopyMemory(LastQueryPath, Path->Buffer, Chars * sizeof(WCHAR));
    LastQueryPath[Chars] = UNICODE_NULL;
    LastQueryLength = Path->Length;
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
    ULONG IoControl = IoStackLocation->Parameters.DeviceIoControl.IoControlCode;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;

    if (DeviceObject != ProviderDevice)
    {
        return SavedDeviceControl(DeviceObject, Irp);
    }

    if ((IoControl != IOCTL_REDIR_QUERY_PATH_EX) &&
        (IoControl != IOCTL_REDIR_QUERY_PATH))
    {
        goto Complete;
    }

    QueryCount++;
    LastQueryIoControl = IoControl;

    if ((Irp->RequestorMode != KernelMode) || (Response == NULL))
    {
        goto Complete;
    }

    if (IoControl == IOCTL_REDIR_QUERY_PATH_EX)
    {
        PQUERY_PATH_REQUEST_EX Request =
            IoStackLocation->Parameters.DeviceIoControl.Type3InputBuffer;

        if (Request != NULL)
        {
            Path = Request->PathName;
        }
    }
    else
    {
        PQUERY_PATH_REQUEST Request =
            IoStackLocation->Parameters.DeviceIoControl.Type3InputBuffer;

        if (Request != NULL)
        {
            Path.Buffer = Request->FilePathName;
            Path.Length = Path.MaximumLength = Request->PathNameLength;
        }
    }

    RecordQueryPath(&Path);

    if (Path.Buffer != NULL)
    {
        LengthAccepted = AcceptedPrefixLength(&Path);
    }

    if (LengthAccepted != 0)
    {
        Response->LengthAccepted = LengthAccepted;
        QueryAccepted++;
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = STATUS_BAD_NETWORK_PATH;
    }

Complete:
    LastQueryStatus = Status;
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
    BOOLEAN Registered = FALSE;
    NTSTATUS Status;

    CreateSeen = FALSE;
    QueryCount = 0;
    QueryAccepted = 0;
    LastQueryIoControl = 0;
    LastQueryLength = 0;
    LastQueryStatus = STATUS_SUCCESS;
    LastQueryPath[0] = UNICODE_NULL;

    Status = IoCreateDevice(KmtDriverObject,
                            0,
                            NULL,
                            FILE_DEVICE_NETWORK_FILE_SYSTEM,
                            FILE_REMOTE_DEVICE,
                            FALSE,
                            &Device);
    ok(NT_SUCCESS(Status), "IoCreateDevice failed (0x%lx)\n", Status);
    if (!NT_SUCCESS(Status)) return;

    Device->Flags &= ~DO_DEVICE_INITIALIZING;
    ProviderDevice = Device;
    SavedCreate = KmtDriverObject->MajorFunction[IRP_MJ_CREATE];
    SavedDeviceControl = KmtDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL];
    SavedCleanup = KmtDriverObject->MajorFunction[IRP_MJ_CLEANUP];
    SavedClose = KmtDriverObject->MajorFunction[IRP_MJ_CLOSE];
    KmtDriverObject->MajorFunction[IRP_MJ_CREATE] = CreateHandler;
    KmtDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DeviceControlHandler;
    KmtDriverObject->MajorFunction[IRP_MJ_CLEANUP] = CloseHandler;
    KmtDriverObject->MajorFunction[IRP_MJ_CLOSE] = CloseHandler;

    Status = CreateProviderRegistration();
    ok(NT_SUCCESS(Status), "Creating the provider registration failed (0x%lx)\n", Status);
    if (NT_SUCCESS(Status))
    {
        Status = AddProviderToOrder();
        ok(NT_SUCCESS(Status), "Adding the provider to the order failed (0x%lx)\n", Status);
    }

    Status = FsRtlRegisterUncProviderEx(&MupHandle, &Name, Device, 0);
    ok(NT_SUCCESS(Status), "FsRtlRegisterUncProviderEx failed (0x%lx)\n", Status);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Registered = TRUE;

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
    ok(CreateSeen,
       "The provider did not receive the create (queries %lu, accepted %lu, ioctl 0x%lx, length %lu, path '%S', status 0x%lx)\n",
       QueryCount, QueryAccepted, LastQueryIoControl, LastQueryLength,
       LastQueryPath, LastQueryStatus);

    if (NT_SUCCESS(Status))
    {
        ZwClose(FileHandle);
    }

Cleanup:
    if (Registered)
    {
        FsRtlDeregisterUncProvider(MupHandle);
    }

    KmtDriverObject->MajorFunction[IRP_MJ_CREATE] = SavedCreate;
    KmtDriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SavedDeviceControl;
    KmtDriverObject->MajorFunction[IRP_MJ_CLEANUP] = SavedCleanup;
    KmtDriverObject->MajorFunction[IRP_MJ_CLOSE] = SavedClose;
    RemoveProviderRegistration();
    IoDeleteDevice(Device);
}
