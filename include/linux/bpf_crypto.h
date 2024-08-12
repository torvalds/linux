/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#ifndef _BPF_CRYPTO_H
#define _BPF_CRYPTO_H

struct bpf_crypto_type {
	void *(*alloc_tfm)(const char *algo);
	void (*free_tfm)(void *tfm);
	int (*has_algo)(const char *algo);
	int (*setkey)(void *tfm, const u8 *key, unsigned int keylen);
	int (*setauthsize)(void *tfm, unsigned int authsize);
	int (*encrypt)(void *tfm, const u8 *src, u8 *dst, unsigned int len, u8 *iv);
	int (*decrypt)(void *tfm, const u8 *src, u8 *dst, unsigned int len, u8 *iv);
	unsigned int (*ivsize)(void *tfm);
	unsigned int (*statesize)(void *tfm);
	u32 (*get_flags)(void *tfm);
	struct module *owner;
	char name[14];
};

int bpf_crypto_register_type(const struct bpf_crypto_type *type);
int bpf_crypto_unregister_type(const struct bpf_crypto_type *type);

#endif /* _BPF_CRYPTO_H */
