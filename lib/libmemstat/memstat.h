/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2005 Robert N. M. Watson
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
 * $FreeBSD$
 */

#ifndef _MEMSTAT_H_
#define	_MEMSTAT_H_

/*
 * Amount of caller data to maintain for each caller data slot.  Applications
 * must not request more than this number of caller save data, or risk
 * corrupting internal libmemstat(3) data structures.  A compile time check
 * in the application is probably appropriate.
 */
#define	MEMSTAT_MAXCALLER	16

/*
 * libmemstat(3) is able to extract memory data from different allocators;
 * when it does so, it tags which allocator it got the data from so that
 * consumers can determine which fields are usable, as data returned varies
 * some.
 */
#define	ALLOCATOR_UNKNOWN	0
#define	ALLOCATOR_MALLOC	1
#define	ALLOCATOR_UMA		2
#define	ALLOCATOR_ANY		255

/*
 * Library maximum type name.  Should be max(set of name maximums over
 * various allocators).
 */
#define	MEMTYPE_MAXNAME		32

/*
 * Library error conditions, mostly from the underlying data sources.  On
 * failure, functions typically return (-1) or (NULL); on success, (0) or a
 * valid data pointer.  The error from the last operation is stored in
 * struct memory_type_list, and accessed via memstat_get_error(list).
 */
#define	MEMSTAT_ERROR_UNDEFINED		0	/* Initialization value. */
#define	MEMSTAT_ERROR_NOMEMORY		1	/* Out of memory. */
#define	MEMSTAT_ERROR_VERSION		2	/* Unsupported version. */
#define	MEMSTAT_ERROR_PERMISSION	3	/* Permission denied. */
#define	MEMSTAT_ERROR_DATAERROR		5	/* Error in stat data. */
#define	MEMSTAT_ERROR_KVM		6	/* See kvm_geterr() for err. */
#define	MEMSTAT_ERROR_KVM_NOSYMBOL	7	/* Symbol not available. */
#define	MEMSTAT_ERROR_KVM_SHORTREAD	8	/* Short kvm_read return. */

/*
 * Forward declare struct memory_type, which holds per-type properties and
 * statistics.  This is an opaque type, to be frobbed only from within the
 * library, in order to avoid building ABI assumptions into the application.
 * Accessor methods should be used to get and sometimes set the fields from
 * consumers of the library.
 */
struct memory_type;

/*
 * struct memory_type_list is the head of a list of memory types and
 * statistics.
 */
struct memory_type_list;

__BEGIN_DECLS
/*
 * Functions that operate without memory type or memory type list context.
 */
const char	*memstat_strerror(int error);

/*
 * Functions for managing memory type and statistics data.
 */
struct memory_type_list	*memstat_mtl_alloc(void);
struct memory_type	*memstat_mtl_first(struct memory_type_list *list);
struct memory_type	*memstat_mtl_next(struct memory_type *mtp);
struct memory_type	*memstat_mtl_find(struct memory_type_list *list,
			    int allocator, const char *name);
void	memstat_mtl_free(struct memory_type_list *list);
int	memstat_mtl_geterror(struct memory_type_list *list);

/*
 * Functions to retrieve data from a live kernel using sysctl.
 */
int	memstat_sysctl_all(struct memory_type_list *list, int flags);
int	memstat_sysctl_malloc(struct memory_type_list *list, int flags);
int	memstat_sysctl_uma(struct memory_type_list *list, int flags);

/*
 * Functions to retrieve data from a kernel core (or /dev/kmem).
 */
int	memstat_kvm_all(struct memory_type_list *list, void *kvm_handle);
int	memstat_kvm_malloc(struct memory_type_list *list, void *kvm_handle);
int	memstat_kvm_uma(struct memory_type_list *list, void *kvm_handle);

/*
 * Accessor methods for struct memory_type.
 */
const char	*memstat_get_name(const struct memory_type *mtp);
int		 memstat_get_allocator(const struct memory_type *mtp);
uint64_t	 memstat_get_countlimit(const struct memory_type *mtp);
uint64_t	 memstat_get_byteslimit(const struct memory_type *mtp);
uint64_t	 memstat_get_sizemask(const struct memory_type *mtp);
uint64_t	 memstat_get_size(const struct memory_type *mtp);
uint64_t	 memstat_get_rsize(const struct memory_type *mtp);
uint64_t	 memstat_get_memalloced(const struct memory_type *mtp);
uint64_t	 memstat_get_memfreed(const struct memory_type *mtp);
uint64_t	 memstat_get_numallocs(const struct memory_type *mtp);
uint64_t	 memstat_get_numfrees(const struct memory_type *mtp);
uint64_t	 memstat_get_bytes(const struct memory_type *mtp);
uint64_t	 memstat_get_count(const struct memory_type *mtp);
uint64_t	 memstat_get_free(const struct memory_type *mtp);
uint64_t	 memstat_get_failures(const struct memory_type *mtp);
uint64_t	 memstat_get_sleeps(const struct memory_type *mtp);
void		*memstat_get_caller_pointer(const struct memory_type *mtp,
		    int index);
void		 memstat_set_caller_pointer(struct memory_type *mtp,
		    int index, void *value);
uint64_t	 memstat_get_caller_uint64(const struct memory_type *mtp,
		    int index);
void		 memstat_set_caller_uint64(struct memory_type *mtp, int index,
		    uint64_t value);
uint64_t	 memstat_get_zonefree(const struct memory_type *mtp);
uint64_t	 memstat_get_kegfree(const struct memory_type *mtp);
uint64_t	 memstat_get_percpu_memalloced(const struct memory_type *mtp,
		    int cpu);
uint64_t	 memstat_get_percpu_memfreed(const struct memory_type *mtp,
		    int cpu);
uint64_t	 memstat_get_percpu_numallocs(const struct memory_type *mtp,
		    int cpu);
uint64_t	 memstat_get_percpu_numfrees(const struct memory_type *mtp,
		    int cpu);
uint64_t	 memstat_get_percpu_sizemask(const struct memory_type *mtp,
		    int cpu);
void		*memstat_get_percpu_caller_pointer(
		    const struct memory_type *mtp, int cpu, int index);
void		 memstat_set_percpu_caller_pointer(struct memory_type *mtp,
		    int cpu, int index, void *value);
uint64_t	 memstat_get_percpu_caller_uint64(
		    const struct memory_type *mtp, int cpu, int index);
void		 memstat_set_percpu_caller_uint64(struct memory_type *mtp,
		    int cpu, int index, uint64_t value);
uint64_t	 memstat_get_percpu_free(const struct memory_type *mtp,
		    int cpu);
__END_DECLS

#endif /* !_MEMSTAT_H_ */
