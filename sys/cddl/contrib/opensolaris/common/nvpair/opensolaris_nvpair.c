/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2000, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2015, 2017 by Delphix. All rights reserved.
 */

#include <sys/debug.h>
#include <sys/nvpair.h>
#include <sys/nvpair_impl.h>
#include <rpc/types.h>
#include <rpc/xdr.h>

#if defined(_KERNEL) && !defined(_BOOT)
#include <sys/varargs.h>
#include <sys/sunddi.h>
#else
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#endif

#ifndef	offsetof
#define	offsetof(s, m)		((size_t)(&(((s *)0)->m)))
#endif
#define	skip_whitespace(p)	while ((*(p) == ' ') || (*(p) == '\t')) p++

#if defined(__FreeBSD__) && !defined(_KERNEL)
/*
 * libnvpair is the lowest commen denominator for ZFS related libraries,
 * defining aok here makes it usable by all ZFS related libraries
 */
int aok;
#endif

/*
 * nvpair.c - Provides kernel & userland interfaces for manipulating
 *	name-value pairs.
 *
 * Overview Diagram
 *
 *  +--------------+
 *  |  nvlist_t    |
 *  |--------------|
 *  | nvl_version  |
 *  | nvl_nvflag   |
 *  | nvl_priv    -+-+
 *  | nvl_flag     | |
 *  | nvl_pad      | |
 *  +--------------+ |
 *                   V
 *      +--------------+      last i_nvp in list
 *      | nvpriv_t     |  +--------------------->
 *      |--------------|  |
 *   +--+- nvp_list    |  |   +------------+
 *   |  |  nvp_last   -+--+   + nv_alloc_t |
 *   |  |  nvp_curr    |      |------------|
 *   |  |  nvp_nva    -+----> | nva_ops    |
 *   |  |  nvp_stat    |      | nva_arg    |
 *   |  +--------------+      +------------+
 *   |
 *   +-------+
 *           V
 *   +---------------------+      +-------------------+
 *   |  i_nvp_t            |  +-->|  i_nvp_t          |  +-->
 *   |---------------------|  |   |-------------------|  |
 *   | nvi_next           -+--+   | nvi_next         -+--+
 *   | nvi_prev (NULL)     | <----+ nvi_prev          |
 *   | . . . . . . . . . . |      | . . . . . . . . . |
 *   | nvp (nvpair_t)      |      | nvp (nvpair_t)    |
 *   |  - nvp_size         |      |  - nvp_size       |
 *   |  - nvp_name_sz      |      |  - nvp_name_sz    |
 *   |  - nvp_value_elem   |      |  - nvp_value_elem |
 *   |  - nvp_type         |      |  - nvp_type       |
 *   |  - data ...         |      |  - data ...       |
 *   +---------------------+      +-------------------+
 *
 *
 *
 *   +---------------------+              +---------------------+
 *   |  i_nvp_t            |  +-->    +-->|  i_nvp_t (last)     |
 *   |---------------------|  |       |   |---------------------|
 *   |  nvi_next          -+--+ ... --+   | nvi_next (NULL)     |
 * <-+- nvi_prev           |<-- ...  <----+ nvi_prev            |
 *   | . . . . . . . . .   |              | . . . . . . . . .   |
 *   | nvp (nvpair_t)      |              | nvp (nvpair_t)      |
 *   |  - nvp_size         |              |  - nvp_size         |
 *   |  - nvp_name_sz      |              |  - nvp_name_sz      |
 *   |  - nvp_value_elem   |              |  - nvp_value_elem   |
 *   |  - DATA_TYPE_NVLIST |              |  - nvp_type         |
 *   |  - data (embedded)  |              |  - data ...         |
 *   |    nvlist name      |              +---------------------+
 *   |  +--------------+   |
 *   |  |  nvlist_t    |   |
 *   |  |--------------|   |
 *   |  | nvl_version  |   |
 *   |  | nvl_nvflag   |   |
 *   |  | nvl_priv   --+---+---->
 *   |  | nvl_flag     |   |
 *   |  | nvl_pad      |   |
 *   |  +--------------+   |
 *   +---------------------+
 *
 *
 * N.B. nvpair_t may be aligned on 4 byte boundary, so +4 will
 * allow value to be aligned on 8 byte boundary
 *
 * name_len is the length of the name string including the null terminator
 * so it must be >= 1
 */
#define	NVP_SIZE_CALC(name_len, data_len) \
	(NV_ALIGN((sizeof (nvpair_t)) + name_len) + NV_ALIGN(data_len))

static int i_get_value_size(data_type_t type, const void *data, uint_t nelem);
static int nvlist_add_common(nvlist_t *nvl, const char *name, data_type_t type,
    uint_t nelem, const void *data);

#define	NV_STAT_EMBEDDED	0x1
#define	EMBEDDED_NVL(nvp)	((nvlist_t *)(void *)NVP_VALUE(nvp))
#define	EMBEDDED_NVL_ARRAY(nvp)	((nvlist_t **)(void *)NVP_VALUE(nvp))

#define	NVP_VALOFF(nvp)	(NV_ALIGN(sizeof (nvpair_t) + (nvp)->nvp_name_sz))
#define	NVPAIR2I_NVP(nvp) \
	((i_nvp_t *)((size_t)(nvp) - offsetof(i_nvp_t, nvi_nvp)))

#ifdef _KERNEL
int nvpair_max_recursion = 20;
#else
int nvpair_max_recursion = 100;
#endif

uint64_t nvlist_hashtable_init_size = (1 << 4);

int
nv_alloc_init(nv_alloc_t *nva, const nv_alloc_ops_t *nvo, /* args */ ...)
{
	va_list valist;
	int err = 0;

	nva->nva_ops = nvo;
	nva->nva_arg = NULL;

	va_start(valist, nvo);
	if (nva->nva_ops->nv_ao_init != NULL)
		err = nva->nva_ops->nv_ao_init(nva, valist);
	va_end(valist);

	return (err);
}

void
nv_alloc_reset(nv_alloc_t *nva)
{
	if (nva->nva_ops->nv_ao_reset != NULL)
		nva->nva_ops->nv_ao_reset(nva);
}

void
nv_alloc_fini(nv_alloc_t *nva)
{
	if (nva->nva_ops->nv_ao_fini != NULL)
		nva->nva_ops->nv_ao_fini(nva);
}

nv_alloc_t *
nvlist_lookup_nv_alloc(nvlist_t *nvl)
{
	nvpriv_t *priv;

	if (nvl == NULL ||
	    (priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv) == NULL)
		return (NULL);

	return (priv->nvp_nva);
}

static void *
nv_mem_zalloc(nvpriv_t *nvp, size_t size)
{
	nv_alloc_t *nva = nvp->nvp_nva;
	void *buf;

	if ((buf = nva->nva_ops->nv_ao_alloc(nva, size)) != NULL)
		bzero(buf, size);

	return (buf);
}

static void
nv_mem_free(nvpriv_t *nvp, void *buf, size_t size)
{
	nv_alloc_t *nva = nvp->nvp_nva;

	nva->nva_ops->nv_ao_free(nva, buf, size);
}

static void
nv_priv_init(nvpriv_t *priv, nv_alloc_t *nva, uint32_t stat)
{
	bzero(priv, sizeof (nvpriv_t));

	priv->nvp_nva = nva;
	priv->nvp_stat = stat;
}

static nvpriv_t *
nv_priv_alloc(nv_alloc_t *nva)
{
	nvpriv_t *priv;

	/*
	 * nv_mem_alloc() cannot called here because it needs the priv
	 * argument.
	 */
	if ((priv = nva->nva_ops->nv_ao_alloc(nva, sizeof (nvpriv_t))) == NULL)
		return (NULL);

	nv_priv_init(priv, nva, 0);

	return (priv);
}

/*
 * Embedded lists need their own nvpriv_t's.  We create a new
 * nvpriv_t using the parameters and allocator from the parent
 * list's nvpriv_t.
 */
static nvpriv_t *
nv_priv_alloc_embedded(nvpriv_t *priv)
{
	nvpriv_t *emb_priv;

	if ((emb_priv = nv_mem_zalloc(priv, sizeof (nvpriv_t))) == NULL)
		return (NULL);

	nv_priv_init(emb_priv, priv->nvp_nva, NV_STAT_EMBEDDED);

	return (emb_priv);
}

static int
nvt_tab_alloc(nvpriv_t *priv, uint64_t buckets)
{
	ASSERT3P(priv->nvp_hashtable, ==, NULL);
	ASSERT0(priv->nvp_nbuckets);
	ASSERT0(priv->nvp_nentries);

	i_nvp_t **tab = nv_mem_zalloc(priv, buckets * sizeof (i_nvp_t *));
	if (tab == NULL)
		return (ENOMEM);

	priv->nvp_hashtable = tab;
	priv->nvp_nbuckets = buckets;
	return (0);
}

static void
nvt_tab_free(nvpriv_t *priv)
{
	i_nvp_t **tab = priv->nvp_hashtable;
	if (tab == NULL) {
		ASSERT0(priv->nvp_nbuckets);
		ASSERT0(priv->nvp_nentries);
		return;
	}

	nv_mem_free(priv, tab, priv->nvp_nbuckets * sizeof (i_nvp_t *));

	priv->nvp_hashtable = NULL;
	priv->nvp_nbuckets = 0;
	priv->nvp_nentries = 0;
}

static uint32_t
nvt_hash(const char *p)
{
	uint32_t g, hval = 0;

	while (*p) {
		hval = (hval << 4) + *p++;
		if ((g = (hval & 0xf0000000)) != 0)
			hval ^= g >> 24;
		hval &= ~g;
	}
	return (hval);
}

static boolean_t
nvt_nvpair_match(nvpair_t *nvp1, nvpair_t *nvp2, uint32_t nvflag)
{
	boolean_t match = B_FALSE;
	if (nvflag & NV_UNIQUE_NAME_TYPE) {
		if (strcmp(NVP_NAME(nvp1), NVP_NAME(nvp2)) == 0 &&
		    NVP_TYPE(nvp1) == NVP_TYPE(nvp2))
			match = B_TRUE;
	} else {
		ASSERT(nvflag == 0 || nvflag & NV_UNIQUE_NAME);
		if (strcmp(NVP_NAME(nvp1), NVP_NAME(nvp2)) == 0)
			match = B_TRUE;
	}
	return (match);
}

static nvpair_t *
nvt_lookup_name_type(nvlist_t *nvl, const char *name, data_type_t type)
{
	nvpriv_t *priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv;
	ASSERT(priv != NULL);

	i_nvp_t **tab = priv->nvp_hashtable;

	if (tab == NULL) {
		ASSERT3P(priv->nvp_list, ==, NULL);
		ASSERT0(priv->nvp_nbuckets);
		ASSERT0(priv->nvp_nentries);
		return (NULL);
	} else {
		ASSERT(priv->nvp_nbuckets != 0);
	}

	uint64_t hash = nvt_hash(name);
	uint64_t index = hash & (priv->nvp_nbuckets - 1);

	ASSERT3U(index, <, priv->nvp_nbuckets);
	i_nvp_t *entry = tab[index];

	for (i_nvp_t *e = entry; e != NULL; e = e->nvi_hashtable_next) {
		if (strcmp(NVP_NAME(&e->nvi_nvp), name) == 0 &&
		    (type == DATA_TYPE_DONTCARE ||
		    NVP_TYPE(&e->nvi_nvp) == type))
			return (&e->nvi_nvp);
	}
	return (NULL);
}

static nvpair_t *
nvt_lookup_name(nvlist_t *nvl, const char *name)
{
	return (nvt_lookup_name_type(nvl, name, DATA_TYPE_DONTCARE));
}

static int
nvt_resize(nvpriv_t *priv, uint32_t new_size)
{
	i_nvp_t **tab = priv->nvp_hashtable;

	/*
	 * Migrate all the entries from the current table
	 * to a newly-allocated table with the new size by
	 * re-adjusting the pointers of their entries.
	 */
	uint32_t size = priv->nvp_nbuckets;
	uint32_t new_mask = new_size - 1;
	ASSERT(((new_size) & ((new_size) - 1)) == 0);

	i_nvp_t **new_tab = nv_mem_zalloc(priv, new_size * sizeof (i_nvp_t *));
	if (new_tab == NULL)
		return (ENOMEM);

	uint32_t nentries = 0;
	for (uint32_t i = 0; i < size; i++) {
		i_nvp_t *next, *e = tab[i];

		while (e != NULL) {
			next = e->nvi_hashtable_next;

			uint32_t hash = nvt_hash(NVP_NAME(&e->nvi_nvp));
			uint32_t index = hash & new_mask;

			e->nvi_hashtable_next = new_tab[index];
			new_tab[index] = e;
			nentries++;

			e = next;
		}
		tab[i] = NULL;
	}
	ASSERT3U(nentries, ==, priv->nvp_nentries);

	nvt_tab_free(priv);

	priv->nvp_hashtable = new_tab;
	priv->nvp_nbuckets = new_size;
	priv->nvp_nentries = nentries;

	return (0);
}

static boolean_t
nvt_needs_togrow(nvpriv_t *priv)
{
	/*
	 * Grow only when we have more elements than buckets
	 * and the # of buckets doesn't overflow.
	 */
	return (priv->nvp_nentries > priv->nvp_nbuckets &&
	    (UINT32_MAX >> 1) >= priv->nvp_nbuckets);
}

/*
 * Allocate a new table that's twice the size of the old one,
 * and migrate all the entries from the old one to the new
 * one by re-adjusting their pointers.
 */
static int
nvt_grow(nvpriv_t *priv)
{
	uint32_t current_size = priv->nvp_nbuckets;
	/* ensure we won't overflow */
	ASSERT3U(UINT32_MAX >> 1, >=, current_size);
	return (nvt_resize(priv, current_size << 1));
}

static boolean_t
nvt_needs_toshrink(nvpriv_t *priv)
{
	/*
	 * Shrink only when the # of elements is less than or
	 * equal to 1/4 the # of buckets. Never shrink less than
	 * nvlist_hashtable_init_size.
	 */
	ASSERT3U(priv->nvp_nbuckets, >=, nvlist_hashtable_init_size);
	if (priv->nvp_nbuckets == nvlist_hashtable_init_size)
		return (B_FALSE);
	return (priv->nvp_nentries <= (priv->nvp_nbuckets >> 2));
}

/*
 * Allocate a new table that's half the size of the old one,
 * and migrate all the entries from the old one to the new
 * one by re-adjusting their pointers.
 */
static int
nvt_shrink(nvpriv_t *priv)
{
	uint32_t current_size = priv->nvp_nbuckets;
	/* ensure we won't overflow */
	ASSERT3U(current_size, >=, nvlist_hashtable_init_size);
	return (nvt_resize(priv, current_size >> 1));
}

static int
nvt_remove_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{
	nvpriv_t *priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv;

	if (nvt_needs_toshrink(priv)) {
		int err = nvt_shrink(priv);
		if (err != 0)
			return (err);
	}
	i_nvp_t **tab = priv->nvp_hashtable;

	char *name = NVP_NAME(nvp);
	uint64_t hash = nvt_hash(name);
	uint64_t index = hash & (priv->nvp_nbuckets - 1);

	ASSERT3U(index, <, priv->nvp_nbuckets);
	i_nvp_t *bucket = tab[index];

	for (i_nvp_t *prev = NULL, *e = bucket;
	    e != NULL; prev = e, e = e->nvi_hashtable_next) {
		if (nvt_nvpair_match(&e->nvi_nvp, nvp, nvl->nvl_flag)) {
			if (prev != NULL) {
				prev->nvi_hashtable_next =
				    e->nvi_hashtable_next;
			} else {
				ASSERT3P(e, ==, bucket);
				tab[index] = e->nvi_hashtable_next;
			}
			e->nvi_hashtable_next = NULL;
			priv->nvp_nentries--;
			break;
		}
	}

	return (0);
}

static int
nvt_add_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{
	nvpriv_t *priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv;

	/* initialize nvpair table now if it doesn't exist. */
	if (priv->nvp_hashtable == NULL) {
		int err = nvt_tab_alloc(priv, nvlist_hashtable_init_size);
		if (err != 0)
			return (err);
	}

	/*
	 * if we don't allow duplicate entries, make sure to
	 * unlink any existing entries from the table.
	 */
	if (nvl->nvl_nvflag != 0) {
		int err = nvt_remove_nvpair(nvl, nvp);
		if (err != 0)
			return (err);
	}

	if (nvt_needs_togrow(priv)) {
		int err = nvt_grow(priv);
		if (err != 0)
			return (err);
	}
	i_nvp_t **tab = priv->nvp_hashtable;

	char *name = NVP_NAME(nvp);
	uint64_t hash = nvt_hash(name);
	uint64_t index = hash & (priv->nvp_nbuckets - 1);

	ASSERT3U(index, <, priv->nvp_nbuckets);
	i_nvp_t *bucket = tab[index];

	/* insert link at the beginning of the bucket */
	i_nvp_t *new_entry = NVPAIR2I_NVP(nvp);
	ASSERT3P(new_entry->nvi_hashtable_next, ==, NULL);
	new_entry->nvi_hashtable_next = bucket;
	tab[index] = new_entry;

	priv->nvp_nentries++;
	return (0);
}

static void
nvlist_init(nvlist_t *nvl, uint32_t nvflag, nvpriv_t *priv)
{
	nvl->nvl_version = NV_VERSION;
	nvl->nvl_nvflag = nvflag & (NV_UNIQUE_NAME|NV_UNIQUE_NAME_TYPE);
	nvl->nvl_priv = (uint64_t)(uintptr_t)priv;
	nvl->nvl_flag = 0;
	nvl->nvl_pad = 0;
}

uint_t
nvlist_nvflag(nvlist_t *nvl)
{
	return (nvl->nvl_nvflag);
}

/*
 * nvlist_alloc - Allocate nvlist.
 */
/*ARGSUSED1*/
int
nvlist_alloc(nvlist_t **nvlp, uint_t nvflag, int kmflag)
{
#if defined(_KERNEL) && !defined(_BOOT)
	return (nvlist_xalloc(nvlp, nvflag,
	    (kmflag == KM_SLEEP ? nv_alloc_sleep : nv_alloc_nosleep)));
#else
	return (nvlist_xalloc(nvlp, nvflag, nv_alloc_nosleep));
#endif
}

int
nvlist_xalloc(nvlist_t **nvlp, uint_t nvflag, nv_alloc_t *nva)
{
	nvpriv_t *priv;

	if (nvlp == NULL || nva == NULL)
		return (EINVAL);

	if ((priv = nv_priv_alloc(nva)) == NULL)
		return (ENOMEM);

	if ((*nvlp = nv_mem_zalloc(priv,
	    NV_ALIGN(sizeof (nvlist_t)))) == NULL) {
		nv_mem_free(priv, priv, sizeof (nvpriv_t));
		return (ENOMEM);
	}

	nvlist_init(*nvlp, nvflag, priv);

	return (0);
}

/*
 * nvp_buf_alloc - Allocate i_nvp_t for storing a new nv pair.
 */
static nvpair_t *
nvp_buf_alloc(nvlist_t *nvl, size_t len)
{
	nvpriv_t *priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv;
	i_nvp_t *buf;
	nvpair_t *nvp;
	size_t nvsize;

	/*
	 * Allocate the buffer
	 */
	nvsize = len + offsetof(i_nvp_t, nvi_nvp);

	if ((buf = nv_mem_zalloc(priv, nvsize)) == NULL)
		return (NULL);

	nvp = &buf->nvi_nvp;
	nvp->nvp_size = len;

	return (nvp);
}

/*
 * nvp_buf_free - de-Allocate an i_nvp_t.
 */
static void
nvp_buf_free(nvlist_t *nvl, nvpair_t *nvp)
{
	nvpriv_t *priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv;
	size_t nvsize = nvp->nvp_size + offsetof(i_nvp_t, nvi_nvp);

	nv_mem_free(priv, NVPAIR2I_NVP(nvp), nvsize);
}

/*
 * nvp_buf_link - link a new nv pair into the nvlist.
 */
static void
nvp_buf_link(nvlist_t *nvl, nvpair_t *nvp)
{
	nvpriv_t *priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv;
	i_nvp_t *curr = NVPAIR2I_NVP(nvp);

	/* Put element at end of nvlist */
	if (priv->nvp_list == NULL) {
		priv->nvp_list = priv->nvp_last = curr;
	} else {
		curr->nvi_prev = priv->nvp_last;
		priv->nvp_last->nvi_next = curr;
		priv->nvp_last = curr;
	}
}

/*
 * nvp_buf_unlink - unlink an removed nvpair out of the nvlist.
 */
static void
nvp_buf_unlink(nvlist_t *nvl, nvpair_t *nvp)
{
	nvpriv_t *priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv;
	i_nvp_t *curr = NVPAIR2I_NVP(nvp);

	/*
	 * protect nvlist_next_nvpair() against walking on freed memory.
	 */
	if (priv->nvp_curr == curr)
		priv->nvp_curr = curr->nvi_next;

	if (curr == priv->nvp_list)
		priv->nvp_list = curr->nvi_next;
	else
		curr->nvi_prev->nvi_next = curr->nvi_next;

	if (curr == priv->nvp_last)
		priv->nvp_last = curr->nvi_prev;
	else
		curr->nvi_next->nvi_prev = curr->nvi_prev;
}

/*
 * take a nvpair type and number of elements and make sure the are valid
 */
static int
i_validate_type_nelem(data_type_t type, uint_t nelem)
{
	switch (type) {
	case DATA_TYPE_BOOLEAN:
		if (nelem != 0)
			return (EINVAL);
		break;
	case DATA_TYPE_BOOLEAN_VALUE:
	case DATA_TYPE_BYTE:
	case DATA_TYPE_INT8:
	case DATA_TYPE_UINT8:
	case DATA_TYPE_INT16:
	case DATA_TYPE_UINT16:
	case DATA_TYPE_INT32:
	case DATA_TYPE_UINT32:
	case DATA_TYPE_INT64:
	case DATA_TYPE_UINT64:
	case DATA_TYPE_STRING:
	case DATA_TYPE_HRTIME:
	case DATA_TYPE_NVLIST:
#if !defined(_KERNEL)
	case DATA_TYPE_DOUBLE:
#endif
		if (nelem != 1)
			return (EINVAL);
		break;
	case DATA_TYPE_BOOLEAN_ARRAY:
	case DATA_TYPE_BYTE_ARRAY:
	case DATA_TYPE_INT8_ARRAY:
	case DATA_TYPE_UINT8_ARRAY:
	case DATA_TYPE_INT16_ARRAY:
	case DATA_TYPE_UINT16_ARRAY:
	case DATA_TYPE_INT32_ARRAY:
	case DATA_TYPE_UINT32_ARRAY:
	case DATA_TYPE_INT64_ARRAY:
	case DATA_TYPE_UINT64_ARRAY:
	case DATA_TYPE_STRING_ARRAY:
	case DATA_TYPE_NVLIST_ARRAY:
		/* we allow arrays with 0 elements */
		break;
	default:
		return (EINVAL);
	}
	return (0);
}

/*
 * Verify nvp_name_sz and check the name string length.
 */
static int
i_validate_nvpair_name(nvpair_t *nvp)
{
	if ((nvp->nvp_name_sz <= 0) ||
	    (nvp->nvp_size < NVP_SIZE_CALC(nvp->nvp_name_sz, 0)))
		return (EFAULT);

	/* verify the name string, make sure its terminated */
	if (NVP_NAME(nvp)[nvp->nvp_name_sz - 1] != '\0')
		return (EFAULT);

	return (strlen(NVP_NAME(nvp)) == nvp->nvp_name_sz - 1 ? 0 : EFAULT);
}

static int
i_validate_nvpair_value(data_type_t type, uint_t nelem, const void *data)
{
	switch (type) {
	case DATA_TYPE_BOOLEAN_VALUE:
		if (*(boolean_t *)data != B_TRUE &&
		    *(boolean_t *)data != B_FALSE)
			return (EINVAL);
		break;
	case DATA_TYPE_BOOLEAN_ARRAY: {
		int i;

		for (i = 0; i < nelem; i++)
			if (((boolean_t *)data)[i] != B_TRUE &&
			    ((boolean_t *)data)[i] != B_FALSE)
				return (EINVAL);
		break;
	}
	default:
		break;
	}

	return (0);
}

/*
 * This function takes a pointer to what should be a nvpair and it's size
 * and then verifies that all the nvpair fields make sense and can be
 * trusted.  This function is used when decoding packed nvpairs.
 */
static int
i_validate_nvpair(nvpair_t *nvp)
{
	data_type_t type = NVP_TYPE(nvp);
	int size1, size2;

	/* verify nvp_name_sz, check the name string length */
	if (i_validate_nvpair_name(nvp) != 0)
		return (EFAULT);

	if (i_validate_nvpair_value(type, NVP_NELEM(nvp), NVP_VALUE(nvp)) != 0)
		return (EFAULT);

	/*
	 * verify nvp_type, nvp_value_elem, and also possibly
	 * verify string values and get the value size.
	 */
	size2 = i_get_value_size(type, NVP_VALUE(nvp), NVP_NELEM(nvp));
	size1 = nvp->nvp_size - NVP_VALOFF(nvp);
	if (size2 < 0 || size1 != NV_ALIGN(size2))
		return (EFAULT);

	return (0);
}

static int
nvlist_copy_pairs(nvlist_t *snvl, nvlist_t *dnvl)
{
	nvpriv_t *priv;
	i_nvp_t *curr;

	if ((priv = (nvpriv_t *)(uintptr_t)snvl->nvl_priv) == NULL)
		return (EINVAL);

	for (curr = priv->nvp_list; curr != NULL; curr = curr->nvi_next) {
		nvpair_t *nvp = &curr->nvi_nvp;
		int err;

		if ((err = nvlist_add_common(dnvl, NVP_NAME(nvp), NVP_TYPE(nvp),
		    NVP_NELEM(nvp), NVP_VALUE(nvp))) != 0)
			return (err);
	}

	return (0);
}

/*
 * Frees all memory allocated for an nvpair (like embedded lists) with
 * the exception of the nvpair buffer itself.
 */
static void
nvpair_free(nvpair_t *nvp)
{
	switch (NVP_TYPE(nvp)) {
	case DATA_TYPE_NVLIST:
		nvlist_free(EMBEDDED_NVL(nvp));
		break;
	case DATA_TYPE_NVLIST_ARRAY: {
		nvlist_t **nvlp = EMBEDDED_NVL_ARRAY(nvp);
		int i;

		for (i = 0; i < NVP_NELEM(nvp); i++)
			nvlist_free(nvlp[i]);
		break;
	}
	default:
		break;
	}
}

/*
 * nvlist_free - free an unpacked nvlist
 */
void
nvlist_free(nvlist_t *nvl)
{
	nvpriv_t *priv;
	i_nvp_t *curr;

	if (nvl == NULL ||
	    (priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv) == NULL)
		return;

	/*
	 * Unpacked nvlist are linked through i_nvp_t
	 */
	curr = priv->nvp_list;
	while (curr != NULL) {
		nvpair_t *nvp = &curr->nvi_nvp;
		curr = curr->nvi_next;

		nvpair_free(nvp);
		nvp_buf_free(nvl, nvp);
	}

	if (!(priv->nvp_stat & NV_STAT_EMBEDDED))
		nv_mem_free(priv, nvl, NV_ALIGN(sizeof (nvlist_t)));
	else
		nvl->nvl_priv = 0;

	nvt_tab_free(priv);
	nv_mem_free(priv, priv, sizeof (nvpriv_t));
}

static int
nvlist_contains_nvp(nvlist_t *nvl, nvpair_t *nvp)
{
	nvpriv_t *priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv;
	i_nvp_t *curr;

	if (nvp == NULL)
		return (0);

	for (curr = priv->nvp_list; curr != NULL; curr = curr->nvi_next)
		if (&curr->nvi_nvp == nvp)
			return (1);

	return (0);
}

/*
 * Make a copy of nvlist
 */
/*ARGSUSED1*/
int
nvlist_dup(nvlist_t *nvl, nvlist_t **nvlp, int kmflag)
{
#if defined(_KERNEL) && !defined(_BOOT)
	return (nvlist_xdup(nvl, nvlp,
	    (kmflag == KM_SLEEP ? nv_alloc_sleep : nv_alloc_nosleep)));
#else
	return (nvlist_xdup(nvl, nvlp, nv_alloc_nosleep));
#endif
}

int
nvlist_xdup(nvlist_t *nvl, nvlist_t **nvlp, nv_alloc_t *nva)
{
	int err;
	nvlist_t *ret;

	if (nvl == NULL || nvlp == NULL)
		return (EINVAL);

	if ((err = nvlist_xalloc(&ret, nvl->nvl_nvflag, nva)) != 0)
		return (err);

	if ((err = nvlist_copy_pairs(nvl, ret)) != 0)
		nvlist_free(ret);
	else
		*nvlp = ret;

	return (err);
}

/*
 * Remove all with matching name
 */
int
nvlist_remove_all(nvlist_t *nvl, const char *name)
{
	int error = ENOENT;

	if (nvl == NULL || name == NULL || nvl->nvl_priv == 0)
		return (EINVAL);

	nvpair_t *nvp;
	while ((nvp = nvt_lookup_name(nvl, name)) != NULL) {
		VERIFY0(nvlist_remove_nvpair(nvl, nvp));
		error = 0;
	}

	return (error);
}

/*
 * Remove first one with matching name and type
 */
int
nvlist_remove(nvlist_t *nvl, const char *name, data_type_t type)
{
	if (nvl == NULL || name == NULL || nvl->nvl_priv == 0)
		return (EINVAL);

	nvpair_t *nvp = nvt_lookup_name_type(nvl, name, type);
	if (nvp == NULL)
		return (ENOENT);

	return (nvlist_remove_nvpair(nvl, nvp));
}

int
nvlist_remove_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{
	if (nvl == NULL || nvp == NULL)
		return (EINVAL);

	int err = nvt_remove_nvpair(nvl, nvp);
	if (err != 0)
		return (err);

	nvp_buf_unlink(nvl, nvp);
	nvpair_free(nvp);
	nvp_buf_free(nvl, nvp);
	return (0);
}

/*
 * This function calculates the size of an nvpair value.
 *
 * The data argument controls the behavior in case of the data types
 * 	DATA_TYPE_STRING    	and
 *	DATA_TYPE_STRING_ARRAY
 * Is data == NULL then the size of the string(s) is excluded.
 */
static int
i_get_value_size(data_type_t type, const void *data, uint_t nelem)
{
	uint64_t value_sz;

	if (i_validate_type_nelem(type, nelem) != 0)
		return (-1);

	/* Calculate required size for holding value */
	switch (type) {
	case DATA_TYPE_BOOLEAN:
		value_sz = 0;
		break;
	case DATA_TYPE_BOOLEAN_VALUE:
		value_sz = sizeof (boolean_t);
		break;
	case DATA_TYPE_BYTE:
		value_sz = sizeof (uchar_t);
		break;
	case DATA_TYPE_INT8:
		value_sz = sizeof (int8_t);
		break;
	case DATA_TYPE_UINT8:
		value_sz = sizeof (uint8_t);
		break;
	case DATA_TYPE_INT16:
		value_sz = sizeof (int16_t);
		break;
	case DATA_TYPE_UINT16:
		value_sz = sizeof (uint16_t);
		break;
	case DATA_TYPE_INT32:
		value_sz = sizeof (int32_t);
		break;
	case DATA_TYPE_UINT32:
		value_sz = sizeof (uint32_t);
		break;
	case DATA_TYPE_INT64:
		value_sz = sizeof (int64_t);
		break;
	case DATA_TYPE_UINT64:
		value_sz = sizeof (uint64_t);
		break;
#if !defined(_KERNEL)
	case DATA_TYPE_DOUBLE:
		value_sz = sizeof (double);
		break;
#endif
	case DATA_TYPE_STRING:
		if (data == NULL)
			value_sz = 0;
		else
			value_sz = strlen(data) + 1;
		break;
	case DATA_TYPE_BOOLEAN_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (boolean_t);
		break;
	case DATA_TYPE_BYTE_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (uchar_t);
		break;
	case DATA_TYPE_INT8_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (int8_t);
		break;
	case DATA_TYPE_UINT8_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (uint8_t);
		break;
	case DATA_TYPE_INT16_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (int16_t);
		break;
	case DATA_TYPE_UINT16_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (uint16_t);
		break;
	case DATA_TYPE_INT32_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (int32_t);
		break;
	case DATA_TYPE_UINT32_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (uint32_t);
		break;
	case DATA_TYPE_INT64_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (int64_t);
		break;
	case DATA_TYPE_UINT64_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (uint64_t);
		break;
	case DATA_TYPE_STRING_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (uint64_t);

		if (data != NULL) {
			char *const *strs = data;
			uint_t i;

			/* no alignment requirement for strings */
			for (i = 0; i < nelem; i++) {
				if (strs[i] == NULL)
					return (-1);
				value_sz += strlen(strs[i]) + 1;
			}
		}
		break;
	case DATA_TYPE_HRTIME:
		value_sz = sizeof (hrtime_t);
		break;
	case DATA_TYPE_NVLIST:
		value_sz = NV_ALIGN(sizeof (nvlist_t));
		break;
	case DATA_TYPE_NVLIST_ARRAY:
		value_sz = (uint64_t)nelem * sizeof (uint64_t) +
		    (uint64_t)nelem * NV_ALIGN(sizeof (nvlist_t));
		break;
	default:
		return (-1);
	}

	return (value_sz > INT32_MAX ? -1 : (int)value_sz);
}

static int
nvlist_copy_embedded(nvlist_t *nvl, nvlist_t *onvl, nvlist_t *emb_nvl)
{
	nvpriv_t *priv;
	int err;

	if ((priv = nv_priv_alloc_embedded((nvpriv_t *)(uintptr_t)
	    nvl->nvl_priv)) == NULL)
		return (ENOMEM);

	nvlist_init(emb_nvl, onvl->nvl_nvflag, priv);

	if ((err = nvlist_copy_pairs(onvl, emb_nvl)) != 0) {
		nvlist_free(emb_nvl);
		emb_nvl->nvl_priv = 0;
	}

	return (err);
}

/*
 * nvlist_add_common - Add new <name,value> pair to nvlist
 */
static int
nvlist_add_common(nvlist_t *nvl, const char *name,
    data_type_t type, uint_t nelem, const void *data)
{
	nvpair_t *nvp;
	uint_t i;

	int nvp_sz, name_sz, value_sz;
	int err = 0;

	if (name == NULL || nvl == NULL || nvl->nvl_priv == 0)
		return (EINVAL);

	if (nelem != 0 && data == NULL)
		return (EINVAL);

	/*
	 * Verify type and nelem and get the value size.
	 * In case of data types DATA_TYPE_STRING and DATA_TYPE_STRING_ARRAY
	 * is the size of the string(s) included.
	 */
	if ((value_sz = i_get_value_size(type, data, nelem)) < 0)
		return (EINVAL);

	if (i_validate_nvpair_value(type, nelem, data) != 0)
		return (EINVAL);

	/*
	 * If we're adding an nvlist or nvlist array, ensure that we are not
	 * adding the input nvlist to itself, which would cause recursion,
	 * and ensure that no NULL nvlist pointers are present.
	 */
	switch (type) {
	case DATA_TYPE_NVLIST:
		if (data == nvl || data == NULL)
			return (EINVAL);
		break;
	case DATA_TYPE_NVLIST_ARRAY: {
		nvlist_t **onvlp = (nvlist_t **)data;
		for (i = 0; i < nelem; i++) {
			if (onvlp[i] == nvl || onvlp[i] == NULL)
				return (EINVAL);
		}
		break;
	}
	default:
		break;
	}

	/* calculate sizes of the nvpair elements and the nvpair itself */
	name_sz = strlen(name) + 1;
	if (name_sz >= 1ULL << (sizeof (nvp->nvp_name_sz) * 8 - 1))
		return (EINVAL);

	nvp_sz = NVP_SIZE_CALC(name_sz, value_sz);

	if ((nvp = nvp_buf_alloc(nvl, nvp_sz)) == NULL)
		return (ENOMEM);

	ASSERT(nvp->nvp_size == nvp_sz);
	nvp->nvp_name_sz = name_sz;
	nvp->nvp_value_elem = nelem;
	nvp->nvp_type = type;
	bcopy(name, NVP_NAME(nvp), name_sz);

	switch (type) {
	case DATA_TYPE_BOOLEAN:
		break;
	case DATA_TYPE_STRING_ARRAY: {
		char *const *strs = data;
		char *buf = NVP_VALUE(nvp);
		char **cstrs = (void *)buf;

		/* skip pre-allocated space for pointer array */
		buf += nelem * sizeof (uint64_t);
		for (i = 0; i < nelem; i++) {
			int slen = strlen(strs[i]) + 1;
			bcopy(strs[i], buf, slen);
			cstrs[i] = buf;
			buf += slen;
		}
		break;
	}
	case DATA_TYPE_NVLIST: {
		nvlist_t *nnvl = EMBEDDED_NVL(nvp);
		nvlist_t *onvl = (nvlist_t *)data;

		if ((err = nvlist_copy_embedded(nvl, onvl, nnvl)) != 0) {
			nvp_buf_free(nvl, nvp);
			return (err);
		}
		break;
	}
	case DATA_TYPE_NVLIST_ARRAY: {
		nvlist_t **onvlp = (nvlist_t **)data;
		nvlist_t **nvlp = EMBEDDED_NVL_ARRAY(nvp);
		nvlist_t *embedded = (nvlist_t *)
		    ((uintptr_t)nvlp + nelem * sizeof (uint64_t));

		for (i = 0; i < nelem; i++) {
			if ((err = nvlist_copy_embedded(nvl,
			    onvlp[i], embedded)) != 0) {
				/*
				 * Free any successfully created lists
				 */
				nvpair_free(nvp);
				nvp_buf_free(nvl, nvp);
				return (err);
			}

			nvlp[i] = embedded++;
		}
		break;
	}
	default:
		bcopy(data, NVP_VALUE(nvp), value_sz);
	}

	/* if unique name, remove before add */
	if (nvl->nvl_nvflag & NV_UNIQUE_NAME)
		(void) nvlist_remove_all(nvl, name);
	else if (nvl->nvl_nvflag & NV_UNIQUE_NAME_TYPE)
		(void) nvlist_remove(nvl, name, type);

	err = nvt_add_nvpair(nvl, nvp);
	if (err != 0) {
		nvpair_free(nvp);
		nvp_buf_free(nvl, nvp);
		return (err);
	}
	nvp_buf_link(nvl, nvp);

	return (0);
}

int
nvlist_add_boolean(nvlist_t *nvl, const char *name)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_BOOLEAN, 0, NULL));
}

int
nvlist_add_boolean_value(nvlist_t *nvl, const char *name, boolean_t val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_BOOLEAN_VALUE, 1, &val));
}

int
nvlist_add_byte(nvlist_t *nvl, const char *name, uchar_t val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_BYTE, 1, &val));
}

int
nvlist_add_int8(nvlist_t *nvl, const char *name, int8_t val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_INT8, 1, &val));
}

int
nvlist_add_uint8(nvlist_t *nvl, const char *name, uint8_t val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_UINT8, 1, &val));
}

int
nvlist_add_int16(nvlist_t *nvl, const char *name, int16_t val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_INT16, 1, &val));
}

int
nvlist_add_uint16(nvlist_t *nvl, const char *name, uint16_t val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_UINT16, 1, &val));
}

int
nvlist_add_int32(nvlist_t *nvl, const char *name, int32_t val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_INT32, 1, &val));
}

int
nvlist_add_uint32(nvlist_t *nvl, const char *name, uint32_t val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_UINT32, 1, &val));
}

int
nvlist_add_int64(nvlist_t *nvl, const char *name, int64_t val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_INT64, 1, &val));
}

int
nvlist_add_uint64(nvlist_t *nvl, const char *name, uint64_t val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_UINT64, 1, &val));
}

#if !defined(_KERNEL)
int
nvlist_add_double(nvlist_t *nvl, const char *name, double val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_DOUBLE, 1, &val));
}
#endif

int
nvlist_add_string(nvlist_t *nvl, const char *name, const char *val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_STRING, 1, (void *)val));
}

int
nvlist_add_boolean_array(nvlist_t *nvl, const char *name,
    boolean_t *a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_BOOLEAN_ARRAY, n, a));
}

int
nvlist_add_byte_array(nvlist_t *nvl, const char *name, uchar_t *a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_BYTE_ARRAY, n, a));
}

int
nvlist_add_int8_array(nvlist_t *nvl, const char *name, int8_t *a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_INT8_ARRAY, n, a));
}

int
nvlist_add_uint8_array(nvlist_t *nvl, const char *name, uint8_t *a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_UINT8_ARRAY, n, a));
}

int
nvlist_add_int16_array(nvlist_t *nvl, const char *name, int16_t *a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_INT16_ARRAY, n, a));
}

int
nvlist_add_uint16_array(nvlist_t *nvl, const char *name, uint16_t *a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_UINT16_ARRAY, n, a));
}

int
nvlist_add_int32_array(nvlist_t *nvl, const char *name, int32_t *a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_INT32_ARRAY, n, a));
}

int
nvlist_add_uint32_array(nvlist_t *nvl, const char *name, uint32_t *a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_UINT32_ARRAY, n, a));
}

int
nvlist_add_int64_array(nvlist_t *nvl, const char *name, int64_t *a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_INT64_ARRAY, n, a));
}

int
nvlist_add_uint64_array(nvlist_t *nvl, const char *name, uint64_t *a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_UINT64_ARRAY, n, a));
}

int
nvlist_add_string_array(nvlist_t *nvl, const char *name,
    char *const *a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_STRING_ARRAY, n, a));
}

int
nvlist_add_hrtime(nvlist_t *nvl, const char *name, hrtime_t val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_HRTIME, 1, &val));
}

int
nvlist_add_nvlist(nvlist_t *nvl, const char *name, nvlist_t *val)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_NVLIST, 1, val));
}

int
nvlist_add_nvlist_array(nvlist_t *nvl, const char *name, nvlist_t **a, uint_t n)
{
	return (nvlist_add_common(nvl, name, DATA_TYPE_NVLIST_ARRAY, n, a));
}

/* reading name-value pairs */
nvpair_t *
nvlist_next_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{
	nvpriv_t *priv;
	i_nvp_t *curr;

	if (nvl == NULL ||
	    (priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv) == NULL)
		return (NULL);

	curr = NVPAIR2I_NVP(nvp);

	/*
	 * Ensure that nvp is a valid nvpair on this nvlist.
	 * NB: nvp_curr is used only as a hint so that we don't always
	 * have to walk the list to determine if nvp is still on the list.
	 */
	if (nvp == NULL)
		curr = priv->nvp_list;
	else if (priv->nvp_curr == curr || nvlist_contains_nvp(nvl, nvp))
		curr = curr->nvi_next;
	else
		curr = NULL;

	priv->nvp_curr = curr;

	return (curr != NULL ? &curr->nvi_nvp : NULL);
}

nvpair_t *
nvlist_prev_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{
	nvpriv_t *priv;
	i_nvp_t *curr;

	if (nvl == NULL ||
	    (priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv) == NULL)
		return (NULL);

	curr = NVPAIR2I_NVP(nvp);

	if (nvp == NULL)
		curr = priv->nvp_last;
	else if (priv->nvp_curr == curr || nvlist_contains_nvp(nvl, nvp))
		curr = curr->nvi_prev;
	else
		curr = NULL;

	priv->nvp_curr = curr;

	return (curr != NULL ? &curr->nvi_nvp : NULL);
}

boolean_t
nvlist_empty(nvlist_t *nvl)
{
	nvpriv_t *priv;

	if (nvl == NULL ||
	    (priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv) == NULL)
		return (B_TRUE);

	return (priv->nvp_list == NULL);
}

char *
nvpair_name(nvpair_t *nvp)
{
	return (NVP_NAME(nvp));
}

data_type_t
nvpair_type(nvpair_t *nvp)
{
	return (NVP_TYPE(nvp));
}

int
nvpair_type_is_array(nvpair_t *nvp)
{
	data_type_t type = NVP_TYPE(nvp);

	if ((type == DATA_TYPE_BYTE_ARRAY) ||
	    (type == DATA_TYPE_INT8_ARRAY) ||
	    (type == DATA_TYPE_UINT8_ARRAY) ||
	    (type == DATA_TYPE_INT16_ARRAY) ||
	    (type == DATA_TYPE_UINT16_ARRAY) ||
	    (type == DATA_TYPE_INT32_ARRAY) ||
	    (type == DATA_TYPE_UINT32_ARRAY) ||
	    (type == DATA_TYPE_INT64_ARRAY) ||
	    (type == DATA_TYPE_UINT64_ARRAY) ||
	    (type == DATA_TYPE_BOOLEAN_ARRAY) ||
	    (type == DATA_TYPE_STRING_ARRAY) ||
	    (type == DATA_TYPE_NVLIST_ARRAY))
		return (1);
	return (0);

}

static int
nvpair_value_common(nvpair_t *nvp, data_type_t type, uint_t *nelem, void *data)
{
	if (nvp == NULL || nvpair_type(nvp) != type)
		return (EINVAL);

	/*
	 * For non-array types, we copy the data.
	 * For array types (including string), we set a pointer.
	 */
	switch (type) {
	case DATA_TYPE_BOOLEAN:
		if (nelem != NULL)
			*nelem = 0;
		break;

	case DATA_TYPE_BOOLEAN_VALUE:
	case DATA_TYPE_BYTE:
	case DATA_TYPE_INT8:
	case DATA_TYPE_UINT8:
	case DATA_TYPE_INT16:
	case DATA_TYPE_UINT16:
	case DATA_TYPE_INT32:
	case DATA_TYPE_UINT32:
	case DATA_TYPE_INT64:
	case DATA_TYPE_UINT64:
	case DATA_TYPE_HRTIME:
#if !defined(_KERNEL)
	case DATA_TYPE_DOUBLE:
#endif
		if (data == NULL)
			return (EINVAL);
		bcopy(NVP_VALUE(nvp), data,
		    (size_t)i_get_value_size(type, NULL, 1));
		if (nelem != NULL)
			*nelem = 1;
		break;

	case DATA_TYPE_NVLIST:
	case DATA_TYPE_STRING:
		if (data == NULL)
			return (EINVAL);
		*(void **)data = (void *)NVP_VALUE(nvp);
		if (nelem != NULL)
			*nelem = 1;
		break;

	case DATA_TYPE_BOOLEAN_ARRAY:
	case DATA_TYPE_BYTE_ARRAY:
	case DATA_TYPE_INT8_ARRAY:
	case DATA_TYPE_UINT8_ARRAY:
	case DATA_TYPE_INT16_ARRAY:
	case DATA_TYPE_UINT16_ARRAY:
	case DATA_TYPE_INT32_ARRAY:
	case DATA_TYPE_UINT32_ARRAY:
	case DATA_TYPE_INT64_ARRAY:
	case DATA_TYPE_UINT64_ARRAY:
	case DATA_TYPE_STRING_ARRAY:
	case DATA_TYPE_NVLIST_ARRAY:
		if (nelem == NULL || data == NULL)
			return (EINVAL);
		if ((*nelem = NVP_NELEM(nvp)) != 0)
			*(void **)data = (void *)NVP_VALUE(nvp);
		else
			*(void **)data = NULL;
		break;

	default:
		return (ENOTSUP);
	}

	return (0);
}

static int
nvlist_lookup_common(nvlist_t *nvl, const char *name, data_type_t type,
    uint_t *nelem, void *data)
{
	if (name == NULL || nvl == NULL || nvl->nvl_priv == 0)
		return (EINVAL);

	if (!(nvl->nvl_nvflag & (NV_UNIQUE_NAME | NV_UNIQUE_NAME_TYPE)))
		return (ENOTSUP);

	nvpair_t *nvp = nvt_lookup_name_type(nvl, name, type);
	if (nvp == NULL)
		return (ENOENT);

	return (nvpair_value_common(nvp, type, nelem, data));
}

int
nvlist_lookup_boolean(nvlist_t *nvl, const char *name)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_BOOLEAN, NULL, NULL));
}

int
nvlist_lookup_boolean_value(nvlist_t *nvl, const char *name, boolean_t *val)
{
	return (nvlist_lookup_common(nvl, name,
	    DATA_TYPE_BOOLEAN_VALUE, NULL, val));
}

int
nvlist_lookup_byte(nvlist_t *nvl, const char *name, uchar_t *val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_BYTE, NULL, val));
}

int
nvlist_lookup_int8(nvlist_t *nvl, const char *name, int8_t *val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_INT8, NULL, val));
}

int
nvlist_lookup_uint8(nvlist_t *nvl, const char *name, uint8_t *val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_UINT8, NULL, val));
}

int
nvlist_lookup_int16(nvlist_t *nvl, const char *name, int16_t *val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_INT16, NULL, val));
}

int
nvlist_lookup_uint16(nvlist_t *nvl, const char *name, uint16_t *val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_UINT16, NULL, val));
}

int
nvlist_lookup_int32(nvlist_t *nvl, const char *name, int32_t *val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_INT32, NULL, val));
}

int
nvlist_lookup_uint32(nvlist_t *nvl, const char *name, uint32_t *val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_UINT32, NULL, val));
}

int
nvlist_lookup_int64(nvlist_t *nvl, const char *name, int64_t *val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_INT64, NULL, val));
}

int
nvlist_lookup_uint64(nvlist_t *nvl, const char *name, uint64_t *val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_UINT64, NULL, val));
}

#if !defined(_KERNEL)
int
nvlist_lookup_double(nvlist_t *nvl, const char *name, double *val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_DOUBLE, NULL, val));
}
#endif

int
nvlist_lookup_string(nvlist_t *nvl, const char *name, char **val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_STRING, NULL, val));
}

int
nvlist_lookup_nvlist(nvlist_t *nvl, const char *name, nvlist_t **val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_NVLIST, NULL, val));
}

int
nvlist_lookup_boolean_array(nvlist_t *nvl, const char *name,
    boolean_t **a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name,
	    DATA_TYPE_BOOLEAN_ARRAY, n, a));
}

int
nvlist_lookup_byte_array(nvlist_t *nvl, const char *name,
    uchar_t **a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_BYTE_ARRAY, n, a));
}

int
nvlist_lookup_int8_array(nvlist_t *nvl, const char *name, int8_t **a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_INT8_ARRAY, n, a));
}

int
nvlist_lookup_uint8_array(nvlist_t *nvl, const char *name,
    uint8_t **a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_UINT8_ARRAY, n, a));
}

int
nvlist_lookup_int16_array(nvlist_t *nvl, const char *name,
    int16_t **a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_INT16_ARRAY, n, a));
}

int
nvlist_lookup_uint16_array(nvlist_t *nvl, const char *name,
    uint16_t **a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_UINT16_ARRAY, n, a));
}

int
nvlist_lookup_int32_array(nvlist_t *nvl, const char *name,
    int32_t **a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_INT32_ARRAY, n, a));
}

int
nvlist_lookup_uint32_array(nvlist_t *nvl, const char *name,
    uint32_t **a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_UINT32_ARRAY, n, a));
}

int
nvlist_lookup_int64_array(nvlist_t *nvl, const char *name,
    int64_t **a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_INT64_ARRAY, n, a));
}

int
nvlist_lookup_uint64_array(nvlist_t *nvl, const char *name,
    uint64_t **a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_UINT64_ARRAY, n, a));
}

int
nvlist_lookup_string_array(nvlist_t *nvl, const char *name,
    char ***a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_STRING_ARRAY, n, a));
}

int
nvlist_lookup_nvlist_array(nvlist_t *nvl, const char *name,
    nvlist_t ***a, uint_t *n)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_NVLIST_ARRAY, n, a));
}

int
nvlist_lookup_hrtime(nvlist_t *nvl, const char *name, hrtime_t *val)
{
	return (nvlist_lookup_common(nvl, name, DATA_TYPE_HRTIME, NULL, val));
}

int
nvlist_lookup_pairs(nvlist_t *nvl, int flag, ...)
{
	va_list ap;
	char *name;
	int noentok = (flag & NV_FLAG_NOENTOK ? 1 : 0);
	int ret = 0;

	va_start(ap, flag);
	while (ret == 0 && (name = va_arg(ap, char *)) != NULL) {
		data_type_t type;
		void *val;
		uint_t *nelem;

		switch (type = va_arg(ap, data_type_t)) {
		case DATA_TYPE_BOOLEAN:
			ret = nvlist_lookup_common(nvl, name, type, NULL, NULL);
			break;

		case DATA_TYPE_BOOLEAN_VALUE:
		case DATA_TYPE_BYTE:
		case DATA_TYPE_INT8:
		case DATA_TYPE_UINT8:
		case DATA_TYPE_INT16:
		case DATA_TYPE_UINT16:
		case DATA_TYPE_INT32:
		case DATA_TYPE_UINT32:
		case DATA_TYPE_INT64:
		case DATA_TYPE_UINT64:
		case DATA_TYPE_HRTIME:
		case DATA_TYPE_STRING:
		case DATA_TYPE_NVLIST:
#if !defined(_KERNEL)
		case DATA_TYPE_DOUBLE:
#endif
			val = va_arg(ap, void *);
			ret = nvlist_lookup_common(nvl, name, type, NULL, val);
			break;

		case DATA_TYPE_BYTE_ARRAY:
		case DATA_TYPE_BOOLEAN_ARRAY:
		case DATA_TYPE_INT8_ARRAY:
		case DATA_TYPE_UINT8_ARRAY:
		case DATA_TYPE_INT16_ARRAY:
		case DATA_TYPE_UINT16_ARRAY:
		case DATA_TYPE_INT32_ARRAY:
		case DATA_TYPE_UINT32_ARRAY:
		case DATA_TYPE_INT64_ARRAY:
		case DATA_TYPE_UINT64_ARRAY:
		case DATA_TYPE_STRING_ARRAY:
		case DATA_TYPE_NVLIST_ARRAY:
			val = va_arg(ap, void *);
			nelem = va_arg(ap, uint_t *);
			ret = nvlist_lookup_common(nvl, name, type, nelem, val);
			break;

		default:
			ret = EINVAL;
		}

		if (ret == ENOENT && noentok)
			ret = 0;
	}
	va_end(ap);

	return (ret);
}

/*
 * Find the 'name'ed nvpair in the nvlist 'nvl'. If 'name' found, the function
 * returns zero and a pointer to the matching nvpair is returned in '*ret'
 * (given 'ret' is non-NULL). If 'sep' is specified then 'name' will penitrate
 * multiple levels of embedded nvlists, with 'sep' as the separator. As an
 * example, if sep is '.', name might look like: "a" or "a.b" or "a.c[3]" or
 * "a.d[3].e[1]".  This matches the C syntax for array embed (for convience,
 * code also supports "a.d[3]e[1]" syntax).
 *
 * If 'ip' is non-NULL and the last name component is an array, return the
 * value of the "...[index]" array index in *ip. For an array reference that
 * is not indexed, *ip will be returned as -1. If there is a syntax error in
 * 'name', and 'ep' is non-NULL then *ep will be set to point to the location
 * inside the 'name' string where the syntax error was detected.
 */
static int
nvlist_lookup_nvpair_ei_sep(nvlist_t *nvl, const char *name, const char sep,
    nvpair_t **ret, int *ip, char **ep)
{
	nvpair_t	*nvp;
	const char	*np;
	char		*sepp;
	char		*idxp, *idxep;
	nvlist_t	**nva;
	long		idx;
	int		n;

	if (ip)
		*ip = -1;			/* not indexed */
	if (ep)
		*ep = NULL;

	if ((nvl == NULL) || (name == NULL))
		return (EINVAL);

	sepp = NULL;
	idx = 0;
	/* step through components of name */
	for (np = name; np && *np; np = sepp) {
		/* ensure unique names */
		if (!(nvl->nvl_nvflag & NV_UNIQUE_NAME))
			return (ENOTSUP);

		/* skip white space */
		skip_whitespace(np);
		if (*np == 0)
			break;

		/* set 'sepp' to end of current component 'np' */
		if (sep)
			sepp = strchr(np, sep);
		else
			sepp = NULL;

		/* find start of next "[ index ]..." */
		idxp = strchr(np, '[');

		/* if sepp comes first, set idxp to NULL */
		if (sepp && idxp && (sepp < idxp))
			idxp = NULL;

		/*
		 * At this point 'idxp' is set if there is an index
		 * expected for the current component.
		 */
		if (idxp) {
			/* set 'n' to length of current 'np' name component */
			n = idxp++ - np;

			/* keep sepp up to date for *ep use as we advance */
			skip_whitespace(idxp);
			sepp = idxp;

			/* determine the index value */
#if defined(_KERNEL) && !defined(_BOOT)
			if (ddi_strtol(idxp, &idxep, 0, &idx))
				goto fail;
#else
			idx = strtol(idxp, &idxep, 0);
#endif
			if (idxep == idxp)
				goto fail;

			/* keep sepp up to date for *ep use as we advance */
			sepp = idxep;

			/* skip white space index value and check for ']' */
			skip_whitespace(sepp);
			if (*sepp++ != ']')
				goto fail;

			/* for embedded arrays, support C syntax: "a[1].b" */
			skip_whitespace(sepp);
			if (sep && (*sepp == sep))
				sepp++;
		} else if (sepp) {
			n = sepp++ - np;
		} else {
			n = strlen(np);
		}

		/* trim trailing whitespace by reducing length of 'np' */
		if (n == 0)
			goto fail;
		for (n--; (np[n] == ' ') || (np[n] == '\t'); n--)
			;
		n++;

		/* skip whitespace, and set sepp to NULL if complete */
		if (sepp) {
			skip_whitespace(sepp);
			if (*sepp == 0)
				sepp = NULL;
		}

		/*
		 * At this point:
		 * o  'n' is the length of current 'np' component.
		 * o  'idxp' is set if there was an index, and value 'idx'.
		 * o  'sepp' is set to the beginning of the next component,
		 *    and set to NULL if we have no more components.
		 *
		 * Search for nvpair with matching component name.
		 */
		for (nvp = nvlist_next_nvpair(nvl, NULL); nvp != NULL;
		    nvp = nvlist_next_nvpair(nvl, nvp)) {

			/* continue if no match on name */
			if (strncmp(np, nvpair_name(nvp), n) ||
			    (strlen(nvpair_name(nvp)) != n))
				continue;

			/* if indexed, verify type is array oriented */
			if (idxp && !nvpair_type_is_array(nvp))
				goto fail;

			/*
			 * Full match found, return nvp and idx if this
			 * was the last component.
			 */
			if (sepp == NULL) {
				if (ret)
					*ret = nvp;
				if (ip && idxp)
					*ip = (int)idx;	/* return index */
				return (0);		/* found */
			}

			/*
			 * More components: current match must be
			 * of DATA_TYPE_NVLIST or DATA_TYPE_NVLIST_ARRAY
			 * to support going deeper.
			 */
			if (nvpair_type(nvp) == DATA_TYPE_NVLIST) {
				nvl = EMBEDDED_NVL(nvp);
				break;
			} else if (nvpair_type(nvp) == DATA_TYPE_NVLIST_ARRAY) {
				(void) nvpair_value_nvlist_array(nvp,
				    &nva, (uint_t *)&n);
				if ((n < 0) || (idx >= n))
					goto fail;
				nvl = nva[idx];
				break;
			}

			/* type does not support more levels */
			goto fail;
		}
		if (nvp == NULL)
			goto fail;		/* 'name' not found */

		/* search for match of next component in embedded 'nvl' list */
	}

fail:	if (ep && sepp)
		*ep = sepp;
	return (EINVAL);
}

/*
 * Return pointer to nvpair with specified 'name'.
 */
int
nvlist_lookup_nvpair(nvlist_t *nvl, const char *name, nvpair_t **ret)
{
	return (nvlist_lookup_nvpair_ei_sep(nvl, name, 0, ret, NULL, NULL));
}

/*
 * Determine if named nvpair exists in nvlist (use embedded separator of '.'
 * and return array index).  See nvlist_lookup_nvpair_ei_sep for more detailed
 * description.
 */
int nvlist_lookup_nvpair_embedded_index(nvlist_t *nvl,
    const char *name, nvpair_t **ret, int *ip, char **ep)
{
	return (nvlist_lookup_nvpair_ei_sep(nvl, name, '.', ret, ip, ep));
}

boolean_t
nvlist_exists(nvlist_t *nvl, const char *name)
{
	nvpriv_t *priv;
	nvpair_t *nvp;
	i_nvp_t *curr;

	if (name == NULL || nvl == NULL ||
	    (priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv) == NULL)
		return (B_FALSE);

	for (curr = priv->nvp_list; curr != NULL; curr = curr->nvi_next) {
		nvp = &curr->nvi_nvp;

		if (strcmp(name, NVP_NAME(nvp)) == 0)
			return (B_TRUE);
	}

	return (B_FALSE);
}

int
nvpair_value_boolean_value(nvpair_t *nvp, boolean_t *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_BOOLEAN_VALUE, NULL, val));
}

int
nvpair_value_byte(nvpair_t *nvp, uchar_t *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_BYTE, NULL, val));
}

int
nvpair_value_int8(nvpair_t *nvp, int8_t *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_INT8, NULL, val));
}

int
nvpair_value_uint8(nvpair_t *nvp, uint8_t *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_UINT8, NULL, val));
}

int
nvpair_value_int16(nvpair_t *nvp, int16_t *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_INT16, NULL, val));
}

int
nvpair_value_uint16(nvpair_t *nvp, uint16_t *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_UINT16, NULL, val));
}

int
nvpair_value_int32(nvpair_t *nvp, int32_t *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_INT32, NULL, val));
}

int
nvpair_value_uint32(nvpair_t *nvp, uint32_t *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_UINT32, NULL, val));
}

int
nvpair_value_int64(nvpair_t *nvp, int64_t *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_INT64, NULL, val));
}

int
nvpair_value_uint64(nvpair_t *nvp, uint64_t *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_UINT64, NULL, val));
}

#if !defined(_KERNEL)
int
nvpair_value_double(nvpair_t *nvp, double *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_DOUBLE, NULL, val));
}
#endif

int
nvpair_value_string(nvpair_t *nvp, char **val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_STRING, NULL, val));
}

int
nvpair_value_nvlist(nvpair_t *nvp, nvlist_t **val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_NVLIST, NULL, val));
}

int
nvpair_value_boolean_array(nvpair_t *nvp, boolean_t **val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_BOOLEAN_ARRAY, nelem, val));
}

int
nvpair_value_byte_array(nvpair_t *nvp, uchar_t **val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_BYTE_ARRAY, nelem, val));
}

int
nvpair_value_int8_array(nvpair_t *nvp, int8_t **val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_INT8_ARRAY, nelem, val));
}

int
nvpair_value_uint8_array(nvpair_t *nvp, uint8_t **val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_UINT8_ARRAY, nelem, val));
}

int
nvpair_value_int16_array(nvpair_t *nvp, int16_t **val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_INT16_ARRAY, nelem, val));
}

int
nvpair_value_uint16_array(nvpair_t *nvp, uint16_t **val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_UINT16_ARRAY, nelem, val));
}

int
nvpair_value_int32_array(nvpair_t *nvp, int32_t **val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_INT32_ARRAY, nelem, val));
}

int
nvpair_value_uint32_array(nvpair_t *nvp, uint32_t **val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_UINT32_ARRAY, nelem, val));
}

int
nvpair_value_int64_array(nvpair_t *nvp, int64_t **val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_INT64_ARRAY, nelem, val));
}

int
nvpair_value_uint64_array(nvpair_t *nvp, uint64_t **val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_UINT64_ARRAY, nelem, val));
}

int
nvpair_value_string_array(nvpair_t *nvp, char ***val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_STRING_ARRAY, nelem, val));
}

int
nvpair_value_nvlist_array(nvpair_t *nvp, nvlist_t ***val, uint_t *nelem)
{
	return (nvpair_value_common(nvp, DATA_TYPE_NVLIST_ARRAY, nelem, val));
}

int
nvpair_value_hrtime(nvpair_t *nvp, hrtime_t *val)
{
	return (nvpair_value_common(nvp, DATA_TYPE_HRTIME, NULL, val));
}

/*
 * Add specified pair to the list.
 */
int
nvlist_add_nvpair(nvlist_t *nvl, nvpair_t *nvp)
{
	if (nvl == NULL || nvp == NULL)
		return (EINVAL);

	return (nvlist_add_common(nvl, NVP_NAME(nvp), NVP_TYPE(nvp),
	    NVP_NELEM(nvp), NVP_VALUE(nvp)));
}

/*
 * Merge the supplied nvlists and put the result in dst.
 * The merged list will contain all names specified in both lists,
 * the values are taken from nvl in the case of duplicates.
 * Return 0 on success.
 */
/*ARGSUSED*/
int
nvlist_merge(nvlist_t *dst, nvlist_t *nvl, int flag)
{
	if (nvl == NULL || dst == NULL)
		return (EINVAL);

	if (dst != nvl)
		return (nvlist_copy_pairs(nvl, dst));

	return (0);
}

/*
 * Encoding related routines
 */
#define	NVS_OP_ENCODE	0
#define	NVS_OP_DECODE	1
#define	NVS_OP_GETSIZE	2

typedef struct nvs_ops nvs_ops_t;

typedef struct {
	int		nvs_op;
	const nvs_ops_t	*nvs_ops;
	void		*nvs_private;
	nvpriv_t	*nvs_priv;
	int		nvs_recursion;
} nvstream_t;

/*
 * nvs operations are:
 *   - nvs_nvlist
 *     encoding / decoding of a nvlist header (nvlist_t)
 *     calculates the size used for header and end detection
 *
 *   - nvs_nvpair
 *     responsible for the first part of encoding / decoding of an nvpair
 *     calculates the decoded size of an nvpair
 *
 *   - nvs_nvp_op
 *     second part of encoding / decoding of an nvpair
 *
 *   - nvs_nvp_size
 *     calculates the encoding size of an nvpair
 *
 *   - nvs_nvl_fini
 *     encodes the end detection mark (zeros).
 */
struct nvs_ops {
	int (*nvs_nvlist)(nvstream_t *, nvlist_t *, size_t *);
	int (*nvs_nvpair)(nvstream_t *, nvpair_t *, size_t *);
	int (*nvs_nvp_op)(nvstream_t *, nvpair_t *);
	int (*nvs_nvp_size)(nvstream_t *, nvpair_t *, size_t *);
	int (*nvs_nvl_fini)(nvstream_t *);
};

typedef struct {
	char	nvh_encoding;	/* nvs encoding method */
	char	nvh_endian;	/* nvs endian */
	char	nvh_reserved1;	/* reserved for future use */
	char	nvh_reserved2;	/* reserved for future use */
} nvs_header_t;

static int
nvs_encode_pairs(nvstream_t *nvs, nvlist_t *nvl)
{
	nvpriv_t *priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv;
	i_nvp_t *curr;

	/*
	 * Walk nvpair in list and encode each nvpair
	 */
	for (curr = priv->nvp_list; curr != NULL; curr = curr->nvi_next)
		if (nvs->nvs_ops->nvs_nvpair(nvs, &curr->nvi_nvp, NULL) != 0)
			return (EFAULT);

	return (nvs->nvs_ops->nvs_nvl_fini(nvs));
}

static int
nvs_decode_pairs(nvstream_t *nvs, nvlist_t *nvl)
{
	nvpair_t *nvp;
	size_t nvsize;
	int err;

	/*
	 * Get decoded size of next pair in stream, alloc
	 * memory for nvpair_t, then decode the nvpair
	 */
	while ((err = nvs->nvs_ops->nvs_nvpair(nvs, NULL, &nvsize)) == 0) {
		if (nvsize == 0) /* end of list */
			break;

		/* make sure len makes sense */
		if (nvsize < NVP_SIZE_CALC(1, 0))
			return (EFAULT);

		if ((nvp = nvp_buf_alloc(nvl, nvsize)) == NULL)
			return (ENOMEM);

		if ((err = nvs->nvs_ops->nvs_nvp_op(nvs, nvp)) != 0) {
			nvp_buf_free(nvl, nvp);
			return (err);
		}

		if (i_validate_nvpair(nvp) != 0) {
			nvpair_free(nvp);
			nvp_buf_free(nvl, nvp);
			return (EFAULT);
		}

		err = nvt_add_nvpair(nvl, nvp);
		if (err != 0) {
			nvpair_free(nvp);
			nvp_buf_free(nvl, nvp);
			return (err);
		}
		nvp_buf_link(nvl, nvp);
	}
	return (err);
}

static int
nvs_getsize_pairs(nvstream_t *nvs, nvlist_t *nvl, size_t *buflen)
{
	nvpriv_t *priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv;
	i_nvp_t *curr;
	uint64_t nvsize = *buflen;
	size_t size;

	/*
	 * Get encoded size of nvpairs in nvlist
	 */
	for (curr = priv->nvp_list; curr != NULL; curr = curr->nvi_next) {
		if (nvs->nvs_ops->nvs_nvp_size(nvs, &curr->nvi_nvp, &size) != 0)
			return (EINVAL);

		if ((nvsize += size) > INT32_MAX)
			return (EINVAL);
	}

	*buflen = nvsize;
	return (0);
}

static int
nvs_operation(nvstream_t *nvs, nvlist_t *nvl, size_t *buflen)
{
	int err;

	if (nvl->nvl_priv == 0)
		return (EFAULT);

	/*
	 * Perform the operation, starting with header, then each nvpair
	 */
	if ((err = nvs->nvs_ops->nvs_nvlist(nvs, nvl, buflen)) != 0)
		return (err);

	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE:
		err = nvs_encode_pairs(nvs, nvl);
		break;

	case NVS_OP_DECODE:
		err = nvs_decode_pairs(nvs, nvl);
		break;

	case NVS_OP_GETSIZE:
		err = nvs_getsize_pairs(nvs, nvl, buflen);
		break;

	default:
		err = EINVAL;
	}

	return (err);
}

static int
nvs_embedded(nvstream_t *nvs, nvlist_t *embedded)
{
	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE: {
		int err;

		if (nvs->nvs_recursion >= nvpair_max_recursion)
			return (EINVAL);
		nvs->nvs_recursion++;
		err = nvs_operation(nvs, embedded, NULL);
		nvs->nvs_recursion--;
		return (err);
	}
	case NVS_OP_DECODE: {
		nvpriv_t *priv;
		int err;

		if (embedded->nvl_version != NV_VERSION)
			return (ENOTSUP);

		if ((priv = nv_priv_alloc_embedded(nvs->nvs_priv)) == NULL)
			return (ENOMEM);

		nvlist_init(embedded, embedded->nvl_nvflag, priv);

		if (nvs->nvs_recursion >= nvpair_max_recursion) {
			nvlist_free(embedded);
			return (EINVAL);
		}
		nvs->nvs_recursion++;
		if ((err = nvs_operation(nvs, embedded, NULL)) != 0)
			nvlist_free(embedded);
		nvs->nvs_recursion--;
		return (err);
	}
	default:
		break;
	}

	return (EINVAL);
}

static int
nvs_embedded_nvl_array(nvstream_t *nvs, nvpair_t *nvp, size_t *size)
{
	size_t nelem = NVP_NELEM(nvp);
	nvlist_t **nvlp = EMBEDDED_NVL_ARRAY(nvp);
	int i;

	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE:
		for (i = 0; i < nelem; i++)
			if (nvs_embedded(nvs, nvlp[i]) != 0)
				return (EFAULT);
		break;

	case NVS_OP_DECODE: {
		size_t len = nelem * sizeof (uint64_t);
		nvlist_t *embedded = (nvlist_t *)((uintptr_t)nvlp + len);

		bzero(nvlp, len);	/* don't trust packed data */
		for (i = 0; i < nelem; i++) {
			if (nvs_embedded(nvs, embedded) != 0) {
				nvpair_free(nvp);
				return (EFAULT);
			}

			nvlp[i] = embedded++;
		}
		break;
	}
	case NVS_OP_GETSIZE: {
		uint64_t nvsize = 0;

		for (i = 0; i < nelem; i++) {
			size_t nvp_sz = 0;

			if (nvs_operation(nvs, nvlp[i], &nvp_sz) != 0)
				return (EINVAL);

			if ((nvsize += nvp_sz) > INT32_MAX)
				return (EINVAL);
		}

		*size = nvsize;
		break;
	}
	default:
		return (EINVAL);
	}

	return (0);
}

static int nvs_native(nvstream_t *, nvlist_t *, char *, size_t *);
static int nvs_xdr(nvstream_t *, nvlist_t *, char *, size_t *);

/*
 * Common routine for nvlist operations:
 * encode, decode, getsize (encoded size).
 */
static int
nvlist_common(nvlist_t *nvl, char *buf, size_t *buflen, int encoding,
    int nvs_op)
{
	int err = 0;
	nvstream_t nvs;
	int nvl_endian;
#if BYTE_ORDER == _LITTLE_ENDIAN
	int host_endian = 1;
#else
	int host_endian = 0;
#endif	/* _LITTLE_ENDIAN */
	nvs_header_t *nvh = (void *)buf;

	if (buflen == NULL || nvl == NULL ||
	    (nvs.nvs_priv = (nvpriv_t *)(uintptr_t)nvl->nvl_priv) == NULL)
		return (EINVAL);

	nvs.nvs_op = nvs_op;
	nvs.nvs_recursion = 0;

	/*
	 * For NVS_OP_ENCODE and NVS_OP_DECODE make sure an nvlist and
	 * a buffer is allocated.  The first 4 bytes in the buffer are
	 * used for encoding method and host endian.
	 */
	switch (nvs_op) {
	case NVS_OP_ENCODE:
		if (buf == NULL || *buflen < sizeof (nvs_header_t))
			return (EINVAL);

		nvh->nvh_encoding = encoding;
		nvh->nvh_endian = nvl_endian = host_endian;
		nvh->nvh_reserved1 = 0;
		nvh->nvh_reserved2 = 0;
		break;

	case NVS_OP_DECODE:
		if (buf == NULL || *buflen < sizeof (nvs_header_t))
			return (EINVAL);

		/* get method of encoding from first byte */
		encoding = nvh->nvh_encoding;
		nvl_endian = nvh->nvh_endian;
		break;

	case NVS_OP_GETSIZE:
		nvl_endian = host_endian;

		/*
		 * add the size for encoding
		 */
		*buflen = sizeof (nvs_header_t);
		break;

	default:
		return (ENOTSUP);
	}

	/*
	 * Create an nvstream with proper encoding method
	 */
	switch (encoding) {
	case NV_ENCODE_NATIVE:
		/*
		 * check endianness, in case we are unpacking
		 * from a file
		 */
		if (nvl_endian != host_endian)
			return (ENOTSUP);
		err = nvs_native(&nvs, nvl, buf, buflen);
		break;
	case NV_ENCODE_XDR:
		err = nvs_xdr(&nvs, nvl, buf, buflen);
		break;
	default:
		err = ENOTSUP;
		break;
	}

	return (err);
}

int
nvlist_size(nvlist_t *nvl, size_t *size, int encoding)
{
	return (nvlist_common(nvl, NULL, size, encoding, NVS_OP_GETSIZE));
}

/*
 * Pack nvlist into contiguous memory
 */
/*ARGSUSED1*/
int
nvlist_pack(nvlist_t *nvl, char **bufp, size_t *buflen, int encoding,
    int kmflag)
{
#if defined(_KERNEL) && !defined(_BOOT)
	return (nvlist_xpack(nvl, bufp, buflen, encoding,
	    (kmflag == KM_SLEEP ? nv_alloc_sleep : nv_alloc_nosleep)));
#else
	return (nvlist_xpack(nvl, bufp, buflen, encoding, nv_alloc_nosleep));
#endif
}

int
nvlist_xpack(nvlist_t *nvl, char **bufp, size_t *buflen, int encoding,
    nv_alloc_t *nva)
{
	nvpriv_t nvpriv;
	size_t alloc_size;
	char *buf;
	int err;

	if (nva == NULL || nvl == NULL || bufp == NULL || buflen == NULL)
		return (EINVAL);

	if (*bufp != NULL)
		return (nvlist_common(nvl, *bufp, buflen, encoding,
		    NVS_OP_ENCODE));

	/*
	 * Here is a difficult situation:
	 * 1. The nvlist has fixed allocator properties.
	 *    All other nvlist routines (like nvlist_add_*, ...) use
	 *    these properties.
	 * 2. When using nvlist_pack() the user can specify their own
	 *    allocator properties (e.g. by using KM_NOSLEEP).
	 *
	 * We use the user specified properties (2). A clearer solution
	 * will be to remove the kmflag from nvlist_pack(), but we will
	 * not change the interface.
	 */
	nv_priv_init(&nvpriv, nva, 0);

	if ((err = nvlist_size(nvl, &alloc_size, encoding)))
		return (err);

	if ((buf = nv_mem_zalloc(&nvpriv, alloc_size)) == NULL)
		return (ENOMEM);

	if ((err = nvlist_common(nvl, buf, &alloc_size, encoding,
	    NVS_OP_ENCODE)) != 0) {
		nv_mem_free(&nvpriv, buf, alloc_size);
	} else {
		*buflen = alloc_size;
		*bufp = buf;
	}

	return (err);
}

/*
 * Unpack buf into an nvlist_t
 */
/*ARGSUSED1*/
int
nvlist_unpack(char *buf, size_t buflen, nvlist_t **nvlp, int kmflag)
{
#if defined(_KERNEL) && !defined(_BOOT)
	return (nvlist_xunpack(buf, buflen, nvlp,
	    (kmflag == KM_SLEEP ? nv_alloc_sleep : nv_alloc_nosleep)));
#else
	return (nvlist_xunpack(buf, buflen, nvlp, nv_alloc_nosleep));
#endif
}

int
nvlist_xunpack(char *buf, size_t buflen, nvlist_t **nvlp, nv_alloc_t *nva)
{
	nvlist_t *nvl;
	int err;

	if (nvlp == NULL)
		return (EINVAL);

	if ((err = nvlist_xalloc(&nvl, 0, nva)) != 0)
		return (err);

	if ((err = nvlist_common(nvl, buf, &buflen, 0, NVS_OP_DECODE)) != 0)
		nvlist_free(nvl);
	else
		*nvlp = nvl;

	return (err);
}

/*
 * Native encoding functions
 */
typedef struct {
	/*
	 * This structure is used when decoding a packed nvpair in
	 * the native format.  n_base points to a buffer containing the
	 * packed nvpair.  n_end is a pointer to the end of the buffer.
	 * (n_end actually points to the first byte past the end of the
	 * buffer.)  n_curr is a pointer that lies between n_base and n_end.
	 * It points to the current data that we are decoding.
	 * The amount of data left in the buffer is equal to n_end - n_curr.
	 * n_flag is used to recognize a packed embedded list.
	 */
	caddr_t n_base;
	caddr_t n_end;
	caddr_t n_curr;
	uint_t  n_flag;
} nvs_native_t;

static int
nvs_native_create(nvstream_t *nvs, nvs_native_t *native, char *buf,
    size_t buflen)
{
	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE:
	case NVS_OP_DECODE:
		nvs->nvs_private = native;
		native->n_curr = native->n_base = buf;
		native->n_end = buf + buflen;
		native->n_flag = 0;
		return (0);

	case NVS_OP_GETSIZE:
		nvs->nvs_private = native;
		native->n_curr = native->n_base = native->n_end = NULL;
		native->n_flag = 0;
		return (0);
	default:
		return (EINVAL);
	}
}

/*ARGSUSED*/
static void
nvs_native_destroy(nvstream_t *nvs)
{
}

static int
native_cp(nvstream_t *nvs, void *buf, size_t size)
{
	nvs_native_t *native = (nvs_native_t *)nvs->nvs_private;

	if (native->n_curr + size > native->n_end)
		return (EFAULT);

	/*
	 * The bcopy() below eliminates alignment requirement
	 * on the buffer (stream) and is preferred over direct access.
	 */
	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE:
		bcopy(buf, native->n_curr, size);
		break;
	case NVS_OP_DECODE:
		bcopy(native->n_curr, buf, size);
		break;
	default:
		return (EINVAL);
	}

	native->n_curr += size;
	return (0);
}

/*
 * operate on nvlist_t header
 */
static int
nvs_native_nvlist(nvstream_t *nvs, nvlist_t *nvl, size_t *size)
{
	nvs_native_t *native = nvs->nvs_private;

	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE:
	case NVS_OP_DECODE:
		if (native->n_flag)
			return (0);	/* packed embedded list */

		native->n_flag = 1;

		/* copy version and nvflag of the nvlist_t */
		if (native_cp(nvs, &nvl->nvl_version, sizeof (int32_t)) != 0 ||
		    native_cp(nvs, &nvl->nvl_nvflag, sizeof (int32_t)) != 0)
			return (EFAULT);

		return (0);

	case NVS_OP_GETSIZE:
		/*
		 * if calculate for packed embedded list
		 * 	4 for end of the embedded list
		 * else
		 * 	2 * sizeof (int32_t) for nvl_version and nvl_nvflag
		 * 	and 4 for end of the entire list
		 */
		if (native->n_flag) {
			*size += 4;
		} else {
			native->n_flag = 1;
			*size += 2 * sizeof (int32_t) + 4;
		}

		return (0);

	default:
		return (EINVAL);
	}
}

static int
nvs_native_nvl_fini(nvstream_t *nvs)
{
	if (nvs->nvs_op == NVS_OP_ENCODE) {
		nvs_native_t *native = (nvs_native_t *)nvs->nvs_private;
		/*
		 * Add 4 zero bytes at end of nvlist. They are used
		 * for end detection by the decode routine.
		 */
		if (native->n_curr + sizeof (int) > native->n_end)
			return (EFAULT);

		bzero(native->n_curr, sizeof (int));
		native->n_curr += sizeof (int);
	}

	return (0);
}

static int
nvpair_native_embedded(nvstream_t *nvs, nvpair_t *nvp)
{
	if (nvs->nvs_op == NVS_OP_ENCODE) {
		nvs_native_t *native = (nvs_native_t *)nvs->nvs_private;
		char *packed = (void *)
		    (native->n_curr - nvp->nvp_size + NVP_VALOFF(nvp));
		/*
		 * Null out the pointer that is meaningless in the packed
		 * structure. The address may not be aligned, so we have
		 * to use bzero.
		 */
		bzero(packed + offsetof(nvlist_t, nvl_priv),
		    sizeof(((nvlist_t *)NULL)->nvl_priv));
	}

	return (nvs_embedded(nvs, EMBEDDED_NVL(nvp)));
}

static int
nvpair_native_embedded_array(nvstream_t *nvs, nvpair_t *nvp)
{
	if (nvs->nvs_op == NVS_OP_ENCODE) {
		nvs_native_t *native = (nvs_native_t *)nvs->nvs_private;
		char *value = native->n_curr - nvp->nvp_size + NVP_VALOFF(nvp);
		size_t len = NVP_NELEM(nvp) * sizeof (uint64_t);
		int i;
		/*
		 * Null out pointers that are meaningless in the packed
		 * structure. The addresses may not be aligned, so we have
		 * to use bzero.
		 */
		bzero(value, len);

		value += len;
		for (i = 0; i < NVP_NELEM(nvp); i++) {
			/*
			 * Null out the pointer that is meaningless in the
			 * packed structure. The address may not be aligned,
			 * so we have to use bzero.
			 */
			bzero(value + offsetof(nvlist_t, nvl_priv),
			    sizeof(((nvlist_t *)NULL)->nvl_priv));
			value += sizeof(nvlist_t);
		}
	}

	return (nvs_embedded_nvl_array(nvs, nvp, NULL));
}

static void
nvpair_native_string_array(nvstream_t *nvs, nvpair_t *nvp)
{
	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE: {
		nvs_native_t *native = (nvs_native_t *)nvs->nvs_private;
		uint64_t *strp = (void *)
		    (native->n_curr - nvp->nvp_size + NVP_VALOFF(nvp));
		/*
		 * Null out pointers that are meaningless in the packed
		 * structure. The addresses may not be aligned, so we have
		 * to use bzero.
		 */
		bzero(strp, NVP_NELEM(nvp) * sizeof (uint64_t));
		break;
	}
	case NVS_OP_DECODE: {
		char **strp = (void *)NVP_VALUE(nvp);
		char *buf = ((char *)strp + NVP_NELEM(nvp) * sizeof (uint64_t));
		int i;

		for (i = 0; i < NVP_NELEM(nvp); i++) {
			strp[i] = buf;
			buf += strlen(buf) + 1;
		}
		break;
	}
	}
}

static int
nvs_native_nvp_op(nvstream_t *nvs, nvpair_t *nvp)
{
	data_type_t type;
	int value_sz;
	int ret = 0;

	/*
	 * We do the initial bcopy of the data before we look at
	 * the nvpair type, because when we're decoding, we won't
	 * have the correct values for the pair until we do the bcopy.
	 */
	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE:
	case NVS_OP_DECODE:
		if (native_cp(nvs, nvp, nvp->nvp_size) != 0)
			return (EFAULT);
		break;
	default:
		return (EINVAL);
	}

	/* verify nvp_name_sz, check the name string length */
	if (i_validate_nvpair_name(nvp) != 0)
		return (EFAULT);

	type = NVP_TYPE(nvp);

	/*
	 * Verify type and nelem and get the value size.
	 * In case of data types DATA_TYPE_STRING and DATA_TYPE_STRING_ARRAY
	 * is the size of the string(s) excluded.
	 */
	if ((value_sz = i_get_value_size(type, NULL, NVP_NELEM(nvp))) < 0)
		return (EFAULT);

	if (NVP_SIZE_CALC(nvp->nvp_name_sz, value_sz) > nvp->nvp_size)
		return (EFAULT);

	switch (type) {
	case DATA_TYPE_NVLIST:
		ret = nvpair_native_embedded(nvs, nvp);
		break;
	case DATA_TYPE_NVLIST_ARRAY:
		ret = nvpair_native_embedded_array(nvs, nvp);
		break;
	case DATA_TYPE_STRING_ARRAY:
		nvpair_native_string_array(nvs, nvp);
		break;
	default:
		break;
	}

	return (ret);
}

static int
nvs_native_nvp_size(nvstream_t *nvs, nvpair_t *nvp, size_t *size)
{
	uint64_t nvp_sz = nvp->nvp_size;

	switch (NVP_TYPE(nvp)) {
	case DATA_TYPE_NVLIST: {
		size_t nvsize = 0;

		if (nvs_operation(nvs, EMBEDDED_NVL(nvp), &nvsize) != 0)
			return (EINVAL);

		nvp_sz += nvsize;
		break;
	}
	case DATA_TYPE_NVLIST_ARRAY: {
		size_t nvsize;

		if (nvs_embedded_nvl_array(nvs, nvp, &nvsize) != 0)
			return (EINVAL);

		nvp_sz += nvsize;
		break;
	}
	default:
		break;
	}

	if (nvp_sz > INT32_MAX)
		return (EINVAL);

	*size = nvp_sz;

	return (0);
}

static int
nvs_native_nvpair(nvstream_t *nvs, nvpair_t *nvp, size_t *size)
{
	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE:
		return (nvs_native_nvp_op(nvs, nvp));

	case NVS_OP_DECODE: {
		nvs_native_t *native = (nvs_native_t *)nvs->nvs_private;
		int32_t decode_len;

		/* try to read the size value from the stream */
		if (native->n_curr + sizeof (int32_t) > native->n_end)
			return (EFAULT);
		bcopy(native->n_curr, &decode_len, sizeof (int32_t));

		/* sanity check the size value */
		if (decode_len < 0 ||
		    decode_len > native->n_end - native->n_curr)
			return (EFAULT);

		*size = decode_len;

		/*
		 * If at the end of the stream then move the cursor
		 * forward, otherwise nvpair_native_op() will read
		 * the entire nvpair at the same cursor position.
		 */
		if (*size == 0)
			native->n_curr += sizeof (int32_t);
		break;
	}

	default:
		return (EINVAL);
	}

	return (0);
}

static const nvs_ops_t nvs_native_ops = {
	nvs_native_nvlist,
	nvs_native_nvpair,
	nvs_native_nvp_op,
	nvs_native_nvp_size,
	nvs_native_nvl_fini
};

static int
nvs_native(nvstream_t *nvs, nvlist_t *nvl, char *buf, size_t *buflen)
{
	nvs_native_t native;
	int err;

	nvs->nvs_ops = &nvs_native_ops;

	if ((err = nvs_native_create(nvs, &native, buf + sizeof (nvs_header_t),
	    *buflen - sizeof (nvs_header_t))) != 0)
		return (err);

	err = nvs_operation(nvs, nvl, buflen);

	nvs_native_destroy(nvs);

	return (err);
}

/*
 * XDR encoding functions
 *
 * An xdr packed nvlist is encoded as:
 *
 *  - encoding methode and host endian (4 bytes)
 *  - nvl_version (4 bytes)
 *  - nvl_nvflag (4 bytes)
 *
 *  - encoded nvpairs, the format of one xdr encoded nvpair is:
 *	- encoded size of the nvpair (4 bytes)
 *	- decoded size of the nvpair (4 bytes)
 *	- name string, (4 + sizeof(NV_ALIGN4(string))
 *	  a string is coded as size (4 bytes) and data
 *	- data type (4 bytes)
 *	- number of elements in the nvpair (4 bytes)
 *	- data
 *
 *  - 2 zero's for end of the entire list (8 bytes)
 */
static int
nvs_xdr_create(nvstream_t *nvs, XDR *xdr, char *buf, size_t buflen)
{
	/* xdr data must be 4 byte aligned */
	if ((ulong_t)buf % 4 != 0)
		return (EFAULT);

	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE:
		xdrmem_create(xdr, buf, (uint_t)buflen, XDR_ENCODE);
		nvs->nvs_private = xdr;
		return (0);
	case NVS_OP_DECODE:
		xdrmem_create(xdr, buf, (uint_t)buflen, XDR_DECODE);
		nvs->nvs_private = xdr;
		return (0);
	case NVS_OP_GETSIZE:
		nvs->nvs_private = NULL;
		return (0);
	default:
		return (EINVAL);
	}
}

static void
nvs_xdr_destroy(nvstream_t *nvs)
{
	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE:
	case NVS_OP_DECODE:
		xdr_destroy((XDR *)nvs->nvs_private);
		break;
	default:
		break;
	}
}

static int
nvs_xdr_nvlist(nvstream_t *nvs, nvlist_t *nvl, size_t *size)
{
	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE:
	case NVS_OP_DECODE: {
		XDR 	*xdr = nvs->nvs_private;

		if (!xdr_int(xdr, &nvl->nvl_version) ||
		    !xdr_u_int(xdr, &nvl->nvl_nvflag))
			return (EFAULT);
		break;
	}
	case NVS_OP_GETSIZE: {
		/*
		 * 2 * 4 for nvl_version + nvl_nvflag
		 * and 8 for end of the entire list
		 */
		*size += 2 * 4 + 8;
		break;
	}
	default:
		return (EINVAL);
	}
	return (0);
}

static int
nvs_xdr_nvl_fini(nvstream_t *nvs)
{
	if (nvs->nvs_op == NVS_OP_ENCODE) {
		XDR *xdr = nvs->nvs_private;
		int zero = 0;

		if (!xdr_int(xdr, &zero) || !xdr_int(xdr, &zero))
			return (EFAULT);
	}

	return (0);
}

/*
 * The format of xdr encoded nvpair is:
 * encode_size, decode_size, name string, data type, nelem, data
 */
static int
nvs_xdr_nvp_op(nvstream_t *nvs, nvpair_t *nvp)
{
	data_type_t type;
	char	*buf;
	char	*buf_end = (char *)nvp + nvp->nvp_size;
	int	value_sz;
	uint_t	nelem, buflen;
	bool_t	ret = FALSE;
	XDR	*xdr = nvs->nvs_private;

	ASSERT(xdr != NULL && nvp != NULL);

	/* name string */
	if ((buf = NVP_NAME(nvp)) >= buf_end)
		return (EFAULT);
	buflen = buf_end - buf;

	if (!xdr_string(xdr, &buf, buflen - 1))
		return (EFAULT);
	nvp->nvp_name_sz = strlen(buf) + 1;

	/* type and nelem */
	if (!xdr_int(xdr, (int *)&nvp->nvp_type) ||
	    !xdr_int(xdr, &nvp->nvp_value_elem))
		return (EFAULT);

	type = NVP_TYPE(nvp);
	nelem = nvp->nvp_value_elem;

	/*
	 * Verify type and nelem and get the value size.
	 * In case of data types DATA_TYPE_STRING and DATA_TYPE_STRING_ARRAY
	 * is the size of the string(s) excluded.
	 */
	if ((value_sz = i_get_value_size(type, NULL, nelem)) < 0)
		return (EFAULT);

	/* if there is no data to extract then return */
	if (nelem == 0)
		return (0);

	/* value */
	if ((buf = NVP_VALUE(nvp)) >= buf_end)
		return (EFAULT);
	buflen = buf_end - buf;

	if (buflen < value_sz)
		return (EFAULT);

	switch (type) {
	case DATA_TYPE_NVLIST:
		if (nvs_embedded(nvs, (void *)buf) == 0)
			return (0);
		break;

	case DATA_TYPE_NVLIST_ARRAY:
		if (nvs_embedded_nvl_array(nvs, nvp, NULL) == 0)
			return (0);
		break;

	case DATA_TYPE_BOOLEAN:
		ret = TRUE;
		break;

	case DATA_TYPE_BYTE:
	case DATA_TYPE_INT8:
	case DATA_TYPE_UINT8:
		ret = xdr_char(xdr, buf);
		break;

	case DATA_TYPE_INT16:
		ret = xdr_short(xdr, (void *)buf);
		break;

	case DATA_TYPE_UINT16:
		ret = xdr_u_short(xdr, (void *)buf);
		break;

	case DATA_TYPE_BOOLEAN_VALUE:
	case DATA_TYPE_INT32:
		ret = xdr_int(xdr, (void *)buf);
		break;

	case DATA_TYPE_UINT32:
		ret = xdr_u_int(xdr, (void *)buf);
		break;

	case DATA_TYPE_INT64:
		ret = xdr_longlong_t(xdr, (void *)buf);
		break;

	case DATA_TYPE_UINT64:
		ret = xdr_u_longlong_t(xdr, (void *)buf);
		break;

	case DATA_TYPE_HRTIME:
		/*
		 * NOTE: must expose the definition of hrtime_t here
		 */
		ret = xdr_longlong_t(xdr, (void *)buf);
		break;
#if !defined(_KERNEL)
	case DATA_TYPE_DOUBLE:
		ret = xdr_double(xdr, (void *)buf);
		break;
#endif
	case DATA_TYPE_STRING:
		ret = xdr_string(xdr, &buf, buflen - 1);
		break;

	case DATA_TYPE_BYTE_ARRAY:
		ret = xdr_opaque(xdr, buf, nelem);
		break;

	case DATA_TYPE_INT8_ARRAY:
	case DATA_TYPE_UINT8_ARRAY:
		ret = xdr_array(xdr, &buf, &nelem, buflen, sizeof (int8_t),
		    (xdrproc_t)xdr_char);
		break;

	case DATA_TYPE_INT16_ARRAY:
		ret = xdr_array(xdr, &buf, &nelem, buflen / sizeof (int16_t),
		    sizeof (int16_t), (xdrproc_t)xdr_short);
		break;

	case DATA_TYPE_UINT16_ARRAY:
		ret = xdr_array(xdr, &buf, &nelem, buflen / sizeof (uint16_t),
		    sizeof (uint16_t), (xdrproc_t)xdr_u_short);
		break;

	case DATA_TYPE_BOOLEAN_ARRAY:
	case DATA_TYPE_INT32_ARRAY:
		ret = xdr_array(xdr, &buf, &nelem, buflen / sizeof (int32_t),
		    sizeof (int32_t), (xdrproc_t)xdr_int);
		break;

	case DATA_TYPE_UINT32_ARRAY:
		ret = xdr_array(xdr, &buf, &nelem, buflen / sizeof (uint32_t),
		    sizeof (uint32_t), (xdrproc_t)xdr_u_int);
		break;

	case DATA_TYPE_INT64_ARRAY:
		ret = xdr_array(xdr, &buf, &nelem, buflen / sizeof (int64_t),
		    sizeof (int64_t), (xdrproc_t)xdr_longlong_t);
		break;

	case DATA_TYPE_UINT64_ARRAY:
		ret = xdr_array(xdr, &buf, &nelem, buflen / sizeof (uint64_t),
		    sizeof (uint64_t), (xdrproc_t)xdr_u_longlong_t);
		break;

	case DATA_TYPE_STRING_ARRAY: {
		size_t len = nelem * sizeof (uint64_t);
		char **strp = (void *)buf;
		int i;

		if (nvs->nvs_op == NVS_OP_DECODE)
			bzero(buf, len);	/* don't trust packed data */

		for (i = 0; i < nelem; i++) {
			if (buflen <= len)
				return (EFAULT);

			buf += len;
			buflen -= len;

			if (xdr_string(xdr, &buf, buflen - 1) != TRUE)
				return (EFAULT);

			if (nvs->nvs_op == NVS_OP_DECODE)
				strp[i] = buf;
			len = strlen(buf) + 1;
		}
		ret = TRUE;
		break;
	}
	default:
		break;
	}

	return (ret == TRUE ? 0 : EFAULT);
}

static int
nvs_xdr_nvp_size(nvstream_t *nvs, nvpair_t *nvp, size_t *size)
{
	data_type_t type = NVP_TYPE(nvp);
	/*
	 * encode_size + decode_size + name string size + data type + nelem
	 * where name string size = 4 + NV_ALIGN4(strlen(NVP_NAME(nvp)))
	 */
	uint64_t nvp_sz = 4 + 4 + 4 + NV_ALIGN4(strlen(NVP_NAME(nvp))) + 4 + 4;

	switch (type) {
	case DATA_TYPE_BOOLEAN:
		break;

	case DATA_TYPE_BOOLEAN_VALUE:
	case DATA_TYPE_BYTE:
	case DATA_TYPE_INT8:
	case DATA_TYPE_UINT8:
	case DATA_TYPE_INT16:
	case DATA_TYPE_UINT16:
	case DATA_TYPE_INT32:
	case DATA_TYPE_UINT32:
		nvp_sz += 4;	/* 4 is the minimum xdr unit */
		break;

	case DATA_TYPE_INT64:
	case DATA_TYPE_UINT64:
	case DATA_TYPE_HRTIME:
#if !defined(_KERNEL)
	case DATA_TYPE_DOUBLE:
#endif
		nvp_sz += 8;
		break;

	case DATA_TYPE_STRING:
		nvp_sz += 4 + NV_ALIGN4(strlen((char *)NVP_VALUE(nvp)));
		break;

	case DATA_TYPE_BYTE_ARRAY:
		nvp_sz += NV_ALIGN4(NVP_NELEM(nvp));
		break;

	case DATA_TYPE_BOOLEAN_ARRAY:
	case DATA_TYPE_INT8_ARRAY:
	case DATA_TYPE_UINT8_ARRAY:
	case DATA_TYPE_INT16_ARRAY:
	case DATA_TYPE_UINT16_ARRAY:
	case DATA_TYPE_INT32_ARRAY:
	case DATA_TYPE_UINT32_ARRAY:
		nvp_sz += 4 + 4 * (uint64_t)NVP_NELEM(nvp);
		break;

	case DATA_TYPE_INT64_ARRAY:
	case DATA_TYPE_UINT64_ARRAY:
		nvp_sz += 4 + 8 * (uint64_t)NVP_NELEM(nvp);
		break;

	case DATA_TYPE_STRING_ARRAY: {
		int i;
		char **strs = (void *)NVP_VALUE(nvp);

		for (i = 0; i < NVP_NELEM(nvp); i++)
			nvp_sz += 4 + NV_ALIGN4(strlen(strs[i]));

		break;
	}

	case DATA_TYPE_NVLIST:
	case DATA_TYPE_NVLIST_ARRAY: {
		size_t nvsize = 0;
		int old_nvs_op = nvs->nvs_op;
		int err;

		nvs->nvs_op = NVS_OP_GETSIZE;
		if (type == DATA_TYPE_NVLIST)
			err = nvs_operation(nvs, EMBEDDED_NVL(nvp), &nvsize);
		else
			err = nvs_embedded_nvl_array(nvs, nvp, &nvsize);
		nvs->nvs_op = old_nvs_op;

		if (err != 0)
			return (EINVAL);

		nvp_sz += nvsize;
		break;
	}

	default:
		return (EINVAL);
	}

	if (nvp_sz > INT32_MAX)
		return (EINVAL);

	*size = nvp_sz;

	return (0);
}


/*
 * The NVS_XDR_MAX_LEN macro takes a packed xdr buffer of size x and estimates
 * the largest nvpair that could be encoded in the buffer.
 *
 * See comments above nvpair_xdr_op() for the format of xdr encoding.
 * The size of a xdr packed nvpair without any data is 5 words.
 *
 * Using the size of the data directly as an estimate would be ok
 * in all cases except one.  If the data type is of DATA_TYPE_STRING_ARRAY
 * then the actual nvpair has space for an array of pointers to index
 * the strings.  These pointers are not encoded into the packed xdr buffer.
 *
 * If the data is of type DATA_TYPE_STRING_ARRAY and all the strings are
 * of length 0, then each string is endcoded in xdr format as a single word.
 * Therefore when expanded to an nvpair there will be 2.25 word used for
 * each string.  (a int64_t allocated for pointer usage, and a single char
 * for the null termination.)
 *
 * This is the calculation performed by the NVS_XDR_MAX_LEN macro.
 */
#define	NVS_XDR_HDR_LEN		((size_t)(5 * 4))
#define	NVS_XDR_DATA_LEN(y)	(((size_t)(y) <= NVS_XDR_HDR_LEN) ? \
					0 : ((size_t)(y) - NVS_XDR_HDR_LEN))
#define	NVS_XDR_MAX_LEN(x)	(NVP_SIZE_CALC(1, 0) + \
					(NVS_XDR_DATA_LEN(x) * 2) + \
					NV_ALIGN4((NVS_XDR_DATA_LEN(x) / 4)))

static int
nvs_xdr_nvpair(nvstream_t *nvs, nvpair_t *nvp, size_t *size)
{
	XDR 	*xdr = nvs->nvs_private;
	int32_t	encode_len, decode_len;

	switch (nvs->nvs_op) {
	case NVS_OP_ENCODE: {
		size_t nvsize;

		if (nvs_xdr_nvp_size(nvs, nvp, &nvsize) != 0)
			return (EFAULT);

		decode_len = nvp->nvp_size;
		encode_len = nvsize;
		if (!xdr_int(xdr, &encode_len) || !xdr_int(xdr, &decode_len))
			return (EFAULT);

		return (nvs_xdr_nvp_op(nvs, nvp));
	}
	case NVS_OP_DECODE: {
		struct xdr_bytesrec bytesrec;

		/* get the encode and decode size */
		if (!xdr_int(xdr, &encode_len) || !xdr_int(xdr, &decode_len))
			return (EFAULT);
		*size = decode_len;

		/* are we at the end of the stream? */
		if (*size == 0)
			return (0);

		/* sanity check the size parameter */
		if (!xdr_control(xdr, XDR_GET_BYTES_AVAIL, &bytesrec))
			return (EFAULT);

		if (*size > NVS_XDR_MAX_LEN(bytesrec.xc_num_avail))
			return (EFAULT);
		break;
	}

	default:
		return (EINVAL);
	}
	return (0);
}

static const struct nvs_ops nvs_xdr_ops = {
	nvs_xdr_nvlist,
	nvs_xdr_nvpair,
	nvs_xdr_nvp_op,
	nvs_xdr_nvp_size,
	nvs_xdr_nvl_fini
};

static int
nvs_xdr(nvstream_t *nvs, nvlist_t *nvl, char *buf, size_t *buflen)
{
	XDR xdr;
	int err;

	nvs->nvs_ops = &nvs_xdr_ops;

	if ((err = nvs_xdr_create(nvs, &xdr, buf + sizeof (nvs_header_t),
	    *buflen - sizeof (nvs_header_t))) != 0)
		return (err);

	err = nvs_operation(nvs, nvl, buflen);

	nvs_xdr_destroy(nvs);

	return (err);
}
