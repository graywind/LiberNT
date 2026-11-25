/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     FsRtlRegisterUncProviderEx accepts a device object that carries no name
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define PROVIDER_NAME L"\\Device\\KmtUncProvider"

START_TEST(FsRtlUncProviderEx)
{
    UNICODE_STRING Name = RTL_CONSTANT_STRING(PROVIDER_NAME);
    PDEVICE_OBJECT Device = NULL;
    HANDLE MupHandle = INVALID_HANDLE_VALUE;
    NTSTATUS Status;

    Status = IoCreateDevice(KmtDriverObject,
                            0,
                            NULL,
                            FILE_DEVICE_NETWORK_FILE_SYSTEM,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Device);
    ok(NT_SUCCESS(Status), "IoCreateDevice failed (0x%lx)\n", Status);
    if (!NT_SUCCESS(Status)) return;

    Status = FsRtlRegisterUncProviderEx(&MupHandle, &Name, Device, 0);
    ok(NT_SUCCESS(Status), "FsRtlRegisterUncProviderEx failed (0x%lx)\n", Status);
    if (NT_SUCCESS(Status)) FsRtlDeregisterUncProvider(MupHandle);

    /* FIXME: deregistering leaves the provider in the MUP list, so the device stays referenced */
    IoDeleteDevice(Device);
}
