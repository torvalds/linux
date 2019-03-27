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

#ifdef _KERNEL

#include <sys/systm.h>

#else /* !_KERNEL */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#endif /* _KERNEL */

#include "bhnd_nvram_plistvar.h"
#include "bhnd_nvram_private.h"

static bhnd_nvram_plist_entry	*bhnd_nvram_plist_get_entry(
				     bhnd_nvram_plist *plist, const char *name);

/**
 * Allocate and initialize a new, empty property list.
 * 
 * The caller is responsible for releasing the returned property value
 * via bhnd_nvram_plist_release().
 * 
 * @retval non-NULL	success
 * @retval NULL		if allocation fails.
 */
bhnd_nvram_plist *
bhnd_nvram_plist_new(void)
{
	bhnd_nvram_plist *plist;

	plist = bhnd_nv_calloc(1, sizeof(*plist));
	if (plist == NULL)
		return NULL;

	/* Implicit caller-owned reference */
	plist->refs = 1;

	/* Initialize entry list */
	plist->num_entries = 0;
	TAILQ_INIT(&plist->entries);

	/* Initialize entry hash table */
	for (size_t i = 0; i < nitems(plist->names); i++)
		LIST_INIT(&plist->names[i]);

	return (plist);
}

/**
 * Retain a reference and return @p plist to the caller.
 * 
 * The caller is responsible for releasing their reference ownership via
 * bhnd_nvram_plist_release().
 * 
 * @param	plist	The property list to be retained.
 */
bhnd_nvram_plist *
bhnd_nvram_plist_retain(bhnd_nvram_plist *plist)
{
	BHND_NV_ASSERT(plist->refs >= 1, ("plist over-released"));

	refcount_acquire(&plist->refs);
	return (plist);
}

/**
 * Release a reference to @p plist.
 *
 * If this is the last reference, all associated resources will be freed.
 * 
 * @param	plist	The property list to be released.
 */
void
bhnd_nvram_plist_release(bhnd_nvram_plist *plist)
{
	bhnd_nvram_plist_entry *ple, *ple_next;

	BHND_NV_ASSERT(plist->refs >= 1, ("plist over-released"));

	/* Drop reference */
	if (!refcount_release(&plist->refs))
		return;

	/* Free all property entries */
	TAILQ_FOREACH_SAFE(ple, &plist->entries, pl_link, ple_next) {
		bhnd_nvram_prop_release(ple->prop);
		bhnd_nv_free(ple);
	}

	/* Free plist instance */
	bhnd_nv_free(plist);
}

/**
 * Return a shallow copy of @p plist.
 * 
 * The caller is responsible for releasing the returned property value
 * via bhnd_nvram_plist_release().
 * 
 * @retval non-NULL	success
 * @retval NULL		if allocation fails.
 */
bhnd_nvram_plist *
bhnd_nvram_plist_copy(bhnd_nvram_plist *plist)
{
	bhnd_nvram_plist	*copy;
	bhnd_nvram_prop		*prop;
	int			 error;

	/* Allocate new, empty plist */
	if ((copy = bhnd_nvram_plist_new()) == NULL)
		return (NULL);

	/* Append all properties */
	prop = NULL;
	while ((prop = bhnd_nvram_plist_next(plist, prop)) != NULL) {
		error = bhnd_nvram_plist_append(copy, prop);
		if (error) {
			if (error != ENOMEM) {
				BHND_NV_LOG("error copying property: %d\n",
				    error);
			}

			bhnd_nvram_plist_release(copy);
			return (NULL);
		}
	}

	/* Return ownership of the copy to our caller */
	return (copy);
}

/**
 * Return the number of properties in @p plist.
 */
size_t
bhnd_nvram_plist_count(bhnd_nvram_plist *plist)
{
	return (plist->num_entries);
}

/**
 * Return true if @p plist contains a property name @p name, false otherwise.
 * 
 * @param	plist	The property list to be queried.
 * @param	name	The property name to be queried.
 */
bool
bhnd_nvram_plist_contains(bhnd_nvram_plist *plist, const char *name)
{
	if (bhnd_nvram_plist_get_entry(plist, name) != NULL)
		return (true);

	return (false);
}

/**
 * Replace the current property value for a property matching the name
 * of @p prop, maintaining the property's current order in @p plist.
 * 
 * If a matching property is not found in @p plist, @p prop will instead be
 * appended.
 * 
 * @param	plist	The property list to be modified.
 * @param	prop	The replacement property.
 * 
 * @retval 0		success
 * @retval ENOMEM	if allocation fails.
 * @retval non-zero	if modifying @p plist otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_plist_replace(bhnd_nvram_plist *plist, bhnd_nvram_prop *prop)
{
	bhnd_nvram_plist_entry	*entry;

	/* Fetch current entry */
	entry = bhnd_nvram_plist_get_entry(plist, prop->name);
	if (entry == NULL) {
		/* Not found -- append property instead */
		return (bhnd_nvram_plist_append(plist, prop));
	}

	/* Replace the current entry's property reference */
	bhnd_nvram_prop_release(entry->prop);
	entry->prop = bhnd_nvram_prop_retain(prop);

	return (0);
}

/**
 * Replace the current property value for a property matching @p name,
 * maintaining the property's order in @p plist.
 * 
 * If @p name is not found in @p plist, a new property will be appended.
 * 
 * @param	plist	The property list to be modified.
 * @param	name	The name of the property to be replaced.
 * @param	val	The replacement value for @p name.
 * 
 * @retval 0		success
 * @retval ENOMEM	if allocation fails.
 * @retval non-zero	if modifying @p plist otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_plist_replace_val(bhnd_nvram_plist *plist, const char *name,
    bhnd_nvram_val *val)
{
	bhnd_nvram_prop		*prop;
	int			 error;

	/* Construct a new property instance for the name and value */
	if ((prop = bhnd_nvram_prop_new(name, val)) == NULL)
		return (ENOMEM);

	/* Attempt replace */
	error = bhnd_nvram_plist_replace(plist, prop);
	bhnd_nvram_prop_release(prop);

	return (error);
}

/**
 * Replace the current property value for a property matching @p name, copying
 * the new property value from the given @p inp buffer of @p itype and @p ilen. 
 * 
 * The current property order of @p name in @p plist will be maintained.
 * 
 * If @p name is not found in @p plist, a new property will be appended.
 * 
 * @param	plist	The property list to be modified.
 * @param	name	The name of the property to be replaced.
 * @param	inp	Input buffer.
 * @param	ilen	Input buffer length.
 * @param	itype	Input buffer type.
 * 
 * @retval 0		success
 * @retval ENOMEM	if allocation fails.
 * @retval non-zero	if modifying @p plist otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_plist_replace_bytes(bhnd_nvram_plist *plist, const char *name,
    const void *inp, size_t ilen, bhnd_nvram_type itype)
{
	bhnd_nvram_prop	*prop;
	int		 error;

	if ((prop = bhnd_nvram_prop_bytes_new(name, inp, ilen, itype)) == NULL)
		return (ENOMEM);

	error = bhnd_nvram_plist_replace(plist, prop);
	bhnd_nvram_prop_release(prop);

	return (error);
}

/**
 * Replace the current property value for a property matching @p name, copying
 * the new property value from @p val.
 * 
 * The current property order of @p name in @p plist will be maintained.
 * 
 * If @p name is not found in @p plist, a new property will be appended.
 * 
 * @param	plist	The property list to be modified.
 * @param	name	The name of the property to be replaced.
 * @param	val	The property's replacement string value.
 * 
 * @retval 0		success
 * @retval ENOMEM	if allocation fails.
 * @retval non-zero	if modifying @p plist otherwise fails, a regular unix
 *			error code will be returned.
 */
int
bhnd_nvram_plist_replace_string(bhnd_nvram_plist *plist, const char *name,
    const char *val)
{
	return (bhnd_nvram_plist_replace_bytes(plist, name, val, strlen(val)+1,
	    BHND_NVRAM_TYPE_STRING));
}

/**
 * Remove the property entry for the property @p name, if any.
 * 
 * @param	plist	The property list to be modified.
 * @param	name	The name of the property to be removed.
 */
void
bhnd_nvram_plist_remove(bhnd_nvram_plist *plist, const char *name)
{
	bhnd_nvram_plist_entry *entry;

	/* Fetch entry */
	entry = bhnd_nvram_plist_get_entry(plist, name);
	if (entry == NULL)
		return;

	/* Remove from entry list and hash table */
	TAILQ_REMOVE(&plist->entries, entry, pl_link);
	LIST_REMOVE(entry, pl_hash_link);

	/* Free plist entry */
	bhnd_nvram_prop_release(entry->prop);
	bhnd_nv_free(entry);

	/* Decrement entry count */
	BHND_NV_ASSERT(plist->num_entries > 0, ("entry count over-release"));
	plist->num_entries--;
}

/**
 * Fetch the property list entry for @p name, if any.
 * 
 * @param	plist	The property list to be queried.
 * @param	name	The property name to be queried.
 * 
 * @retval non-NULL	if @p name is found.
 * @retval NULL		if @p name is not found.
 */
static bhnd_nvram_plist_entry *
bhnd_nvram_plist_get_entry(bhnd_nvram_plist *plist, const char *name)
{
	bhnd_nvram_plist_entry_list	*hash_list;
	bhnd_nvram_plist_entry		*entry;
	uint32_t			 h;

	h = hash32_str(name, HASHINIT);
	hash_list = &plist->names[h % nitems(plist->names)];

	LIST_FOREACH(entry, hash_list, pl_hash_link) {
		if (strcmp(entry->prop->name, name) == 0)
			return (entry);
	};

	/* Not found */
	return (NULL);
}

/**
 * Append all properties from @p tail to @p plist.
  * 
 * @param	plist	The property list to be modified.
 * @param	tail	The property list to append.
 * 
 * @retval 0		success
 * @retval ENOMEM	if allocation fails.
 * @retval EEXIST	an existing property from @p tail was found in @p plist.
 */
int
bhnd_nvram_plist_append_list(bhnd_nvram_plist *plist, bhnd_nvram_plist *tail)
{
	bhnd_nvram_prop	*p;
	int		 error;

	p = NULL;
	while ((p = bhnd_nvram_plist_next(tail, p)) != NULL) {
		if ((error = bhnd_nvram_plist_append(plist, p)))
			return (error);
	}

	return (0);
}

/**
 * Append @p prop to @p plist.
 * 
 * @param	plist	The property list to be modified.
 * @param	prop	The property to append.
 * 
 * @retval 0		success
 * @retval ENOMEM	if allocation fails.
 * @retval EEXIST	an existing property with @p name was found in @p plist.
 */
int
bhnd_nvram_plist_append(bhnd_nvram_plist *plist, bhnd_nvram_prop *prop)
{
	bhnd_nvram_plist_entry_list	*hash_list;
	bhnd_nvram_plist_entry 		*entry;
	uint32_t			 h;

	if (bhnd_nvram_plist_contains(plist, prop->name))
		return (EEXIST);

	/* Have we hit the maximum representable entry count? */
	if (plist->num_entries == SIZE_MAX)
		return (ENOMEM);

	/* Allocate new entry */
	entry = bhnd_nv_malloc(sizeof(*entry));
	if (entry == NULL)
		return (ENOMEM);

	entry->prop = bhnd_nvram_prop_retain(prop);

	/* Append to entry list */
	TAILQ_INSERT_TAIL(&plist->entries, entry, pl_link);

	/* Add to name-based hash table */
	h = hash32_str(prop->name, HASHINIT);
	hash_list = &plist->names[h % nitems(plist->names)];
	LIST_INSERT_HEAD(hash_list, entry, pl_hash_link);

	/* Increment entry count */
	plist->num_entries++;

	return (0);
}

/**
 * Append a new property to @p plist with @p name and @p val.
 * 
 * @param	plist	The property list to be modified.
 * @param	name	The name of the property to be appended.
 * @param	val	The value of the property to be appended.
 * 
 * @retval 0		success
 * @retval ENOMEM	if allocation fails.
 * @retval EEXIST	an existing property with @p name was found in @p plist.
 */
int
bhnd_nvram_plist_append_val(bhnd_nvram_plist *plist, const char *name,
    bhnd_nvram_val *val)
{
	bhnd_nvram_prop	*prop;
	int		 error;

	if ((prop = bhnd_nvram_prop_new(name, val)) == NULL)
		return (ENOMEM);

	error = bhnd_nvram_plist_append(plist, prop);
	bhnd_nvram_prop_release(prop);

	return (error);
}

/**
 * Append a new property to @p plist, copying the property value from the
 * given @p inp buffer of @p itype and @p ilen.
 * 
 * @param	plist	The property list to be modified.
 * @param	name	The name of the property to be appended.
 * @param	inp	Input buffer.
 * @param	ilen	Input buffer length.
 * @param	itype	Input buffer type.
 * 
 * @retval 0		success
 * @retval ENOMEM	if allocation fails.
 * @retval EEXIST	an existing property with @p name was found in @p plist.
 */
int
bhnd_nvram_plist_append_bytes(bhnd_nvram_plist *plist, const char *name,
    const void *inp, size_t ilen, bhnd_nvram_type itype)
{
	bhnd_nvram_prop	*prop;
	int		 error;

	if ((prop = bhnd_nvram_prop_bytes_new(name, inp, ilen, itype)) == NULL)
		return (ENOMEM);

	error = bhnd_nvram_plist_append(plist, prop);
	bhnd_nvram_prop_release(prop);

	return (error);
}

/**
 * Append a new string property to @p plist, copying the property value from
 * @p val.
 * 
 * @param	plist	The property list to be modified.
 * @param	name	The name of the property to be appended.
 * @param	val	The new property's string value.
 *
 * @retval 0		success
 * @retval ENOMEM	if allocation fails.
 * @retval EEXIST	an existing property with @p name was found in @p plist.
 */
int
bhnd_nvram_plist_append_string(bhnd_nvram_plist *plist, const char *name,
    const char *val)
{
	return (bhnd_nvram_plist_append_bytes(plist, name, val, strlen(val)+1,
	    BHND_NVRAM_TYPE_STRING));
}

/**
 * Iterate over all properties in @p plist.
 * 
 * @param	plist	The property list to be iterated.
 * @param	prop	A property in @p plist, or NULL to return the first
 *			property in @p plist.
 * 
 * @retval non-NULL	A borrowed reference to the next property in @p plist.
 * @retval NULL		If the end of the property list is reached or @p prop
 *			is not found in @p plist.
 */
bhnd_nvram_prop *
bhnd_nvram_plist_next(bhnd_nvram_plist *plist, bhnd_nvram_prop *prop)
{
	bhnd_nvram_plist_entry *entry;

	if (prop == NULL) {
		if ((entry = TAILQ_FIRST(&plist->entries)) == NULL)
			return (NULL);

		return (entry->prop);
	}

	/* Look up previous property entry by name */
	if ((entry = bhnd_nvram_plist_get_entry(plist, prop->name)) == NULL)
		return (NULL);

	/* The property instance must be identical */
	if (entry->prop != prop)
		return (NULL);

	/* Fetch next entry */
	if ((entry = TAILQ_NEXT(entry, pl_link)) == NULL)
		return (NULL);

	return (entry->prop);
}

/**
 * Return a borrowed reference to a named property, or NULL if @p name is
 * not found in @p plist.
 * 
 * @param	plist	The property list to be queried.
 * @param	name	The name of the property to be returned.
 *
 * @retval non-NULL	if @p name is found.
 * @retval NULL		if @p name is not found.
 */
bhnd_nvram_prop *
bhnd_nvram_plist_get_prop(bhnd_nvram_plist *plist, const char *name)
{
	bhnd_nvram_plist_entry *entry;

	if ((entry = bhnd_nvram_plist_get_entry(plist, name)) == NULL)
		return (NULL);

	return (entry->prop);
}

/**
 * Return a borrowed reference to the named property's value, or NULL if
 * @p name is not found in @p plist.
 * 
 * @param	plist	The property list to be queried.
 * @param	name	The name of the property to be returned.
 *
 * @retval non-NULL	if @p name is found.
 * @retval NULL		if @p name is not found.
 */
bhnd_nvram_val *
bhnd_nvram_plist_get_val(bhnd_nvram_plist *plist, const char *name)
{
	bhnd_nvram_prop *prop;

	if ((prop = bhnd_nvram_plist_get_prop(plist, name)) == NULL)
		return (NULL);

	return (bhnd_nvram_prop_val(prop));
}

/**
 * Attempt to encode a named property's value as @p otype, writing the result
 * to @p outp.
 *
 * @param		plist	The property list to be queried.
 * @param		name	The name of the property value to be returned.
 * @param[out]		outp	On success, the value will be written to this 
 *				buffer. This argment may be NULL if the value is
 *				not desired.
 * @param[in,out]	olen	The capacity of @p outp. On success, will be set
 *				to the actual size of the requested value.
 * @param		otype	The data type to be written to @p outp.
 *
 * @retval 0		success
 * @retval ENOENT	If @p name is not found in @p plist.
 * @retval ENOMEM	If the @p outp is non-NULL, and the provided @p olen
 *			is too small to hold the encoded value.
 * @retval EFTYPE	If value coercion from @p prop to @p otype is
 *			impossible.
 * @retval ERANGE	If value coercion would overflow (or underflow) the
 *			a @p otype representation.
 */
int
bhnd_nvram_plist_get_encoded(bhnd_nvram_plist *plist, const char *name,
    void *outp, size_t olen, bhnd_nvram_type otype)
{
	bhnd_nvram_prop *prop;

	if ((prop = bhnd_nvram_plist_get_prop(plist, name)) == NULL)
		return (ENOENT);

	return (bhnd_nvram_prop_encode(prop, outp, &olen, otype));
}

/**
 * Return the character representation of a named property's value.
 * 
 * @param	plist	The property list to be queried.
 * @param	name	The name of the property value to be returned.
 * @param[out]	val	On success, the character value of @p name.
 *
 * @retval 0		success
 * @retval ENOENT	If @p name is not found in @p plist.
 * @retval EFTYPE	If coercion of the property's value to @p val.
 * @retval ERANGE	If coercion of the property's value would overflow
 *			(or underflow) @p val.
 */
int
bhnd_nvram_plist_get_char(bhnd_nvram_plist *plist, const char *name,
    u_char *val)
{
	return (bhnd_nvram_plist_get_encoded(plist, name, val, sizeof(*val),
	    BHND_NVRAM_TYPE_CHAR));
}

/**
 * Return the uint8 representation of a named property's value.
 * 
 * @param	plist	The property list to be queried.
 * @param	name	The name of the property value to be returned.
 * @param[out]	val	On success, the uint8 value of @p name.
 *
 * @retval 0		success
 * @retval ENOENT	If @p name is not found in @p plist.
 * @retval EFTYPE	If coercion of the property's value to @p val.
 * @retval ERANGE	If coercion of the property's value would overflow
 *			(or underflow) @p val.
 */
int
bhnd_nvram_plist_get_uint8(bhnd_nvram_plist *plist, const char *name,
    uint8_t *val)
{
	return (bhnd_nvram_plist_get_encoded(plist, name, val, sizeof(*val),
	    BHND_NVRAM_TYPE_UINT8));
}

/**
 * Return the uint16 representation of a named property's value.
 * 
 * @param	plist	The property list to be queried.
 * @param	name	The name of the property value to be returned.
 * @param[out]	val	On success, the uint16 value of @p name.
 *
 * @retval 0		success
 * @retval ENOENT	If @p name is not found in @p plist.
 * @retval EFTYPE	If coercion of the property's value to @p val.
 * @retval ERANGE	If coercion of the property's value would overflow
 *			(or underflow) @p val.
 */
int
bhnd_nvram_plist_get_uint16(bhnd_nvram_plist *plist, const char *name,
    uint16_t *val)
{
	return (bhnd_nvram_plist_get_encoded(plist, name, val, sizeof(*val),
	    BHND_NVRAM_TYPE_UINT16));
}

/**
 * Return the uint32 representation of a named property's value.
 * 
 * @param	plist	The property list to be queried.
 * @param	name	The name of the property value to be returned.
 * @param[out]	val	On success, the uint32 value of @p name.
 *
 * @retval 0		success
 * @retval ENOENT	If @p name is not found in @p plist.
 * @retval EFTYPE	If coercion of the property's value to @p val.
 * @retval ERANGE	If coercion of the property's value would overflow
 *			(or underflow) @p val.
 */
int
bhnd_nvram_plist_get_uint32(bhnd_nvram_plist *plist, const char *name,
    uint32_t *val)
{
	return (bhnd_nvram_plist_get_encoded(plist, name, val, sizeof(*val),
	    BHND_NVRAM_TYPE_UINT32));
}

/**
 * Return the uint64 representation of a named property's value.
 * 
 * @param	plist	The property list to be queried.
 * @param	name	The name of the property value to be returned.
 * @param[out]	val	On success, the uint64 value of @p name.
 *
 * @retval 0		success
 * @retval ENOENT	If @p name is not found in @p plist.
 * @retval EFTYPE	If coercion of the property's value to @p val.
 * @retval ERANGE	If coercion of the property's value would overflow
 *			(or underflow) @p val.
 */
int
bhnd_nvram_plist_get_uint64(bhnd_nvram_plist *plist, const char *name,
    uint64_t *val)
{
	return (bhnd_nvram_plist_get_encoded(plist, name, val, sizeof(*val),
	    BHND_NVRAM_TYPE_UINT64));
}

/**
 * Return the boolean representation of a named property's value.
 * 
 * @param	plist	The property list to be queried.
 * @param	name	The name of the property value to be returned.
 * @param[out]	val	On success, the boolean value of @p name.
 *
 * @retval 0		success
 * @retval ENOENT	If @p name is not found in @p plist.
 * @retval EFTYPE	If coercion of the property's value to @p val.
 * @retval ERANGE	If coercion of the property's value would overflow
 *			(or underflow) @p val.
 */
int
bhnd_nvram_plist_get_bool(bhnd_nvram_plist *plist, const char *name,
    bool *val)
{
	return (bhnd_nvram_plist_get_encoded(plist, name, val, sizeof(*val),
	    BHND_NVRAM_TYPE_BOOL));
}

/**
 * Allocate and initialize a new property value.
 * 
 * The caller is responsible for releasing the returned property value
 * via bhnd_nvram_prop_release().
 * 
 * @param	name	Property name.
 * @param	val	Property value.
 * 
 * @retval non-NULL	success
 * @retval NULL		if allocation fails.
 */
struct bhnd_nvram_prop *
bhnd_nvram_prop_new(const char *name, bhnd_nvram_val *val)
{
	struct bhnd_nvram_prop *prop;

	prop = bhnd_nv_calloc(1, sizeof(*prop));
	if (prop == NULL)
		return NULL;

	/* Implicit caller-owned reference */
	prop->refs = 1;

	if ((prop->name = bhnd_nv_strdup(name)) == NULL)
		goto failed;

	if ((prop->val = bhnd_nvram_val_copy(val)) == NULL)
		goto failed;

	return (prop);

failed:
	if (prop->name != NULL)
		bhnd_nv_free(prop->name);

	if (prop->val != NULL)
		bhnd_nvram_val_release(prop->val);

	bhnd_nv_free(prop);
	return (NULL);
}

/**
 * Allocate a new property value and attempt to initialize its value from
 * the given @p inp buffer of @p itype and @p ilen.
 *
 * The caller is responsible for releasing the returned property value
 * via bhnd_nvram_prop_release().
 *
 * @param	name	Property name.
 * @param	inp	Input buffer.
 * @param	ilen	Input buffer length.
 * @param	itype	Input buffer type.
 * 
 * @retval non-NULL	success
 * @retval NULL		if allocation or initialization fails.
 */
bhnd_nvram_prop *
bhnd_nvram_prop_bytes_new(const char *name, const void *inp, size_t ilen,
    bhnd_nvram_type itype)
{
	bhnd_nvram_prop	*prop;
	bhnd_nvram_val	*val;
	int		 error;

	/* Construct new value instance */
	error = bhnd_nvram_val_new(&val, NULL, inp, ilen, itype,
	    BHND_NVRAM_VAL_DYNAMIC);
	if (error) {
		if (error != ENOMEM) {
			BHND_NV_LOG("invalid input data; initialization "
			    "failed: %d\n", error);
		}

		return (NULL);
	}

	/* Delegate to default implementation */
	prop = bhnd_nvram_prop_new(name, val);

	/* Clean up */
	bhnd_nvram_val_release(val);
	return (prop);
}

/**
 * Retain a reference and return @p prop to the caller.
 * 
 * The caller is responsible for releasing their reference ownership via
 * bhnd_nvram_prop_release().
 * 
 * @param	prop	The property to be retained.
 */
bhnd_nvram_prop *
bhnd_nvram_prop_retain(bhnd_nvram_prop *prop)
{
	BHND_NV_ASSERT(prop->refs >= 1, ("prop over-released"));

	refcount_acquire(&prop->refs);
	return (prop);
}

/**
 * Release a reference to @p prop.
 *
 * If this is the last reference, all associated resources will be freed.
 * 
 * @param	prop	The property to be released.
 */
void
bhnd_nvram_prop_release(bhnd_nvram_prop *prop)
{
	BHND_NV_ASSERT(prop->refs >= 1, ("prop over-released"));

	/* Drop reference */
	if (!refcount_release(&prop->refs))
		return;

	/* Free property data */
	bhnd_nvram_val_release(prop->val);
	bhnd_nv_free(prop->name);
	bhnd_nv_free(prop);
}

/**
 * Return a borrowed reference to the property's name.
 * 
 * @param	prop	The property to query.
 */
const char *
bhnd_nvram_prop_name(bhnd_nvram_prop *prop)
{
	return (prop->name);
}

/**
 * Return a borrowed reference to the property's value.
 * 
 * @param	prop	The property to query.
 */
bhnd_nvram_val *
bhnd_nvram_prop_val(bhnd_nvram_prop *prop)
{
	return (prop->val);
}

/**
 * Return the property's value type.
 * 
 * @param	prop	The property to query.
 */
bhnd_nvram_type
bhnd_nvram_prop_type(bhnd_nvram_prop *prop)
{
	return (bhnd_nvram_val_type(prop->val));
}

/**
 * Return true if @p prop has a NULL value type (BHND_NVRAM_TYPE_NULL), false
 * otherwise.
 * 
 * @param      prop    The property to query.
 */
bool
bhnd_nvram_prop_is_null(bhnd_nvram_prop *prop)
{
	return (bhnd_nvram_prop_type(prop) == BHND_NVRAM_TYPE_NULL);
}

/**
 * Return a borrowed reference to the property's internal value representation.
 *
 * @param	prop	The property to query.
 * @param[out]	olen	The returned data's size, in bytes.
 * @param[out]	otype	The returned data's type.
 */
const void *
bhnd_nvram_prop_bytes(bhnd_nvram_prop *prop, size_t *olen,
    bhnd_nvram_type *otype)
{
	const void *bytes;

	bytes = bhnd_nvram_val_bytes(prop->val, olen, otype);
	BHND_NV_ASSERT(*otype == bhnd_nvram_prop_type(prop), ("type mismatch"));

	return (bytes);
}

/**
 * Attempt to encode the property's value as @p otype, writing the result
 * to @p outp.
 *
 * @param		prop	The property to be encoded.
 * @param[out]		outp	On success, the value will be written to this 
 *				buffer. This argment may be NULL if the value is
 *				not desired.
 * @param[in,out]	olen	The capacity of @p outp. On success, will be set
 *				to the actual size of the requested value.
 * @param		otype	The data type to be written to @p outp.
 *
 * @retval 0		success
 * @retval ENOMEM	If the @p outp is non-NULL, and the provided @p olen
 *			is too small to hold the encoded value.
 * @retval EFTYPE	If value coercion from @p prop to @p otype is
 *			impossible.
 * @retval ERANGE	If value coercion would overflow (or underflow) the
 *			a @p otype representation.
 */
int
bhnd_nvram_prop_encode(bhnd_nvram_prop *prop, void *outp, size_t *olen,
    bhnd_nvram_type otype)
{
	return (bhnd_nvram_val_encode(prop->val, outp, olen, otype));
}
