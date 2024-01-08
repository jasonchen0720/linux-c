#ifndef __GENERIC_BIT_H__
#define __GENERIC_BIT_H__

#define BYTE_PER_LONG		 		(sizeof(long))
#define BITS_PER_LONG		 		(BYTE_PER_LONG << 3)
#define BITS_MSK_LONG				(~0UL)
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BITS_TO_LONGS(nr)	(((nr) + BITS_PER_LONG - 1) / BITS_PER_LONG)

static inline void set_bit(int nr, unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p  |= mask;
}

static inline void clear_bit(int nr, unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p &= ~mask;
}
static inline int test_bit(int nr, const unsigned long *addr)
{
	return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

static inline void change_bit(int nr, unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p ^= mask;
}

int fhs(unsigned long n);
int first_bit_zero(const unsigned long * addr);
int first_bit_one(const unsigned long *addr);
#endif
