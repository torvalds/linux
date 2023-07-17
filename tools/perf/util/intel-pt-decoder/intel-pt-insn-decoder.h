/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * intel_pt_insn_decoder.h: Intel Processor Trace support
 * Copyright (c) 2013-2014, Intel Corporation.
 */

#ifndef INCLUDE__INTEL_PT_INSN_DECODER_H__
#define INCLUDE__INTEL_PT_INSN_DECODER_H__

#include <stddef.h>
#include <stdint.h>

#define INTEL_PT_INSN_DESC_MAX		32
#define INTEL_PT_INSN_BUF_SZ		16

enum intel_pt_insn_op {
	INTEL_PT_OP_OTHER,
	INTEL_PT_OP_CALL,
	INTEL_PT_OP_RET,
	INTEL_PT_OP_JCC,
	INTEL_PT_OP_JMP,
	INTEL_PT_OP_LOOP,
	INTEL_PT_OP_IRET,
	INTEL_PT_OP_INT,
	INTEL_PT_OP_SYSCALL,
	INTEL_PT_OP_SYSRET,
	INTEL_PT_OP_VMENTRY,
	INTEL_PT_OP_ERETS,
	INTEL_PT_OP_ERETU,
};

enum intel_pt_insn_branch {
	INTEL_PT_BR_NO_BRANCH,
	INTEL_PT_BR_INDIRECT,
	INTEL_PT_BR_CONDITIONAL,
	INTEL_PT_BR_UNCONDITIONAL,
};

struct intel_pt_insn {
	enum intel_pt_insn_op		op;
	enum intel_pt_insn_branch	branch;
	bool				emulated_ptwrite;
	int				length;
	int32_t				rel;
	unsigned char			buf[INTEL_PT_INSN_BUF_SZ];
};

int intel_pt_get_insn(const unsigned char *buf, size_t len, int x86_64,
		      struct intel_pt_insn *intel_pt_insn);

const char *intel_pt_insn_name(enum intel_pt_insn_op op);

int intel_pt_insn_desc(const struct intel_pt_insn *intel_pt_insn, char *buf,
		       size_t buf_len);

int intel_pt_insn_type(enum intel_pt_insn_op op);

#endif
