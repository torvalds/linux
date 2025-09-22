/*
 * Written by Michael Shalayeff. Public Domain
 */

#include "math.h"

float
remainderf(float x, float p)
{
	__asm__ volatile("frem,sgl %0,%1,%0" : "+f" (x) : "f" (p));

	return (x);
}
DEF_STD(remainderf);
