/*	$OpenBSD: htonl.c,v 1.8 2024/04/15 14:30:48 naddy Exp $ */
/*
 * Public domain.
 */

#include <sys/types.h>
#include <endian.h>

#undef htonl

uint32_t
htonl(uint32_t x)
{
	return htobe32(x);
}
