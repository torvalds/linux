/*
 * Written by Michael Shalayeff. Public Domain
 */

#include <math.h>

double
remainder(double x, double p)
{
	__asm__ volatile("frem,dbl %0,%1,%0" : "+f" (x) : "f" (p));

	return (x);
}
DEF_STD(remainder);
LDBL_UNUSED_CLONE(remainder);
