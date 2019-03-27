/* This file is in the public domain. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/types.h>
#include <sys/systm.h>

#include <sodium/utils.h>

void
sodium_memzero(void *b, size_t n)
{
	explicit_bzero(b, n);
}
