/* This file is in the public domain. */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");
#include <sys/libkern.h>

#include <sodium/randombytes.h>

void
randombytes_buf(void *buf, size_t size)
{
	arc4random_buf(buf, size);
}
