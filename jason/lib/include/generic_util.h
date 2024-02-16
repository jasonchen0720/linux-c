#ifndef __GENERIC_UTILS_H__
#define __GENERIC_UTILS_H__
#define BIT(nr)				(1 << (nr))
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))
#if 0
#define BYTE_PER_LONG		 		4
#define BITS_PER_LONG		 		32
#define BITS_MSK_LONG				0xffffffff
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#endif
#define __set_bit(n, f)		do {(f) |=  (1UL << (n));} while (0)
#define __clr_bit(n, f)		do {(f) &= ~(1UL << (n));} while (0)
#define __test_bit(n, f)	((f) & (1UL << (n)))

#endif
