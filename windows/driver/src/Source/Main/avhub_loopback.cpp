/*++

AVHub addition (not part of the upstream Simple Audio Sample).

Module Name:

    avhub_loopback.cpp

Abstract:

    Ring buffer behind the AVHub loopback. Written by the render stream and
    read by the capture stream of the same device.

--*/

#include "common.h"
#include "avhub_loopback.h"

#pragma code_seg("PAGE")
NTSTATUS
AvhubFifoInit
(
    _Out_ PAVHUB_FIFO   Fifo,
    _In_  ULONG         Capacity
)
{
    PAGED_CODE();

    RtlZeroMemory(Fifo, sizeof(*Fifo));
    KeInitializeSpinLock(&Fifo->Lock);

    Fifo->Buffer = (PUCHAR)ExAllocatePool2(POOL_FLAG_NON_PAGED, Capacity, AVHUB_FIFO_POOLTAG);
    if (Fifo->Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Fifo->Capacity = Capacity;
    return STATUS_SUCCESS;
}

#pragma code_seg("PAGE")
VOID
AvhubFifoFree
(
    _Inout_ PAVHUB_FIFO Fifo
)
{
    PAGED_CODE();

    if (Fifo->Buffer != NULL)
    {
        ExFreePoolWithTag(Fifo->Buffer, AVHUB_FIFO_POOLTAG);
    }
    RtlZeroMemory(Fifo, sizeof(*Fifo));
}

#pragma code_seg()
VOID
AvhubFifoWrite
(
    _Inout_                         PAVHUB_FIFO Fifo,
    _In_reads_bytes_(Bytes)         const UCHAR* Source,
    _In_                            ULONG       Bytes,
    _In_                            ULONG       BlockAlign
)
{
    KIRQL   oldIrql;
    ULONG   freeBytes;
    ULONG   dropBytes;
    ULONG   writeOffset;
    ULONG   firstRun;

    if (Fifo->Buffer == NULL || Bytes == 0 || BlockAlign == 0)
    {
        return;
    }

    KeAcquireSpinLock(&Fifo->Lock, &oldIrql);

    if (Fifo->BlockAlign != BlockAlign)
    {
        // New format: the old bytes no longer line up with the frame layout.
        Fifo->Count = 0;
        Fifo->ReadOffset = 0;
        Fifo->BlockAlign = BlockAlign;
    }

    // A write larger than the whole FIFO keeps only its newest Capacity bytes.
    if (Bytes > Fifo->Capacity)
    {
        Source += Bytes - Fifo->Capacity;
        Bytes = Fifo->Capacity;
    }

    freeBytes = Fifo->Capacity - Fifo->Count;
    if (Bytes > freeBytes)
    {
        // Drop the oldest audio. Round up to whole frames so the reader stays aligned.
        dropBytes = Bytes - freeBytes;
        dropBytes = ((dropBytes + BlockAlign - 1) / BlockAlign) * BlockAlign;
        if (dropBytes > Fifo->Count)
        {
            dropBytes = Fifo->Count;
        }
        Fifo->ReadOffset = (Fifo->ReadOffset + dropBytes) % Fifo->Capacity;
        Fifo->Count -= dropBytes;
    }

    writeOffset = (Fifo->ReadOffset + Fifo->Count) % Fifo->Capacity;
    firstRun = min(Bytes, Fifo->Capacity - writeOffset);
    RtlCopyMemory(Fifo->Buffer + writeOffset, Source, firstRun);
    RtlCopyMemory(Fifo->Buffer, Source + firstRun, Bytes - firstRun);
    Fifo->Count += Bytes;

    KeReleaseSpinLock(&Fifo->Lock, oldIrql);
}

#pragma code_seg()
ULONG
AvhubFifoRead
(
    _Inout_                         PAVHUB_FIFO Fifo,
    _Out_writes_bytes_(Bytes)       UCHAR*      Destination,
    _In_                            ULONG       Bytes,
    _In_                            ULONG       BlockAlign
)
{
    KIRQL   oldIrql;
    ULONG   copyBytes = 0;
    ULONG   firstRun;

    if (Fifo->Buffer == NULL || Bytes == 0)
    {
        RtlZeroMemory(Destination, Bytes);
        return 0;
    }

    KeAcquireSpinLock(&Fifo->Lock, &oldIrql);

    if (Fifo->BlockAlign == BlockAlign && Fifo->Count > 0)
    {
        copyBytes = min(Bytes, Fifo->Count);
        firstRun = min(copyBytes, Fifo->Capacity - Fifo->ReadOffset);
        RtlCopyMemory(Destination, Fifo->Buffer + Fifo->ReadOffset, firstRun);
        RtlCopyMemory(Destination + firstRun, Fifo->Buffer, copyBytes - firstRun);
        Fifo->ReadOffset = (Fifo->ReadOffset + copyBytes) % Fifo->Capacity;
        Fifo->Count -= copyBytes;
    }

    KeReleaseSpinLock(&Fifo->Lock, oldIrql);

    if (copyBytes < Bytes)
    {
        RtlZeroMemory(Destination + copyBytes, Bytes - copyBytes);
    }
    return copyBytes;
}
