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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/ddt.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include <util/sscanf.h>

int ddt_zap_leaf_blockshift = 12;
int ddt_zap_indirect_blockshift = 12;

static int
ddt_zap_create(objset_t *os, uint64_t *objectp, dmu_tx_t *tx, boolean_t prehash)
{
	zap_flags_t flags = ZAP_FLAG_HASH64 | ZAP_FLAG_UINT64_KEY;

	if (prehash)
		flags |= ZAP_FLAG_PRE_HASHED_KEY;

	*objectp = zap_create_flags(os, 0, flags, DMU_OT_DDT_ZAP,
	    ddt_zap_leaf_blockshift, ddt_zap_indirect_blockshift,
	    DMU_OT_NONE, 0, tx);

	return (*objectp == 0 ? ENOTSUP : 0);
}

static int
ddt_zap_destroy(objset_t *os, uint64_t object, dmu_tx_t *tx)
{
	return (zap_destroy(os, object, tx));
}

static int
ddt_zap_lookup(objset_t *os, uint64_t object, ddt_entry_t *dde)
{
	uchar_t *cbuf;
	uint64_t one, csize;
	int error;

	cbuf = kmem_alloc(sizeof (dde->dde_phys) + 1, KM_SLEEP);

	error = zap_length_uint64(os, object, (uint64_t *)&dde->dde_key,
	    DDT_KEY_WORDS, &one, &csize);
	if (error)
		goto out;

	ASSERT(one == 1);
	ASSERT(csize <= (sizeof (dde->dde_phys) + 1));

	error = zap_lookup_uint64(os, object, (uint64_t *)&dde->dde_key,
	    DDT_KEY_WORDS, 1, csize, cbuf);
	if (error)
		goto out;

	ddt_decompress(cbuf, dde->dde_phys, csize, sizeof (dde->dde_phys));
out:
	kmem_free(cbuf, sizeof (dde->dde_phys) + 1);

	return (error);
}

static void
ddt_zap_prefetch(objset_t *os, uint64_t object, ddt_entry_t *dde)
{
	(void) zap_prefetch_uint64(os, object, (uint64_t *)&dde->dde_key,
	    DDT_KEY_WORDS);
}

static int
ddt_zap_update(objset_t *os, uint64_t object, ddt_entry_t *dde, dmu_tx_t *tx)
{
	uchar_t cbuf[sizeof (dde->dde_phys) + 1];
	uint64_t csize;

	csize = ddt_compress(dde->dde_phys, cbuf,
	    sizeof (dde->dde_phys), sizeof (cbuf));

	return (zap_update_uint64(os, object, (uint64_t *)&dde->dde_key,
	    DDT_KEY_WORDS, 1, csize, cbuf, tx));
}

static int
ddt_zap_remove(objset_t *os, uint64_t object, ddt_entry_t *dde, dmu_tx_t *tx)
{
	return (zap_remove_uint64(os, object, (uint64_t *)&dde->dde_key,
	    DDT_KEY_WORDS, tx));
}

static int
ddt_zap_walk(objset_t *os, uint64_t object, ddt_entry_t *dde, uint64_t *walk)
{
	zap_cursor_t zc;
	zap_attribute_t za;
	int error;

	zap_cursor_init_serialized(&zc, os, object, *walk);
	if ((error = zap_cursor_retrieve(&zc, &za)) == 0) {
		uchar_t cbuf[sizeof (dde->dde_phys) + 1];
		uint64_t csize = za.za_num_integers;
		ASSERT(za.za_integer_length == 1);
		error = zap_lookup_uint64(os, object, (uint64_t *)za.za_name,
		    DDT_KEY_WORDS, 1, csize, cbuf);
		ASSERT(error == 0);
		if (error == 0) {
			ddt_decompress(cbuf, dde->dde_phys, csize,
			    sizeof (dde->dde_phys));
			dde->dde_key = *(ddt_key_t *)za.za_name;
		}
		zap_cursor_advance(&zc);
		*walk = zap_cursor_serialize(&zc);
	}
	zap_cursor_fini(&zc);
	return (error);
}

static int
ddt_zap_count(objset_t *os, uint64_t object, uint64_t *count)
{
	return (zap_count(os, object, count));
}

const ddt_ops_t ddt_zap_ops = {
	"zap",
	ddt_zap_create,
	ddt_zap_destroy,
	ddt_zap_lookup,
	ddt_zap_prefetch,
	ddt_zap_update,
	ddt_zap_remove,
	ddt_zap_walk,
	ddt_zap_count,
};
