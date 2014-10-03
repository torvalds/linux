/* PKCS#7 crypto data parser
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

struct key;
struct pkcs7_message;

/*
 * pkcs7_parser.c
 */
extern struct pkcs7_message *pkcs7_parse_message(const void *data,
						 size_t datalen);
extern void pkcs7_free_message(struct pkcs7_message *pkcs7);

extern int pkcs7_get_content_data(const struct pkcs7_message *pkcs7,
				  const void **_data, size_t *_datalen,
				  bool want_wrapper);

/*
 * pkcs7_trust.c
 */
extern int pkcs7_validate_trust(struct pkcs7_message *pkcs7,
				struct key *trust_keyring,
				bool *_trusted);

/*
 * pkcs7_verify.c
 */
extern int pkcs7_verify(struct pkcs7_message *pkcs7);
