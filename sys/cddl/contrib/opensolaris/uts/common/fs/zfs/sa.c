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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Portions Copyright 2011 iXsystems, Inc
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 * Copyright (c) 2014 Spectra Logic Corporation, All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

#include <sys/zfs_context.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/sunddi.h>
#include <sys/sa_impl.h>
#include <sys/dnode.h>
#include <sys/errno.h>
#include <sys/zfs_context.h>

/*
 * ZFS System attributes:
 *
 * A generic mechanism to allow for arbitrary attributes
 * to be stored in a dnode.  The data will be stored in the bonus buffer of
 * the dnode and if necessary a special "spill" block will be used to handle
 * overflow situations.  The spill block will be sized to fit the data
 * from 512 - 128K.  When a spill block is used the BP (blkptr_t) for the
 * spill block is stored at the end of the current bonus buffer.  Any
 * attributes that would be in the way of the blkptr_t will be relocated
 * into the spill block.
 *
 * Attribute registration:
 *
 * Stored persistently on a per dataset basis
 * a mapping between attribute "string" names and their actual attribute
 * numeric values, length, and byteswap function.  The names are only used
 * during registration.  All  attributes are known by their unique attribute
 * id value.  If an attribute can have a variable size then the value
 * 0 will be used to indicate this.
 *
 * Attribute Layout:
 *
 * Attribute layouts are a way to compactly store multiple attributes, but
 * without taking the overhead associated with managing each attribute
 * individually.  Since you will typically have the same set of attributes
 * stored in the same order a single table will be used to represent that
 * layout.  The ZPL for example will usually have only about 10 different
 * layouts (regular files, device files, symlinks,
 * regular files + scanstamp, files/dir with extended attributes, and then
 * you have the possibility of all of those minus ACL, because it would
 * be kicked out into the spill block)
 *
 * Layouts are simply an array of the attributes and their
 * ordering i.e. [0, 1, 4, 5, 2]
 *
 * Each distinct layout is given a unique layout number and that is whats
 * stored in the header at the beginning of the SA data buffer.
 *
 * A layout only covers a single dbuf (bonus or spill).  If a set of
 * attributes is split up between the bonus buffer and a spill buffer then
 * two different layouts will be used.  This allows us to byteswap the
 * spill without looking at the bonus buffer and keeps the on disk format of
 * the bonus and spill buffer the same.
 *
 * Adding a single attribute will cause the entire set of attributes to
 * be rewritten and could result in a new layout number being constructed
 * as part of the rewrite if no such layout exists for the new set of
 * attribues.  The new attribute will be appended to the end of the already
 * existing attributes.
 *
 * Both the attribute registration and attribute layout information are
 * stored in normal ZAP attributes.  Their should be a small number of
 * known layouts and the set of attributes is assumed to typically be quite
 * small.
 *
 * The registered attributes and layout "table" information is maintained
 * in core and a special "sa_os_t" is attached to the objset_t.
 *
 * A special interface is provided to allow for quickly applying
 * a large set of attributes at once.  sa_replace_all_by_template() is
 * used to set an array of attributes.  This is used by the ZPL when
 * creating a brand new file.  The template that is passed into the function
 * specifies the attribute, size for variable length attributes, location of
 * data and special "data locator" function if the data isn't in a contiguous
 * location.
 *
 * Byteswap implications:
 *
 * Since the SA attributes are not entirely self describing we can't do
 * the normal byteswap processing.  The special ZAP layout attribute and
 * attribute registration attributes define the byteswap function and the
 * size of the attributes, unless it is variable sized.
 * The normal ZFS byteswapping infrastructure assumes you don't need
 * to read any objects in order to do the necessary byteswapping.  Whereas
 * SA attributes can only be properly byteswapped if the dataset is opened
 * and the layout/attribute ZAP attributes are available.  Because of this
 * the SA attributes will be byteswapped when they are first accessed by
 * the SA code that will read the SA data.
 */

typedef void (sa_iterfunc_t)(void *hdr, void *addr, sa_attr_type_t,
    uint16_t length, int length_idx, boolean_t, void *userp);

static int sa_build_index(sa_handle_t *hdl, sa_buf_type_t buftype);
static void sa_idx_tab_hold(objset_t *os, sa_idx_tab_t *idx_tab);
static sa_idx_tab_t *sa_find_idx_tab(objset_t *os, dmu_object_type_t bonustype,
    sa_hdr_phys_t *hdr);
static void sa_idx_tab_rele(objset_t *os, void *arg);
static void sa_copy_data(sa_data_locator_t *func, void *start, void *target,
    int buflen);
static int sa_modify_attrs(sa_handle_t *hdl, sa_attr_type_t newattr,
    sa_data_op_t action, sa_data_locator_t *locator, void *datastart,
    uint16_t buflen, dmu_tx_t *tx);

arc_byteswap_func_t *sa_bswap_table[] = {
	byteswap_uint64_array,
	byteswap_uint32_array,
	byteswap_uint16_array,
	byteswap_uint8_array,
	zfs_acl_byteswap,
};

#define	SA_COPY_DATA(f, s, t, l) \
	{ \
		if (f == NULL) { \
			if (l == 8) { \
				*(uint64_t *)t = *(uint64_t *)s; \
			} else if (l == 16) { \
				*(uint64_t *)t = *(uint64_t *)s; \
				*(uint64_t *)((uintptr_t)t + 8) = \
				    *(uint64_t *)((uintptr_t)s + 8); \
			} else { \
				bcopy(s, t, l); \
			} \
		} else \
			sa_copy_data(f, s, t, l); \
	}

/*
 * This table is fixed and cannot be changed.  Its purpose is to
 * allow the SA code to work with both old/new ZPL file systems.
 * It contains the list of legacy attributes.  These attributes aren't
 * stored in the "attribute" registry zap objects, since older ZPL file systems
 * won't have the registry.  Only objsets of type ZFS_TYPE_FILESYSTEM will
 * use this static table.
 */
sa_attr_reg_t sa_legacy_attrs[] = {
	{"ZPL_ATIME", sizeof (uint64_t) * 2, SA_UINT64_ARRAY, 0},
	{"ZPL_MTIME", sizeof (uint64_t) * 2, SA_UINT64_ARRAY, 1},
	{"ZPL_CTIME", sizeof (uint64_t) * 2, SA_UINT64_ARRAY, 2},
	{"ZPL_CRTIME", sizeof (uint64_t) * 2, SA_UINT64_ARRAY, 3},
	{"ZPL_GEN", sizeof (uint64_t), SA_UINT64_ARRAY, 4},
	{"ZPL_MODE", sizeof (uint64_t), SA_UINT64_ARRAY, 5},
	{"ZPL_SIZE", sizeof (uint64_t), SA_UINT64_ARRAY, 6},
	{"ZPL_PARENT", sizeof (uint64_t), SA_UINT64_ARRAY, 7},
	{"ZPL_LINKS", sizeof (uint64_t), SA_UINT64_ARRAY, 8},
	{"ZPL_XATTR", sizeof (uint64_t), SA_UINT64_ARRAY, 9},
	{"ZPL_RDEV", sizeof (uint64_t), SA_UINT64_ARRAY, 10},
	{"ZPL_FLAGS", sizeof (uint64_t), SA_UINT64_ARRAY, 11},
	{"ZPL_UID", sizeof (uint64_t), SA_UINT64_ARRAY, 12},
	{"ZPL_GID", sizeof (uint64_t), SA_UINT64_ARRAY, 13},
	{"ZPL_PAD", sizeof (uint64_t) * 4, SA_UINT64_ARRAY, 14},
	{"ZPL_ZNODE_ACL", 88, SA_UINT8_ARRAY, 15},
};

/*
 * This is only used for objects of type DMU_OT_ZNODE
 */
sa_attr_type_t sa_legacy_zpl_layout[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

/*
 * Special dummy layout used for buffers with no attributes.
 */
sa_attr_type_t sa_dummy_zpl_layout[] = { 0 };

static int sa_legacy_attr_count = 16;
static kmem_cache_t *sa_cache = NULL;

/*ARGSUSED*/
static int
sa_cache_constructor(void *buf, void *unused, int kmflag)
{
	sa_handle_t *hdl = buf;

	mutex_init(&hdl->sa_lock, NULL, MUTEX_DEFAULT, NULL);
	return (0);
}

/*ARGSUSED*/
static void
sa_cache_destructor(void *buf, void *unused)
{
	sa_handle_t *hdl = buf;
	mutex_destroy(&hdl->sa_lock);
}

void
sa_cache_init(void)
{
	sa_cache = kmem_cache_create("sa_cache",
	    sizeof (sa_handle_t), 0, sa_cache_constructor,
	    sa_cache_destructor, NULL, NULL, NULL, 0);
}

void
sa_cache_fini(void)
{
	if (sa_cache)
		kmem_cache_destroy(sa_cache);
}

static int
layout_num_compare(const void *arg1, const void *arg2)
{
	const sa_lot_t *node1 = (const sa_lot_t *)arg1;
	const sa_lot_t *node2 = (const sa_lot_t *)arg2;

	return (AVL_CMP(node1->lot_num, node2->lot_num));
}

static int
layout_hash_compare(const void *arg1, const void *arg2)
{
	const sa_lot_t *node1 = (const sa_lot_t *)arg1;
	const sa_lot_t *node2 = (const sa_lot_t *)arg2;

	int cmp = AVL_CMP(node1->lot_hash, node2->lot_hash);
	if (likely(cmp))
		return (cmp);

	return (AVL_CMP(node1->lot_instance, node2->lot_instance));
}

boolean_t
sa_layout_equal(sa_lot_t *tbf, sa_attr_type_t *attrs, int count)
{
	int i;

	if (count != tbf->lot_attr_count)
		return (1);

	for (i = 0; i != count; i++) {
		if (attrs[i] != tbf->lot_attrs[i])
			return (1);
	}
	return (0);
}

#define	SA_ATTR_HASH(attr) (zfs_crc64_table[(-1ULL ^ attr) & 0xFF])

static uint64_t
sa_layout_info_hash(sa_attr_type_t *attrs, int attr_count)
{
	int i;
	uint64_t crc = -1ULL;

	for (i = 0; i != attr_count; i++)
		crc ^= SA_ATTR_HASH(attrs[i]);

	return (crc);
}

static int
sa_get_spill(sa_handle_t *hdl)
{
	int rc;
	if (hdl->sa_spill == NULL) {
		if ((rc = dmu_spill_hold_existing(hdl->sa_bonus, NULL,
		    &hdl->sa_spill)) == 0)
			VERIFY(0 == sa_build_index(hdl, SA_SPILL));
	} else {
		rc = 0;
	}

	return (rc);
}

/*
 * Main attribute lookup/update function
 * returns 0 for success or non zero for failures
 *
 * Operates on bulk array, first failure will abort further processing
 */
int
sa_attr_op(sa_handle_t *hdl, sa_bulk_attr_t *bulk, int count,
    sa_data_op_t data_op, dmu_tx_t *tx)
{
	sa_os_t *sa = hdl->sa_os->os_sa;
	int i;
	int error = 0;
	sa_buf_type_t buftypes;

	buftypes = 0;

	ASSERT(count > 0);
	for (i = 0; i != count; i++) {
		ASSERT(bulk[i].sa_attr <= hdl->sa_os->os_sa->sa_num_attrs);

		bulk[i].sa_addr = NULL;
		/* First check the bonus buffer */

		if (hdl->sa_bonus_tab && TOC_ATTR_PRESENT(
		    hdl->sa_bonus_tab->sa_idx_tab[bulk[i].sa_attr])) {
			SA_ATTR_INFO(sa, hdl->sa_bonus_tab,
			    SA_GET_HDR(hdl, SA_BONUS),
			    bulk[i].sa_attr, bulk[i], SA_BONUS, hdl);
			if (tx && !(buftypes & SA_BONUS)) {
				dmu_buf_will_dirty(hdl->sa_bonus, tx);
				buftypes |= SA_BONUS;
			}
		}
		if (bulk[i].sa_addr == NULL &&
		    ((error = sa_get_spill(hdl)) == 0)) {
			if (TOC_ATTR_PRESENT(
			    hdl->sa_spill_tab->sa_idx_tab[bulk[i].sa_attr])) {
				SA_ATTR_INFO(sa, hdl->sa_spill_tab,
				    SA_GET_HDR(hdl, SA_SPILL),
				    bulk[i].sa_attr, bulk[i], SA_SPILL, hdl);
				if (tx && !(buftypes & SA_SPILL) &&
				    bulk[i].sa_size == bulk[i].sa_length) {
					dmu_buf_will_dirty(hdl->sa_spill, tx);
					buftypes |= SA_SPILL;
				}
			}
		}
		if (error && error != ENOENT) {
			return ((error == ECKSUM) ? EIO : error);
		}

		switch (data_op) {
		case SA_LOOKUP:
			if (bulk[i].sa_addr == NULL)
				return (SET_ERROR(ENOENT));
			if (bulk[i].sa_data) {
				SA_COPY_DATA(bulk[i].sa_data_func,
				    bulk[i].sa_addr, bulk[i].sa_data,
				    bulk[i].sa_size);
			}
			continue;

		case SA_UPDATE:
			/* existing rewrite of attr */
			if (bulk[i].sa_addr &&
			    bulk[i].sa_size == bulk[i].sa_length) {
				SA_COPY_DATA(bulk[i].sa_data_func,
				    bulk[i].sa_data, bulk[i].sa_addr,
				    bulk[i].sa_length);
				continue;
			} else if (bulk[i].sa_addr) { /* attr size change */
				error = sa_modify_attrs(hdl, bulk[i].sa_attr,
				    SA_REPLACE, bulk[i].sa_data_func,
				    bulk[i].sa_data, bulk[i].sa_length, tx);
			} else { /* adding new attribute */
				error = sa_modify_attrs(hdl, bulk[i].sa_attr,
				    SA_ADD, bulk[i].sa_data_func,
				    bulk[i].sa_data, bulk[i].sa_length, tx);
			}
			if (error)
				return (error);
			break;
		}
	}
	return (error);
}

static sa_lot_t *
sa_add_layout_entry(objset_t *os, sa_attr_type_t *attrs, int attr_count,
    uint64_t lot_num, uint64_t hash, boolean_t zapadd, dmu_tx_t *tx)
{
	sa_os_t *sa = os->os_sa;
	sa_lot_t *tb, *findtb;
	int i;
	avl_index_t loc;

	ASSERT(MUTEX_HELD(&sa->sa_lock));
	tb = kmem_zalloc(sizeof (sa_lot_t), KM_SLEEP);
	tb->lot_attr_count = attr_count;
	tb->lot_attrs = kmem_alloc(sizeof (sa_attr_type_t) * attr_count,
	    KM_SLEEP);
	bcopy(attrs, tb->lot_attrs, sizeof (sa_attr_type_t) * attr_count);
	tb->lot_num = lot_num;
	tb->lot_hash = hash;
	tb->lot_instance = 0;

	if (zapadd) {
		char attr_name[8];

		if (sa->sa_layout_attr_obj == 0) {
			sa->sa_layout_attr_obj = zap_create_link(os,
			    DMU_OT_SA_ATTR_LAYOUTS,
			    sa->sa_master_obj, SA_LAYOUTS, tx);
		}

		(void) snprintf(attr_name, sizeof (attr_name),
		    "%d", (int)lot_num);
		VERIFY(0 == zap_update(os, os->os_sa->sa_layout_attr_obj,
		    attr_name, 2, attr_count, attrs, tx));
	}

	list_create(&tb->lot_idx_tab, sizeof (sa_idx_tab_t),
	    offsetof(sa_idx_tab_t, sa_next));

	for (i = 0; i != attr_count; i++) {
		if (sa->sa_attr_table[tb->lot_attrs[i]].sa_length == 0)
			tb->lot_var_sizes++;
	}

	avl_add(&sa->sa_layout_num_tree, tb);

	/* verify we don't have a hash collision */
	if ((findtb = avl_find(&sa->sa_layout_hash_tree, tb, &loc)) != NULL) {
		for (; findtb && findtb->lot_hash == hash;
		    findtb = AVL_NEXT(&sa->sa_layout_hash_tree, findtb)) {
			if (findtb->lot_instance != tb->lot_instance)
				break;
			tb->lot_instance++;
		}
	}
	avl_add(&sa->sa_layout_hash_tree, tb);
	return (tb);
}

static void
sa_find_layout(objset_t *os, uint64_t hash, sa_attr_type_t *attrs,
    int count, dmu_tx_t *tx, sa_lot_t **lot)
{
	sa_lot_t *tb, tbsearch;
	avl_index_t loc;
	sa_os_t *sa = os->os_sa;
	boolean_t found = B_FALSE;

	mutex_enter(&sa->sa_lock);
	tbsearch.lot_hash = hash;
	tbsearch.lot_instance = 0;
	tb = avl_find(&sa->sa_layout_hash_tree, &tbsearch, &loc);
	if (tb) {
		for (; tb && tb->lot_hash == hash;
		    tb = AVL_NEXT(&sa->sa_layout_hash_tree, tb)) {
			if (sa_layout_equal(tb, attrs, count) == 0) {
				found = B_TRUE;
				break;
			}
		}
	}
	if (!found) {
		tb = sa_add_layout_entry(os, attrs, count,
		    avl_numnodes(&sa->sa_layout_num_tree), hash, B_TRUE, tx);
	}
	mutex_exit(&sa->sa_lock);
	*lot = tb;
}

static int
sa_resize_spill(sa_handle_t *hdl, uint32_t size, dmu_tx_t *tx)
{
	int error;
	uint32_t blocksize;

	if (size == 0) {
		blocksize = SPA_MINBLOCKSIZE;
	} else if (size > SPA_OLD_MAXBLOCKSIZE) {
		ASSERT(0);
		return (SET_ERROR(EFBIG));
	} else {
		blocksize = P2ROUNDUP_TYPED(size, SPA_MINBLOCKSIZE, uint32_t);
	}

	error = dbuf_spill_set_blksz(hdl->sa_spill, blocksize, tx);
	ASSERT(error == 0);
	return (error);
}

static void
sa_copy_data(sa_data_locator_t *func, void *datastart, void *target, int buflen)
{
	if (func == NULL) {
		bcopy(datastart, target, buflen);
	} else {
		boolean_t start;
		int bytes;
		void *dataptr;
		void *saptr = target;
		uint32_t length;

		start = B_TRUE;
		bytes = 0;
		while (bytes < buflen) {
			func(&dataptr, &length, buflen, start, datastart);
			bcopy(dataptr, saptr, length);
			saptr = (void *)((caddr_t)saptr + length);
			bytes += length;
			start = B_FALSE;
		}
	}
}

/*
 * Determine several different sizes
 * first the sa header size
 * the number of bytes to be stored
 * if spill would occur the index in the attribute array is returned
 *
 * the boolean will_spill will be set when spilling is necessary.  It
 * is only set when the buftype is SA_BONUS
 */
static int
sa_find_sizes(sa_os_t *sa, sa_bulk_attr_t *attr_desc, int attr_count,
    dmu_buf_t *db, sa_buf_type_t buftype, int full_space, int *index,
    int *total, boolean_t *will_spill)
{
	int var_size = 0;
	int i;
	int hdrsize;
	int extra_hdrsize;

	if (buftype == SA_BONUS && sa->sa_force_spill) {
		*total = 0;
		*index = 0;
		*will_spill = B_TRUE;
		return (0);
	}

	*index = -1;
	*total = 0;
	*will_spill = B_FALSE;

	extra_hdrsize = 0;
	hdrsize = (SA_BONUSTYPE_FROM_DB(db) == DMU_OT_ZNODE) ? 0 :
	    sizeof (sa_hdr_phys_t);

	ASSERT(IS_P2ALIGNED(full_space, 8));

	for (i = 0; i != attr_count; i++) {
		boolean_t is_var_sz;

		*total = P2ROUNDUP(*total, 8);
		*total += attr_desc[i].sa_length;
		if (*will_spill)
			continue;

		is_var_sz = (SA_REGISTERED_LEN(sa, attr_desc[i].sa_attr) == 0);
		if (is_var_sz) {
			var_size++;
		}

		if (is_var_sz && var_size > 1) {
			/*
			 * Don't worry that the spill block might overflow.
			 * It will be resized if needed in sa_build_layouts().
			 */
			if (buftype == SA_SPILL ||
			    P2ROUNDUP(hdrsize + sizeof (uint16_t), 8) +
			    *total < full_space) {
				/*
				 * Account for header space used by array of
				 * optional sizes of variable-length attributes.
				 * Record the extra header size in case this
				 * increase needs to be reversed due to
				 * spill-over.
				 */
				hdrsize += sizeof (uint16_t);
				if (*index != -1)
					extra_hdrsize += sizeof (uint16_t);
			} else {
				ASSERT(buftype == SA_BONUS);
				if (*index == -1)
					*index = i;
				*will_spill = B_TRUE;
				continue;
			}
		}

		/*
		 * find index of where spill *could* occur.
		 * Then continue to count of remainder attribute
		 * space.  The sum is used later for sizing bonus
		 * and spill buffer.
		 */
		if (buftype == SA_BONUS && *index == -1 &&
		    (*total + P2ROUNDUP(hdrsize, 8)) >
		    (full_space - sizeof (blkptr_t))) {
			*index = i;
		}

		if ((*total + P2ROUNDUP(hdrsize, 8)) > full_space &&
		    buftype == SA_BONUS)
			*will_spill = B_TRUE;
	}

	if (*will_spill)
		hdrsize -= extra_hdrsize;

	hdrsize = P2ROUNDUP(hdrsize, 8);
	return (hdrsize);
}

#define	BUF_SPACE_NEEDED(total, header) (total + header)

/*
 * Find layout that corresponds to ordering of attributes
 * If not found a new layout number is created and added to
 * persistent layout tables.
 */
static int
sa_build_layouts(sa_handle_t *hdl, sa_bulk_attr_t *attr_desc, int attr_count,
    dmu_tx_t *tx)
{
	sa_os_t *sa = hdl->sa_os->os_sa;
	uint64_t hash;
	sa_buf_type_t buftype;
	sa_hdr_phys_t *sahdr;
	void *data_start;
	int buf_space;
	sa_attr_type_t *attrs, *attrs_start;
	int i, lot_count;
	int dnodesize;
	int hdrsize;
	int spillhdrsize = 0;
	int used;
	dmu_object_type_t bonustype;
	sa_lot_t *lot;
	int len_idx;
	int spill_used;
	int bonuslen;
	boolean_t spilling;

	dmu_buf_will_dirty(hdl->sa_bonus, tx);
	bonustype = SA_BONUSTYPE_FROM_DB(hdl->sa_bonus);
	dmu_object_dnsize_from_db(hdl->sa_bonus, &dnodesize);
	bonuslen = DN_BONUS_SIZE(dnodesize);
	
	/* first determine bonus header size and sum of all attributes */
	hdrsize = sa_find_sizes(sa, attr_desc, attr_count, hdl->sa_bonus,
	    SA_BONUS, bonuslen, &i, &used, &spilling);

	if (used > SPA_OLD_MAXBLOCKSIZE)
		return (SET_ERROR(EFBIG));

	VERIFY(0 == dmu_set_bonus(hdl->sa_bonus, spilling ?
	    MIN(bonuslen - sizeof (blkptr_t), used + hdrsize) :
	    used + hdrsize, tx));

	ASSERT((bonustype == DMU_OT_ZNODE && spilling == 0) ||
	    bonustype == DMU_OT_SA);

	/* setup and size spill buffer when needed */
	if (spilling) {
		boolean_t dummy;

		if (hdl->sa_spill == NULL) {
			VERIFY(dmu_spill_hold_by_bonus(hdl->sa_bonus, NULL,
			    &hdl->sa_spill) == 0);
		}
		dmu_buf_will_dirty(hdl->sa_spill, tx);

		spillhdrsize = sa_find_sizes(sa, &attr_desc[i],
		    attr_count - i, hdl->sa_spill, SA_SPILL,
		    hdl->sa_spill->db_size, &i, &spill_used, &dummy);

		if (spill_used > SPA_OLD_MAXBLOCKSIZE)
			return (SET_ERROR(EFBIG));

		buf_space = hdl->sa_spill->db_size - spillhdrsize;
		if (BUF_SPACE_NEEDED(spill_used, spillhdrsize) >
		    hdl->sa_spill->db_size)
			VERIFY(0 == sa_resize_spill(hdl,
			    BUF_SPACE_NEEDED(spill_used, spillhdrsize), tx));
	}

	/* setup starting pointers to lay down data */
	data_start = (void *)((uintptr_t)hdl->sa_bonus->db_data + hdrsize);
	sahdr = (sa_hdr_phys_t *)hdl->sa_bonus->db_data;
	buftype = SA_BONUS;

	if (spilling)
		buf_space = (sa->sa_force_spill) ?
		    0 : SA_BLKPTR_SPACE - hdrsize;
	else
		buf_space = hdl->sa_bonus->db_size - hdrsize;

	attrs_start = attrs = kmem_alloc(sizeof (sa_attr_type_t) * attr_count,
	    KM_SLEEP);
	lot_count = 0;

	for (i = 0, len_idx = 0, hash = -1ULL; i != attr_count; i++) {
		uint16_t length;

		ASSERT(IS_P2ALIGNED(data_start, 8));
		ASSERT(IS_P2ALIGNED(buf_space, 8));
		attrs[i] = attr_desc[i].sa_attr;
		length = SA_REGISTERED_LEN(sa, attrs[i]);
		if (length == 0)
			length = attr_desc[i].sa_length;
		else
			VERIFY(length == attr_desc[i].sa_length);

		if (buf_space < length) {  /* switch to spill buffer */
			VERIFY(spilling);
			VERIFY(bonustype == DMU_OT_SA);
			if (buftype == SA_BONUS && !sa->sa_force_spill) {
				sa_find_layout(hdl->sa_os, hash, attrs_start,
				    lot_count, tx, &lot);
				SA_SET_HDR(sahdr, lot->lot_num, hdrsize);
			}

			buftype = SA_SPILL;
			hash = -1ULL;
			len_idx = 0;

			sahdr = (sa_hdr_phys_t *)hdl->sa_spill->db_data;
			sahdr->sa_magic = SA_MAGIC;
			data_start = (void *)((uintptr_t)sahdr +
			    spillhdrsize);
			attrs_start = &attrs[i];
			buf_space = hdl->sa_spill->db_size - spillhdrsize;
			lot_count = 0;
		}
		hash ^= SA_ATTR_HASH(attrs[i]);
		attr_desc[i].sa_addr = data_start;
		attr_desc[i].sa_size = length;
		SA_COPY_DATA(attr_desc[i].sa_data_func, attr_desc[i].sa_data,
		    data_start, length);
		if (sa->sa_attr_table[attrs[i]].sa_length == 0) {
			sahdr->sa_lengths[len_idx++] = length;
		}
		VERIFY((uintptr_t)data_start % 8 == 0);
		data_start = (void *)P2ROUNDUP(((uintptr_t)data_start +
		    length), 8);
		buf_space -= P2ROUNDUP(length, 8);
		lot_count++;
	}

	sa_find_layout(hdl->sa_os, hash, attrs_start, lot_count, tx, &lot);

	/*
	 * Verify that old znodes always have layout number 0.
	 * Must be DMU_OT_SA for arbitrary layouts
	 */
	VERIFY((bonustype == DMU_OT_ZNODE && lot->lot_num == 0) ||
	    (bonustype == DMU_OT_SA && lot->lot_num > 1));

	if (bonustype == DMU_OT_SA) {
		SA_SET_HDR(sahdr, lot->lot_num,
		    buftype == SA_BONUS ? hdrsize : spillhdrsize);
	}

	kmem_free(attrs, sizeof (sa_attr_type_t) * attr_count);
	if (hdl->sa_bonus_tab) {
		sa_idx_tab_rele(hdl->sa_os, hdl->sa_bonus_tab);
		hdl->sa_bonus_tab = NULL;
	}
	if (!sa->sa_force_spill)
		VERIFY(0 == sa_build_index(hdl, SA_BONUS));
	if (hdl->sa_spill) {
		sa_idx_tab_rele(hdl->sa_os, hdl->sa_spill_tab);
		if (!spilling) {
			/*
			 * remove spill block that is no longer needed.
			 */
			dmu_buf_rele(hdl->sa_spill, NULL);
			hdl->sa_spill = NULL;
			hdl->sa_spill_tab = NULL;
			VERIFY(0 == dmu_rm_spill(hdl->sa_os,
			    sa_handle_object(hdl), tx));
		} else {
			VERIFY(0 == sa_build_index(hdl, SA_SPILL));
		}
	}

	return (0);
}

static void
sa_free_attr_table(sa_os_t *sa)
{
	int i;

	if (sa->sa_attr_table == NULL)
		return;

	for (i = 0; i != sa->sa_num_attrs; i++) {
		if (sa->sa_attr_table[i].sa_name)
			kmem_free(sa->sa_attr_table[i].sa_name,
			    strlen(sa->sa_attr_table[i].sa_name) + 1);
	}

	kmem_free(sa->sa_attr_table,
	    sizeof (sa_attr_table_t) * sa->sa_num_attrs);

	sa->sa_attr_table = NULL;
}

static int
sa_attr_table_setup(objset_t *os, sa_attr_reg_t *reg_attrs, int count)
{
	sa_os_t *sa = os->os_sa;
	uint64_t sa_attr_count = 0;
	uint64_t sa_reg_count = 0;
	int error = 0;
	uint64_t attr_value;
	sa_attr_table_t *tb;
	zap_cursor_t zc;
	zap_attribute_t za;
	int registered_count = 0;
	int i;
	dmu_objset_type_t ostype = dmu_objset_type(os);

	sa->sa_user_table =
	    kmem_zalloc(count * sizeof (sa_attr_type_t), KM_SLEEP);
	sa->sa_user_table_sz = count * sizeof (sa_attr_type_t);

	if (sa->sa_reg_attr_obj != 0) {
		error = zap_count(os, sa->sa_reg_attr_obj,
		    &sa_attr_count);

		/*
		 * Make sure we retrieved a count and that it isn't zero
		 */
		if (error || (error == 0 && sa_attr_count == 0)) {
			if (error == 0)
				error = SET_ERROR(EINVAL);
			goto bail;
		}
		sa_reg_count = sa_attr_count;
	}

	if (ostype == DMU_OST_ZFS && sa_attr_count == 0)
		sa_attr_count += sa_legacy_attr_count;

	/* Allocate attribute numbers for attributes that aren't registered */
	for (i = 0; i != count; i++) {
		boolean_t found = B_FALSE;
		int j;

		if (ostype == DMU_OST_ZFS) {
			for (j = 0; j != sa_legacy_attr_count; j++) {
				if (strcmp(reg_attrs[i].sa_name,
				    sa_legacy_attrs[j].sa_name) == 0) {
					sa->sa_user_table[i] =
					    sa_legacy_attrs[j].sa_attr;
					found = B_TRUE;
				}
			}
		}
		if (found)
			continue;

		if (sa->sa_reg_attr_obj)
			error = zap_lookup(os, sa->sa_reg_attr_obj,
			    reg_attrs[i].sa_name, 8, 1, &attr_value);
		else
			error = SET_ERROR(ENOENT);
		switch (error) {
		case ENOENT:
			sa->sa_user_table[i] = (sa_attr_type_t)sa_attr_count;
			sa_attr_count++;
			break;
		case 0:
			sa->sa_user_table[i] = ATTR_NUM(attr_value);
			break;
		default:
			goto bail;
		}
	}

	sa->sa_num_attrs = sa_attr_count;
	tb = sa->sa_attr_table =
	    kmem_zalloc(sizeof (sa_attr_table_t) * sa_attr_count, KM_SLEEP);

	/*
	 * Attribute table is constructed from requested attribute list,
	 * previously foreign registered attributes, and also the legacy
	 * ZPL set of attributes.
	 */

	if (sa->sa_reg_attr_obj) {
		for (zap_cursor_init(&zc, os, sa->sa_reg_attr_obj);
		    (error = zap_cursor_retrieve(&zc, &za)) == 0;
		    zap_cursor_advance(&zc)) {
			uint64_t value;
			value  = za.za_first_integer;

			registered_count++;
			tb[ATTR_NUM(value)].sa_attr = ATTR_NUM(value);
			tb[ATTR_NUM(value)].sa_length = ATTR_LENGTH(value);
			tb[ATTR_NUM(value)].sa_byteswap = ATTR_BSWAP(value);
			tb[ATTR_NUM(value)].sa_registered = B_TRUE;

			if (tb[ATTR_NUM(value)].sa_name) {
				continue;
			}
			tb[ATTR_NUM(value)].sa_name =
			    kmem_zalloc(strlen(za.za_name) +1, KM_SLEEP);
			(void) strlcpy(tb[ATTR_NUM(value)].sa_name, za.za_name,
			    strlen(za.za_name) +1);
		}
		zap_cursor_fini(&zc);
		/*
		 * Make sure we processed the correct number of registered
		 * attributes
		 */
		if (registered_count != sa_reg_count) {
			ASSERT(error != 0);
			goto bail;
		}

	}

	if (ostype == DMU_OST_ZFS) {
		for (i = 0; i != sa_legacy_attr_count; i++) {
			if (tb[i].sa_name)
				continue;
			tb[i].sa_attr = sa_legacy_attrs[i].sa_attr;
			tb[i].sa_length = sa_legacy_attrs[i].sa_length;
			tb[i].sa_byteswap = sa_legacy_attrs[i].sa_byteswap;
			tb[i].sa_registered = B_FALSE;
			tb[i].sa_name =
			    kmem_zalloc(strlen(sa_legacy_attrs[i].sa_name) +1,
			    KM_SLEEP);
			(void) strlcpy(tb[i].sa_name,
			    sa_legacy_attrs[i].sa_name,
			    strlen(sa_legacy_attrs[i].sa_name) + 1);
		}
	}

	for (i = 0; i != count; i++) {
		sa_attr_type_t attr_id;

		attr_id = sa->sa_user_table[i];
		if (tb[attr_id].sa_name)
			continue;

		tb[attr_id].sa_length = reg_attrs[i].sa_length;
		tb[attr_id].sa_byteswap = reg_attrs[i].sa_byteswap;
		tb[attr_id].sa_attr = attr_id;
		tb[attr_id].sa_name =
		    kmem_zalloc(strlen(reg_attrs[i].sa_name) + 1, KM_SLEEP);
		(void) strlcpy(tb[attr_id].sa_name, reg_attrs[i].sa_name,
		    strlen(reg_attrs[i].sa_name) + 1);
	}

	sa->sa_need_attr_registration =
	    (sa_attr_count != registered_count);

	return (0);
bail:
	kmem_free(sa->sa_user_table, count * sizeof (sa_attr_type_t));
	sa->sa_user_table = NULL;
	sa_free_attr_table(sa);
	return ((error != 0) ? error : EINVAL);
}

int
sa_setup(objset_t *os, uint64_t sa_obj, sa_attr_reg_t *reg_attrs, int count,
    sa_attr_type_t **user_table)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	sa_os_t *sa;
	dmu_objset_type_t ostype = dmu_objset_type(os);
	sa_attr_type_t *tb;
	int error;

	mutex_enter(&os->os_user_ptr_lock);
	if (os->os_sa) {
		mutex_enter(&os->os_sa->sa_lock);
		mutex_exit(&os->os_user_ptr_lock);
		tb = os->os_sa->sa_user_table;
		mutex_exit(&os->os_sa->sa_lock);
		*user_table = tb;
		return (0);
	}

	sa = kmem_zalloc(sizeof (sa_os_t), KM_SLEEP);
	mutex_init(&sa->sa_lock, NULL, MUTEX_DEFAULT, NULL);
	sa->sa_master_obj = sa_obj;

	os->os_sa = sa;
	mutex_enter(&sa->sa_lock);
	mutex_exit(&os->os_user_ptr_lock);
	avl_create(&sa->sa_layout_num_tree, layout_num_compare,
	    sizeof (sa_lot_t), offsetof(sa_lot_t, lot_num_node));
	avl_create(&sa->sa_layout_hash_tree, layout_hash_compare,
	    sizeof (sa_lot_t), offsetof(sa_lot_t, lot_hash_node));

	if (sa_obj) {
		error = zap_lookup(os, sa_obj, SA_LAYOUTS,
		    8, 1, &sa->sa_layout_attr_obj);
		if (error != 0 && error != ENOENT)
			goto fail;
		error = zap_lookup(os, sa_obj, SA_REGISTRY,
		    8, 1, &sa->sa_reg_attr_obj);
		if (error != 0 && error != ENOENT)
			goto fail;
	}

	if ((error = sa_attr_table_setup(os, reg_attrs, count)) != 0)
		goto fail;

	if (sa->sa_layout_attr_obj != 0) {
		uint64_t layout_count;

		error = zap_count(os, sa->sa_layout_attr_obj,
		    &layout_count);

		/*
		 * Layout number count should be > 0
		 */
		if (error || (error == 0 && layout_count == 0)) {
			if (error == 0)
				error = SET_ERROR(EINVAL);
			goto fail;
		}

		for (zap_cursor_init(&zc, os, sa->sa_layout_attr_obj);
		    (error = zap_cursor_retrieve(&zc, &za)) == 0;
		    zap_cursor_advance(&zc)) {
			sa_attr_type_t *lot_attrs;
			uint64_t lot_num;

			lot_attrs = kmem_zalloc(sizeof (sa_attr_type_t) *
			    za.za_num_integers, KM_SLEEP);

			if ((error = (zap_lookup(os, sa->sa_layout_attr_obj,
			    za.za_name, 2, za.za_num_integers,
			    lot_attrs))) != 0) {
				kmem_free(lot_attrs, sizeof (sa_attr_type_t) *
				    za.za_num_integers);
				break;
			}
			VERIFY(ddi_strtoull(za.za_name, NULL, 10,
			    (unsigned long long *)&lot_num) == 0);

			(void) sa_add_layout_entry(os, lot_attrs,
			    za.za_num_integers, lot_num,
			    sa_layout_info_hash(lot_attrs,
			    za.za_num_integers), B_FALSE, NULL);
			kmem_free(lot_attrs, sizeof (sa_attr_type_t) *
			    za.za_num_integers);
		}
		zap_cursor_fini(&zc);

		/*
		 * Make sure layout count matches number of entries added
		 * to AVL tree
		 */
		if (avl_numnodes(&sa->sa_layout_num_tree) != layout_count) {
			ASSERT(error != 0);
			goto fail;
		}
	}

	/* Add special layout number for old ZNODES */
	if (ostype == DMU_OST_ZFS) {
		(void) sa_add_layout_entry(os, sa_legacy_zpl_layout,
		    sa_legacy_attr_count, 0,
		    sa_layout_info_hash(sa_legacy_zpl_layout,
		    sa_legacy_attr_count), B_FALSE, NULL);

		(void) sa_add_layout_entry(os, sa_dummy_zpl_layout, 0, 1,
		    0, B_FALSE, NULL);
	}
	*user_table = os->os_sa->sa_user_table;
	mutex_exit(&sa->sa_lock);
	return (0);
fail:
	os->os_sa = NULL;
	sa_free_attr_table(sa);
	if (sa->sa_user_table)
		kmem_free(sa->sa_user_table, sa->sa_user_table_sz);
	mutex_exit(&sa->sa_lock);
	avl_destroy(&sa->sa_layout_hash_tree);
	avl_destroy(&sa->sa_layout_num_tree);
	mutex_destroy(&sa->sa_lock);
	kmem_free(sa, sizeof (sa_os_t));
	return ((error == ECKSUM) ? EIO : error);
}

void
sa_tear_down(objset_t *os)
{
	sa_os_t *sa = os->os_sa;
	sa_lot_t *layout;
	void *cookie;

	kmem_free(sa->sa_user_table, sa->sa_user_table_sz);

	/* Free up attr table */

	sa_free_attr_table(sa);

	cookie = NULL;
	while (layout = avl_destroy_nodes(&sa->sa_layout_hash_tree, &cookie)) {
		sa_idx_tab_t *tab;
		while (tab = list_head(&layout->lot_idx_tab)) {
			ASSERT(refcount_count(&tab->sa_refcount));
			sa_idx_tab_rele(os, tab);
		}
	}

	cookie = NULL;
	while (layout = avl_destroy_nodes(&sa->sa_layout_num_tree, &cookie)) {
		kmem_free(layout->lot_attrs,
		    sizeof (sa_attr_type_t) * layout->lot_attr_count);
		kmem_free(layout, sizeof (sa_lot_t));
	}

	avl_destroy(&sa->sa_layout_hash_tree);
	avl_destroy(&sa->sa_layout_num_tree);
	mutex_destroy(&sa->sa_lock);

	kmem_free(sa, sizeof (sa_os_t));
	os->os_sa = NULL;
}

void
sa_build_idx_tab(void *hdr, void *attr_addr, sa_attr_type_t attr,
    uint16_t length, int length_idx, boolean_t var_length, void *userp)
{
	sa_idx_tab_t *idx_tab = userp;

	if (var_length) {
		ASSERT(idx_tab->sa_variable_lengths);
		idx_tab->sa_variable_lengths[length_idx] = length;
	}
	TOC_ATTR_ENCODE(idx_tab->sa_idx_tab[attr], length_idx,
	    (uint32_t)((uintptr_t)attr_addr - (uintptr_t)hdr));
}

static void
sa_attr_iter(objset_t *os, sa_hdr_phys_t *hdr, dmu_object_type_t type,
    sa_iterfunc_t func, sa_lot_t *tab, void *userp)
{
	void *data_start;
	sa_lot_t *tb = tab;
	sa_lot_t search;
	avl_index_t loc;
	sa_os_t *sa = os->os_sa;
	int i;
	uint16_t *length_start = NULL;
	uint8_t length_idx = 0;

	if (tab == NULL) {
		search.lot_num = SA_LAYOUT_NUM(hdr, type);
		tb = avl_find(&sa->sa_layout_num_tree, &search, &loc);
		ASSERT(tb);
	}

	if (IS_SA_BONUSTYPE(type)) {
		data_start = (void *)P2ROUNDUP(((uintptr_t)hdr +
		    offsetof(sa_hdr_phys_t, sa_lengths) +
		    (sizeof (uint16_t) * tb->lot_var_sizes)), 8);
		length_start = hdr->sa_lengths;
	} else {
		data_start = hdr;
	}

	for (i = 0; i != tb->lot_attr_count; i++) {
		int attr_length, reg_length;
		uint8_t idx_len;

		reg_length = sa->sa_attr_table[tb->lot_attrs[i]].sa_length;
		if (reg_length) {
			attr_length = reg_length;
			idx_len = 0;
		} else {
			attr_length = length_start[length_idx];
			idx_len = length_idx++;
		}

		func(hdr, data_start, tb->lot_attrs[i], attr_length,
		    idx_len, reg_length == 0 ? B_TRUE : B_FALSE, userp);

		data_start = (void *)P2ROUNDUP(((uintptr_t)data_start +
		    attr_length), 8);
	}
}

/*ARGSUSED*/
void
sa_byteswap_cb(void *hdr, void *attr_addr, sa_attr_type_t attr,
    uint16_t length, int length_idx, boolean_t variable_length, void *userp)
{
	sa_handle_t *hdl = userp;
	sa_os_t *sa = hdl->sa_os->os_sa;

	sa_bswap_table[sa->sa_attr_table[attr].sa_byteswap](attr_addr, length);
}

void
sa_byteswap(sa_handle_t *hdl, sa_buf_type_t buftype)
{
	sa_hdr_phys_t *sa_hdr_phys = SA_GET_HDR(hdl, buftype);
	dmu_buf_impl_t *db;
	sa_os_t *sa = hdl->sa_os->os_sa;
	int num_lengths = 1;
	int i;

	ASSERT(MUTEX_HELD(&sa->sa_lock));
	if (sa_hdr_phys->sa_magic == SA_MAGIC)
		return;

	db = SA_GET_DB(hdl, buftype);

	if (buftype == SA_SPILL) {
		arc_release(db->db_buf, NULL);
		arc_buf_thaw(db->db_buf);
	}

	sa_hdr_phys->sa_magic = BSWAP_32(sa_hdr_phys->sa_magic);
	sa_hdr_phys->sa_layout_info = BSWAP_16(sa_hdr_phys->sa_layout_info);

	/*
	 * Determine number of variable lenghts in header
	 * The standard 8 byte header has one for free and a
	 * 16 byte header would have 4 + 1;
	 */
	if (SA_HDR_SIZE(sa_hdr_phys) > 8)
		num_lengths += (SA_HDR_SIZE(sa_hdr_phys) - 8) >> 1;
	for (i = 0; i != num_lengths; i++)
		sa_hdr_phys->sa_lengths[i] =
		    BSWAP_16(sa_hdr_phys->sa_lengths[i]);

	sa_attr_iter(hdl->sa_os, sa_hdr_phys, DMU_OT_SA,
	    sa_byteswap_cb, NULL, hdl);

	if (buftype == SA_SPILL)
		arc_buf_freeze(((dmu_buf_impl_t *)hdl->sa_spill)->db_buf);
}

static int
sa_build_index(sa_handle_t *hdl, sa_buf_type_t buftype)
{
	sa_hdr_phys_t *sa_hdr_phys;
	dmu_buf_impl_t *db = SA_GET_DB(hdl, buftype);
	dmu_object_type_t bonustype = SA_BONUSTYPE_FROM_DB(db);
	sa_os_t *sa = hdl->sa_os->os_sa;
	sa_idx_tab_t *idx_tab;

	sa_hdr_phys = SA_GET_HDR(hdl, buftype);

	mutex_enter(&sa->sa_lock);

	/* Do we need to byteswap? */

	/* only check if not old znode */
	if (IS_SA_BONUSTYPE(bonustype) && sa_hdr_phys->sa_magic != SA_MAGIC &&
	    sa_hdr_phys->sa_magic != 0) {
		VERIFY(BSWAP_32(sa_hdr_phys->sa_magic) == SA_MAGIC);
		sa_byteswap(hdl, buftype);
	}

	idx_tab = sa_find_idx_tab(hdl->sa_os, bonustype, sa_hdr_phys);

	if (buftype == SA_BONUS)
		hdl->sa_bonus_tab = idx_tab;
	else
		hdl->sa_spill_tab = idx_tab;

	mutex_exit(&sa->sa_lock);
	return (0);
}

/*ARGSUSED*/
static void
sa_evict_sync(void *dbu)
{
	panic("evicting sa dbuf\n");
}

static void
sa_idx_tab_rele(objset_t *os, void *arg)
{
	sa_os_t *sa = os->os_sa;
	sa_idx_tab_t *idx_tab = arg;

	if (idx_tab == NULL)
		return;

	mutex_enter(&sa->sa_lock);
	if (refcount_remove(&idx_tab->sa_refcount, NULL) == 0) {
		list_remove(&idx_tab->sa_layout->lot_idx_tab, idx_tab);
		if (idx_tab->sa_variable_lengths)
			kmem_free(idx_tab->sa_variable_lengths,
			    sizeof (uint16_t) *
			    idx_tab->sa_layout->lot_var_sizes);
		refcount_destroy(&idx_tab->sa_refcount);
		kmem_free(idx_tab->sa_idx_tab,
		    sizeof (uint32_t) * sa->sa_num_attrs);
		kmem_free(idx_tab, sizeof (sa_idx_tab_t));
	}
	mutex_exit(&sa->sa_lock);
}

static void
sa_idx_tab_hold(objset_t *os, sa_idx_tab_t *idx_tab)
{
	sa_os_t *sa = os->os_sa;

	ASSERT(MUTEX_HELD(&sa->sa_lock));
	(void) refcount_add(&idx_tab->sa_refcount, NULL);
}

void
sa_handle_destroy(sa_handle_t *hdl)
{
	dmu_buf_t *db = hdl->sa_bonus;

	mutex_enter(&hdl->sa_lock);
	(void) dmu_buf_remove_user(db, &hdl->sa_dbu);

	if (hdl->sa_bonus_tab)
		sa_idx_tab_rele(hdl->sa_os, hdl->sa_bonus_tab);

	if (hdl->sa_spill_tab)
		sa_idx_tab_rele(hdl->sa_os, hdl->sa_spill_tab);

	dmu_buf_rele(hdl->sa_bonus, NULL);

	if (hdl->sa_spill)
		dmu_buf_rele((dmu_buf_t *)hdl->sa_spill, NULL);
	mutex_exit(&hdl->sa_lock);

	kmem_cache_free(sa_cache, hdl);
}

int
sa_handle_get_from_db(objset_t *os, dmu_buf_t *db, void *userp,
    sa_handle_type_t hdl_type, sa_handle_t **handlepp)
{
	int error = 0;
	dmu_object_info_t doi;
	sa_handle_t *handle = NULL;

#ifdef ZFS_DEBUG
	dmu_object_info_from_db(db, &doi);
	ASSERT(doi.doi_bonus_type == DMU_OT_SA ||
	    doi.doi_bonus_type == DMU_OT_ZNODE);
#endif
	/* find handle, if it exists */
	/* if one doesn't exist then create a new one, and initialize it */

	if (hdl_type == SA_HDL_SHARED)
		handle = dmu_buf_get_user(db);

	if (handle == NULL) {
		sa_handle_t *winner = NULL;

		handle = kmem_cache_alloc(sa_cache, KM_SLEEP);
		handle->sa_dbu.dbu_evict_func_sync = NULL;
		handle->sa_dbu.dbu_evict_func_async = NULL;
		handle->sa_userp = userp;
		handle->sa_bonus = db;
		handle->sa_os = os;
		handle->sa_spill = NULL;
		handle->sa_bonus_tab = NULL;
		handle->sa_spill_tab = NULL;

		error = sa_build_index(handle, SA_BONUS);

		if (hdl_type == SA_HDL_SHARED) {
			dmu_buf_init_user(&handle->sa_dbu, sa_evict_sync, NULL,
			    NULL);
			winner = dmu_buf_set_user_ie(db, &handle->sa_dbu);
		}

		if (winner != NULL) {
			kmem_cache_free(sa_cache, handle);
			handle = winner;
		}
	}
	*handlepp = handle;

	return (error);
}

int
sa_handle_get(objset_t *objset, uint64_t objid, void *userp,
    sa_handle_type_t hdl_type, sa_handle_t **handlepp)
{
	dmu_buf_t *db;
	int error;

	if (error = dmu_bonus_hold(objset, objid, NULL, &db))
		return (error);

	return (sa_handle_get_from_db(objset, db, userp, hdl_type,
	    handlepp));
}

int
sa_buf_hold(objset_t *objset, uint64_t obj_num, void *tag, dmu_buf_t **db)
{
	return (dmu_bonus_hold(objset, obj_num, tag, db));
}

void
sa_buf_rele(dmu_buf_t *db, void *tag)
{
	dmu_buf_rele(db, tag);
}

int
sa_lookup_impl(sa_handle_t *hdl, sa_bulk_attr_t *bulk, int count)
{
	ASSERT(hdl);
	ASSERT(MUTEX_HELD(&hdl->sa_lock));
	return (sa_attr_op(hdl, bulk, count, SA_LOOKUP, NULL));
}

int
sa_lookup(sa_handle_t *hdl, sa_attr_type_t attr, void *buf, uint32_t buflen)
{
	int error;
	sa_bulk_attr_t bulk;

	bulk.sa_attr = attr;
	bulk.sa_data = buf;
	bulk.sa_length = buflen;
	bulk.sa_data_func = NULL;

	ASSERT(hdl);
	mutex_enter(&hdl->sa_lock);
	error = sa_lookup_impl(hdl, &bulk, 1);
	mutex_exit(&hdl->sa_lock);
	return (error);
}

#ifdef _KERNEL
int
sa_lookup_uio(sa_handle_t *hdl, sa_attr_type_t attr, uio_t *uio)
{
	int error;
	sa_bulk_attr_t bulk;

	bulk.sa_data = NULL;
	bulk.sa_attr = attr;
	bulk.sa_data_func = NULL;

	ASSERT(hdl);

	mutex_enter(&hdl->sa_lock);
	if ((error = sa_attr_op(hdl, &bulk, 1, SA_LOOKUP, NULL)) == 0) {
		error = uiomove((void *)bulk.sa_addr, MIN(bulk.sa_size,
		    uio->uio_resid), UIO_READ, uio);
	}
	mutex_exit(&hdl->sa_lock);
	return (error);

}
#endif

static sa_idx_tab_t *
sa_find_idx_tab(objset_t *os, dmu_object_type_t bonustype, sa_hdr_phys_t *hdr)
{
	sa_idx_tab_t *idx_tab;
	sa_os_t *sa = os->os_sa;
	sa_lot_t *tb, search;
	avl_index_t loc;

	/*
	 * Deterimine layout number.  If SA node and header == 0 then
	 * force the index table to the dummy "1" empty layout.
	 *
	 * The layout number would only be zero for a newly created file
	 * that has not added any attributes yet, or with crypto enabled which
	 * doesn't write any attributes to the bonus buffer.
	 */

	search.lot_num = SA_LAYOUT_NUM(hdr, bonustype);

	tb = avl_find(&sa->sa_layout_num_tree, &search, &loc);

	/* Verify header size is consistent with layout information */
	ASSERT(tb);
	ASSERT(IS_SA_BONUSTYPE(bonustype) &&
	    SA_HDR_SIZE_MATCH_LAYOUT(hdr, tb) || !IS_SA_BONUSTYPE(bonustype) ||
	    (IS_SA_BONUSTYPE(bonustype) && hdr->sa_layout_info == 0));

	/*
	 * See if any of the already existing TOC entries can be reused?
	 */

	for (idx_tab = list_head(&tb->lot_idx_tab); idx_tab;
	    idx_tab = list_next(&tb->lot_idx_tab, idx_tab)) {
		boolean_t valid_idx = B_TRUE;
		int i;

		if (tb->lot_var_sizes != 0 &&
		    idx_tab->sa_variable_lengths != NULL) {
			for (i = 0; i != tb->lot_var_sizes; i++) {
				if (hdr->sa_lengths[i] !=
				    idx_tab->sa_variable_lengths[i]) {
					valid_idx = B_FALSE;
					break;
				}
			}
		}
		if (valid_idx) {
			sa_idx_tab_hold(os, idx_tab);
			return (idx_tab);
		}
	}

	/* No such luck, create a new entry */
	idx_tab = kmem_zalloc(sizeof (sa_idx_tab_t), KM_SLEEP);
	idx_tab->sa_idx_tab =
	    kmem_zalloc(sizeof (uint32_t) * sa->sa_num_attrs, KM_SLEEP);
	idx_tab->sa_layout = tb;
	refcount_create(&idx_tab->sa_refcount);
	if (tb->lot_var_sizes)
		idx_tab->sa_variable_lengths = kmem_alloc(sizeof (uint16_t) *
		    tb->lot_var_sizes, KM_SLEEP);

	sa_attr_iter(os, hdr, bonustype, sa_build_idx_tab,
	    tb, idx_tab);
	sa_idx_tab_hold(os, idx_tab);   /* one hold for consumer */
	sa_idx_tab_hold(os, idx_tab);	/* one for layout */
	list_insert_tail(&tb->lot_idx_tab, idx_tab);
	return (idx_tab);
}

void
sa_default_locator(void **dataptr, uint32_t *len, uint32_t total_len,
    boolean_t start, void *userdata)
{
	ASSERT(start);

	*dataptr = userdata;
	*len = total_len;
}

static void
sa_attr_register_sync(sa_handle_t *hdl, dmu_tx_t *tx)
{
	uint64_t attr_value = 0;
	sa_os_t *sa = hdl->sa_os->os_sa;
	sa_attr_table_t *tb = sa->sa_attr_table;
	int i;

	mutex_enter(&sa->sa_lock);

	if (!sa->sa_need_attr_registration || sa->sa_master_obj == 0) {
		mutex_exit(&sa->sa_lock);
		return;
	}

	if (sa->sa_reg_attr_obj == 0) {
		sa->sa_reg_attr_obj = zap_create_link(hdl->sa_os,
		    DMU_OT_SA_ATTR_REGISTRATION,
		    sa->sa_master_obj, SA_REGISTRY, tx);
	}
	for (i = 0; i != sa->sa_num_attrs; i++) {
		if (sa->sa_attr_table[i].sa_registered)
			continue;
		ATTR_ENCODE(attr_value, tb[i].sa_attr, tb[i].sa_length,
		    tb[i].sa_byteswap);
		VERIFY(0 == zap_update(hdl->sa_os, sa->sa_reg_attr_obj,
		    tb[i].sa_name, 8, 1, &attr_value, tx));
		tb[i].sa_registered = B_TRUE;
	}
	sa->sa_need_attr_registration = B_FALSE;
	mutex_exit(&sa->sa_lock);
}

/*
 * Replace all attributes with attributes specified in template.
 * If dnode had a spill buffer then those attributes will be
 * also be replaced, possibly with just an empty spill block
 *
 * This interface is intended to only be used for bulk adding of
 * attributes for a new file.  It will also be used by the ZPL
 * when converting and old formatted znode to native SA support.
 */
int
sa_replace_all_by_template_locked(sa_handle_t *hdl, sa_bulk_attr_t *attr_desc,
    int attr_count, dmu_tx_t *tx)
{
	sa_os_t *sa = hdl->sa_os->os_sa;

	if (sa->sa_need_attr_registration)
		sa_attr_register_sync(hdl, tx);
	return (sa_build_layouts(hdl, attr_desc, attr_count, tx));
}

int
sa_replace_all_by_template(sa_handle_t *hdl, sa_bulk_attr_t *attr_desc,
    int attr_count, dmu_tx_t *tx)
{
	int error;

	mutex_enter(&hdl->sa_lock);
	error = sa_replace_all_by_template_locked(hdl, attr_desc,
	    attr_count, tx);
	mutex_exit(&hdl->sa_lock);
	return (error);
}

/*
 * Add/remove a single attribute or replace a variable-sized attribute value
 * with a value of a different size, and then rewrite the entire set
 * of attributes.
 * Same-length attribute value replacement (including fixed-length attributes)
 * is handled more efficiently by the upper layers.
 */
static int
sa_modify_attrs(sa_handle_t *hdl, sa_attr_type_t newattr,
    sa_data_op_t action, sa_data_locator_t *locator, void *datastart,
    uint16_t buflen, dmu_tx_t *tx)
{
	sa_os_t *sa = hdl->sa_os->os_sa;
	dmu_buf_impl_t *db = (dmu_buf_impl_t *)hdl->sa_bonus;
	dnode_t *dn;
	sa_bulk_attr_t *attr_desc;
	void *old_data[2];
	int bonus_attr_count = 0;
	int bonus_data_size = 0;
	int spill_data_size = 0;
	int spill_attr_count = 0;
	int error;
	uint16_t length, reg_length;
	int i, j, k, length_idx;
	sa_hdr_phys_t *hdr;
	sa_idx_tab_t *idx_tab;
	int attr_count;
	int count;

	ASSERT(MUTEX_HELD(&hdl->sa_lock));

	/* First make of copy of the old data */

	DB_DNODE_ENTER(db);
	dn = DB_DNODE(db);
	if (dn->dn_bonuslen != 0) {
		bonus_data_size = hdl->sa_bonus->db_size;
		old_data[0] = kmem_alloc(bonus_data_size, KM_SLEEP);
		bcopy(hdl->sa_bonus->db_data, old_data[0],
		    hdl->sa_bonus->db_size);
		bonus_attr_count = hdl->sa_bonus_tab->sa_layout->lot_attr_count;
	} else {
		old_data[0] = NULL;
	}
	DB_DNODE_EXIT(db);

	/* Bring spill buffer online if it isn't currently */

	if ((error = sa_get_spill(hdl)) == 0) {
		spill_data_size = hdl->sa_spill->db_size;
		old_data[1] = kmem_alloc(spill_data_size, KM_SLEEP);
		bcopy(hdl->sa_spill->db_data, old_data[1],
		    hdl->sa_spill->db_size);
		spill_attr_count =
		    hdl->sa_spill_tab->sa_layout->lot_attr_count;
	} else if (error && error != ENOENT) {
		if (old_data[0])
			kmem_free(old_data[0], bonus_data_size);
		return (error);
	} else {
		old_data[1] = NULL;
	}

	/* build descriptor of all attributes */

	attr_count = bonus_attr_count + spill_attr_count;
	if (action == SA_ADD)
		attr_count++;
	else if (action == SA_REMOVE)
		attr_count--;

	attr_desc = kmem_zalloc(sizeof (sa_bulk_attr_t) * attr_count, KM_SLEEP);

	/*
	 * loop through bonus and spill buffer if it exists, and
	 * build up new attr_descriptor to reset the attributes
	 */
	k = j = 0;
	count = bonus_attr_count;
	hdr = SA_GET_HDR(hdl, SA_BONUS);
	idx_tab = SA_IDX_TAB_GET(hdl, SA_BONUS);
	for (; k != 2; k++) {
		/*
		 * Iterate over each attribute in layout.  Fetch the
		 * size of variable-length attributes needing rewrite
		 * from sa_lengths[].
		 */
		for (i = 0, length_idx = 0; i != count; i++) {
			sa_attr_type_t attr;

			attr = idx_tab->sa_layout->lot_attrs[i];
			reg_length = SA_REGISTERED_LEN(sa, attr);
			if (reg_length == 0) {
				length = hdr->sa_lengths[length_idx];
				length_idx++;
			} else {
				length = reg_length;
			}
			if (attr == newattr) {
				/*
				 * There is nothing to do for SA_REMOVE,
				 * so it is just skipped.
				 */
				if (action == SA_REMOVE)
					continue;

				/*
				 * Duplicate attributes are not allowed, so the
				 * action can not be SA_ADD here.
				 */
				ASSERT3S(action, ==, SA_REPLACE);

				/*
				 * Only a variable-sized attribute can be
				 * replaced here, and its size must be changing.
				 */
				ASSERT3U(reg_length, ==, 0);
				ASSERT3U(length, !=, buflen);
				SA_ADD_BULK_ATTR(attr_desc, j, attr,
				    locator, datastart, buflen);
			} else {
				SA_ADD_BULK_ATTR(attr_desc, j, attr,
				    NULL, (void *)
				    (TOC_OFF(idx_tab->sa_idx_tab[attr]) +
				    (uintptr_t)old_data[k]), length);
			}
		}
		if (k == 0 && hdl->sa_spill) {
			hdr = SA_GET_HDR(hdl, SA_SPILL);
			idx_tab = SA_IDX_TAB_GET(hdl, SA_SPILL);
			count = spill_attr_count;
		} else {
			break;
		}
	}
	if (action == SA_ADD) {
		reg_length = SA_REGISTERED_LEN(sa, newattr);
		IMPLY(reg_length != 0, reg_length == buflen);
		SA_ADD_BULK_ATTR(attr_desc, j, newattr, locator,
		    datastart, buflen);
	}
	ASSERT3U(j, ==, attr_count);

	error = sa_build_layouts(hdl, attr_desc, attr_count, tx);

	if (old_data[0])
		kmem_free(old_data[0], bonus_data_size);
	if (old_data[1])
		kmem_free(old_data[1], spill_data_size);
	kmem_free(attr_desc, sizeof (sa_bulk_attr_t) * attr_count);

	return (error);
}

static int
sa_bulk_update_impl(sa_handle_t *hdl, sa_bulk_attr_t *bulk, int count,
    dmu_tx_t *tx)
{
	int error;
	sa_os_t *sa = hdl->sa_os->os_sa;
	dmu_object_type_t bonustype;

	bonustype = SA_BONUSTYPE_FROM_DB(SA_GET_DB(hdl, SA_BONUS));

	ASSERT(hdl);
	ASSERT(MUTEX_HELD(&hdl->sa_lock));

	/* sync out registration table if necessary */
	if (sa->sa_need_attr_registration)
		sa_attr_register_sync(hdl, tx);

	error = sa_attr_op(hdl, bulk, count, SA_UPDATE, tx);
	if (error == 0 && !IS_SA_BONUSTYPE(bonustype) && sa->sa_update_cb)
		sa->sa_update_cb(hdl, tx);

	return (error);
}

/*
 * update or add new attribute
 */
int
sa_update(sa_handle_t *hdl, sa_attr_type_t type,
    void *buf, uint32_t buflen, dmu_tx_t *tx)
{
	int error;
	sa_bulk_attr_t bulk;

	bulk.sa_attr = type;
	bulk.sa_data_func = NULL;
	bulk.sa_length = buflen;
	bulk.sa_data = buf;

	mutex_enter(&hdl->sa_lock);
	error = sa_bulk_update_impl(hdl, &bulk, 1, tx);
	mutex_exit(&hdl->sa_lock);
	return (error);
}

int
sa_update_from_cb(sa_handle_t *hdl, sa_attr_type_t attr,
    uint32_t buflen, sa_data_locator_t *locator, void *userdata, dmu_tx_t *tx)
{
	int error;
	sa_bulk_attr_t bulk;

	bulk.sa_attr = attr;
	bulk.sa_data = userdata;
	bulk.sa_data_func = locator;
	bulk.sa_length = buflen;

	mutex_enter(&hdl->sa_lock);
	error = sa_bulk_update_impl(hdl, &bulk, 1, tx);
	mutex_exit(&hdl->sa_lock);
	return (error);
}

/*
 * Return size of an attribute
 */

int
sa_size(sa_handle_t *hdl, sa_attr_type_t attr, int *size)
{
	sa_bulk_attr_t bulk;
	int error;

	bulk.sa_data = NULL;
	bulk.sa_attr = attr;
	bulk.sa_data_func = NULL;

	ASSERT(hdl);
	mutex_enter(&hdl->sa_lock);
	if ((error = sa_attr_op(hdl, &bulk, 1, SA_LOOKUP, NULL)) != 0) {
		mutex_exit(&hdl->sa_lock);
		return (error);
	}
	*size = bulk.sa_size;

	mutex_exit(&hdl->sa_lock);
	return (0);
}

int
sa_bulk_lookup_locked(sa_handle_t *hdl, sa_bulk_attr_t *attrs, int count)
{
	ASSERT(hdl);
	ASSERT(MUTEX_HELD(&hdl->sa_lock));
	return (sa_lookup_impl(hdl, attrs, count));
}

int
sa_bulk_lookup(sa_handle_t *hdl, sa_bulk_attr_t *attrs, int count)
{
	int error;

	ASSERT(hdl);
	mutex_enter(&hdl->sa_lock);
	error = sa_bulk_lookup_locked(hdl, attrs, count);
	mutex_exit(&hdl->sa_lock);
	return (error);
}

int
sa_bulk_update(sa_handle_t *hdl, sa_bulk_attr_t *attrs, int count, dmu_tx_t *tx)
{
	int error;

	ASSERT(hdl);
	mutex_enter(&hdl->sa_lock);
	error = sa_bulk_update_impl(hdl, attrs, count, tx);
	mutex_exit(&hdl->sa_lock);
	return (error);
}

int
sa_remove(sa_handle_t *hdl, sa_attr_type_t attr, dmu_tx_t *tx)
{
	int error;

	mutex_enter(&hdl->sa_lock);
	error = sa_modify_attrs(hdl, attr, SA_REMOVE, NULL,
	    NULL, 0, tx);
	mutex_exit(&hdl->sa_lock);
	return (error);
}

void
sa_object_info(sa_handle_t *hdl, dmu_object_info_t *doi)
{
	dmu_object_info_from_db((dmu_buf_t *)hdl->sa_bonus, doi);
}

void
sa_object_size(sa_handle_t *hdl, uint32_t *blksize, u_longlong_t *nblocks)
{
	dmu_object_size_from_db((dmu_buf_t *)hdl->sa_bonus,
	    blksize, nblocks);
}

void
sa_set_userp(sa_handle_t *hdl, void *ptr)
{
	hdl->sa_userp = ptr;
}

dmu_buf_t *
sa_get_db(sa_handle_t *hdl)
{
	return ((dmu_buf_t *)hdl->sa_bonus);
}

void *
sa_get_userdata(sa_handle_t *hdl)
{
	return (hdl->sa_userp);
}

void
sa_register_update_callback_locked(objset_t *os, sa_update_cb_t *func)
{
	ASSERT(MUTEX_HELD(&os->os_sa->sa_lock));
	os->os_sa->sa_update_cb = func;
}

void
sa_register_update_callback(objset_t *os, sa_update_cb_t *func)
{

	mutex_enter(&os->os_sa->sa_lock);
	sa_register_update_callback_locked(os, func);
	mutex_exit(&os->os_sa->sa_lock);
}

uint64_t
sa_handle_object(sa_handle_t *hdl)
{
	return (hdl->sa_bonus->db_object);
}

boolean_t
sa_enabled(objset_t *os)
{
	return (os->os_sa == NULL);
}

int
sa_set_sa_object(objset_t *os, uint64_t sa_object)
{
	sa_os_t *sa = os->os_sa;

	if (sa->sa_master_obj)
		return (1);

	sa->sa_master_obj = sa_object;

	return (0);
}

int
sa_hdrsize(void *arg)
{
	sa_hdr_phys_t *hdr = arg;

	return (SA_HDR_SIZE(hdr));
}

void
sa_handle_lock(sa_handle_t *hdl)
{
	ASSERT(hdl);
	mutex_enter(&hdl->sa_lock);
}

void
sa_handle_unlock(sa_handle_t *hdl)
{
	ASSERT(hdl);
	mutex_exit(&hdl->sa_lock);
}
