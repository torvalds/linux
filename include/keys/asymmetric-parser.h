/* Asymmetric public-key cryptography data parser
 *
 * See Documentation/crypto/asymmetric-keys.txt
 *
 * Copyright (C) 2012 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef _KEYS_ASYMMETRIC_PARSER_H
#define _KEYS_ASYMMETRIC_PARSER_H

/*
 * Key data parser.  Called during key instantiation.
 */
struct asymmetric_key_parser {
	struct list_head	link;
	struct module		*owner;
	const char		*name;

	/* Attempt to parse a key from the data blob passed to add_key() or
	 * keyctl_instantiate().  Should also generate a proposed description
	 * that the caller can optionally use for the key.
	 *
	 * Return EBADMSG if not recognised.
	 */
	int (*parse)(struct key_preparsed_payload *prep);
};

extern int register_asymmetric_key_parser(struct asymmetric_key_parser *);
extern void unregister_asymmetric_key_parser(struct asymmetric_key_parser *);

#endif /* _KEYS_ASYMMETRIC_PARSER_H */
