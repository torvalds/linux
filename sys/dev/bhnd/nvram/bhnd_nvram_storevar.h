/*-
 * Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
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

#ifndef _BHND_NVRAM_BHND_NVRAM_STOREVAR_H_
#define _BHND_NVRAM_BHND_NVRAM_STOREVAR_H_

#include <sys/types.h>

#ifndef _KERNEL
#include <pthread.h>
#endif

#include "bhnd_nvram_plist.h"

#include "bhnd_nvram_store.h"

/** Index is only generated if minimum variable count is met */
#define	BHND_NV_IDX_VAR_THRESHOLD	15

#define	BHND_NVSTORE_ROOT_PATH		"/"
#define	BHND_NVSTORE_ROOT_PATH_LEN	sizeof(BHND_NVSTORE_ROOT_PATH)

#define BHND_NVSTORE_GET_FLAG(_value, _flag)	\
	(((_value) & BHND_NVSTORE_ ## _flag) != 0)
#define	BHND_NVSTORE_GET_BITS(_value, _field)	\
	((_value) & BHND_NVSTORE_ ## _field ## _MASK)

/* Forward declarations */
typedef struct bhnd_nvstore_name_info	bhnd_nvstore_name_info;
typedef struct bhnd_nvstore_index	bhnd_nvstore_index;
typedef struct bhnd_nvstore_path	bhnd_nvstore_path;

typedef struct bhnd_nvstore_alias	bhnd_nvstore_alias;

typedef struct bhnd_nvstore_alias_list	bhnd_nvstore_alias_list;
typedef struct bhnd_nvstore_update_list	bhnd_nvstore_update_list;
typedef struct bhnd_nvstore_path_list	bhnd_nvstore_path_list;

LIST_HEAD(bhnd_nvstore_alias_list,	bhnd_nvstore_alias);
LIST_HEAD(bhnd_nvstore_update_list,	bhnd_nvstore_update);
LIST_HEAD(bhnd_nvstore_path_list,	bhnd_nvstore_path);

/**
 * NVRAM store variable entry types.
 */
typedef enum {
	BHND_NVSTORE_VAR	= 0,	/**< simple variable (var=...) */
	BHND_NVSTORE_ALIAS_DECL	= 1,	/**< alias declaration ('devpath0=pci/1/1') */	
} bhnd_nvstore_var_type;

/**
 * NVRAM path descriptor types.
 */
typedef enum {
	BHND_NVSTORE_PATH_STRING	= 0,	/**< path is a string value */
	BHND_NVSTORE_PATH_ALIAS		= 1	/**< path is an alias reference */
} bhnd_nvstore_path_type;

/**
 * NVRAM variable namespaces.
 */
typedef enum {
	BHND_NVSTORE_NAME_INTERNAL	= 1,	/**< internal namespace. permits
						     use of reserved devpath and
						     alias name prefixes. */
	BHND_NVSTORE_NAME_EXTERNAL	= 2,	/**< external namespace. forbids
						     use of name prefixes used
						     for device path handling */
} bhnd_nvstore_name_type;

bhnd_nvstore_path	*bhnd_nvstore_path_new(const char *path_str,
			     size_t path_len);
void			 bhnd_nvstore_path_free(struct bhnd_nvstore_path *path);

bhnd_nvstore_index	*bhnd_nvstore_index_new(size_t capacity);
void			 bhnd_nvstore_index_free(bhnd_nvstore_index *index);
int			 bhnd_nvstore_index_append(struct bhnd_nvram_store *sc,
			     bhnd_nvstore_index *index,
			     void *cookiep);
int			 bhnd_nvstore_index_prepare(
			     struct bhnd_nvram_store *sc,
			     bhnd_nvstore_index *index);
void			*bhnd_nvstore_index_lookup(struct bhnd_nvram_store *sc,
			     bhnd_nvstore_index *index, const char *name);

bhnd_nvstore_path	*bhnd_nvstore_get_root_path(
			    struct bhnd_nvram_store *sc);
bool			 bhnd_nvstore_is_root_path(struct bhnd_nvram_store *sc,
			     bhnd_nvstore_path *path);

void			*bhnd_nvstore_path_data_next(
			     struct bhnd_nvram_store *sc,
			     bhnd_nvstore_path *path, void **indexp);
void			*bhnd_nvstore_path_data_lookup(
			     struct bhnd_nvram_store *sc,
			     bhnd_nvstore_path *path, const char *name);
bhnd_nvram_prop		*bhnd_nvstore_path_get_update(
			     struct bhnd_nvram_store *sc,
			     bhnd_nvstore_path *path, const char *name);
int			 bhnd_nvstore_path_register_update(
			     struct bhnd_nvram_store *sc,
			     bhnd_nvstore_path *path, const char *name,
			     bhnd_nvram_val *value);

bhnd_nvstore_alias	*bhnd_nvstore_find_alias(struct bhnd_nvram_store *sc,
			     const char *path);
bhnd_nvstore_alias	*bhnd_nvstore_get_alias(struct bhnd_nvram_store *sc,
			     u_long alias_val);

bhnd_nvstore_path	*bhnd_nvstore_get_path(struct bhnd_nvram_store *sc,
			     const char *path, size_t path_len);
bhnd_nvstore_path	*bhnd_nvstore_resolve_path_alias(
			     struct bhnd_nvram_store *sc, u_long aval);

bhnd_nvstore_path	*bhnd_nvstore_var_get_path(struct bhnd_nvram_store *sc,
			     bhnd_nvstore_name_info *info);
int			 bhnd_nvstore_var_register_path(
			     struct bhnd_nvram_store *sc,
			     bhnd_nvstore_name_info *info, void *cookiep);

int			 bhnd_nvstore_register_path(struct bhnd_nvram_store *sc,
			     const char *path, size_t path_len);
int			 bhnd_nvstore_register_alias(
			     struct bhnd_nvram_store *sc,
			     const bhnd_nvstore_name_info *info, void *cookiep);

const char		*bhnd_nvstore_parse_relpath(const char *parent,
			     const char *child);
int			 bhnd_nvstore_parse_name_info(const char *name,
			     bhnd_nvstore_name_type name_type,
			     uint32_t data_caps, bhnd_nvstore_name_info *info);

/**
 * NVRAM variable name descriptor.
 * 
 * For NVRAM data instances supporting BHND_NVRAM_DATA_CAP_DEVPATHS, the
 * NVRAM-vended variable name will be in one of four formats:
 * 
 * - Simple Variable:
 * 	'variable'
 * - Device Variable:
 * 	'pci/1/1/variable'
 * - Device Alias Variable:
 * 	'0:variable'
 * - Device Path Alias Definition:
 * 	'devpath0=pci/1/1/variable'
 *
 * Device Paths:
 * 
 * The device path format is device class-specific; the known supported device
 * classes are:
 * 	- sb:		BCMA/SIBA SoC core device path.
 *	- pci:		PCI device path (and PCIe on some earlier devices).
 *	- pcie:		PCIe device path.
 *	- usb:		USB device path.
 *
 * The device path format is loosely defined as '[class]/[domain]/[bus]/[slot]',
 * with missing values either assumed to be zero, a value specific to the
 * device class, or irrelevant to the device class in question.
 * 
 * Examples:
 *	sb/1			BCMA/SIBA backplane 0, core 1.
 *	pc/1/1			PCMCIA bus 1, slot 1
 *	pci/1/1			PCI/PCIe domain 0, bus 1, device 1
 *	pcie/1/1		PCIe domain 0, bus 1, device 1
 *	usb/0xbd17		USB PID 0xbd17 (VID defaults to Broadcom 0x0a5c)
 *
 * Device Path Aliases:
 * 
 * Device path aliases reduce duplication of device paths in the flash encoding
 * of NVRAM data; a single devpath[alias]=[devpath] variable entry is defined,
 * and then later variables may reference the device path via its alias:
 * 	devpath1=usb/0xbd17
 *	1:mcs5gpo0=0x1100
 * 
 * Alias values are always positive, base 10 integers.
 */
struct bhnd_nvstore_name_info {
	const char		*name;		/**< variable name */
	bhnd_nvstore_var_type	 type;		/**< variable type */
	bhnd_nvstore_path_type	 path_type;	/**< path type */

	/** Path information */
	union {
		/* BHND_NVSTORE_PATH_STRING */
		struct {
			const char	*value;		/**< device path */
			size_t		 value_len;	/**< device path length */
		} str;

		/** BHND_NVSTORE_PATH_ALIAS */
		struct {
			u_long		 value;		/**< device alias */
		} alias;
	} path;
};

/**
 * NVRAM variable index.
 * 
 * Provides effecient name-based lookup by maintaining an array of cached
 * cookiep values, sorted lexicographically by relative variable name.
 */
struct bhnd_nvstore_index {
	size_t	 count;		/**< entry count */
	size_t	 capacity;	/**< entry capacity */
	void	*cookiep[];	/**< cookiep values */
};

/**
 * NVRAM device path.
 */
struct bhnd_nvstore_path {
	char				*path_str;	/**< canonical path string */
	size_t				 num_vars;	/**< per-path count of committed
							     (non-pending) variables */
	bhnd_nvstore_index		*index;		/**< per-path index, or NULL if
							     this is a root path for
							     which the data source
							     may be queried directly. */
	bhnd_nvram_plist		*pending;	/**< pending changes */

	LIST_ENTRY(bhnd_nvstore_path) np_link;
};

/**
 * NVRAM device path alias.
 */
struct bhnd_nvstore_alias {
	bhnd_nvstore_path	*path;		/**< borrowed path reference */
	void			*cookiep;	/**< NVRAM variable's cookiep value */
	u_long			 alias;		/**< alias value */

	LIST_ENTRY(bhnd_nvstore_alias) na_link;
};

/** bhnd nvram store instance state */
struct bhnd_nvram_store {
#ifdef _KERNEL
	struct mtx		 mtx;
#else
	pthread_mutex_t		 mtx;
#endif
	struct bhnd_nvram_data	*data;		/**< backing data */
	uint32_t		 data_caps;	/**< data capability flags */
	bhnd_nvram_plist	*data_opts;	/**< data serialization options */

	bhnd_nvstore_alias_list	 aliases[4];	/**< path alias hash table */
	size_t			 num_aliases;	/**< alias count */

	bhnd_nvstore_path	*root_path;	/**< root path instance */
	bhnd_nvstore_path_list	 paths[4];	/**< path hash table */
	size_t			 num_paths;	/**< path count */
};

#ifdef _KERNEL

#define	BHND_NVSTORE_LOCK_INIT(sc) \
	mtx_init(&(sc)->mtx, "BHND NVRAM store lock", NULL, MTX_DEF)
#define	BHND_NVSTORE_LOCK(sc)			mtx_lock(&(sc)->mtx)
#define	BHND_NVSTORE_UNLOCK(sc)			mtx_unlock(&(sc)->mtx)
#define	BHND_NVSTORE_LOCK_ASSERT(sc, what)	mtx_assert(&(sc)->mtx, what)
#define	BHND_NVSTORE_LOCK_DESTROY(sc)		mtx_destroy(&(sc)->mtx)

#else /* !_KERNEL */

#define	BHND_NVSTORE_LOCK_INIT(sc)	do {				\
	int error = pthread_mutex_init(&(sc)->mtx, NULL);		\
	if (error)							\
		BHND_NV_PANIC("pthread_mutex_init() failed: %d",	\
		    error);						\
} while(0)

#define	BHND_NVSTORE_LOCK(sc)		pthread_mutex_lock(&(sc)->mtx)
#define	BHND_NVSTORE_UNLOCK(sc)		pthread_mutex_unlock(&(sc)->mtx)
#define	BHND_NVSTORE_LOCK_DESTROY(sc)	pthread_mutex_destroy(&(sc)->mtx)
#define	BHND_NVSTORE_LOCK_ASSERT(sc, what)

#endif /* _KERNEL */

#endif /* _BHND_NVRAM_BHND_NVRAM_STOREVAR_H_ */
