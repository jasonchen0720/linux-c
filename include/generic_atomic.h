#ifndef __GENERIC_ATOMIC_H__
#define __GENERIC_ATOMIC_H__

#if ((__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__) >= 40102)
#define aop_barrier()   (__sync_synchronize())

/* atomic get */
#define aop_get(ptr)    ({ __typeof__(*(ptr)) volatile *_val = (ptr); aop_barrier(); (*_val); })

/* atomic set */
#define aop_set(ptr, value)		((void)__sync_lock_test_and_set((ptr), (value)))

/* atomic swap, return old value */
#define aop_swap(ptr, value)	((__typeof__(*(ptr)))__sync_lock_test_and_set((ptr), (value)))

/* compare and set */
#define aop_cas(ptr, comp, value)  ((__typeof__(*(ptr)))__sync_val_compare_and_swap((ptr), (comp), (value)))

#define aop_clear(ptr)             ((void)__sync_lock_release((ptr)))

/* Return new value after '+' operation done */
#define aop_addf(ptr, value)      ((__typeof__(*(ptr)))__sync_add_and_fetch((ptr), (value)))

/* Return new value after '-' operation done */
#define aop_subf(ptr, value)      ((__typeof__(*(ptr)))__sync_sub_and_fetch((ptr), (value)))

/* Return new value after '|' operation done */
#define aop_orf(ptr, value)       ((__typeof__(*(ptr)))__sync_or_and_fetch((ptr), (value)))

/* Return new value after '&' operation done */
#define aop_andf(ptr, value)      ((__typeof__(*(ptr)))__sync_and_and_fetch((ptr), (value)))

/* Return new value after '^' operation done */
#define aop_xorf(ptr, value)      ((__typeof__(*(ptr)))__sync_xor_and_fetch((ptr), (value)))

/* Return old value */
#define aop_fadd(ptr, value)      ((__typeof__(*(ptr)))__sync_fetch_and_add((ptr), (value)))
#define aop_fsub(ptr, value)      ((__typeof__(*(ptr)))__sync_fetch_and_sub((ptr), (value)))
#define aop_for(ptr, value)       ((__typeof__(*(ptr)))__sync_fetch_and_or((ptr), (value)))
#define aop_fand(ptr, value)      ((__typeof__(*(ptr)))__sync_fetch_and_and((ptr), (value)))
#define aop_fxor(ptr, value)      ((__typeof__(*(ptr)))__sync_fetch_and_xor((ptr), (value)))
#else
#define aop_barrier() __asm__ __volatile__("": : :"memory")
#error "can not supported atomic operation."
#endif

#define aop_inc(ptr)             ((void)aop_addf((ptr), 1))
#define aop_dec(ptr)             ((void)aop_subf((ptr), 1))
#define aop_add(ptr, val)        ((void)aop_addf((ptr), (val)))
#define aop_sub(ptr, val)        ((void)aop_subf((ptr), (val)))
#define aop_or(ptr, val)         ((void)aop_orf((ptr), (val)))
#define aop_and(ptr, val)        ((void)aop_andf((ptr), (val)))
#define aop_xor(ptr, val)        ((void)aop_xorf((ptr), (val)))
#define aop_setbit(ptr, mask)    ((void)aop_orf((ptr), (mask)))
#define aop_clrbit(ptr, mask)    ((void)aop_andf((ptr), ~(mask)))
#define aop_chgbit(ptr, mask)    ((void)aop_xorf((ptr), (mask)))

#endif
