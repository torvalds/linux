/* SPDX-License-Identifier: GPL-2.0 */

#ifndef PLATFORM_CERTS_INTERNAL_H
#define PLATFORM_CERTS_INTERNAL_H

#include <linux/efi.h>

void blacklist_hash(const char *source, const void *data,
		    size_t len, const char *type,
		    size_t type_len);

/*
 * Blacklist an X509 TBS hash.
 */
void blacklist_x509_tbs(const char *source, const void *data, size_t len);

/*
 * Blacklist the hash of an executable.
 */
void blacklist_binary(const char *source, const void *data, size_t len);

/*
 * Return the handler for particular signature list types found in the db.
 */
efi_element_handler_t get_handler_for_db(const efi_guid_t *sig_type);

/*
 * Return the handler for particular signature list types found in the mok.
 */
efi_element_handler_t get_handler_for_mok(const efi_guid_t *sig_type);

/*
 * Return the handler for particular signature list types found in the dbx.
 */
efi_element_handler_t get_handler_for_dbx(const efi_guid_t *sig_type);

#endif
