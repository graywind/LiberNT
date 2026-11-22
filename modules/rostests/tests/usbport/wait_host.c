/*
 * PROJECT: LiberNT host-side USBPORT tests
 * LICENSE: GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE: Test that USBPORT_Wait never waits on a thread at raised IRQL.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define IN
#define NTAPI
#define FALSE 0
#define KernelMode 0
#define PASSIVE_LEVEL 0
#define DISPATCH_LEVEL 2
#define STATUS_SUCCESS 0

typedef uint32_t ULONG;
typedef uint32_t KIRQL;
typedef int32_t NTSTATUS;
typedef int BOOLEAN;
typedef void *PVOID;

typedef union
{
    struct
    {
        uint32_t LowPart;
        int32_t HighPart;
    };
    int64_t QuadPart;
} LARGE_INTEGER;

#define DPRINT(...)

static KIRQL CurrentIrql;
static ULONG StallMicroseconds;
static ULONG DelayCount;
static int64_t DelayInterval;

static KIRQL KeGetCurrentIrql(void)
{
    return CurrentIrql;
}

static void KeStallExecutionProcessor(ULONG Microseconds)
{
    StallMicroseconds += Microseconds;
}

static ULONG KeQueryTimeIncrement(void)
{
    return 1;
}

static NTSTATUS KeDelayExecutionThread(int WaitMode, BOOLEAN Alertable, LARGE_INTEGER *Interval)
{
    DelayCount++;
    DelayInterval = Interval->QuadPart;
    return STATUS_SUCCESS;
}

#include "wait.h"

int main(void)
{
    CurrentIrql = PASSIVE_LEVEL;
    assert(USBPORT_Wait(NULL, 20) == STATUS_SUCCESS);
    assert(DelayCount == 1);
    assert(StallMicroseconds == 0);
    assert(DelayInterval < 0);

    DelayCount = 0;
    StallMicroseconds = 0;
    DelayInterval = 0;
    CurrentIrql = DISPATCH_LEVEL;
    assert(USBPORT_Wait(NULL, 20) == STATUS_SUCCESS);
    assert(DelayCount == 0);
    assert(StallMicroseconds == 20000);

    puts("USBPORT_Wait IRQL behavior: ok");
    return 0;
}
