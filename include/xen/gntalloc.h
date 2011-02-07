/******************************************************************************
 * gntalloc.h
 *
 * Interface to /dev/xen/gntalloc.
 *
 * Author: Daniel De Graaf <dgdegra@tycho.nsa.gov>
 *
 * This file is in the public domain.
 */

#ifndef __LINUX_PUBLIC_GNTALLOC_H__
#define __LINUX_PUBLIC_GNTALLOC_H__

/*
 * Allocates a new page and creates a new grant reference.
 */
#define IOCTL_GNTALLOC_ALLOC_GREF \
_IOC(_IOC_NONE, 'G', 5, sizeof(struct ioctl_gntalloc_alloc_gref))
struct ioctl_gntalloc_alloc_gref {
	/* IN parameters */
	/* The ID of the domain to be given access to the grants. */
	uint16_t domid;
	/* Flags for this mapping */
	uint16_t flags;
	/* Number of pages to map */
	uint32_t count;
	/* OUT parameters */
	/* The offset to be used on a subsequent call to mmap(). */
	uint64_t index;
	/* The grant references of the newly created grant, one per page */
	/* Variable size, depending on count */
	uint32_t gref_ids[1];
};

#define GNTALLOC_FLAG_WRITABLE 1

/*
 * Deallocates the grant reference, allowing the associated page to be freed if
 * no other domains are using it.
 */
#define IOCTL_GNTALLOC_DEALLOC_GREF \
_IOC(_IOC_NONE, 'G', 6, sizeof(struct ioctl_gntalloc_dealloc_gref))
struct ioctl_gntalloc_dealloc_gref {
	/* IN parameters */
	/* The offset returned in the map operation */
	uint64_t index;
	/* Number of references to unmap */
	uint32_t count;
};
#endif /* __LINUX_PUBLIC_GNTALLOC_H__ */
