/*-
 * Copyright 2018 Toomas Soome <tsoome@me.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include "libi386.h"

/*
 * The idea is borrowed from pxe.c and zfsimpl.c. The original buffer
 * space in pxe.c was 2x 0x2000. Allocating it from BSS will give us needed
 * memory below 1MB and usable for real mode calls.
 *
 * Note the allocations and frees are to be done in reverse order (LIFO).
 */

static char bio_buffer[BIO_BUFFER_SIZE];
static char *bio_buffer_end = bio_buffer + BIO_BUFFER_SIZE;
static char *bio_buffer_ptr = bio_buffer;

void *
bio_alloc(size_t size)
{
	char *ptr;

	ptr = bio_buffer_ptr;
	if (ptr + size > bio_buffer_end)
		return (NULL);
	bio_buffer_ptr += size;

	return (ptr);
}

void
bio_free(void *ptr, size_t size)
{

	if (ptr == NULL)
		return;

	bio_buffer_ptr -= size;
	if (bio_buffer_ptr != ptr)
		panic("bio_alloc()/bio_free() mismatch\n");
}
