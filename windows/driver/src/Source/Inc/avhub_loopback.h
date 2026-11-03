/*++

AVHub addition (not part of the upstream Simple Audio Sample).

Module Name:

    avhub_loopback.h

Abstract:

    Byte FIFO that carries audio from an AVHub render endpoint to the capture
    endpoint of the same device. One FIFO per adapter (per device instance).
    Stream position updates run at DISPATCH_LEVEL, so the FIFO is guarded by
    a spin lock and uses non-paged memory.

--*/

#ifndef _AVHUB_LOOPBACK_H_
#define _AVHUB_LOOPBACK_H_

// 0.2 s of 16-bit stereo at 48 kHz (48000 * 4 bytes * 0.2). Keeps the loop
// latency bounded when the capture side is slower than the render side.
#define AVHUB_FIFO_CAPACITY_BYTES   38400

#define AVHUB_FIFO_POOLTAG          'fHVA'

typedef struct _AVHUB_FIFO
{
    KSPIN_LOCK  Lock;
    PUCHAR      Buffer;         // non-paged, Capacity bytes
    ULONG       Capacity;       // bytes
    ULONG       ReadOffset;     // index of the oldest byte
    ULONG       Count;          // bytes currently stored
    ULONG       BlockAlign;     // bytes per frame of the writer; 0 = no writer yet
} AVHUB_FIFO, *PAVHUB_FIFO;

// PASSIVE_LEVEL only (allocates).
NTSTATUS
AvhubFifoInit
(
    _Out_ PAVHUB_FIFO   Fifo,
    _In_  ULONG         Capacity
);

// PASSIVE_LEVEL only. Safe to call on a zeroed or failed FIFO.
VOID
AvhubFifoFree
(
    _Inout_ PAVHUB_FIFO Fifo
);

// Appends Bytes to the FIFO. When full, the oldest audio is dropped.
// A change of BlockAlign (a new render format) clears the FIFO.
// Runs at DISPATCH_LEVEL.
VOID
AvhubFifoWrite
(
    _Inout_                             PAVHUB_FIFO Fifo,
    _In_reads_bytes_(Bytes)             const UCHAR* Source,
    _In_                                ULONG       Bytes,
    _In_                                ULONG       BlockAlign
);

// Reads up to Bytes into Destination and zero-fills the rest. Returns the
// number of audio bytes copied. Returns 0 (silence) if the FIFO was written
// with a different BlockAlign than the reader expects.
// Runs at DISPATCH_LEVEL.
ULONG
AvhubFifoRead
(
    _Inout_                             PAVHUB_FIFO Fifo,
    _Out_writes_bytes_(Bytes)           UCHAR*      Destination,
    _In_                                ULONG       Bytes,
    _In_                                ULONG       BlockAlign
);

#endif  // _AVHUB_LOOPBACK_H_
