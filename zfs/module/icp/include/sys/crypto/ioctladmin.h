/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_CRYPTO_IOCTLADMIN_H
#define	_SYS_CRYPTO_IOCTLADMIN_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>

#define	ADMIN_IOCTL_DEVICE	"/dev/cryptoadm"

#define	CRYPTOADMIN(x)		(('y' << 8) | (x))

/*
 * Administrative IOCTLs
 */

typedef struct crypto_get_dev_list {
	uint_t			dl_return_value;
	uint_t			dl_dev_count;
	crypto_dev_list_entry_t	dl_devs[1];
} crypto_get_dev_list_t;

typedef struct crypto_get_soft_list {
	uint_t			sl_return_value;
	uint_t			sl_soft_count;
	size_t			sl_soft_len;
	caddr_t			sl_soft_names;
} crypto_get_soft_list_t;

typedef struct crypto_get_dev_info {
	uint_t			di_return_value;
	char			di_dev_name[MAXNAMELEN];
	uint_t			di_dev_instance;
	uint_t			di_count;
	crypto_mech_name_t	di_list[1];
} crypto_get_dev_info_t;

typedef struct crypto_get_soft_info {
	uint_t			si_return_value;
	char			si_name[MAXNAMELEN];
	uint_t			si_count;
	crypto_mech_name_t	si_list[1];
} crypto_get_soft_info_t;

typedef struct crypto_load_dev_disabled {
	uint_t			dd_return_value;
	char			dd_dev_name[MAXNAMELEN];
	uint_t			dd_dev_instance;
	uint_t			dd_count;
	crypto_mech_name_t	dd_list[1];
} crypto_load_dev_disabled_t;

typedef struct crypto_load_soft_disabled {
	uint_t			sd_return_value;
	char			sd_name[MAXNAMELEN];
	uint_t			sd_count;
	crypto_mech_name_t	sd_list[1];
} crypto_load_soft_disabled_t;

typedef struct crypto_unload_soft_module {
	uint_t			sm_return_value;
	char			sm_name[MAXNAMELEN];
} crypto_unload_soft_module_t;

typedef struct crypto_load_soft_config {
	uint_t			sc_return_value;
	char			sc_name[MAXNAMELEN];
	uint_t			sc_count;
	crypto_mech_name_t	sc_list[1];
} crypto_load_soft_config_t;

typedef struct crypto_load_door {
	uint_t			ld_return_value;
	uint_t			ld_did;
} crypto_load_door_t;

#ifdef	_KERNEL
#ifdef	_SYSCALL32

typedef struct crypto_get_soft_list32 {
	uint32_t		sl_return_value;
	uint32_t		sl_soft_count;
	size32_t		sl_soft_len;
	caddr32_t		sl_soft_names;
} crypto_get_soft_list32_t;

#endif	/* _SYSCALL32 */
#endif	/* _KERNEL */

#define	CRYPTO_GET_VERSION		CRYPTOADMIN(1)
#define	CRYPTO_GET_DEV_LIST		CRYPTOADMIN(2)
#define	CRYPTO_GET_SOFT_LIST		CRYPTOADMIN(3)
#define	CRYPTO_GET_DEV_INFO		CRYPTOADMIN(4)
#define	CRYPTO_GET_SOFT_INFO		CRYPTOADMIN(5)
#define	CRYPTO_LOAD_DEV_DISABLED	CRYPTOADMIN(8)
#define	CRYPTO_LOAD_SOFT_DISABLED	CRYPTOADMIN(9)
#define	CRYPTO_UNLOAD_SOFT_MODULE	CRYPTOADMIN(10)
#define	CRYPTO_LOAD_SOFT_CONFIG		CRYPTOADMIN(11)
#define	CRYPTO_POOL_CREATE		CRYPTOADMIN(12)
#define	CRYPTO_POOL_WAIT		CRYPTOADMIN(13)
#define	CRYPTO_POOL_RUN			CRYPTOADMIN(14)
#define	CRYPTO_LOAD_DOOR		CRYPTOADMIN(15)

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CRYPTO_IOCTLADMIN_H */
