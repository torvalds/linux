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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/systm.h>
#else /* !_KERNEL */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#endif /* _KERNEL */

#include "bhnd_nvram_private.h"

#include "bhnd_nvram_io.h"
#include "bhnd_nvram_iovar.h"

/**
 * Buffer-backed NVRAM I/O context.
 * 
 * iobuf instances are gauranteed to provide persistent references to its
 * backing contigious buffer via bhnd_nvram_io_read_ptr() and
 * bhnd_nvram_io_write_ptr().
 */
struct bhnd_nvram_iobuf {
	struct bhnd_nvram_io	 io;		/**< common I/O instance state */
	void			*buf;		/**< backing buffer. if inline-allocated, will
						     be a reference to data[]. */
	size_t			 size;		/**< size of @p buf */
	size_t			 capacity;	/**< capacity of @p buf */
	uint8_t			 data[];	/**< inline buffer allocation */
};

BHND_NVRAM_IOPS_DEFN(iobuf)

/**
 * Allocate and return a new I/O context with an uninitialized
 * buffer of @p size and @p capacity.
 *
 * The caller is responsible for deallocating the returned I/O context via
 * bhnd_nvram_io_free().
 *
 * If @p capacity is less than @p size, a capacity of @p size will be used.
 * 
 * @param	size		The initial size of the I/O context.
 * @param	capacity	The total capacity of the I/O context buffer;
 *				the returned I/O context may be resized up to
 *				@p capacity via bhnd_nvram_io_setsize().
 *
 * @retval	bhnd_nvram_iobuf	success.
 * @retval	NULL			allocation failed.
 * @retval	NULL			the requested @p capacity is less than
 *					@p size.
 */
struct bhnd_nvram_io *
bhnd_nvram_iobuf_empty(size_t size, size_t capacity)
{
	struct bhnd_nvram_iobuf	*iobuf;
	size_t			 iosz;
	bool			 inline_alloc;

	/* Sanity check the capacity */
	if (size > capacity)
		return (NULL);

	/* Would sizeof(iobuf)+capacity overflow? */
	if (SIZE_MAX - sizeof(*iobuf) < capacity) {
		inline_alloc = false;
		iosz = sizeof(*iobuf);
	} else {
		inline_alloc = true;
		iosz = sizeof(*iobuf) + capacity;
	}

	/* Allocate I/O context */
	iobuf = bhnd_nv_malloc(iosz);
	if (iobuf == NULL)
		return (NULL);

	iobuf->io.iops = &bhnd_nvram_iobuf_ops;
	iobuf->buf = NULL;
	iobuf->size = size;
	iobuf->capacity = capacity;

	/* Either allocate our backing buffer, or initialize the
	 * backing buffer with a reference to our inline allocation. */
	if (inline_alloc)
		iobuf->buf = &iobuf->data;
	else
		iobuf->buf = bhnd_nv_malloc(iobuf->capacity);


	if (iobuf->buf == NULL) {
		bhnd_nv_free(iobuf);
		return (NULL);
	}

	return (&iobuf->io);
}

/**
 * Allocate and return a new I/O context, copying @p size from @p buffer.
 *
 * The caller is responsible for deallocating the returned I/O context via
 * bhnd_nvram_io_free().
 * 
 * @param	buffer	The buffer data be copied by the returned I/O context.
 * @param	size	The size of @p buffer, in bytes.
 * 
 * @retval	bhnd_nvram_io	success.
 * @retval	NULL		allocation failed.
 */
struct bhnd_nvram_io *
bhnd_nvram_iobuf_new(const void *buffer, size_t size)
{
	struct bhnd_nvram_io	*io;
	struct bhnd_nvram_iobuf	*iobuf;

	/* Allocate the iobuf */
	if ((io = bhnd_nvram_iobuf_empty(size, size)) == NULL)
		return (NULL);

	/* Copy the input to our new iobuf instance */
	iobuf = (struct bhnd_nvram_iobuf *)io;
	memcpy(iobuf->buf, buffer, iobuf->size);

	return (io);
}

/**
 * Allocate and return a new I/O context providing an in-memory copy
 * of the data mapped by @p src.
 *
 * The caller is responsible for deallocating the returned I/O context via
 * bhnd_nvram_io_free().
 * 
 * @param	src	The I/O context to be copied.
 * 
 * @retval	bhnd_nvram_io	success.
 * @retval	NULL		allocation failed.
 * @retval	NULL		copying @p src failed.
 */
struct bhnd_nvram_io *
bhnd_nvram_iobuf_copy(struct bhnd_nvram_io *src)
{
	return (bhnd_nvram_iobuf_copy_range(src, 0x0,
	    bhnd_nvram_io_getsize(src)));
}

/**
 * Allocate and return a new I/O context providing an in-memory copy
 * of @p size bytes mapped at @p offset by @p src.
 *
 * The caller is responsible for deallocating the returned I/O context via
 * bhnd_nvram_io_free().
 * 
 * @param	src	The I/O context to be copied.
 * @param	offset	The offset of the bytes to be copied from @p src.
 * @param	size	The number of bytes to copy at @p offset from @p src.
 * 
 * @retval	bhnd_nvram_io	success.
 * @retval	NULL		allocation failed.
 * @retval	NULL		copying @p src failed.
 */
struct bhnd_nvram_io *
bhnd_nvram_iobuf_copy_range(struct bhnd_nvram_io *src, size_t offset,
    size_t size)
{
	struct bhnd_nvram_io	*io;
	struct bhnd_nvram_iobuf	*iobuf;
	int			 error;

	/* Check if offset+size would overflow */
	if (SIZE_MAX - size < offset)
		return (NULL);

	/* Allocate the iobuf instance */
	if ((io = bhnd_nvram_iobuf_empty(size, size)) == NULL)
		return (NULL);

	/* Copy the input I/O context */
	iobuf = (struct bhnd_nvram_iobuf *)io;
	if ((error = bhnd_nvram_io_read(src, offset, iobuf->buf, size))) {
		bhnd_nvram_io_free(&iobuf->io);
		return (NULL);
	}

	return (io);
}


static void
bhnd_nvram_iobuf_free(struct bhnd_nvram_io *io)
{
	struct bhnd_nvram_iobuf	*iobuf = (struct bhnd_nvram_iobuf *)io;

	/* Free the backing buffer if it wasn't allocated inline */
	if (iobuf->buf != &iobuf->data)
		bhnd_nv_free(iobuf->buf);

	bhnd_nv_free(iobuf);
}

static size_t
bhnd_nvram_iobuf_getsize(struct bhnd_nvram_io *io)
{
	struct bhnd_nvram_iobuf	*iobuf = (struct bhnd_nvram_iobuf *)io;
	return (iobuf->size);
}

static int
bhnd_nvram_iobuf_setsize(struct bhnd_nvram_io *io, size_t size)
{
	struct bhnd_nvram_iobuf	*iobuf = (struct bhnd_nvram_iobuf *)io;

	/* Can't exceed the actual capacity */
	if (size > iobuf->capacity)
		return (ENXIO);

	iobuf->size = size;
	return (0);
}

/* Common iobuf_(read|write)_ptr implementation */
static int
bhnd_nvram_iobuf_ptr(struct bhnd_nvram_iobuf *iobuf, size_t offset, void **ptr,
    size_t nbytes, size_t *navail)
{
	size_t avail;

	/* Verify offset+nbytes fall within the buffer range */
	if (offset > iobuf->size)
		return (ENXIO);

	avail = iobuf->size - offset;
	if (avail < nbytes)
		return (ENXIO);

	/* Valid I/O range, provide a pointer to the buffer and the
	 * total count of available bytes */
	*ptr = ((uint8_t *)iobuf->buf) + offset;
	if (navail != NULL)
		*navail = avail;

	return (0);
}

static int
bhnd_nvram_iobuf_read_ptr(struct bhnd_nvram_io *io, size_t offset,
    const void **ptr, size_t nbytes, size_t *navail)
{
	struct bhnd_nvram_iobuf	*iobuf;
	void			*ioptr;
	int			 error;

	iobuf = (struct bhnd_nvram_iobuf *) io;

	/* Return a pointer into our backing buffer */
	error = bhnd_nvram_iobuf_ptr(iobuf, offset, &ioptr, nbytes, navail);
	if (error)
		return (error);

	*ptr = ioptr;

	return (0);
}

static int
bhnd_nvram_iobuf_write_ptr(struct bhnd_nvram_io *io, size_t offset,
    void **ptr, size_t nbytes, size_t *navail)
{
	struct bhnd_nvram_iobuf	*iobuf;

	iobuf = (struct bhnd_nvram_iobuf *) io;

	/* Return a pointer into our backing buffer */
	return (bhnd_nvram_iobuf_ptr(iobuf, offset, ptr, nbytes, navail));
}

static int
bhnd_nvram_iobuf_read(struct bhnd_nvram_io *io, size_t offset, void *buffer,
    size_t nbytes)
{
	const void	*ptr;
	int		 error;

	/* Try to fetch a direct pointer for at least nbytes */
	if ((error = bhnd_nvram_io_read_ptr(io, offset, &ptr, nbytes, NULL)))
		return (error);

	/* Copy out the requested data */
	memcpy(buffer, ptr, nbytes);
	return (0);
}

static int
bhnd_nvram_iobuf_write(struct bhnd_nvram_io *io, size_t offset,
    void *buffer, size_t nbytes)
{
	void	*ptr;
	int	 error;

	/* Try to fetch a direct pointer for at least nbytes */
	if ((error = bhnd_nvram_io_write_ptr(io, offset, &ptr, nbytes, NULL)))
		return (error);

	/* Copy in the provided data */
	memcpy(ptr, buffer, nbytes);
	return (0);
}
