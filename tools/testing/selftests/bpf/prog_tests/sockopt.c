// SPDX-License-Identifier: GPL-2.0
#include <test_progs.h>
#include "cgroup_helpers.h"

static char bpf_log_buf[4096];
static bool verbose;

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

enum sockopt_test_error {
	OK = 0,
	DENY_LOAD,
	DENY_ATTACH,
	EOPNOTSUPP_GETSOCKOPT,
	EPERM_GETSOCKOPT,
	EFAULT_GETSOCKOPT,
	EPERM_SETSOCKOPT,
	EFAULT_SETSOCKOPT,
};

static struct sockopt_test {
	const char			*descr;
	const struct bpf_insn		insns[64];
	enum bpf_attach_type		attach_type;
	enum bpf_attach_type		expected_attach_type;

	int				set_optname;
	int				set_level;
	const char			set_optval[64];
	socklen_t			set_optlen;

	int				get_optname;
	int				get_level;
	const char			get_optval[64];
	socklen_t			get_optlen;
	socklen_t			get_optlen_ret;

	enum sockopt_test_error		error;
} tests[] = {

	/* ==================== getsockopt ====================  */

	{
		.descr = "getsockopt: no expected_attach_type",
		.insns = {
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),

		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = 0,
		.error = DENY_LOAD,
	},
	{
		.descr = "getsockopt: wrong expected_attach_type",
		.insns = {
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),

		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,
		.error = DENY_ATTACH,
	},
	{
		.descr = "getsockopt: bypass bpf hook",
		.insns = {
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.get_level = SOL_IP,
		.set_level = SOL_IP,

		.get_optname = IP_TOS,
		.set_optname = IP_TOS,

		.set_optval = { 1 << 3 },
		.set_optlen = 1,

		.get_optval = { 1 << 3 },
		.get_optlen = 1,
	},
	{
		.descr = "getsockopt: return EPERM from bpf hook",
		.insns = {
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.get_level = SOL_IP,
		.get_optname = IP_TOS,

		.get_optlen = 1,
		.error = EPERM_GETSOCKOPT,
	},
	{
		.descr = "getsockopt: no optval bounds check, deny loading",
		.insns = {
			/* r6 = ctx->optval */
			BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval)),

			/* ctx->optval[0] = 0x80 */
			BPF_MOV64_IMM(BPF_REG_0, 0x80),
			BPF_STX_MEM(BPF_W, BPF_REG_6, BPF_REG_0, 0),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,
		.error = DENY_LOAD,
	},
	{
		.descr = "getsockopt: read ctx->level",
		.insns = {
			/* r6 = ctx->level */
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, level)),

			/* if (ctx->level == 123) { */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_6, 123, 4),
			/* ctx->retval = 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, retval)),
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),
			/* } else { */
			/* return 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			/* } */
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.get_level = 123,

		.get_optlen = 1,
	},
	{
		.descr = "getsockopt: deny writing to ctx->level",
		.insns = {
			/* ctx->level = 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, level)),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.error = DENY_LOAD,
	},
	{
		.descr = "getsockopt: read ctx->optname",
		.insns = {
			/* r6 = ctx->optname */
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optname)),

			/* if (ctx->optname == 123) { */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_6, 123, 4),
			/* ctx->retval = 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, retval)),
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),
			/* } else { */
			/* return 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			/* } */
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.get_optname = 123,

		.get_optlen = 1,
	},
	{
		.descr = "getsockopt: read ctx->retval",
		.insns = {
			/* r6 = ctx->retval */
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, retval)),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.get_level = SOL_IP,
		.get_optname = IP_TOS,
		.get_optlen = 1,
	},
	{
		.descr = "getsockopt: deny writing to ctx->optname",
		.insns = {
			/* ctx->optname = 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optname)),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.error = DENY_LOAD,
	},
	{
		.descr = "getsockopt: read ctx->optlen",
		.insns = {
			/* r6 = ctx->optlen */
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optlen)),

			/* if (ctx->optlen == 64) { */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_6, 64, 4),
			/* ctx->retval = 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, retval)),
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),
			/* } else { */
			/* return 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			/* } */
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.get_optlen = 64,
	},
	{
		.descr = "getsockopt: deny bigger ctx->optlen",
		.insns = {
			/* ctx->optlen = 65 */
			BPF_MOV64_IMM(BPF_REG_0, 65),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optlen)),

			/* ctx->retval = 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, retval)),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.get_optlen = 64,

		.error = EFAULT_GETSOCKOPT,
	},
	{
		.descr = "getsockopt: ignore >PAGE_SIZE optlen",
		.insns = {
			/* write 0xFF to the first optval byte */

			/* r6 = ctx->optval */
			BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval)),
			/* r2 = ctx->optval */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_6),
			/* r6 = ctx->optval + 1 */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, 1),

			/* r7 = ctx->optval_end */
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval_end)),

			/* if (ctx->optval + 1 <= ctx->optval_end) { */
			BPF_JMP_REG(BPF_JGT, BPF_REG_6, BPF_REG_7, 1),
			/* ctx->optval[0] = 0xF0 */
			BPF_ST_MEM(BPF_B, BPF_REG_2, 0, 0xFF),
			/* } */

			/* retval changes are ignored */
			/* ctx->retval = 5 */
			BPF_MOV64_IMM(BPF_REG_0, 5),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, retval)),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.get_level = 1234,
		.get_optname = 5678,
		.get_optval = {}, /* the changes are ignored */
		.get_optlen = PAGE_SIZE + 1,
		.error = EOPNOTSUPP_GETSOCKOPT,
	},
	{
		.descr = "getsockopt: support smaller ctx->optlen",
		.insns = {
			/* ctx->optlen = 32 */
			BPF_MOV64_IMM(BPF_REG_0, 32),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optlen)),
			/* ctx->retval = 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, retval)),
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.get_optlen = 64,
		.get_optlen_ret = 32,
	},
	{
		.descr = "getsockopt: deny writing to ctx->optval",
		.insns = {
			/* ctx->optval = 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optval)),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.error = DENY_LOAD,
	},
	{
		.descr = "getsockopt: deny writing to ctx->optval_end",
		.insns = {
			/* ctx->optval_end = 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optval_end)),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.error = DENY_LOAD,
	},
	{
		.descr = "getsockopt: rewrite value",
		.insns = {
			/* r6 = ctx->optval */
			BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval)),
			/* r2 = ctx->optval */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_6),
			/* r6 = ctx->optval + 1 */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, 1),

			/* r7 = ctx->optval_end */
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval_end)),

			/* if (ctx->optval + 1 <= ctx->optval_end) { */
			BPF_JMP_REG(BPF_JGT, BPF_REG_6, BPF_REG_7, 1),
			/* ctx->optval[0] = 0xF0 */
			BPF_ST_MEM(BPF_B, BPF_REG_2, 0, 0xF0),
			/* } */

			/* ctx->retval = 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, retval)),

			/* return 1*/
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_GETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,

		.get_level = SOL_IP,
		.get_optname = IP_TOS,

		.get_optval = { 0xF0 },
		.get_optlen = 1,
	},

	/* ==================== setsockopt ====================  */

	{
		.descr = "setsockopt: no expected_attach_type",
		.insns = {
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),

		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = 0,
		.error = DENY_LOAD,
	},
	{
		.descr = "setsockopt: wrong expected_attach_type",
		.insns = {
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),

		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_GETSOCKOPT,
		.error = DENY_ATTACH,
	},
	{
		.descr = "setsockopt: bypass bpf hook",
		.insns = {
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.get_level = SOL_IP,
		.set_level = SOL_IP,

		.get_optname = IP_TOS,
		.set_optname = IP_TOS,

		.set_optval = { 1 << 3 },
		.set_optlen = 1,

		.get_optval = { 1 << 3 },
		.get_optlen = 1,
	},
	{
		.descr = "setsockopt: return EPERM from bpf hook",
		.insns = {
			/* return 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.set_level = SOL_IP,
		.set_optname = IP_TOS,

		.set_optlen = 1,
		.error = EPERM_SETSOCKOPT,
	},
	{
		.descr = "setsockopt: no optval bounds check, deny loading",
		.insns = {
			/* r6 = ctx->optval */
			BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval)),

			/* r0 = ctx->optval[0] */
			BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_6, 0),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,
		.error = DENY_LOAD,
	},
	{
		.descr = "setsockopt: read ctx->level",
		.insns = {
			/* r6 = ctx->level */
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, level)),

			/* if (ctx->level == 123) { */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_6, 123, 4),
			/* ctx->optlen = -1 */
			BPF_MOV64_IMM(BPF_REG_0, -1),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optlen)),
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),
			/* } else { */
			/* return 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			/* } */
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.set_level = 123,

		.set_optlen = 1,
	},
	{
		.descr = "setsockopt: allow changing ctx->level",
		.insns = {
			/* ctx->level = SOL_IP */
			BPF_MOV64_IMM(BPF_REG_0, SOL_IP),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, level)),
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.get_level = SOL_IP,
		.set_level = 234, /* should be rewritten to SOL_IP */

		.get_optname = IP_TOS,
		.set_optname = IP_TOS,

		.set_optval = { 1 << 3 },
		.set_optlen = 1,
		.get_optval = { 1 << 3 },
		.get_optlen = 1,
	},
	{
		.descr = "setsockopt: read ctx->optname",
		.insns = {
			/* r6 = ctx->optname */
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optname)),

			/* if (ctx->optname == 123) { */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_6, 123, 4),
			/* ctx->optlen = -1 */
			BPF_MOV64_IMM(BPF_REG_0, -1),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optlen)),
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),
			/* } else { */
			/* return 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			/* } */
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.set_optname = 123,

		.set_optlen = 1,
	},
	{
		.descr = "setsockopt: allow changing ctx->optname",
		.insns = {
			/* ctx->optname = IP_TOS */
			BPF_MOV64_IMM(BPF_REG_0, IP_TOS),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optname)),
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.get_level = SOL_IP,
		.set_level = SOL_IP,

		.get_optname = IP_TOS,
		.set_optname = 456, /* should be rewritten to IP_TOS */

		.set_optval = { 1 << 3 },
		.set_optlen = 1,
		.get_optval = { 1 << 3 },
		.get_optlen = 1,
	},
	{
		.descr = "setsockopt: read ctx->optlen",
		.insns = {
			/* r6 = ctx->optlen */
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optlen)),

			/* if (ctx->optlen == 64) { */
			BPF_JMP_IMM(BPF_JNE, BPF_REG_6, 64, 4),
			/* ctx->optlen = -1 */
			BPF_MOV64_IMM(BPF_REG_0, -1),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optlen)),
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),
			/* } else { */
			/* return 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			/* } */
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.set_optlen = 64,
	},
	{
		.descr = "setsockopt: ctx->optlen == -1 is ok",
		.insns = {
			/* ctx->optlen = -1 */
			BPF_MOV64_IMM(BPF_REG_0, -1),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optlen)),
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.set_optlen = 64,
	},
	{
		.descr = "setsockopt: deny ctx->optlen < 0 (except -1)",
		.insns = {
			/* ctx->optlen = -2 */
			BPF_MOV64_IMM(BPF_REG_0, -2),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optlen)),
			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.set_optlen = 4,

		.error = EFAULT_SETSOCKOPT,
	},
	{
		.descr = "setsockopt: deny ctx->optlen > input optlen",
		.insns = {
			/* ctx->optlen = 65 */
			BPF_MOV64_IMM(BPF_REG_0, 65),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optlen)),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.set_optlen = 64,

		.error = EFAULT_SETSOCKOPT,
	},
	{
		.descr = "setsockopt: ignore >PAGE_SIZE optlen",
		.insns = {
			/* write 0xFF to the first optval byte */

			/* r6 = ctx->optval */
			BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval)),
			/* r2 = ctx->optval */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_6),
			/* r6 = ctx->optval + 1 */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, 1),

			/* r7 = ctx->optval_end */
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval_end)),

			/* if (ctx->optval + 1 <= ctx->optval_end) { */
			BPF_JMP_REG(BPF_JGT, BPF_REG_6, BPF_REG_7, 1),
			/* ctx->optval[0] = 0xF0 */
			BPF_ST_MEM(BPF_B, BPF_REG_2, 0, 0xF0),
			/* } */

			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.set_level = SOL_IP,
		.set_optname = IP_TOS,
		.set_optval = {},
		.set_optlen = PAGE_SIZE + 1,

		.get_level = SOL_IP,
		.get_optname = IP_TOS,
		.get_optval = {}, /* the changes are ignored */
		.get_optlen = 4,
	},
	{
		.descr = "setsockopt: allow changing ctx->optlen within bounds",
		.insns = {
			/* r6 = ctx->optval */
			BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval)),
			/* r2 = ctx->optval */
			BPF_MOV64_REG(BPF_REG_2, BPF_REG_6),
			/* r6 = ctx->optval + 1 */
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_6, 1),

			/* r7 = ctx->optval_end */
			BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval_end)),

			/* if (ctx->optval + 1 <= ctx->optval_end) { */
			BPF_JMP_REG(BPF_JGT, BPF_REG_6, BPF_REG_7, 1),
			/* ctx->optval[0] = 1 << 3 */
			BPF_ST_MEM(BPF_B, BPF_REG_2, 0, 1 << 3),
			/* } */

			/* ctx->optlen = 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optlen)),

			/* return 1*/
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.get_level = SOL_IP,
		.set_level = SOL_IP,

		.get_optname = IP_TOS,
		.set_optname = IP_TOS,

		.set_optval = { 1, 1, 1, 1 },
		.set_optlen = 4,
		.get_optval = { 1 << 3 },
		.get_optlen = 1,
	},
	{
		.descr = "setsockopt: deny write ctx->retval",
		.insns = {
			/* ctx->retval = 0 */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			BPF_STX_MEM(BPF_W, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, retval)),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.error = DENY_LOAD,
	},
	{
		.descr = "setsockopt: deny read ctx->retval",
		.insns = {
			/* r6 = ctx->retval */
			BPF_LDX_MEM(BPF_W, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, retval)),

			/* return 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.error = DENY_LOAD,
	},
	{
		.descr = "setsockopt: deny writing to ctx->optval",
		.insns = {
			/* ctx->optval = 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optval)),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.error = DENY_LOAD,
	},
	{
		.descr = "setsockopt: deny writing to ctx->optval_end",
		.insns = {
			/* ctx->optval_end = 1 */
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_STX_MEM(BPF_DW, BPF_REG_1, BPF_REG_0,
				    offsetof(struct bpf_sockopt, optval_end)),
			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.error = DENY_LOAD,
	},
	{
		.descr = "setsockopt: allow IP_TOS <= 128",
		.insns = {
			/* r6 = ctx->optval */
			BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval)),
			/* r7 = ctx->optval + 1 */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_6),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, 1),

			/* r8 = ctx->optval_end */
			BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval_end)),

			/* if (ctx->optval + 1 <= ctx->optval_end) { */
			BPF_JMP_REG(BPF_JGT, BPF_REG_7, BPF_REG_8, 4),

			/* r9 = ctx->optval[0] */
			BPF_LDX_MEM(BPF_B, BPF_REG_9, BPF_REG_6, 0),

			/* if (ctx->optval[0] < 128) */
			BPF_JMP_IMM(BPF_JGT, BPF_REG_9, 128, 2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),
			/* } */

			/* } else { */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			/* } */

			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.get_level = SOL_IP,
		.set_level = SOL_IP,

		.get_optname = IP_TOS,
		.set_optname = IP_TOS,

		.set_optval = { 0x80 },
		.set_optlen = 1,
		.get_optval = { 0x80 },
		.get_optlen = 1,
	},
	{
		.descr = "setsockopt: deny IP_TOS > 128",
		.insns = {
			/* r6 = ctx->optval */
			BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval)),
			/* r7 = ctx->optval + 1 */
			BPF_MOV64_REG(BPF_REG_7, BPF_REG_6),
			BPF_ALU64_IMM(BPF_ADD, BPF_REG_7, 1),

			/* r8 = ctx->optval_end */
			BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_1,
				    offsetof(struct bpf_sockopt, optval_end)),

			/* if (ctx->optval + 1 <= ctx->optval_end) { */
			BPF_JMP_REG(BPF_JGT, BPF_REG_7, BPF_REG_8, 4),

			/* r9 = ctx->optval[0] */
			BPF_LDX_MEM(BPF_B, BPF_REG_9, BPF_REG_6, 0),

			/* if (ctx->optval[0] < 128) */
			BPF_JMP_IMM(BPF_JGT, BPF_REG_9, 128, 2),
			BPF_MOV64_IMM(BPF_REG_0, 1),
			BPF_JMP_A(1),
			/* } */

			/* } else { */
			BPF_MOV64_IMM(BPF_REG_0, 0),
			/* } */

			BPF_EXIT_INSN(),
		},
		.attach_type = BPF_CGROUP_SETSOCKOPT,
		.expected_attach_type = BPF_CGROUP_SETSOCKOPT,

		.get_level = SOL_IP,
		.set_level = SOL_IP,

		.get_optname = IP_TOS,
		.set_optname = IP_TOS,

		.set_optval = { 0x81 },
		.set_optlen = 1,
		.get_optval = { 0x00 },
		.get_optlen = 1,

		.error = EPERM_SETSOCKOPT,
	},
};

static int load_prog(const struct bpf_insn *insns,
		     enum bpf_attach_type expected_attach_type)
{
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.expected_attach_type = expected_attach_type,
		.log_level = 2,
		.log_buf = bpf_log_buf,
		.log_size = sizeof(bpf_log_buf),
	);
	int fd, insns_cnt = 0;

	for (;
	     insns[insns_cnt].code != (BPF_JMP | BPF_EXIT);
	     insns_cnt++) {
	}
	insns_cnt++;

	fd = bpf_prog_load(BPF_PROG_TYPE_CGROUP_SOCKOPT, NULL, "GPL", insns, insns_cnt, &opts);
	if (verbose && fd < 0)
		fprintf(stderr, "%s\n", bpf_log_buf);

	return fd;
}

static int run_test(int cgroup_fd, struct sockopt_test *test)
{
	int sock_fd, err, prog_fd;
	void *optval = NULL;
	int ret = 0;

	prog_fd = load_prog(test->insns, test->expected_attach_type);
	if (prog_fd < 0) {
		if (test->error == DENY_LOAD)
			return 0;

		log_err("Failed to load BPF program");
		return -1;
	}

	err = bpf_prog_attach(prog_fd, cgroup_fd, test->attach_type, 0);
	if (err < 0) {
		if (test->error == DENY_ATTACH)
			goto close_prog_fd;

		log_err("Failed to attach BPF program");
		ret = -1;
		goto close_prog_fd;
	}

	sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_fd < 0) {
		log_err("Failed to create AF_INET socket");
		ret = -1;
		goto detach_prog;
	}

	if (test->set_optlen) {
		if (test->set_optlen >= PAGE_SIZE) {
			int num_pages = test->set_optlen / PAGE_SIZE;
			int remainder = test->set_optlen % PAGE_SIZE;

			test->set_optlen = num_pages * sysconf(_SC_PAGESIZE) + remainder;
		}

		err = setsockopt(sock_fd, test->set_level, test->set_optname,
				 test->set_optval, test->set_optlen);
		if (err) {
			if (errno == EPERM && test->error == EPERM_SETSOCKOPT)
				goto close_sock_fd;
			if (errno == EFAULT && test->error == EFAULT_SETSOCKOPT)
				goto free_optval;

			log_err("Failed to call setsockopt");
			ret = -1;
			goto close_sock_fd;
		}
	}

	if (test->get_optlen) {
		if (test->get_optlen >= PAGE_SIZE) {
			int num_pages = test->get_optlen / PAGE_SIZE;
			int remainder = test->get_optlen % PAGE_SIZE;

			test->get_optlen = num_pages * sysconf(_SC_PAGESIZE) + remainder;
		}

		optval = malloc(test->get_optlen);
		memset(optval, 0, test->get_optlen);
		socklen_t optlen = test->get_optlen;
		socklen_t expected_get_optlen = test->get_optlen_ret ?:
			test->get_optlen;

		err = getsockopt(sock_fd, test->get_level, test->get_optname,
				 optval, &optlen);
		if (err) {
			if (errno == EOPNOTSUPP && test->error == EOPNOTSUPP_GETSOCKOPT)
				goto free_optval;
			if (errno == EPERM && test->error == EPERM_GETSOCKOPT)
				goto free_optval;
			if (errno == EFAULT && test->error == EFAULT_GETSOCKOPT)
				goto free_optval;

			log_err("Failed to call getsockopt");
			ret = -1;
			goto free_optval;
		}

		if (optlen != expected_get_optlen) {
			errno = 0;
			log_err("getsockopt returned unexpected optlen");
			ret = -1;
			goto free_optval;
		}

		if (memcmp(optval, test->get_optval, optlen) != 0) {
			errno = 0;
			log_err("getsockopt returned unexpected optval");
			ret = -1;
			goto free_optval;
		}
	}

	ret = test->error != OK;

free_optval:
	free(optval);
close_sock_fd:
	close(sock_fd);
detach_prog:
	bpf_prog_detach2(prog_fd, cgroup_fd, test->attach_type);
close_prog_fd:
	close(prog_fd);
	return ret;
}

void test_sockopt(void)
{
	int cgroup_fd, i;

	cgroup_fd = test__join_cgroup("/sockopt");
	if (!ASSERT_GE(cgroup_fd, 0, "join_cgroup"))
		return;

	for (i = 0; i < ARRAY_SIZE(tests); i++) {
		if (!test__start_subtest(tests[i].descr))
			continue;

		ASSERT_OK(run_test(cgroup_fd, &tests[i]), tests[i].descr);
	}

	close(cgroup_fd);
}
