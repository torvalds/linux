/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2019 ARM Limited */
#ifndef __TESTCASES_H__
#define __TESTCASES_H__

#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <ucontext.h>
#include <signal.h>

/* Architecture specific sigframe definitions */
#include <asm/sigcontext.h>

#define FPSIMD_CTX	(1 << 0)
#define SVE_CTX		(1 << 1)
#define ZA_CTX		(1 << 2)
#define EXTRA_CTX	(1 << 3)

#define KSFT_BAD_MAGIC	0xdeadbeef

#define HDR_SZ \
	sizeof(struct _aarch64_ctx)

#define GET_SF_RESV_HEAD(sf) \
	(struct _aarch64_ctx *)(&(sf).uc.uc_mcontext.__reserved)

#define GET_SF_RESV_SIZE(sf) \
	sizeof((sf).uc.uc_mcontext.__reserved)

#define GET_UCP_RESV_SIZE(ucp) \
	sizeof((ucp)->uc_mcontext.__reserved)

#define ASSERT_BAD_CONTEXT(uc) do {					\
	char *err = NULL;						\
	if (!validate_reserved((uc), GET_UCP_RESV_SIZE((uc)), &err)) {	\
		if (err)						\
			fprintf(stderr,					\
				"Using badly built context - ERR: %s\n",\
				err);					\
	} else {							\
		abort();						\
	}								\
} while (0)

#define ASSERT_GOOD_CONTEXT(uc) do {					 \
	char *err = NULL;						 \
	if (!validate_reserved((uc), GET_UCP_RESV_SIZE((uc)), &err)) {	 \
		if (err)						 \
			fprintf(stderr,					 \
				"Detected BAD context - ERR: %s\n", err);\
		abort();						 \
	} else {							 \
		fprintf(stderr, "uc context validated.\n");		 \
	}								 \
} while (0)

/*
 * A simple record-walker for __reserved area: it walks through assuming
 * only to find a proper struct __aarch64_ctx header descriptor.
 *
 * Instead it makes no assumptions on the content and ordering of the
 * records, any needed bounds checking must be enforced by the caller
 * if wanted: this way can be used by caller on any maliciously built bad
 * contexts.
 *
 * head->size accounts both for payload and header _aarch64_ctx size !
 */
#define GET_RESV_NEXT_HEAD(h) \
	(struct _aarch64_ctx *)((char *)(h) + (h)->size)

struct fake_sigframe {
	siginfo_t	info;
	ucontext_t	uc;
};


bool validate_reserved(ucontext_t *uc, size_t resv_sz, char **err);

bool validate_extra_context(struct extra_context *extra, char **err);

struct _aarch64_ctx *get_header(struct _aarch64_ctx *head, uint32_t magic,
				size_t resv_sz, size_t *offset);

static inline struct _aarch64_ctx *get_terminator(struct _aarch64_ctx *head,
						  size_t resv_sz,
						  size_t *offset)
{
	return get_header(head, 0, resv_sz, offset);
}

static inline void write_terminator_record(struct _aarch64_ctx *tail)
{
	if (tail) {
		tail->magic = 0;
		tail->size = 0;
	}
}

struct _aarch64_ctx *get_starting_head(struct _aarch64_ctx *shead,
				       size_t need_sz, size_t resv_sz,
				       size_t *offset);
#endif
