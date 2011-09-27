/*
 * Crypto user configuration API.
 *
 * Copyright (C) 2011 secunet Security Networks AG
 * Copyright (C) 2011 Steffen Klassert <steffen.klassert@secunet.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 */

/* Netlink configuration messages.  */
enum {
	CRYPTO_MSG_BASE = 0x10,
	CRYPTO_MSG_NEWALG = 0x10,
	CRYPTO_MSG_DELALG,
	CRYPTO_MSG_UPDATEALG,
	CRYPTO_MSG_GETALG,
	__CRYPTO_MSG_MAX
};
#define CRYPTO_MSG_MAX (__CRYPTO_MSG_MAX - 1)
#define CRYPTO_NR_MSGTYPES (CRYPTO_MSG_MAX + 1 - CRYPTO_MSG_BASE)

#define CRYPTO_MAX_NAME CRYPTO_MAX_ALG_NAME

/* Netlink message attributes.  */
enum crypto_attr_type_t {
	CRYPTOCFGA_UNSPEC,
	CRYPTOCFGA_PRIORITY_VAL,	/* __u32 */
	__CRYPTOCFGA_MAX

#define CRYPTOCFGA_MAX (__CRYPTOCFGA_MAX - 1)
};

struct crypto_user_alg {
	char cru_name[CRYPTO_MAX_ALG_NAME];
	char cru_driver_name[CRYPTO_MAX_ALG_NAME];
	char cru_module_name[CRYPTO_MAX_ALG_NAME];
	__u32 cru_type;
	__u32 cru_mask;
	__u32 cru_refcnt;
	__u32 cru_flags;
};
