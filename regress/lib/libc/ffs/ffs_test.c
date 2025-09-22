/*	$OpenBSD: ffs_test.c,v 1.1 2020/06/26 20:16:22 naddy Exp $ */
/*
 * Written by Christian Weisgerber <naddy@openbsd.org>.
 * Public domain.
 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

int
main(void)
{
	assert(ffs(0) == 0);
	assert(ffs(0x8080) == 8);
	assert(ffs(INT32_MIN) == 32);
	return (0);
}
