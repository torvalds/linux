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
 * Memory-backed NVRAM I/O context.
 *
 * ioptr instances are gauranteed to provide persistent references to its
 * backing contigious memory via bhnd_nvram_io_read_ptr() and
 * bhnd_nvram_io_write_ptr().
 */
struct bhnd_nvram_ioptr {
	struct bhnd_nvram_io	 io;		/**< common I/O instance state */
	void			*ptr;		/**< backing memory */
	size_t			 size;		/**< size at @p ptr */
	size_t			 capacity;	/**< capacity at @p ptr */
	uint32_t		 flags;		/**< flags (see BHND_NVRAM_IOPTR_*) */
};

BHND_NVRAM_IOPS_DEFN(ioptr)

/**
 * Allocate and return a new I/O context, mapping @p size bytes at @p ptr.
 
 * The caller is responsible for deallocating the returned I/O context via
 * bhnd_nvram_io_free().
 *
 * @param	ptr		The pointer to be mapped by the returned I/O
 *				context. Must remain valid for the lifetime of
 *				the returned I/O context.
 * @param	size		The total number of bytes mapped at @p ptr.
 * @param	capacity	The maximum number of bytes that may be mapped
 *				at @p ptr via bhnd_nvram_ioptr_setsize().
 * @param	flags		Access flags (see BHND_NVRAM_IOPTR_*).
 *
 * @retval	bhnd_nvram_io	success.
 * @retval	NULL		allocation failed.
 * @retval	NULL		the requested @p capacity is less than @p size.
 */
struct bhnd_nvram_io *
bhnd_nvram_ioptr_new(const void *ptr, size_t size, size_t capacity,
    uint32_t flags)
{
	struct bhnd_nvram_ioptr	*ioptr;

	/* Sanity check the capacity */
	if (size > capacity)
		return (NULL);
	
	/* Allocate I/O context */
	ioptr = bhnd_nv_malloc(sizeof(*ioptr));
	if (ioptr == NULL)
		return (NULL);
	
	ioptr->io.iops = &bhnd_nvram_ioptr_ops;
	ioptr->ptr = __DECONST(void *, ptr);
	ioptr->size = size;
	ioptr->capacity = capacity;
	ioptr->flags = flags;

	return (&ioptr->io);
}

static void
bhnd_nvram_ioptr_free(struct bhnd_nvram_io *io)
{	
	bhnd_nv_free(io);
}

static size_t
bhnd_nvram_ioptr_getsize(struct bhnd_nvram_io *io)
{
	struct bhnd_nvram_ioptr	*ioptr = (struct bhnd_nvram_ioptr *)io;
	return (ioptr->size);
}

static int
bhnd_nvram_ioptr_setsize(struct bhnd_nvram_io *io, size_t size)
{
	struct bhnd_nvram_ioptr	*ioptr = (struct bhnd_nvram_ioptr *)io;

	/* Must be writable */
	if (!(ioptr->flags & BHND_NVRAM_IOPTR_RDWR))
		return (ENODEV);
	
	/* Can't exceed the actual capacity */
	if (size > ioptr->capacity)
		return (ENXIO);
	
	ioptr->size = size;
	return (0);
}

/* Common ioptr_(read|write)_ptr implementation */
static int
bhnd_nvram_ioptr_ptr(struct bhnd_nvram_ioptr *ioptr, size_t offset, void **ptr,
		     size_t nbytes, size_t *navail)
{
	size_t avail;
	
	/* Verify offset+nbytes fall within the buffer range */
	if (offset > ioptr->size)
		return (ENXIO);
	
	avail = ioptr->size - offset;
	if (avail < nbytes)
		return (ENXIO);
	
	/* Valid I/O range, provide a pointer to the buffer and the
	 * total count of available bytes */
	*ptr = ((uint8_t *)ioptr->ptr) + offset;
	if (navail != NULL)
		*navail = avail;
	
	return (0);
}

static int
bhnd_nvram_ioptr_read_ptr(struct bhnd_nvram_io *io, size_t offset,
			  const void **ptr, size_t nbytes, size_t *navail)
{
	struct bhnd_nvram_ioptr	*ioptr;
	void			*writep;
	int			 error;
	
	ioptr = (struct bhnd_nvram_ioptr *) io;
	
	/* Return a pointer into our backing buffer */
	error = bhnd_nvram_ioptr_ptr(ioptr, offset, &writep, nbytes, navail);
	if (error)
		return (error);
	
	*ptr = writep;
	
	return (0);
}

static int
bhnd_nvram_ioptr_write_ptr(struct bhnd_nvram_io *io, size_t offset,
			   void **ptr, size_t nbytes, size_t *navail)
{
	struct bhnd_nvram_ioptr	*ioptr;
	
	ioptr = (struct bhnd_nvram_ioptr *) io;

	/* Must be writable */
	if (!(ioptr->flags & BHND_NVRAM_IOPTR_RDWR))
		return (ENODEV);
	
	/* Return a pointer into our backing buffer */
	return (bhnd_nvram_ioptr_ptr(ioptr, offset, ptr, nbytes, navail));
}

static int
bhnd_nvram_ioptr_read(struct bhnd_nvram_io *io, size_t offset, void *buffer,
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
bhnd_nvram_ioptr_write(struct bhnd_nvram_io *io, size_t offset,
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
