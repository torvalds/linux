/*
 * RSA internal helpers
 *
 * Copyright (c) 2015, Intel Corporation
 * Authors: Tadeusz Struk <tadeusz.struk@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 */
#ifndef _RSA_HELPER_
#define _RSA_HELPER_
#include <linux/mpi.h>

struct rsa_key {
	MPI n;
	MPI e;
	MPI d;
};

int rsa_parse_pub_key(struct rsa_key *rsa_key, const void *key,
		      unsigned int key_len);

int rsa_parse_priv_key(struct rsa_key *rsa_key, const void *key,
		       unsigned int key_len);

void rsa_free_key(struct rsa_key *rsa_key);

extern struct crypto_template rsa_pkcs1pad_tmpl;
#endif
