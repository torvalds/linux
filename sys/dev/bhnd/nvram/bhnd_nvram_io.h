/*-
 * Copyright (c) 2016 Landon Fuller <landonf@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 * 
 * $FreeBSD$
 */

#ifndef _BHND_NVRAM_BHND_NVRAM_IO_H_
#define _BHND_NVRAM_BHND_NVRAM_IO_H_

#ifdef _KERNEL
#include <sys/param.h>

#include <dev/bhnd/bhnd.h>
#else /* !_KERNEL */
#include <errno.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#endif /* _KERNEL */

struct bhnd_nvram_io;

struct bhnd_nvram_io	*bhnd_nvram_iobuf_new(const void *buffer, size_t size);
struct bhnd_nvram_io	*bhnd_nvram_iobuf_empty(size_t size, size_t capacity);
struct bhnd_nvram_io	*bhnd_nvram_iobuf_copy(struct bhnd_nvram_io *src);
struct bhnd_nvram_io	*bhnd_nvram_iobuf_copy_range(struct bhnd_nvram_io *src,
			     size_t offset, size_t size);

struct bhnd_nvram_io	*bhnd_nvram_ioptr_new(const void *ptr, size_t size,
			     size_t capacity, uint32_t flags);

#ifdef _KERNEL
struct bhnd_nvram_io	*bhnd_nvram_iores_new(struct bhnd_resource *r,
			     bus_size_t offset, bus_size_t size,
			     u_int bus_width);
#endif /* _KERNEL */

size_t			 bhnd_nvram_io_getsize(struct bhnd_nvram_io *io);
int			 bhnd_nvram_io_setsize(struct bhnd_nvram_io *io,
			     size_t size);

int			 bhnd_nvram_io_read(struct bhnd_nvram_io *io,
			     size_t offset, void *buffer, size_t nbytes);
int			 bhnd_nvram_io_read_ptr(struct bhnd_nvram_io *io,
			     size_t offset, const void **ptr, size_t nbytes,
			     size_t *navail);

int			 bhnd_nvram_io_write(struct bhnd_nvram_io *io,
			     size_t offset, void *buffer, size_t nbytes);
int			 bhnd_nvram_io_write_ptr(struct bhnd_nvram_io *io,
			     size_t offset, void **ptr, size_t nbytes,
			     size_t *navail);

void			 bhnd_nvram_io_free(struct bhnd_nvram_io *io);

/**
 * bhnd_nvram_ioptr flags
 */
enum {
	BHND_NVRAM_IOPTR_RDONLY	= (1<<0),	/**< read-only */
	BHND_NVRAM_IOPTR_RDWR	= (1<<1),	/**< read/write */
};

#endif /* _BHND_NVRAM_BHND_NVRAM_IO_H_ */
