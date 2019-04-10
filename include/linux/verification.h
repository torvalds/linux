/* Signature verification
 *
 * Copyright (C) 2014 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _LINUX_VERIFICATION_H
#define _LINUX_VERIFICATION_H

/*
 * Indicate that both builtin trusted keys and secondary trusted keys
 * should be used.
 */
#define VERIFY_USE_SECONDARY_KEYRING ((struct key *)1UL)
#define VERIFY_USE_PLATFORM_KEYRING  ((struct key *)2UL)

/*
 * The use to which an asymmetric key is being put.
 */
enum key_being_used_for {
	VERIFYING_MODULE_SIGNATURE,
	VERIFYING_FIRMWARE_SIGNATURE,
	VERIFYING_KEXEC_PE_SIGNATURE,
	VERIFYING_KEY_SIGNATURE,
	VERIFYING_KEY_SELF_SIGNATURE,
	VERIFYING_UNSPECIFIED_SIGNATURE,
	NR__KEY_BEING_USED_FOR
};
extern const char *const key_being_used_for[NR__KEY_BEING_USED_FOR];

#ifdef CONFIG_SYSTEM_DATA_VERIFICATION

struct key;

extern int verify_pkcs7_signature(const void *data, size_t len,
				  const void *raw_pkcs7, size_t pkcs7_len,
				  struct key *trusted_keys,
				  enum key_being_used_for usage,
				  int (*view_content)(void *ctx,
						      const void *data, size_t len,
						      size_t asn1hdrlen),
				  void *ctx);

#ifdef CONFIG_SIGNED_PE_FILE_VERIFICATION
extern int verify_pefile_signature(const void *pebuf, unsigned pelen,
				   struct key *trusted_keys,
				   enum key_being_used_for usage);
#endif

#endif /* CONFIG_SYSTEM_DATA_VERIFICATION */
#endif /* _LINUX_VERIFY_PEFILE_H */
