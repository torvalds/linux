/* $OpenBSD: modes_local.h,v 1.7 2025/07/13 06:01:33 jsing Exp $ */
/* ====================================================================
 * Copyright (c) 2010 The OpenSSL Project.  All rights reserved.
 *
 * Redistribution and use is governed by OpenSSL license.
 * ====================================================================
 */

#include <endian.h>

#include <openssl/opensslconf.h>

#include <openssl/modes.h>

__BEGIN_HIDDEN_DECLS

#if defined(_LP64)
#define U64(C) C##UL
#else
#define U64(C) C##ULL
#endif

/* GCM definitions */

typedef struct {
	uint64_t hi, lo;
} u128;

struct gcm128_context {
	/* Following 6 names follow names in GCM specification */
	union {
		uint64_t u[2];
		uint32_t d[4];
		uint8_t c[16];
		size_t t[16/sizeof(size_t)];
	} Yi, EKi, EK0, len, Xi, H;
	/* Relative position of Xi, H and pre-computed Htable is used
	 * in some assembler modules, i.e. don't change the order! */
	u128 Htable[16];
	void (*gmult)(uint64_t Xi[2], const u128 Htable[16]);
	void (*ghash)(uint64_t Xi[2], const u128 Htable[16], const uint8_t *inp,
	    size_t len);
	unsigned int mres, ares;
	block128_f block;
	void *key;
};

struct xts128_context {
	const void *key1, *key2;
	block128_f block1, block2;
};

struct ccm128_context {
	union {
		uint64_t u[2];
		uint8_t c[16];
	} nonce, cmac;
	uint64_t blocks;
	block128_f block;
	void *key;
};

__END_HIDDEN_DECLS
