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
 */

#ifndef	_SYS_SA_H
#define	_SYS_SA_H

#include <sys/dmu.h>

/*
 * Currently available byteswap functions.
 * If it all possible new attributes should used
 * one of the already defined byteswap functions.
 * If a new byteswap function is added then the
 * ZPL/Pool version will need to be bumped.
 */

typedef enum sa_bswap_type {
	SA_UINT64_ARRAY,
	SA_UINT32_ARRAY,
	SA_UINT16_ARRAY,
	SA_UINT8_ARRAY,
	SA_ACL,
} sa_bswap_type_t;

typedef uint16_t	sa_attr_type_t;

/*
 * Attribute to register support for.
 */
typedef struct sa_attr_reg {
	char 			*sa_name;	/* attribute name */
	uint16_t 		sa_length;
	sa_bswap_type_t		sa_byteswap;	/* bswap functon enum */
	sa_attr_type_t 		sa_attr; /* filled in during registration */
} sa_attr_reg_t;


typedef void (sa_data_locator_t)(void **, uint32_t *, uint32_t,
    boolean_t, void *userptr);

/*
 * array of attributes to store.
 *
 * This array should be treated as opaque/private data.
 * The SA_BULK_ADD_ATTR() macro should be used for manipulating
 * the array.
 *
 * When sa_replace_all_by_template() is used the attributes
 * will be stored in the order defined in the array, except that
 * the attributes may be split between the bonus and the spill buffer
 *
 */
typedef struct sa_bulk_attr {
	void			*sa_data;
	sa_data_locator_t	*sa_data_func;
	uint16_t		sa_length;
	sa_attr_type_t		sa_attr;
	/* the following are private to the sa framework */
	void 			*sa_addr;
	uint16_t		sa_buftype;
	uint16_t		sa_size;
} sa_bulk_attr_t;

/*
 * The on-disk format of sa_hdr_phys_t limits SA lengths to 16-bit values.
 */
#define	SA_ATTR_MAX_LEN UINT16_MAX

/*
 * special macro for adding entries for bulk attr support
 * bulk - sa_bulk_attr_t
 * count - integer that will be incremented during each add
 * attr - attribute to manipulate
 * func - function for accessing data.
 * data - pointer to data.
 * len - length of data
 */

#define	SA_ADD_BULK_ATTR(b, idx, attr, func, data, len) \
{ \
	ASSERT3U(len, <=, SA_ATTR_MAX_LEN); \
	b[idx].sa_attr = attr;\
	b[idx].sa_data_func = func; \
	b[idx].sa_data = data; \
	b[idx++].sa_length = len; \
}

typedef struct sa_os sa_os_t;

typedef enum sa_handle_type {
	SA_HDL_SHARED,
	SA_HDL_PRIVATE
} sa_handle_type_t;

struct sa_handle;
typedef void *sa_lookup_tab_t;
typedef struct sa_handle sa_handle_t;

typedef void (sa_update_cb_t)(sa_handle_t *, dmu_tx_t *tx);

int sa_handle_get(objset_t *, uint64_t, void *userp,
    sa_handle_type_t, sa_handle_t **);
int sa_handle_get_from_db(objset_t *, dmu_buf_t *, void *userp,
    sa_handle_type_t, sa_handle_t **);
void sa_handle_destroy(sa_handle_t *);
int sa_buf_hold(objset_t *, uint64_t, void *, dmu_buf_t **);
void sa_buf_rele(dmu_buf_t *, void *);
int sa_lookup(sa_handle_t *, sa_attr_type_t, void *buf, uint32_t buflen);
int sa_update(sa_handle_t *, sa_attr_type_t, void *buf,
    uint32_t buflen, dmu_tx_t *);
int sa_remove(sa_handle_t *, sa_attr_type_t, dmu_tx_t *);
int sa_bulk_lookup(sa_handle_t *, sa_bulk_attr_t *, int count);
int sa_bulk_lookup_locked(sa_handle_t *, sa_bulk_attr_t *, int count);
int sa_bulk_update(sa_handle_t *, sa_bulk_attr_t *, int count, dmu_tx_t *);
int sa_size(sa_handle_t *, sa_attr_type_t, int *);
void sa_object_info(sa_handle_t *, dmu_object_info_t *);
void sa_object_size(sa_handle_t *, uint32_t *, u_longlong_t *);
void *sa_get_userdata(sa_handle_t *);
void sa_set_userp(sa_handle_t *, void *);
dmu_buf_t *sa_get_db(sa_handle_t *);
uint64_t sa_handle_object(sa_handle_t *);
boolean_t sa_attr_would_spill(sa_handle_t *, sa_attr_type_t, int size);
void sa_spill_rele(sa_handle_t *);
void sa_register_update_callback(objset_t *, sa_update_cb_t *);
int sa_setup(objset_t *, uint64_t, sa_attr_reg_t *, int, sa_attr_type_t **);
void sa_tear_down(objset_t *);
int sa_replace_all_by_template(sa_handle_t *, sa_bulk_attr_t *,
    int, dmu_tx_t *);
int sa_replace_all_by_template_locked(sa_handle_t *, sa_bulk_attr_t *,
    int, dmu_tx_t *);
boolean_t sa_enabled(objset_t *);
void sa_cache_init(void);
void sa_cache_fini(void);
int sa_set_sa_object(objset_t *, uint64_t);
int sa_hdrsize(void *);
void sa_handle_lock(sa_handle_t *);
void sa_handle_unlock(sa_handle_t *);

#ifdef _KERNEL
int sa_lookup_uio(sa_handle_t *, sa_attr_type_t, uio_t *);
#endif

#ifdef	__cplusplus
extern "C" {
#endif


#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SA_H */
