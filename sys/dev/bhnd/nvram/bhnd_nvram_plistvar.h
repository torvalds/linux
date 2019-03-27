/*-
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
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

#ifndef _BHND_NVRAM_BHND_PLISTVAR_H_
#define _BHND_NVRAM_BHND_PLISTVAR_H_

#include "bhnd_nvram_plist.h"
#include <sys/queue.h>

LIST_HEAD(bhnd_nvram_plist_entry_list, bhnd_nvram_plist_entry);

typedef struct bhnd_nvram_plist_entry		bhnd_nvram_plist_entry;
typedef struct bhnd_nvram_plist_entry_list	bhnd_nvram_plist_entry_list;

/**
 * NVRAM property.
 */
struct bhnd_nvram_prop {
	volatile u_int	 refs;	/**< refcount */

	char		*name;	/**< property name */
	bhnd_nvram_val	*val;	/**< property value */
};

/**
 * NVRAM property list entry.
 */
struct bhnd_nvram_plist_entry {
	bhnd_nvram_prop	*prop;

	TAILQ_ENTRY(bhnd_nvram_plist_entry)	pl_link;
	LIST_ENTRY(bhnd_nvram_plist_entry)	pl_hash_link;
};

/**
 * NVRAM property list.
 * 
 * Provides an ordered list of property values.
 */
struct bhnd_nvram_plist {
	volatile u_int				refs;		/**< refcount */
	TAILQ_HEAD(,bhnd_nvram_plist_entry)	entries;	/**< all properties */
	size_t					num_entries;	/**< entry count */
	bhnd_nvram_plist_entry_list		names[16];	/**< name-based hash table */
};

#endif /* _BHND_NVRAM_BHND_PLISTVAR_H_ */
