// Test shim: stands in for the WDK headers so avhub_loopback.cpp compiles and runs on macOS.
// Only the kernel APIs used by the FIFO are provided. Not part of the driver build.
#ifndef AVHUB_TEST_SHIM_COMMON_H
#define AVHUB_TEST_SHIM_COMMON_H

#include <cstddef>
#include <cstdlib>
#include <cstring>

typedef unsigned char   UCHAR;
typedef UCHAR*          PUCHAR;
typedef unsigned long   ULONG;
typedef int             NTSTATUS;
typedef unsigned char   KIRQL;
typedef void            VOID;
typedef unsigned long long POOL_FLAGS;

#define STATUS_SUCCESS                  ((NTSTATUS)0)
#define STATUS_INSUFFICIENT_RESOURCES   ((NTSTATUS)0xC000009A)
#define POOL_FLAG_NON_PAGED             ((POOL_FLAGS)0x0000000000000040ULL)

#define _In_
#define _In_opt_
#define _Out_
#define _Inout_
#define _In_reads_bytes_(x)
#define _Out_writes_bytes_(x)

#define PAGED_CODE()
#define min(a, b) (((a) < (b)) ? (a) : (b))

// Spin lock: zero means unlocked, so RtlZeroMemory on a FIFO is safe.
typedef struct _KSPIN_LOCK { volatile unsigned long v; } KSPIN_LOCK;

inline void KeInitializeSpinLock(KSPIN_LOCK* p) { p->v = 0; }
inline void KeAcquireSpinLock(KSPIN_LOCK* p, KIRQL* irql) {
    while (__sync_lock_test_and_set(&p->v, 1)) {}
    *irql = 0;
}
inline void KeReleaseSpinLock(KSPIN_LOCK* p, KIRQL) { __sync_lock_release(&p->v); }

inline void* ExAllocatePool2(POOL_FLAGS, size_t bytes, unsigned long) { return std::malloc(bytes); }
inline void ExFreePoolWithTag(void* p, unsigned long) { std::free(p); }

#define RtlZeroMemory(dst, n) std::memset((dst), 0, (n))
#define RtlCopyMemory(dst, src, n) std::memmove((dst), (src), (n))

#endif
