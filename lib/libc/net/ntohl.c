/*	$OpenBSD: ntohl.c,v 1.8 2024/04/15 14:30:48 naddy Exp $ */
/*
 * Public domain.
 */

#include <sys/types.h>
#include <endian.h>

#undef ntohl

uint32_t
ntohl(uint32_t x)
{
	return be32toh(x);
}
