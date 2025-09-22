/*	$OpenBSD: htons.c,v 1.10 2024/04/15 14:30:48 naddy Exp $ */
/*
 * Public domain.
 */

#include <sys/types.h>
#include <endian.h>

#undef htons

uint16_t
htons(uint16_t x)
{
	return htobe16(x);
}
