/*
 * Trivial program to check that we have a valid 64-bit build environment.
 * Copyright (c) 2015 Andy Lutomirski
 * GPL v2
 */

#ifndef __x86_64__
# error wrong architecture
#endif

#include <stdio.h>

int main()
{
	printf("\n");

	return 0;
}
