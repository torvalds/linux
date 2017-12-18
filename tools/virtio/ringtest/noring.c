// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include "main.h"
#include <assert.h>

/* stub implementation: useful for measuring overhead */
void alloc_ring(void)
{
}

/* guest side */
int add_inbuf(unsigned len, void *buf, void *datap)
{
	return 0;
}

/*
 * skb_array API provides no way for producer to find out whether a given
 * buffer was consumed.  Our tests merely require that a successful get_buf
 * implies that add_inbuf succeed in the past, and that add_inbuf will succeed,
 * fake it accordingly.
 */
void *get_buf(unsigned *lenp, void **bufp)
{
	return "Buffer";
}

bool used_empty()
{
	return false;
}

void disable_call()
{
	assert(0);
}

bool enable_call()
{
	assert(0);
}

void kick_available(void)
{
	assert(0);
}

/* host side */
void disable_kick()
{
	assert(0);
}

bool enable_kick()
{
	assert(0);
}

bool avail_empty()
{
	return false;
}

bool use_buf(unsigned *lenp, void **bufp)
{
	return true;
}

void call_used(void)
{
	assert(0);
}
