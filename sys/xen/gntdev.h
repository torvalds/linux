/*-
 * Copyright (c) 2016 Akshay Jaggi <jaggi@FreeBSD.org>
 * All rights reserved.
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
 *
 * gntdev.h
 *
 * Interface to /dev/xen/gntdev.
 *
 * This device provides the user with two kinds of functionalities:
 * 1. Grant Allocation
 *    Allocate a page of our own memory, and share it with a foreign domain.
 * 2. Grant Mapping
 *    Map a grant allocated by a foreign domain, into our own memory.
 *
 *
 * Grant Allocation
 *
 * Steps to allocate a grant:
 * 1. Do an `IOCTL_GNTDEV_ALLOC_GREF ioctl`, with
 *     - `domid`, as the domain-id of the foreign domain
 *     - `flags`, ORed with GNTDEV_ALLOC_FLAG_WRITABLE if you want the foreign
 *       domain to have write access to the shared memory
 *     - `count`, with the number of pages to share with the foreign domain
 *
 *    Ensure that the structure you allocate has enough memory to store
 *    all the allocated grant-refs, i.e., you need to allocate
 *    (sizeof(struct ioctl_gntdev_alloc_gref) + (count - 1)*sizeof(uint32_t))
 *    bytes of memory.
 *
 * 2. Mmap the address given in `index` after a successful ioctl.
 *    This will give you access to the granted pages.
 *
 * Note:
 * 1. The grant is not removed until all three of the following conditions
 *    are met
 *     - The region is not mmaped. That is, munmap() has been called if
 *       the region was mmapped previously.
 *     - IOCTL_GNTDEV_DEALLOC_GREF ioctl has been performed. After you
 *       perform this ioctl, you can no longer mmap or set notify on
 *       the grant.
 *     - The foreign domain has stopped using the grant.
 * 2. Granted pages can only belong to one mmap region.
 * 3. Every page of granted memory is a unit in itself. What this means
 *    is that you can set a unmap notification for each of the granted
 *    pages, individually; you can mmap and dealloc-ioctl a contiguous
 *    range of allocated grants (even if alloc-ioctls were performed
 *    individually), etc.
 *
 *
 * Grant Mapping
 *
 * Steps to map a grant:
 * 1. Do a `IOCTL_GNTDEV_MAP_GRANT_REF` ioctl, with
 *     - `count`, as the number of foreign grants to map
 *     - `refs[i].domid`, as the domain id of the foreign domain
 *     - `refs[i].ref`, as the grant-ref for the grant to be mapped
 *
 * 2. Mmap the address given in `index` after a successful ioctl.
 *    This will give you access to the mapped pages.
 *
 * Note:
 * 1. The map hypercall is not made till the region is mmapped.
 * 2. The unit is defined by the map ioctl. This means that only one
 *    unmap notification can be set on a group of pages that were
 *    mapped together in one ioctl, and also no single mmaping of contiguous
 *    grant-maps is possible.
 * 3. You can mmap the same grant-map region multiple times.
 * 4. The grant is not unmapped until both of the following conditions are met
 *     - The region is not mmaped. That is, munmap() has been called for
 *       as many times as the grant was mmapped.
 *     - IOCTL_GNTDEV_UNMAP_GRANT_REF ioctl has been called.
 * 5. IOCTL_GNTDEV_GET_OFFSET_FOR_VADDR ioctl gives index and count of
 *    a grant-map from the virtual address of the location where the grant
 *    is mmapped.
 *
 *
 * IOCTL_GNTDEV_SET_UNMAP_NOTIFY
 * This ioctl allows us to set notifications to be made when the grant is
 * either unmapped (in case of a mapped grant), or when it is ready to be
 * deallocated by us, ie, the grant is no more mmapped, and the dealloc
 * ioctl has been called (in case of an allocated grant). OR `action` with
 * the required notification masks, and fill in the appropriate fields.
 *  - UNMAP_NOTIFY_CLEAR_BYTE clears the byte at `index`, where index is
 *    the address of the byte in file address space.
 *  - UNMAP_NOTIFY_SEND_EVENT sends an event channel notification on
 *    `event_channel_port`
 * In case of multiple notify ioctls, only the last one survives.
 *
 * $FreeBSD$
 */

#ifndef __XEN_GNTDEV_H__
#define __XEN_GNTDEV_H__

#include <sys/types.h>

#define IOCTL_GNTDEV_SET_UNMAP_NOTIFY					\
	_IOW('E', 0, struct ioctl_gntdev_unmap_notify)
struct ioctl_gntdev_unmap_notify {
    /* IN parameters */
    uint64_t index;
    uint32_t action;
    uint32_t event_channel_port;
};

#define UNMAP_NOTIFY_CLEAR_BYTE 0x1
#define UNMAP_NOTIFY_SEND_EVENT 0x2

/*-------------------- Grant Allocation IOCTLs  ------------------------------*/

#define IOCTL_GNTDEV_ALLOC_GREF						\
	_IOWR('E', 1, struct ioctl_gntdev_alloc_gref)
struct ioctl_gntdev_alloc_gref {
    /* IN parameters */
    uint16_t domid;
    uint16_t flags;
    uint32_t count;
    /* OUT parameters */
    uint64_t index;
    /* Variable OUT parameter */
    uint32_t *gref_ids;
};

#define GNTDEV_ALLOC_FLAG_WRITABLE 1

#define IOCTL_GNTDEV_DEALLOC_GREF					\
	_IOW('E', 2, struct ioctl_gntdev_dealloc_gref)
struct ioctl_gntdev_dealloc_gref {
    /* IN parameters */
    uint64_t index;
    uint32_t count;
};

/*-------------------- Grant Mapping IOCTLs  ---------------------------------*/

struct ioctl_gntdev_grant_ref {
    uint32_t domid;
    uint32_t ref;
};

#define IOCTL_GNTDEV_MAP_GRANT_REF					\
	_IOWR('E', 3, struct ioctl_gntdev_map_grant_ref)
struct ioctl_gntdev_map_grant_ref {
    /* IN parameters */
    uint32_t count;
    uint32_t pad0;
    /* OUT parameters */
    uint64_t index;
    /* Variable IN parameter */
    struct ioctl_gntdev_grant_ref *refs;
};

#define IOCTL_GNTDEV_UNMAP_GRANT_REF					\
	_IOW('E', 4, struct ioctl_gntdev_unmap_grant_ref)
struct ioctl_gntdev_unmap_grant_ref {
    /* IN parameters */
    uint64_t index;
    uint32_t count;
};

#define IOCTL_GNTDEV_GET_OFFSET_FOR_VADDR				\
	_IOWR('E', 5, struct ioctl_gntdev_get_offset_for_vaddr)
struct ioctl_gntdev_get_offset_for_vaddr {
    /* IN parameters */
    uint64_t vaddr;
    /* OUT parameters */
    uint64_t offset;
    uint32_t count;
};

#endif /* __XEN_GNTDEV_H__ */
