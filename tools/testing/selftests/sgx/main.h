/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2016-20 Intel Corporation.
 */

#ifndef MAIN_H
#define MAIN_H

struct encl_segment {
	void *src;
	off_t offset;
	size_t size;
	unsigned int prot;
	unsigned int flags;
	bool measure;
};

struct encl {
	int fd;
	void *bin;
	off_t bin_size;
	void *src;
	size_t src_size;
	size_t encl_size;
	off_t encl_base;
	unsigned int nr_segments;
	struct encl_segment *segment_tbl;
	struct sgx_secs secs;
	struct sgx_sigstruct sigstruct;
};

extern unsigned char sign_key[];
extern unsigned char sign_key_end[];

void encl_delete(struct encl *ctx);
bool encl_load(const char *path, struct encl *encl);
bool encl_measure(struct encl *encl);
bool encl_build(struct encl *encl);

int sgx_enter_enclave(void *rdi, void *rsi, long rdx, u32 function, void *r8, void *r9,
		      struct sgx_enclave_run *run);

#endif /* MAIN_H */
