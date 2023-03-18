#ifndef __IPC_ATOMIC_H__
#define __IPC_ATOMIC_H__
#define IPC_GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#if (IPC_GCC_VERSION >= 40102)
	#define barrier()                     	(__sync_synchronize())
	#define ATOMIC_GET(ptr)                 ({ __typeof__(*(ptr)) volatile *_val = (ptr); barrier(); (*_val); })
	#define ATOMIC_SET(ptr, value)          ((void)__sync_lock_test_and_set((ptr), (value)))
	#define ATOMIC_BCS(ptr, comp, value)    ((int)__sync_bool_compare_and_swap((ptr), (comp), (value)))
	#define ATOMIC_VCS(ptr, comp, value)    ((__typeof__(*(ptr)))__sync_val_compare_and_swap((ptr), (comp), (value)))
	#define ATOMIC_FADD(ptr, value)         ((__typeof__(*(ptr)))__sync_fetch_and_add((ptr), (value)))
	#define ATOMIC_FSUB(ptr, value)         ((__typeof__(*(ptr)))__sync_fetch_and_sub((ptr), (value)))
#else
	#define barrier() __asm__ __volatile__("": : :"memory")
	#error "Not supported atomic operation from gcc buildin function."
#endif


#endif
