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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 */

#ifndef	_SYS_ZAP_H
#define	_SYS_ZAP_H

/*
 * ZAP - ZFS Attribute Processor
 *
 * The ZAP is a module which sits on top of the DMU (Data Management
 * Unit) and implements a higher-level storage primitive using DMU
 * objects.  Its primary consumer is the ZPL (ZFS Posix Layer).
 *
 * A "zapobj" is a DMU object which the ZAP uses to stores attributes.
 * Users should use only zap routines to access a zapobj - they should
 * not access the DMU object directly using DMU routines.
 *
 * The attributes stored in a zapobj are name-value pairs.  The name is
 * a zero-terminated string of up to ZAP_MAXNAMELEN bytes (including
 * terminating NULL).  The value is an array of integers, which may be
 * 1, 2, 4, or 8 bytes long.  The total space used by the array (number
 * of integers * integer length) can be up to ZAP_MAXVALUELEN bytes.
 * Note that an 8-byte integer value can be used to store the location
 * (object number) of another dmu object (which may be itself a zapobj).
 * Note that you can use a zero-length attribute to store a single bit
 * of information - the attribute is present or not.
 *
 * The ZAP routines are thread-safe.  However, you must observe the
 * DMU's restriction that a transaction may not be operated on
 * concurrently.
 *
 * Any of the routines that return an int may return an I/O error (EIO
 * or ECHECKSUM).
 *
 *
 * Implementation / Performance Notes:
 *
 * The ZAP is intended to operate most efficiently on attributes with
 * short (49 bytes or less) names and single 8-byte values, for which
 * the microzap will be used.  The ZAP should be efficient enough so
 * that the user does not need to cache these attributes.
 *
 * The ZAP's locking scheme makes its routines thread-safe.  Operations
 * on different zapobjs will be processed concurrently.  Operations on
 * the same zapobj which only read data will be processed concurrently.
 * Operations on the same zapobj which modify data will be processed
 * concurrently when there are many attributes in the zapobj (because
 * the ZAP uses per-block locking - more than 128 * (number of cpus)
 * small attributes will suffice).
 */

/*
 * We're using zero-terminated byte strings (ie. ASCII or UTF-8 C
 * strings) for the names of attributes, rather than a byte string
 * bounded by an explicit length.  If some day we want to support names
 * in character sets which have embedded zeros (eg. UTF-16, UTF-32),
 * we'll have to add routines for using length-bounded strings.
 */

#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Specifies matching criteria for ZAP lookups.
 * MT_NORMALIZE		Use ZAP normalization flags, which can include both
 *			unicode normalization and case-insensitivity.
 * MT_MATCH_CASE	Do case-sensitive lookups even if MT_NORMALIZE is
 *			specified and ZAP normalization flags include
 *			U8_TEXTPREP_TOUPPER.
 */
typedef enum matchtype {
	MT_NORMALIZE = 1 << 0,
	MT_MATCH_CASE = 1 << 1,
} matchtype_t;

typedef enum zap_flags {
	/* Use 64-bit hash value (serialized cursors will always use 64-bits) */
	ZAP_FLAG_HASH64 = 1 << 0,
	/* Key is binary, not string (zap_add_uint64() can be used) */
	ZAP_FLAG_UINT64_KEY = 1 << 1,
	/*
	 * First word of key (which must be an array of uint64) is
	 * already randomly distributed.
	 */
	ZAP_FLAG_PRE_HASHED_KEY = 1 << 2,
} zap_flags_t;

/*
 * Create a new zapobj with no attributes and return its object number.
 */
uint64_t zap_create(objset_t *ds, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx);
uint64_t zap_create_dnsize(objset_t *ds, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx);
uint64_t zap_create_norm(objset_t *ds, int normflags, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx);
uint64_t zap_create_norm_dnsize(objset_t *ds, int normflags,
    dmu_object_type_t ot, dmu_object_type_t bonustype, int bonuslen,
    int dnodesize, dmu_tx_t *tx);
uint64_t zap_create_flags(objset_t *os, int normflags, zap_flags_t flags,
    dmu_object_type_t ot, int leaf_blockshift, int indirect_blockshift,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx);
uint64_t zap_create_flags_dnsize(objset_t *os, int normflags,
    zap_flags_t flags, dmu_object_type_t ot, int leaf_blockshift,
    int indirect_blockshift, dmu_object_type_t bonustype, int bonuslen,
    int dnodesize, dmu_tx_t *tx);
uint64_t zap_create_link(objset_t *os, dmu_object_type_t ot,
    uint64_t parent_obj, const char *name, dmu_tx_t *tx);
uint64_t zap_create_link_dnsize(objset_t *os, dmu_object_type_t ot,
    uint64_t parent_obj, const char *name, int dnodesize, dmu_tx_t *tx);

/*
 * Initialize an already-allocated object.
 */
void mzap_create_impl(objset_t *os, uint64_t obj, int normflags,
    zap_flags_t flags, dmu_tx_t *tx);

/*
 * Create a new zapobj with no attributes from the given (unallocated)
 * object number.
 */
int zap_create_claim(objset_t *ds, uint64_t obj, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx);
int zap_create_claim_dnsize(objset_t *ds, uint64_t obj, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx);
int zap_create_claim_norm(objset_t *ds, uint64_t obj,
    int normflags, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx);
int zap_create_claim_norm_dnsize(objset_t *ds, uint64_t obj,
    int normflags, dmu_object_type_t ot,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx);

/*
 * The zapobj passed in must be a valid ZAP object for all of the
 * following routines.
 */

/*
 * Destroy this zapobj and all its attributes.
 *
 * Frees the object number using dmu_object_free.
 */
int zap_destroy(objset_t *ds, uint64_t zapobj, dmu_tx_t *tx);

/*
 * Manipulate attributes.
 *
 * 'integer_size' is in bytes, and must be 1, 2, 4, or 8.
 */

/*
 * Retrieve the contents of the attribute with the given name.
 *
 * If the requested attribute does not exist, the call will fail and
 * return ENOENT.
 *
 * If 'integer_size' is smaller than the attribute's integer size, the
 * call will fail and return EINVAL.
 *
 * If 'integer_size' is equal to or larger than the attribute's integer
 * size, the call will succeed and return 0.
 *
 * When converting to a larger integer size, the integers will be treated as
 * unsigned (ie. no sign-extension will be performed).
 *
 * 'num_integers' is the length (in integers) of 'buf'.
 *
 * If the attribute is longer than the buffer, as many integers as will
 * fit will be transferred to 'buf'.  If the entire attribute was not
 * transferred, the call will return EOVERFLOW.
 */
int zap_lookup(objset_t *ds, uint64_t zapobj, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf);

/*
 * If rn_len is nonzero, realname will be set to the name of the found
 * entry (which may be different from the requested name if matchtype is
 * not MT_EXACT).
 *
 * If normalization_conflictp is not NULL, it will be set if there is
 * another name with the same case/unicode normalized form.
 */
int zap_lookup_norm(objset_t *ds, uint64_t zapobj, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    matchtype_t mt, char *realname, int rn_len,
    boolean_t *normalization_conflictp);
int zap_lookup_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, uint64_t integer_size, uint64_t num_integers, void *buf);
int zap_contains(objset_t *ds, uint64_t zapobj, const char *name);
int zap_prefetch(objset_t *os, uint64_t zapobj, const char *name);
int zap_prefetch_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints);

int zap_lookup_by_dnode(dnode_t *dn, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf);
int zap_lookup_norm_by_dnode(dnode_t *dn, const char *name,
    uint64_t integer_size, uint64_t num_integers, void *buf,
    matchtype_t mt, char *realname, int rn_len,
    boolean_t *ncp);

int zap_count_write_by_dnode(dnode_t *dn, const char *name,
    int add, refcount_t *towrite, refcount_t *tooverwrite);

/*
 * Create an attribute with the given name and value.
 *
 * If an attribute with the given name already exists, the call will
 * fail and return EEXIST.
 */
int zap_add(objset_t *ds, uint64_t zapobj, const char *key,
    int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx);
int zap_add_by_dnode(dnode_t *dn, const char *key,
    int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx);
int zap_add_uint64(objset_t *ds, uint64_t zapobj, const uint64_t *key,
    int key_numints, int integer_size, uint64_t num_integers,
    const void *val, dmu_tx_t *tx);

/*
 * Set the attribute with the given name to the given value.  If an
 * attribute with the given name does not exist, it will be created.  If
 * an attribute with the given name already exists, the previous value
 * will be overwritten.  The integer_size may be different from the
 * existing attribute's integer size, in which case the attribute's
 * integer size will be updated to the new value.
 */
int zap_update(objset_t *ds, uint64_t zapobj, const char *name,
    int integer_size, uint64_t num_integers, const void *val, dmu_tx_t *tx);
int zap_update_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints,
    int integer_size, uint64_t num_integers, const void *val, dmu_tx_t *tx);

/*
 * Get the length (in integers) and the integer size of the specified
 * attribute.
 *
 * If the requested attribute does not exist, the call will fail and
 * return ENOENT.
 */
int zap_length(objset_t *ds, uint64_t zapobj, const char *name,
    uint64_t *integer_size, uint64_t *num_integers);
int zap_length_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, uint64_t *integer_size, uint64_t *num_integers);

/*
 * Remove the specified attribute.
 *
 * If the specified attribute does not exist, the call will fail and
 * return ENOENT.
 */
int zap_remove(objset_t *ds, uint64_t zapobj, const char *name, dmu_tx_t *tx);
int zap_remove_norm(objset_t *ds, uint64_t zapobj, const char *name,
    matchtype_t mt, dmu_tx_t *tx);
int zap_remove_by_dnode(dnode_t *dn, const char *name, dmu_tx_t *tx);
int zap_remove_uint64(objset_t *os, uint64_t zapobj, const uint64_t *key,
    int key_numints, dmu_tx_t *tx);

/*
 * Returns (in *count) the number of attributes in the specified zap
 * object.
 */
int zap_count(objset_t *ds, uint64_t zapobj, uint64_t *count);

/*
 * Returns (in name) the name of the entry whose (value & mask)
 * (za_first_integer) is value, or ENOENT if not found.  The string
 * pointed to by name must be at least 256 bytes long.  If mask==0, the
 * match must be exact (ie, same as mask=-1ULL).
 */
int zap_value_search(objset_t *os, uint64_t zapobj,
    uint64_t value, uint64_t mask, char *name);

/*
 * Transfer all the entries from fromobj into intoobj.  Only works on
 * int_size=8 num_integers=1 values.  Fails if there are any duplicated
 * entries.
 */
int zap_join(objset_t *os, uint64_t fromobj, uint64_t intoobj, dmu_tx_t *tx);

/* Same as zap_join, but set the values to 'value'. */
int zap_join_key(objset_t *os, uint64_t fromobj, uint64_t intoobj,
    uint64_t value, dmu_tx_t *tx);

/* Same as zap_join, but add together any duplicated entries. */
int zap_join_increment(objset_t *os, uint64_t fromobj, uint64_t intoobj,
    dmu_tx_t *tx);

/*
 * Manipulate entries where the name + value are the "same" (the name is
 * a stringified version of the value).
 */
int zap_add_int(objset_t *os, uint64_t obj, uint64_t value, dmu_tx_t *tx);
int zap_remove_int(objset_t *os, uint64_t obj, uint64_t value, dmu_tx_t *tx);
int zap_lookup_int(objset_t *os, uint64_t obj, uint64_t value);
int zap_increment_int(objset_t *os, uint64_t obj, uint64_t key, int64_t delta,
    dmu_tx_t *tx);

/* Here the key is an int and the value is a different int. */
int zap_add_int_key(objset_t *os, uint64_t obj,
    uint64_t key, uint64_t value, dmu_tx_t *tx);
int zap_update_int_key(objset_t *os, uint64_t obj,
    uint64_t key, uint64_t value, dmu_tx_t *tx);
int zap_lookup_int_key(objset_t *os, uint64_t obj,
    uint64_t key, uint64_t *valuep);

int zap_increment(objset_t *os, uint64_t obj, const char *name, int64_t delta,
    dmu_tx_t *tx);

struct zap;
struct zap_leaf;
typedef struct zap_cursor {
	/* This structure is opaque! */
	objset_t *zc_objset;
	struct zap *zc_zap;
	struct zap_leaf *zc_leaf;
	uint64_t zc_zapobj;
	uint64_t zc_serialized;
	uint64_t zc_hash;
	uint32_t zc_cd;
} zap_cursor_t;

typedef struct {
	int za_integer_length;
	/*
	 * za_normalization_conflict will be set if there are additional
	 * entries with this normalized form (eg, "foo" and "Foo").
	 */
	boolean_t za_normalization_conflict;
	uint64_t za_num_integers;
	uint64_t za_first_integer;	/* no sign extension for <8byte ints */
	char za_name[ZAP_MAXNAMELEN];
} zap_attribute_t;

/*
 * The interface for listing all the attributes of a zapobj can be
 * thought of as cursor moving down a list of the attributes one by
 * one.  The cookie returned by the zap_cursor_serialize routine is
 * persistent across system calls (and across reboot, even).
 */

/*
 * Initialize a zap cursor, pointing to the "first" attribute of the
 * zapobj.  You must _fini the cursor when you are done with it.
 */
void zap_cursor_init(zap_cursor_t *zc, objset_t *ds, uint64_t zapobj);
void zap_cursor_fini(zap_cursor_t *zc);

/*
 * Get the attribute currently pointed to by the cursor.  Returns
 * ENOENT if at the end of the attributes.
 */
int zap_cursor_retrieve(zap_cursor_t *zc, zap_attribute_t *za);

/*
 * Advance the cursor to the next attribute.
 */
void zap_cursor_advance(zap_cursor_t *zc);

/*
 * Get a persistent cookie pointing to the current position of the zap
 * cursor.  The low 4 bits in the cookie are always zero, and thus can
 * be used as to differentiate a serialized cookie from a different type
 * of value.  The cookie will be less than 2^32 as long as there are
 * fewer than 2^22 (4.2 million) entries in the zap object.
 */
uint64_t zap_cursor_serialize(zap_cursor_t *zc);

/*
 * Initialize a zap cursor pointing to the position recorded by
 * zap_cursor_serialize (in the "serialized" argument).  You can also
 * use a "serialized" argument of 0 to start at the beginning of the
 * zapobj (ie.  zap_cursor_init_serialized(..., 0) is equivalent to
 * zap_cursor_init(...).)
 */
void zap_cursor_init_serialized(zap_cursor_t *zc, objset_t *ds,
    uint64_t zapobj, uint64_t serialized);


#define	ZAP_HISTOGRAM_SIZE 10

typedef struct zap_stats {
	/*
	 * Size of the pointer table (in number of entries).
	 * This is always a power of 2, or zero if it's a microzap.
	 * In general, it should be considerably greater than zs_num_leafs.
	 */
	uint64_t zs_ptrtbl_len;

	uint64_t zs_blocksize;		/* size of zap blocks */

	/*
	 * The number of blocks used.  Note that some blocks may be
	 * wasted because old ptrtbl's and large name/value blocks are
	 * not reused.  (Although their space is reclaimed, we don't
	 * reuse those offsets in the object.)
	 */
	uint64_t zs_num_blocks;

	/*
	 * Pointer table values from zap_ptrtbl in the zap_phys_t
	 */
	uint64_t zs_ptrtbl_nextblk;	  /* next (larger) copy start block */
	uint64_t zs_ptrtbl_blks_copied;   /* number source blocks copied */
	uint64_t zs_ptrtbl_zt_blk;	  /* starting block number */
	uint64_t zs_ptrtbl_zt_numblks;    /* number of blocks */
	uint64_t zs_ptrtbl_zt_shift;	  /* bits to index it */

	/*
	 * Values of the other members of the zap_phys_t
	 */
	uint64_t zs_block_type;		/* ZBT_HEADER */
	uint64_t zs_magic;		/* ZAP_MAGIC */
	uint64_t zs_num_leafs;		/* The number of leaf blocks */
	uint64_t zs_num_entries;	/* The number of zap entries */
	uint64_t zs_salt;		/* salt to stir into hash function */

	/*
	 * Histograms.  For all histograms, the last index
	 * (ZAP_HISTOGRAM_SIZE-1) includes any values which are greater
	 * than what can be represented.  For example
	 * zs_leafs_with_n5_entries[ZAP_HISTOGRAM_SIZE-1] is the number
	 * of leafs with more than 45 entries.
	 */

	/*
	 * zs_leafs_with_n_pointers[n] is the number of leafs with
	 * 2^n pointers to it.
	 */
	uint64_t zs_leafs_with_2n_pointers[ZAP_HISTOGRAM_SIZE];

	/*
	 * zs_leafs_with_n_entries[n] is the number of leafs with
	 * [n*5, (n+1)*5) entries.  In the current implementation, there
	 * can be at most 55 entries in any block, but there may be
	 * fewer if the name or value is large, or the block is not
	 * completely full.
	 */
	uint64_t zs_blocks_with_n5_entries[ZAP_HISTOGRAM_SIZE];

	/*
	 * zs_leafs_n_tenths_full[n] is the number of leafs whose
	 * fullness is in the range [n/10, (n+1)/10).
	 */
	uint64_t zs_blocks_n_tenths_full[ZAP_HISTOGRAM_SIZE];

	/*
	 * zs_entries_using_n_chunks[n] is the number of entries which
	 * consume n 24-byte chunks.  (Note, large names/values only use
	 * one chunk, but contribute to zs_num_blocks_large.)
	 */
	uint64_t zs_entries_using_n_chunks[ZAP_HISTOGRAM_SIZE];

	/*
	 * zs_buckets_with_n_entries[n] is the number of buckets (each
	 * leaf has 64 buckets) with n entries.
	 * zs_buckets_with_n_entries[1] should be very close to
	 * zs_num_entries.
	 */
	uint64_t zs_buckets_with_n_entries[ZAP_HISTOGRAM_SIZE];
} zap_stats_t;

/*
 * Get statistics about a ZAP object.  Note: you need to be aware of the
 * internal implementation of the ZAP to correctly interpret some of the
 * statistics.  This interface shouldn't be relied on unless you really
 * know what you're doing.
 */
int zap_get_stats(objset_t *ds, uint64_t zapobj, zap_stats_t *zs);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZAP_H */
