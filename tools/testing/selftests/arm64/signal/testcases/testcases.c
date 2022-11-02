// SPDX-License-Identifier: GPL-2.0
/* Copyright (C) 2019 ARM Limited */
#include "testcases.h"

struct _aarch64_ctx *get_header(struct _aarch64_ctx *head, uint32_t magic,
				size_t resv_sz, size_t *offset)
{
	size_t offs = 0;
	struct _aarch64_ctx *found = NULL;

	if (!head || resv_sz < HDR_SZ)
		return found;

	while (offs <= resv_sz - HDR_SZ &&
	       head->magic != magic && head->magic) {
		offs += head->size;
		head = GET_RESV_NEXT_HEAD(head);
	}
	if (head->magic == magic) {
		found = head;
		if (offset)
			*offset = offs;
	}

	return found;
}

bool validate_extra_context(struct extra_context *extra, char **err)
{
	struct _aarch64_ctx *term;

	if (!extra || !err)
		return false;

	fprintf(stderr, "Validating EXTRA...\n");
	term = GET_RESV_NEXT_HEAD(&extra->head);
	if (!term || term->magic || term->size) {
		*err = "Missing terminator after EXTRA context";
		return false;
	}
	if (extra->datap & 0x0fUL)
		*err = "Extra DATAP misaligned";
	else if (extra->size & 0x0fUL)
		*err = "Extra SIZE misaligned";
	else if (extra->datap != (uint64_t)term + sizeof(*term))
		*err = "Extra DATAP misplaced (not contiguous)";
	if (*err)
		return false;

	return true;
}

bool validate_sve_context(struct sve_context *sve, char **err)
{
	/* Size will be rounded up to a multiple of 16 bytes */
	size_t regs_size
		= ((SVE_SIG_CONTEXT_SIZE(sve_vq_from_vl(sve->vl)) + 15) / 16) * 16;

	if (!sve || !err)
		return false;

	/* Either a bare sve_context or a sve_context followed by regs data */
	if ((sve->head.size != sizeof(struct sve_context)) &&
	    (sve->head.size != regs_size)) {
		*err = "bad size for SVE context";
		return false;
	}

	if (!sve_vl_valid(sve->vl)) {
		*err = "SVE VL invalid";

		return false;
	}

	return true;
}

bool validate_reserved(ucontext_t *uc, size_t resv_sz, char **err)
{
	bool terminated = false;
	size_t offs = 0;
	int flags = 0;
	struct extra_context *extra = NULL;
	struct sve_context *sve = NULL;
	struct _aarch64_ctx *head =
		(struct _aarch64_ctx *)uc->uc_mcontext.__reserved;

	if (!err)
		return false;
	/* Walk till the end terminator verifying __reserved contents */
	while (head && !terminated && offs < resv_sz) {
		if ((uint64_t)head & 0x0fUL) {
			*err = "Misaligned HEAD";
			return false;
		}

		switch (head->magic) {
		case 0:
			if (head->size)
				*err = "Bad size for terminator";
			else
				terminated = true;
			break;
		case FPSIMD_MAGIC:
			if (flags & FPSIMD_CTX)
				*err = "Multiple FPSIMD_MAGIC";
			else if (head->size !=
				 sizeof(struct fpsimd_context))
				*err = "Bad size for fpsimd_context";
			flags |= FPSIMD_CTX;
			break;
		case ESR_MAGIC:
			if (head->size != sizeof(struct esr_context))
				*err = "Bad size for esr_context";
			break;
		case SVE_MAGIC:
			if (flags & SVE_CTX)
				*err = "Multiple SVE_MAGIC";
			/* Size is validated in validate_sve_context() */
			sve = (struct sve_context *)head;
			flags |= SVE_CTX;
			break;
		case EXTRA_MAGIC:
			if (flags & EXTRA_CTX)
				*err = "Multiple EXTRA_MAGIC";
			else if (head->size !=
				 sizeof(struct extra_context))
				*err = "Bad size for extra_context";
			flags |= EXTRA_CTX;
			extra = (struct extra_context *)head;
			break;
		case KSFT_BAD_MAGIC:
			/*
			 * This is a BAD magic header defined
			 * artificially by a testcase and surely
			 * unknown to the Kernel parse_user_sigframe().
			 * It MUST cause a Kernel induced SEGV
			 */
			*err = "BAD MAGIC !";
			break;
		default:
			/*
			 * A still unknown Magic: potentially freshly added
			 * to the Kernel code and still unknown to the
			 * tests.
			 */
			fprintf(stdout,
				"SKIP Unknown MAGIC: 0x%X - Is KSFT arm64/signal up to date ?\n",
				head->magic);
			break;
		}

		if (*err)
			return false;

		offs += head->size;
		if (resv_sz < offs + sizeof(*head)) {
			*err = "HEAD Overrun";
			return false;
		}

		if (flags & EXTRA_CTX)
			if (!validate_extra_context(extra, err))
				return false;
		if (flags & SVE_CTX)
			if (!validate_sve_context(sve, err))
				return false;

		head = GET_RESV_NEXT_HEAD(head);
	}

	if (terminated && !(flags & FPSIMD_CTX)) {
		*err = "Missing FPSIMD";
		return false;
	}

	return true;
}

/*
 * This function walks through the records inside the provided reserved area
 * trying to find enough space to fit @need_sz bytes: if not enough space is
 * available and an extra_context record is present, it throws away the
 * extra_context record.
 *
 * It returns a pointer to a new header where it is possible to start storing
 * our need_sz bytes.
 *
 * @shead: points to the start of reserved area
 * @need_sz: needed bytes
 * @resv_sz: reserved area size in bytes
 * @offset: if not null, this will be filled with the offset of the return
 *	    head pointer from @shead
 *
 * @return: pointer to a new head where to start storing need_sz bytes, or
 *	    NULL if space could not be made available.
 */
struct _aarch64_ctx *get_starting_head(struct _aarch64_ctx *shead,
				       size_t need_sz, size_t resv_sz,
				       size_t *offset)
{
	size_t offs = 0;
	struct _aarch64_ctx *head;

	head = get_terminator(shead, resv_sz, &offs);
	/* not found a terminator...no need to update offset if any */
	if (!head)
		return head;
	if (resv_sz - offs < need_sz) {
		fprintf(stderr, "Low on space:%zd. Discarding extra_context.\n",
			resv_sz - offs);
		head = get_header(shead, EXTRA_MAGIC, resv_sz, &offs);
		if (!head || resv_sz - offs < need_sz) {
			fprintf(stderr,
				"Failed to reclaim space on sigframe.\n");
			return NULL;
		}
	}

	fprintf(stderr, "Available space:%zd\n", resv_sz - offs);
	if (offset)
		*offset = offs;
	return head;
}
