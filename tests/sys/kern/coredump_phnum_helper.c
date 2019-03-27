/*
 * Copyright (c) 2016, Conrad Meyer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/mman.h>

#include <err.h>
#include <stdint.h>
#include <stdlib.h>

/*
 * This program is intended to create a bunch of segment mappings, then dump
 * core.
 */
int
main(int argc __unused, char **argv __unused)
{
	void *v;
	unsigned i;

	for (i = 0; i < UINT16_MAX + 1000; i++) {
		/*
		 * Alternate protections; otherwise the kernel will just extend
		 * the adjacent same-protection previous mapping.
		 */
		v = mmap(NULL, PAGE_SIZE,
		    (((i % 2) == 0) ? PROT_READ : 0) | PROT_WRITE,
		    MAP_ANON | MAP_PRIVATE, -1, 0);
		if (v == MAP_FAILED)
			err(1, "mmap");
	}

	/* Dump core. */
	abort();
}
