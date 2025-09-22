/*	$OpenBSD: ntohs.c,v 1.10 2024/04/15 14:30:48 naddy Exp $ */
/*
 * Public domain.
 */

#include <sys/types.h>
#include <endian.h>

#undef ntohs

uint16_t
ntohs(uint16_t x)
{
	return be16toh(x);
}
