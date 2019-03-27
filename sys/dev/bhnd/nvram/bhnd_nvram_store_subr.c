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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/hash.h>
#include <sys/queue.h>

#ifdef _KERNEL

#include <sys/ctype.h>
#include <sys/systm.h>

#include <machine/_inttypes.h>

#else /* !_KERNEL */

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#endif /* _KERNEL */

#include "bhnd_nvram_private.h"
#include "bhnd_nvram_datavar.h"

#include "bhnd_nvram_storevar.h"

static int			 bhnd_nvstore_idx_cmp(void *ctx,
				     const void *lhs, const void *rhs);

/**
 * Allocate and initialize a new path instance.
 * 
 * The caller is responsible for deallocating the instance via
 * bhnd_nvstore_path_free().
 * 
 * @param	path_str	The path's canonical string representation.
 * @param	path_len	The length of @p path_str.
 * 
 * @retval non-NULL	success
 * @retval NULL		if allocation fails.
 */
bhnd_nvstore_path *
bhnd_nvstore_path_new(const char *path_str, size_t path_len)
{
	bhnd_nvstore_path *path;

	/* Allocate new entry */
	path = bhnd_nv_malloc(sizeof(*path));
	if (path == NULL)
		return (NULL);

	path->index = NULL;
	path->num_vars = 0;

	path->pending = bhnd_nvram_plist_new();
	if (path->pending == NULL)
		goto failed;

	path->path_str = bhnd_nv_strndup(path_str, path_len);
	if (path->path_str == NULL)
		goto failed;

	return (path);

failed:
	if (path->pending != NULL)
		bhnd_nvram_plist_release(path->pending);

	if (path->path_str != NULL)
		bhnd_nv_free(path->path_str);

	bhnd_nv_free(path);

	return (NULL);
}

/**
 * Free an NVRAM path instance, releasing all associated resources.
 */
void
bhnd_nvstore_path_free(struct bhnd_nvstore_path *path)
{
	/* Free the per-path index */
	if (path->index != NULL)
		bhnd_nvstore_index_free(path->index);

	bhnd_nvram_plist_release(path->pending);
	bhnd_nv_free(path->path_str);
	bhnd_nv_free(path);
}

/**
 * Allocate and initialize a new index instance with @p capacity.
 * 
 * The caller is responsible for deallocating the instance via
 * bhnd_nvstore_index_free().
 * 
 * @param	capacity	The maximum number of variables to be indexed.
 * 
 * @retval non-NULL	success
 * @retval NULL		if allocation fails.
 */
bhnd_nvstore_index *
bhnd_nvstore_index_new(size_t capacity)
{
	bhnd_nvstore_index	*index;
	size_t			 bytes;

	/* Allocate and populate variable index */
	bytes = sizeof(struct bhnd_nvstore_index) + (sizeof(void *) * capacity);
	index = bhnd_nv_malloc(bytes);
	if (index == NULL) {
		BHND_NV_LOG("error allocating %zu byte index\n", bytes);
		return (NULL);
	}

	index->count = 0;
	index->capacity = capacity;

	return (index);
}

/**
 * Free an index instance, releasing all associated resources.
 * 
 * @param	index	An index instance previously allocated via
 *			bhnd_nvstore_index_new().
 */
void
bhnd_nvstore_index_free(bhnd_nvstore_index *index)
{
	bhnd_nv_free(index);
}

/**
 * Append a new NVRAM variable's @p cookiep value to @p index.
 * 
 * After one or more append requests, the index must be prepared via
 * bhnd_nvstore_index_prepare() before any indexed lookups are performed.
 *
 * @param	sc	The NVRAM store from which NVRAM values will be queried.
 * @param	index	The index to be modified.
 * @param	cookiep	The cookiep value (as provided by the backing NVRAM
 *			data instance of @p sc) to be included in @p index.
 * 
 * @retval 0		success
 * @retval ENOMEM	if appending an additional entry would exceed the
 *			capacity of @p index.
 */
int
bhnd_nvstore_index_append(struct bhnd_nvram_store *sc,
    bhnd_nvstore_index *index, void *cookiep)
{
	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);

	if (index->count >= index->capacity)
		return (ENOMEM);

	index->cookiep[index->count] = cookiep;
	index->count++;
	return (0);
}

/* sort function for bhnd_nvstore_index_prepare() */
static int
bhnd_nvstore_idx_cmp(void *ctx, const void *lhs, const void *rhs)
{
	struct bhnd_nvram_store	*sc;
	void			*l_cookiep, *r_cookiep;
	const char		*l_str, *r_str;
	const char		*l_name, *r_name;
	int			 order;

	sc = ctx;
	l_cookiep = *(void * const *)lhs;
	r_cookiep = *(void * const *)rhs;

	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);

	/* Fetch string pointers from the cookiep values */
	l_str = bhnd_nvram_data_getvar_name(sc->data, l_cookiep);
	r_str = bhnd_nvram_data_getvar_name(sc->data, r_cookiep);

	/* Trim device path prefixes */
	if (sc->data_caps & BHND_NVRAM_DATA_CAP_DEVPATHS) {
		l_name = bhnd_nvram_trim_path_name(l_str);
		r_name = bhnd_nvram_trim_path_name(r_str);
	} else {
		l_name = l_str;
		r_name = r_str;
	}

	/* Perform comparison */
	order = strcmp(l_name, r_name);
	if (order != 0 || lhs == rhs)
		return (order);

	/* If the backing data incorrectly contains variables with duplicate
	 * names, we need a sort order that provides stable behavior.
	 * 
	 * Since Broadcom's own code varies wildly on this question, we just
	 * use a simple precedence rule: The first declaration of a variable
	 * takes precedence. */
	return (bhnd_nvram_data_getvar_order(sc->data, l_cookiep, r_cookiep));
}

/**
 * Prepare @p index for querying via bhnd_nvstore_index_lookup().
 * 
 * After one or more append requests, the index must be prepared via
 * bhnd_nvstore_index_prepare() before any indexed lookups are performed.
 *
 * @param	sc	The NVRAM store from which NVRAM values will be queried.
 * @param	index	The index to be prepared.
 * 
 * @retval 0		success
 * @retval non-zero	if preparing @p index otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvstore_index_prepare(struct bhnd_nvram_store *sc,
    bhnd_nvstore_index *index)
{
	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);

	/* Sort the index table */
	qsort_r(index->cookiep, index->count, sizeof(index->cookiep[0]), sc,
	    bhnd_nvstore_idx_cmp);

	return (0);
}

/**
 * Return a borrowed reference to the root path node.
 * 
 * @param	sc	The NVRAM store.
 */
bhnd_nvstore_path *
bhnd_nvstore_get_root_path(struct bhnd_nvram_store *sc)
{
	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);
	return (sc->root_path);
}

/**
 * Return true if @p path is the root path node.
 * 
 * @param	sc	The NVRAM store.
 * @param	path	The path to query.
 */
bool
bhnd_nvstore_is_root_path(struct bhnd_nvram_store *sc, bhnd_nvstore_path *path)
{
	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);
	return (sc->root_path == path);
}

/**
 * Return the update entry matching @p name in @p path, or NULL if no entry
 * found.
 * 
 * @param sc	The NVRAM store.
 * @param path	The path to query.
 * @param name	The NVRAM variable name to search for in @p path's update list.
 * 
 * @retval non-NULL	success
 * @retval NULL		if @p name is not found in @p path.
 */
bhnd_nvram_prop *
bhnd_nvstore_path_get_update(struct bhnd_nvram_store *sc,
    bhnd_nvstore_path *path, const char *name)
{
	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);
	return (bhnd_nvram_plist_get_prop(path->pending, name));
}

/**
 * Register or remove an update record for @p name in @p path.
 * 
 * @param sc	The NVRAM store.
 * @param path	The path to be modified.
 * @param name	The path-relative variable name to be modified.
 * @param value	The new value. A value of BHND_NVRAM_TYPE_NULL denotes deletion.
 * 
 * @retval 0		success
 * @retval ENOMEM	if allocation fails.
 * @retval ENOENT	if @p name is unknown.
 * @retval EINVAL	if @p value is NULL, and deletion of @p is not
 *			supported.
 * @retval EINVAL	if @p value cannot be converted to a supported value
 *			type.
 */
int
bhnd_nvstore_path_register_update(struct bhnd_nvram_store *sc,
    bhnd_nvstore_path *path, const char *name, bhnd_nvram_val *value)
{
	bhnd_nvram_val		*prop_val;
	const char		*full_name;
	void			*cookiep;
	char			*namebuf;
	int			 error;
	bool			 nvram_committed;

	namebuf = NULL;
	prop_val = NULL;

	/* Determine whether the variable is currently defined in the
	 * backing NVRAM data, and derive its full path-prefixed name */
	nvram_committed = false;
	cookiep = bhnd_nvstore_path_data_lookup(sc, path, name);
	if (cookiep != NULL) {
		/* Variable is defined in the backing data */
		nvram_committed = true;

		/* Use the existing variable name */
		full_name = bhnd_nvram_data_getvar_name(sc->data, cookiep);
	} else if (path == sc->root_path) {
		/* No prefix required for root path */
		full_name = name;
	} else {
		bhnd_nvstore_alias	*alias;
		int			 len;

		/* New variable is being set; we need to determine the
		 * appropriate path prefix */
		alias = bhnd_nvstore_find_alias(sc, path->path_str);
		if (alias != NULL) {
			/* Use <alias>:name */
			len = bhnd_nv_asprintf(&namebuf, "%lu:%s", alias->alias,
			    name);
		} else {
			/* Use path/name */
			len = bhnd_nv_asprintf(&namebuf, "%s/%s",
			    path->path_str, name);
		}

		if (len < 0)
			return (ENOMEM);

		full_name = namebuf;
	}

	/* Allow the data store to filter the NVRAM operation */
	if (bhnd_nvram_val_type(value) == BHND_NVRAM_TYPE_NULL) {
		error = bhnd_nvram_data_filter_unsetvar(sc->data, full_name);
		if (error) {
			BHND_NV_LOG("cannot unset property %s: %d\n", full_name,
			    error);
			goto cleanup;
		}

		if ((prop_val = bhnd_nvram_val_copy(value)) == NULL) {
			error = ENOMEM;
			goto cleanup;
		}
	} else {
		error = bhnd_nvram_data_filter_setvar(sc->data, full_name,
		    value,  &prop_val);
		if (error) {
			BHND_NV_LOG("cannot set property %s: %d\n", full_name,
			    error);
			goto cleanup;
		}
	}

	/* Add relative variable name to the per-path update list */
	if (bhnd_nvram_val_type(value) == BHND_NVRAM_TYPE_NULL &&
	    !nvram_committed)
	{
		/* This is a deletion request for a variable not defined in
		 * out backing store; we can simply remove the corresponding
		 * update entry. */
		bhnd_nvram_plist_remove(path->pending, name);
	} else {
		/* Update or append a pending update entry */
		error = bhnd_nvram_plist_replace_val(path->pending, name,
		    prop_val);
		if (error)
			goto cleanup;
	}

	/* Success */
	error = 0;

cleanup:
	if (namebuf != NULL)
		bhnd_nv_free(namebuf);

	if (prop_val != NULL)
		bhnd_nvram_val_release(prop_val);

	return (error);
}

/**
 * Iterate over all variable cookiep values retrievable from the backing
 * data store in @p path.
 * 
 * @warning Pending updates in @p path are ignored by this function.
 *
 * @param		sc	The NVRAM store.
 * @param		path	The NVRAM path to be iterated.
 * @param[in,out]	indexp	A pointer to an opaque indexp value previously
 *				returned by bhnd_nvstore_path_data_next(), or a
 *				NULL value to begin iteration.
 *
 * @return Returns the next variable name, or NULL if there are no more
 * variables defined in @p path.
 */
void *
bhnd_nvstore_path_data_next(struct bhnd_nvram_store *sc,
     bhnd_nvstore_path *path, void **indexp)
{
	void **index_ref;

	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);

	/* No index */
	if (path->index == NULL) {
		/* An index is required for all non-empty, non-root path
		 * instances */
		BHND_NV_ASSERT(bhnd_nvstore_is_root_path(sc, path),
		    ("missing index for non-root path %s", path->path_str));

		/* Iterate NVRAM data directly, using the NVRAM data's cookiep
		 * value as our indexp context */
		if ((bhnd_nvram_data_next(sc->data, indexp)) == NULL)
			return (NULL);

		return (*indexp);
	}

	/* Empty index */
	if (path->index->count == 0)
		return (NULL);

	if (*indexp == NULL) {
		/* First index entry */
		index_ref = &path->index->cookiep[0];
	} else {
		size_t idxpos;

		/* Advance to next index entry */
		index_ref = *indexp;
		index_ref++;

		/* Hit end of index? */
		BHND_NV_ASSERT(index_ref > path->index->cookiep,
		    ("invalid indexp"));

		idxpos = (index_ref - path->index->cookiep);
		if (idxpos >= path->index->count)
			return (NULL);
	}

	/* Provide new index position */
	*indexp = index_ref;

	/* Return the data's cookiep value */
	return (*index_ref);
}

/**
 * Perform an lookup of @p name in the backing NVRAM data for @p path,
 * returning the associated cookiep value, or NULL if the variable is not found
 * in the backing NVRAM data.
 * 
 * @warning Pending updates in @p path are ignored by this function.
 * 
 * @param	sc	The NVRAM store from which NVRAM values will be queried.
 * @param	path	The path to be queried.
 * @param	name	The variable name to be queried.
 * 
 * @retval non-NULL	success
 * @retval NULL		if @p name is not found in @p index.
 */
void *
bhnd_nvstore_path_data_lookup(struct bhnd_nvram_store *sc,
    bhnd_nvstore_path *path, const char *name)
{
	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);

	/* No index */
	if (path->index == NULL) {
		/* An index is required for all non-empty, non-root path
		 * instances */
		BHND_NV_ASSERT(bhnd_nvstore_is_root_path(sc, path),
		    ("missing index for non-root path %s", path->path_str));

		/* Look up directly in NVRAM data */
		return (bhnd_nvram_data_find(sc->data, name));
	}

	/* Otherwise, delegate to an index-based lookup */
	return (bhnd_nvstore_index_lookup(sc, path->index, name));
}

/**
 * Perform an index lookup of @p name, returning the associated cookiep
 * value, or NULL if the variable does not exist.
 * 
 * @param	sc	The NVRAM store from which NVRAM values will be queried.
 * @param	index	The index to be queried.
 * @param	name	The variable name to be queried.
 * 
 * @retval non-NULL	success
 * @retval NULL		if @p name is not found in @p index.
 */
void *
bhnd_nvstore_index_lookup(struct bhnd_nvram_store *sc,
    bhnd_nvstore_index *index, const char *name)
{
	void		*cookiep;
	const char	*indexed_name;
	size_t		 min, mid, max;
	uint32_t	 data_caps;
	int		 order;

	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);
	BHND_NV_ASSERT(index != NULL, ("NULL index"));

	/*
	 * Locate the requested variable using a binary search.
	 */
	if (index->count == 0)
		return (NULL);

	data_caps = sc->data_caps;
	min = 0;
	max = index->count - 1;

	while (max >= min) {
		/* Select midpoint */
		mid = (min + max) / 2;
		cookiep = index->cookiep[mid];

		/* Fetch variable name */
		indexed_name = bhnd_nvram_data_getvar_name(sc->data, cookiep);

		/* Trim any path prefix */
		if (data_caps & BHND_NVRAM_DATA_CAP_DEVPATHS)
			indexed_name = bhnd_nvram_trim_path_name(indexed_name);

		/* Determine which side of the partition to search */
		order = strcmp(indexed_name, name);
		if (order < 0) {
			/* Search upper partition */
			min = mid + 1;
		} else if (order > 0) {
			/* Search (non-empty) lower partition */
			if (mid == 0)
				break;
			max = mid - 1;
		} else if (order == 0) {
			size_t	idx;

			/*
			 * Match found.
			 * 
			 * If this happens to be a key with multiple definitions
			 * in the backing store, we need to find the entry with
			 * the highest declaration precedence.
			 * 
			 * Duplicates are sorted in order of descending
			 * precedence; to find the highest precedence entry,
			 * we search backwards through the index.
			 */
			idx = mid;
			while (idx > 0) {
				void		*dup_cookiep;
				const char	*dup_name;

				/* Fetch preceding index entry */
				idx--;
				dup_cookiep = index->cookiep[idx];
				dup_name = bhnd_nvram_data_getvar_name(sc->data,
				    dup_cookiep);

				/* Trim any path prefix */
				if (data_caps & BHND_NVRAM_DATA_CAP_DEVPATHS) {
					dup_name = bhnd_nvram_trim_path_name(
					    dup_name);
				}

				/* If no match, current cookiep is the variable
				 * definition with the highest precedence */
				if (strcmp(indexed_name, dup_name) != 0)
					return (cookiep);

				/* Otherwise, prefer this earlier definition,
				 * and keep searching for a higher-precedence
				 * definitions */
				cookiep = dup_cookiep;
			}

			return (cookiep);
		}
	}

	/* Not found */
	return (NULL);
}

/**
 * Return the device path entry registered for @p path, if any.
 * 
 * @param	sc		The NVRAM store to be queried.
 * @param	path		The device path to search for.
 * @param	path_len	The length of @p path.
 *
 * @retval non-NULL	if found.
 * @retval NULL		if not found.
 */
bhnd_nvstore_path *
bhnd_nvstore_get_path(struct bhnd_nvram_store *sc, const char *path,
    size_t path_len)
{
	bhnd_nvstore_path_list	*plist;
	bhnd_nvstore_path	*p;
	uint32_t		 h;

	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);

	/* Use hash lookup */
	h = hash32_strn(path, path_len, HASHINIT);
	plist = &sc->paths[h % nitems(sc->paths)];

	LIST_FOREACH(p, plist, np_link) {
		/* Check for prefix match */
		if (strncmp(p->path_str, path, path_len) != 0)
			continue;

		/* Check for complete match */
		if (strnlen(path, path_len) != strlen(p->path_str))
			continue;

		return (p);
	}

	/* Not found */
	return (NULL);
}

/**
 * Resolve @p aval to its corresponding device path entry, if any.
 * 
 * @param	sc		The NVRAM store to be queried.
 * @param	aval		The device path alias value to search for.
 *
 * @retval non-NULL	if found.
 * @retval NULL		if not found.
 */
bhnd_nvstore_path *
bhnd_nvstore_resolve_path_alias(struct bhnd_nvram_store *sc, u_long aval)
{
	bhnd_nvstore_alias *alias;

	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);

	/* Fetch alias entry */
	if ((alias = bhnd_nvstore_get_alias(sc, aval)) == NULL)
		return (NULL);

	return (alias->path);
}

/**
 * Register a device path entry for the path referenced by variable name
 * @p info, if any.
 *
 * @param	sc		The NVRAM store to be updated.
 * @param	info		The NVRAM variable name info.
 * @param	cookiep		The NVRAM variable's cookiep value.
 *
 * @retval 0		if the path was successfully registered, or an identical
 *			path or alias entry exists.
 * @retval EEXIST	if a conflicting entry already exists for the path or
 *			alias referenced by @p info.
 * @retval ENOENT	if @p info contains a dangling alias reference.
 * @retval EINVAL	if @p info contains an unsupported bhnd_nvstore_var_type
 *			and bhnd_nvstore_path_type combination.
 * @retval ENOMEM	if allocation fails.
 */
int
bhnd_nvstore_var_register_path(struct bhnd_nvram_store *sc,
    bhnd_nvstore_name_info *info, void *cookiep)
{
	switch (info->type) {
	case BHND_NVSTORE_VAR:
		/* Variable */
		switch (info->path_type) {
		case BHND_NVSTORE_PATH_STRING:
			/* Variable contains a full path string
			 * (pci/1/1/varname); register the path */
			return (bhnd_nvstore_register_path(sc,
			    info->path.str.value, info->path.str.value_len));

		case BHND_NVSTORE_PATH_ALIAS:
			/* Variable contains an alias reference (0:varname).
			 * There's no path to register */
			return (0);
		}

		BHND_NV_PANIC("unsupported path type %d", info->path_type);
		break;

	case BHND_NVSTORE_ALIAS_DECL:
		/* Alias declaration */
		return (bhnd_nvstore_register_alias(sc, info, cookiep));
	}

	BHND_NV_PANIC("unsupported var type %d", info->type);
}

/**
 * Resolve the device path entry referenced by @p info.
 *
 * @param	sc		The NVRAM store to be updated.
 * @param	info		Variable name information descriptor containing
 *				the path or path alias to be resolved.
 *
 * @retval non-NULL	if found.
 * @retval NULL		if not found.
 */
bhnd_nvstore_path *
bhnd_nvstore_var_get_path(struct bhnd_nvram_store *sc,
    bhnd_nvstore_name_info *info)
{
	switch (info->path_type) {
	case BHND_NVSTORE_PATH_STRING:
		return (bhnd_nvstore_get_path(sc, info->path.str.value,
		    info->path.str.value_len));
	case BHND_NVSTORE_PATH_ALIAS:
		return (bhnd_nvstore_resolve_path_alias(sc,
		    info->path.alias.value));
	}

	BHND_NV_PANIC("unsupported path type %d", info->path_type);
}

/**
 * Return the device path alias entry registered for @p alias_val, if any.
 * 
 * @param	sc		The NVRAM store to be queried.
 * @param	alias_val	The alias value to search for.
 *
 * @retval non-NULL	if found.
 * @retval NULL		if not found.
 */
bhnd_nvstore_alias *
bhnd_nvstore_get_alias(struct bhnd_nvram_store *sc, u_long alias_val)
{
	bhnd_nvstore_alias_list	*alist;
	bhnd_nvstore_alias	*alias;

	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);

	/* Can use hash lookup */
	alist = &sc->aliases[alias_val % nitems(sc->aliases)];
	LIST_FOREACH(alias, alist, na_link) {
		if (alias->alias == alias_val)
			return (alias);			
	}

	/* Not found */
	return (NULL);
}

/**
 * Return the device path alias entry registered for @p path, if any.
 * 
 * @param	sc	The NVRAM store to be queried.
 * @param	path	The alias path to search for.
 *
 * @retval non-NULL	if found.
 * @retval NULL		if not found.
 */
bhnd_nvstore_alias *
bhnd_nvstore_find_alias(struct bhnd_nvram_store *sc, const char *path)
{
	bhnd_nvstore_alias *alias;

	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);

	/* Have to scan the full table */
	for (size_t i = 0; i < nitems(sc->aliases); i++) {
		LIST_FOREACH(alias, &sc->aliases[i], na_link) {
			if (strcmp(alias->path->path_str, path) == 0)
				return (alias);			
		}
	}

	/* Not found */
	return (NULL);
}

/**
 * Register a device path entry for @p path.
 * 
 * @param	sc		The NVRAM store to be updated.
 * @param	path_str	The absolute device path string.
 * @param	path_len	The length of @p path_str.
 * 
 * @retval 0		if the path was successfully registered, or an identical
 *			path/alias entry already exists.
 * @retval ENOMEM	if allocation fails.
 */
int
bhnd_nvstore_register_path(struct bhnd_nvram_store *sc, const char *path_str,
    size_t path_len)
{
	bhnd_nvstore_path_list	*plist;
	bhnd_nvstore_path	*path;
	uint32_t		 h;

	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);

	/* Already exists? */
	if (bhnd_nvstore_get_path(sc, path_str, path_len) != NULL)
		return (0);

	/* Can't represent more than SIZE_MAX paths */
	if (sc->num_paths == SIZE_MAX)
		return (ENOMEM);

	/* Allocate new entry */
	path = bhnd_nvstore_path_new(path_str, path_len);
	if (path == NULL)
		return (ENOMEM);

	/* Insert in path hash table */
	h = hash32_str(path->path_str, HASHINIT);
	plist = &sc->paths[h % nitems(sc->paths)];
	LIST_INSERT_HEAD(plist, path, np_link);

	/* Increment path count */
	sc->num_paths++;

	return (0);
}

/**
 * Register a device path alias for an NVRAM 'devpathX' variable.
 * 
 * The path value for the alias will be fetched from the backing NVRAM data.
 * 
 * @param	sc	The NVRAM store to be updated.
 * @param	info	The NVRAM variable name info.
 * @param	cookiep	The NVRAM variable's cookiep value.
 * 
 * @retval 0		if the alias was successfully registered, or an
 *			identical alias entry exists.
 * @retval EEXIST	if a conflicting alias or path entry already exists.
 * @retval EINVAL	if @p info is not a BHND_NVSTORE_ALIAS_DECL or does
 *			not contain a BHND_NVSTORE_PATH_ALIAS entry.
 * @retval ENOMEM	if allocation fails.
 */
int
bhnd_nvstore_register_alias(struct bhnd_nvram_store *sc,
    const bhnd_nvstore_name_info *info, void *cookiep)
{
	bhnd_nvstore_alias_list	*alist;
	bhnd_nvstore_alias	*alias;
	bhnd_nvstore_path	*path;
	char			*path_str;
	size_t			 path_len;
	int			 error;

	BHND_NVSTORE_LOCK_ASSERT(sc, MA_OWNED);

	path_str = NULL;
	alias = NULL;

	/* Can't represent more than SIZE_MAX aliases */
	if (sc->num_aliases == SIZE_MAX)
		return (ENOMEM);

	/* Must be an alias declaration */
	if (info->type != BHND_NVSTORE_ALIAS_DECL)
		return (EINVAL);

	if (info->path_type != BHND_NVSTORE_PATH_ALIAS)
		return (EINVAL);

	/* Fetch the devpath variable's value length */
	error = bhnd_nvram_data_getvar(sc->data, cookiep, NULL, &path_len,
	    BHND_NVRAM_TYPE_STRING);
	if (error)
		return (ENOMEM);

	/* Allocate path string buffer */
	if ((path_str = bhnd_nv_malloc(path_len)) == NULL)
		return (ENOMEM);

	/* Decode to our new buffer */
	error = bhnd_nvram_data_getvar(sc->data, cookiep, path_str, &path_len,
	    BHND_NVRAM_TYPE_STRING);
	if (error)
		goto failed;

	/* Trim trailing '/' character(s) from the path length */
	path_len = strnlen(path_str, path_len);
	while (path_len > 0 && path_str[path_len-1] == '/') {
		path_str[path_len-1] = '\0';
		path_len--;
	}

	/* Is a conflicting alias entry already registered for this alias
	 * value? */
	alias = bhnd_nvstore_get_alias(sc, info->path.alias.value);
	if (alias != NULL) {
		if (alias->cookiep != cookiep ||
		    strcmp(alias->path->path_str, path_str) != 0)
		{
			error = EEXIST;
			goto failed;
		}
	}

	/* Is a conflicting entry already registered for the alias path? */
	if ((alias = bhnd_nvstore_find_alias(sc, path_str)) != NULL) {
		if (alias->alias != info->path.alias.value ||
		    alias->cookiep != cookiep ||
		    strcmp(alias->path->path_str, path_str) != 0)
		{
			error = EEXIST;
			goto failed;
		}
	}

	/* Get (or register) the target path entry */
	path = bhnd_nvstore_get_path(sc, path_str, path_len);
	if (path == NULL) {
		error = bhnd_nvstore_register_path(sc, path_str, path_len);
		if (error)
			goto failed;

		path = bhnd_nvstore_get_path(sc, path_str, path_len);
		BHND_NV_ASSERT(path != NULL, ("missing registered path"));
	}

	/* Allocate alias entry */
	alias = bhnd_nv_calloc(1, sizeof(*alias));
	if (alias == NULL) {
		error = ENOMEM;
		goto failed;
	}

	alias->path = path;
	alias->cookiep = cookiep;
	alias->alias = info->path.alias.value;

	/* Insert in alias hash table */
	alist = &sc->aliases[alias->alias % nitems(sc->aliases)];
	LIST_INSERT_HEAD(alist, alias, na_link);

	/* Increment alias count */
	sc->num_aliases++;

	bhnd_nv_free(path_str);
	return (0);

failed:
	if (path_str != NULL)
		bhnd_nv_free(path_str);

	if (alias != NULL)
		bhnd_nv_free(alias);

	return (error);
}

/**
 * If @p child is equal to or a child path of @p parent, return a pointer to
 * @p child's path component(s) relative to @p parent; otherwise, return NULL.
 */
const char *
bhnd_nvstore_parse_relpath(const char *parent, const char *child)
{
	size_t prefix_len;

	/* All paths have an implicit leading '/'; this allows us to treat
	 * our manufactured root path of "/" as a prefix to all NVRAM-defined
	 * paths (which do not necessarily include a leading '/' */
	if (*parent == '/')
		parent++;

	if (*child == '/')
		child++;

	/* Is parent a prefix of child? */
	prefix_len = strlen(parent);
	if (strncmp(parent, child, prefix_len) != 0)
		return (NULL);

	/* A zero-length prefix matches everything */
	if (prefix_len == 0)
		return (child);

	/* Is child equal to parent? */
	if (child[prefix_len] == '\0')
		return (child + prefix_len);

	/* Is child actually a child of parent? */
	if (child[prefix_len] == '/')
		return (child + prefix_len + 1);

	/* No match (e.g. parent=/foo..., child=/fooo...) */
	return (NULL);
}

/**
 * Parse a raw NVRAM variable name and return its @p entry_type, its
 * type-specific @p prefix (e.g. '0:', 'pci/1/1', 'devpath'), and its
 * type-specific @p suffix (e.g. 'varname', '0').
 * 
 * @param	name		The NVRAM variable name to be parsed. This
 *				value must remain valid for the lifetime of
 *				@p info.
 * @param	type		The NVRAM name type -- either INTERNAL for names
 *				parsed from backing NVRAM data, or EXTERNAL for
 *				names provided by external NVRAM store clients.
 * @param	data_caps	The backing NVRAM data capabilities
 *				(see bhnd_nvram_data_caps()).
 * @param[out]	info		On success, the parsed variable name info.
 * 
 * @retval 0		success
 * @retval non-zero	if parsing @p name otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvstore_parse_name_info(const char *name, bhnd_nvstore_name_type type,
    uint32_t data_caps, bhnd_nvstore_name_info *info)
{
	const char	*p;
	char		*endp;

	/* Skip path parsing? */
	if (data_caps & BHND_NVRAM_DATA_CAP_DEVPATHS) {
		/* devpath declaration? (devpath0=pci/1/1) */
		if (strncmp(name, "devpath", strlen("devpath")) == 0) {
			u_long alias;

			/* Perform standard validation on the relative
			 * variable name */
			if (type != BHND_NVSTORE_NAME_INTERNAL &&
			    !bhnd_nvram_validate_name(name))
			{
				return (ENOENT);
			}

			/* Parse alias value that should follow a 'devpath'
			 * prefix */
			p = name + strlen("devpath");
			alias = strtoul(p, &endp, 10);
			if (endp != p && *endp == '\0') {
				info->type = BHND_NVSTORE_ALIAS_DECL;
				info->path_type = BHND_NVSTORE_PATH_ALIAS;
				info->name = name;
				info->path.alias.value = alias;

				return (0);
			}
		}

		/* device aliased variable? (0:varname) */
		if (bhnd_nv_isdigit(*name)) {
			u_long alias;

			/* Parse '0:' alias prefix */
			alias = strtoul(name, &endp, 10);
			if (endp != name && *endp == ':') {
				/* Perform standard validation on the relative
				 * variable name */
				if (type != BHND_NVSTORE_NAME_INTERNAL &&
				    !bhnd_nvram_validate_name(name))
				{
					return (ENOENT);
				}

				info->type = BHND_NVSTORE_VAR;
				info->path_type = BHND_NVSTORE_PATH_ALIAS;

				/* name follows 0: prefix */
				info->name = endp + 1;
				info->path.alias.value = alias;

				return (0);
			}
		}

		/* device variable? (pci/1/1/varname) */
		if ((p = strrchr(name, '/')) != NULL) {
			const char	*path, *relative_name;
			size_t		 path_len;

			/* Determine the path length; 'p' points at the last
			 * path separator in 'name' */
			path_len = p - name;
			path = name;

			/* The relative variable name directly follows the
			 * final path separator '/' */
			relative_name = path + path_len + 1;

			/* Now that we calculated the name offset, exclude all
			 * trailing '/' characters from the path length */
			while (path_len > 0 && path[path_len-1] == '/')
				path_len--;

			/* Perform standard validation on the relative
			 * variable name */
			if (type != BHND_NVSTORE_NAME_INTERNAL &&
			    !bhnd_nvram_validate_name(relative_name))
			{
				return (ENOENT);
			}

			/* Initialize result with pointers into the name
			 * buffer */
			info->type = BHND_NVSTORE_VAR;
			info->path_type = BHND_NVSTORE_PATH_STRING;
			info->name = relative_name;
			info->path.str.value = path;
			info->path.str.value_len = path_len;

			return (0);
		}
	}

	/* If all other parsing fails, the result is a simple variable with
	 * an implicit path of "/" */
	if (type != BHND_NVSTORE_NAME_INTERNAL &&
	    !bhnd_nvram_validate_name(name))
	{
		/* Invalid relative name */
		return (ENOENT);
	}

	info->type = BHND_NVSTORE_VAR;
	info->path_type = BHND_NVSTORE_PATH_STRING;
	info->name = name;
	info->path.str.value = BHND_NVSTORE_ROOT_PATH;
	info->path.str.value_len = BHND_NVSTORE_ROOT_PATH_LEN;

	return (0);
}
