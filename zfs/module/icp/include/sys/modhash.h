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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_MODHASH_H
#define	_SYS_MODHASH_H

/*
 * Generic hash implementation for the kernel.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>

/*
 * Opaque data types for storing keys and values
 */
typedef void *mod_hash_val_t;
typedef void *mod_hash_key_t;

/*
 * Opaque data type for reservation
 */
typedef void *mod_hash_hndl_t;

/*
 * Opaque type for hash itself.
 */
struct mod_hash;
typedef struct mod_hash mod_hash_t;

/*
 * String hash table
 */
mod_hash_t *mod_hash_create_strhash_nodtr(char *, size_t,
	void (*)(mod_hash_val_t));
mod_hash_t *mod_hash_create_strhash(char *, size_t, void (*)(mod_hash_val_t));
void mod_hash_destroy_strhash(mod_hash_t *);
int mod_hash_strkey_cmp(mod_hash_key_t, mod_hash_key_t);
void mod_hash_strkey_dtor(mod_hash_key_t);
void mod_hash_strval_dtor(mod_hash_val_t);
uint_t mod_hash_bystr(void *, mod_hash_key_t);

/*
 * Pointer hash table
 */
mod_hash_t *mod_hash_create_ptrhash(char *, size_t, void (*)(mod_hash_val_t),
	size_t);
void mod_hash_destroy_ptrhash(mod_hash_t *);
int mod_hash_ptrkey_cmp(mod_hash_key_t, mod_hash_key_t);
uint_t mod_hash_byptr(void *, mod_hash_key_t);

/*
 * ID hash table
 */
mod_hash_t *mod_hash_create_idhash(char *, size_t, void (*)(mod_hash_val_t));
void mod_hash_destroy_idhash(mod_hash_t *);
int mod_hash_idkey_cmp(mod_hash_key_t, mod_hash_key_t);
uint_t mod_hash_byid(void *, mod_hash_key_t);
uint_t mod_hash_iddata_gen(size_t);

/*
 * Hash management functions
 */
mod_hash_t *mod_hash_create_extended(char *, size_t, void (*)(mod_hash_key_t),
	void (*)(mod_hash_val_t), uint_t (*)(void *, mod_hash_key_t), void *,
	int (*)(mod_hash_key_t, mod_hash_key_t), int);

void mod_hash_destroy_hash(mod_hash_t *);
void mod_hash_clear(mod_hash_t *);

/*
 * Null key and value destructors
 */
void mod_hash_null_keydtor(mod_hash_key_t);
void mod_hash_null_valdtor(mod_hash_val_t);

/*
 * Basic hash operations
 */

/*
 * Error codes for insert, remove, find, destroy.
 */
#define	MH_ERR_NOMEM -1
#define	MH_ERR_DUPLICATE -2
#define	MH_ERR_NOTFOUND -3

/*
 * Return codes for hash walkers
 */
#define	MH_WALK_CONTINUE 0
#define	MH_WALK_TERMINATE 1

/*
 * Basic hash operations
 */
int mod_hash_insert(mod_hash_t *, mod_hash_key_t, mod_hash_val_t);
int mod_hash_replace(mod_hash_t *, mod_hash_key_t, mod_hash_val_t);
int mod_hash_remove(mod_hash_t *, mod_hash_key_t, mod_hash_val_t *);
int mod_hash_destroy(mod_hash_t *, mod_hash_key_t);
int mod_hash_find(mod_hash_t *, mod_hash_key_t, mod_hash_val_t *);
int mod_hash_find_cb(mod_hash_t *, mod_hash_key_t, mod_hash_val_t *,
	void (*)(mod_hash_key_t, mod_hash_val_t));
int mod_hash_find_cb_rval(mod_hash_t *, mod_hash_key_t, mod_hash_val_t *,
	int (*)(mod_hash_key_t, mod_hash_val_t), int *);
void mod_hash_walk(mod_hash_t *,
	uint_t (*)(mod_hash_key_t, mod_hash_val_t *, void *), void *);

/*
 * Reserving hash operations
 */
int mod_hash_reserve(mod_hash_t *, mod_hash_hndl_t *);
int mod_hash_reserve_nosleep(mod_hash_t *, mod_hash_hndl_t *);
void mod_hash_cancel(mod_hash_t *, mod_hash_hndl_t *);
int mod_hash_insert_reserve(mod_hash_t *, mod_hash_key_t, mod_hash_val_t,
	mod_hash_hndl_t);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_MODHASH_H */
