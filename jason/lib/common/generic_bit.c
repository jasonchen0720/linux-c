#include "generic_bit.h"
/*
 * Find the highest bit set.
 */
int fhs(unsigned long n)
{
	int p = 0;
	while (n) {
		n >>= 1;
		p++;
	}
	return p;
}
int first_bit_zero(const unsigned long * addr)
{
	int offset = 0;
	const unsigned long *base;
	unsigned long tmp;

	base = addr;

	while ((tmp = *addr) == BITS_MSK_LONG)
		addr++;

	offset = (addr - base) * BITS_PER_LONG;
	while ((tmp & 0xff) == 0xff) {
		offset += 8;
		tmp >>= 8;
	}
	while (tmp & 1) {
		offset++;
		tmp >>= 1;
	}
	return offset;
}

int first_bit_one(const unsigned long *addr)
{
	int offset = 0;
	const unsigned long *base;
	unsigned long tmp;

	base = addr;

	while ((tmp = *addr) == 0)
		addr++;

	offset = (addr - base) * BITS_PER_LONG;
	while (!(tmp & 0xff)) {
		offset += 8;
		tmp >>= 8;
	}
	while (!(tmp & 1)) {
		offset++;
		tmp >>= 1;
	}

	return offset;
}
