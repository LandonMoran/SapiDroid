#ifndef ZB_ATOMIC_H
#define ZB_ATOMIC_H

#if defined(_MSC_VER)
#include <intrin.h>
static inline int zb_cancel_load(const volatile int *p)
{
    return (int)_InterlockedCompareExchange((volatile long *)p, 0, 0);
}
static inline void zb_cancel_store(volatile int *p, int value)
{
    _InterlockedExchange((volatile long *)p, (long)value);
}
#elif defined(__GNUC__) || defined(__clang__)
static inline int zb_cancel_load(const volatile int *p)
{
    return __atomic_load_n(p, __ATOMIC_RELAXED);
}
static inline void zb_cancel_store(volatile int *p, int value)
{
    __atomic_store_n(p, value, __ATOMIC_RELAXED);
}
#else
#error "A compiler with atomic integer operations is required"
#endif

#endif
