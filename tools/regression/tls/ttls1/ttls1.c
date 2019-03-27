/* $FreeBSD$ */

#include <stdio.h>

extern int __thread xx1;
extern int __thread xx2;
extern int __thread xxa[];
int __thread a[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
extern int xxyy();

int main(int argc, char** argv)
{
	printf("xx1=%d, xx2=%d, xxa[5]=%d, a[5]=%d, xxyy()=%d\n",
	    xx1, xx2, xxa[5], a[5], xxyy());
}
