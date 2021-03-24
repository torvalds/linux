// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 * Copyright (c) 2018 Covalent IO, Inc. http://covalent.io
 */
#include <uapi/linux/btf.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/bpf_verifier.h>
#include <linux/filter.h>
#include <net/netlink.h>
#include <linux/file.h>
#include <linux/vmalloc.h>
#include <linux/stringify.h>
#include <linux/bsearch.h>
#include <linux/sort.h>
#include <linux/perf_event.h>
#include <linux/ctype.h>
#include <linux/error-injection.h>
#include <linux/bpf_lsm.h>
#include <linux/btf_ids.h>

#include "disasm.h"

static const struct bpf_verifier_ops * const bpf_verifier_ops[] = {
#define BPF_PROG_TYPE(_id, _name, prog_ctx_type, kern_ctx_type) \
	[_id] = & _name ## _verifier_ops,
#define BPF_MAP_TYPE(_id, _ops)
#define BPF_LINK_TYPE(_id, _name)
#include <linux/bpf_types.h>
#undef BPF_PROG_TYPE
#undef BPF_MAP_TYPE
#undef BPF_LINK_TYPE
};

/* bpf_check() is a static code analyzer that walks eBPF program
 * instruction by instruction and updates register/stack state.
 * All paths of conditional branches are analyzed until 'bpf_exit' insn.
 *
 * The first pass is depth-first-search to check that the program is a DAG.
 * It rejects the following programs:
 * - larger than BPF_MAXINSNS insns
 * - if loop is present (detected via back-edge)
 * - unreachable insns exist (shouldn't be a forest. program = one function)
 * - out of bounds or malformed jumps
 * The second pass is all possible path descent from the 1st insn.
 * Since it's analyzing all pathes through the program, the length of the
 * analysis is limited to 64k insn, which may be hit even if total number of
 * insn is less then 4K, but there are too many branches that change stack/regs.
 * Number of 'branches to be analyzed' is limited to 1k
 *
 * On entry to each instruction, each register has a type, and the instruction
 * changes the types of the registers depending on instruction semantics.
 * If instruction is BPF_MOV64_REG(BPF_REG_1, BPF_REG_5), then type of R5 is
 * copied to R1.
 *
 * All registers are 64-bit.
 * R0 - return register
 * R1-R5 argument passing registers
 * R6-R9 callee saved registers
 * R10 - frame pointer read-only
 *
 * At the start of BPF program the register R1 contains a pointer to bpf_context
 * and has type PTR_TO_CTX.
 *
 * Verifier tracks arithmetic operations on pointers in case:
 *    BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),
 *    BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -20),
 * 1st insn copies R10 (which has FRAME_PTR) type into R1
 * and 2nd arithmetic instruction is pattern matched to recognize
 * that it wants to construct a pointer to some element within stack.
 * So after 2nd insn, the register R1 has type PTR_TO_STACK
 * (and -20 constant is saved for further stack bounds checking).
 * Meaning that this reg is a pointer to stack plus known immediate constant.
 *
 * Most of the time the registers have SCALAR_VALUE type, which
 * means the register has some value, but it's not a valid pointer.
 * (like pointer plus pointer becomes SCALAR_VALUE type)
 *
 * When verifier sees load or store instructions the type of base register
 * can be: PTR_TO_MAP_VALUE, PTR_TO_CTX, PTR_TO_STACK, PTR_TO_SOCKET. These are
 * four pointer types recognized by check_mem_access() function.
 *
 * PTR_TO_MAP_VALUE means that this register is pointing to 'map element value'
 * and the range of [ptr, ptr + map's value_size) is accessible.
 *
 * registers used to pass values to function calls are checked against
 * function argument constraints.
 *
 * ARG_PTR_TO_MAP_KEY is one of such argument constraints.
 * It means that the register type passed to this function must be
 * PTR_TO_STACK and it will be used inside the function as
 * 'pointer to map element key'
 *
 * For example the argument constraints for bpf_map_lookup_elem():
 *   .ret_type = RET_PTR_TO_MAP_VALUE_OR_NULL,
 *   .arg1_type = ARG_CONST_MAP_PTR,
 *   .arg2_type = ARG_PTR_TO_MAP_KEY,
 *
 * ret_type says that this function returns 'pointer to map elem value or null'
 * function expects 1st argument to be a const pointer to 'struct bpf_map' and
 * 2nd argument should be a pointer to stack, which will be used inside
 * the helper function as a pointer to map element key.
 *
 * On the kernel side the helper function looks like:
 * u64 bpf_map_lookup_elem(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5)
 * {
 *    struct bpf_map *map = (struct bpf_map *) (unsigned long) r1;
 *    void *key = (void *) (unsigned long) r2;
 *    void *value;
 *
 *    here kernel can access 'key' and 'map' pointers safely, knowing that
 *    [key, key + map->key_size) bytes are valid and were initialized on
 *    the stack of eBPF program.
 * }
 *
 * Corresponding eBPF program may look like:
 *    BPF_MOV64_REG(BPF_REG_2, BPF_REG_10),  // after this insn R2 type is FRAME_PTR
 *    BPF_ALU64_IMM(BPF_ADD, BPF_REG_2, -4), // after this insn R2 type is PTR_TO_STACK
 *    BPF_LD_MAP_FD(BPF_REG_1, map_fd),      // after this insn R1 type is CONST_PTR_TO_MAP
 *    BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
 * here verifier looks at prototype of map_lookup_elem() and sees:
 * .arg1_type == ARG_CONST_MAP_PTR and R1->type == CONST_PTR_TO_MAP, which is ok,
 * Now verifier knows that this map has key of R1->map_ptr->key_size bytes
 *
 * Then .arg2_type == ARG_PTR_TO_MAP_KEY and R2->type == PTR_TO_STACK, ok so far,
 * Now verifier checks that [R2, R2 + map's key_size) are within stack limits
 * and were initialized prior to this call.
 * If it's ok, then verifier allows this BPF_CALL insn and looks at
 * .ret_type which is RET_PTR_TO_MAP_VALUE_OR_NULL, so it sets
 * R0->type = PTR_TO_MAP_VALUE_OR_NULL which means bpf_map_lookup_elem() function
 * returns ether pointer to map value or NULL.
 *
 * When type PTR_TO_MAP_VALUE_OR_NULL passes through 'if (reg != 0) goto +off'
 * insn, the register holding that pointer in the true branch changes state to
 * PTR_TO_MAP_VALUE and the same register changes state to CONST_IMM in the false
 * branch. See check_cond_jmp_op().
 *
 * After the call R0 is set to return type of the function and registers R1-R5
 * are set to NOT_INIT to indicate that they are no longer readable.
 *
 * The following reference types represent a potential reference to a kernel
 * resource which, after first being allocated, must be checked and freed by
 * the BPF program:
 * - PTR_TO_SOCKET_OR_NULL, PTR_TO_SOCKET
 *
 * When the verifier sees a helper call return a reference type, it allocates a
 * pointer id for the reference and stores it in the current function state.
 * Similar to the way that PTR_TO_MAP_VALUE_OR_NULL is converted into
 * PTR_TO_MAP_VALUE, PTR_TO_SOCKET_OR_NULL becomes PTR_TO_SOCKET when the type
 * passes through a NULL-check conditional. For the branch wherein the state is
 * changed to CONST_IMM, the verifier releases the reference.
 *
 * For each helper function that allocates a reference, such as
 * bpf_sk_lookup_tcp(), there is a corresponding release function, such as
 * bpf_sk_release(). When a reference type passes into the release function,
 * the verifier also releases the reference. If any unchecked or unreleased
 * reference remains at the end of the program, the verifier rejects it.
 */

/* verifier_state + insn_idx are pushed to stack when branch is encountered */
struct bpf_verifier_stack_elem {
	/* verifer state is 'st'
	 * before processing instruction 'insn_idx'
	 * and after processing instruction 'prev_insn_idx'
	 */
	struct bpf_verifier_state st;
	int insn_idx;
	int prev_insn_idx;
	struct bpf_verifier_stack_elem *next;
	/* length of verifier log at the time this state was pushed on stack */
	u32 log_pos;
};

#define BPF_COMPLEXITY_LIMIT_JMP_SEQ	8192
#define BPF_COMPLEXITY_LIMIT_STATES	64

#define BPF_MAP_KEY_POISON	(1ULL << 63)
#define BPF_MAP_KEY_SEEN	(1ULL << 62)

#define BPF_MAP_PTR_UNPRIV	1UL
#define BPF_MAP_PTR_POISON	((void *)((0xeB9FUL << 1) +	\
					  POISON_POINTER_DELTA))
#define BPF_MAP_PTR(X)		((struct bpf_map *)((X) & ~BPF_MAP_PTR_UNPRIV))

static bool bpf_map_ptr_poisoned(const struct bpf_insn_aux_data *aux)
{
	return BPF_MAP_PTR(aux->map_ptr_state) == BPF_MAP_PTR_POISON;
}

static bool bpf_map_ptr_unpriv(const struct bpf_insn_aux_data *aux)
{
	return aux->map_ptr_state & BPF_MAP_PTR_UNPRIV;
}

static void bpf_map_ptr_store(struct bpf_insn_aux_data *aux,
			      const struct bpf_map *map, bool unpriv)
{
	BUILD_BUG_ON((unsigned long)BPF_MAP_PTR_POISON & BPF_MAP_PTR_UNPRIV);
	unpriv |= bpf_map_ptr_unpriv(aux);
	aux->map_ptr_state = (unsigned long)map |
			     (unpriv ? BPF_MAP_PTR_UNPRIV : 0UL);
}

static bool bpf_map_key_poisoned(const struct bpf_insn_aux_data *aux)
{
	return aux->map_key_state & BPF_MAP_KEY_POISON;
}

static bool bpf_map_key_unseen(const struct bpf_insn_aux_data *aux)
{
	return !(aux->map_key_state & BPF_MAP_KEY_SEEN);
}

static u64 bpf_map_key_immediate(const struct bpf_insn_aux_data *aux)
{
	return aux->map_key_state & ~(BPF_MAP_KEY_SEEN | BPF_MAP_KEY_POISON);
}

static void bpf_map_key_store(struct bpf_insn_aux_data *aux, u64 state)
{
	bool poisoned = bpf_map_key_poisoned(aux);

	aux->map_key_state = state | BPF_MAP_KEY_SEEN |
			     (poisoned ? BPF_MAP_KEY_POISON : 0ULL);
}

static bool bpf_pseudo_call(const struct bpf_insn *insn)
{
	return insn->code == (BPF_JMP | BPF_CALL) &&
	       insn->src_reg == BPF_PSEUDO_CALL;
}

struct bpf_call_arg_meta {
	struct bpf_map *map_ptr;
	bool raw_mode;
	bool pkt_access;
	int regno;
	int access_size;
	int mem_size;
	u64 msize_max_value;
	int ref_obj_id;
	int func_id;
	struct btf *btf;
	u32 btf_id;
	struct btf *ret_btf;
	u32 ret_btf_id;
};

struct btf *btf_vmlinux;

static DEFINE_MUTEX(bpf_verifier_lock);

static const struct bpf_line_info *
find_linfo(const struct bpf_verifier_env *env, u32 insn_off)
{
	const struct bpf_line_info *linfo;
	const struct bpf_prog *prog;
	u32 i, nr_linfo;

	prog = env->prog;
	nr_linfo = prog->aux->nr_linfo;

	if (!nr_linfo || insn_off >= prog->len)
		return NULL;

	linfo = prog->aux->linfo;
	for (i = 1; i < nr_linfo; i++)
		if (insn_off < linfo[i].insn_off)
			break;

	return &linfo[i - 1];
}

void bpf_verifier_vlog(struct bpf_verifier_log *log, const char *fmt,
		       va_list args)
{
	unsigned int n;

	n = vscnprintf(log->kbuf, BPF_VERIFIER_TMP_LOG_SIZE, fmt, args);

	WARN_ONCE(n >= BPF_VERIFIER_TMP_LOG_SIZE - 1,
		  "verifier log line truncated - local buffer too short\n");

	n = min(log->len_total - log->len_used - 1, n);
	log->kbuf[n] = '\0';

	if (log->level == BPF_LOG_KERNEL) {
		pr_err("BPF:%s\n", log->kbuf);
		return;
	}
	if (!copy_to_user(log->ubuf + log->len_used, log->kbuf, n + 1))
		log->len_used += n;
	else
		log->ubuf = NULL;
}

static void bpf_vlog_reset(struct bpf_verifier_log *log, u32 new_pos)
{
	char zero = 0;

	if (!bpf_verifier_log_needed(log))
		return;

	log->len_used = new_pos;
	if (put_user(zero, log->ubuf + new_pos))
		log->ubuf = NULL;
}

/* log_level controls verbosity level of eBPF verifier.
 * bpf_verifier_log_write() is used to dump the verification trace to the log,
 * so the user can figure out what's wrong with the program
 */
__printf(2, 3) void bpf_verifier_log_write(struct bpf_verifier_env *env,
					   const char *fmt, ...)
{
	va_list args;

	if (!bpf_verifier_log_needed(&env->log))
		return;

	va_start(args, fmt);
	bpf_verifier_vlog(&env->log, fmt, args);
	va_end(args);
}
EXPORT_SYMBOL_GPL(bpf_verifier_log_write);

__printf(2, 3) static void verbose(void *private_data, const char *fmt, ...)
{
	struct bpf_verifier_env *env = private_data;
	va_list args;

	if (!bpf_verifier_log_needed(&env->log))
		return;

	va_start(args, fmt);
	bpf_verifier_vlog(&env->log, fmt, args);
	va_end(args);
}

__printf(2, 3) void bpf_log(struct bpf_verifier_log *log,
			    const char *fmt, ...)
{
	va_list args;

	if (!bpf_verifier_log_needed(log))
		return;

	va_start(args, fmt);
	bpf_verifier_vlog(log, fmt, args);
	va_end(args);
}

static const char *ltrim(const char *s)
{
	while (isspace(*s))
		s++;

	return s;
}

__printf(3, 4) static void verbose_linfo(struct bpf_verifier_env *env,
					 u32 insn_off,
					 const char *prefix_fmt, ...)
{
	const struct bpf_line_info *linfo;

	if (!bpf_verifier_log_needed(&env->log))
		return;

	linfo = find_linfo(env, insn_off);
	if (!linfo || linfo == env->prev_linfo)
		return;

	if (prefix_fmt) {
		va_list args;

		va_start(args, prefix_fmt);
		bpf_verifier_vlog(&env->log, prefix_fmt, args);
		va_end(args);
	}

	verbose(env, "%s\n",
		ltrim(btf_name_by_offset(env->prog->aux->btf,
					 linfo->line_off)));

	env->prev_linfo = linfo;
}

static bool type_is_pkt_pointer(enum bpf_reg_type type)
{
	return type == PTR_TO_PACKET ||
	       type == PTR_TO_PACKET_META;
}

static bool type_is_sk_pointer(enum bpf_reg_type type)
{
	return type == PTR_TO_SOCKET ||
		type == PTR_TO_SOCK_COMMON ||
		type == PTR_TO_TCP_SOCK ||
		type == PTR_TO_XDP_SOCK;
}

static bool reg_type_not_null(enum bpf_reg_type type)
{
	return type == PTR_TO_SOCKET ||
		type == PTR_TO_TCP_SOCK ||
		type == PTR_TO_MAP_VALUE ||
		type == PTR_TO_SOCK_COMMON;
}

static bool reg_type_may_be_null(enum bpf_reg_type type)
{
	return type == PTR_TO_MAP_VALUE_OR_NULL ||
	       type == PTR_TO_SOCKET_OR_NULL ||
	       type == PTR_TO_SOCK_COMMON_OR_NULL ||
	       type == PTR_TO_TCP_SOCK_OR_NULL ||
	       type == PTR_TO_BTF_ID_OR_NULL ||
	       type == PTR_TO_MEM_OR_NULL ||
	       type == PTR_TO_RDONLY_BUF_OR_NULL ||
	       type == PTR_TO_RDWR_BUF_OR_NULL;
}

static bool reg_may_point_to_spin_lock(const struct bpf_reg_state *reg)
{
	return reg->type == PTR_TO_MAP_VALUE &&
		map_value_has_spin_lock(reg->map_ptr);
}

static bool reg_type_may_be_refcounted_or_null(enum bpf_reg_type type)
{
	return type == PTR_TO_SOCKET ||
		type == PTR_TO_SOCKET_OR_NULL ||
		type == PTR_TO_TCP_SOCK ||
		type == PTR_TO_TCP_SOCK_OR_NULL ||
		type == PTR_TO_MEM ||
		type == PTR_TO_MEM_OR_NULL;
}

static bool arg_type_may_be_refcounted(enum bpf_arg_type type)
{
	return type == ARG_PTR_TO_SOCK_COMMON;
}

static bool arg_type_may_be_null(enum bpf_arg_type type)
{
	return type == ARG_PTR_TO_MAP_VALUE_OR_NULL ||
	       type == ARG_PTR_TO_MEM_OR_NULL ||
	       type == ARG_PTR_TO_CTX_OR_NULL ||
	       type == ARG_PTR_TO_SOCKET_OR_NULL ||
	       type == ARG_PTR_TO_ALLOC_MEM_OR_NULL;
}

/* Determine whether the function releases some resources allocated by another
 * function call. The first reference type argument will be assumed to be
 * released by release_reference().
 */
static bool is_release_function(enum bpf_func_id func_id)
{
	return func_id == BPF_FUNC_sk_release ||
	       func_id == BPF_FUNC_ringbuf_submit ||
	       func_id == BPF_FUNC_ringbuf_discard;
}

static bool may_be_acquire_function(enum bpf_func_id func_id)
{
	return func_id == BPF_FUNC_sk_lookup_tcp ||
		func_id == BPF_FUNC_sk_lookup_udp ||
		func_id == BPF_FUNC_skc_lookup_tcp ||
		func_id == BPF_FUNC_map_lookup_elem ||
	        func_id == BPF_FUNC_ringbuf_reserve;
}

static bool is_acquire_function(enum bpf_func_id func_id,
				const struct bpf_map *map)
{
	enum bpf_map_type map_type = map ? map->map_type : BPF_MAP_TYPE_UNSPEC;

	if (func_id == BPF_FUNC_sk_lookup_tcp ||
	    func_id == BPF_FUNC_sk_lookup_udp ||
	    func_id == BPF_FUNC_skc_lookup_tcp ||
	    func_id == BPF_FUNC_ringbuf_reserve)
		return true;

	if (func_id == BPF_FUNC_map_lookup_elem &&
	    (map_type == BPF_MAP_TYPE_SOCKMAP ||
	     map_type == BPF_MAP_TYPE_SOCKHASH))
		return true;

	return false;
}

static bool is_ptr_cast_function(enum bpf_func_id func_id)
{
	return func_id == BPF_FUNC_tcp_sock ||
		func_id == BPF_FUNC_sk_fullsock ||
		func_id == BPF_FUNC_skc_to_tcp_sock ||
		func_id == BPF_FUNC_skc_to_tcp6_sock ||
		func_id == BPF_FUNC_skc_to_udp6_sock ||
		func_id == BPF_FUNC_skc_to_tcp_timewait_sock ||
		func_id == BPF_FUNC_skc_to_tcp_request_sock;
}

static bool is_cmpxchg_insn(const struct bpf_insn *insn)
{
	return BPF_CLASS(insn->code) == BPF_STX &&
	       BPF_MODE(insn->code) == BPF_ATOMIC &&
	       insn->imm == BPF_CMPXCHG;
}

/* string representation of 'enum bpf_reg_type' */
static const char * const reg_type_str[] = {
	[NOT_INIT]		= "?",
	[SCALAR_VALUE]		= "inv",
	[PTR_TO_CTX]		= "ctx",
	[CONST_PTR_TO_MAP]	= "map_ptr",
	[PTR_TO_MAP_VALUE]	= "map_value",
	[PTR_TO_MAP_VALUE_OR_NULL] = "map_value_or_null",
	[PTR_TO_STACK]		= "fp",
	[PTR_TO_PACKET]		= "pkt",
	[PTR_TO_PACKET_META]	= "pkt_meta",
	[PTR_TO_PACKET_END]	= "pkt_end",
	[PTR_TO_FLOW_KEYS]	= "flow_keys",
	[PTR_TO_SOCKET]		= "sock",
	[PTR_TO_SOCKET_OR_NULL] = "sock_or_null",
	[PTR_TO_SOCK_COMMON]	= "sock_common",
	[PTR_TO_SOCK_COMMON_OR_NULL] = "sock_common_or_null",
	[PTR_TO_TCP_SOCK]	= "tcp_sock",
	[PTR_TO_TCP_SOCK_OR_NULL] = "tcp_sock_or_null",
	[PTR_TO_TP_BUFFER]	= "tp_buffer",
	[PTR_TO_XDP_SOCK]	= "xdp_sock",
	[PTR_TO_BTF_ID]		= "ptr_",
	[PTR_TO_BTF_ID_OR_NULL]	= "ptr_or_null_",
	[PTR_TO_PERCPU_BTF_ID]	= "percpu_ptr_",
	[PTR_TO_MEM]		= "mem",
	[PTR_TO_MEM_OR_NULL]	= "mem_or_null",
	[PTR_TO_RDONLY_BUF]	= "rdonly_buf",
	[PTR_TO_RDONLY_BUF_OR_NULL] = "rdonly_buf_or_null",
	[PTR_TO_RDWR_BUF]	= "rdwr_buf",
	[PTR_TO_RDWR_BUF_OR_NULL] = "rdwr_buf_or_null",
};

static char slot_type_char[] = {
	[STACK_INVALID]	= '?',
	[STACK_SPILL]	= 'r',
	[STACK_MISC]	= 'm',
	[STACK_ZERO]	= '0',
};

static void print_liveness(struct bpf_verifier_env *env,
			   enum bpf_reg_liveness live)
{
	if (live & (REG_LIVE_READ | REG_LIVE_WRITTEN | REG_LIVE_DONE))
	    verbose(env, "_");
	if (live & REG_LIVE_READ)
		verbose(env, "r");
	if (live & REG_LIVE_WRITTEN)
		verbose(env, "w");
	if (live & REG_LIVE_DONE)
		verbose(env, "D");
}

static struct bpf_func_state *func(struct bpf_verifier_env *env,
				   const struct bpf_reg_state *reg)
{
	struct bpf_verifier_state *cur = env->cur_state;

	return cur->frame[reg->frameno];
}

static const char *kernel_type_name(const struct btf* btf, u32 id)
{
	return btf_name_by_offset(btf, btf_type_by_id(btf, id)->name_off);
}

static void print_verifier_state(struct bpf_verifier_env *env,
				 const struct bpf_func_state *state)
{
	const struct bpf_reg_state *reg;
	enum bpf_reg_type t;
	int i;

	if (state->frameno)
		verbose(env, " frame%d:", state->frameno);
	for (i = 0; i < MAX_BPF_REG; i++) {
		reg = &state->regs[i];
		t = reg->type;
		if (t == NOT_INIT)
			continue;
		verbose(env, " R%d", i);
		print_liveness(env, reg->live);
		verbose(env, "=%s", reg_type_str[t]);
		if (t == SCALAR_VALUE && reg->precise)
			verbose(env, "P");
		if ((t == SCALAR_VALUE || t == PTR_TO_STACK) &&
		    tnum_is_const(reg->var_off)) {
			/* reg->off should be 0 for SCALAR_VALUE */
			verbose(env, "%lld", reg->var_off.value + reg->off);
		} else {
			if (t == PTR_TO_BTF_ID ||
			    t == PTR_TO_BTF_ID_OR_NULL ||
			    t == PTR_TO_PERCPU_BTF_ID)
				verbose(env, "%s", kernel_type_name(reg->btf, reg->btf_id));
			verbose(env, "(id=%d", reg->id);
			if (reg_type_may_be_refcounted_or_null(t))
				verbose(env, ",ref_obj_id=%d", reg->ref_obj_id);
			if (t != SCALAR_VALUE)
				verbose(env, ",off=%d", reg->off);
			if (type_is_pkt_pointer(t))
				verbose(env, ",r=%d", reg->range);
			else if (t == CONST_PTR_TO_MAP ||
				 t == PTR_TO_MAP_VALUE ||
				 t == PTR_TO_MAP_VALUE_OR_NULL)
				verbose(env, ",ks=%d,vs=%d",
					reg->map_ptr->key_size,
					reg->map_ptr->value_size);
			if (tnum_is_const(reg->var_off)) {
				/* Typically an immediate SCALAR_VALUE, but
				 * could be a pointer whose offset is too big
				 * for reg->off
				 */
				verbose(env, ",imm=%llx", reg->var_off.value);
			} else {
				if (reg->smin_value != reg->umin_value &&
				    reg->smin_value != S64_MIN)
					verbose(env, ",smin_value=%lld",
						(long long)reg->smin_value);
				if (reg->smax_value != reg->umax_value &&
				    reg->smax_value != S64_MAX)
					verbose(env, ",smax_value=%lld",
						(long long)reg->smax_value);
				if (reg->umin_value != 0)
					verbose(env, ",umin_value=%llu",
						(unsigned long long)reg->umin_value);
				if (reg->umax_value != U64_MAX)
					verbose(env, ",umax_value=%llu",
						(unsigned long long)reg->umax_value);
				if (!tnum_is_unknown(reg->var_off)) {
					char tn_buf[48];

					tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
					verbose(env, ",var_off=%s", tn_buf);
				}
				if (reg->s32_min_value != reg->smin_value &&
				    reg->s32_min_value != S32_MIN)
					verbose(env, ",s32_min_value=%d",
						(int)(reg->s32_min_value));
				if (reg->s32_max_value != reg->smax_value &&
				    reg->s32_max_value != S32_MAX)
					verbose(env, ",s32_max_value=%d",
						(int)(reg->s32_max_value));
				if (reg->u32_min_value != reg->umin_value &&
				    reg->u32_min_value != U32_MIN)
					verbose(env, ",u32_min_value=%d",
						(int)(reg->u32_min_value));
				if (reg->u32_max_value != reg->umax_value &&
				    reg->u32_max_value != U32_MAX)
					verbose(env, ",u32_max_value=%d",
						(int)(reg->u32_max_value));
			}
			verbose(env, ")");
		}
	}
	for (i = 0; i < state->allocated_stack / BPF_REG_SIZE; i++) {
		char types_buf[BPF_REG_SIZE + 1];
		bool valid = false;
		int j;

		for (j = 0; j < BPF_REG_SIZE; j++) {
			if (state->stack[i].slot_type[j] != STACK_INVALID)
				valid = true;
			types_buf[j] = slot_type_char[
					state->stack[i].slot_type[j]];
		}
		types_buf[BPF_REG_SIZE] = 0;
		if (!valid)
			continue;
		verbose(env, " fp%d", (-i - 1) * BPF_REG_SIZE);
		print_liveness(env, state->stack[i].spilled_ptr.live);
		if (state->stack[i].slot_type[0] == STACK_SPILL) {
			reg = &state->stack[i].spilled_ptr;
			t = reg->type;
			verbose(env, "=%s", reg_type_str[t]);
			if (t == SCALAR_VALUE && reg->precise)
				verbose(env, "P");
			if (t == SCALAR_VALUE && tnum_is_const(reg->var_off))
				verbose(env, "%lld", reg->var_off.value + reg->off);
		} else {
			verbose(env, "=%s", types_buf);
		}
	}
	if (state->acquired_refs && state->refs[0].id) {
		verbose(env, " refs=%d", state->refs[0].id);
		for (i = 1; i < state->acquired_refs; i++)
			if (state->refs[i].id)
				verbose(env, ",%d", state->refs[i].id);
	}
	verbose(env, "\n");
}

#define COPY_STATE_FN(NAME, COUNT, FIELD, SIZE)				\
static int copy_##NAME##_state(struct bpf_func_state *dst,		\
			       const struct bpf_func_state *src)	\
{									\
	if (!src->FIELD)						\
		return 0;						\
	if (WARN_ON_ONCE(dst->COUNT < src->COUNT)) {			\
		/* internal bug, make state invalid to reject the program */ \
		memset(dst, 0, sizeof(*dst));				\
		return -EFAULT;						\
	}								\
	memcpy(dst->FIELD, src->FIELD,					\
	       sizeof(*src->FIELD) * (src->COUNT / SIZE));		\
	return 0;							\
}
/* copy_reference_state() */
COPY_STATE_FN(reference, acquired_refs, refs, 1)
/* copy_stack_state() */
COPY_STATE_FN(stack, allocated_stack, stack, BPF_REG_SIZE)
#undef COPY_STATE_FN

#define REALLOC_STATE_FN(NAME, COUNT, FIELD, SIZE)			\
static int realloc_##NAME##_state(struct bpf_func_state *state, int size, \
				  bool copy_old)			\
{									\
	u32 old_size = state->COUNT;					\
	struct bpf_##NAME##_state *new_##FIELD;				\
	int slot = size / SIZE;						\
									\
	if (size <= old_size || !size) {				\
		if (copy_old)						\
			return 0;					\
		state->COUNT = slot * SIZE;				\
		if (!size && old_size) {				\
			kfree(state->FIELD);				\
			state->FIELD = NULL;				\
		}							\
		return 0;						\
	}								\
	new_##FIELD = kmalloc_array(slot, sizeof(struct bpf_##NAME##_state), \
				    GFP_KERNEL);			\
	if (!new_##FIELD)						\
		return -ENOMEM;						\
	if (copy_old) {							\
		if (state->FIELD)					\
			memcpy(new_##FIELD, state->FIELD,		\
			       sizeof(*new_##FIELD) * (old_size / SIZE)); \
		memset(new_##FIELD + old_size / SIZE, 0,		\
		       sizeof(*new_##FIELD) * (size - old_size) / SIZE); \
	}								\
	state->COUNT = slot * SIZE;					\
	kfree(state->FIELD);						\
	state->FIELD = new_##FIELD;					\
	return 0;							\
}
/* realloc_reference_state() */
REALLOC_STATE_FN(reference, acquired_refs, refs, 1)
/* realloc_stack_state() */
REALLOC_STATE_FN(stack, allocated_stack, stack, BPF_REG_SIZE)
#undef REALLOC_STATE_FN

/* do_check() starts with zero-sized stack in struct bpf_verifier_state to
 * make it consume minimal amount of memory. check_stack_write() access from
 * the program calls into realloc_func_state() to grow the stack size.
 * Note there is a non-zero 'parent' pointer inside bpf_verifier_state
 * which realloc_stack_state() copies over. It points to previous
 * bpf_verifier_state which is never reallocated.
 */
static int realloc_func_state(struct bpf_func_state *state, int stack_size,
			      int refs_size, bool copy_old)
{
	int err = realloc_reference_state(state, refs_size, copy_old);
	if (err)
		return err;
	return realloc_stack_state(state, stack_size, copy_old);
}

/* Acquire a pointer id from the env and update the state->refs to include
 * this new pointer reference.
 * On success, returns a valid pointer id to associate with the register
 * On failure, returns a negative errno.
 */
static int acquire_reference_state(struct bpf_verifier_env *env, int insn_idx)
{
	struct bpf_func_state *state = cur_func(env);
	int new_ofs = state->acquired_refs;
	int id, err;

	err = realloc_reference_state(state, state->acquired_refs + 1, true);
	if (err)
		return err;
	id = ++env->id_gen;
	state->refs[new_ofs].id = id;
	state->refs[new_ofs].insn_idx = insn_idx;

	return id;
}

/* release function corresponding to acquire_reference_state(). Idempotent. */
static int release_reference_state(struct bpf_func_state *state, int ptr_id)
{
	int i, last_idx;

	last_idx = state->acquired_refs - 1;
	for (i = 0; i < state->acquired_refs; i++) {
		if (state->refs[i].id == ptr_id) {
			if (last_idx && i != last_idx)
				memcpy(&state->refs[i], &state->refs[last_idx],
				       sizeof(*state->refs));
			memset(&state->refs[last_idx], 0, sizeof(*state->refs));
			state->acquired_refs--;
			return 0;
		}
	}
	return -EINVAL;
}

static int transfer_reference_state(struct bpf_func_state *dst,
				    struct bpf_func_state *src)
{
	int err = realloc_reference_state(dst, src->acquired_refs, false);
	if (err)
		return err;
	err = copy_reference_state(dst, src);
	if (err)
		return err;
	return 0;
}

static void free_func_state(struct bpf_func_state *state)
{
	if (!state)
		return;
	kfree(state->refs);
	kfree(state->stack);
	kfree(state);
}

static void clear_jmp_history(struct bpf_verifier_state *state)
{
	kfree(state->jmp_history);
	state->jmp_history = NULL;
	state->jmp_history_cnt = 0;
}

static void free_verifier_state(struct bpf_verifier_state *state,
				bool free_self)
{
	int i;

	for (i = 0; i <= state->curframe; i++) {
		free_func_state(state->frame[i]);
		state->frame[i] = NULL;
	}
	clear_jmp_history(state);
	if (free_self)
		kfree(state);
}

/* copy verifier state from src to dst growing dst stack space
 * when necessary to accommodate larger src stack
 */
static int copy_func_state(struct bpf_func_state *dst,
			   const struct bpf_func_state *src)
{
	int err;

	err = realloc_func_state(dst, src->allocated_stack, src->acquired_refs,
				 false);
	if (err)
		return err;
	memcpy(dst, src, offsetof(struct bpf_func_state, acquired_refs));
	err = copy_reference_state(dst, src);
	if (err)
		return err;
	return copy_stack_state(dst, src);
}

static int copy_verifier_state(struct bpf_verifier_state *dst_state,
			       const struct bpf_verifier_state *src)
{
	struct bpf_func_state *dst;
	u32 jmp_sz = sizeof(struct bpf_idx_pair) * src->jmp_history_cnt;
	int i, err;

	if (dst_state->jmp_history_cnt < src->jmp_history_cnt) {
		kfree(dst_state->jmp_history);
		dst_state->jmp_history = kmalloc(jmp_sz, GFP_USER);
		if (!dst_state->jmp_history)
			return -ENOMEM;
	}
	memcpy(dst_state->jmp_history, src->jmp_history, jmp_sz);
	dst_state->jmp_history_cnt = src->jmp_history_cnt;

	/* if dst has more stack frames then src frame, free them */
	for (i = src->curframe + 1; i <= dst_state->curframe; i++) {
		free_func_state(dst_state->frame[i]);
		dst_state->frame[i] = NULL;
	}
	dst_state->speculative = src->speculative;
	dst_state->curframe = src->curframe;
	dst_state->active_spin_lock = src->active_spin_lock;
	dst_state->branches = src->branches;
	dst_state->parent = src->parent;
	dst_state->first_insn_idx = src->first_insn_idx;
	dst_state->last_insn_idx = src->last_insn_idx;
	for (i = 0; i <= src->curframe; i++) {
		dst = dst_state->frame[i];
		if (!dst) {
			dst = kzalloc(sizeof(*dst), GFP_KERNEL);
			if (!dst)
				return -ENOMEM;
			dst_state->frame[i] = dst;
		}
		err = copy_func_state(dst, src->frame[i]);
		if (err)
			return err;
	}
	return 0;
}

static void update_branch_counts(struct bpf_verifier_env *env, struct bpf_verifier_state *st)
{
	while (st) {
		u32 br = --st->branches;

		/* WARN_ON(br > 1) technically makes sense here,
		 * but see comment in push_stack(), hence:
		 */
		WARN_ONCE((int)br < 0,
			  "BUG update_branch_counts:branches_to_explore=%d\n",
			  br);
		if (br)
			break;
		st = st->parent;
	}
}

static int pop_stack(struct bpf_verifier_env *env, int *prev_insn_idx,
		     int *insn_idx, bool pop_log)
{
	struct bpf_verifier_state *cur = env->cur_state;
	struct bpf_verifier_stack_elem *elem, *head = env->head;
	int err;

	if (env->head == NULL)
		return -ENOENT;

	if (cur) {
		err = copy_verifier_state(cur, &head->st);
		if (err)
			return err;
	}
	if (pop_log)
		bpf_vlog_reset(&env->log, head->log_pos);
	if (insn_idx)
		*insn_idx = head->insn_idx;
	if (prev_insn_idx)
		*prev_insn_idx = head->prev_insn_idx;
	elem = head->next;
	free_verifier_state(&head->st, false);
	kfree(head);
	env->head = elem;
	env->stack_size--;
	return 0;
}

static struct bpf_verifier_state *push_stack(struct bpf_verifier_env *env,
					     int insn_idx, int prev_insn_idx,
					     bool speculative)
{
	struct bpf_verifier_state *cur = env->cur_state;
	struct bpf_verifier_stack_elem *elem;
	int err;

	elem = kzalloc(sizeof(struct bpf_verifier_stack_elem), GFP_KERNEL);
	if (!elem)
		goto err;

	elem->insn_idx = insn_idx;
	elem->prev_insn_idx = prev_insn_idx;
	elem->next = env->head;
	elem->log_pos = env->log.len_used;
	env->head = elem;
	env->stack_size++;
	err = copy_verifier_state(&elem->st, cur);
	if (err)
		goto err;
	elem->st.speculative |= speculative;
	if (env->stack_size > BPF_COMPLEXITY_LIMIT_JMP_SEQ) {
		verbose(env, "The sequence of %d jumps is too complex.\n",
			env->stack_size);
		goto err;
	}
	if (elem->st.parent) {
		++elem->st.parent->branches;
		/* WARN_ON(branches > 2) technically makes sense here,
		 * but
		 * 1. speculative states will bump 'branches' for non-branch
		 * instructions
		 * 2. is_state_visited() heuristics may decide not to create
		 * a new state for a sequence of branches and all such current
		 * and cloned states will be pointing to a single parent state
		 * which might have large 'branches' count.
		 */
	}
	return &elem->st;
err:
	free_verifier_state(env->cur_state, true);
	env->cur_state = NULL;
	/* pop all elements and return */
	while (!pop_stack(env, NULL, NULL, false));
	return NULL;
}

#define CALLER_SAVED_REGS 6
static const int caller_saved[CALLER_SAVED_REGS] = {
	BPF_REG_0, BPF_REG_1, BPF_REG_2, BPF_REG_3, BPF_REG_4, BPF_REG_5
};

static void __mark_reg_not_init(const struct bpf_verifier_env *env,
				struct bpf_reg_state *reg);

/* This helper doesn't clear reg->id */
static void ___mark_reg_known(struct bpf_reg_state *reg, u64 imm)
{
	reg->var_off = tnum_const(imm);
	reg->smin_value = (s64)imm;
	reg->smax_value = (s64)imm;
	reg->umin_value = imm;
	reg->umax_value = imm;

	reg->s32_min_value = (s32)imm;
	reg->s32_max_value = (s32)imm;
	reg->u32_min_value = (u32)imm;
	reg->u32_max_value = (u32)imm;
}

/* Mark the unknown part of a register (variable offset or scalar value) as
 * known to have the value @imm.
 */
static void __mark_reg_known(struct bpf_reg_state *reg, u64 imm)
{
	/* Clear id, off, and union(map_ptr, range) */
	memset(((u8 *)reg) + sizeof(reg->type), 0,
	       offsetof(struct bpf_reg_state, var_off) - sizeof(reg->type));
	___mark_reg_known(reg, imm);
}

static void __mark_reg32_known(struct bpf_reg_state *reg, u64 imm)
{
	reg->var_off = tnum_const_subreg(reg->var_off, imm);
	reg->s32_min_value = (s32)imm;
	reg->s32_max_value = (s32)imm;
	reg->u32_min_value = (u32)imm;
	reg->u32_max_value = (u32)imm;
}

/* Mark the 'variable offset' part of a register as zero.  This should be
 * used only on registers holding a pointer type.
 */
static void __mark_reg_known_zero(struct bpf_reg_state *reg)
{
	__mark_reg_known(reg, 0);
}

static void __mark_reg_const_zero(struct bpf_reg_state *reg)
{
	__mark_reg_known(reg, 0);
	reg->type = SCALAR_VALUE;
}

static void mark_reg_known_zero(struct bpf_verifier_env *env,
				struct bpf_reg_state *regs, u32 regno)
{
	if (WARN_ON(regno >= MAX_BPF_REG)) {
		verbose(env, "mark_reg_known_zero(regs, %u)\n", regno);
		/* Something bad happened, let's kill all regs */
		for (regno = 0; regno < MAX_BPF_REG; regno++)
			__mark_reg_not_init(env, regs + regno);
		return;
	}
	__mark_reg_known_zero(regs + regno);
}

static void mark_ptr_not_null_reg(struct bpf_reg_state *reg)
{
	switch (reg->type) {
	case PTR_TO_MAP_VALUE_OR_NULL: {
		const struct bpf_map *map = reg->map_ptr;

		if (map->inner_map_meta) {
			reg->type = CONST_PTR_TO_MAP;
			reg->map_ptr = map->inner_map_meta;
		} else if (map->map_type == BPF_MAP_TYPE_XSKMAP) {
			reg->type = PTR_TO_XDP_SOCK;
		} else if (map->map_type == BPF_MAP_TYPE_SOCKMAP ||
			   map->map_type == BPF_MAP_TYPE_SOCKHASH) {
			reg->type = PTR_TO_SOCKET;
		} else {
			reg->type = PTR_TO_MAP_VALUE;
		}
		break;
	}
	case PTR_TO_SOCKET_OR_NULL:
		reg->type = PTR_TO_SOCKET;
		break;
	case PTR_TO_SOCK_COMMON_OR_NULL:
		reg->type = PTR_TO_SOCK_COMMON;
		break;
	case PTR_TO_TCP_SOCK_OR_NULL:
		reg->type = PTR_TO_TCP_SOCK;
		break;
	case PTR_TO_BTF_ID_OR_NULL:
		reg->type = PTR_TO_BTF_ID;
		break;
	case PTR_TO_MEM_OR_NULL:
		reg->type = PTR_TO_MEM;
		break;
	case PTR_TO_RDONLY_BUF_OR_NULL:
		reg->type = PTR_TO_RDONLY_BUF;
		break;
	case PTR_TO_RDWR_BUF_OR_NULL:
		reg->type = PTR_TO_RDWR_BUF;
		break;
	default:
		WARN_ONCE(1, "unknown nullable register type");
	}
}

static bool reg_is_pkt_pointer(const struct bpf_reg_state *reg)
{
	return type_is_pkt_pointer(reg->type);
}

static bool reg_is_pkt_pointer_any(const struct bpf_reg_state *reg)
{
	return reg_is_pkt_pointer(reg) ||
	       reg->type == PTR_TO_PACKET_END;
}

/* Unmodified PTR_TO_PACKET[_META,_END] register from ctx access. */
static bool reg_is_init_pkt_pointer(const struct bpf_reg_state *reg,
				    enum bpf_reg_type which)
{
	/* The register can already have a range from prior markings.
	 * This is fine as long as it hasn't been advanced from its
	 * origin.
	 */
	return reg->type == which &&
	       reg->id == 0 &&
	       reg->off == 0 &&
	       tnum_equals_const(reg->var_off, 0);
}

/* Reset the min/max bounds of a register */
static void __mark_reg_unbounded(struct bpf_reg_state *reg)
{
	reg->smin_value = S64_MIN;
	reg->smax_value = S64_MAX;
	reg->umin_value = 0;
	reg->umax_value = U64_MAX;

	reg->s32_min_value = S32_MIN;
	reg->s32_max_value = S32_MAX;
	reg->u32_min_value = 0;
	reg->u32_max_value = U32_MAX;
}

static void __mark_reg64_unbounded(struct bpf_reg_state *reg)
{
	reg->smin_value = S64_MIN;
	reg->smax_value = S64_MAX;
	reg->umin_value = 0;
	reg->umax_value = U64_MAX;
}

static void __mark_reg32_unbounded(struct bpf_reg_state *reg)
{
	reg->s32_min_value = S32_MIN;
	reg->s32_max_value = S32_MAX;
	reg->u32_min_value = 0;
	reg->u32_max_value = U32_MAX;
}

static void __update_reg32_bounds(struct bpf_reg_state *reg)
{
	struct tnum var32_off = tnum_subreg(reg->var_off);

	/* min signed is max(sign bit) | min(other bits) */
	reg->s32_min_value = max_t(s32, reg->s32_min_value,
			var32_off.value | (var32_off.mask & S32_MIN));
	/* max signed is min(sign bit) | max(other bits) */
	reg->s32_max_value = min_t(s32, reg->s32_max_value,
			var32_off.value | (var32_off.mask & S32_MAX));
	reg->u32_min_value = max_t(u32, reg->u32_min_value, (u32)var32_off.value);
	reg->u32_max_value = min(reg->u32_max_value,
				 (u32)(var32_off.value | var32_off.mask));
}

static void __update_reg64_bounds(struct bpf_reg_state *reg)
{
	/* min signed is max(sign bit) | min(other bits) */
	reg->smin_value = max_t(s64, reg->smin_value,
				reg->var_off.value | (reg->var_off.mask & S64_MIN));
	/* max signed is min(sign bit) | max(other bits) */
	reg->smax_value = min_t(s64, reg->smax_value,
				reg->var_off.value | (reg->var_off.mask & S64_MAX));
	reg->umin_value = max(reg->umin_value, reg->var_off.value);
	reg->umax_value = min(reg->umax_value,
			      reg->var_off.value | reg->var_off.mask);
}

static void __update_reg_bounds(struct bpf_reg_state *reg)
{
	__update_reg32_bounds(reg);
	__update_reg64_bounds(reg);
}

/* Uses signed min/max values to inform unsigned, and vice-versa */
static void __reg32_deduce_bounds(struct bpf_reg_state *reg)
{
	/* Learn sign from signed bounds.
	 * If we cannot cross the sign boundary, then signed and unsigned bounds
	 * are the same, so combine.  This works even in the negative case, e.g.
	 * -3 s<= x s<= -1 implies 0xf...fd u<= x u<= 0xf...ff.
	 */
	if (reg->s32_min_value >= 0 || reg->s32_max_value < 0) {
		reg->s32_min_value = reg->u32_min_value =
			max_t(u32, reg->s32_min_value, reg->u32_min_value);
		reg->s32_max_value = reg->u32_max_value =
			min_t(u32, reg->s32_max_value, reg->u32_max_value);
		return;
	}
	/* Learn sign from unsigned bounds.  Signed bounds cross the sign
	 * boundary, so we must be careful.
	 */
	if ((s32)reg->u32_max_value >= 0) {
		/* Positive.  We can't learn anything from the smin, but smax
		 * is positive, hence safe.
		 */
		reg->s32_min_value = reg->u32_min_value;
		reg->s32_max_value = reg->u32_max_value =
			min_t(u32, reg->s32_max_value, reg->u32_max_value);
	} else if ((s32)reg->u32_min_value < 0) {
		/* Negative.  We can't learn anything from the smax, but smin
		 * is negative, hence safe.
		 */
		reg->s32_min_value = reg->u32_min_value =
			max_t(u32, reg->s32_min_value, reg->u32_min_value);
		reg->s32_max_value = reg->u32_max_value;
	}
}

static void __reg64_deduce_bounds(struct bpf_reg_state *reg)
{
	/* Learn sign from signed bounds.
	 * If we cannot cross the sign boundary, then signed and unsigned bounds
	 * are the same, so combine.  This works even in the negative case, e.g.
	 * -3 s<= x s<= -1 implies 0xf...fd u<= x u<= 0xf...ff.
	 */
	if (reg->smin_value >= 0 || reg->smax_value < 0) {
		reg->smin_value = reg->umin_value = max_t(u64, reg->smin_value,
							  reg->umin_value);
		reg->smax_value = reg->umax_value = min_t(u64, reg->smax_value,
							  reg->umax_value);
		return;
	}
	/* Learn sign from unsigned bounds.  Signed bounds cross the sign
	 * boundary, so we must be careful.
	 */
	if ((s64)reg->umax_value >= 0) {
		/* Positive.  We can't learn anything from the smin, but smax
		 * is positive, hence safe.
		 */
		reg->smin_value = reg->umin_value;
		reg->smax_value = reg->umax_value = min_t(u64, reg->smax_value,
							  reg->umax_value);
	} else if ((s64)reg->umin_value < 0) {
		/* Negative.  We can't learn anything from the smax, but smin
		 * is negative, hence safe.
		 */
		reg->smin_value = reg->umin_value = max_t(u64, reg->smin_value,
							  reg->umin_value);
		reg->smax_value = reg->umax_value;
	}
}

static void __reg_deduce_bounds(struct bpf_reg_state *reg)
{
	__reg32_deduce_bounds(reg);
	__reg64_deduce_bounds(reg);
}

/* Attempts to improve var_off based on unsigned min/max information */
static void __reg_bound_offset(struct bpf_reg_state *reg)
{
	struct tnum var64_off = tnum_intersect(reg->var_off,
					       tnum_range(reg->umin_value,
							  reg->umax_value));
	struct tnum var32_off = tnum_intersect(tnum_subreg(reg->var_off),
						tnum_range(reg->u32_min_value,
							   reg->u32_max_value));

	reg->var_off = tnum_or(tnum_clear_subreg(var64_off), var32_off);
}

static void __reg_assign_32_into_64(struct bpf_reg_state *reg)
{
	reg->umin_value = reg->u32_min_value;
	reg->umax_value = reg->u32_max_value;
	/* Attempt to pull 32-bit signed bounds into 64-bit bounds
	 * but must be positive otherwise set to worse case bounds
	 * and refine later from tnum.
	 */
	if (reg->s32_min_value >= 0 && reg->s32_max_value >= 0)
		reg->smax_value = reg->s32_max_value;
	else
		reg->smax_value = U32_MAX;
	if (reg->s32_min_value >= 0)
		reg->smin_value = reg->s32_min_value;
	else
		reg->smin_value = 0;
}

static void __reg_combine_32_into_64(struct bpf_reg_state *reg)
{
	/* special case when 64-bit register has upper 32-bit register
	 * zeroed. Typically happens after zext or <<32, >>32 sequence
	 * allowing us to use 32-bit bounds directly,
	 */
	if (tnum_equals_const(tnum_clear_subreg(reg->var_off), 0)) {
		__reg_assign_32_into_64(reg);
	} else {
		/* Otherwise the best we can do is push lower 32bit known and
		 * unknown bits into register (var_off set from jmp logic)
		 * then learn as much as possible from the 64-bit tnum
		 * known and unknown bits. The previous smin/smax bounds are
		 * invalid here because of jmp32 compare so mark them unknown
		 * so they do not impact tnum bounds calculation.
		 */
		__mark_reg64_unbounded(reg);
		__update_reg_bounds(reg);
	}

	/* Intersecting with the old var_off might have improved our bounds
	 * slightly.  e.g. if umax was 0x7f...f and var_off was (0; 0xf...fc),
	 * then new var_off is (0; 0x7f...fc) which improves our umax.
	 */
	__reg_deduce_bounds(reg);
	__reg_bound_offset(reg);
	__update_reg_bounds(reg);
}

static bool __reg64_bound_s32(s64 a)
{
	return a > S32_MIN && a < S32_MAX;
}

static bool __reg64_bound_u32(u64 a)
{
	if (a > U32_MIN && a < U32_MAX)
		return true;
	return false;
}

static void __reg_combine_64_into_32(struct bpf_reg_state *reg)
{
	__mark_reg32_unbounded(reg);

	if (__reg64_bound_s32(reg->smin_value) && __reg64_bound_s32(reg->smax_value)) {
		reg->s32_min_value = (s32)reg->smin_value;
		reg->s32_max_value = (s32)reg->smax_value;
	}
	if (__reg64_bound_u32(reg->umin_value))
		reg->u32_min_value = (u32)reg->umin_value;
	if (__reg64_bound_u32(reg->umax_value))
		reg->u32_max_value = (u32)reg->umax_value;

	/* Intersecting with the old var_off might have improved our bounds
	 * slightly.  e.g. if umax was 0x7f...f and var_off was (0; 0xf...fc),
	 * then new var_off is (0; 0x7f...fc) which improves our umax.
	 */
	__reg_deduce_bounds(reg);
	__reg_bound_offset(reg);
	__update_reg_bounds(reg);
}

/* Mark a register as having a completely unknown (scalar) value. */
static void __mark_reg_unknown(const struct bpf_verifier_env *env,
			       struct bpf_reg_state *reg)
{
	/*
	 * Clear type, id, off, and union(map_ptr, range) and
	 * padding between 'type' and union
	 */
	memset(reg, 0, offsetof(struct bpf_reg_state, var_off));
	reg->type = SCALAR_VALUE;
	reg->var_off = tnum_unknown;
	reg->frameno = 0;
	reg->precise = env->subprog_cnt > 1 || !env->bpf_capable;
	__mark_reg_unbounded(reg);
}

static void mark_reg_unknown(struct bpf_verifier_env *env,
			     struct bpf_reg_state *regs, u32 regno)
{
	if (WARN_ON(regno >= MAX_BPF_REG)) {
		verbose(env, "mark_reg_unknown(regs, %u)\n", regno);
		/* Something bad happened, let's kill all regs except FP */
		for (regno = 0; regno < BPF_REG_FP; regno++)
			__mark_reg_not_init(env, regs + regno);
		return;
	}
	__mark_reg_unknown(env, regs + regno);
}

static void __mark_reg_not_init(const struct bpf_verifier_env *env,
				struct bpf_reg_state *reg)
{
	__mark_reg_unknown(env, reg);
	reg->type = NOT_INIT;
}

static void mark_reg_not_init(struct bpf_verifier_env *env,
			      struct bpf_reg_state *regs, u32 regno)
{
	if (WARN_ON(regno >= MAX_BPF_REG)) {
		verbose(env, "mark_reg_not_init(regs, %u)\n", regno);
		/* Something bad happened, let's kill all regs except FP */
		for (regno = 0; regno < BPF_REG_FP; regno++)
			__mark_reg_not_init(env, regs + regno);
		return;
	}
	__mark_reg_not_init(env, regs + regno);
}

static void mark_btf_ld_reg(struct bpf_verifier_env *env,
			    struct bpf_reg_state *regs, u32 regno,
			    enum bpf_reg_type reg_type,
			    struct btf *btf, u32 btf_id)
{
	if (reg_type == SCALAR_VALUE) {
		mark_reg_unknown(env, regs, regno);
		return;
	}
	mark_reg_known_zero(env, regs, regno);
	regs[regno].type = PTR_TO_BTF_ID;
	regs[regno].btf = btf;
	regs[regno].btf_id = btf_id;
}

#define DEF_NOT_SUBREG	(0)
static void init_reg_state(struct bpf_verifier_env *env,
			   struct bpf_func_state *state)
{
	struct bpf_reg_state *regs = state->regs;
	int i;

	for (i = 0; i < MAX_BPF_REG; i++) {
		mark_reg_not_init(env, regs, i);
		regs[i].live = REG_LIVE_NONE;
		regs[i].parent = NULL;
		regs[i].subreg_def = DEF_NOT_SUBREG;
	}

	/* frame pointer */
	regs[BPF_REG_FP].type = PTR_TO_STACK;
	mark_reg_known_zero(env, regs, BPF_REG_FP);
	regs[BPF_REG_FP].frameno = state->frameno;
}

#define BPF_MAIN_FUNC (-1)
static void init_func_state(struct bpf_verifier_env *env,
			    struct bpf_func_state *state,
			    int callsite, int frameno, int subprogno)
{
	state->callsite = callsite;
	state->frameno = frameno;
	state->subprogno = subprogno;
	init_reg_state(env, state);
}

enum reg_arg_type {
	SRC_OP,		/* register is used as source operand */
	DST_OP,		/* register is used as destination operand */
	DST_OP_NO_MARK	/* same as above, check only, don't mark */
};

static int cmp_subprogs(const void *a, const void *b)
{
	return ((struct bpf_subprog_info *)a)->start -
	       ((struct bpf_subprog_info *)b)->start;
}

static int find_subprog(struct bpf_verifier_env *env, int off)
{
	struct bpf_subprog_info *p;

	p = bsearch(&off, env->subprog_info, env->subprog_cnt,
		    sizeof(env->subprog_info[0]), cmp_subprogs);
	if (!p)
		return -ENOENT;
	return p - env->subprog_info;

}

static int add_subprog(struct bpf_verifier_env *env, int off)
{
	int insn_cnt = env->prog->len;
	int ret;

	if (off >= insn_cnt || off < 0) {
		verbose(env, "call to invalid destination\n");
		return -EINVAL;
	}
	ret = find_subprog(env, off);
	if (ret >= 0)
		return 0;
	if (env->subprog_cnt >= BPF_MAX_SUBPROGS) {
		verbose(env, "too many subprograms\n");
		return -E2BIG;
	}
	env->subprog_info[env->subprog_cnt++].start = off;
	sort(env->subprog_info, env->subprog_cnt,
	     sizeof(env->subprog_info[0]), cmp_subprogs, NULL);
	return 0;
}

static int check_subprogs(struct bpf_verifier_env *env)
{
	int i, ret, subprog_start, subprog_end, off, cur_subprog = 0;
	struct bpf_subprog_info *subprog = env->subprog_info;
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;

	/* Add entry function. */
	ret = add_subprog(env, 0);
	if (ret < 0)
		return ret;

	/* determine subprog starts. The end is one before the next starts */
	for (i = 0; i < insn_cnt; i++) {
		if (!bpf_pseudo_call(insn + i))
			continue;
		if (!env->bpf_capable) {
			verbose(env,
				"function calls to other bpf functions are allowed for CAP_BPF and CAP_SYS_ADMIN\n");
			return -EPERM;
		}
		ret = add_subprog(env, i + insn[i].imm + 1);
		if (ret < 0)
			return ret;
	}

	/* Add a fake 'exit' subprog which could simplify subprog iteration
	 * logic. 'subprog_cnt' should not be increased.
	 */
	subprog[env->subprog_cnt].start = insn_cnt;

	if (env->log.level & BPF_LOG_LEVEL2)
		for (i = 0; i < env->subprog_cnt; i++)
			verbose(env, "func#%d @%d\n", i, subprog[i].start);

	/* now check that all jumps are within the same subprog */
	subprog_start = subprog[cur_subprog].start;
	subprog_end = subprog[cur_subprog + 1].start;
	for (i = 0; i < insn_cnt; i++) {
		u8 code = insn[i].code;

		if (code == (BPF_JMP | BPF_CALL) &&
		    insn[i].imm == BPF_FUNC_tail_call &&
		    insn[i].src_reg != BPF_PSEUDO_CALL)
			subprog[cur_subprog].has_tail_call = true;
		if (BPF_CLASS(code) == BPF_LD &&
		    (BPF_MODE(code) == BPF_ABS || BPF_MODE(code) == BPF_IND))
			subprog[cur_subprog].has_ld_abs = true;
		if (BPF_CLASS(code) != BPF_JMP && BPF_CLASS(code) != BPF_JMP32)
			goto next;
		if (BPF_OP(code) == BPF_EXIT || BPF_OP(code) == BPF_CALL)
			goto next;
		off = i + insn[i].off + 1;
		if (off < subprog_start || off >= subprog_end) {
			verbose(env, "jump out of range from insn %d to %d\n", i, off);
			return -EINVAL;
		}
next:
		if (i == subprog_end - 1) {
			/* to avoid fall-through from one subprog into another
			 * the last insn of the subprog should be either exit
			 * or unconditional jump back
			 */
			if (code != (BPF_JMP | BPF_EXIT) &&
			    code != (BPF_JMP | BPF_JA)) {
				verbose(env, "last insn is not an exit or jmp\n");
				return -EINVAL;
			}
			subprog_start = subprog_end;
			cur_subprog++;
			if (cur_subprog < env->subprog_cnt)
				subprog_end = subprog[cur_subprog + 1].start;
		}
	}
	return 0;
}

/* Parentage chain of this register (or stack slot) should take care of all
 * issues like callee-saved registers, stack slot allocation time, etc.
 */
static int mark_reg_read(struct bpf_verifier_env *env,
			 const struct bpf_reg_state *state,
			 struct bpf_reg_state *parent, u8 flag)
{
	bool writes = parent == state->parent; /* Observe write marks */
	int cnt = 0;

	while (parent) {
		/* if read wasn't screened by an earlier write ... */
		if (writes && state->live & REG_LIVE_WRITTEN)
			break;
		if (parent->live & REG_LIVE_DONE) {
			verbose(env, "verifier BUG type %s var_off %lld off %d\n",
				reg_type_str[parent->type],
				parent->var_off.value, parent->off);
			return -EFAULT;
		}
		/* The first condition is more likely to be true than the
		 * second, checked it first.
		 */
		if ((parent->live & REG_LIVE_READ) == flag ||
		    parent->live & REG_LIVE_READ64)
			/* The parentage chain never changes and
			 * this parent was already marked as LIVE_READ.
			 * There is no need to keep walking the chain again and
			 * keep re-marking all parents as LIVE_READ.
			 * This case happens when the same register is read
			 * multiple times without writes into it in-between.
			 * Also, if parent has the stronger REG_LIVE_READ64 set,
			 * then no need to set the weak REG_LIVE_READ32.
			 */
			break;
		/* ... then we depend on parent's value */
		parent->live |= flag;
		/* REG_LIVE_READ64 overrides REG_LIVE_READ32. */
		if (flag == REG_LIVE_READ64)
			parent->live &= ~REG_LIVE_READ32;
		state = parent;
		parent = state->parent;
		writes = true;
		cnt++;
	}

	if (env->longest_mark_read_walk < cnt)
		env->longest_mark_read_walk = cnt;
	return 0;
}

/* This function is supposed to be used by the following 32-bit optimization
 * code only. It returns TRUE if the source or destination register operates
 * on 64-bit, otherwise return FALSE.
 */
static bool is_reg64(struct bpf_verifier_env *env, struct bpf_insn *insn,
		     u32 regno, struct bpf_reg_state *reg, enum reg_arg_type t)
{
	u8 code, class, op;

	code = insn->code;
	class = BPF_CLASS(code);
	op = BPF_OP(code);
	if (class == BPF_JMP) {
		/* BPF_EXIT for "main" will reach here. Return TRUE
		 * conservatively.
		 */
		if (op == BPF_EXIT)
			return true;
		if (op == BPF_CALL) {
			/* BPF to BPF call will reach here because of marking
			 * caller saved clobber with DST_OP_NO_MARK for which we
			 * don't care the register def because they are anyway
			 * marked as NOT_INIT already.
			 */
			if (insn->src_reg == BPF_PSEUDO_CALL)
				return false;
			/* Helper call will reach here because of arg type
			 * check, conservatively return TRUE.
			 */
			if (t == SRC_OP)
				return true;

			return false;
		}
	}

	if (class == BPF_ALU64 || class == BPF_JMP ||
	    /* BPF_END always use BPF_ALU class. */
	    (class == BPF_ALU && op == BPF_END && insn->imm == 64))
		return true;

	if (class == BPF_ALU || class == BPF_JMP32)
		return false;

	if (class == BPF_LDX) {
		if (t != SRC_OP)
			return BPF_SIZE(code) == BPF_DW;
		/* LDX source must be ptr. */
		return true;
	}

	if (class == BPF_STX) {
		/* BPF_STX (including atomic variants) has multiple source
		 * operands, one of which is a ptr. Check whether the caller is
		 * asking about it.
		 */
		if (t == SRC_OP && reg->type != SCALAR_VALUE)
			return true;
		return BPF_SIZE(code) == BPF_DW;
	}

	if (class == BPF_LD) {
		u8 mode = BPF_MODE(code);

		/* LD_IMM64 */
		if (mode == BPF_IMM)
			return true;

		/* Both LD_IND and LD_ABS return 32-bit data. */
		if (t != SRC_OP)
			return  false;

		/* Implicit ctx ptr. */
		if (regno == BPF_REG_6)
			return true;

		/* Explicit source could be any width. */
		return true;
	}

	if (class == BPF_ST)
		/* The only source register for BPF_ST is a ptr. */
		return true;

	/* Conservatively return true at default. */
	return true;
}

/* Return the regno defined by the insn, or -1. */
static int insn_def_regno(const struct bpf_insn *insn)
{
	switch (BPF_CLASS(insn->code)) {
	case BPF_JMP:
	case BPF_JMP32:
	case BPF_ST:
		return -1;
	case BPF_STX:
		if (BPF_MODE(insn->code) == BPF_ATOMIC &&
		    (insn->imm & BPF_FETCH)) {
			if (insn->imm == BPF_CMPXCHG)
				return BPF_REG_0;
			else
				return insn->src_reg;
		} else {
			return -1;
		}
	default:
		return insn->dst_reg;
	}
}

/* Return TRUE if INSN has defined any 32-bit value explicitly. */
static bool insn_has_def32(struct bpf_verifier_env *env, struct bpf_insn *insn)
{
	int dst_reg = insn_def_regno(insn);

	if (dst_reg == -1)
		return false;

	return !is_reg64(env, insn, dst_reg, NULL, DST_OP);
}

static void mark_insn_zext(struct bpf_verifier_env *env,
			   struct bpf_reg_state *reg)
{
	s32 def_idx = reg->subreg_def;

	if (def_idx == DEF_NOT_SUBREG)
		return;

	env->insn_aux_data[def_idx - 1].zext_dst = true;
	/* The dst will be zero extended, so won't be sub-register anymore. */
	reg->subreg_def = DEF_NOT_SUBREG;
}

static int check_reg_arg(struct bpf_verifier_env *env, u32 regno,
			 enum reg_arg_type t)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	struct bpf_func_state *state = vstate->frame[vstate->curframe];
	struct bpf_insn *insn = env->prog->insnsi + env->insn_idx;
	struct bpf_reg_state *reg, *regs = state->regs;
	bool rw64;

	if (regno >= MAX_BPF_REG) {
		verbose(env, "R%d is invalid\n", regno);
		return -EINVAL;
	}

	reg = &regs[regno];
	rw64 = is_reg64(env, insn, regno, reg, t);
	if (t == SRC_OP) {
		/* check whether register used as source operand can be read */
		if (reg->type == NOT_INIT) {
			verbose(env, "R%d !read_ok\n", regno);
			return -EACCES;
		}
		/* We don't need to worry about FP liveness because it's read-only */
		if (regno == BPF_REG_FP)
			return 0;

		if (rw64)
			mark_insn_zext(env, reg);

		return mark_reg_read(env, reg, reg->parent,
				     rw64 ? REG_LIVE_READ64 : REG_LIVE_READ32);
	} else {
		/* check whether register used as dest operand can be written to */
		if (regno == BPF_REG_FP) {
			verbose(env, "frame pointer is read only\n");
			return -EACCES;
		}
		reg->live |= REG_LIVE_WRITTEN;
		reg->subreg_def = rw64 ? DEF_NOT_SUBREG : env->insn_idx + 1;
		if (t == DST_OP)
			mark_reg_unknown(env, regs, regno);
	}
	return 0;
}

/* for any branch, call, exit record the history of jmps in the given state */
static int push_jmp_history(struct bpf_verifier_env *env,
			    struct bpf_verifier_state *cur)
{
	u32 cnt = cur->jmp_history_cnt;
	struct bpf_idx_pair *p;

	cnt++;
	p = krealloc(cur->jmp_history, cnt * sizeof(*p), GFP_USER);
	if (!p)
		return -ENOMEM;
	p[cnt - 1].idx = env->insn_idx;
	p[cnt - 1].prev_idx = env->prev_insn_idx;
	cur->jmp_history = p;
	cur->jmp_history_cnt = cnt;
	return 0;
}

/* Backtrack one insn at a time. If idx is not at the top of recorded
 * history then previous instruction came from straight line execution.
 */
static int get_prev_insn_idx(struct bpf_verifier_state *st, int i,
			     u32 *history)
{
	u32 cnt = *history;

	if (cnt && st->jmp_history[cnt - 1].idx == i) {
		i = st->jmp_history[cnt - 1].prev_idx;
		(*history)--;
	} else {
		i--;
	}
	return i;
}

/* For given verifier state backtrack_insn() is called from the last insn to
 * the first insn. Its purpose is to compute a bitmask of registers and
 * stack slots that needs precision in the parent verifier state.
 */
static int backtrack_insn(struct bpf_verifier_env *env, int idx,
			  u32 *reg_mask, u64 *stack_mask)
{
	const struct bpf_insn_cbs cbs = {
		.cb_print	= verbose,
		.private_data	= env,
	};
	struct bpf_insn *insn = env->prog->insnsi + idx;
	u8 class = BPF_CLASS(insn->code);
	u8 opcode = BPF_OP(insn->code);
	u8 mode = BPF_MODE(insn->code);
	u32 dreg = 1u << insn->dst_reg;
	u32 sreg = 1u << insn->src_reg;
	u32 spi;

	if (insn->code == 0)
		return 0;
	if (env->log.level & BPF_LOG_LEVEL) {
		verbose(env, "regs=%x stack=%llx before ", *reg_mask, *stack_mask);
		verbose(env, "%d: ", idx);
		print_bpf_insn(&cbs, insn, env->allow_ptr_leaks);
	}

	if (class == BPF_ALU || class == BPF_ALU64) {
		if (!(*reg_mask & dreg))
			return 0;
		if (opcode == BPF_MOV) {
			if (BPF_SRC(insn->code) == BPF_X) {
				/* dreg = sreg
				 * dreg needs precision after this insn
				 * sreg needs precision before this insn
				 */
				*reg_mask &= ~dreg;
				*reg_mask |= sreg;
			} else {
				/* dreg = K
				 * dreg needs precision after this insn.
				 * Corresponding register is already marked
				 * as precise=true in this verifier state.
				 * No further markings in parent are necessary
				 */
				*reg_mask &= ~dreg;
			}
		} else {
			if (BPF_SRC(insn->code) == BPF_X) {
				/* dreg += sreg
				 * both dreg and sreg need precision
				 * before this insn
				 */
				*reg_mask |= sreg;
			} /* else dreg += K
			   * dreg still needs precision before this insn
			   */
		}
	} else if (class == BPF_LDX) {
		if (!(*reg_mask & dreg))
			return 0;
		*reg_mask &= ~dreg;

		/* scalars can only be spilled into stack w/o losing precision.
		 * Load from any other memory can be zero extended.
		 * The desire to keep that precision is already indicated
		 * by 'precise' mark in corresponding register of this state.
		 * No further tracking necessary.
		 */
		if (insn->src_reg != BPF_REG_FP)
			return 0;
		if (BPF_SIZE(insn->code) != BPF_DW)
			return 0;

		/* dreg = *(u64 *)[fp - off] was a fill from the stack.
		 * that [fp - off] slot contains scalar that needs to be
		 * tracked with precision
		 */
		spi = (-insn->off - 1) / BPF_REG_SIZE;
		if (spi >= 64) {
			verbose(env, "BUG spi %d\n", spi);
			WARN_ONCE(1, "verifier backtracking bug");
			return -EFAULT;
		}
		*stack_mask |= 1ull << spi;
	} else if (class == BPF_STX || class == BPF_ST) {
		if (*reg_mask & dreg)
			/* stx & st shouldn't be using _scalar_ dst_reg
			 * to access memory. It means backtracking
			 * encountered a case of pointer subtraction.
			 */
			return -ENOTSUPP;
		/* scalars can only be spilled into stack */
		if (insn->dst_reg != BPF_REG_FP)
			return 0;
		if (BPF_SIZE(insn->code) != BPF_DW)
			return 0;
		spi = (-insn->off - 1) / BPF_REG_SIZE;
		if (spi >= 64) {
			verbose(env, "BUG spi %d\n", spi);
			WARN_ONCE(1, "verifier backtracking bug");
			return -EFAULT;
		}
		if (!(*stack_mask & (1ull << spi)))
			return 0;
		*stack_mask &= ~(1ull << spi);
		if (class == BPF_STX)
			*reg_mask |= sreg;
	} else if (class == BPF_JMP || class == BPF_JMP32) {
		if (opcode == BPF_CALL) {
			if (insn->src_reg == BPF_PSEUDO_CALL)
				return -ENOTSUPP;
			/* regular helper call sets R0 */
			*reg_mask &= ~1;
			if (*reg_mask & 0x3f) {
				/* if backtracing was looking for registers R1-R5
				 * they should have been found already.
				 */
				verbose(env, "BUG regs %x\n", *reg_mask);
				WARN_ONCE(1, "verifier backtracking bug");
				return -EFAULT;
			}
		} else if (opcode == BPF_EXIT) {
			return -ENOTSUPP;
		}
	} else if (class == BPF_LD) {
		if (!(*reg_mask & dreg))
			return 0;
		*reg_mask &= ~dreg;
		/* It's ld_imm64 or ld_abs or ld_ind.
		 * For ld_imm64 no further tracking of precision
		 * into parent is necessary
		 */
		if (mode == BPF_IND || mode == BPF_ABS)
			/* to be analyzed */
			return -ENOTSUPP;
	}
	return 0;
}

/* the scalar precision tracking algorithm:
 * . at the start all registers have precise=false.
 * . scalar ranges are tracked as normal through alu and jmp insns.
 * . once precise value of the scalar register is used in:
 *   .  ptr + scalar alu
 *   . if (scalar cond K|scalar)
 *   .  helper_call(.., scalar, ...) where ARG_CONST is expected
 *   backtrack through the verifier states and mark all registers and
 *   stack slots with spilled constants that these scalar regisers
 *   should be precise.
 * . during state pruning two registers (or spilled stack slots)
 *   are equivalent if both are not precise.
 *
 * Note the verifier cannot simply walk register parentage chain,
 * since many different registers and stack slots could have been
 * used to compute single precise scalar.
 *
 * The approach of starting with precise=true for all registers and then
 * backtrack to mark a register as not precise when the verifier detects
 * that program doesn't care about specific value (e.g., when helper
 * takes register as ARG_ANYTHING parameter) is not safe.
 *
 * It's ok to walk single parentage chain of the verifier states.
 * It's possible that this backtracking will go all the way till 1st insn.
 * All other branches will be explored for needing precision later.
 *
 * The backtracking needs to deal with cases like:
 *   R8=map_value(id=0,off=0,ks=4,vs=1952,imm=0) R9_w=map_value(id=0,off=40,ks=4,vs=1952,imm=0)
 * r9 -= r8
 * r5 = r9
 * if r5 > 0x79f goto pc+7
 *    R5_w=inv(id=0,umax_value=1951,var_off=(0x0; 0x7ff))
 * r5 += 1
 * ...
 * call bpf_perf_event_output#25
 *   where .arg5_type = ARG_CONST_SIZE_OR_ZERO
 *
 * and this case:
 * r6 = 1
 * call foo // uses callee's r6 inside to compute r0
 * r0 += r6
 * if r0 == 0 goto
 *
 * to track above reg_mask/stack_mask needs to be independent for each frame.
 *
 * Also if parent's curframe > frame where backtracking started,
 * the verifier need to mark registers in both frames, otherwise callees
 * may incorrectly prune callers. This is similar to
 * commit 7640ead93924 ("bpf: verifier: make sure callees don't prune with caller differences")
 *
 * For now backtracking falls back into conservative marking.
 */
static void mark_all_scalars_precise(struct bpf_verifier_env *env,
				     struct bpf_verifier_state *st)
{
	struct bpf_func_state *func;
	struct bpf_reg_state *reg;
	int i, j;

	/* big hammer: mark all scalars precise in this path.
	 * pop_stack may still get !precise scalars.
	 */
	for (; st; st = st->parent)
		for (i = 0; i <= st->curframe; i++) {
			func = st->frame[i];
			for (j = 0; j < BPF_REG_FP; j++) {
				reg = &func->regs[j];
				if (reg->type != SCALAR_VALUE)
					continue;
				reg->precise = true;
			}
			for (j = 0; j < func->allocated_stack / BPF_REG_SIZE; j++) {
				if (func->stack[j].slot_type[0] != STACK_SPILL)
					continue;
				reg = &func->stack[j].spilled_ptr;
				if (reg->type != SCALAR_VALUE)
					continue;
				reg->precise = true;
			}
		}
}

static int __mark_chain_precision(struct bpf_verifier_env *env, int regno,
				  int spi)
{
	struct bpf_verifier_state *st = env->cur_state;
	int first_idx = st->first_insn_idx;
	int last_idx = env->insn_idx;
	struct bpf_func_state *func;
	struct bpf_reg_state *reg;
	u32 reg_mask = regno >= 0 ? 1u << regno : 0;
	u64 stack_mask = spi >= 0 ? 1ull << spi : 0;
	bool skip_first = true;
	bool new_marks = false;
	int i, err;

	if (!env->bpf_capable)
		return 0;

	func = st->frame[st->curframe];
	if (regno >= 0) {
		reg = &func->regs[regno];
		if (reg->type != SCALAR_VALUE) {
			WARN_ONCE(1, "backtracing misuse");
			return -EFAULT;
		}
		if (!reg->precise)
			new_marks = true;
		else
			reg_mask = 0;
		reg->precise = true;
	}

	while (spi >= 0) {
		if (func->stack[spi].slot_type[0] != STACK_SPILL) {
			stack_mask = 0;
			break;
		}
		reg = &func->stack[spi].spilled_ptr;
		if (reg->type != SCALAR_VALUE) {
			stack_mask = 0;
			break;
		}
		if (!reg->precise)
			new_marks = true;
		else
			stack_mask = 0;
		reg->precise = true;
		break;
	}

	if (!new_marks)
		return 0;
	if (!reg_mask && !stack_mask)
		return 0;
	for (;;) {
		DECLARE_BITMAP(mask, 64);
		u32 history = st->jmp_history_cnt;

		if (env->log.level & BPF_LOG_LEVEL)
			verbose(env, "last_idx %d first_idx %d\n", last_idx, first_idx);
		for (i = last_idx;;) {
			if (skip_first) {
				err = 0;
				skip_first = false;
			} else {
				err = backtrack_insn(env, i, &reg_mask, &stack_mask);
			}
			if (err == -ENOTSUPP) {
				mark_all_scalars_precise(env, st);
				return 0;
			} else if (err) {
				return err;
			}
			if (!reg_mask && !stack_mask)
				/* Found assignment(s) into tracked register in this state.
				 * Since this state is already marked, just return.
				 * Nothing to be tracked further in the parent state.
				 */
				return 0;
			if (i == first_idx)
				break;
			i = get_prev_insn_idx(st, i, &history);
			if (i >= env->prog->len) {
				/* This can happen if backtracking reached insn 0
				 * and there are still reg_mask or stack_mask
				 * to backtrack.
				 * It means the backtracking missed the spot where
				 * particular register was initialized with a constant.
				 */
				verbose(env, "BUG backtracking idx %d\n", i);
				WARN_ONCE(1, "verifier backtracking bug");
				return -EFAULT;
			}
		}
		st = st->parent;
		if (!st)
			break;

		new_marks = false;
		func = st->frame[st->curframe];
		bitmap_from_u64(mask, reg_mask);
		for_each_set_bit(i, mask, 32) {
			reg = &func->regs[i];
			if (reg->type != SCALAR_VALUE) {
				reg_mask &= ~(1u << i);
				continue;
			}
			if (!reg->precise)
				new_marks = true;
			reg->precise = true;
		}

		bitmap_from_u64(mask, stack_mask);
		for_each_set_bit(i, mask, 64) {
			if (i >= func->allocated_stack / BPF_REG_SIZE) {
				/* the sequence of instructions:
				 * 2: (bf) r3 = r10
				 * 3: (7b) *(u64 *)(r3 -8) = r0
				 * 4: (79) r4 = *(u64 *)(r10 -8)
				 * doesn't contain jmps. It's backtracked
				 * as a single block.
				 * During backtracking insn 3 is not recognized as
				 * stack access, so at the end of backtracking
				 * stack slot fp-8 is still marked in stack_mask.
				 * However the parent state may not have accessed
				 * fp-8 and it's "unallocated" stack space.
				 * In such case fallback to conservative.
				 */
				mark_all_scalars_precise(env, st);
				return 0;
			}

			if (func->stack[i].slot_type[0] != STACK_SPILL) {
				stack_mask &= ~(1ull << i);
				continue;
			}
			reg = &func->stack[i].spilled_ptr;
			if (reg->type != SCALAR_VALUE) {
				stack_mask &= ~(1ull << i);
				continue;
			}
			if (!reg->precise)
				new_marks = true;
			reg->precise = true;
		}
		if (env->log.level & BPF_LOG_LEVEL) {
			print_verifier_state(env, func);
			verbose(env, "parent %s regs=%x stack=%llx marks\n",
				new_marks ? "didn't have" : "already had",
				reg_mask, stack_mask);
		}

		if (!reg_mask && !stack_mask)
			break;
		if (!new_marks)
			break;

		last_idx = st->last_insn_idx;
		first_idx = st->first_insn_idx;
	}
	return 0;
}

static int mark_chain_precision(struct bpf_verifier_env *env, int regno)
{
	return __mark_chain_precision(env, regno, -1);
}

static int mark_chain_precision_stack(struct bpf_verifier_env *env, int spi)
{
	return __mark_chain_precision(env, -1, spi);
}

static bool is_spillable_regtype(enum bpf_reg_type type)
{
	switch (type) {
	case PTR_TO_MAP_VALUE:
	case PTR_TO_MAP_VALUE_OR_NULL:
	case PTR_TO_STACK:
	case PTR_TO_CTX:
	case PTR_TO_PACKET:
	case PTR_TO_PACKET_META:
	case PTR_TO_PACKET_END:
	case PTR_TO_FLOW_KEYS:
	case CONST_PTR_TO_MAP:
	case PTR_TO_SOCKET:
	case PTR_TO_SOCKET_OR_NULL:
	case PTR_TO_SOCK_COMMON:
	case PTR_TO_SOCK_COMMON_OR_NULL:
	case PTR_TO_TCP_SOCK:
	case PTR_TO_TCP_SOCK_OR_NULL:
	case PTR_TO_XDP_SOCK:
	case PTR_TO_BTF_ID:
	case PTR_TO_BTF_ID_OR_NULL:
	case PTR_TO_RDONLY_BUF:
	case PTR_TO_RDONLY_BUF_OR_NULL:
	case PTR_TO_RDWR_BUF:
	case PTR_TO_RDWR_BUF_OR_NULL:
	case PTR_TO_PERCPU_BTF_ID:
	case PTR_TO_MEM:
	case PTR_TO_MEM_OR_NULL:
		return true;
	default:
		return false;
	}
}

/* Does this register contain a constant zero? */
static bool register_is_null(struct bpf_reg_state *reg)
{
	return reg->type == SCALAR_VALUE && tnum_equals_const(reg->var_off, 0);
}

static bool register_is_const(struct bpf_reg_state *reg)
{
	return reg->type == SCALAR_VALUE && tnum_is_const(reg->var_off);
}

static bool __is_scalar_unbounded(struct bpf_reg_state *reg)
{
	return tnum_is_unknown(reg->var_off) &&
	       reg->smin_value == S64_MIN && reg->smax_value == S64_MAX &&
	       reg->umin_value == 0 && reg->umax_value == U64_MAX &&
	       reg->s32_min_value == S32_MIN && reg->s32_max_value == S32_MAX &&
	       reg->u32_min_value == 0 && reg->u32_max_value == U32_MAX;
}

static bool register_is_bounded(struct bpf_reg_state *reg)
{
	return reg->type == SCALAR_VALUE && !__is_scalar_unbounded(reg);
}

static bool __is_pointer_value(bool allow_ptr_leaks,
			       const struct bpf_reg_state *reg)
{
	if (allow_ptr_leaks)
		return false;

	return reg->type != SCALAR_VALUE;
}

static void save_register_state(struct bpf_func_state *state,
				int spi, struct bpf_reg_state *reg)
{
	int i;

	state->stack[spi].spilled_ptr = *reg;
	state->stack[spi].spilled_ptr.live |= REG_LIVE_WRITTEN;

	for (i = 0; i < BPF_REG_SIZE; i++)
		state->stack[spi].slot_type[i] = STACK_SPILL;
}

/* check_stack_{read,write}_fixed_off functions track spill/fill of registers,
 * stack boundary and alignment are checked in check_mem_access()
 */
static int check_stack_write_fixed_off(struct bpf_verifier_env *env,
				       /* stack frame we're writing to */
				       struct bpf_func_state *state,
				       int off, int size, int value_regno,
				       int insn_idx)
{
	struct bpf_func_state *cur; /* state of the current function */
	int i, slot = -off - 1, spi = slot / BPF_REG_SIZE, err;
	u32 dst_reg = env->prog->insnsi[insn_idx].dst_reg;
	struct bpf_reg_state *reg = NULL;

	err = realloc_func_state(state, round_up(slot + 1, BPF_REG_SIZE),
				 state->acquired_refs, true);
	if (err)
		return err;
	/* caller checked that off % size == 0 and -MAX_BPF_STACK <= off < 0,
	 * so it's aligned access and [off, off + size) are within stack limits
	 */
	if (!env->allow_ptr_leaks &&
	    state->stack[spi].slot_type[0] == STACK_SPILL &&
	    size != BPF_REG_SIZE) {
		verbose(env, "attempt to corrupt spilled pointer on stack\n");
		return -EACCES;
	}

	cur = env->cur_state->frame[env->cur_state->curframe];
	if (value_regno >= 0)
		reg = &cur->regs[value_regno];

	if (reg && size == BPF_REG_SIZE && register_is_bounded(reg) &&
	    !register_is_null(reg) && env->bpf_capable) {
		if (dst_reg != BPF_REG_FP) {
			/* The backtracking logic can only recognize explicit
			 * stack slot address like [fp - 8]. Other spill of
			 * scalar via different register has to be conervative.
			 * Backtrack from here and mark all registers as precise
			 * that contributed into 'reg' being a constant.
			 */
			err = mark_chain_precision(env, value_regno);
			if (err)
				return err;
		}
		save_register_state(state, spi, reg);
	} else if (reg && is_spillable_regtype(reg->type)) {
		/* register containing pointer is being spilled into stack */
		if (size != BPF_REG_SIZE) {
			verbose_linfo(env, insn_idx, "; ");
			verbose(env, "invalid size of register spill\n");
			return -EACCES;
		}

		if (state != cur && reg->type == PTR_TO_STACK) {
			verbose(env, "cannot spill pointers to stack into stack frame of the caller\n");
			return -EINVAL;
		}

		if (!env->bypass_spec_v4) {
			bool sanitize = false;

			if (state->stack[spi].slot_type[0] == STACK_SPILL &&
			    register_is_const(&state->stack[spi].spilled_ptr))
				sanitize = true;
			for (i = 0; i < BPF_REG_SIZE; i++)
				if (state->stack[spi].slot_type[i] == STACK_MISC) {
					sanitize = true;
					break;
				}
			if (sanitize) {
				int *poff = &env->insn_aux_data[insn_idx].sanitize_stack_off;
				int soff = (-spi - 1) * BPF_REG_SIZE;

				/* detected reuse of integer stack slot with a pointer
				 * which means either llvm is reusing stack slot or
				 * an attacker is trying to exploit CVE-2018-3639
				 * (speculative store bypass)
				 * Have to sanitize that slot with preemptive
				 * store of zero.
				 */
				if (*poff && *poff != soff) {
					/* disallow programs where single insn stores
					 * into two different stack slots, since verifier
					 * cannot sanitize them
					 */
					verbose(env,
						"insn %d cannot access two stack slots fp%d and fp%d",
						insn_idx, *poff, soff);
					return -EINVAL;
				}
				*poff = soff;
			}
		}
		save_register_state(state, spi, reg);
	} else {
		u8 type = STACK_MISC;

		/* regular write of data into stack destroys any spilled ptr */
		state->stack[spi].spilled_ptr.type = NOT_INIT;
		/* Mark slots as STACK_MISC if they belonged to spilled ptr. */
		if (state->stack[spi].slot_type[0] == STACK_SPILL)
			for (i = 0; i < BPF_REG_SIZE; i++)
				state->stack[spi].slot_type[i] = STACK_MISC;

		/* only mark the slot as written if all 8 bytes were written
		 * otherwise read propagation may incorrectly stop too soon
		 * when stack slots are partially written.
		 * This heuristic means that read propagation will be
		 * conservative, since it will add reg_live_read marks
		 * to stack slots all the way to first state when programs
		 * writes+reads less than 8 bytes
		 */
		if (size == BPF_REG_SIZE)
			state->stack[spi].spilled_ptr.live |= REG_LIVE_WRITTEN;

		/* when we zero initialize stack slots mark them as such */
		if (reg && register_is_null(reg)) {
			/* backtracking doesn't work for STACK_ZERO yet. */
			err = mark_chain_precision(env, value_regno);
			if (err)
				return err;
			type = STACK_ZERO;
		}

		/* Mark slots affected by this stack write. */
		for (i = 0; i < size; i++)
			state->stack[spi].slot_type[(slot - i) % BPF_REG_SIZE] =
				type;
	}
	return 0;
}

/* Write the stack: 'stack[ptr_regno + off] = value_regno'. 'ptr_regno' is
 * known to contain a variable offset.
 * This function checks whether the write is permitted and conservatively
 * tracks the effects of the write, considering that each stack slot in the
 * dynamic range is potentially written to.
 *
 * 'off' includes 'regno->off'.
 * 'value_regno' can be -1, meaning that an unknown value is being written to
 * the stack.
 *
 * Spilled pointers in range are not marked as written because we don't know
 * what's going to be actually written. This means that read propagation for
 * future reads cannot be terminated by this write.
 *
 * For privileged programs, uninitialized stack slots are considered
 * initialized by this write (even though we don't know exactly what offsets
 * are going to be written to). The idea is that we don't want the verifier to
 * reject future reads that access slots written to through variable offsets.
 */
static int check_stack_write_var_off(struct bpf_verifier_env *env,
				     /* func where register points to */
				     struct bpf_func_state *state,
				     int ptr_regno, int off, int size,
				     int value_regno, int insn_idx)
{
	struct bpf_func_state *cur; /* state of the current function */
	int min_off, max_off;
	int i, err;
	struct bpf_reg_state *ptr_reg = NULL, *value_reg = NULL;
	bool writing_zero = false;
	/* set if the fact that we're writing a zero is used to let any
	 * stack slots remain STACK_ZERO
	 */
	bool zero_used = false;

	cur = env->cur_state->frame[env->cur_state->curframe];
	ptr_reg = &cur->regs[ptr_regno];
	min_off = ptr_reg->smin_value + off;
	max_off = ptr_reg->smax_value + off + size;
	if (value_regno >= 0)
		value_reg = &cur->regs[value_regno];
	if (value_reg && register_is_null(value_reg))
		writing_zero = true;

	err = realloc_func_state(state, round_up(-min_off, BPF_REG_SIZE),
				 state->acquired_refs, true);
	if (err)
		return err;


	/* Variable offset writes destroy any spilled pointers in range. */
	for (i = min_off; i < max_off; i++) {
		u8 new_type, *stype;
		int slot, spi;

		slot = -i - 1;
		spi = slot / BPF_REG_SIZE;
		stype = &state->stack[spi].slot_type[slot % BPF_REG_SIZE];

		if (!env->allow_ptr_leaks
				&& *stype != NOT_INIT
				&& *stype != SCALAR_VALUE) {
			/* Reject the write if there's are spilled pointers in
			 * range. If we didn't reject here, the ptr status
			 * would be erased below (even though not all slots are
			 * actually overwritten), possibly opening the door to
			 * leaks.
			 */
			verbose(env, "spilled ptr in range of var-offset stack write; insn %d, ptr off: %d",
				insn_idx, i);
			return -EINVAL;
		}

		/* Erase all spilled pointers. */
		state->stack[spi].spilled_ptr.type = NOT_INIT;

		/* Update the slot type. */
		new_type = STACK_MISC;
		if (writing_zero && *stype == STACK_ZERO) {
			new_type = STACK_ZERO;
			zero_used = true;
		}
		/* If the slot is STACK_INVALID, we check whether it's OK to
		 * pretend that it will be initialized by this write. The slot
		 * might not actually be written to, and so if we mark it as
		 * initialized future reads might leak uninitialized memory.
		 * For privileged programs, we will accept such reads to slots
		 * that may or may not be written because, if we're reject
		 * them, the error would be too confusing.
		 */
		if (*stype == STACK_INVALID && !env->allow_uninit_stack) {
			verbose(env, "uninit stack in range of var-offset write prohibited for !root; insn %d, off: %d",
					insn_idx, i);
			return -EINVAL;
		}
		*stype = new_type;
	}
	if (zero_used) {
		/* backtracking doesn't work for STACK_ZERO yet. */
		err = mark_chain_precision(env, value_regno);
		if (err)
			return err;
	}
	return 0;
}

/* When register 'dst_regno' is assigned some values from stack[min_off,
 * max_off), we set the register's type according to the types of the
 * respective stack slots. If all the stack values are known to be zeros, then
 * so is the destination reg. Otherwise, the register is considered to be
 * SCALAR. This function does not deal with register filling; the caller must
 * ensure that all spilled registers in the stack range have been marked as
 * read.
 */
static void mark_reg_stack_read(struct bpf_verifier_env *env,
				/* func where src register points to */
				struct bpf_func_state *ptr_state,
				int min_off, int max_off, int dst_regno)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	struct bpf_func_state *state = vstate->frame[vstate->curframe];
	int i, slot, spi;
	u8 *stype;
	int zeros = 0;

	for (i = min_off; i < max_off; i++) {
		slot = -i - 1;
		spi = slot / BPF_REG_SIZE;
		stype = ptr_state->stack[spi].slot_type;
		if (stype[slot % BPF_REG_SIZE] != STACK_ZERO)
			break;
		zeros++;
	}
	if (zeros == max_off - min_off) {
		/* any access_size read into register is zero extended,
		 * so the whole register == const_zero
		 */
		__mark_reg_const_zero(&state->regs[dst_regno]);
		/* backtracking doesn't support STACK_ZERO yet,
		 * so mark it precise here, so that later
		 * backtracking can stop here.
		 * Backtracking may not need this if this register
		 * doesn't participate in pointer adjustment.
		 * Forward propagation of precise flag is not
		 * necessary either. This mark is only to stop
		 * backtracking. Any register that contributed
		 * to const 0 was marked precise before spill.
		 */
		state->regs[dst_regno].precise = true;
	} else {
		/* have read misc data from the stack */
		mark_reg_unknown(env, state->regs, dst_regno);
	}
	state->regs[dst_regno].live |= REG_LIVE_WRITTEN;
}

/* Read the stack at 'off' and put the results into the register indicated by
 * 'dst_regno'. It handles reg filling if the addressed stack slot is a
 * spilled reg.
 *
 * 'dst_regno' can be -1, meaning that the read value is not going to a
 * register.
 *
 * The access is assumed to be within the current stack bounds.
 */
static int check_stack_read_fixed_off(struct bpf_verifier_env *env,
				      /* func where src register points to */
				      struct bpf_func_state *reg_state,
				      int off, int size, int dst_regno)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	struct bpf_func_state *state = vstate->frame[vstate->curframe];
	int i, slot = -off - 1, spi = slot / BPF_REG_SIZE;
	struct bpf_reg_state *reg;
	u8 *stype;

	stype = reg_state->stack[spi].slot_type;
	reg = &reg_state->stack[spi].spilled_ptr;

	if (stype[0] == STACK_SPILL) {
		if (size != BPF_REG_SIZE) {
			if (reg->type != SCALAR_VALUE) {
				verbose_linfo(env, env->insn_idx, "; ");
				verbose(env, "invalid size of register fill\n");
				return -EACCES;
			}
			if (dst_regno >= 0) {
				mark_reg_unknown(env, state->regs, dst_regno);
				state->regs[dst_regno].live |= REG_LIVE_WRITTEN;
			}
			mark_reg_read(env, reg, reg->parent, REG_LIVE_READ64);
			return 0;
		}
		for (i = 1; i < BPF_REG_SIZE; i++) {
			if (stype[(slot - i) % BPF_REG_SIZE] != STACK_SPILL) {
				verbose(env, "corrupted spill memory\n");
				return -EACCES;
			}
		}

		if (dst_regno >= 0) {
			/* restore register state from stack */
			state->regs[dst_regno] = *reg;
			/* mark reg as written since spilled pointer state likely
			 * has its liveness marks cleared by is_state_visited()
			 * which resets stack/reg liveness for state transitions
			 */
			state->regs[dst_regno].live |= REG_LIVE_WRITTEN;
		} else if (__is_pointer_value(env->allow_ptr_leaks, reg)) {
			/* If dst_regno==-1, the caller is asking us whether
			 * it is acceptable to use this value as a SCALAR_VALUE
			 * (e.g. for XADD).
			 * We must not allow unprivileged callers to do that
			 * with spilled pointers.
			 */
			verbose(env, "leaking pointer from stack off %d\n",
				off);
			return -EACCES;
		}
		mark_reg_read(env, reg, reg->parent, REG_LIVE_READ64);
	} else {
		u8 type;

		for (i = 0; i < size; i++) {
			type = stype[(slot - i) % BPF_REG_SIZE];
			if (type == STACK_MISC)
				continue;
			if (type == STACK_ZERO)
				continue;
			verbose(env, "invalid read from stack off %d+%d size %d\n",
				off, i, size);
			return -EACCES;
		}
		mark_reg_read(env, reg, reg->parent, REG_LIVE_READ64);
		if (dst_regno >= 0)
			mark_reg_stack_read(env, reg_state, off, off + size, dst_regno);
	}
	return 0;
}

enum stack_access_src {
	ACCESS_DIRECT = 1,  /* the access is performed by an instruction */
	ACCESS_HELPER = 2,  /* the access is performed by a helper */
};

static int check_stack_range_initialized(struct bpf_verifier_env *env,
					 int regno, int off, int access_size,
					 bool zero_size_allowed,
					 enum stack_access_src type,
					 struct bpf_call_arg_meta *meta);

static struct bpf_reg_state *reg_state(struct bpf_verifier_env *env, int regno)
{
	return cur_regs(env) + regno;
}

/* Read the stack at 'ptr_regno + off' and put the result into the register
 * 'dst_regno'.
 * 'off' includes the pointer register's fixed offset(i.e. 'ptr_regno.off'),
 * but not its variable offset.
 * 'size' is assumed to be <= reg size and the access is assumed to be aligned.
 *
 * As opposed to check_stack_read_fixed_off, this function doesn't deal with
 * filling registers (i.e. reads of spilled register cannot be detected when
 * the offset is not fixed). We conservatively mark 'dst_regno' as containing
 * SCALAR_VALUE. That's why we assert that the 'ptr_regno' has a variable
 * offset; for a fixed offset check_stack_read_fixed_off should be used
 * instead.
 */
static int check_stack_read_var_off(struct bpf_verifier_env *env,
				    int ptr_regno, int off, int size, int dst_regno)
{
	/* The state of the source register. */
	struct bpf_reg_state *reg = reg_state(env, ptr_regno);
	struct bpf_func_state *ptr_state = func(env, reg);
	int err;
	int min_off, max_off;

	/* Note that we pass a NULL meta, so raw access will not be permitted.
	 */
	err = check_stack_range_initialized(env, ptr_regno, off, size,
					    false, ACCESS_DIRECT, NULL);
	if (err)
		return err;

	min_off = reg->smin_value + off;
	max_off = reg->smax_value + off;
	mark_reg_stack_read(env, ptr_state, min_off, max_off + size, dst_regno);
	return 0;
}

/* check_stack_read dispatches to check_stack_read_fixed_off or
 * check_stack_read_var_off.
 *
 * The caller must ensure that the offset falls within the allocated stack
 * bounds.
 *
 * 'dst_regno' is a register which will receive the value from the stack. It
 * can be -1, meaning that the read value is not going to a register.
 */
static int check_stack_read(struct bpf_verifier_env *env,
			    int ptr_regno, int off, int size,
			    int dst_regno)
{
	struct bpf_reg_state *reg = reg_state(env, ptr_regno);
	struct bpf_func_state *state = func(env, reg);
	int err;
	/* Some accesses are only permitted with a static offset. */
	bool var_off = !tnum_is_const(reg->var_off);

	/* The offset is required to be static when reads don't go to a
	 * register, in order to not leak pointers (see
	 * check_stack_read_fixed_off).
	 */
	if (dst_regno < 0 && var_off) {
		char tn_buf[48];

		tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
		verbose(env, "variable offset stack pointer cannot be passed into helper function; var_off=%s off=%d size=%d\n",
			tn_buf, off, size);
		return -EACCES;
	}
	/* Variable offset is prohibited for unprivileged mode for simplicity
	 * since it requires corresponding support in Spectre masking for stack
	 * ALU. See also retrieve_ptr_limit().
	 */
	if (!env->bypass_spec_v1 && var_off) {
		char tn_buf[48];

		tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
		verbose(env, "R%d variable offset stack access prohibited for !root, var_off=%s\n",
				ptr_regno, tn_buf);
		return -EACCES;
	}

	if (!var_off) {
		off += reg->var_off.value;
		err = check_stack_read_fixed_off(env, state, off, size,
						 dst_regno);
	} else {
		/* Variable offset stack reads need more conservative handling
		 * than fixed offset ones. Note that dst_regno >= 0 on this
		 * branch.
		 */
		err = check_stack_read_var_off(env, ptr_regno, off, size,
					       dst_regno);
	}
	return err;
}


/* check_stack_write dispatches to check_stack_write_fixed_off or
 * check_stack_write_var_off.
 *
 * 'ptr_regno' is the register used as a pointer into the stack.
 * 'off' includes 'ptr_regno->off', but not its variable offset (if any).
 * 'value_regno' is the register whose value we're writing to the stack. It can
 * be -1, meaning that we're not writing from a register.
 *
 * The caller must ensure that the offset falls within the maximum stack size.
 */
static int check_stack_write(struct bpf_verifier_env *env,
			     int ptr_regno, int off, int size,
			     int value_regno, int insn_idx)
{
	struct bpf_reg_state *reg = reg_state(env, ptr_regno);
	struct bpf_func_state *state = func(env, reg);
	int err;

	if (tnum_is_const(reg->var_off)) {
		off += reg->var_off.value;
		err = check_stack_write_fixed_off(env, state, off, size,
						  value_regno, insn_idx);
	} else {
		/* Variable offset stack reads need more conservative handling
		 * than fixed offset ones.
		 */
		err = check_stack_write_var_off(env, state,
						ptr_regno, off, size,
						value_regno, insn_idx);
	}
	return err;
}

static int check_map_access_type(struct bpf_verifier_env *env, u32 regno,
				 int off, int size, enum bpf_access_type type)
{
	struct bpf_reg_state *regs = cur_regs(env);
	struct bpf_map *map = regs[regno].map_ptr;
	u32 cap = bpf_map_flags_to_cap(map);

	if (type == BPF_WRITE && !(cap & BPF_MAP_CAN_WRITE)) {
		verbose(env, "write into map forbidden, value_size=%d off=%d size=%d\n",
			map->value_size, off, size);
		return -EACCES;
	}

	if (type == BPF_READ && !(cap & BPF_MAP_CAN_READ)) {
		verbose(env, "read from map forbidden, value_size=%d off=%d size=%d\n",
			map->value_size, off, size);
		return -EACCES;
	}

	return 0;
}

/* check read/write into memory region (e.g., map value, ringbuf sample, etc) */
static int __check_mem_access(struct bpf_verifier_env *env, int regno,
			      int off, int size, u32 mem_size,
			      bool zero_size_allowed)
{
	bool size_ok = size > 0 || (size == 0 && zero_size_allowed);
	struct bpf_reg_state *reg;

	if (off >= 0 && size_ok && (u64)off + size <= mem_size)
		return 0;

	reg = &cur_regs(env)[regno];
	switch (reg->type) {
	case PTR_TO_MAP_VALUE:
		verbose(env, "invalid access to map value, value_size=%d off=%d size=%d\n",
			mem_size, off, size);
		break;
	case PTR_TO_PACKET:
	case PTR_TO_PACKET_META:
	case PTR_TO_PACKET_END:
		verbose(env, "invalid access to packet, off=%d size=%d, R%d(id=%d,off=%d,r=%d)\n",
			off, size, regno, reg->id, off, mem_size);
		break;
	case PTR_TO_MEM:
	default:
		verbose(env, "invalid access to memory, mem_size=%u off=%d size=%d\n",
			mem_size, off, size);
	}

	return -EACCES;
}

/* check read/write into a memory region with possible variable offset */
static int check_mem_region_access(struct bpf_verifier_env *env, u32 regno,
				   int off, int size, u32 mem_size,
				   bool zero_size_allowed)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	struct bpf_func_state *state = vstate->frame[vstate->curframe];
	struct bpf_reg_state *reg = &state->regs[regno];
	int err;

	/* We may have adjusted the register pointing to memory region, so we
	 * need to try adding each of min_value and max_value to off
	 * to make sure our theoretical access will be safe.
	 */
	if (env->log.level & BPF_LOG_LEVEL)
		print_verifier_state(env, state);

	/* The minimum value is only important with signed
	 * comparisons where we can't assume the floor of a
	 * value is 0.  If we are using signed variables for our
	 * index'es we need to make sure that whatever we use
	 * will have a set floor within our range.
	 */
	if (reg->smin_value < 0 &&
	    (reg->smin_value == S64_MIN ||
	     (off + reg->smin_value != (s64)(s32)(off + reg->smin_value)) ||
	      reg->smin_value + off < 0)) {
		verbose(env, "R%d min value is negative, either use unsigned index or do a if (index >=0) check.\n",
			regno);
		return -EACCES;
	}
	err = __check_mem_access(env, regno, reg->smin_value + off, size,
				 mem_size, zero_size_allowed);
	if (err) {
		verbose(env, "R%d min value is outside of the allowed memory range\n",
			regno);
		return err;
	}

	/* If we haven't set a max value then we need to bail since we can't be
	 * sure we won't do bad things.
	 * If reg->umax_value + off could overflow, treat that as unbounded too.
	 */
	if (reg->umax_value >= BPF_MAX_VAR_OFF) {
		verbose(env, "R%d unbounded memory access, make sure to bounds check any such access\n",
			regno);
		return -EACCES;
	}
	err = __check_mem_access(env, regno, reg->umax_value + off, size,
				 mem_size, zero_size_allowed);
	if (err) {
		verbose(env, "R%d max value is outside of the allowed memory range\n",
			regno);
		return err;
	}

	return 0;
}

/* check read/write into a map element with possible variable offset */
static int check_map_access(struct bpf_verifier_env *env, u32 regno,
			    int off, int size, bool zero_size_allowed)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	struct bpf_func_state *state = vstate->frame[vstate->curframe];
	struct bpf_reg_state *reg = &state->regs[regno];
	struct bpf_map *map = reg->map_ptr;
	int err;

	err = check_mem_region_access(env, regno, off, size, map->value_size,
				      zero_size_allowed);
	if (err)
		return err;

	if (map_value_has_spin_lock(map)) {
		u32 lock = map->spin_lock_off;

		/* if any part of struct bpf_spin_lock can be touched by
		 * load/store reject this program.
		 * To check that [x1, x2) overlaps with [y1, y2)
		 * it is sufficient to check x1 < y2 && y1 < x2.
		 */
		if (reg->smin_value + off < lock + sizeof(struct bpf_spin_lock) &&
		     lock < reg->umax_value + off + size) {
			verbose(env, "bpf_spin_lock cannot be accessed directly by load/store\n");
			return -EACCES;
		}
	}
	return err;
}

#define MAX_PACKET_OFF 0xffff

static enum bpf_prog_type resolve_prog_type(struct bpf_prog *prog)
{
	return prog->aux->dst_prog ? prog->aux->dst_prog->type : prog->type;
}

static bool may_access_direct_pkt_data(struct bpf_verifier_env *env,
				       const struct bpf_call_arg_meta *meta,
				       enum bpf_access_type t)
{
	enum bpf_prog_type prog_type = resolve_prog_type(env->prog);

	switch (prog_type) {
	/* Program types only with direct read access go here! */
	case BPF_PROG_TYPE_LWT_IN:
	case BPF_PROG_TYPE_LWT_OUT:
	case BPF_PROG_TYPE_LWT_SEG6LOCAL:
	case BPF_PROG_TYPE_SK_REUSEPORT:
	case BPF_PROG_TYPE_FLOW_DISSECTOR:
	case BPF_PROG_TYPE_CGROUP_SKB:
		if (t == BPF_WRITE)
			return false;
		fallthrough;

	/* Program types with direct read + write access go here! */
	case BPF_PROG_TYPE_SCHED_CLS:
	case BPF_PROG_TYPE_SCHED_ACT:
	case BPF_PROG_TYPE_XDP:
	case BPF_PROG_TYPE_LWT_XMIT:
	case BPF_PROG_TYPE_SK_SKB:
	case BPF_PROG_TYPE_SK_MSG:
		if (meta)
			return meta->pkt_access;

		env->seen_direct_write = true;
		return true;

	case BPF_PROG_TYPE_CGROUP_SOCKOPT:
		if (t == BPF_WRITE)
			env->seen_direct_write = true;

		return true;

	default:
		return false;
	}
}

static int check_packet_access(struct bpf_verifier_env *env, u32 regno, int off,
			       int size, bool zero_size_allowed)
{
	struct bpf_reg_state *regs = cur_regs(env);
	struct bpf_reg_state *reg = &regs[regno];
	int err;

	/* We may have added a variable offset to the packet pointer; but any
	 * reg->range we have comes after that.  We are only checking the fixed
	 * offset.
	 */

	/* We don't allow negative numbers, because we aren't tracking enough
	 * detail to prove they're safe.
	 */
	if (reg->smin_value < 0) {
		verbose(env, "R%d min value is negative, either use unsigned index or do a if (index >=0) check.\n",
			regno);
		return -EACCES;
	}

	err = reg->range < 0 ? -EINVAL :
	      __check_mem_access(env, regno, off, size, reg->range,
				 zero_size_allowed);
	if (err) {
		verbose(env, "R%d offset is outside of the packet\n", regno);
		return err;
	}

	/* __check_mem_access has made sure "off + size - 1" is within u16.
	 * reg->umax_value can't be bigger than MAX_PACKET_OFF which is 0xffff,
	 * otherwise find_good_pkt_pointers would have refused to set range info
	 * that __check_mem_access would have rejected this pkt access.
	 * Therefore, "off + reg->umax_value + size - 1" won't overflow u32.
	 */
	env->prog->aux->max_pkt_offset =
		max_t(u32, env->prog->aux->max_pkt_offset,
		      off + reg->umax_value + size - 1);

	return err;
}

/* check access to 'struct bpf_context' fields.  Supports fixed offsets only */
static int check_ctx_access(struct bpf_verifier_env *env, int insn_idx, int off, int size,
			    enum bpf_access_type t, enum bpf_reg_type *reg_type,
			    struct btf **btf, u32 *btf_id)
{
	struct bpf_insn_access_aux info = {
		.reg_type = *reg_type,
		.log = &env->log,
	};

	if (env->ops->is_valid_access &&
	    env->ops->is_valid_access(off, size, t, env->prog, &info)) {
		/* A non zero info.ctx_field_size indicates that this field is a
		 * candidate for later verifier transformation to load the whole
		 * field and then apply a mask when accessed with a narrower
		 * access than actual ctx access size. A zero info.ctx_field_size
		 * will only allow for whole field access and rejects any other
		 * type of narrower access.
		 */
		*reg_type = info.reg_type;

		if (*reg_type == PTR_TO_BTF_ID || *reg_type == PTR_TO_BTF_ID_OR_NULL) {
			*btf = info.btf;
			*btf_id = info.btf_id;
		} else {
			env->insn_aux_data[insn_idx].ctx_field_size = info.ctx_field_size;
		}
		/* remember the offset of last byte accessed in ctx */
		if (env->prog->aux->max_ctx_offset < off + size)
			env->prog->aux->max_ctx_offset = off + size;
		return 0;
	}

	verbose(env, "invalid bpf_context access off=%d size=%d\n", off, size);
	return -EACCES;
}

static int check_flow_keys_access(struct bpf_verifier_env *env, int off,
				  int size)
{
	if (size < 0 || off < 0 ||
	    (u64)off + size > sizeof(struct bpf_flow_keys)) {
		verbose(env, "invalid access to flow keys off=%d size=%d\n",
			off, size);
		return -EACCES;
	}
	return 0;
}

static int check_sock_access(struct bpf_verifier_env *env, int insn_idx,
			     u32 regno, int off, int size,
			     enum bpf_access_type t)
{
	struct bpf_reg_state *regs = cur_regs(env);
	struct bpf_reg_state *reg = &regs[regno];
	struct bpf_insn_access_aux info = {};
	bool valid;

	if (reg->smin_value < 0) {
		verbose(env, "R%d min value is negative, either use unsigned index or do a if (index >=0) check.\n",
			regno);
		return -EACCES;
	}

	switch (reg->type) {
	case PTR_TO_SOCK_COMMON:
		valid = bpf_sock_common_is_valid_access(off, size, t, &info);
		break;
	case PTR_TO_SOCKET:
		valid = bpf_sock_is_valid_access(off, size, t, &info);
		break;
	case PTR_TO_TCP_SOCK:
		valid = bpf_tcp_sock_is_valid_access(off, size, t, &info);
		break;
	case PTR_TO_XDP_SOCK:
		valid = bpf_xdp_sock_is_valid_access(off, size, t, &info);
		break;
	default:
		valid = false;
	}


	if (valid) {
		env->insn_aux_data[insn_idx].ctx_field_size =
			info.ctx_field_size;
		return 0;
	}

	verbose(env, "R%d invalid %s access off=%d size=%d\n",
		regno, reg_type_str[reg->type], off, size);

	return -EACCES;
}

static bool is_pointer_value(struct bpf_verifier_env *env, int regno)
{
	return __is_pointer_value(env->allow_ptr_leaks, reg_state(env, regno));
}

static bool is_ctx_reg(struct bpf_verifier_env *env, int regno)
{
	const struct bpf_reg_state *reg = reg_state(env, regno);

	return reg->type == PTR_TO_CTX;
}

static bool is_sk_reg(struct bpf_verifier_env *env, int regno)
{
	const struct bpf_reg_state *reg = reg_state(env, regno);

	return type_is_sk_pointer(reg->type);
}

static bool is_pkt_reg(struct bpf_verifier_env *env, int regno)
{
	const struct bpf_reg_state *reg = reg_state(env, regno);

	return type_is_pkt_pointer(reg->type);
}

static bool is_flow_key_reg(struct bpf_verifier_env *env, int regno)
{
	const struct bpf_reg_state *reg = reg_state(env, regno);

	/* Separate to is_ctx_reg() since we still want to allow BPF_ST here. */
	return reg->type == PTR_TO_FLOW_KEYS;
}

static int check_pkt_ptr_alignment(struct bpf_verifier_env *env,
				   const struct bpf_reg_state *reg,
				   int off, int size, bool strict)
{
	struct tnum reg_off;
	int ip_align;

	/* Byte size accesses are always allowed. */
	if (!strict || size == 1)
		return 0;

	/* For platforms that do not have a Kconfig enabling
	 * CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS the value of
	 * NET_IP_ALIGN is universally set to '2'.  And on platforms
	 * that do set CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS, we get
	 * to this code only in strict mode where we want to emulate
	 * the NET_IP_ALIGN==2 checking.  Therefore use an
	 * unconditional IP align value of '2'.
	 */
	ip_align = 2;

	reg_off = tnum_add(reg->var_off, tnum_const(ip_align + reg->off + off));
	if (!tnum_is_aligned(reg_off, size)) {
		char tn_buf[48];

		tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
		verbose(env,
			"misaligned packet access off %d+%s+%d+%d size %d\n",
			ip_align, tn_buf, reg->off, off, size);
		return -EACCES;
	}

	return 0;
}

static int check_generic_ptr_alignment(struct bpf_verifier_env *env,
				       const struct bpf_reg_state *reg,
				       const char *pointer_desc,
				       int off, int size, bool strict)
{
	struct tnum reg_off;

	/* Byte size accesses are always allowed. */
	if (!strict || size == 1)
		return 0;

	reg_off = tnum_add(reg->var_off, tnum_const(reg->off + off));
	if (!tnum_is_aligned(reg_off, size)) {
		char tn_buf[48];

		tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
		verbose(env, "misaligned %saccess off %s+%d+%d size %d\n",
			pointer_desc, tn_buf, reg->off, off, size);
		return -EACCES;
	}

	return 0;
}

static int check_ptr_alignment(struct bpf_verifier_env *env,
			       const struct bpf_reg_state *reg, int off,
			       int size, bool strict_alignment_once)
{
	bool strict = env->strict_alignment || strict_alignment_once;
	const char *pointer_desc = "";

	switch (reg->type) {
	case PTR_TO_PACKET:
	case PTR_TO_PACKET_META:
		/* Special case, because of NET_IP_ALIGN. Given metadata sits
		 * right in front, treat it the very same way.
		 */
		return check_pkt_ptr_alignment(env, reg, off, size, strict);
	case PTR_TO_FLOW_KEYS:
		pointer_desc = "flow keys ";
		break;
	case PTR_TO_MAP_VALUE:
		pointer_desc = "value ";
		break;
	case PTR_TO_CTX:
		pointer_desc = "context ";
		break;
	case PTR_TO_STACK:
		pointer_desc = "stack ";
		/* The stack spill tracking logic in check_stack_write_fixed_off()
		 * and check_stack_read_fixed_off() relies on stack accesses being
		 * aligned.
		 */
		strict = true;
		break;
	case PTR_TO_SOCKET:
		pointer_desc = "sock ";
		break;
	case PTR_TO_SOCK_COMMON:
		pointer_desc = "sock_common ";
		break;
	case PTR_TO_TCP_SOCK:
		pointer_desc = "tcp_sock ";
		break;
	case PTR_TO_XDP_SOCK:
		pointer_desc = "xdp_sock ";
		break;
	default:
		break;
	}
	return check_generic_ptr_alignment(env, reg, pointer_desc, off, size,
					   strict);
}

static int update_stack_depth(struct bpf_verifier_env *env,
			      const struct bpf_func_state *func,
			      int off)
{
	u16 stack = env->subprog_info[func->subprogno].stack_depth;

	if (stack >= -off)
		return 0;

	/* update known max for given subprogram */
	env->subprog_info[func->subprogno].stack_depth = -off;
	return 0;
}

/* starting from main bpf function walk all instructions of the function
 * and recursively walk all callees that given function can call.
 * Ignore jump and exit insns.
 * Since recursion is prevented by check_cfg() this algorithm
 * only needs a local stack of MAX_CALL_FRAMES to remember callsites
 */
static int check_max_stack_depth(struct bpf_verifier_env *env)
{
	int depth = 0, frame = 0, idx = 0, i = 0, subprog_end;
	struct bpf_subprog_info *subprog = env->subprog_info;
	struct bpf_insn *insn = env->prog->insnsi;
	bool tail_call_reachable = false;
	int ret_insn[MAX_CALL_FRAMES];
	int ret_prog[MAX_CALL_FRAMES];
	int j;

process_func:
	/* protect against potential stack overflow that might happen when
	 * bpf2bpf calls get combined with tailcalls. Limit the caller's stack
	 * depth for such case down to 256 so that the worst case scenario
	 * would result in 8k stack size (32 which is tailcall limit * 256 =
	 * 8k).
	 *
	 * To get the idea what might happen, see an example:
	 * func1 -> sub rsp, 128
	 *  subfunc1 -> sub rsp, 256
	 *  tailcall1 -> add rsp, 256
	 *   func2 -> sub rsp, 192 (total stack size = 128 + 192 = 320)
	 *   subfunc2 -> sub rsp, 64
	 *   subfunc22 -> sub rsp, 128
	 *   tailcall2 -> add rsp, 128
	 *    func3 -> sub rsp, 32 (total stack size 128 + 192 + 64 + 32 = 416)
	 *
	 * tailcall will unwind the current stack frame but it will not get rid
	 * of caller's stack as shown on the example above.
	 */
	if (idx && subprog[idx].has_tail_call && depth >= 256) {
		verbose(env,
			"tail_calls are not allowed when call stack of previous frames is %d bytes. Too large\n",
			depth);
		return -EACCES;
	}
	/* round up to 32-bytes, since this is granularity
	 * of interpreter stack size
	 */
	depth += round_up(max_t(u32, subprog[idx].stack_depth, 1), 32);
	if (depth > MAX_BPF_STACK) {
		verbose(env, "combined stack size of %d calls is %d. Too large\n",
			frame + 1, depth);
		return -EACCES;
	}
continue_func:
	subprog_end = subprog[idx + 1].start;
	for (; i < subprog_end; i++) {
		if (!bpf_pseudo_call(insn + i))
			continue;
		/* remember insn and function to return to */
		ret_insn[frame] = i + 1;
		ret_prog[frame] = idx;

		/* find the callee */
		i = i + insn[i].imm + 1;
		idx = find_subprog(env, i);
		if (idx < 0) {
			WARN_ONCE(1, "verifier bug. No program starts at insn %d\n",
				  i);
			return -EFAULT;
		}

		if (subprog[idx].has_tail_call)
			tail_call_reachable = true;

		frame++;
		if (frame >= MAX_CALL_FRAMES) {
			verbose(env, "the call stack of %d frames is too deep !\n",
				frame);
			return -E2BIG;
		}
		goto process_func;
	}
	/* if tail call got detected across bpf2bpf calls then mark each of the
	 * currently present subprog frames as tail call reachable subprogs;
	 * this info will be utilized by JIT so that we will be preserving the
	 * tail call counter throughout bpf2bpf calls combined with tailcalls
	 */
	if (tail_call_reachable)
		for (j = 0; j < frame; j++)
			subprog[ret_prog[j]].tail_call_reachable = true;

	/* end of for() loop means the last insn of the 'subprog'
	 * was reached. Doesn't matter whether it was JA or EXIT
	 */
	if (frame == 0)
		return 0;
	depth -= round_up(max_t(u32, subprog[idx].stack_depth, 1), 32);
	frame--;
	i = ret_insn[frame];
	idx = ret_prog[frame];
	goto continue_func;
}

#ifndef CONFIG_BPF_JIT_ALWAYS_ON
static int get_callee_stack_depth(struct bpf_verifier_env *env,
				  const struct bpf_insn *insn, int idx)
{
	int start = idx + insn->imm + 1, subprog;

	subprog = find_subprog(env, start);
	if (subprog < 0) {
		WARN_ONCE(1, "verifier bug. No program starts at insn %d\n",
			  start);
		return -EFAULT;
	}
	return env->subprog_info[subprog].stack_depth;
}
#endif

int check_ctx_reg(struct bpf_verifier_env *env,
		  const struct bpf_reg_state *reg, int regno)
{
	/* Access to ctx or passing it to a helper is only allowed in
	 * its original, unmodified form.
	 */

	if (reg->off) {
		verbose(env, "dereference of modified ctx ptr R%d off=%d disallowed\n",
			regno, reg->off);
		return -EACCES;
	}

	if (!tnum_is_const(reg->var_off) || reg->var_off.value) {
		char tn_buf[48];

		tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
		verbose(env, "variable ctx access var_off=%s disallowed\n", tn_buf);
		return -EACCES;
	}

	return 0;
}

static int __check_buffer_access(struct bpf_verifier_env *env,
				 const char *buf_info,
				 const struct bpf_reg_state *reg,
				 int regno, int off, int size)
{
	if (off < 0) {
		verbose(env,
			"R%d invalid %s buffer access: off=%d, size=%d\n",
			regno, buf_info, off, size);
		return -EACCES;
	}
	if (!tnum_is_const(reg->var_off) || reg->var_off.value) {
		char tn_buf[48];

		tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
		verbose(env,
			"R%d invalid variable buffer offset: off=%d, var_off=%s\n",
			regno, off, tn_buf);
		return -EACCES;
	}

	return 0;
}

static int check_tp_buffer_access(struct bpf_verifier_env *env,
				  const struct bpf_reg_state *reg,
				  int regno, int off, int size)
{
	int err;

	err = __check_buffer_access(env, "tracepoint", reg, regno, off, size);
	if (err)
		return err;

	if (off + size > env->prog->aux->max_tp_access)
		env->prog->aux->max_tp_access = off + size;

	return 0;
}

static int check_buffer_access(struct bpf_verifier_env *env,
			       const struct bpf_reg_state *reg,
			       int regno, int off, int size,
			       bool zero_size_allowed,
			       const char *buf_info,
			       u32 *max_access)
{
	int err;

	err = __check_buffer_access(env, buf_info, reg, regno, off, size);
	if (err)
		return err;

	if (off + size > *max_access)
		*max_access = off + size;

	return 0;
}

/* BPF architecture zero extends alu32 ops into 64-bit registesr */
static void zext_32_to_64(struct bpf_reg_state *reg)
{
	reg->var_off = tnum_subreg(reg->var_off);
	__reg_assign_32_into_64(reg);
}

/* truncate register to smaller size (in bytes)
 * must be called with size < BPF_REG_SIZE
 */
static void coerce_reg_to_size(struct bpf_reg_state *reg, int size)
{
	u64 mask;

	/* clear high bits in bit representation */
	reg->var_off = tnum_cast(reg->var_off, size);

	/* fix arithmetic bounds */
	mask = ((u64)1 << (size * 8)) - 1;
	if ((reg->umin_value & ~mask) == (reg->umax_value & ~mask)) {
		reg->umin_value &= mask;
		reg->umax_value &= mask;
	} else {
		reg->umin_value = 0;
		reg->umax_value = mask;
	}
	reg->smin_value = reg->umin_value;
	reg->smax_value = reg->umax_value;

	/* If size is smaller than 32bit register the 32bit register
	 * values are also truncated so we push 64-bit bounds into
	 * 32-bit bounds. Above were truncated < 32-bits already.
	 */
	if (size >= 4)
		return;
	__reg_combine_64_into_32(reg);
}

static bool bpf_map_is_rdonly(const struct bpf_map *map)
{
	return (map->map_flags & BPF_F_RDONLY_PROG) && map->frozen;
}

static int bpf_map_direct_read(struct bpf_map *map, int off, int size, u64 *val)
{
	void *ptr;
	u64 addr;
	int err;

	err = map->ops->map_direct_value_addr(map, &addr, off);
	if (err)
		return err;
	ptr = (void *)(long)addr + off;

	switch (size) {
	case sizeof(u8):
		*val = (u64)*(u8 *)ptr;
		break;
	case sizeof(u16):
		*val = (u64)*(u16 *)ptr;
		break;
	case sizeof(u32):
		*val = (u64)*(u32 *)ptr;
		break;
	case sizeof(u64):
		*val = *(u64 *)ptr;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int check_ptr_to_btf_access(struct bpf_verifier_env *env,
				   struct bpf_reg_state *regs,
				   int regno, int off, int size,
				   enum bpf_access_type atype,
				   int value_regno)
{
	struct bpf_reg_state *reg = regs + regno;
	const struct btf_type *t = btf_type_by_id(reg->btf, reg->btf_id);
	const char *tname = btf_name_by_offset(reg->btf, t->name_off);
	u32 btf_id;
	int ret;

	if (off < 0) {
		verbose(env,
			"R%d is ptr_%s invalid negative access: off=%d\n",
			regno, tname, off);
		return -EACCES;
	}
	if (!tnum_is_const(reg->var_off) || reg->var_off.value) {
		char tn_buf[48];

		tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
		verbose(env,
			"R%d is ptr_%s invalid variable offset: off=%d, var_off=%s\n",
			regno, tname, off, tn_buf);
		return -EACCES;
	}

	if (env->ops->btf_struct_access) {
		ret = env->ops->btf_struct_access(&env->log, reg->btf, t,
						  off, size, atype, &btf_id);
	} else {
		if (atype != BPF_READ) {
			verbose(env, "only read is supported\n");
			return -EACCES;
		}

		ret = btf_struct_access(&env->log, reg->btf, t, off, size,
					atype, &btf_id);
	}

	if (ret < 0)
		return ret;

	if (atype == BPF_READ && value_regno >= 0)
		mark_btf_ld_reg(env, regs, value_regno, ret, reg->btf, btf_id);

	return 0;
}

static int check_ptr_to_map_access(struct bpf_verifier_env *env,
				   struct bpf_reg_state *regs,
				   int regno, int off, int size,
				   enum bpf_access_type atype,
				   int value_regno)
{
	struct bpf_reg_state *reg = regs + regno;
	struct bpf_map *map = reg->map_ptr;
	const struct btf_type *t;
	const char *tname;
	u32 btf_id;
	int ret;

	if (!btf_vmlinux) {
		verbose(env, "map_ptr access not supported without CONFIG_DEBUG_INFO_BTF\n");
		return -ENOTSUPP;
	}

	if (!map->ops->map_btf_id || !*map->ops->map_btf_id) {
		verbose(env, "map_ptr access not supported for map type %d\n",
			map->map_type);
		return -ENOTSUPP;
	}

	t = btf_type_by_id(btf_vmlinux, *map->ops->map_btf_id);
	tname = btf_name_by_offset(btf_vmlinux, t->name_off);

	if (!env->allow_ptr_to_map_access) {
		verbose(env,
			"%s access is allowed only to CAP_PERFMON and CAP_SYS_ADMIN\n",
			tname);
		return -EPERM;
	}

	if (off < 0) {
		verbose(env, "R%d is %s invalid negative access: off=%d\n",
			regno, tname, off);
		return -EACCES;
	}

	if (atype != BPF_READ) {
		verbose(env, "only read from %s is supported\n", tname);
		return -EACCES;
	}

	ret = btf_struct_access(&env->log, btf_vmlinux, t, off, size, atype, &btf_id);
	if (ret < 0)
		return ret;

	if (value_regno >= 0)
		mark_btf_ld_reg(env, regs, value_regno, ret, btf_vmlinux, btf_id);

	return 0;
}

/* Check that the stack access at the given offset is within bounds. The
 * maximum valid offset is -1.
 *
 * The minimum valid offset is -MAX_BPF_STACK for writes, and
 * -state->allocated_stack for reads.
 */
static int check_stack_slot_within_bounds(int off,
					  struct bpf_func_state *state,
					  enum bpf_access_type t)
{
	int min_valid_off;

	if (t == BPF_WRITE)
		min_valid_off = -MAX_BPF_STACK;
	else
		min_valid_off = -state->allocated_stack;

	if (off < min_valid_off || off > -1)
		return -EACCES;
	return 0;
}

/* Check that the stack access at 'regno + off' falls within the maximum stack
 * bounds.
 *
 * 'off' includes `regno->offset`, but not its dynamic part (if any).
 */
static int check_stack_access_within_bounds(
		struct bpf_verifier_env *env,
		int regno, int off, int access_size,
		enum stack_access_src src, enum bpf_access_type type)
{
	struct bpf_reg_state *regs = cur_regs(env);
	struct bpf_reg_state *reg = regs + regno;
	struct bpf_func_state *state = func(env, reg);
	int min_off, max_off;
	int err;
	char *err_extra;

	if (src == ACCESS_HELPER)
		/* We don't know if helpers are reading or writing (or both). */
		err_extra = " indirect access to";
	else if (type == BPF_READ)
		err_extra = " read from";
	else
		err_extra = " write to";

	if (tnum_is_const(reg->var_off)) {
		min_off = reg->var_off.value + off;
		if (access_size > 0)
			max_off = min_off + access_size - 1;
		else
			max_off = min_off;
	} else {
		if (reg->smax_value >= BPF_MAX_VAR_OFF ||
		    reg->smin_value <= -BPF_MAX_VAR_OFF) {
			verbose(env, "invalid unbounded variable-offset%s stack R%d\n",
				err_extra, regno);
			return -EACCES;
		}
		min_off = reg->smin_value + off;
		if (access_size > 0)
			max_off = reg->smax_value + off + access_size - 1;
		else
			max_off = min_off;
	}

	err = check_stack_slot_within_bounds(min_off, state, type);
	if (!err)
		err = check_stack_slot_within_bounds(max_off, state, type);

	if (err) {
		if (tnum_is_const(reg->var_off)) {
			verbose(env, "invalid%s stack R%d off=%d size=%d\n",
				err_extra, regno, off, access_size);
		} else {
			char tn_buf[48];

			tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
			verbose(env, "invalid variable-offset%s stack R%d var_off=%s size=%d\n",
				err_extra, regno, tn_buf, access_size);
		}
	}
	return err;
}

/* check whether memory at (regno + off) is accessible for t = (read | write)
 * if t==write, value_regno is a register which value is stored into memory
 * if t==read, value_regno is a register which will receive the value from memory
 * if t==write && value_regno==-1, some unknown value is stored into memory
 * if t==read && value_regno==-1, don't care what we read from memory
 */
static int check_mem_access(struct bpf_verifier_env *env, int insn_idx, u32 regno,
			    int off, int bpf_size, enum bpf_access_type t,
			    int value_regno, bool strict_alignment_once)
{
	struct bpf_reg_state *regs = cur_regs(env);
	struct bpf_reg_state *reg = regs + regno;
	struct bpf_func_state *state;
	int size, err = 0;

	size = bpf_size_to_bytes(bpf_size);
	if (size < 0)
		return size;

	/* alignment checks will add in reg->off themselves */
	err = check_ptr_alignment(env, reg, off, size, strict_alignment_once);
	if (err)
		return err;

	/* for access checks, reg->off is just part of off */
	off += reg->off;

	if (reg->type == PTR_TO_MAP_VALUE) {
		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose(env, "R%d leaks addr into map\n", value_regno);
			return -EACCES;
		}
		err = check_map_access_type(env, regno, off, size, t);
		if (err)
			return err;
		err = check_map_access(env, regno, off, size, false);
		if (!err && t == BPF_READ && value_regno >= 0) {
			struct bpf_map *map = reg->map_ptr;

			/* if map is read-only, track its contents as scalars */
			if (tnum_is_const(reg->var_off) &&
			    bpf_map_is_rdonly(map) &&
			    map->ops->map_direct_value_addr) {
				int map_off = off + reg->var_off.value;
				u64 val = 0;

				err = bpf_map_direct_read(map, map_off, size,
							  &val);
				if (err)
					return err;

				regs[value_regno].type = SCALAR_VALUE;
				__mark_reg_known(&regs[value_regno], val);
			} else {
				mark_reg_unknown(env, regs, value_regno);
			}
		}
	} else if (reg->type == PTR_TO_MEM) {
		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose(env, "R%d leaks addr into mem\n", value_regno);
			return -EACCES;
		}
		err = check_mem_region_access(env, regno, off, size,
					      reg->mem_size, false);
		if (!err && t == BPF_READ && value_regno >= 0)
			mark_reg_unknown(env, regs, value_regno);
	} else if (reg->type == PTR_TO_CTX) {
		enum bpf_reg_type reg_type = SCALAR_VALUE;
		struct btf *btf = NULL;
		u32 btf_id = 0;

		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose(env, "R%d leaks addr into ctx\n", value_regno);
			return -EACCES;
		}

		err = check_ctx_reg(env, reg, regno);
		if (err < 0)
			return err;

		err = check_ctx_access(env, insn_idx, off, size, t, &reg_type, &btf, &btf_id);
		if (err)
			verbose_linfo(env, insn_idx, "; ");
		if (!err && t == BPF_READ && value_regno >= 0) {
			/* ctx access returns either a scalar, or a
			 * PTR_TO_PACKET[_META,_END]. In the latter
			 * case, we know the offset is zero.
			 */
			if (reg_type == SCALAR_VALUE) {
				mark_reg_unknown(env, regs, value_regno);
			} else {
				mark_reg_known_zero(env, regs,
						    value_regno);
				if (reg_type_may_be_null(reg_type))
					regs[value_regno].id = ++env->id_gen;
				/* A load of ctx field could have different
				 * actual load size with the one encoded in the
				 * insn. When the dst is PTR, it is for sure not
				 * a sub-register.
				 */
				regs[value_regno].subreg_def = DEF_NOT_SUBREG;
				if (reg_type == PTR_TO_BTF_ID ||
				    reg_type == PTR_TO_BTF_ID_OR_NULL) {
					regs[value_regno].btf = btf;
					regs[value_regno].btf_id = btf_id;
				}
			}
			regs[value_regno].type = reg_type;
		}

	} else if (reg->type == PTR_TO_STACK) {
		/* Basic bounds checks. */
		err = check_stack_access_within_bounds(env, regno, off, size, ACCESS_DIRECT, t);
		if (err)
			return err;

		state = func(env, reg);
		err = update_stack_depth(env, state, off);
		if (err)
			return err;

		if (t == BPF_READ)
			err = check_stack_read(env, regno, off, size,
					       value_regno);
		else
			err = check_stack_write(env, regno, off, size,
						value_regno, insn_idx);
	} else if (reg_is_pkt_pointer(reg)) {
		if (t == BPF_WRITE && !may_access_direct_pkt_data(env, NULL, t)) {
			verbose(env, "cannot write into packet\n");
			return -EACCES;
		}
		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose(env, "R%d leaks addr into packet\n",
				value_regno);
			return -EACCES;
		}
		err = check_packet_access(env, regno, off, size, false);
		if (!err && t == BPF_READ && value_regno >= 0)
			mark_reg_unknown(env, regs, value_regno);
	} else if (reg->type == PTR_TO_FLOW_KEYS) {
		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose(env, "R%d leaks addr into flow keys\n",
				value_regno);
			return -EACCES;
		}

		err = check_flow_keys_access(env, off, size);
		if (!err && t == BPF_READ && value_regno >= 0)
			mark_reg_unknown(env, regs, value_regno);
	} else if (type_is_sk_pointer(reg->type)) {
		if (t == BPF_WRITE) {
			verbose(env, "R%d cannot write into %s\n",
				regno, reg_type_str[reg->type]);
			return -EACCES;
		}
		err = check_sock_access(env, insn_idx, regno, off, size, t);
		if (!err && value_regno >= 0)
			mark_reg_unknown(env, regs, value_regno);
	} else if (reg->type == PTR_TO_TP_BUFFER) {
		err = check_tp_buffer_access(env, reg, regno, off, size);
		if (!err && t == BPF_READ && value_regno >= 0)
			mark_reg_unknown(env, regs, value_regno);
	} else if (reg->type == PTR_TO_BTF_ID) {
		err = check_ptr_to_btf_access(env, regs, regno, off, size, t,
					      value_regno);
	} else if (reg->type == CONST_PTR_TO_MAP) {
		err = check_ptr_to_map_access(env, regs, regno, off, size, t,
					      value_regno);
	} else if (reg->type == PTR_TO_RDONLY_BUF) {
		if (t == BPF_WRITE) {
			verbose(env, "R%d cannot write into %s\n",
				regno, reg_type_str[reg->type]);
			return -EACCES;
		}
		err = check_buffer_access(env, reg, regno, off, size, false,
					  "rdonly",
					  &env->prog->aux->max_rdonly_access);
		if (!err && value_regno >= 0)
			mark_reg_unknown(env, regs, value_regno);
	} else if (reg->type == PTR_TO_RDWR_BUF) {
		err = check_buffer_access(env, reg, regno, off, size, false,
					  "rdwr",
					  &env->prog->aux->max_rdwr_access);
		if (!err && t == BPF_READ && value_regno >= 0)
			mark_reg_unknown(env, regs, value_regno);
	} else {
		verbose(env, "R%d invalid mem access '%s'\n", regno,
			reg_type_str[reg->type]);
		return -EACCES;
	}

	if (!err && size < BPF_REG_SIZE && value_regno >= 0 && t == BPF_READ &&
	    regs[value_regno].type == SCALAR_VALUE) {
		/* b/h/w load zero-extends, mark upper bits as known 0 */
		coerce_reg_to_size(&regs[value_regno], size);
	}
	return err;
}

static int check_atomic(struct bpf_verifier_env *env, int insn_idx, struct bpf_insn *insn)
{
	int load_reg;
	int err;

	switch (insn->imm) {
	case BPF_ADD:
	case BPF_ADD | BPF_FETCH:
	case BPF_AND:
	case BPF_AND | BPF_FETCH:
	case BPF_OR:
	case BPF_OR | BPF_FETCH:
	case BPF_XOR:
	case BPF_XOR | BPF_FETCH:
	case BPF_XCHG:
	case BPF_CMPXCHG:
		break;
	default:
		verbose(env, "BPF_ATOMIC uses invalid atomic opcode %02x\n", insn->imm);
		return -EINVAL;
	}

	if (BPF_SIZE(insn->code) != BPF_W && BPF_SIZE(insn->code) != BPF_DW) {
		verbose(env, "invalid atomic operand size\n");
		return -EINVAL;
	}

	/* check src1 operand */
	err = check_reg_arg(env, insn->src_reg, SRC_OP);
	if (err)
		return err;

	/* check src2 operand */
	err = check_reg_arg(env, insn->dst_reg, SRC_OP);
	if (err)
		return err;

	if (insn->imm == BPF_CMPXCHG) {
		/* Check comparison of R0 with memory location */
		err = check_reg_arg(env, BPF_REG_0, SRC_OP);
		if (err)
			return err;
	}

	if (is_pointer_value(env, insn->src_reg)) {
		verbose(env, "R%d leaks addr into mem\n", insn->src_reg);
		return -EACCES;
	}

	if (is_ctx_reg(env, insn->dst_reg) ||
	    is_pkt_reg(env, insn->dst_reg) ||
	    is_flow_key_reg(env, insn->dst_reg) ||
	    is_sk_reg(env, insn->dst_reg)) {
		verbose(env, "BPF_ATOMIC stores into R%d %s is not allowed\n",
			insn->dst_reg,
			reg_type_str[reg_state(env, insn->dst_reg)->type]);
		return -EACCES;
	}

	if (insn->imm & BPF_FETCH) {
		if (insn->imm == BPF_CMPXCHG)
			load_reg = BPF_REG_0;
		else
			load_reg = insn->src_reg;

		/* check and record load of old value */
		err = check_reg_arg(env, load_reg, DST_OP);
		if (err)
			return err;
	} else {
		/* This instruction accesses a memory location but doesn't
		 * actually load it into a register.
		 */
		load_reg = -1;
	}

	/* check whether we can read the memory */
	err = check_mem_access(env, insn_idx, insn->dst_reg, insn->off,
			       BPF_SIZE(insn->code), BPF_READ, load_reg, true);
	if (err)
		return err;

	/* check whether we can write into the same memory */
	err = check_mem_access(env, insn_idx, insn->dst_reg, insn->off,
			       BPF_SIZE(insn->code), BPF_WRITE, -1, true);
	if (err)
		return err;

	return 0;
}

/* When register 'regno' is used to read the stack (either directly or through
 * a helper function) make sure that it's within stack boundary and, depending
 * on the access type, that all elements of the stack are initialized.
 *
 * 'off' includes 'regno->off', but not its dynamic part (if any).
 *
 * All registers that have been spilled on the stack in the slots within the
 * read offsets are marked as read.
 */
static int check_stack_range_initialized(
		struct bpf_verifier_env *env, int regno, int off,
		int access_size, bool zero_size_allowed,
		enum stack_access_src type, struct bpf_call_arg_meta *meta)
{
	struct bpf_reg_state *reg = reg_state(env, regno);
	struct bpf_func_state *state = func(env, reg);
	int err, min_off, max_off, i, j, slot, spi;
	char *err_extra = type == ACCESS_HELPER ? " indirect" : "";
	enum bpf_access_type bounds_check_type;
	/* Some accesses can write anything into the stack, others are
	 * read-only.
	 */
	bool clobber = false;

	if (access_size == 0 && !zero_size_allowed) {
		verbose(env, "invalid zero-sized read\n");
		return -EACCES;
	}

	if (type == ACCESS_HELPER) {
		/* The bounds checks for writes are more permissive than for
		 * reads. However, if raw_mode is not set, we'll do extra
		 * checks below.
		 */
		bounds_check_type = BPF_WRITE;
		clobber = true;
	} else {
		bounds_check_type = BPF_READ;
	}
	err = check_stack_access_within_bounds(env, regno, off, access_size,
					       type, bounds_check_type);
	if (err)
		return err;


	if (tnum_is_const(reg->var_off)) {
		min_off = max_off = reg->var_off.value + off;
	} else {
		/* Variable offset is prohibited for unprivileged mode for
		 * simplicity since it requires corresponding support in
		 * Spectre masking for stack ALU.
		 * See also retrieve_ptr_limit().
		 */
		if (!env->bypass_spec_v1) {
			char tn_buf[48];

			tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
			verbose(env, "R%d%s variable offset stack access prohibited for !root, var_off=%s\n",
				regno, err_extra, tn_buf);
			return -EACCES;
		}
		/* Only initialized buffer on stack is allowed to be accessed
		 * with variable offset. With uninitialized buffer it's hard to
		 * guarantee that whole memory is marked as initialized on
		 * helper return since specific bounds are unknown what may
		 * cause uninitialized stack leaking.
		 */
		if (meta && meta->raw_mode)
			meta = NULL;

		min_off = reg->smin_value + off;
		max_off = reg->smax_value + off;
	}

	if (meta && meta->raw_mode) {
		meta->access_size = access_size;
		meta->regno = regno;
		return 0;
	}

	for (i = min_off; i < max_off + access_size; i++) {
		u8 *stype;

		slot = -i - 1;
		spi = slot / BPF_REG_SIZE;
		if (state->allocated_stack <= slot)
			goto err;
		stype = &state->stack[spi].slot_type[slot % BPF_REG_SIZE];
		if (*stype == STACK_MISC)
			goto mark;
		if (*stype == STACK_ZERO) {
			if (clobber) {
				/* helper can write anything into the stack */
				*stype = STACK_MISC;
			}
			goto mark;
		}

		if (state->stack[spi].slot_type[0] == STACK_SPILL &&
		    state->stack[spi].spilled_ptr.type == PTR_TO_BTF_ID)
			goto mark;

		if (state->stack[spi].slot_type[0] == STACK_SPILL &&
		    (state->stack[spi].spilled_ptr.type == SCALAR_VALUE ||
		     env->allow_ptr_leaks)) {
			if (clobber) {
				__mark_reg_unknown(env, &state->stack[spi].spilled_ptr);
				for (j = 0; j < BPF_REG_SIZE; j++)
					state->stack[spi].slot_type[j] = STACK_MISC;
			}
			goto mark;
		}

err:
		if (tnum_is_const(reg->var_off)) {
			verbose(env, "invalid%s read from stack R%d off %d+%d size %d\n",
				err_extra, regno, min_off, i - min_off, access_size);
		} else {
			char tn_buf[48];

			tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
			verbose(env, "invalid%s read from stack R%d var_off %s+%d size %d\n",
				err_extra, regno, tn_buf, i - min_off, access_size);
		}
		return -EACCES;
mark:
		/* reading any byte out of 8-byte 'spill_slot' will cause
		 * the whole slot to be marked as 'read'
		 */
		mark_reg_read(env, &state->stack[spi].spilled_ptr,
			      state->stack[spi].spilled_ptr.parent,
			      REG_LIVE_READ64);
	}
	return update_stack_depth(env, state, min_off);
}

static int check_helper_mem_access(struct bpf_verifier_env *env, int regno,
				   int access_size, bool zero_size_allowed,
				   struct bpf_call_arg_meta *meta)
{
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];

	switch (reg->type) {
	case PTR_TO_PACKET:
	case PTR_TO_PACKET_META:
		return check_packet_access(env, regno, reg->off, access_size,
					   zero_size_allowed);
	case PTR_TO_MAP_VALUE:
		if (check_map_access_type(env, regno, reg->off, access_size,
					  meta && meta->raw_mode ? BPF_WRITE :
					  BPF_READ))
			return -EACCES;
		return check_map_access(env, regno, reg->off, access_size,
					zero_size_allowed);
	case PTR_TO_MEM:
		return check_mem_region_access(env, regno, reg->off,
					       access_size, reg->mem_size,
					       zero_size_allowed);
	case PTR_TO_RDONLY_BUF:
		if (meta && meta->raw_mode)
			return -EACCES;
		return check_buffer_access(env, reg, regno, reg->off,
					   access_size, zero_size_allowed,
					   "rdonly",
					   &env->prog->aux->max_rdonly_access);
	case PTR_TO_RDWR_BUF:
		return check_buffer_access(env, reg, regno, reg->off,
					   access_size, zero_size_allowed,
					   "rdwr",
					   &env->prog->aux->max_rdwr_access);
	case PTR_TO_STACK:
		return check_stack_range_initialized(
				env,
				regno, reg->off, access_size,
				zero_size_allowed, ACCESS_HELPER, meta);
	default: /* scalar_value or invalid ptr */
		/* Allow zero-byte read from NULL, regardless of pointer type */
		if (zero_size_allowed && access_size == 0 &&
		    register_is_null(reg))
			return 0;

		verbose(env, "R%d type=%s expected=%s\n", regno,
			reg_type_str[reg->type],
			reg_type_str[PTR_TO_STACK]);
		return -EACCES;
	}
}

int check_mem_reg(struct bpf_verifier_env *env, struct bpf_reg_state *reg,
		   u32 regno, u32 mem_size)
{
	if (register_is_null(reg))
		return 0;

	if (reg_type_may_be_null(reg->type)) {
		/* Assuming that the register contains a value check if the memory
		 * access is safe. Temporarily save and restore the register's state as
		 * the conversion shouldn't be visible to a caller.
		 */
		const struct bpf_reg_state saved_reg = *reg;
		int rv;

		mark_ptr_not_null_reg(reg);
		rv = check_helper_mem_access(env, regno, mem_size, true, NULL);
		*reg = saved_reg;
		return rv;
	}

	return check_helper_mem_access(env, regno, mem_size, true, NULL);
}

/* Implementation details:
 * bpf_map_lookup returns PTR_TO_MAP_VALUE_OR_NULL
 * Two bpf_map_lookups (even with the same key) will have different reg->id.
 * For traditional PTR_TO_MAP_VALUE the verifier clears reg->id after
 * value_or_null->value transition, since the verifier only cares about
 * the range of access to valid map value pointer and doesn't care about actual
 * address of the map element.
 * For maps with 'struct bpf_spin_lock' inside map value the verifier keeps
 * reg->id > 0 after value_or_null->value transition. By doing so
 * two bpf_map_lookups will be considered two different pointers that
 * point to different bpf_spin_locks.
 * The verifier allows taking only one bpf_spin_lock at a time to avoid
 * dead-locks.
 * Since only one bpf_spin_lock is allowed the checks are simpler than
 * reg_is_refcounted() logic. The verifier needs to remember only
 * one spin_lock instead of array of acquired_refs.
 * cur_state->active_spin_lock remembers which map value element got locked
 * and clears it after bpf_spin_unlock.
 */
static int process_spin_lock(struct bpf_verifier_env *env, int regno,
			     bool is_lock)
{
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	struct bpf_verifier_state *cur = env->cur_state;
	bool is_const = tnum_is_const(reg->var_off);
	struct bpf_map *map = reg->map_ptr;
	u64 val = reg->var_off.value;

	if (!is_const) {
		verbose(env,
			"R%d doesn't have constant offset. bpf_spin_lock has to be at the constant offset\n",
			regno);
		return -EINVAL;
	}
	if (!map->btf) {
		verbose(env,
			"map '%s' has to have BTF in order to use bpf_spin_lock\n",
			map->name);
		return -EINVAL;
	}
	if (!map_value_has_spin_lock(map)) {
		if (map->spin_lock_off == -E2BIG)
			verbose(env,
				"map '%s' has more than one 'struct bpf_spin_lock'\n",
				map->name);
		else if (map->spin_lock_off == -ENOENT)
			verbose(env,
				"map '%s' doesn't have 'struct bpf_spin_lock'\n",
				map->name);
		else
			verbose(env,
				"map '%s' is not a struct type or bpf_spin_lock is mangled\n",
				map->name);
		return -EINVAL;
	}
	if (map->spin_lock_off != val + reg->off) {
		verbose(env, "off %lld doesn't point to 'struct bpf_spin_lock'\n",
			val + reg->off);
		return -EINVAL;
	}
	if (is_lock) {
		if (cur->active_spin_lock) {
			verbose(env,
				"Locking two bpf_spin_locks are not allowed\n");
			return -EINVAL;
		}
		cur->active_spin_lock = reg->id;
	} else {
		if (!cur->active_spin_lock) {
			verbose(env, "bpf_spin_unlock without taking a lock\n");
			return -EINVAL;
		}
		if (cur->active_spin_lock != reg->id) {
			verbose(env, "bpf_spin_unlock of different lock\n");
			return -EINVAL;
		}
		cur->active_spin_lock = 0;
	}
	return 0;
}

static bool arg_type_is_mem_ptr(enum bpf_arg_type type)
{
	return type == ARG_PTR_TO_MEM ||
	       type == ARG_PTR_TO_MEM_OR_NULL ||
	       type == ARG_PTR_TO_UNINIT_MEM;
}

static bool arg_type_is_mem_size(enum bpf_arg_type type)
{
	return type == ARG_CONST_SIZE ||
	       type == ARG_CONST_SIZE_OR_ZERO;
}

static bool arg_type_is_alloc_size(enum bpf_arg_type type)
{
	return type == ARG_CONST_ALLOC_SIZE_OR_ZERO;
}

static bool arg_type_is_int_ptr(enum bpf_arg_type type)
{
	return type == ARG_PTR_TO_INT ||
	       type == ARG_PTR_TO_LONG;
}

static int int_ptr_type_to_size(enum bpf_arg_type type)
{
	if (type == ARG_PTR_TO_INT)
		return sizeof(u32);
	else if (type == ARG_PTR_TO_LONG)
		return sizeof(u64);

	return -EINVAL;
}

static int resolve_map_arg_type(struct bpf_verifier_env *env,
				 const struct bpf_call_arg_meta *meta,
				 enum bpf_arg_type *arg_type)
{
	if (!meta->map_ptr) {
		/* kernel subsystem misconfigured verifier */
		verbose(env, "invalid map_ptr to access map->type\n");
		return -EACCES;
	}

	switch (meta->map_ptr->map_type) {
	case BPF_MAP_TYPE_SOCKMAP:
	case BPF_MAP_TYPE_SOCKHASH:
		if (*arg_type == ARG_PTR_TO_MAP_VALUE) {
			*arg_type = ARG_PTR_TO_BTF_ID_SOCK_COMMON;
		} else {
			verbose(env, "invalid arg_type for sockmap/sockhash\n");
			return -EINVAL;
		}
		break;

	default:
		break;
	}
	return 0;
}

struct bpf_reg_types {
	const enum bpf_reg_type types[10];
	u32 *btf_id;
};

static const struct bpf_reg_types map_key_value_types = {
	.types = {
		PTR_TO_STACK,
		PTR_TO_PACKET,
		PTR_TO_PACKET_META,
		PTR_TO_MAP_VALUE,
	},
};

static const struct bpf_reg_types sock_types = {
	.types = {
		PTR_TO_SOCK_COMMON,
		PTR_TO_SOCKET,
		PTR_TO_TCP_SOCK,
		PTR_TO_XDP_SOCK,
	},
};

#ifdef CONFIG_NET
static const struct bpf_reg_types btf_id_sock_common_types = {
	.types = {
		PTR_TO_SOCK_COMMON,
		PTR_TO_SOCKET,
		PTR_TO_TCP_SOCK,
		PTR_TO_XDP_SOCK,
		PTR_TO_BTF_ID,
	},
	.btf_id = &btf_sock_ids[BTF_SOCK_TYPE_SOCK_COMMON],
};
#endif

static const struct bpf_reg_types mem_types = {
	.types = {
		PTR_TO_STACK,
		PTR_TO_PACKET,
		PTR_TO_PACKET_META,
		PTR_TO_MAP_VALUE,
		PTR_TO_MEM,
		PTR_TO_RDONLY_BUF,
		PTR_TO_RDWR_BUF,
	},
};

static const struct bpf_reg_types int_ptr_types = {
	.types = {
		PTR_TO_STACK,
		PTR_TO_PACKET,
		PTR_TO_PACKET_META,
		PTR_TO_MAP_VALUE,
	},
};

static const struct bpf_reg_types fullsock_types = { .types = { PTR_TO_SOCKET } };
static const struct bpf_reg_types scalar_types = { .types = { SCALAR_VALUE } };
static const struct bpf_reg_types context_types = { .types = { PTR_TO_CTX } };
static const struct bpf_reg_types alloc_mem_types = { .types = { PTR_TO_MEM } };
static const struct bpf_reg_types const_map_ptr_types = { .types = { CONST_PTR_TO_MAP } };
static const struct bpf_reg_types btf_ptr_types = { .types = { PTR_TO_BTF_ID } };
static const struct bpf_reg_types spin_lock_types = { .types = { PTR_TO_MAP_VALUE } };
static const struct bpf_reg_types percpu_btf_ptr_types = { .types = { PTR_TO_PERCPU_BTF_ID } };

static const struct bpf_reg_types *compatible_reg_types[__BPF_ARG_TYPE_MAX] = {
	[ARG_PTR_TO_MAP_KEY]		= &map_key_value_types,
	[ARG_PTR_TO_MAP_VALUE]		= &map_key_value_types,
	[ARG_PTR_TO_UNINIT_MAP_VALUE]	= &map_key_value_types,
	[ARG_PTR_TO_MAP_VALUE_OR_NULL]	= &map_key_value_types,
	[ARG_CONST_SIZE]		= &scalar_types,
	[ARG_CONST_SIZE_OR_ZERO]	= &scalar_types,
	[ARG_CONST_ALLOC_SIZE_OR_ZERO]	= &scalar_types,
	[ARG_CONST_MAP_PTR]		= &const_map_ptr_types,
	[ARG_PTR_TO_CTX]		= &context_types,
	[ARG_PTR_TO_CTX_OR_NULL]	= &context_types,
	[ARG_PTR_TO_SOCK_COMMON]	= &sock_types,
#ifdef CONFIG_NET
	[ARG_PTR_TO_BTF_ID_SOCK_COMMON]	= &btf_id_sock_common_types,
#endif
	[ARG_PTR_TO_SOCKET]		= &fullsock_types,
	[ARG_PTR_TO_SOCKET_OR_NULL]	= &fullsock_types,
	[ARG_PTR_TO_BTF_ID]		= &btf_ptr_types,
	[ARG_PTR_TO_SPIN_LOCK]		= &spin_lock_types,
	[ARG_PTR_TO_MEM]		= &mem_types,
	[ARG_PTR_TO_MEM_OR_NULL]	= &mem_types,
	[ARG_PTR_TO_UNINIT_MEM]		= &mem_types,
	[ARG_PTR_TO_ALLOC_MEM]		= &alloc_mem_types,
	[ARG_PTR_TO_ALLOC_MEM_OR_NULL]	= &alloc_mem_types,
	[ARG_PTR_TO_INT]		= &int_ptr_types,
	[ARG_PTR_TO_LONG]		= &int_ptr_types,
	[ARG_PTR_TO_PERCPU_BTF_ID]	= &percpu_btf_ptr_types,
};

static int check_reg_type(struct bpf_verifier_env *env, u32 regno,
			  enum bpf_arg_type arg_type,
			  const u32 *arg_btf_id)
{
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	enum bpf_reg_type expected, type = reg->type;
	const struct bpf_reg_types *compatible;
	int i, j;

	compatible = compatible_reg_types[arg_type];
	if (!compatible) {
		verbose(env, "verifier internal error: unsupported arg type %d\n", arg_type);
		return -EFAULT;
	}

	for (i = 0; i < ARRAY_SIZE(compatible->types); i++) {
		expected = compatible->types[i];
		if (expected == NOT_INIT)
			break;

		if (type == expected)
			goto found;
	}

	verbose(env, "R%d type=%s expected=", regno, reg_type_str[type]);
	for (j = 0; j + 1 < i; j++)
		verbose(env, "%s, ", reg_type_str[compatible->types[j]]);
	verbose(env, "%s\n", reg_type_str[compatible->types[j]]);
	return -EACCES;

found:
	if (type == PTR_TO_BTF_ID) {
		if (!arg_btf_id) {
			if (!compatible->btf_id) {
				verbose(env, "verifier internal error: missing arg compatible BTF ID\n");
				return -EFAULT;
			}
			arg_btf_id = compatible->btf_id;
		}

		if (!btf_struct_ids_match(&env->log, reg->btf, reg->btf_id, reg->off,
					  btf_vmlinux, *arg_btf_id)) {
			verbose(env, "R%d is of type %s but %s is expected\n",
				regno, kernel_type_name(reg->btf, reg->btf_id),
				kernel_type_name(btf_vmlinux, *arg_btf_id));
			return -EACCES;
		}

		if (!tnum_is_const(reg->var_off) || reg->var_off.value) {
			verbose(env, "R%d is a pointer to in-kernel struct with non-zero offset\n",
				regno);
			return -EACCES;
		}
	}

	return 0;
}

static int check_func_arg(struct bpf_verifier_env *env, u32 arg,
			  struct bpf_call_arg_meta *meta,
			  const struct bpf_func_proto *fn)
{
	u32 regno = BPF_REG_1 + arg;
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	enum bpf_arg_type arg_type = fn->arg_type[arg];
	enum bpf_reg_type type = reg->type;
	int err = 0;

	if (arg_type == ARG_DONTCARE)
		return 0;

	err = check_reg_arg(env, regno, SRC_OP);
	if (err)
		return err;

	if (arg_type == ARG_ANYTHING) {
		if (is_pointer_value(env, regno)) {
			verbose(env, "R%d leaks addr into helper function\n",
				regno);
			return -EACCES;
		}
		return 0;
	}

	if (type_is_pkt_pointer(type) &&
	    !may_access_direct_pkt_data(env, meta, BPF_READ)) {
		verbose(env, "helper access to the packet is not allowed\n");
		return -EACCES;
	}

	if (arg_type == ARG_PTR_TO_MAP_VALUE ||
	    arg_type == ARG_PTR_TO_UNINIT_MAP_VALUE ||
	    arg_type == ARG_PTR_TO_MAP_VALUE_OR_NULL) {
		err = resolve_map_arg_type(env, meta, &arg_type);
		if (err)
			return err;
	}

	if (register_is_null(reg) && arg_type_may_be_null(arg_type))
		/* A NULL register has a SCALAR_VALUE type, so skip
		 * type checking.
		 */
		goto skip_type_check;

	err = check_reg_type(env, regno, arg_type, fn->arg_btf_id[arg]);
	if (err)
		return err;

	if (type == PTR_TO_CTX) {
		err = check_ctx_reg(env, reg, regno);
		if (err < 0)
			return err;
	}

skip_type_check:
	if (reg->ref_obj_id) {
		if (meta->ref_obj_id) {
			verbose(env, "verifier internal error: more than one arg with ref_obj_id R%d %u %u\n",
				regno, reg->ref_obj_id,
				meta->ref_obj_id);
			return -EFAULT;
		}
		meta->ref_obj_id = reg->ref_obj_id;
	}

	if (arg_type == ARG_CONST_MAP_PTR) {
		/* bpf_map_xxx(map_ptr) call: remember that map_ptr */
		meta->map_ptr = reg->map_ptr;
	} else if (arg_type == ARG_PTR_TO_MAP_KEY) {
		/* bpf_map_xxx(..., map_ptr, ..., key) call:
		 * check that [key, key + map->key_size) are within
		 * stack limits and initialized
		 */
		if (!meta->map_ptr) {
			/* in function declaration map_ptr must come before
			 * map_key, so that it's verified and known before
			 * we have to check map_key here. Otherwise it means
			 * that kernel subsystem misconfigured verifier
			 */
			verbose(env, "invalid map_ptr to access map->key\n");
			return -EACCES;
		}
		err = check_helper_mem_access(env, regno,
					      meta->map_ptr->key_size, false,
					      NULL);
	} else if (arg_type == ARG_PTR_TO_MAP_VALUE ||
		   (arg_type == ARG_PTR_TO_MAP_VALUE_OR_NULL &&
		    !register_is_null(reg)) ||
		   arg_type == ARG_PTR_TO_UNINIT_MAP_VALUE) {
		/* bpf_map_xxx(..., map_ptr, ..., value) call:
		 * check [value, value + map->value_size) validity
		 */
		if (!meta->map_ptr) {
			/* kernel subsystem misconfigured verifier */
			verbose(env, "invalid map_ptr to access map->value\n");
			return -EACCES;
		}
		meta->raw_mode = (arg_type == ARG_PTR_TO_UNINIT_MAP_VALUE);
		err = check_helper_mem_access(env, regno,
					      meta->map_ptr->value_size, false,
					      meta);
	} else if (arg_type == ARG_PTR_TO_PERCPU_BTF_ID) {
		if (!reg->btf_id) {
			verbose(env, "Helper has invalid btf_id in R%d\n", regno);
			return -EACCES;
		}
		meta->ret_btf = reg->btf;
		meta->ret_btf_id = reg->btf_id;
	} else if (arg_type == ARG_PTR_TO_SPIN_LOCK) {
		if (meta->func_id == BPF_FUNC_spin_lock) {
			if (process_spin_lock(env, regno, true))
				return -EACCES;
		} else if (meta->func_id == BPF_FUNC_spin_unlock) {
			if (process_spin_lock(env, regno, false))
				return -EACCES;
		} else {
			verbose(env, "verifier internal error\n");
			return -EFAULT;
		}
	} else if (arg_type_is_mem_ptr(arg_type)) {
		/* The access to this pointer is only checked when we hit the
		 * next is_mem_size argument below.
		 */
		meta->raw_mode = (arg_type == ARG_PTR_TO_UNINIT_MEM);
	} else if (arg_type_is_mem_size(arg_type)) {
		bool zero_size_allowed = (arg_type == ARG_CONST_SIZE_OR_ZERO);

		/* This is used to refine r0 return value bounds for helpers
		 * that enforce this value as an upper bound on return values.
		 * See do_refine_retval_range() for helpers that can refine
		 * the return value. C type of helper is u32 so we pull register
		 * bound from umax_value however, if negative verifier errors
		 * out. Only upper bounds can be learned because retval is an
		 * int type and negative retvals are allowed.
		 */
		meta->msize_max_value = reg->umax_value;

		/* The register is SCALAR_VALUE; the access check
		 * happens using its boundaries.
		 */
		if (!tnum_is_const(reg->var_off))
			/* For unprivileged variable accesses, disable raw
			 * mode so that the program is required to
			 * initialize all the memory that the helper could
			 * just partially fill up.
			 */
			meta = NULL;

		if (reg->smin_value < 0) {
			verbose(env, "R%d min value is negative, either use unsigned or 'var &= const'\n",
				regno);
			return -EACCES;
		}

		if (reg->umin_value == 0) {
			err = check_helper_mem_access(env, regno - 1, 0,
						      zero_size_allowed,
						      meta);
			if (err)
				return err;
		}

		if (reg->umax_value >= BPF_MAX_VAR_SIZ) {
			verbose(env, "R%d unbounded memory access, use 'var &= const' or 'if (var < const)'\n",
				regno);
			return -EACCES;
		}
		err = check_helper_mem_access(env, regno - 1,
					      reg->umax_value,
					      zero_size_allowed, meta);
		if (!err)
			err = mark_chain_precision(env, regno);
	} else if (arg_type_is_alloc_size(arg_type)) {
		if (!tnum_is_const(reg->var_off)) {
			verbose(env, "R%d is not a known constant'\n",
				regno);
			return -EACCES;
		}
		meta->mem_size = reg->var_off.value;
	} else if (arg_type_is_int_ptr(arg_type)) {
		int size = int_ptr_type_to_size(arg_type);

		err = check_helper_mem_access(env, regno, size, false, meta);
		if (err)
			return err;
		err = check_ptr_alignment(env, reg, 0, size, true);
	}

	return err;
}

static bool may_update_sockmap(struct bpf_verifier_env *env, int func_id)
{
	enum bpf_attach_type eatype = env->prog->expected_attach_type;
	enum bpf_prog_type type = resolve_prog_type(env->prog);

	if (func_id != BPF_FUNC_map_update_elem)
		return false;

	/* It's not possible to get access to a locked struct sock in these
	 * contexts, so updating is safe.
	 */
	switch (type) {
	case BPF_PROG_TYPE_TRACING:
		if (eatype == BPF_TRACE_ITER)
			return true;
		break;
	case BPF_PROG_TYPE_SOCKET_FILTER:
	case BPF_PROG_TYPE_SCHED_CLS:
	case BPF_PROG_TYPE_SCHED_ACT:
	case BPF_PROG_TYPE_XDP:
	case BPF_PROG_TYPE_SK_REUSEPORT:
	case BPF_PROG_TYPE_FLOW_DISSECTOR:
	case BPF_PROG_TYPE_SK_LOOKUP:
		return true;
	default:
		break;
	}

	verbose(env, "cannot update sockmap in this context\n");
	return false;
}

static bool allow_tail_call_in_subprogs(struct bpf_verifier_env *env)
{
	return env->prog->jit_requested && IS_ENABLED(CONFIG_X86_64);
}

static int check_map_func_compatibility(struct bpf_verifier_env *env,
					struct bpf_map *map, int func_id)
{
	if (!map)
		return 0;

	/* We need a two way check, first is from map perspective ... */
	switch (map->map_type) {
	case BPF_MAP_TYPE_PROG_ARRAY:
		if (func_id != BPF_FUNC_tail_call)
			goto error;
		break;
	case BPF_MAP_TYPE_PERF_EVENT_ARRAY:
		if (func_id != BPF_FUNC_perf_event_read &&
		    func_id != BPF_FUNC_perf_event_output &&
		    func_id != BPF_FUNC_skb_output &&
		    func_id != BPF_FUNC_perf_event_read_value &&
		    func_id != BPF_FUNC_xdp_output)
			goto error;
		break;
	case BPF_MAP_TYPE_RINGBUF:
		if (func_id != BPF_FUNC_ringbuf_output &&
		    func_id != BPF_FUNC_ringbuf_reserve &&
		    func_id != BPF_FUNC_ringbuf_submit &&
		    func_id != BPF_FUNC_ringbuf_discard &&
		    func_id != BPF_FUNC_ringbuf_query)
			goto error;
		break;
	case BPF_MAP_TYPE_STACK_TRACE:
		if (func_id != BPF_FUNC_get_stackid)
			goto error;
		break;
	case BPF_MAP_TYPE_CGROUP_ARRAY:
		if (func_id != BPF_FUNC_skb_under_cgroup &&
		    func_id != BPF_FUNC_current_task_under_cgroup)
			goto error;
		break;
	case BPF_MAP_TYPE_CGROUP_STORAGE:
	case BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE:
		if (func_id != BPF_FUNC_get_local_storage)
			goto error;
		break;
	case BPF_MAP_TYPE_DEVMAP:
	case BPF_MAP_TYPE_DEVMAP_HASH:
		if (func_id != BPF_FUNC_redirect_map &&
		    func_id != BPF_FUNC_map_lookup_elem)
			goto error;
		break;
	/* Restrict bpf side of cpumap and xskmap, open when use-cases
	 * appear.
	 */
	case BPF_MAP_TYPE_CPUMAP:
		if (func_id != BPF_FUNC_redirect_map)
			goto error;
		break;
	case BPF_MAP_TYPE_XSKMAP:
		if (func_id != BPF_FUNC_redirect_map &&
		    func_id != BPF_FUNC_map_lookup_elem)
			goto error;
		break;
	case BPF_MAP_TYPE_ARRAY_OF_MAPS:
	case BPF_MAP_TYPE_HASH_OF_MAPS:
		if (func_id != BPF_FUNC_map_lookup_elem)
			goto error;
		break;
	case BPF_MAP_TYPE_SOCKMAP:
		if (func_id != BPF_FUNC_sk_redirect_map &&
		    func_id != BPF_FUNC_sock_map_update &&
		    func_id != BPF_FUNC_map_delete_elem &&
		    func_id != BPF_FUNC_msg_redirect_map &&
		    func_id != BPF_FUNC_sk_select_reuseport &&
		    func_id != BPF_FUNC_map_lookup_elem &&
		    !may_update_sockmap(env, func_id))
			goto error;
		break;
	case BPF_MAP_TYPE_SOCKHASH:
		if (func_id != BPF_FUNC_sk_redirect_hash &&
		    func_id != BPF_FUNC_sock_hash_update &&
		    func_id != BPF_FUNC_map_delete_elem &&
		    func_id != BPF_FUNC_msg_redirect_hash &&
		    func_id != BPF_FUNC_sk_select_reuseport &&
		    func_id != BPF_FUNC_map_lookup_elem &&
		    !may_update_sockmap(env, func_id))
			goto error;
		break;
	case BPF_MAP_TYPE_REUSEPORT_SOCKARRAY:
		if (func_id != BPF_FUNC_sk_select_reuseport)
			goto error;
		break;
	case BPF_MAP_TYPE_QUEUE:
	case BPF_MAP_TYPE_STACK:
		if (func_id != BPF_FUNC_map_peek_elem &&
		    func_id != BPF_FUNC_map_pop_elem &&
		    func_id != BPF_FUNC_map_push_elem)
			goto error;
		break;
	case BPF_MAP_TYPE_SK_STORAGE:
		if (func_id != BPF_FUNC_sk_storage_get &&
		    func_id != BPF_FUNC_sk_storage_delete)
			goto error;
		break;
	case BPF_MAP_TYPE_INODE_STORAGE:
		if (func_id != BPF_FUNC_inode_storage_get &&
		    func_id != BPF_FUNC_inode_storage_delete)
			goto error;
		break;
	case BPF_MAP_TYPE_TASK_STORAGE:
		if (func_id != BPF_FUNC_task_storage_get &&
		    func_id != BPF_FUNC_task_storage_delete)
			goto error;
		break;
	default:
		break;
	}

	/* ... and second from the function itself. */
	switch (func_id) {
	case BPF_FUNC_tail_call:
		if (map->map_type != BPF_MAP_TYPE_PROG_ARRAY)
			goto error;
		if (env->subprog_cnt > 1 && !allow_tail_call_in_subprogs(env)) {
			verbose(env, "tail_calls are not allowed in non-JITed programs with bpf-to-bpf calls\n");
			return -EINVAL;
		}
		break;
	case BPF_FUNC_perf_event_read:
	case BPF_FUNC_perf_event_output:
	case BPF_FUNC_perf_event_read_value:
	case BPF_FUNC_skb_output:
	case BPF_FUNC_xdp_output:
		if (map->map_type != BPF_MAP_TYPE_PERF_EVENT_ARRAY)
			goto error;
		break;
	case BPF_FUNC_get_stackid:
		if (map->map_type != BPF_MAP_TYPE_STACK_TRACE)
			goto error;
		break;
	case BPF_FUNC_current_task_under_cgroup:
	case BPF_FUNC_skb_under_cgroup:
		if (map->map_type != BPF_MAP_TYPE_CGROUP_ARRAY)
			goto error;
		break;
	case BPF_FUNC_redirect_map:
		if (map->map_type != BPF_MAP_TYPE_DEVMAP &&
		    map->map_type != BPF_MAP_TYPE_DEVMAP_HASH &&
		    map->map_type != BPF_MAP_TYPE_CPUMAP &&
		    map->map_type != BPF_MAP_TYPE_XSKMAP)
			goto error;
		break;
	case BPF_FUNC_sk_redirect_map:
	case BPF_FUNC_msg_redirect_map:
	case BPF_FUNC_sock_map_update:
		if (map->map_type != BPF_MAP_TYPE_SOCKMAP)
			goto error;
		break;
	case BPF_FUNC_sk_redirect_hash:
	case BPF_FUNC_msg_redirect_hash:
	case BPF_FUNC_sock_hash_update:
		if (map->map_type != BPF_MAP_TYPE_SOCKHASH)
			goto error;
		break;
	case BPF_FUNC_get_local_storage:
		if (map->map_type != BPF_MAP_TYPE_CGROUP_STORAGE &&
		    map->map_type != BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE)
			goto error;
		break;
	case BPF_FUNC_sk_select_reuseport:
		if (map->map_type != BPF_MAP_TYPE_REUSEPORT_SOCKARRAY &&
		    map->map_type != BPF_MAP_TYPE_SOCKMAP &&
		    map->map_type != BPF_MAP_TYPE_SOCKHASH)
			goto error;
		break;
	case BPF_FUNC_map_peek_elem:
	case BPF_FUNC_map_pop_elem:
	case BPF_FUNC_map_push_elem:
		if (map->map_type != BPF_MAP_TYPE_QUEUE &&
		    map->map_type != BPF_MAP_TYPE_STACK)
			goto error;
		break;
	case BPF_FUNC_sk_storage_get:
	case BPF_FUNC_sk_storage_delete:
		if (map->map_type != BPF_MAP_TYPE_SK_STORAGE)
			goto error;
		break;
	case BPF_FUNC_inode_storage_get:
	case BPF_FUNC_inode_storage_delete:
		if (map->map_type != BPF_MAP_TYPE_INODE_STORAGE)
			goto error;
		break;
	case BPF_FUNC_task_storage_get:
	case BPF_FUNC_task_storage_delete:
		if (map->map_type != BPF_MAP_TYPE_TASK_STORAGE)
			goto error;
		break;
	default:
		break;
	}

	return 0;
error:
	verbose(env, "cannot pass map_type %d into func %s#%d\n",
		map->map_type, func_id_name(func_id), func_id);
	return -EINVAL;
}

static bool check_raw_mode_ok(const struct bpf_func_proto *fn)
{
	int count = 0;

	if (fn->arg1_type == ARG_PTR_TO_UNINIT_MEM)
		count++;
	if (fn->arg2_type == ARG_PTR_TO_UNINIT_MEM)
		count++;
	if (fn->arg3_type == ARG_PTR_TO_UNINIT_MEM)
		count++;
	if (fn->arg4_type == ARG_PTR_TO_UNINIT_MEM)
		count++;
	if (fn->arg5_type == ARG_PTR_TO_UNINIT_MEM)
		count++;

	/* We only support one arg being in raw mode at the moment,
	 * which is sufficient for the helper functions we have
	 * right now.
	 */
	return count <= 1;
}

static bool check_args_pair_invalid(enum bpf_arg_type arg_curr,
				    enum bpf_arg_type arg_next)
{
	return (arg_type_is_mem_ptr(arg_curr) &&
	        !arg_type_is_mem_size(arg_next)) ||
	       (!arg_type_is_mem_ptr(arg_curr) &&
		arg_type_is_mem_size(arg_next));
}

static bool check_arg_pair_ok(const struct bpf_func_proto *fn)
{
	/* bpf_xxx(..., buf, len) call will access 'len'
	 * bytes from memory 'buf'. Both arg types need
	 * to be paired, so make sure there's no buggy
	 * helper function specification.
	 */
	if (arg_type_is_mem_size(fn->arg1_type) ||
	    arg_type_is_mem_ptr(fn->arg5_type)  ||
	    check_args_pair_invalid(fn->arg1_type, fn->arg2_type) ||
	    check_args_pair_invalid(fn->arg2_type, fn->arg3_type) ||
	    check_args_pair_invalid(fn->arg3_type, fn->arg4_type) ||
	    check_args_pair_invalid(fn->arg4_type, fn->arg5_type))
		return false;

	return true;
}

static bool check_refcount_ok(const struct bpf_func_proto *fn, int func_id)
{
	int count = 0;

	if (arg_type_may_be_refcounted(fn->arg1_type))
		count++;
	if (arg_type_may_be_refcounted(fn->arg2_type))
		count++;
	if (arg_type_may_be_refcounted(fn->arg3_type))
		count++;
	if (arg_type_may_be_refcounted(fn->arg4_type))
		count++;
	if (arg_type_may_be_refcounted(fn->arg5_type))
		count++;

	/* A reference acquiring function cannot acquire
	 * another refcounted ptr.
	 */
	if (may_be_acquire_function(func_id) && count)
		return false;

	/* We only support one arg being unreferenced at the moment,
	 * which is sufficient for the helper functions we have right now.
	 */
	return count <= 1;
}

static bool check_btf_id_ok(const struct bpf_func_proto *fn)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fn->arg_type); i++) {
		if (fn->arg_type[i] == ARG_PTR_TO_BTF_ID && !fn->arg_btf_id[i])
			return false;

		if (fn->arg_type[i] != ARG_PTR_TO_BTF_ID && fn->arg_btf_id[i])
			return false;
	}

	return true;
}

static int check_func_proto(const struct bpf_func_proto *fn, int func_id)
{
	return check_raw_mode_ok(fn) &&
	       check_arg_pair_ok(fn) &&
	       check_btf_id_ok(fn) &&
	       check_refcount_ok(fn, func_id) ? 0 : -EINVAL;
}

/* Packet data might have moved, any old PTR_TO_PACKET[_META,_END]
 * are now invalid, so turn them into unknown SCALAR_VALUE.
 */
static void __clear_all_pkt_pointers(struct bpf_verifier_env *env,
				     struct bpf_func_state *state)
{
	struct bpf_reg_state *regs = state->regs, *reg;
	int i;

	for (i = 0; i < MAX_BPF_REG; i++)
		if (reg_is_pkt_pointer_any(&regs[i]))
			mark_reg_unknown(env, regs, i);

	bpf_for_each_spilled_reg(i, state, reg) {
		if (!reg)
			continue;
		if (reg_is_pkt_pointer_any(reg))
			__mark_reg_unknown(env, reg);
	}
}

static void clear_all_pkt_pointers(struct bpf_verifier_env *env)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	int i;

	for (i = 0; i <= vstate->curframe; i++)
		__clear_all_pkt_pointers(env, vstate->frame[i]);
}

enum {
	AT_PKT_END = -1,
	BEYOND_PKT_END = -2,
};

static void mark_pkt_end(struct bpf_verifier_state *vstate, int regn, bool range_open)
{
	struct bpf_func_state *state = vstate->frame[vstate->curframe];
	struct bpf_reg_state *reg = &state->regs[regn];

	if (reg->type != PTR_TO_PACKET)
		/* PTR_TO_PACKET_META is not supported yet */
		return;

	/* The 'reg' is pkt > pkt_end or pkt >= pkt_end.
	 * How far beyond pkt_end it goes is unknown.
	 * if (!range_open) it's the case of pkt >= pkt_end
	 * if (range_open) it's the case of pkt > pkt_end
	 * hence this pointer is at least 1 byte bigger than pkt_end
	 */
	if (range_open)
		reg->range = BEYOND_PKT_END;
	else
		reg->range = AT_PKT_END;
}

static void release_reg_references(struct bpf_verifier_env *env,
				   struct bpf_func_state *state,
				   int ref_obj_id)
{
	struct bpf_reg_state *regs = state->regs, *reg;
	int i;

	for (i = 0; i < MAX_BPF_REG; i++)
		if (regs[i].ref_obj_id == ref_obj_id)
			mark_reg_unknown(env, regs, i);

	bpf_for_each_spilled_reg(i, state, reg) {
		if (!reg)
			continue;
		if (reg->ref_obj_id == ref_obj_id)
			__mark_reg_unknown(env, reg);
	}
}

/* The pointer with the specified id has released its reference to kernel
 * resources. Identify all copies of the same pointer and clear the reference.
 */
static int release_reference(struct bpf_verifier_env *env,
			     int ref_obj_id)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	int err;
	int i;

	err = release_reference_state(cur_func(env), ref_obj_id);
	if (err)
		return err;

	for (i = 0; i <= vstate->curframe; i++)
		release_reg_references(env, vstate->frame[i], ref_obj_id);

	return 0;
}

static void clear_caller_saved_regs(struct bpf_verifier_env *env,
				    struct bpf_reg_state *regs)
{
	int i;

	/* after the call registers r0 - r5 were scratched */
	for (i = 0; i < CALLER_SAVED_REGS; i++) {
		mark_reg_not_init(env, regs, caller_saved[i]);
		check_reg_arg(env, caller_saved[i], DST_OP_NO_MARK);
	}
}

static int check_func_call(struct bpf_verifier_env *env, struct bpf_insn *insn,
			   int *insn_idx)
{
	struct bpf_verifier_state *state = env->cur_state;
	struct bpf_func_info_aux *func_info_aux;
	struct bpf_func_state *caller, *callee;
	int i, err, subprog, target_insn;
	bool is_global = false;

	if (state->curframe + 1 >= MAX_CALL_FRAMES) {
		verbose(env, "the call stack of %d frames is too deep\n",
			state->curframe + 2);
		return -E2BIG;
	}

	target_insn = *insn_idx + insn->imm;
	subprog = find_subprog(env, target_insn + 1);
	if (subprog < 0) {
		verbose(env, "verifier bug. No program starts at insn %d\n",
			target_insn + 1);
		return -EFAULT;
	}

	caller = state->frame[state->curframe];
	if (state->frame[state->curframe + 1]) {
		verbose(env, "verifier bug. Frame %d already allocated\n",
			state->curframe + 1);
		return -EFAULT;
	}

	func_info_aux = env->prog->aux->func_info_aux;
	if (func_info_aux)
		is_global = func_info_aux[subprog].linkage == BTF_FUNC_GLOBAL;
	err = btf_check_func_arg_match(env, subprog, caller->regs);
	if (err == -EFAULT)
		return err;
	if (is_global) {
		if (err) {
			verbose(env, "Caller passes invalid args into func#%d\n",
				subprog);
			return err;
		} else {
			if (env->log.level & BPF_LOG_LEVEL)
				verbose(env,
					"Func#%d is global and valid. Skipping.\n",
					subprog);
			clear_caller_saved_regs(env, caller->regs);

			/* All global functions return a 64-bit SCALAR_VALUE */
			mark_reg_unknown(env, caller->regs, BPF_REG_0);
			caller->regs[BPF_REG_0].subreg_def = DEF_NOT_SUBREG;

			/* continue with next insn after call */
			return 0;
		}
	}

	callee = kzalloc(sizeof(*callee), GFP_KERNEL);
	if (!callee)
		return -ENOMEM;
	state->frame[state->curframe + 1] = callee;

	/* callee cannot access r0, r6 - r9 for reading and has to write
	 * into its own stack before reading from it.
	 * callee can read/write into caller's stack
	 */
	init_func_state(env, callee,
			/* remember the callsite, it will be used by bpf_exit */
			*insn_idx /* callsite */,
			state->curframe + 1 /* frameno within this callchain */,
			subprog /* subprog number within this prog */);

	/* Transfer references to the callee */
	err = transfer_reference_state(callee, caller);
	if (err)
		return err;

	/* copy r1 - r5 args that callee can access.  The copy includes parent
	 * pointers, which connects us up to the liveness chain
	 */
	for (i = BPF_REG_1; i <= BPF_REG_5; i++)
		callee->regs[i] = caller->regs[i];

	clear_caller_saved_regs(env, caller->regs);

	/* only increment it after check_reg_arg() finished */
	state->curframe++;

	/* and go analyze first insn of the callee */
	*insn_idx = target_insn;

	if (env->log.level & BPF_LOG_LEVEL) {
		verbose(env, "caller:\n");
		print_verifier_state(env, caller);
		verbose(env, "callee:\n");
		print_verifier_state(env, callee);
	}
	return 0;
}

static int prepare_func_exit(struct bpf_verifier_env *env, int *insn_idx)
{
	struct bpf_verifier_state *state = env->cur_state;
	struct bpf_func_state *caller, *callee;
	struct bpf_reg_state *r0;
	int err;

	callee = state->frame[state->curframe];
	r0 = &callee->regs[BPF_REG_0];
	if (r0->type == PTR_TO_STACK) {
		/* technically it's ok to return caller's stack pointer
		 * (or caller's caller's pointer) back to the caller,
		 * since these pointers are valid. Only current stack
		 * pointer will be invalid as soon as function exits,
		 * but let's be conservative
		 */
		verbose(env, "cannot return stack pointer to the caller\n");
		return -EINVAL;
	}

	state->curframe--;
	caller = state->frame[state->curframe];
	/* return to the caller whatever r0 had in the callee */
	caller->regs[BPF_REG_0] = *r0;

	/* Transfer references to the caller */
	err = transfer_reference_state(caller, callee);
	if (err)
		return err;

	*insn_idx = callee->callsite + 1;
	if (env->log.level & BPF_LOG_LEVEL) {
		verbose(env, "returning from callee:\n");
		print_verifier_state(env, callee);
		verbose(env, "to caller at %d:\n", *insn_idx);
		print_verifier_state(env, caller);
	}
	/* clear everything in the callee */
	free_func_state(callee);
	state->frame[state->curframe + 1] = NULL;
	return 0;
}

static void do_refine_retval_range(struct bpf_reg_state *regs, int ret_type,
				   int func_id,
				   struct bpf_call_arg_meta *meta)
{
	struct bpf_reg_state *ret_reg = &regs[BPF_REG_0];

	if (ret_type != RET_INTEGER ||
	    (func_id != BPF_FUNC_get_stack &&
	     func_id != BPF_FUNC_probe_read_str &&
	     func_id != BPF_FUNC_probe_read_kernel_str &&
	     func_id != BPF_FUNC_probe_read_user_str))
		return;

	ret_reg->smax_value = meta->msize_max_value;
	ret_reg->s32_max_value = meta->msize_max_value;
	ret_reg->smin_value = -MAX_ERRNO;
	ret_reg->s32_min_value = -MAX_ERRNO;
	__reg_deduce_bounds(ret_reg);
	__reg_bound_offset(ret_reg);
	__update_reg_bounds(ret_reg);
}

static int
record_func_map(struct bpf_verifier_env *env, struct bpf_call_arg_meta *meta,
		int func_id, int insn_idx)
{
	struct bpf_insn_aux_data *aux = &env->insn_aux_data[insn_idx];
	struct bpf_map *map = meta->map_ptr;

	if (func_id != BPF_FUNC_tail_call &&
	    func_id != BPF_FUNC_map_lookup_elem &&
	    func_id != BPF_FUNC_map_update_elem &&
	    func_id != BPF_FUNC_map_delete_elem &&
	    func_id != BPF_FUNC_map_push_elem &&
	    func_id != BPF_FUNC_map_pop_elem &&
	    func_id != BPF_FUNC_map_peek_elem)
		return 0;

	if (map == NULL) {
		verbose(env, "kernel subsystem misconfigured verifier\n");
		return -EINVAL;
	}

	/* In case of read-only, some additional restrictions
	 * need to be applied in order to prevent altering the
	 * state of the map from program side.
	 */
	if ((map->map_flags & BPF_F_RDONLY_PROG) &&
	    (func_id == BPF_FUNC_map_delete_elem ||
	     func_id == BPF_FUNC_map_update_elem ||
	     func_id == BPF_FUNC_map_push_elem ||
	     func_id == BPF_FUNC_map_pop_elem)) {
		verbose(env, "write into map forbidden\n");
		return -EACCES;
	}

	if (!BPF_MAP_PTR(aux->map_ptr_state))
		bpf_map_ptr_store(aux, meta->map_ptr,
				  !meta->map_ptr->bypass_spec_v1);
	else if (BPF_MAP_PTR(aux->map_ptr_state) != meta->map_ptr)
		bpf_map_ptr_store(aux, BPF_MAP_PTR_POISON,
				  !meta->map_ptr->bypass_spec_v1);
	return 0;
}

static int
record_func_key(struct bpf_verifier_env *env, struct bpf_call_arg_meta *meta,
		int func_id, int insn_idx)
{
	struct bpf_insn_aux_data *aux = &env->insn_aux_data[insn_idx];
	struct bpf_reg_state *regs = cur_regs(env), *reg;
	struct bpf_map *map = meta->map_ptr;
	struct tnum range;
	u64 val;
	int err;

	if (func_id != BPF_FUNC_tail_call)
		return 0;
	if (!map || map->map_type != BPF_MAP_TYPE_PROG_ARRAY) {
		verbose(env, "kernel subsystem misconfigured verifier\n");
		return -EINVAL;
	}

	range = tnum_range(0, map->max_entries - 1);
	reg = &regs[BPF_REG_3];

	if (!register_is_const(reg) || !tnum_in(range, reg->var_off)) {
		bpf_map_key_store(aux, BPF_MAP_KEY_POISON);
		return 0;
	}

	err = mark_chain_precision(env, BPF_REG_3);
	if (err)
		return err;

	val = reg->var_off.value;
	if (bpf_map_key_unseen(aux))
		bpf_map_key_store(aux, val);
	else if (!bpf_map_key_poisoned(aux) &&
		  bpf_map_key_immediate(aux) != val)
		bpf_map_key_store(aux, BPF_MAP_KEY_POISON);
	return 0;
}

static int check_reference_leak(struct bpf_verifier_env *env)
{
	struct bpf_func_state *state = cur_func(env);
	int i;

	for (i = 0; i < state->acquired_refs; i++) {
		verbose(env, "Unreleased reference id=%d alloc_insn=%d\n",
			state->refs[i].id, state->refs[i].insn_idx);
	}
	return state->acquired_refs ? -EINVAL : 0;
}

static int check_helper_call(struct bpf_verifier_env *env, int func_id, int insn_idx)
{
	const struct bpf_func_proto *fn = NULL;
	struct bpf_reg_state *regs;
	struct bpf_call_arg_meta meta;
	bool changes_data;
	int i, err;

	/* find function prototype */
	if (func_id < 0 || func_id >= __BPF_FUNC_MAX_ID) {
		verbose(env, "invalid func %s#%d\n", func_id_name(func_id),
			func_id);
		return -EINVAL;
	}

	if (env->ops->get_func_proto)
		fn = env->ops->get_func_proto(func_id, env->prog);
	if (!fn) {
		verbose(env, "unknown func %s#%d\n", func_id_name(func_id),
			func_id);
		return -EINVAL;
	}

	/* eBPF programs must be GPL compatible to use GPL-ed functions */
	if (!env->prog->gpl_compatible && fn->gpl_only) {
		verbose(env, "cannot call GPL-restricted function from non-GPL compatible program\n");
		return -EINVAL;
	}

	if (fn->allowed && !fn->allowed(env->prog)) {
		verbose(env, "helper call is not allowed in probe\n");
		return -EINVAL;
	}

	/* With LD_ABS/IND some JITs save/restore skb from r1. */
	changes_data = bpf_helper_changes_pkt_data(fn->func);
	if (changes_data && fn->arg1_type != ARG_PTR_TO_CTX) {
		verbose(env, "kernel subsystem misconfigured func %s#%d: r1 != ctx\n",
			func_id_name(func_id), func_id);
		return -EINVAL;
	}

	memset(&meta, 0, sizeof(meta));
	meta.pkt_access = fn->pkt_access;

	err = check_func_proto(fn, func_id);
	if (err) {
		verbose(env, "kernel subsystem misconfigured func %s#%d\n",
			func_id_name(func_id), func_id);
		return err;
	}

	meta.func_id = func_id;
	/* check args */
	for (i = 0; i < 5; i++) {
		err = check_func_arg(env, i, &meta, fn);
		if (err)
			return err;
	}

	err = record_func_map(env, &meta, func_id, insn_idx);
	if (err)
		return err;

	err = record_func_key(env, &meta, func_id, insn_idx);
	if (err)
		return err;

	/* Mark slots with STACK_MISC in case of raw mode, stack offset
	 * is inferred from register state.
	 */
	for (i = 0; i < meta.access_size; i++) {
		err = check_mem_access(env, insn_idx, meta.regno, i, BPF_B,
				       BPF_WRITE, -1, false);
		if (err)
			return err;
	}

	if (func_id == BPF_FUNC_tail_call) {
		err = check_reference_leak(env);
		if (err) {
			verbose(env, "tail_call would lead to reference leak\n");
			return err;
		}
	} else if (is_release_function(func_id)) {
		err = release_reference(env, meta.ref_obj_id);
		if (err) {
			verbose(env, "func %s#%d reference has not been acquired before\n",
				func_id_name(func_id), func_id);
			return err;
		}
	}

	regs = cur_regs(env);

	/* check that flags argument in get_local_storage(map, flags) is 0,
	 * this is required because get_local_storage() can't return an error.
	 */
	if (func_id == BPF_FUNC_get_local_storage &&
	    !register_is_null(&regs[BPF_REG_2])) {
		verbose(env, "get_local_storage() doesn't support non-zero flags\n");
		return -EINVAL;
	}

	/* reset caller saved regs */
	for (i = 0; i < CALLER_SAVED_REGS; i++) {
		mark_reg_not_init(env, regs, caller_saved[i]);
		check_reg_arg(env, caller_saved[i], DST_OP_NO_MARK);
	}

	/* helper call returns 64-bit value. */
	regs[BPF_REG_0].subreg_def = DEF_NOT_SUBREG;

	/* update return register (already marked as written above) */
	if (fn->ret_type == RET_INTEGER) {
		/* sets type to SCALAR_VALUE */
		mark_reg_unknown(env, regs, BPF_REG_0);
	} else if (fn->ret_type == RET_VOID) {
		regs[BPF_REG_0].type = NOT_INIT;
	} else if (fn->ret_type == RET_PTR_TO_MAP_VALUE_OR_NULL ||
		   fn->ret_type == RET_PTR_TO_MAP_VALUE) {
		/* There is no offset yet applied, variable or fixed */
		mark_reg_known_zero(env, regs, BPF_REG_0);
		/* remember map_ptr, so that check_map_access()
		 * can check 'value_size' boundary of memory access
		 * to map element returned from bpf_map_lookup_elem()
		 */
		if (meta.map_ptr == NULL) {
			verbose(env,
				"kernel subsystem misconfigured verifier\n");
			return -EINVAL;
		}
		regs[BPF_REG_0].map_ptr = meta.map_ptr;
		if (fn->ret_type == RET_PTR_TO_MAP_VALUE) {
			regs[BPF_REG_0].type = PTR_TO_MAP_VALUE;
			if (map_value_has_spin_lock(meta.map_ptr))
				regs[BPF_REG_0].id = ++env->id_gen;
		} else {
			regs[BPF_REG_0].type = PTR_TO_MAP_VALUE_OR_NULL;
		}
	} else if (fn->ret_type == RET_PTR_TO_SOCKET_OR_NULL) {
		mark_reg_known_zero(env, regs, BPF_REG_0);
		regs[BPF_REG_0].type = PTR_TO_SOCKET_OR_NULL;
	} else if (fn->ret_type == RET_PTR_TO_SOCK_COMMON_OR_NULL) {
		mark_reg_known_zero(env, regs, BPF_REG_0);
		regs[BPF_REG_0].type = PTR_TO_SOCK_COMMON_OR_NULL;
	} else if (fn->ret_type == RET_PTR_TO_TCP_SOCK_OR_NULL) {
		mark_reg_known_zero(env, regs, BPF_REG_0);
		regs[BPF_REG_0].type = PTR_TO_TCP_SOCK_OR_NULL;
	} else if (fn->ret_type == RET_PTR_TO_ALLOC_MEM_OR_NULL) {
		mark_reg_known_zero(env, regs, BPF_REG_0);
		regs[BPF_REG_0].type = PTR_TO_MEM_OR_NULL;
		regs[BPF_REG_0].mem_size = meta.mem_size;
	} else if (fn->ret_type == RET_PTR_TO_MEM_OR_BTF_ID_OR_NULL ||
		   fn->ret_type == RET_PTR_TO_MEM_OR_BTF_ID) {
		const struct btf_type *t;

		mark_reg_known_zero(env, regs, BPF_REG_0);
		t = btf_type_skip_modifiers(meta.ret_btf, meta.ret_btf_id, NULL);
		if (!btf_type_is_struct(t)) {
			u32 tsize;
			const struct btf_type *ret;
			const char *tname;

			/* resolve the type size of ksym. */
			ret = btf_resolve_size(meta.ret_btf, t, &tsize);
			if (IS_ERR(ret)) {
				tname = btf_name_by_offset(meta.ret_btf, t->name_off);
				verbose(env, "unable to resolve the size of type '%s': %ld\n",
					tname, PTR_ERR(ret));
				return -EINVAL;
			}
			regs[BPF_REG_0].type =
				fn->ret_type == RET_PTR_TO_MEM_OR_BTF_ID ?
				PTR_TO_MEM : PTR_TO_MEM_OR_NULL;
			regs[BPF_REG_0].mem_size = tsize;
		} else {
			regs[BPF_REG_0].type =
				fn->ret_type == RET_PTR_TO_MEM_OR_BTF_ID ?
				PTR_TO_BTF_ID : PTR_TO_BTF_ID_OR_NULL;
			regs[BPF_REG_0].btf = meta.ret_btf;
			regs[BPF_REG_0].btf_id = meta.ret_btf_id;
		}
	} else if (fn->ret_type == RET_PTR_TO_BTF_ID_OR_NULL ||
		   fn->ret_type == RET_PTR_TO_BTF_ID) {
		int ret_btf_id;

		mark_reg_known_zero(env, regs, BPF_REG_0);
		regs[BPF_REG_0].type = fn->ret_type == RET_PTR_TO_BTF_ID ?
						     PTR_TO_BTF_ID :
						     PTR_TO_BTF_ID_OR_NULL;
		ret_btf_id = *fn->ret_btf_id;
		if (ret_btf_id == 0) {
			verbose(env, "invalid return type %d of func %s#%d\n",
				fn->ret_type, func_id_name(func_id), func_id);
			return -EINVAL;
		}
		/* current BPF helper definitions are only coming from
		 * built-in code with type IDs from  vmlinux BTF
		 */
		regs[BPF_REG_0].btf = btf_vmlinux;
		regs[BPF_REG_0].btf_id = ret_btf_id;
	} else {
		verbose(env, "unknown return type %d of func %s#%d\n",
			fn->ret_type, func_id_name(func_id), func_id);
		return -EINVAL;
	}

	if (reg_type_may_be_null(regs[BPF_REG_0].type))
		regs[BPF_REG_0].id = ++env->id_gen;

	if (is_ptr_cast_function(func_id)) {
		/* For release_reference() */
		regs[BPF_REG_0].ref_obj_id = meta.ref_obj_id;
	} else if (is_acquire_function(func_id, meta.map_ptr)) {
		int id = acquire_reference_state(env, insn_idx);

		if (id < 0)
			return id;
		/* For mark_ptr_or_null_reg() */
		regs[BPF_REG_0].id = id;
		/* For release_reference() */
		regs[BPF_REG_0].ref_obj_id = id;
	}

	do_refine_retval_range(regs, fn->ret_type, func_id, &meta);

	err = check_map_func_compatibility(env, meta.map_ptr, func_id);
	if (err)
		return err;

	if ((func_id == BPF_FUNC_get_stack ||
	     func_id == BPF_FUNC_get_task_stack) &&
	    !env->prog->has_callchain_buf) {
		const char *err_str;

#ifdef CONFIG_PERF_EVENTS
		err = get_callchain_buffers(sysctl_perf_event_max_stack);
		err_str = "cannot get callchain buffer for func %s#%d\n";
#else
		err = -ENOTSUPP;
		err_str = "func %s#%d not supported without CONFIG_PERF_EVENTS\n";
#endif
		if (err) {
			verbose(env, err_str, func_id_name(func_id), func_id);
			return err;
		}

		env->prog->has_callchain_buf = true;
	}

	if (func_id == BPF_FUNC_get_stackid || func_id == BPF_FUNC_get_stack)
		env->prog->call_get_stack = true;

	if (changes_data)
		clear_all_pkt_pointers(env);
	return 0;
}

static bool signed_add_overflows(s64 a, s64 b)
{
	/* Do the add in u64, where overflow is well-defined */
	s64 res = (s64)((u64)a + (u64)b);

	if (b < 0)
		return res > a;
	return res < a;
}

static bool signed_add32_overflows(s32 a, s32 b)
{
	/* Do the add in u32, where overflow is well-defined */
	s32 res = (s32)((u32)a + (u32)b);

	if (b < 0)
		return res > a;
	return res < a;
}

static bool signed_sub_overflows(s64 a, s64 b)
{
	/* Do the sub in u64, where overflow is well-defined */
	s64 res = (s64)((u64)a - (u64)b);

	if (b < 0)
		return res < a;
	return res > a;
}

static bool signed_sub32_overflows(s32 a, s32 b)
{
	/* Do the sub in u32, where overflow is well-defined */
	s32 res = (s32)((u32)a - (u32)b);

	if (b < 0)
		return res < a;
	return res > a;
}

static bool check_reg_sane_offset(struct bpf_verifier_env *env,
				  const struct bpf_reg_state *reg,
				  enum bpf_reg_type type)
{
	bool known = tnum_is_const(reg->var_off);
	s64 val = reg->var_off.value;
	s64 smin = reg->smin_value;

	if (known && (val >= BPF_MAX_VAR_OFF || val <= -BPF_MAX_VAR_OFF)) {
		verbose(env, "math between %s pointer and %lld is not allowed\n",
			reg_type_str[type], val);
		return false;
	}

	if (reg->off >= BPF_MAX_VAR_OFF || reg->off <= -BPF_MAX_VAR_OFF) {
		verbose(env, "%s pointer offset %d is not allowed\n",
			reg_type_str[type], reg->off);
		return false;
	}

	if (smin == S64_MIN) {
		verbose(env, "math between %s pointer and register with unbounded min value is not allowed\n",
			reg_type_str[type]);
		return false;
	}

	if (smin >= BPF_MAX_VAR_OFF || smin <= -BPF_MAX_VAR_OFF) {
		verbose(env, "value %lld makes %s pointer be out of bounds\n",
			smin, reg_type_str[type]);
		return false;
	}

	return true;
}

static struct bpf_insn_aux_data *cur_aux(struct bpf_verifier_env *env)
{
	return &env->insn_aux_data[env->insn_idx];
}

enum {
	REASON_BOUNDS	= -1,
	REASON_TYPE	= -2,
	REASON_PATHS	= -3,
	REASON_LIMIT	= -4,
	REASON_STACK	= -5,
};

static int retrieve_ptr_limit(const struct bpf_reg_state *ptr_reg,
			      const struct bpf_reg_state *off_reg,
			      u32 *alu_limit, u8 opcode)
{
	bool off_is_neg = off_reg->smin_value < 0;
	bool mask_to_left = (opcode == BPF_ADD &&  off_is_neg) ||
			    (opcode == BPF_SUB && !off_is_neg);
	u32 max = 0, ptr_limit = 0;

	if (!tnum_is_const(off_reg->var_off) &&
	    (off_reg->smin_value < 0) != (off_reg->smax_value < 0))
		return REASON_BOUNDS;

	switch (ptr_reg->type) {
	case PTR_TO_STACK:
		/* Offset 0 is out-of-bounds, but acceptable start for the
		 * left direction, see BPF_REG_FP. Also, unknown scalar
		 * offset where we would need to deal with min/max bounds is
		 * currently prohibited for unprivileged.
		 */
		max = MAX_BPF_STACK + mask_to_left;
		ptr_limit = -(ptr_reg->var_off.value + ptr_reg->off);
		break;
	case PTR_TO_MAP_VALUE:
		max = ptr_reg->map_ptr->value_size;
		ptr_limit = (mask_to_left ?
			     ptr_reg->smin_value :
			     ptr_reg->umax_value) + ptr_reg->off;
		break;
	default:
		return REASON_TYPE;
	}

	if (ptr_limit >= max)
		return REASON_LIMIT;
	*alu_limit = ptr_limit;
	return 0;
}

static bool can_skip_alu_sanitation(const struct bpf_verifier_env *env,
				    const struct bpf_insn *insn)
{
	return env->bypass_spec_v1 || BPF_SRC(insn->code) == BPF_K;
}

static int update_alu_sanitation_state(struct bpf_insn_aux_data *aux,
				       u32 alu_state, u32 alu_limit)
{
	/* If we arrived here from different branches with different
	 * state or limits to sanitize, then this won't work.
	 */
	if (aux->alu_state &&
	    (aux->alu_state != alu_state ||
	     aux->alu_limit != alu_limit))
		return REASON_PATHS;

	/* Corresponding fixup done in fixup_bpf_calls(). */
	aux->alu_state = alu_state;
	aux->alu_limit = alu_limit;
	return 0;
}

static int sanitize_val_alu(struct bpf_verifier_env *env,
			    struct bpf_insn *insn)
{
	struct bpf_insn_aux_data *aux = cur_aux(env);

	if (can_skip_alu_sanitation(env, insn))
		return 0;

	return update_alu_sanitation_state(aux, BPF_ALU_NON_POINTER, 0);
}

static bool sanitize_needed(u8 opcode)
{
	return opcode == BPF_ADD || opcode == BPF_SUB;
}

static int sanitize_ptr_alu(struct bpf_verifier_env *env,
			    struct bpf_insn *insn,
			    const struct bpf_reg_state *ptr_reg,
			    const struct bpf_reg_state *off_reg,
			    struct bpf_reg_state *dst_reg,
			    struct bpf_insn_aux_data *tmp_aux,
			    const bool commit_window)
{
	struct bpf_insn_aux_data *aux = commit_window ? cur_aux(env) : tmp_aux;
	struct bpf_verifier_state *vstate = env->cur_state;
	bool off_is_neg = off_reg->smin_value < 0;
	bool ptr_is_dst_reg = ptr_reg == dst_reg;
	u8 opcode = BPF_OP(insn->code);
	u32 alu_state, alu_limit;
	struct bpf_reg_state tmp;
	bool ret;
	int err;

	if (can_skip_alu_sanitation(env, insn))
		return 0;

	/* We already marked aux for masking from non-speculative
	 * paths, thus we got here in the first place. We only care
	 * to explore bad access from here.
	 */
	if (vstate->speculative)
		goto do_sim;

	err = retrieve_ptr_limit(ptr_reg, off_reg, &alu_limit, opcode);
	if (err < 0)
		return err;

	if (commit_window) {
		/* In commit phase we narrow the masking window based on
		 * the observed pointer move after the simulated operation.
		 */
		alu_state = tmp_aux->alu_state;
		alu_limit = abs(tmp_aux->alu_limit - alu_limit);
	} else {
		alu_state  = off_is_neg ? BPF_ALU_NEG_VALUE : 0;
		alu_state |= ptr_is_dst_reg ?
			     BPF_ALU_SANITIZE_SRC : BPF_ALU_SANITIZE_DST;
	}

	err = update_alu_sanitation_state(aux, alu_state, alu_limit);
	if (err < 0)
		return err;
do_sim:
	/* If we're in commit phase, we're done here given we already
	 * pushed the truncated dst_reg into the speculative verification
	 * stack.
	 */
	if (commit_window)
		return 0;

	/* Simulate and find potential out-of-bounds access under
	 * speculative execution from truncation as a result of
	 * masking when off was not within expected range. If off
	 * sits in dst, then we temporarily need to move ptr there
	 * to simulate dst (== 0) +/-= ptr. Needed, for example,
	 * for cases where we use K-based arithmetic in one direction
	 * and truncated reg-based in the other in order to explore
	 * bad access.
	 */
	if (!ptr_is_dst_reg) {
		tmp = *dst_reg;
		*dst_reg = *ptr_reg;
	}
	ret = push_stack(env, env->insn_idx + 1, env->insn_idx, true);
	if (!ptr_is_dst_reg && ret)
		*dst_reg = tmp;
	return !ret ? REASON_STACK : 0;
}

static int sanitize_err(struct bpf_verifier_env *env,
			const struct bpf_insn *insn, int reason,
			const struct bpf_reg_state *off_reg,
			const struct bpf_reg_state *dst_reg)
{
	static const char *err = "pointer arithmetic with it prohibited for !root";
	const char *op = BPF_OP(insn->code) == BPF_ADD ? "add" : "sub";
	u32 dst = insn->dst_reg, src = insn->src_reg;

	switch (reason) {
	case REASON_BOUNDS:
		verbose(env, "R%d has unknown scalar with mixed signed bounds, %s\n",
			off_reg == dst_reg ? dst : src, err);
		break;
	case REASON_TYPE:
		verbose(env, "R%d has pointer with unsupported alu operation, %s\n",
			off_reg == dst_reg ? src : dst, err);
		break;
	case REASON_PATHS:
		verbose(env, "R%d tried to %s from different maps, paths or scalars, %s\n",
			dst, op, err);
		break;
	case REASON_LIMIT:
		verbose(env, "R%d tried to %s beyond pointer bounds, %s\n",
			dst, op, err);
		break;
	case REASON_STACK:
		verbose(env, "R%d could not be pushed for speculative verification, %s\n",
			dst, err);
		break;
	default:
		verbose(env, "verifier internal error: unknown reason (%d)\n",
			reason);
		break;
	}

	return -EACCES;
}

/* check that stack access falls within stack limits and that 'reg' doesn't
 * have a variable offset.
 *
 * Variable offset is prohibited for unprivileged mode for simplicity since it
 * requires corresponding support in Spectre masking for stack ALU.  See also
 * retrieve_ptr_limit().
 *
 *
 * 'off' includes 'reg->off'.
 */
static int check_stack_access_for_ptr_arithmetic(
				struct bpf_verifier_env *env,
				int regno,
				const struct bpf_reg_state *reg,
				int off)
{
	if (!tnum_is_const(reg->var_off)) {
		char tn_buf[48];

		tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
		verbose(env, "R%d variable stack access prohibited for !root, var_off=%s off=%d\n",
			regno, tn_buf, off);
		return -EACCES;
	}

	if (off >= 0 || off < -MAX_BPF_STACK) {
		verbose(env, "R%d stack pointer arithmetic goes out of range, "
			"prohibited for !root; off=%d\n", regno, off);
		return -EACCES;
	}

	return 0;
}

static int sanitize_check_bounds(struct bpf_verifier_env *env,
				 const struct bpf_insn *insn,
				 const struct bpf_reg_state *dst_reg)
{
	u32 dst = insn->dst_reg;

	/* For unprivileged we require that resulting offset must be in bounds
	 * in order to be able to sanitize access later on.
	 */
	if (env->bypass_spec_v1)
		return 0;

	switch (dst_reg->type) {
	case PTR_TO_STACK:
		if (check_stack_access_for_ptr_arithmetic(env, dst, dst_reg,
					dst_reg->off + dst_reg->var_off.value))
			return -EACCES;
		break;
	case PTR_TO_MAP_VALUE:
		if (check_map_access(env, dst, dst_reg->off, 1, false)) {
			verbose(env, "R%d pointer arithmetic of map value goes out of range, "
				"prohibited for !root\n", dst);
			return -EACCES;
		}
		break;
	default:
		break;
	}

	return 0;
}

/* Handles arithmetic on a pointer and a scalar: computes new min/max and var_off.
 * Caller should also handle BPF_MOV case separately.
 * If we return -EACCES, caller may want to try again treating pointer as a
 * scalar.  So we only emit a diagnostic if !env->allow_ptr_leaks.
 */
static int adjust_ptr_min_max_vals(struct bpf_verifier_env *env,
				   struct bpf_insn *insn,
				   const struct bpf_reg_state *ptr_reg,
				   const struct bpf_reg_state *off_reg)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	struct bpf_func_state *state = vstate->frame[vstate->curframe];
	struct bpf_reg_state *regs = state->regs, *dst_reg;
	bool known = tnum_is_const(off_reg->var_off);
	s64 smin_val = off_reg->smin_value, smax_val = off_reg->smax_value,
	    smin_ptr = ptr_reg->smin_value, smax_ptr = ptr_reg->smax_value;
	u64 umin_val = off_reg->umin_value, umax_val = off_reg->umax_value,
	    umin_ptr = ptr_reg->umin_value, umax_ptr = ptr_reg->umax_value;
	struct bpf_insn_aux_data tmp_aux = {};
	u8 opcode = BPF_OP(insn->code);
	u32 dst = insn->dst_reg;
	int ret;

	dst_reg = &regs[dst];

	if ((known && (smin_val != smax_val || umin_val != umax_val)) ||
	    smin_val > smax_val || umin_val > umax_val) {
		/* Taint dst register if offset had invalid bounds derived from
		 * e.g. dead branches.
		 */
		__mark_reg_unknown(env, dst_reg);
		return 0;
	}

	if (BPF_CLASS(insn->code) != BPF_ALU64) {
		/* 32-bit ALU ops on pointers produce (meaningless) scalars */
		if (opcode == BPF_SUB && env->allow_ptr_leaks) {
			__mark_reg_unknown(env, dst_reg);
			return 0;
		}

		verbose(env,
			"R%d 32-bit pointer arithmetic prohibited\n",
			dst);
		return -EACCES;
	}

	switch (ptr_reg->type) {
	case PTR_TO_MAP_VALUE_OR_NULL:
		verbose(env, "R%d pointer arithmetic on %s prohibited, null-check it first\n",
			dst, reg_type_str[ptr_reg->type]);
		return -EACCES;
	case CONST_PTR_TO_MAP:
		/* smin_val represents the known value */
		if (known && smin_val == 0 && opcode == BPF_ADD)
			break;
		fallthrough;
	case PTR_TO_PACKET_END:
	case PTR_TO_SOCKET:
	case PTR_TO_SOCKET_OR_NULL:
	case PTR_TO_SOCK_COMMON:
	case PTR_TO_SOCK_COMMON_OR_NULL:
	case PTR_TO_TCP_SOCK:
	case PTR_TO_TCP_SOCK_OR_NULL:
	case PTR_TO_XDP_SOCK:
		verbose(env, "R%d pointer arithmetic on %s prohibited\n",
			dst, reg_type_str[ptr_reg->type]);
		return -EACCES;
	default:
		break;
	}

	/* In case of 'scalar += pointer', dst_reg inherits pointer type and id.
	 * The id may be overwritten later if we create a new variable offset.
	 */
	dst_reg->type = ptr_reg->type;
	dst_reg->id = ptr_reg->id;

	if (!check_reg_sane_offset(env, off_reg, ptr_reg->type) ||
	    !check_reg_sane_offset(env, ptr_reg, ptr_reg->type))
		return -EINVAL;

	/* pointer types do not carry 32-bit bounds at the moment. */
	__mark_reg32_unbounded(dst_reg);

	if (sanitize_needed(opcode)) {
		ret = sanitize_ptr_alu(env, insn, ptr_reg, off_reg, dst_reg,
				       &tmp_aux, false);
		if (ret < 0)
			return sanitize_err(env, insn, ret, off_reg, dst_reg);
	}

	switch (opcode) {
	case BPF_ADD:
		/* We can take a fixed offset as long as it doesn't overflow
		 * the s32 'off' field
		 */
		if (known && (ptr_reg->off + smin_val ==
			      (s64)(s32)(ptr_reg->off + smin_val))) {
			/* pointer += K.  Accumulate it into fixed offset */
			dst_reg->smin_value = smin_ptr;
			dst_reg->smax_value = smax_ptr;
			dst_reg->umin_value = umin_ptr;
			dst_reg->umax_value = umax_ptr;
			dst_reg->var_off = ptr_reg->var_off;
			dst_reg->off = ptr_reg->off + smin_val;
			dst_reg->raw = ptr_reg->raw;
			break;
		}
		/* A new variable offset is created.  Note that off_reg->off
		 * == 0, since it's a scalar.
		 * dst_reg gets the pointer type and since some positive
		 * integer value was added to the pointer, give it a new 'id'
		 * if it's a PTR_TO_PACKET.
		 * this creates a new 'base' pointer, off_reg (variable) gets
		 * added into the variable offset, and we copy the fixed offset
		 * from ptr_reg.
		 */
		if (signed_add_overflows(smin_ptr, smin_val) ||
		    signed_add_overflows(smax_ptr, smax_val)) {
			dst_reg->smin_value = S64_MIN;
			dst_reg->smax_value = S64_MAX;
		} else {
			dst_reg->smin_value = smin_ptr + smin_val;
			dst_reg->smax_value = smax_ptr + smax_val;
		}
		if (umin_ptr + umin_val < umin_ptr ||
		    umax_ptr + umax_val < umax_ptr) {
			dst_reg->umin_value = 0;
			dst_reg->umax_value = U64_MAX;
		} else {
			dst_reg->umin_value = umin_ptr + umin_val;
			dst_reg->umax_value = umax_ptr + umax_val;
		}
		dst_reg->var_off = tnum_add(ptr_reg->var_off, off_reg->var_off);
		dst_reg->off = ptr_reg->off;
		dst_reg->raw = ptr_reg->raw;
		if (reg_is_pkt_pointer(ptr_reg)) {
			dst_reg->id = ++env->id_gen;
			/* something was added to pkt_ptr, set range to zero */
			memset(&dst_reg->raw, 0, sizeof(dst_reg->raw));
		}
		break;
	case BPF_SUB:
		if (dst_reg == off_reg) {
			/* scalar -= pointer.  Creates an unknown scalar */
			verbose(env, "R%d tried to subtract pointer from scalar\n",
				dst);
			return -EACCES;
		}
		/* We don't allow subtraction from FP, because (according to
		 * test_verifier.c test "invalid fp arithmetic", JITs might not
		 * be able to deal with it.
		 */
		if (ptr_reg->type == PTR_TO_STACK) {
			verbose(env, "R%d subtraction from stack pointer prohibited\n",
				dst);
			return -EACCES;
		}
		if (known && (ptr_reg->off - smin_val ==
			      (s64)(s32)(ptr_reg->off - smin_val))) {
			/* pointer -= K.  Subtract it from fixed offset */
			dst_reg->smin_value = smin_ptr;
			dst_reg->smax_value = smax_ptr;
			dst_reg->umin_value = umin_ptr;
			dst_reg->umax_value = umax_ptr;
			dst_reg->var_off = ptr_reg->var_off;
			dst_reg->id = ptr_reg->id;
			dst_reg->off = ptr_reg->off - smin_val;
			dst_reg->raw = ptr_reg->raw;
			break;
		}
		/* A new variable offset is created.  If the subtrahend is known
		 * nonnegative, then any reg->range we had before is still good.
		 */
		if (signed_sub_overflows(smin_ptr, smax_val) ||
		    signed_sub_overflows(smax_ptr, smin_val)) {
			/* Overflow possible, we know nothing */
			dst_reg->smin_value = S64_MIN;
			dst_reg->smax_value = S64_MAX;
		} else {
			dst_reg->smin_value = smin_ptr - smax_val;
			dst_reg->smax_value = smax_ptr - smin_val;
		}
		if (umin_ptr < umax_val) {
			/* Overflow possible, we know nothing */
			dst_reg->umin_value = 0;
			dst_reg->umax_value = U64_MAX;
		} else {
			/* Cannot overflow (as long as bounds are consistent) */
			dst_reg->umin_value = umin_ptr - umax_val;
			dst_reg->umax_value = umax_ptr - umin_val;
		}
		dst_reg->var_off = tnum_sub(ptr_reg->var_off, off_reg->var_off);
		dst_reg->off = ptr_reg->off;
		dst_reg->raw = ptr_reg->raw;
		if (reg_is_pkt_pointer(ptr_reg)) {
			dst_reg->id = ++env->id_gen;
			/* something was added to pkt_ptr, set range to zero */
			if (smin_val < 0)
				memset(&dst_reg->raw, 0, sizeof(dst_reg->raw));
		}
		break;
	case BPF_AND:
	case BPF_OR:
	case BPF_XOR:
		/* bitwise ops on pointers are troublesome, prohibit. */
		verbose(env, "R%d bitwise operator %s on pointer prohibited\n",
			dst, bpf_alu_string[opcode >> 4]);
		return -EACCES;
	default:
		/* other operators (e.g. MUL,LSH) produce non-pointer results */
		verbose(env, "R%d pointer arithmetic with %s operator prohibited\n",
			dst, bpf_alu_string[opcode >> 4]);
		return -EACCES;
	}

	if (!check_reg_sane_offset(env, dst_reg, ptr_reg->type))
		return -EINVAL;

	__update_reg_bounds(dst_reg);
	__reg_deduce_bounds(dst_reg);
	__reg_bound_offset(dst_reg);

	if (sanitize_check_bounds(env, insn, dst_reg) < 0)
		return -EACCES;
	if (sanitize_needed(opcode)) {
		ret = sanitize_ptr_alu(env, insn, dst_reg, off_reg, dst_reg,
				       &tmp_aux, true);
		if (ret < 0)
			return sanitize_err(env, insn, ret, off_reg, dst_reg);
	}

	return 0;
}

static void scalar32_min_max_add(struct bpf_reg_state *dst_reg,
				 struct bpf_reg_state *src_reg)
{
	s32 smin_val = src_reg->s32_min_value;
	s32 smax_val = src_reg->s32_max_value;
	u32 umin_val = src_reg->u32_min_value;
	u32 umax_val = src_reg->u32_max_value;

	if (signed_add32_overflows(dst_reg->s32_min_value, smin_val) ||
	    signed_add32_overflows(dst_reg->s32_max_value, smax_val)) {
		dst_reg->s32_min_value = S32_MIN;
		dst_reg->s32_max_value = S32_MAX;
	} else {
		dst_reg->s32_min_value += smin_val;
		dst_reg->s32_max_value += smax_val;
	}
	if (dst_reg->u32_min_value + umin_val < umin_val ||
	    dst_reg->u32_max_value + umax_val < umax_val) {
		dst_reg->u32_min_value = 0;
		dst_reg->u32_max_value = U32_MAX;
	} else {
		dst_reg->u32_min_value += umin_val;
		dst_reg->u32_max_value += umax_val;
	}
}

static void scalar_min_max_add(struct bpf_reg_state *dst_reg,
			       struct bpf_reg_state *src_reg)
{
	s64 smin_val = src_reg->smin_value;
	s64 smax_val = src_reg->smax_value;
	u64 umin_val = src_reg->umin_value;
	u64 umax_val = src_reg->umax_value;

	if (signed_add_overflows(dst_reg->smin_value, smin_val) ||
	    signed_add_overflows(dst_reg->smax_value, smax_val)) {
		dst_reg->smin_value = S64_MIN;
		dst_reg->smax_value = S64_MAX;
	} else {
		dst_reg->smin_value += smin_val;
		dst_reg->smax_value += smax_val;
	}
	if (dst_reg->umin_value + umin_val < umin_val ||
	    dst_reg->umax_value + umax_val < umax_val) {
		dst_reg->umin_value = 0;
		dst_reg->umax_value = U64_MAX;
	} else {
		dst_reg->umin_value += umin_val;
		dst_reg->umax_value += umax_val;
	}
}

static void scalar32_min_max_sub(struct bpf_reg_state *dst_reg,
				 struct bpf_reg_state *src_reg)
{
	s32 smin_val = src_reg->s32_min_value;
	s32 smax_val = src_reg->s32_max_value;
	u32 umin_val = src_reg->u32_min_value;
	u32 umax_val = src_reg->u32_max_value;

	if (signed_sub32_overflows(dst_reg->s32_min_value, smax_val) ||
	    signed_sub32_overflows(dst_reg->s32_max_value, smin_val)) {
		/* Overflow possible, we know nothing */
		dst_reg->s32_min_value = S32_MIN;
		dst_reg->s32_max_value = S32_MAX;
	} else {
		dst_reg->s32_min_value -= smax_val;
		dst_reg->s32_max_value -= smin_val;
	}
	if (dst_reg->u32_min_value < umax_val) {
		/* Overflow possible, we know nothing */
		dst_reg->u32_min_value = 0;
		dst_reg->u32_max_value = U32_MAX;
	} else {
		/* Cannot overflow (as long as bounds are consistent) */
		dst_reg->u32_min_value -= umax_val;
		dst_reg->u32_max_value -= umin_val;
	}
}

static void scalar_min_max_sub(struct bpf_reg_state *dst_reg,
			       struct bpf_reg_state *src_reg)
{
	s64 smin_val = src_reg->smin_value;
	s64 smax_val = src_reg->smax_value;
	u64 umin_val = src_reg->umin_value;
	u64 umax_val = src_reg->umax_value;

	if (signed_sub_overflows(dst_reg->smin_value, smax_val) ||
	    signed_sub_overflows(dst_reg->smax_value, smin_val)) {
		/* Overflow possible, we know nothing */
		dst_reg->smin_value = S64_MIN;
		dst_reg->smax_value = S64_MAX;
	} else {
		dst_reg->smin_value -= smax_val;
		dst_reg->smax_value -= smin_val;
	}
	if (dst_reg->umin_value < umax_val) {
		/* Overflow possible, we know nothing */
		dst_reg->umin_value = 0;
		dst_reg->umax_value = U64_MAX;
	} else {
		/* Cannot overflow (as long as bounds are consistent) */
		dst_reg->umin_value -= umax_val;
		dst_reg->umax_value -= umin_val;
	}
}

static void scalar32_min_max_mul(struct bpf_reg_state *dst_reg,
				 struct bpf_reg_state *src_reg)
{
	s32 smin_val = src_reg->s32_min_value;
	u32 umin_val = src_reg->u32_min_value;
	u32 umax_val = src_reg->u32_max_value;

	if (smin_val < 0 || dst_reg->s32_min_value < 0) {
		/* Ain't nobody got time to multiply that sign */
		__mark_reg32_unbounded(dst_reg);
		return;
	}
	/* Both values are positive, so we can work with unsigned and
	 * copy the result to signed (unless it exceeds S32_MAX).
	 */
	if (umax_val > U16_MAX || dst_reg->u32_max_value > U16_MAX) {
		/* Potential overflow, we know nothing */
		__mark_reg32_unbounded(dst_reg);
		return;
	}
	dst_reg->u32_min_value *= umin_val;
	dst_reg->u32_max_value *= umax_val;
	if (dst_reg->u32_max_value > S32_MAX) {
		/* Overflow possible, we know nothing */
		dst_reg->s32_min_value = S32_MIN;
		dst_reg->s32_max_value = S32_MAX;
	} else {
		dst_reg->s32_min_value = dst_reg->u32_min_value;
		dst_reg->s32_max_value = dst_reg->u32_max_value;
	}
}

static void scalar_min_max_mul(struct bpf_reg_state *dst_reg,
			       struct bpf_reg_state *src_reg)
{
	s64 smin_val = src_reg->smin_value;
	u64 umin_val = src_reg->umin_value;
	u64 umax_val = src_reg->umax_value;

	if (smin_val < 0 || dst_reg->smin_value < 0) {
		/* Ain't nobody got time to multiply that sign */
		__mark_reg64_unbounded(dst_reg);
		return;
	}
	/* Both values are positive, so we can work with unsigned and
	 * copy the result to signed (unless it exceeds S64_MAX).
	 */
	if (umax_val > U32_MAX || dst_reg->umax_value > U32_MAX) {
		/* Potential overflow, we know nothing */
		__mark_reg64_unbounded(dst_reg);
		return;
	}
	dst_reg->umin_value *= umin_val;
	dst_reg->umax_value *= umax_val;
	if (dst_reg->umax_value > S64_MAX) {
		/* Overflow possible, we know nothing */
		dst_reg->smin_value = S64_MIN;
		dst_reg->smax_value = S64_MAX;
	} else {
		dst_reg->smin_value = dst_reg->umin_value;
		dst_reg->smax_value = dst_reg->umax_value;
	}
}

static void scalar32_min_max_and(struct bpf_reg_state *dst_reg,
				 struct bpf_reg_state *src_reg)
{
	bool src_known = tnum_subreg_is_const(src_reg->var_off);
	bool dst_known = tnum_subreg_is_const(dst_reg->var_off);
	struct tnum var32_off = tnum_subreg(dst_reg->var_off);
	s32 smin_val = src_reg->s32_min_value;
	u32 umax_val = src_reg->u32_max_value;

	/* Assuming scalar64_min_max_and will be called so its safe
	 * to skip updating register for known 32-bit case.
	 */
	if (src_known && dst_known)
		return;

	/* We get our minimum from the var_off, since that's inherently
	 * bitwise.  Our maximum is the minimum of the operands' maxima.
	 */
	dst_reg->u32_min_value = var32_off.value;
	dst_reg->u32_max_value = min(dst_reg->u32_max_value, umax_val);
	if (dst_reg->s32_min_value < 0 || smin_val < 0) {
		/* Lose signed bounds when ANDing negative numbers,
		 * ain't nobody got time for that.
		 */
		dst_reg->s32_min_value = S32_MIN;
		dst_reg->s32_max_value = S32_MAX;
	} else {
		/* ANDing two positives gives a positive, so safe to
		 * cast result into s64.
		 */
		dst_reg->s32_min_value = dst_reg->u32_min_value;
		dst_reg->s32_max_value = dst_reg->u32_max_value;
	}

}

static void scalar_min_max_and(struct bpf_reg_state *dst_reg,
			       struct bpf_reg_state *src_reg)
{
	bool src_known = tnum_is_const(src_reg->var_off);
	bool dst_known = tnum_is_const(dst_reg->var_off);
	s64 smin_val = src_reg->smin_value;
	u64 umax_val = src_reg->umax_value;

	if (src_known && dst_known) {
		__mark_reg_known(dst_reg, dst_reg->var_off.value);
		return;
	}

	/* We get our minimum from the var_off, since that's inherently
	 * bitwise.  Our maximum is the minimum of the operands' maxima.
	 */
	dst_reg->umin_value = dst_reg->var_off.value;
	dst_reg->umax_value = min(dst_reg->umax_value, umax_val);
	if (dst_reg->smin_value < 0 || smin_val < 0) {
		/* Lose signed bounds when ANDing negative numbers,
		 * ain't nobody got time for that.
		 */
		dst_reg->smin_value = S64_MIN;
		dst_reg->smax_value = S64_MAX;
	} else {
		/* ANDing two positives gives a positive, so safe to
		 * cast result into s64.
		 */
		dst_reg->smin_value = dst_reg->umin_value;
		dst_reg->smax_value = dst_reg->umax_value;
	}
	/* We may learn something more from the var_off */
	__update_reg_bounds(dst_reg);
}

static void scalar32_min_max_or(struct bpf_reg_state *dst_reg,
				struct bpf_reg_state *src_reg)
{
	bool src_known = tnum_subreg_is_const(src_reg->var_off);
	bool dst_known = tnum_subreg_is_const(dst_reg->var_off);
	struct tnum var32_off = tnum_subreg(dst_reg->var_off);
	s32 smin_val = src_reg->s32_min_value;
	u32 umin_val = src_reg->u32_min_value;

	/* Assuming scalar64_min_max_or will be called so it is safe
	 * to skip updating register for known case.
	 */
	if (src_known && dst_known)
		return;

	/* We get our maximum from the var_off, and our minimum is the
	 * maximum of the operands' minima
	 */
	dst_reg->u32_min_value = max(dst_reg->u32_min_value, umin_val);
	dst_reg->u32_max_value = var32_off.value | var32_off.mask;
	if (dst_reg->s32_min_value < 0 || smin_val < 0) {
		/* Lose signed bounds when ORing negative numbers,
		 * ain't nobody got time for that.
		 */
		dst_reg->s32_min_value = S32_MIN;
		dst_reg->s32_max_value = S32_MAX;
	} else {
		/* ORing two positives gives a positive, so safe to
		 * cast result into s64.
		 */
		dst_reg->s32_min_value = dst_reg->u32_min_value;
		dst_reg->s32_max_value = dst_reg->u32_max_value;
	}
}

static void scalar_min_max_or(struct bpf_reg_state *dst_reg,
			      struct bpf_reg_state *src_reg)
{
	bool src_known = tnum_is_const(src_reg->var_off);
	bool dst_known = tnum_is_const(dst_reg->var_off);
	s64 smin_val = src_reg->smin_value;
	u64 umin_val = src_reg->umin_value;

	if (src_known && dst_known) {
		__mark_reg_known(dst_reg, dst_reg->var_off.value);
		return;
	}

	/* We get our maximum from the var_off, and our minimum is the
	 * maximum of the operands' minima
	 */
	dst_reg->umin_value = max(dst_reg->umin_value, umin_val);
	dst_reg->umax_value = dst_reg->var_off.value | dst_reg->var_off.mask;
	if (dst_reg->smin_value < 0 || smin_val < 0) {
		/* Lose signed bounds when ORing negative numbers,
		 * ain't nobody got time for that.
		 */
		dst_reg->smin_value = S64_MIN;
		dst_reg->smax_value = S64_MAX;
	} else {
		/* ORing two positives gives a positive, so safe to
		 * cast result into s64.
		 */
		dst_reg->smin_value = dst_reg->umin_value;
		dst_reg->smax_value = dst_reg->umax_value;
	}
	/* We may learn something more from the var_off */
	__update_reg_bounds(dst_reg);
}

static void scalar32_min_max_xor(struct bpf_reg_state *dst_reg,
				 struct bpf_reg_state *src_reg)
{
	bool src_known = tnum_subreg_is_const(src_reg->var_off);
	bool dst_known = tnum_subreg_is_const(dst_reg->var_off);
	struct tnum var32_off = tnum_subreg(dst_reg->var_off);
	s32 smin_val = src_reg->s32_min_value;

	/* Assuming scalar64_min_max_xor will be called so it is safe
	 * to skip updating register for known case.
	 */
	if (src_known && dst_known)
		return;

	/* We get both minimum and maximum from the var32_off. */
	dst_reg->u32_min_value = var32_off.value;
	dst_reg->u32_max_value = var32_off.value | var32_off.mask;

	if (dst_reg->s32_min_value >= 0 && smin_val >= 0) {
		/* XORing two positive sign numbers gives a positive,
		 * so safe to cast u32 result into s32.
		 */
		dst_reg->s32_min_value = dst_reg->u32_min_value;
		dst_reg->s32_max_value = dst_reg->u32_max_value;
	} else {
		dst_reg->s32_min_value = S32_MIN;
		dst_reg->s32_max_value = S32_MAX;
	}
}

static void scalar_min_max_xor(struct bpf_reg_state *dst_reg,
			       struct bpf_reg_state *src_reg)
{
	bool src_known = tnum_is_const(src_reg->var_off);
	bool dst_known = tnum_is_const(dst_reg->var_off);
	s64 smin_val = src_reg->smin_value;

	if (src_known && dst_known) {
		/* dst_reg->var_off.value has been updated earlier */
		__mark_reg_known(dst_reg, dst_reg->var_off.value);
		return;
	}

	/* We get both minimum and maximum from the var_off. */
	dst_reg->umin_value = dst_reg->var_off.value;
	dst_reg->umax_value = dst_reg->var_off.value | dst_reg->var_off.mask;

	if (dst_reg->smin_value >= 0 && smin_val >= 0) {
		/* XORing two positive sign numbers gives a positive,
		 * so safe to cast u64 result into s64.
		 */
		dst_reg->smin_value = dst_reg->umin_value;
		dst_reg->smax_value = dst_reg->umax_value;
	} else {
		dst_reg->smin_value = S64_MIN;
		dst_reg->smax_value = S64_MAX;
	}

	__update_reg_bounds(dst_reg);
}

static void __scalar32_min_max_lsh(struct bpf_reg_state *dst_reg,
				   u64 umin_val, u64 umax_val)
{
	/* We lose all sign bit information (except what we can pick
	 * up from var_off)
	 */
	dst_reg->s32_min_value = S32_MIN;
	dst_reg->s32_max_value = S32_MAX;
	/* If we might shift our top bit out, then we know nothing */
	if (umax_val > 31 || dst_reg->u32_max_value > 1ULL << (31 - umax_val)) {
		dst_reg->u32_min_value = 0;
		dst_reg->u32_max_value = U32_MAX;
	} else {
		dst_reg->u32_min_value <<= umin_val;
		dst_reg->u32_max_value <<= umax_val;
	}
}

static void scalar32_min_max_lsh(struct bpf_reg_state *dst_reg,
				 struct bpf_reg_state *src_reg)
{
	u32 umax_val = src_reg->u32_max_value;
	u32 umin_val = src_reg->u32_min_value;
	/* u32 alu operation will zext upper bits */
	struct tnum subreg = tnum_subreg(dst_reg->var_off);

	__scalar32_min_max_lsh(dst_reg, umin_val, umax_val);
	dst_reg->var_off = tnum_subreg(tnum_lshift(subreg, umin_val));
	/* Not required but being careful mark reg64 bounds as unknown so
	 * that we are forced to pick them up from tnum and zext later and
	 * if some path skips this step we are still safe.
	 */
	__mark_reg64_unbounded(dst_reg);
	__update_reg32_bounds(dst_reg);
}

static void __scalar64_min_max_lsh(struct bpf_reg_state *dst_reg,
				   u64 umin_val, u64 umax_val)
{
	/* Special case <<32 because it is a common compiler pattern to sign
	 * extend subreg by doing <<32 s>>32. In this case if 32bit bounds are
	 * positive we know this shift will also be positive so we can track
	 * bounds correctly. Otherwise we lose all sign bit information except
	 * what we can pick up from var_off. Perhaps we can generalize this
	 * later to shifts of any length.
	 */
	if (umin_val == 32 && umax_val == 32 && dst_reg->s32_max_value >= 0)
		dst_reg->smax_value = (s64)dst_reg->s32_max_value << 32;
	else
		dst_reg->smax_value = S64_MAX;

	if (umin_val == 32 && umax_val == 32 && dst_reg->s32_min_value >= 0)
		dst_reg->smin_value = (s64)dst_reg->s32_min_value << 32;
	else
		dst_reg->smin_value = S64_MIN;

	/* If we might shift our top bit out, then we know nothing */
	if (dst_reg->umax_value > 1ULL << (63 - umax_val)) {
		dst_reg->umin_value = 0;
		dst_reg->umax_value = U64_MAX;
	} else {
		dst_reg->umin_value <<= umin_val;
		dst_reg->umax_value <<= umax_val;
	}
}

static void scalar_min_max_lsh(struct bpf_reg_state *dst_reg,
			       struct bpf_reg_state *src_reg)
{
	u64 umax_val = src_reg->umax_value;
	u64 umin_val = src_reg->umin_value;

	/* scalar64 calc uses 32bit unshifted bounds so must be called first */
	__scalar64_min_max_lsh(dst_reg, umin_val, umax_val);
	__scalar32_min_max_lsh(dst_reg, umin_val, umax_val);

	dst_reg->var_off = tnum_lshift(dst_reg->var_off, umin_val);
	/* We may learn something more from the var_off */
	__update_reg_bounds(dst_reg);
}

static void scalar32_min_max_rsh(struct bpf_reg_state *dst_reg,
				 struct bpf_reg_state *src_reg)
{
	struct tnum subreg = tnum_subreg(dst_reg->var_off);
	u32 umax_val = src_reg->u32_max_value;
	u32 umin_val = src_reg->u32_min_value;

	/* BPF_RSH is an unsigned shift.  If the value in dst_reg might
	 * be negative, then either:
	 * 1) src_reg might be zero, so the sign bit of the result is
	 *    unknown, so we lose our signed bounds
	 * 2) it's known negative, thus the unsigned bounds capture the
	 *    signed bounds
	 * 3) the signed bounds cross zero, so they tell us nothing
	 *    about the result
	 * If the value in dst_reg is known nonnegative, then again the
	 * unsigned bounds capture the signed bounds.
	 * Thus, in all cases it suffices to blow away our signed bounds
	 * and rely on inferring new ones from the unsigned bounds and
	 * var_off of the result.
	 */
	dst_reg->s32_min_value = S32_MIN;
	dst_reg->s32_max_value = S32_MAX;

	dst_reg->var_off = tnum_rshift(subreg, umin_val);
	dst_reg->u32_min_value >>= umax_val;
	dst_reg->u32_max_value >>= umin_val;

	__mark_reg64_unbounded(dst_reg);
	__update_reg32_bounds(dst_reg);
}

static void scalar_min_max_rsh(struct bpf_reg_state *dst_reg,
			       struct bpf_reg_state *src_reg)
{
	u64 umax_val = src_reg->umax_value;
	u64 umin_val = src_reg->umin_value;

	/* BPF_RSH is an unsigned shift.  If the value in dst_reg might
	 * be negative, then either:
	 * 1) src_reg might be zero, so the sign bit of the result is
	 *    unknown, so we lose our signed bounds
	 * 2) it's known negative, thus the unsigned bounds capture the
	 *    signed bounds
	 * 3) the signed bounds cross zero, so they tell us nothing
	 *    about the result
	 * If the value in dst_reg is known nonnegative, then again the
	 * unsigned bounds capture the signed bounds.
	 * Thus, in all cases it suffices to blow away our signed bounds
	 * and rely on inferring new ones from the unsigned bounds and
	 * var_off of the result.
	 */
	dst_reg->smin_value = S64_MIN;
	dst_reg->smax_value = S64_MAX;
	dst_reg->var_off = tnum_rshift(dst_reg->var_off, umin_val);
	dst_reg->umin_value >>= umax_val;
	dst_reg->umax_value >>= umin_val;

	/* Its not easy to operate on alu32 bounds here because it depends
	 * on bits being shifted in. Take easy way out and mark unbounded
	 * so we can recalculate later from tnum.
	 */
	__mark_reg32_unbounded(dst_reg);
	__update_reg_bounds(dst_reg);
}

static void scalar32_min_max_arsh(struct bpf_reg_state *dst_reg,
				  struct bpf_reg_state *src_reg)
{
	u64 umin_val = src_reg->u32_min_value;

	/* Upon reaching here, src_known is true and
	 * umax_val is equal to umin_val.
	 */
	dst_reg->s32_min_value = (u32)(((s32)dst_reg->s32_min_value) >> umin_val);
	dst_reg->s32_max_value = (u32)(((s32)dst_reg->s32_max_value) >> umin_val);

	dst_reg->var_off = tnum_arshift(tnum_subreg(dst_reg->var_off), umin_val, 32);

	/* blow away the dst_reg umin_value/umax_value and rely on
	 * dst_reg var_off to refine the result.
	 */
	dst_reg->u32_min_value = 0;
	dst_reg->u32_max_value = U32_MAX;

	__mark_reg64_unbounded(dst_reg);
	__update_reg32_bounds(dst_reg);
}

static void scalar_min_max_arsh(struct bpf_reg_state *dst_reg,
				struct bpf_reg_state *src_reg)
{
	u64 umin_val = src_reg->umin_value;

	/* Upon reaching here, src_known is true and umax_val is equal
	 * to umin_val.
	 */
	dst_reg->smin_value >>= umin_val;
	dst_reg->smax_value >>= umin_val;

	dst_reg->var_off = tnum_arshift(dst_reg->var_off, umin_val, 64);

	/* blow away the dst_reg umin_value/umax_value and rely on
	 * dst_reg var_off to refine the result.
	 */
	dst_reg->umin_value = 0;
	dst_reg->umax_value = U64_MAX;

	/* Its not easy to operate on alu32 bounds here because it depends
	 * on bits being shifted in from upper 32-bits. Take easy way out
	 * and mark unbounded so we can recalculate later from tnum.
	 */
	__mark_reg32_unbounded(dst_reg);
	__update_reg_bounds(dst_reg);
}

/* WARNING: This function does calculations on 64-bit values, but the actual
 * execution may occur on 32-bit values. Therefore, things like bitshifts
 * need extra checks in the 32-bit case.
 */
static int adjust_scalar_min_max_vals(struct bpf_verifier_env *env,
				      struct bpf_insn *insn,
				      struct bpf_reg_state *dst_reg,
				      struct bpf_reg_state src_reg)
{
	struct bpf_reg_state *regs = cur_regs(env);
	u8 opcode = BPF_OP(insn->code);
	bool src_known;
	s64 smin_val, smax_val;
	u64 umin_val, umax_val;
	s32 s32_min_val, s32_max_val;
	u32 u32_min_val, u32_max_val;
	u64 insn_bitness = (BPF_CLASS(insn->code) == BPF_ALU64) ? 64 : 32;
	bool alu32 = (BPF_CLASS(insn->code) != BPF_ALU64);
	int ret;

	smin_val = src_reg.smin_value;
	smax_val = src_reg.smax_value;
	umin_val = src_reg.umin_value;
	umax_val = src_reg.umax_value;

	s32_min_val = src_reg.s32_min_value;
	s32_max_val = src_reg.s32_max_value;
	u32_min_val = src_reg.u32_min_value;
	u32_max_val = src_reg.u32_max_value;

	if (alu32) {
		src_known = tnum_subreg_is_const(src_reg.var_off);
		if ((src_known &&
		     (s32_min_val != s32_max_val || u32_min_val != u32_max_val)) ||
		    s32_min_val > s32_max_val || u32_min_val > u32_max_val) {
			/* Taint dst register if offset had invalid bounds
			 * derived from e.g. dead branches.
			 */
			__mark_reg_unknown(env, dst_reg);
			return 0;
		}
	} else {
		src_known = tnum_is_const(src_reg.var_off);
		if ((src_known &&
		     (smin_val != smax_val || umin_val != umax_val)) ||
		    smin_val > smax_val || umin_val > umax_val) {
			/* Taint dst register if offset had invalid bounds
			 * derived from e.g. dead branches.
			 */
			__mark_reg_unknown(env, dst_reg);
			return 0;
		}
	}

	if (!src_known &&
	    opcode != BPF_ADD && opcode != BPF_SUB && opcode != BPF_AND) {
		__mark_reg_unknown(env, dst_reg);
		return 0;
	}

	if (sanitize_needed(opcode)) {
		ret = sanitize_val_alu(env, insn);
		if (ret < 0)
			return sanitize_err(env, insn, ret, NULL, NULL);
	}

	/* Calculate sign/unsigned bounds and tnum for alu32 and alu64 bit ops.
	 * There are two classes of instructions: The first class we track both
	 * alu32 and alu64 sign/unsigned bounds independently this provides the
	 * greatest amount of precision when alu operations are mixed with jmp32
	 * operations. These operations are BPF_ADD, BPF_SUB, BPF_MUL, BPF_ADD,
	 * and BPF_OR. This is possible because these ops have fairly easy to
	 * understand and calculate behavior in both 32-bit and 64-bit alu ops.
	 * See alu32 verifier tests for examples. The second class of
	 * operations, BPF_LSH, BPF_RSH, and BPF_ARSH, however are not so easy
	 * with regards to tracking sign/unsigned bounds because the bits may
	 * cross subreg boundaries in the alu64 case. When this happens we mark
	 * the reg unbounded in the subreg bound space and use the resulting
	 * tnum to calculate an approximation of the sign/unsigned bounds.
	 */
	switch (opcode) {
	case BPF_ADD:
		scalar32_min_max_add(dst_reg, &src_reg);
		scalar_min_max_add(dst_reg, &src_reg);
		dst_reg->var_off = tnum_add(dst_reg->var_off, src_reg.var_off);
		break;
	case BPF_SUB:
		scalar32_min_max_sub(dst_reg, &src_reg);
		scalar_min_max_sub(dst_reg, &src_reg);
		dst_reg->var_off = tnum_sub(dst_reg->var_off, src_reg.var_off);
		break;
	case BPF_MUL:
		dst_reg->var_off = tnum_mul(dst_reg->var_off, src_reg.var_off);
		scalar32_min_max_mul(dst_reg, &src_reg);
		scalar_min_max_mul(dst_reg, &src_reg);
		break;
	case BPF_AND:
		dst_reg->var_off = tnum_and(dst_reg->var_off, src_reg.var_off);
		scalar32_min_max_and(dst_reg, &src_reg);
		scalar_min_max_and(dst_reg, &src_reg);
		break;
	case BPF_OR:
		dst_reg->var_off = tnum_or(dst_reg->var_off, src_reg.var_off);
		scalar32_min_max_or(dst_reg, &src_reg);
		scalar_min_max_or(dst_reg, &src_reg);
		break;
	case BPF_XOR:
		dst_reg->var_off = tnum_xor(dst_reg->var_off, src_reg.var_off);
		scalar32_min_max_xor(dst_reg, &src_reg);
		scalar_min_max_xor(dst_reg, &src_reg);
		break;
	case BPF_LSH:
		if (umax_val >= insn_bitness) {
			/* Shifts greater than 31 or 63 are undefined.
			 * This includes shifts by a negative number.
			 */
			mark_reg_unknown(env, regs, insn->dst_reg);
			break;
		}
		if (alu32)
			scalar32_min_max_lsh(dst_reg, &src_reg);
		else
			scalar_min_max_lsh(dst_reg, &src_reg);
		break;
	case BPF_RSH:
		if (umax_val >= insn_bitness) {
			/* Shifts greater than 31 or 63 are undefined.
			 * This includes shifts by a negative number.
			 */
			mark_reg_unknown(env, regs, insn->dst_reg);
			break;
		}
		if (alu32)
			scalar32_min_max_rsh(dst_reg, &src_reg);
		else
			scalar_min_max_rsh(dst_reg, &src_reg);
		break;
	case BPF_ARSH:
		if (umax_val >= insn_bitness) {
			/* Shifts greater than 31 or 63 are undefined.
			 * This includes shifts by a negative number.
			 */
			mark_reg_unknown(env, regs, insn->dst_reg);
			break;
		}
		if (alu32)
			scalar32_min_max_arsh(dst_reg, &src_reg);
		else
			scalar_min_max_arsh(dst_reg, &src_reg);
		break;
	default:
		mark_reg_unknown(env, regs, insn->dst_reg);
		break;
	}

	/* ALU32 ops are zero extended into 64bit register */
	if (alu32)
		zext_32_to_64(dst_reg);

	__update_reg_bounds(dst_reg);
	__reg_deduce_bounds(dst_reg);
	__reg_bound_offset(dst_reg);
	return 0;
}

/* Handles ALU ops other than BPF_END, BPF_NEG and BPF_MOV: computes new min/max
 * and var_off.
 */
static int adjust_reg_min_max_vals(struct bpf_verifier_env *env,
				   struct bpf_insn *insn)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	struct bpf_func_state *state = vstate->frame[vstate->curframe];
	struct bpf_reg_state *regs = state->regs, *dst_reg, *src_reg;
	struct bpf_reg_state *ptr_reg = NULL, off_reg = {0};
	u8 opcode = BPF_OP(insn->code);
	int err;

	dst_reg = &regs[insn->dst_reg];
	src_reg = NULL;
	if (dst_reg->type != SCALAR_VALUE)
		ptr_reg = dst_reg;
	else
		/* Make sure ID is cleared otherwise dst_reg min/max could be
		 * incorrectly propagated into other registers by find_equal_scalars()
		 */
		dst_reg->id = 0;
	if (BPF_SRC(insn->code) == BPF_X) {
		src_reg = &regs[insn->src_reg];
		if (src_reg->type != SCALAR_VALUE) {
			if (dst_reg->type != SCALAR_VALUE) {
				/* Combining two pointers by any ALU op yields
				 * an arbitrary scalar. Disallow all math except
				 * pointer subtraction
				 */
				if (opcode == BPF_SUB && env->allow_ptr_leaks) {
					mark_reg_unknown(env, regs, insn->dst_reg);
					return 0;
				}
				verbose(env, "R%d pointer %s pointer prohibited\n",
					insn->dst_reg,
					bpf_alu_string[opcode >> 4]);
				return -EACCES;
			} else {
				/* scalar += pointer
				 * This is legal, but we have to reverse our
				 * src/dest handling in computing the range
				 */
				err = mark_chain_precision(env, insn->dst_reg);
				if (err)
					return err;
				return adjust_ptr_min_max_vals(env, insn,
							       src_reg, dst_reg);
			}
		} else if (ptr_reg) {
			/* pointer += scalar */
			err = mark_chain_precision(env, insn->src_reg);
			if (err)
				return err;
			return adjust_ptr_min_max_vals(env, insn,
						       dst_reg, src_reg);
		}
	} else {
		/* Pretend the src is a reg with a known value, since we only
		 * need to be able to read from this state.
		 */
		off_reg.type = SCALAR_VALUE;
		__mark_reg_known(&off_reg, insn->imm);
		src_reg = &off_reg;
		if (ptr_reg) /* pointer += K */
			return adjust_ptr_min_max_vals(env, insn,
						       ptr_reg, src_reg);
	}

	/* Got here implies adding two SCALAR_VALUEs */
	if (WARN_ON_ONCE(ptr_reg)) {
		print_verifier_state(env, state);
		verbose(env, "verifier internal error: unexpected ptr_reg\n");
		return -EINVAL;
	}
	if (WARN_ON(!src_reg)) {
		print_verifier_state(env, state);
		verbose(env, "verifier internal error: no src_reg\n");
		return -EINVAL;
	}
	return adjust_scalar_min_max_vals(env, insn, dst_reg, *src_reg);
}

/* check validity of 32-bit and 64-bit arithmetic operations */
static int check_alu_op(struct bpf_verifier_env *env, struct bpf_insn *insn)
{
	struct bpf_reg_state *regs = cur_regs(env);
	u8 opcode = BPF_OP(insn->code);
	int err;

	if (opcode == BPF_END || opcode == BPF_NEG) {
		if (opcode == BPF_NEG) {
			if (BPF_SRC(insn->code) != 0 ||
			    insn->src_reg != BPF_REG_0 ||
			    insn->off != 0 || insn->imm != 0) {
				verbose(env, "BPF_NEG uses reserved fields\n");
				return -EINVAL;
			}
		} else {
			if (insn->src_reg != BPF_REG_0 || insn->off != 0 ||
			    (insn->imm != 16 && insn->imm != 32 && insn->imm != 64) ||
			    BPF_CLASS(insn->code) == BPF_ALU64) {
				verbose(env, "BPF_END uses reserved fields\n");
				return -EINVAL;
			}
		}

		/* check src operand */
		err = check_reg_arg(env, insn->dst_reg, SRC_OP);
		if (err)
			return err;

		if (is_pointer_value(env, insn->dst_reg)) {
			verbose(env, "R%d pointer arithmetic prohibited\n",
				insn->dst_reg);
			return -EACCES;
		}

		/* check dest operand */
		err = check_reg_arg(env, insn->dst_reg, DST_OP);
		if (err)
			return err;

	} else if (opcode == BPF_MOV) {

		if (BPF_SRC(insn->code) == BPF_X) {
			if (insn->imm != 0 || insn->off != 0) {
				verbose(env, "BPF_MOV uses reserved fields\n");
				return -EINVAL;
			}

			/* check src operand */
			err = check_reg_arg(env, insn->src_reg, SRC_OP);
			if (err)
				return err;
		} else {
			if (insn->src_reg != BPF_REG_0 || insn->off != 0) {
				verbose(env, "BPF_MOV uses reserved fields\n");
				return -EINVAL;
			}
		}

		/* check dest operand, mark as required later */
		err = check_reg_arg(env, insn->dst_reg, DST_OP_NO_MARK);
		if (err)
			return err;

		if (BPF_SRC(insn->code) == BPF_X) {
			struct bpf_reg_state *src_reg = regs + insn->src_reg;
			struct bpf_reg_state *dst_reg = regs + insn->dst_reg;

			if (BPF_CLASS(insn->code) == BPF_ALU64) {
				/* case: R1 = R2
				 * copy register state to dest reg
				 */
				if (src_reg->type == SCALAR_VALUE && !src_reg->id)
					/* Assign src and dst registers the same ID
					 * that will be used by find_equal_scalars()
					 * to propagate min/max range.
					 */
					src_reg->id = ++env->id_gen;
				*dst_reg = *src_reg;
				dst_reg->live |= REG_LIVE_WRITTEN;
				dst_reg->subreg_def = DEF_NOT_SUBREG;
			} else {
				/* R1 = (u32) R2 */
				if (is_pointer_value(env, insn->src_reg)) {
					verbose(env,
						"R%d partial copy of pointer\n",
						insn->src_reg);
					return -EACCES;
				} else if (src_reg->type == SCALAR_VALUE) {
					*dst_reg = *src_reg;
					/* Make sure ID is cleared otherwise
					 * dst_reg min/max could be incorrectly
					 * propagated into src_reg by find_equal_scalars()
					 */
					dst_reg->id = 0;
					dst_reg->live |= REG_LIVE_WRITTEN;
					dst_reg->subreg_def = env->insn_idx + 1;
				} else {
					mark_reg_unknown(env, regs,
							 insn->dst_reg);
				}
				zext_32_to_64(dst_reg);
			}
		} else {
			/* case: R = imm
			 * remember the value we stored into this reg
			 */
			/* clear any state __mark_reg_known doesn't set */
			mark_reg_unknown(env, regs, insn->dst_reg);
			regs[insn->dst_reg].type = SCALAR_VALUE;
			if (BPF_CLASS(insn->code) == BPF_ALU64) {
				__mark_reg_known(regs + insn->dst_reg,
						 insn->imm);
			} else {
				__mark_reg_known(regs + insn->dst_reg,
						 (u32)insn->imm);
			}
		}

	} else if (opcode > BPF_END) {
		verbose(env, "invalid BPF_ALU opcode %x\n", opcode);
		return -EINVAL;

	} else {	/* all other ALU ops: and, sub, xor, add, ... */

		if (BPF_SRC(insn->code) == BPF_X) {
			if (insn->imm != 0 || insn->off != 0) {
				verbose(env, "BPF_ALU uses reserved fields\n");
				return -EINVAL;
			}
			/* check src1 operand */
			err = check_reg_arg(env, insn->src_reg, SRC_OP);
			if (err)
				return err;
		} else {
			if (insn->src_reg != BPF_REG_0 || insn->off != 0) {
				verbose(env, "BPF_ALU uses reserved fields\n");
				return -EINVAL;
			}
		}

		/* check src2 operand */
		err = check_reg_arg(env, insn->dst_reg, SRC_OP);
		if (err)
			return err;

		if ((opcode == BPF_MOD || opcode == BPF_DIV) &&
		    BPF_SRC(insn->code) == BPF_K && insn->imm == 0) {
			verbose(env, "div by zero\n");
			return -EINVAL;
		}

		if ((opcode == BPF_LSH || opcode == BPF_RSH ||
		     opcode == BPF_ARSH) && BPF_SRC(insn->code) == BPF_K) {
			int size = BPF_CLASS(insn->code) == BPF_ALU64 ? 64 : 32;

			if (insn->imm < 0 || insn->imm >= size) {
				verbose(env, "invalid shift %d\n", insn->imm);
				return -EINVAL;
			}
		}

		/* check dest operand */
		err = check_reg_arg(env, insn->dst_reg, DST_OP_NO_MARK);
		if (err)
			return err;

		return adjust_reg_min_max_vals(env, insn);
	}

	return 0;
}

static void __find_good_pkt_pointers(struct bpf_func_state *state,
				     struct bpf_reg_state *dst_reg,
				     enum bpf_reg_type type, int new_range)
{
	struct bpf_reg_state *reg;
	int i;

	for (i = 0; i < MAX_BPF_REG; i++) {
		reg = &state->regs[i];
		if (reg->type == type && reg->id == dst_reg->id)
			/* keep the maximum range already checked */
			reg->range = max(reg->range, new_range);
	}

	bpf_for_each_spilled_reg(i, state, reg) {
		if (!reg)
			continue;
		if (reg->type == type && reg->id == dst_reg->id)
			reg->range = max(reg->range, new_range);
	}
}

static void find_good_pkt_pointers(struct bpf_verifier_state *vstate,
				   struct bpf_reg_state *dst_reg,
				   enum bpf_reg_type type,
				   bool range_right_open)
{
	int new_range, i;

	if (dst_reg->off < 0 ||
	    (dst_reg->off == 0 && range_right_open))
		/* This doesn't give us any range */
		return;

	if (dst_reg->umax_value > MAX_PACKET_OFF ||
	    dst_reg->umax_value + dst_reg->off > MAX_PACKET_OFF)
		/* Risk of overflow.  For instance, ptr + (1<<63) may be less
		 * than pkt_end, but that's because it's also less than pkt.
		 */
		return;

	new_range = dst_reg->off;
	if (range_right_open)
		new_range--;

	/* Examples for register markings:
	 *
	 * pkt_data in dst register:
	 *
	 *   r2 = r3;
	 *   r2 += 8;
	 *   if (r2 > pkt_end) goto <handle exception>
	 *   <access okay>
	 *
	 *   r2 = r3;
	 *   r2 += 8;
	 *   if (r2 < pkt_end) goto <access okay>
	 *   <handle exception>
	 *
	 *   Where:
	 *     r2 == dst_reg, pkt_end == src_reg
	 *     r2=pkt(id=n,off=8,r=0)
	 *     r3=pkt(id=n,off=0,r=0)
	 *
	 * pkt_data in src register:
	 *
	 *   r2 = r3;
	 *   r2 += 8;
	 *   if (pkt_end >= r2) goto <access okay>
	 *   <handle exception>
	 *
	 *   r2 = r3;
	 *   r2 += 8;
	 *   if (pkt_end <= r2) goto <handle exception>
	 *   <access okay>
	 *
	 *   Where:
	 *     pkt_end == dst_reg, r2 == src_reg
	 *     r2=pkt(id=n,off=8,r=0)
	 *     r3=pkt(id=n,off=0,r=0)
	 *
	 * Find register r3 and mark its range as r3=pkt(id=n,off=0,r=8)
	 * or r3=pkt(id=n,off=0,r=8-1), so that range of bytes [r3, r3 + 8)
	 * and [r3, r3 + 8-1) respectively is safe to access depending on
	 * the check.
	 */

	/* If our ids match, then we must have the same max_value.  And we
	 * don't care about the other reg's fixed offset, since if it's too big
	 * the range won't allow anything.
	 * dst_reg->off is known < MAX_PACKET_OFF, therefore it fits in a u16.
	 */
	for (i = 0; i <= vstate->curframe; i++)
		__find_good_pkt_pointers(vstate->frame[i], dst_reg, type,
					 new_range);
}

static int is_branch32_taken(struct bpf_reg_state *reg, u32 val, u8 opcode)
{
	struct tnum subreg = tnum_subreg(reg->var_off);
	s32 sval = (s32)val;

	switch (opcode) {
	case BPF_JEQ:
		if (tnum_is_const(subreg))
			return !!tnum_equals_const(subreg, val);
		break;
	case BPF_JNE:
		if (tnum_is_const(subreg))
			return !tnum_equals_const(subreg, val);
		break;
	case BPF_JSET:
		if ((~subreg.mask & subreg.value) & val)
			return 1;
		if (!((subreg.mask | subreg.value) & val))
			return 0;
		break;
	case BPF_JGT:
		if (reg->u32_min_value > val)
			return 1;
		else if (reg->u32_max_value <= val)
			return 0;
		break;
	case BPF_JSGT:
		if (reg->s32_min_value > sval)
			return 1;
		else if (reg->s32_max_value <= sval)
			return 0;
		break;
	case BPF_JLT:
		if (reg->u32_max_value < val)
			return 1;
		else if (reg->u32_min_value >= val)
			return 0;
		break;
	case BPF_JSLT:
		if (reg->s32_max_value < sval)
			return 1;
		else if (reg->s32_min_value >= sval)
			return 0;
		break;
	case BPF_JGE:
		if (reg->u32_min_value >= val)
			return 1;
		else if (reg->u32_max_value < val)
			return 0;
		break;
	case BPF_JSGE:
		if (reg->s32_min_value >= sval)
			return 1;
		else if (reg->s32_max_value < sval)
			return 0;
		break;
	case BPF_JLE:
		if (reg->u32_max_value <= val)
			return 1;
		else if (reg->u32_min_value > val)
			return 0;
		break;
	case BPF_JSLE:
		if (reg->s32_max_value <= sval)
			return 1;
		else if (reg->s32_min_value > sval)
			return 0;
		break;
	}

	return -1;
}


static int is_branch64_taken(struct bpf_reg_state *reg, u64 val, u8 opcode)
{
	s64 sval = (s64)val;

	switch (opcode) {
	case BPF_JEQ:
		if (tnum_is_const(reg->var_off))
			return !!tnum_equals_const(reg->var_off, val);
		break;
	case BPF_JNE:
		if (tnum_is_const(reg->var_off))
			return !tnum_equals_const(reg->var_off, val);
		break;
	case BPF_JSET:
		if ((~reg->var_off.mask & reg->var_off.value) & val)
			return 1;
		if (!((reg->var_off.mask | reg->var_off.value) & val))
			return 0;
		break;
	case BPF_JGT:
		if (reg->umin_value > val)
			return 1;
		else if (reg->umax_value <= val)
			return 0;
		break;
	case BPF_JSGT:
		if (reg->smin_value > sval)
			return 1;
		else if (reg->smax_value <= sval)
			return 0;
		break;
	case BPF_JLT:
		if (reg->umax_value < val)
			return 1;
		else if (reg->umin_value >= val)
			return 0;
		break;
	case BPF_JSLT:
		if (reg->smax_value < sval)
			return 1;
		else if (reg->smin_value >= sval)
			return 0;
		break;
	case BPF_JGE:
		if (reg->umin_value >= val)
			return 1;
		else if (reg->umax_value < val)
			return 0;
		break;
	case BPF_JSGE:
		if (reg->smin_value >= sval)
			return 1;
		else if (reg->smax_value < sval)
			return 0;
		break;
	case BPF_JLE:
		if (reg->umax_value <= val)
			return 1;
		else if (reg->umin_value > val)
			return 0;
		break;
	case BPF_JSLE:
		if (reg->smax_value <= sval)
			return 1;
		else if (reg->smin_value > sval)
			return 0;
		break;
	}

	return -1;
}

/* compute branch direction of the expression "if (reg opcode val) goto target;"
 * and return:
 *  1 - branch will be taken and "goto target" will be executed
 *  0 - branch will not be taken and fall-through to next insn
 * -1 - unknown. Example: "if (reg < 5)" is unknown when register value
 *      range [0,10]
 */
static int is_branch_taken(struct bpf_reg_state *reg, u64 val, u8 opcode,
			   bool is_jmp32)
{
	if (__is_pointer_value(false, reg)) {
		if (!reg_type_not_null(reg->type))
			return -1;

		/* If pointer is valid tests against zero will fail so we can
		 * use this to direct branch taken.
		 */
		if (val != 0)
			return -1;

		switch (opcode) {
		case BPF_JEQ:
			return 0;
		case BPF_JNE:
			return 1;
		default:
			return -1;
		}
	}

	if (is_jmp32)
		return is_branch32_taken(reg, val, opcode);
	return is_branch64_taken(reg, val, opcode);
}

static int flip_opcode(u32 opcode)
{
	/* How can we transform "a <op> b" into "b <op> a"? */
	static const u8 opcode_flip[16] = {
		/* these stay the same */
		[BPF_JEQ  >> 4] = BPF_JEQ,
		[BPF_JNE  >> 4] = BPF_JNE,
		[BPF_JSET >> 4] = BPF_JSET,
		/* these swap "lesser" and "greater" (L and G in the opcodes) */
		[BPF_JGE  >> 4] = BPF_JLE,
		[BPF_JGT  >> 4] = BPF_JLT,
		[BPF_JLE  >> 4] = BPF_JGE,
		[BPF_JLT  >> 4] = BPF_JGT,
		[BPF_JSGE >> 4] = BPF_JSLE,
		[BPF_JSGT >> 4] = BPF_JSLT,
		[BPF_JSLE >> 4] = BPF_JSGE,
		[BPF_JSLT >> 4] = BPF_JSGT
	};
	return opcode_flip[opcode >> 4];
}

static int is_pkt_ptr_branch_taken(struct bpf_reg_state *dst_reg,
				   struct bpf_reg_state *src_reg,
				   u8 opcode)
{
	struct bpf_reg_state *pkt;

	if (src_reg->type == PTR_TO_PACKET_END) {
		pkt = dst_reg;
	} else if (dst_reg->type == PTR_TO_PACKET_END) {
		pkt = src_reg;
		opcode = flip_opcode(opcode);
	} else {
		return -1;
	}

	if (pkt->range >= 0)
		return -1;

	switch (opcode) {
	case BPF_JLE:
		/* pkt <= pkt_end */
		fallthrough;
	case BPF_JGT:
		/* pkt > pkt_end */
		if (pkt->range == BEYOND_PKT_END)
			/* pkt has at last one extra byte beyond pkt_end */
			return opcode == BPF_JGT;
		break;
	case BPF_JLT:
		/* pkt < pkt_end */
		fallthrough;
	case BPF_JGE:
		/* pkt >= pkt_end */
		if (pkt->range == BEYOND_PKT_END || pkt->range == AT_PKT_END)
			return opcode == BPF_JGE;
		break;
	}
	return -1;
}

/* Adjusts the register min/max values in the case that the dst_reg is the
 * variable register that we are working on, and src_reg is a constant or we're
 * simply doing a BPF_K check.
 * In JEQ/JNE cases we also adjust the var_off values.
 */
static void reg_set_min_max(struct bpf_reg_state *true_reg,
			    struct bpf_reg_state *false_reg,
			    u64 val, u32 val32,
			    u8 opcode, bool is_jmp32)
{
	struct tnum false_32off = tnum_subreg(false_reg->var_off);
	struct tnum false_64off = false_reg->var_off;
	struct tnum true_32off = tnum_subreg(true_reg->var_off);
	struct tnum true_64off = true_reg->var_off;
	s64 sval = (s64)val;
	s32 sval32 = (s32)val32;

	/* If the dst_reg is a pointer, we can't learn anything about its
	 * variable offset from the compare (unless src_reg were a pointer into
	 * the same object, but we don't bother with that.
	 * Since false_reg and true_reg have the same type by construction, we
	 * only need to check one of them for pointerness.
	 */
	if (__is_pointer_value(false, false_reg))
		return;

	switch (opcode) {
	case BPF_JEQ:
	case BPF_JNE:
	{
		struct bpf_reg_state *reg =
			opcode == BPF_JEQ ? true_reg : false_reg;

		/* JEQ/JNE comparison doesn't change the register equivalence.
		 * r1 = r2;
		 * if (r1 == 42) goto label;
		 * ...
		 * label: // here both r1 and r2 are known to be 42.
		 *
		 * Hence when marking register as known preserve it's ID.
		 */
		if (is_jmp32)
			__mark_reg32_known(reg, val32);
		else
			___mark_reg_known(reg, val);
		break;
	}
	case BPF_JSET:
		if (is_jmp32) {
			false_32off = tnum_and(false_32off, tnum_const(~val32));
			if (is_power_of_2(val32))
				true_32off = tnum_or(true_32off,
						     tnum_const(val32));
		} else {
			false_64off = tnum_and(false_64off, tnum_const(~val));
			if (is_power_of_2(val))
				true_64off = tnum_or(true_64off,
						     tnum_const(val));
		}
		break;
	case BPF_JGE:
	case BPF_JGT:
	{
		if (is_jmp32) {
			u32 false_umax = opcode == BPF_JGT ? val32  : val32 - 1;
			u32 true_umin = opcode == BPF_JGT ? val32 + 1 : val32;

			false_reg->u32_max_value = min(false_reg->u32_max_value,
						       false_umax);
			true_reg->u32_min_value = max(true_reg->u32_min_value,
						      true_umin);
		} else {
			u64 false_umax = opcode == BPF_JGT ? val    : val - 1;
			u64 true_umin = opcode == BPF_JGT ? val + 1 : val;

			false_reg->umax_value = min(false_reg->umax_value, false_umax);
			true_reg->umin_value = max(true_reg->umin_value, true_umin);
		}
		break;
	}
	case BPF_JSGE:
	case BPF_JSGT:
	{
		if (is_jmp32) {
			s32 false_smax = opcode == BPF_JSGT ? sval32    : sval32 - 1;
			s32 true_smin = opcode == BPF_JSGT ? sval32 + 1 : sval32;

			false_reg->s32_max_value = min(false_reg->s32_max_value, false_smax);
			true_reg->s32_min_value = max(true_reg->s32_min_value, true_smin);
		} else {
			s64 false_smax = opcode == BPF_JSGT ? sval    : sval - 1;
			s64 true_smin = opcode == BPF_JSGT ? sval + 1 : sval;

			false_reg->smax_value = min(false_reg->smax_value, false_smax);
			true_reg->smin_value = max(true_reg->smin_value, true_smin);
		}
		break;
	}
	case BPF_JLE:
	case BPF_JLT:
	{
		if (is_jmp32) {
			u32 false_umin = opcode == BPF_JLT ? val32  : val32 + 1;
			u32 true_umax = opcode == BPF_JLT ? val32 - 1 : val32;

			false_reg->u32_min_value = max(false_reg->u32_min_value,
						       false_umin);
			true_reg->u32_max_value = min(true_reg->u32_max_value,
						      true_umax);
		} else {
			u64 false_umin = opcode == BPF_JLT ? val    : val + 1;
			u64 true_umax = opcode == BPF_JLT ? val - 1 : val;

			false_reg->umin_value = max(false_reg->umin_value, false_umin);
			true_reg->umax_value = min(true_reg->umax_value, true_umax);
		}
		break;
	}
	case BPF_JSLE:
	case BPF_JSLT:
	{
		if (is_jmp32) {
			s32 false_smin = opcode == BPF_JSLT ? sval32    : sval32 + 1;
			s32 true_smax = opcode == BPF_JSLT ? sval32 - 1 : sval32;

			false_reg->s32_min_value = max(false_reg->s32_min_value, false_smin);
			true_reg->s32_max_value = min(true_reg->s32_max_value, true_smax);
		} else {
			s64 false_smin = opcode == BPF_JSLT ? sval    : sval + 1;
			s64 true_smax = opcode == BPF_JSLT ? sval - 1 : sval;

			false_reg->smin_value = max(false_reg->smin_value, false_smin);
			true_reg->smax_value = min(true_reg->smax_value, true_smax);
		}
		break;
	}
	default:
		return;
	}

	if (is_jmp32) {
		false_reg->var_off = tnum_or(tnum_clear_subreg(false_64off),
					     tnum_subreg(false_32off));
		true_reg->var_off = tnum_or(tnum_clear_subreg(true_64off),
					    tnum_subreg(true_32off));
		__reg_combine_32_into_64(false_reg);
		__reg_combine_32_into_64(true_reg);
	} else {
		false_reg->var_off = false_64off;
		true_reg->var_off = true_64off;
		__reg_combine_64_into_32(false_reg);
		__reg_combine_64_into_32(true_reg);
	}
}

/* Same as above, but for the case that dst_reg holds a constant and src_reg is
 * the variable reg.
 */
static void reg_set_min_max_inv(struct bpf_reg_state *true_reg,
				struct bpf_reg_state *false_reg,
				u64 val, u32 val32,
				u8 opcode, bool is_jmp32)
{
	opcode = flip_opcode(opcode);
	/* This uses zero as "not present in table"; luckily the zero opcode,
	 * BPF_JA, can't get here.
	 */
	if (opcode)
		reg_set_min_max(true_reg, false_reg, val, val32, opcode, is_jmp32);
}

/* Regs are known to be equal, so intersect their min/max/var_off */
static void __reg_combine_min_max(struct bpf_reg_state *src_reg,
				  struct bpf_reg_state *dst_reg)
{
	src_reg->umin_value = dst_reg->umin_value = max(src_reg->umin_value,
							dst_reg->umin_value);
	src_reg->umax_value = dst_reg->umax_value = min(src_reg->umax_value,
							dst_reg->umax_value);
	src_reg->smin_value = dst_reg->smin_value = max(src_reg->smin_value,
							dst_reg->smin_value);
	src_reg->smax_value = dst_reg->smax_value = min(src_reg->smax_value,
							dst_reg->smax_value);
	src_reg->var_off = dst_reg->var_off = tnum_intersect(src_reg->var_off,
							     dst_reg->var_off);
	/* We might have learned new bounds from the var_off. */
	__update_reg_bounds(src_reg);
	__update_reg_bounds(dst_reg);
	/* We might have learned something about the sign bit. */
	__reg_deduce_bounds(src_reg);
	__reg_deduce_bounds(dst_reg);
	/* We might have learned some bits from the bounds. */
	__reg_bound_offset(src_reg);
	__reg_bound_offset(dst_reg);
	/* Intersecting with the old var_off might have improved our bounds
	 * slightly.  e.g. if umax was 0x7f...f and var_off was (0; 0xf...fc),
	 * then new var_off is (0; 0x7f...fc) which improves our umax.
	 */
	__update_reg_bounds(src_reg);
	__update_reg_bounds(dst_reg);
}

static void reg_combine_min_max(struct bpf_reg_state *true_src,
				struct bpf_reg_state *true_dst,
				struct bpf_reg_state *false_src,
				struct bpf_reg_state *false_dst,
				u8 opcode)
{
	switch (opcode) {
	case BPF_JEQ:
		__reg_combine_min_max(true_src, true_dst);
		break;
	case BPF_JNE:
		__reg_combine_min_max(false_src, false_dst);
		break;
	}
}

static void mark_ptr_or_null_reg(struct bpf_func_state *state,
				 struct bpf_reg_state *reg, u32 id,
				 bool is_null)
{
	if (reg_type_may_be_null(reg->type) && reg->id == id &&
	    !WARN_ON_ONCE(!reg->id)) {
		/* Old offset (both fixed and variable parts) should
		 * have been known-zero, because we don't allow pointer
		 * arithmetic on pointers that might be NULL.
		 */
		if (WARN_ON_ONCE(reg->smin_value || reg->smax_value ||
				 !tnum_equals_const(reg->var_off, 0) ||
				 reg->off)) {
			__mark_reg_known_zero(reg);
			reg->off = 0;
		}
		if (is_null) {
			reg->type = SCALAR_VALUE;
			/* We don't need id and ref_obj_id from this point
			 * onwards anymore, thus we should better reset it,
			 * so that state pruning has chances to take effect.
			 */
			reg->id = 0;
			reg->ref_obj_id = 0;

			return;
		}

		mark_ptr_not_null_reg(reg);

		if (!reg_may_point_to_spin_lock(reg)) {
			/* For not-NULL ptr, reg->ref_obj_id will be reset
			 * in release_reg_references().
			 *
			 * reg->id is still used by spin_lock ptr. Other
			 * than spin_lock ptr type, reg->id can be reset.
			 */
			reg->id = 0;
		}
	}
}

static void __mark_ptr_or_null_regs(struct bpf_func_state *state, u32 id,
				    bool is_null)
{
	struct bpf_reg_state *reg;
	int i;

	for (i = 0; i < MAX_BPF_REG; i++)
		mark_ptr_or_null_reg(state, &state->regs[i], id, is_null);

	bpf_for_each_spilled_reg(i, state, reg) {
		if (!reg)
			continue;
		mark_ptr_or_null_reg(state, reg, id, is_null);
	}
}

/* The logic is similar to find_good_pkt_pointers(), both could eventually
 * be folded together at some point.
 */
static void mark_ptr_or_null_regs(struct bpf_verifier_state *vstate, u32 regno,
				  bool is_null)
{
	struct bpf_func_state *state = vstate->frame[vstate->curframe];
	struct bpf_reg_state *regs = state->regs;
	u32 ref_obj_id = regs[regno].ref_obj_id;
	u32 id = regs[regno].id;
	int i;

	if (ref_obj_id && ref_obj_id == id && is_null)
		/* regs[regno] is in the " == NULL" branch.
		 * No one could have freed the reference state before
		 * doing the NULL check.
		 */
		WARN_ON_ONCE(release_reference_state(state, id));

	for (i = 0; i <= vstate->curframe; i++)
		__mark_ptr_or_null_regs(vstate->frame[i], id, is_null);
}

static bool try_match_pkt_pointers(const struct bpf_insn *insn,
				   struct bpf_reg_state *dst_reg,
				   struct bpf_reg_state *src_reg,
				   struct bpf_verifier_state *this_branch,
				   struct bpf_verifier_state *other_branch)
{
	if (BPF_SRC(insn->code) != BPF_X)
		return false;

	/* Pointers are always 64-bit. */
	if (BPF_CLASS(insn->code) == BPF_JMP32)
		return false;

	switch (BPF_OP(insn->code)) {
	case BPF_JGT:
		if ((dst_reg->type == PTR_TO_PACKET &&
		     src_reg->type == PTR_TO_PACKET_END) ||
		    (dst_reg->type == PTR_TO_PACKET_META &&
		     reg_is_init_pkt_pointer(src_reg, PTR_TO_PACKET))) {
			/* pkt_data' > pkt_end, pkt_meta' > pkt_data */
			find_good_pkt_pointers(this_branch, dst_reg,
					       dst_reg->type, false);
			mark_pkt_end(other_branch, insn->dst_reg, true);
		} else if ((dst_reg->type == PTR_TO_PACKET_END &&
			    src_reg->type == PTR_TO_PACKET) ||
			   (reg_is_init_pkt_pointer(dst_reg, PTR_TO_PACKET) &&
			    src_reg->type == PTR_TO_PACKET_META)) {
			/* pkt_end > pkt_data', pkt_data > pkt_meta' */
			find_good_pkt_pointers(other_branch, src_reg,
					       src_reg->type, true);
			mark_pkt_end(this_branch, insn->src_reg, false);
		} else {
			return false;
		}
		break;
	case BPF_JLT:
		if ((dst_reg->type == PTR_TO_PACKET &&
		     src_reg->type == PTR_TO_PACKET_END) ||
		    (dst_reg->type == PTR_TO_PACKET_META &&
		     reg_is_init_pkt_pointer(src_reg, PTR_TO_PACKET))) {
			/* pkt_data' < pkt_end, pkt_meta' < pkt_data */
			find_good_pkt_pointers(other_branch, dst_reg,
					       dst_reg->type, true);
			mark_pkt_end(this_branch, insn->dst_reg, false);
		} else if ((dst_reg->type == PTR_TO_PACKET_END &&
			    src_reg->type == PTR_TO_PACKET) ||
			   (reg_is_init_pkt_pointer(dst_reg, PTR_TO_PACKET) &&
			    src_reg->type == PTR_TO_PACKET_META)) {
			/* pkt_end < pkt_data', pkt_data > pkt_meta' */
			find_good_pkt_pointers(this_branch, src_reg,
					       src_reg->type, false);
			mark_pkt_end(other_branch, insn->src_reg, true);
		} else {
			return false;
		}
		break;
	case BPF_JGE:
		if ((dst_reg->type == PTR_TO_PACKET &&
		     src_reg->type == PTR_TO_PACKET_END) ||
		    (dst_reg->type == PTR_TO_PACKET_META &&
		     reg_is_init_pkt_pointer(src_reg, PTR_TO_PACKET))) {
			/* pkt_data' >= pkt_end, pkt_meta' >= pkt_data */
			find_good_pkt_pointers(this_branch, dst_reg,
					       dst_reg->type, true);
			mark_pkt_end(other_branch, insn->dst_reg, false);
		} else if ((dst_reg->type == PTR_TO_PACKET_END &&
			    src_reg->type == PTR_TO_PACKET) ||
			   (reg_is_init_pkt_pointer(dst_reg, PTR_TO_PACKET) &&
			    src_reg->type == PTR_TO_PACKET_META)) {
			/* pkt_end >= pkt_data', pkt_data >= pkt_meta' */
			find_good_pkt_pointers(other_branch, src_reg,
					       src_reg->type, false);
			mark_pkt_end(this_branch, insn->src_reg, true);
		} else {
			return false;
		}
		break;
	case BPF_JLE:
		if ((dst_reg->type == PTR_TO_PACKET &&
		     src_reg->type == PTR_TO_PACKET_END) ||
		    (dst_reg->type == PTR_TO_PACKET_META &&
		     reg_is_init_pkt_pointer(src_reg, PTR_TO_PACKET))) {
			/* pkt_data' <= pkt_end, pkt_meta' <= pkt_data */
			find_good_pkt_pointers(other_branch, dst_reg,
					       dst_reg->type, false);
			mark_pkt_end(this_branch, insn->dst_reg, true);
		} else if ((dst_reg->type == PTR_TO_PACKET_END &&
			    src_reg->type == PTR_TO_PACKET) ||
			   (reg_is_init_pkt_pointer(dst_reg, PTR_TO_PACKET) &&
			    src_reg->type == PTR_TO_PACKET_META)) {
			/* pkt_end <= pkt_data', pkt_data <= pkt_meta' */
			find_good_pkt_pointers(this_branch, src_reg,
					       src_reg->type, true);
			mark_pkt_end(other_branch, insn->src_reg, false);
		} else {
			return false;
		}
		break;
	default:
		return false;
	}

	return true;
}

static void find_equal_scalars(struct bpf_verifier_state *vstate,
			       struct bpf_reg_state *known_reg)
{
	struct bpf_func_state *state;
	struct bpf_reg_state *reg;
	int i, j;

	for (i = 0; i <= vstate->curframe; i++) {
		state = vstate->frame[i];
		for (j = 0; j < MAX_BPF_REG; j++) {
			reg = &state->regs[j];
			if (reg->type == SCALAR_VALUE && reg->id == known_reg->id)
				*reg = *known_reg;
		}

		bpf_for_each_spilled_reg(j, state, reg) {
			if (!reg)
				continue;
			if (reg->type == SCALAR_VALUE && reg->id == known_reg->id)
				*reg = *known_reg;
		}
	}
}

static int check_cond_jmp_op(struct bpf_verifier_env *env,
			     struct bpf_insn *insn, int *insn_idx)
{
	struct bpf_verifier_state *this_branch = env->cur_state;
	struct bpf_verifier_state *other_branch;
	struct bpf_reg_state *regs = this_branch->frame[this_branch->curframe]->regs;
	struct bpf_reg_state *dst_reg, *other_branch_regs, *src_reg = NULL;
	u8 opcode = BPF_OP(insn->code);
	bool is_jmp32;
	int pred = -1;
	int err;

	/* Only conditional jumps are expected to reach here. */
	if (opcode == BPF_JA || opcode > BPF_JSLE) {
		verbose(env, "invalid BPF_JMP/JMP32 opcode %x\n", opcode);
		return -EINVAL;
	}

	if (BPF_SRC(insn->code) == BPF_X) {
		if (insn->imm != 0) {
			verbose(env, "BPF_JMP/JMP32 uses reserved fields\n");
			return -EINVAL;
		}

		/* check src1 operand */
		err = check_reg_arg(env, insn->src_reg, SRC_OP);
		if (err)
			return err;

		if (is_pointer_value(env, insn->src_reg)) {
			verbose(env, "R%d pointer comparison prohibited\n",
				insn->src_reg);
			return -EACCES;
		}
		src_reg = &regs[insn->src_reg];
	} else {
		if (insn->src_reg != BPF_REG_0) {
			verbose(env, "BPF_JMP/JMP32 uses reserved fields\n");
			return -EINVAL;
		}
	}

	/* check src2 operand */
	err = check_reg_arg(env, insn->dst_reg, SRC_OP);
	if (err)
		return err;

	dst_reg = &regs[insn->dst_reg];
	is_jmp32 = BPF_CLASS(insn->code) == BPF_JMP32;

	if (BPF_SRC(insn->code) == BPF_K) {
		pred = is_branch_taken(dst_reg, insn->imm, opcode, is_jmp32);
	} else if (src_reg->type == SCALAR_VALUE &&
		   is_jmp32 && tnum_is_const(tnum_subreg(src_reg->var_off))) {
		pred = is_branch_taken(dst_reg,
				       tnum_subreg(src_reg->var_off).value,
				       opcode,
				       is_jmp32);
	} else if (src_reg->type == SCALAR_VALUE &&
		   !is_jmp32 && tnum_is_const(src_reg->var_off)) {
		pred = is_branch_taken(dst_reg,
				       src_reg->var_off.value,
				       opcode,
				       is_jmp32);
	} else if (reg_is_pkt_pointer_any(dst_reg) &&
		   reg_is_pkt_pointer_any(src_reg) &&
		   !is_jmp32) {
		pred = is_pkt_ptr_branch_taken(dst_reg, src_reg, opcode);
	}

	if (pred >= 0) {
		/* If we get here with a dst_reg pointer type it is because
		 * above is_branch_taken() special cased the 0 comparison.
		 */
		if (!__is_pointer_value(false, dst_reg))
			err = mark_chain_precision(env, insn->dst_reg);
		if (BPF_SRC(insn->code) == BPF_X && !err &&
		    !__is_pointer_value(false, src_reg))
			err = mark_chain_precision(env, insn->src_reg);
		if (err)
			return err;
	}
	if (pred == 1) {
		/* only follow the goto, ignore fall-through */
		*insn_idx += insn->off;
		return 0;
	} else if (pred == 0) {
		/* only follow fall-through branch, since
		 * that's where the program will go
		 */
		return 0;
	}

	other_branch = push_stack(env, *insn_idx + insn->off + 1, *insn_idx,
				  false);
	if (!other_branch)
		return -EFAULT;
	other_branch_regs = other_branch->frame[other_branch->curframe]->regs;

	/* detect if we are comparing against a constant value so we can adjust
	 * our min/max values for our dst register.
	 * this is only legit if both are scalars (or pointers to the same
	 * object, I suppose, but we don't support that right now), because
	 * otherwise the different base pointers mean the offsets aren't
	 * comparable.
	 */
	if (BPF_SRC(insn->code) == BPF_X) {
		struct bpf_reg_state *src_reg = &regs[insn->src_reg];

		if (dst_reg->type == SCALAR_VALUE &&
		    src_reg->type == SCALAR_VALUE) {
			if (tnum_is_const(src_reg->var_off) ||
			    (is_jmp32 &&
			     tnum_is_const(tnum_subreg(src_reg->var_off))))
				reg_set_min_max(&other_branch_regs[insn->dst_reg],
						dst_reg,
						src_reg->var_off.value,
						tnum_subreg(src_reg->var_off).value,
						opcode, is_jmp32);
			else if (tnum_is_const(dst_reg->var_off) ||
				 (is_jmp32 &&
				  tnum_is_const(tnum_subreg(dst_reg->var_off))))
				reg_set_min_max_inv(&other_branch_regs[insn->src_reg],
						    src_reg,
						    dst_reg->var_off.value,
						    tnum_subreg(dst_reg->var_off).value,
						    opcode, is_jmp32);
			else if (!is_jmp32 &&
				 (opcode == BPF_JEQ || opcode == BPF_JNE))
				/* Comparing for equality, we can combine knowledge */
				reg_combine_min_max(&other_branch_regs[insn->src_reg],
						    &other_branch_regs[insn->dst_reg],
						    src_reg, dst_reg, opcode);
			if (src_reg->id &&
			    !WARN_ON_ONCE(src_reg->id != other_branch_regs[insn->src_reg].id)) {
				find_equal_scalars(this_branch, src_reg);
				find_equal_scalars(other_branch, &other_branch_regs[insn->src_reg]);
			}

		}
	} else if (dst_reg->type == SCALAR_VALUE) {
		reg_set_min_max(&other_branch_regs[insn->dst_reg],
					dst_reg, insn->imm, (u32)insn->imm,
					opcode, is_jmp32);
	}

	if (dst_reg->type == SCALAR_VALUE && dst_reg->id &&
	    !WARN_ON_ONCE(dst_reg->id != other_branch_regs[insn->dst_reg].id)) {
		find_equal_scalars(this_branch, dst_reg);
		find_equal_scalars(other_branch, &other_branch_regs[insn->dst_reg]);
	}

	/* detect if R == 0 where R is returned from bpf_map_lookup_elem().
	 * NOTE: these optimizations below are related with pointer comparison
	 *       which will never be JMP32.
	 */
	if (!is_jmp32 && BPF_SRC(insn->code) == BPF_K &&
	    insn->imm == 0 && (opcode == BPF_JEQ || opcode == BPF_JNE) &&
	    reg_type_may_be_null(dst_reg->type)) {
		/* Mark all identical registers in each branch as either
		 * safe or unknown depending R == 0 or R != 0 conditional.
		 */
		mark_ptr_or_null_regs(this_branch, insn->dst_reg,
				      opcode == BPF_JNE);
		mark_ptr_or_null_regs(other_branch, insn->dst_reg,
				      opcode == BPF_JEQ);
	} else if (!try_match_pkt_pointers(insn, dst_reg, &regs[insn->src_reg],
					   this_branch, other_branch) &&
		   is_pointer_value(env, insn->dst_reg)) {
		verbose(env, "R%d pointer comparison prohibited\n",
			insn->dst_reg);
		return -EACCES;
	}
	if (env->log.level & BPF_LOG_LEVEL)
		print_verifier_state(env, this_branch->frame[this_branch->curframe]);
	return 0;
}

/* verify BPF_LD_IMM64 instruction */
static int check_ld_imm(struct bpf_verifier_env *env, struct bpf_insn *insn)
{
	struct bpf_insn_aux_data *aux = cur_aux(env);
	struct bpf_reg_state *regs = cur_regs(env);
	struct bpf_reg_state *dst_reg;
	struct bpf_map *map;
	int err;

	if (BPF_SIZE(insn->code) != BPF_DW) {
		verbose(env, "invalid BPF_LD_IMM insn\n");
		return -EINVAL;
	}
	if (insn->off != 0) {
		verbose(env, "BPF_LD_IMM64 uses reserved fields\n");
		return -EINVAL;
	}

	err = check_reg_arg(env, insn->dst_reg, DST_OP);
	if (err)
		return err;

	dst_reg = &regs[insn->dst_reg];
	if (insn->src_reg == 0) {
		u64 imm = ((u64)(insn + 1)->imm << 32) | (u32)insn->imm;

		dst_reg->type = SCALAR_VALUE;
		__mark_reg_known(&regs[insn->dst_reg], imm);
		return 0;
	}

	if (insn->src_reg == BPF_PSEUDO_BTF_ID) {
		mark_reg_known_zero(env, regs, insn->dst_reg);

		dst_reg->type = aux->btf_var.reg_type;
		switch (dst_reg->type) {
		case PTR_TO_MEM:
			dst_reg->mem_size = aux->btf_var.mem_size;
			break;
		case PTR_TO_BTF_ID:
		case PTR_TO_PERCPU_BTF_ID:
			dst_reg->btf = aux->btf_var.btf;
			dst_reg->btf_id = aux->btf_var.btf_id;
			break;
		default:
			verbose(env, "bpf verifier is misconfigured\n");
			return -EFAULT;
		}
		return 0;
	}

	map = env->used_maps[aux->map_index];
	mark_reg_known_zero(env, regs, insn->dst_reg);
	dst_reg->map_ptr = map;

	if (insn->src_reg == BPF_PSEUDO_MAP_VALUE) {
		dst_reg->type = PTR_TO_MAP_VALUE;
		dst_reg->off = aux->map_off;
		if (map_value_has_spin_lock(map))
			dst_reg->id = ++env->id_gen;
	} else if (insn->src_reg == BPF_PSEUDO_MAP_FD) {
		dst_reg->type = CONST_PTR_TO_MAP;
	} else {
		verbose(env, "bpf verifier is misconfigured\n");
		return -EINVAL;
	}

	return 0;
}

static bool may_access_skb(enum bpf_prog_type type)
{
	switch (type) {
	case BPF_PROG_TYPE_SOCKET_FILTER:
	case BPF_PROG_TYPE_SCHED_CLS:
	case BPF_PROG_TYPE_SCHED_ACT:
		return true;
	default:
		return false;
	}
}

/* verify safety of LD_ABS|LD_IND instructions:
 * - they can only appear in the programs where ctx == skb
 * - since they are wrappers of function calls, they scratch R1-R5 registers,
 *   preserve R6-R9, and store return value into R0
 *
 * Implicit input:
 *   ctx == skb == R6 == CTX
 *
 * Explicit input:
 *   SRC == any register
 *   IMM == 32-bit immediate
 *
 * Output:
 *   R0 - 8/16/32-bit skb data converted to cpu endianness
 */
static int check_ld_abs(struct bpf_verifier_env *env, struct bpf_insn *insn)
{
	struct bpf_reg_state *regs = cur_regs(env);
	static const int ctx_reg = BPF_REG_6;
	u8 mode = BPF_MODE(insn->code);
	int i, err;

	if (!may_access_skb(resolve_prog_type(env->prog))) {
		verbose(env, "BPF_LD_[ABS|IND] instructions not allowed for this program type\n");
		return -EINVAL;
	}

	if (!env->ops->gen_ld_abs) {
		verbose(env, "bpf verifier is misconfigured\n");
		return -EINVAL;
	}

	if (insn->dst_reg != BPF_REG_0 || insn->off != 0 ||
	    BPF_SIZE(insn->code) == BPF_DW ||
	    (mode == BPF_ABS && insn->src_reg != BPF_REG_0)) {
		verbose(env, "BPF_LD_[ABS|IND] uses reserved fields\n");
		return -EINVAL;
	}

	/* check whether implicit source operand (register R6) is readable */
	err = check_reg_arg(env, ctx_reg, SRC_OP);
	if (err)
		return err;

	/* Disallow usage of BPF_LD_[ABS|IND] with reference tracking, as
	 * gen_ld_abs() may terminate the program at runtime, leading to
	 * reference leak.
	 */
	err = check_reference_leak(env);
	if (err) {
		verbose(env, "BPF_LD_[ABS|IND] cannot be mixed with socket references\n");
		return err;
	}

	if (env->cur_state->active_spin_lock) {
		verbose(env, "BPF_LD_[ABS|IND] cannot be used inside bpf_spin_lock-ed region\n");
		return -EINVAL;
	}

	if (regs[ctx_reg].type != PTR_TO_CTX) {
		verbose(env,
			"at the time of BPF_LD_ABS|IND R6 != pointer to skb\n");
		return -EINVAL;
	}

	if (mode == BPF_IND) {
		/* check explicit source operand */
		err = check_reg_arg(env, insn->src_reg, SRC_OP);
		if (err)
			return err;
	}

	err = check_ctx_reg(env, &regs[ctx_reg], ctx_reg);
	if (err < 0)
		return err;

	/* reset caller saved regs to unreadable */
	for (i = 0; i < CALLER_SAVED_REGS; i++) {
		mark_reg_not_init(env, regs, caller_saved[i]);
		check_reg_arg(env, caller_saved[i], DST_OP_NO_MARK);
	}

	/* mark destination R0 register as readable, since it contains
	 * the value fetched from the packet.
	 * Already marked as written above.
	 */
	mark_reg_unknown(env, regs, BPF_REG_0);
	/* ld_abs load up to 32-bit skb data. */
	regs[BPF_REG_0].subreg_def = env->insn_idx + 1;
	return 0;
}

static int check_return_code(struct bpf_verifier_env *env)
{
	struct tnum enforce_attach_type_range = tnum_unknown;
	const struct bpf_prog *prog = env->prog;
	struct bpf_reg_state *reg;
	struct tnum range = tnum_range(0, 1);
	enum bpf_prog_type prog_type = resolve_prog_type(env->prog);
	int err;
	const bool is_subprog = env->cur_state->frame[0]->subprogno;

	/* LSM and struct_ops func-ptr's return type could be "void" */
	if (!is_subprog &&
	    (prog_type == BPF_PROG_TYPE_STRUCT_OPS ||
	     prog_type == BPF_PROG_TYPE_LSM) &&
	    !prog->aux->attach_func_proto->type)
		return 0;

	/* eBPF calling convetion is such that R0 is used
	 * to return the value from eBPF program.
	 * Make sure that it's readable at this time
	 * of bpf_exit, which means that program wrote
	 * something into it earlier
	 */
	err = check_reg_arg(env, BPF_REG_0, SRC_OP);
	if (err)
		return err;

	if (is_pointer_value(env, BPF_REG_0)) {
		verbose(env, "R0 leaks addr as return value\n");
		return -EACCES;
	}

	reg = cur_regs(env) + BPF_REG_0;
	if (is_subprog) {
		if (reg->type != SCALAR_VALUE) {
			verbose(env, "At subprogram exit the register R0 is not a scalar value (%s)\n",
				reg_type_str[reg->type]);
			return -EINVAL;
		}
		return 0;
	}

	switch (prog_type) {
	case BPF_PROG_TYPE_CGROUP_SOCK_ADDR:
		if (env->prog->expected_attach_type == BPF_CGROUP_UDP4_RECVMSG ||
		    env->prog->expected_attach_type == BPF_CGROUP_UDP6_RECVMSG ||
		    env->prog->expected_attach_type == BPF_CGROUP_INET4_GETPEERNAME ||
		    env->prog->expected_attach_type == BPF_CGROUP_INET6_GETPEERNAME ||
		    env->prog->expected_attach_type == BPF_CGROUP_INET4_GETSOCKNAME ||
		    env->prog->expected_attach_type == BPF_CGROUP_INET6_GETSOCKNAME)
			range = tnum_range(1, 1);
		if (env->prog->expected_attach_type == BPF_CGROUP_INET4_BIND ||
		    env->prog->expected_attach_type == BPF_CGROUP_INET6_BIND)
			range = tnum_range(0, 3);
		break;
	case BPF_PROG_TYPE_CGROUP_SKB:
		if (env->prog->expected_attach_type == BPF_CGROUP_INET_EGRESS) {
			range = tnum_range(0, 3);
			enforce_attach_type_range = tnum_range(2, 3);
		}
		break;
	case BPF_PROG_TYPE_CGROUP_SOCK:
	case BPF_PROG_TYPE_SOCK_OPS:
	case BPF_PROG_TYPE_CGROUP_DEVICE:
	case BPF_PROG_TYPE_CGROUP_SYSCTL:
	case BPF_PROG_TYPE_CGROUP_SOCKOPT:
		break;
	case BPF_PROG_TYPE_RAW_TRACEPOINT:
		if (!env->prog->aux->attach_btf_id)
			return 0;
		range = tnum_const(0);
		break;
	case BPF_PROG_TYPE_TRACING:
		switch (env->prog->expected_attach_type) {
		case BPF_TRACE_FENTRY:
		case BPF_TRACE_FEXIT:
			range = tnum_const(0);
			break;
		case BPF_TRACE_RAW_TP:
		case BPF_MODIFY_RETURN:
			return 0;
		case BPF_TRACE_ITER:
			break;
		default:
			return -ENOTSUPP;
		}
		break;
	case BPF_PROG_TYPE_SK_LOOKUP:
		range = tnum_range(SK_DROP, SK_PASS);
		break;
	case BPF_PROG_TYPE_EXT:
		/* freplace program can return anything as its return value
		 * depends on the to-be-replaced kernel func or bpf program.
		 */
	default:
		return 0;
	}

	if (reg->type != SCALAR_VALUE) {
		verbose(env, "At program exit the register R0 is not a known value (%s)\n",
			reg_type_str[reg->type]);
		return -EINVAL;
	}

	if (!tnum_in(range, reg->var_off)) {
		char tn_buf[48];

		verbose(env, "At program exit the register R0 ");
		if (!tnum_is_unknown(reg->var_off)) {
			tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
			verbose(env, "has value %s", tn_buf);
		} else {
			verbose(env, "has unknown scalar value");
		}
		tnum_strn(tn_buf, sizeof(tn_buf), range);
		verbose(env, " should have been in %s\n", tn_buf);
		return -EINVAL;
	}

	if (!tnum_is_unknown(enforce_attach_type_range) &&
	    tnum_in(enforce_attach_type_range, reg->var_off))
		env->prog->enforce_expected_attach_type = 1;
	return 0;
}

/* non-recursive DFS pseudo code
 * 1  procedure DFS-iterative(G,v):
 * 2      label v as discovered
 * 3      let S be a stack
 * 4      S.push(v)
 * 5      while S is not empty
 * 6            t <- S.pop()
 * 7            if t is what we're looking for:
 * 8                return t
 * 9            for all edges e in G.adjacentEdges(t) do
 * 10               if edge e is already labelled
 * 11                   continue with the next edge
 * 12               w <- G.adjacentVertex(t,e)
 * 13               if vertex w is not discovered and not explored
 * 14                   label e as tree-edge
 * 15                   label w as discovered
 * 16                   S.push(w)
 * 17                   continue at 5
 * 18               else if vertex w is discovered
 * 19                   label e as back-edge
 * 20               else
 * 21                   // vertex w is explored
 * 22                   label e as forward- or cross-edge
 * 23           label t as explored
 * 24           S.pop()
 *
 * convention:
 * 0x10 - discovered
 * 0x11 - discovered and fall-through edge labelled
 * 0x12 - discovered and fall-through and branch edges labelled
 * 0x20 - explored
 */

enum {
	DISCOVERED = 0x10,
	EXPLORED = 0x20,
	FALLTHROUGH = 1,
	BRANCH = 2,
};

static u32 state_htab_size(struct bpf_verifier_env *env)
{
	return env->prog->len;
}

static struct bpf_verifier_state_list **explored_state(
					struct bpf_verifier_env *env,
					int idx)
{
	struct bpf_verifier_state *cur = env->cur_state;
	struct bpf_func_state *state = cur->frame[cur->curframe];

	return &env->explored_states[(idx ^ state->callsite) % state_htab_size(env)];
}

static void init_explored_state(struct bpf_verifier_env *env, int idx)
{
	env->insn_aux_data[idx].prune_point = true;
}

enum {
	DONE_EXPLORING = 0,
	KEEP_EXPLORING = 1,
};

/* t, w, e - match pseudo-code above:
 * t - index of current instruction
 * w - next instruction
 * e - edge
 */
static int push_insn(int t, int w, int e, struct bpf_verifier_env *env,
		     bool loop_ok)
{
	int *insn_stack = env->cfg.insn_stack;
	int *insn_state = env->cfg.insn_state;

	if (e == FALLTHROUGH && insn_state[t] >= (DISCOVERED | FALLTHROUGH))
		return DONE_EXPLORING;

	if (e == BRANCH && insn_state[t] >= (DISCOVERED | BRANCH))
		return DONE_EXPLORING;

	if (w < 0 || w >= env->prog->len) {
		verbose_linfo(env, t, "%d: ", t);
		verbose(env, "jump out of range from insn %d to %d\n", t, w);
		return -EINVAL;
	}

	if (e == BRANCH)
		/* mark branch target for state pruning */
		init_explored_state(env, w);

	if (insn_state[w] == 0) {
		/* tree-edge */
		insn_state[t] = DISCOVERED | e;
		insn_state[w] = DISCOVERED;
		if (env->cfg.cur_stack >= env->prog->len)
			return -E2BIG;
		insn_stack[env->cfg.cur_stack++] = w;
		return KEEP_EXPLORING;
	} else if ((insn_state[w] & 0xF0) == DISCOVERED) {
		if (loop_ok && env->bpf_capable)
			return DONE_EXPLORING;
		verbose_linfo(env, t, "%d: ", t);
		verbose_linfo(env, w, "%d: ", w);
		verbose(env, "back-edge from insn %d to %d\n", t, w);
		return -EINVAL;
	} else if (insn_state[w] == EXPLORED) {
		/* forward- or cross-edge */
		insn_state[t] = DISCOVERED | e;
	} else {
		verbose(env, "insn state internal bug\n");
		return -EFAULT;
	}
	return DONE_EXPLORING;
}

/* Visits the instruction at index t and returns one of the following:
 *  < 0 - an error occurred
 *  DONE_EXPLORING - the instruction was fully explored
 *  KEEP_EXPLORING - there is still work to be done before it is fully explored
 */
static int visit_insn(int t, int insn_cnt, struct bpf_verifier_env *env)
{
	struct bpf_insn *insns = env->prog->insnsi;
	int ret;

	/* All non-branch instructions have a single fall-through edge. */
	if (BPF_CLASS(insns[t].code) != BPF_JMP &&
	    BPF_CLASS(insns[t].code) != BPF_JMP32)
		return push_insn(t, t + 1, FALLTHROUGH, env, false);

	switch (BPF_OP(insns[t].code)) {
	case BPF_EXIT:
		return DONE_EXPLORING;

	case BPF_CALL:
		ret = push_insn(t, t + 1, FALLTHROUGH, env, false);
		if (ret)
			return ret;

		if (t + 1 < insn_cnt)
			init_explored_state(env, t + 1);
		if (insns[t].src_reg == BPF_PSEUDO_CALL) {
			init_explored_state(env, t);
			ret = push_insn(t, t + insns[t].imm + 1, BRANCH,
					env, false);
		}
		return ret;

	case BPF_JA:
		if (BPF_SRC(insns[t].code) != BPF_K)
			return -EINVAL;

		/* unconditional jump with single edge */
		ret = push_insn(t, t + insns[t].off + 1, FALLTHROUGH, env,
				true);
		if (ret)
			return ret;

		/* unconditional jmp is not a good pruning point,
		 * but it's marked, since backtracking needs
		 * to record jmp history in is_state_visited().
		 */
		init_explored_state(env, t + insns[t].off + 1);
		/* tell verifier to check for equivalent states
		 * after every call and jump
		 */
		if (t + 1 < insn_cnt)
			init_explored_state(env, t + 1);

		return ret;

	default:
		/* conditional jump with two edges */
		init_explored_state(env, t);
		ret = push_insn(t, t + 1, FALLTHROUGH, env, true);
		if (ret)
			return ret;

		return push_insn(t, t + insns[t].off + 1, BRANCH, env, true);
	}
}

/* non-recursive depth-first-search to detect loops in BPF program
 * loop == back-edge in directed graph
 */
static int check_cfg(struct bpf_verifier_env *env)
{
	int insn_cnt = env->prog->len;
	int *insn_stack, *insn_state;
	int ret = 0;
	int i;

	insn_state = env->cfg.insn_state = kvcalloc(insn_cnt, sizeof(int), GFP_KERNEL);
	if (!insn_state)
		return -ENOMEM;

	insn_stack = env->cfg.insn_stack = kvcalloc(insn_cnt, sizeof(int), GFP_KERNEL);
	if (!insn_stack) {
		kvfree(insn_state);
		return -ENOMEM;
	}

	insn_state[0] = DISCOVERED; /* mark 1st insn as discovered */
	insn_stack[0] = 0; /* 0 is the first instruction */
	env->cfg.cur_stack = 1;

	while (env->cfg.cur_stack > 0) {
		int t = insn_stack[env->cfg.cur_stack - 1];

		ret = visit_insn(t, insn_cnt, env);
		switch (ret) {
		case DONE_EXPLORING:
			insn_state[t] = EXPLORED;
			env->cfg.cur_stack--;
			break;
		case KEEP_EXPLORING:
			break;
		default:
			if (ret > 0) {
				verbose(env, "visit_insn internal bug\n");
				ret = -EFAULT;
			}
			goto err_free;
		}
	}

	if (env->cfg.cur_stack < 0) {
		verbose(env, "pop stack internal bug\n");
		ret = -EFAULT;
		goto err_free;
	}

	for (i = 0; i < insn_cnt; i++) {
		if (insn_state[i] != EXPLORED) {
			verbose(env, "unreachable insn %d\n", i);
			ret = -EINVAL;
			goto err_free;
		}
	}
	ret = 0; /* cfg looks good */

err_free:
	kvfree(insn_state);
	kvfree(insn_stack);
	env->cfg.insn_state = env->cfg.insn_stack = NULL;
	return ret;
}

static int check_abnormal_return(struct bpf_verifier_env *env)
{
	int i;

	for (i = 1; i < env->subprog_cnt; i++) {
		if (env->subprog_info[i].has_ld_abs) {
			verbose(env, "LD_ABS is not allowed in subprogs without BTF\n");
			return -EINVAL;
		}
		if (env->subprog_info[i].has_tail_call) {
			verbose(env, "tail_call is not allowed in subprogs without BTF\n");
			return -EINVAL;
		}
	}
	return 0;
}

/* The minimum supported BTF func info size */
#define MIN_BPF_FUNCINFO_SIZE	8
#define MAX_FUNCINFO_REC_SIZE	252

static int check_btf_func(struct bpf_verifier_env *env,
			  const union bpf_attr *attr,
			  union bpf_attr __user *uattr)
{
	const struct btf_type *type, *func_proto, *ret_type;
	u32 i, nfuncs, urec_size, min_size;
	u32 krec_size = sizeof(struct bpf_func_info);
	struct bpf_func_info *krecord;
	struct bpf_func_info_aux *info_aux = NULL;
	struct bpf_prog *prog;
	const struct btf *btf;
	void __user *urecord;
	u32 prev_offset = 0;
	bool scalar_return;
	int ret = -ENOMEM;

	nfuncs = attr->func_info_cnt;
	if (!nfuncs) {
		if (check_abnormal_return(env))
			return -EINVAL;
		return 0;
	}

	if (nfuncs != env->subprog_cnt) {
		verbose(env, "number of funcs in func_info doesn't match number of subprogs\n");
		return -EINVAL;
	}

	urec_size = attr->func_info_rec_size;
	if (urec_size < MIN_BPF_FUNCINFO_SIZE ||
	    urec_size > MAX_FUNCINFO_REC_SIZE ||
	    urec_size % sizeof(u32)) {
		verbose(env, "invalid func info rec size %u\n", urec_size);
		return -EINVAL;
	}

	prog = env->prog;
	btf = prog->aux->btf;

	urecord = u64_to_user_ptr(attr->func_info);
	min_size = min_t(u32, krec_size, urec_size);

	krecord = kvcalloc(nfuncs, krec_size, GFP_KERNEL | __GFP_NOWARN);
	if (!krecord)
		return -ENOMEM;
	info_aux = kcalloc(nfuncs, sizeof(*info_aux), GFP_KERNEL | __GFP_NOWARN);
	if (!info_aux)
		goto err_free;

	for (i = 0; i < nfuncs; i++) {
		ret = bpf_check_uarg_tail_zero(urecord, krec_size, urec_size);
		if (ret) {
			if (ret == -E2BIG) {
				verbose(env, "nonzero tailing record in func info");
				/* set the size kernel expects so loader can zero
				 * out the rest of the record.
				 */
				if (put_user(min_size, &uattr->func_info_rec_size))
					ret = -EFAULT;
			}
			goto err_free;
		}

		if (copy_from_user(&krecord[i], urecord, min_size)) {
			ret = -EFAULT;
			goto err_free;
		}

		/* check insn_off */
		ret = -EINVAL;
		if (i == 0) {
			if (krecord[i].insn_off) {
				verbose(env,
					"nonzero insn_off %u for the first func info record",
					krecord[i].insn_off);
				goto err_free;
			}
		} else if (krecord[i].insn_off <= prev_offset) {
			verbose(env,
				"same or smaller insn offset (%u) than previous func info record (%u)",
				krecord[i].insn_off, prev_offset);
			goto err_free;
		}

		if (env->subprog_info[i].start != krecord[i].insn_off) {
			verbose(env, "func_info BTF section doesn't match subprog layout in BPF program\n");
			goto err_free;
		}

		/* check type_id */
		type = btf_type_by_id(btf, krecord[i].type_id);
		if (!type || !btf_type_is_func(type)) {
			verbose(env, "invalid type id %d in func info",
				krecord[i].type_id);
			goto err_free;
		}
		info_aux[i].linkage = BTF_INFO_VLEN(type->info);

		func_proto = btf_type_by_id(btf, type->type);
		if (unlikely(!func_proto || !btf_type_is_func_proto(func_proto)))
			/* btf_func_check() already verified it during BTF load */
			goto err_free;
		ret_type = btf_type_skip_modifiers(btf, func_proto->type, NULL);
		scalar_return =
			btf_type_is_small_int(ret_type) || btf_type_is_enum(ret_type);
		if (i && !scalar_return && env->subprog_info[i].has_ld_abs) {
			verbose(env, "LD_ABS is only allowed in functions that return 'int'.\n");
			goto err_free;
		}
		if (i && !scalar_return && env->subprog_info[i].has_tail_call) {
			verbose(env, "tail_call is only allowed in functions that return 'int'.\n");
			goto err_free;
		}

		prev_offset = krecord[i].insn_off;
		urecord += urec_size;
	}

	prog->aux->func_info = krecord;
	prog->aux->func_info_cnt = nfuncs;
	prog->aux->func_info_aux = info_aux;
	return 0;

err_free:
	kvfree(krecord);
	kfree(info_aux);
	return ret;
}

static void adjust_btf_func(struct bpf_verifier_env *env)
{
	struct bpf_prog_aux *aux = env->prog->aux;
	int i;

	if (!aux->func_info)
		return;

	for (i = 0; i < env->subprog_cnt; i++)
		aux->func_info[i].insn_off = env->subprog_info[i].start;
}

#define MIN_BPF_LINEINFO_SIZE	(offsetof(struct bpf_line_info, line_col) + \
		sizeof(((struct bpf_line_info *)(0))->line_col))
#define MAX_LINEINFO_REC_SIZE	MAX_FUNCINFO_REC_SIZE

static int check_btf_line(struct bpf_verifier_env *env,
			  const union bpf_attr *attr,
			  union bpf_attr __user *uattr)
{
	u32 i, s, nr_linfo, ncopy, expected_size, rec_size, prev_offset = 0;
	struct bpf_subprog_info *sub;
	struct bpf_line_info *linfo;
	struct bpf_prog *prog;
	const struct btf *btf;
	void __user *ulinfo;
	int err;

	nr_linfo = attr->line_info_cnt;
	if (!nr_linfo)
		return 0;

	rec_size = attr->line_info_rec_size;
	if (rec_size < MIN_BPF_LINEINFO_SIZE ||
	    rec_size > MAX_LINEINFO_REC_SIZE ||
	    rec_size & (sizeof(u32) - 1))
		return -EINVAL;

	/* Need to zero it in case the userspace may
	 * pass in a smaller bpf_line_info object.
	 */
	linfo = kvcalloc(nr_linfo, sizeof(struct bpf_line_info),
			 GFP_KERNEL | __GFP_NOWARN);
	if (!linfo)
		return -ENOMEM;

	prog = env->prog;
	btf = prog->aux->btf;

	s = 0;
	sub = env->subprog_info;
	ulinfo = u64_to_user_ptr(attr->line_info);
	expected_size = sizeof(struct bpf_line_info);
	ncopy = min_t(u32, expected_size, rec_size);
	for (i = 0; i < nr_linfo; i++) {
		err = bpf_check_uarg_tail_zero(ulinfo, expected_size, rec_size);
		if (err) {
			if (err == -E2BIG) {
				verbose(env, "nonzero tailing record in line_info");
				if (put_user(expected_size,
					     &uattr->line_info_rec_size))
					err = -EFAULT;
			}
			goto err_free;
		}

		if (copy_from_user(&linfo[i], ulinfo, ncopy)) {
			err = -EFAULT;
			goto err_free;
		}

		/*
		 * Check insn_off to ensure
		 * 1) strictly increasing AND
		 * 2) bounded by prog->len
		 *
		 * The linfo[0].insn_off == 0 check logically falls into
		 * the later "missing bpf_line_info for func..." case
		 * because the first linfo[0].insn_off must be the
		 * first sub also and the first sub must have
		 * subprog_info[0].start == 0.
		 */
		if ((i && linfo[i].insn_off <= prev_offset) ||
		    linfo[i].insn_off >= prog->len) {
			verbose(env, "Invalid line_info[%u].insn_off:%u (prev_offset:%u prog->len:%u)\n",
				i, linfo[i].insn_off, prev_offset,
				prog->len);
			err = -EINVAL;
			goto err_free;
		}

		if (!prog->insnsi[linfo[i].insn_off].code) {
			verbose(env,
				"Invalid insn code at line_info[%u].insn_off\n",
				i);
			err = -EINVAL;
			goto err_free;
		}

		if (!btf_name_by_offset(btf, linfo[i].line_off) ||
		    !btf_name_by_offset(btf, linfo[i].file_name_off)) {
			verbose(env, "Invalid line_info[%u].line_off or .file_name_off\n", i);
			err = -EINVAL;
			goto err_free;
		}

		if (s != env->subprog_cnt) {
			if (linfo[i].insn_off == sub[s].start) {
				sub[s].linfo_idx = i;
				s++;
			} else if (sub[s].start < linfo[i].insn_off) {
				verbose(env, "missing bpf_line_info for func#%u\n", s);
				err = -EINVAL;
				goto err_free;
			}
		}

		prev_offset = linfo[i].insn_off;
		ulinfo += rec_size;
	}

	if (s != env->subprog_cnt) {
		verbose(env, "missing bpf_line_info for %u funcs starting from func#%u\n",
			env->subprog_cnt - s, s);
		err = -EINVAL;
		goto err_free;
	}

	prog->aux->linfo = linfo;
	prog->aux->nr_linfo = nr_linfo;

	return 0;

err_free:
	kvfree(linfo);
	return err;
}

static int check_btf_info(struct bpf_verifier_env *env,
			  const union bpf_attr *attr,
			  union bpf_attr __user *uattr)
{
	struct btf *btf;
	int err;

	if (!attr->func_info_cnt && !attr->line_info_cnt) {
		if (check_abnormal_return(env))
			return -EINVAL;
		return 0;
	}

	btf = btf_get_by_fd(attr->prog_btf_fd);
	if (IS_ERR(btf))
		return PTR_ERR(btf);
	if (btf_is_kernel(btf)) {
		btf_put(btf);
		return -EACCES;
	}
	env->prog->aux->btf = btf;

	err = check_btf_func(env, attr, uattr);
	if (err)
		return err;

	err = check_btf_line(env, attr, uattr);
	if (err)
		return err;

	return 0;
}

/* check %cur's range satisfies %old's */
static bool range_within(struct bpf_reg_state *old,
			 struct bpf_reg_state *cur)
{
	return old->umin_value <= cur->umin_value &&
	       old->umax_value >= cur->umax_value &&
	       old->smin_value <= cur->smin_value &&
	       old->smax_value >= cur->smax_value &&
	       old->u32_min_value <= cur->u32_min_value &&
	       old->u32_max_value >= cur->u32_max_value &&
	       old->s32_min_value <= cur->s32_min_value &&
	       old->s32_max_value >= cur->s32_max_value;
}

/* Maximum number of register states that can exist at once */
#define ID_MAP_SIZE	(MAX_BPF_REG + MAX_BPF_STACK / BPF_REG_SIZE)
struct idpair {
	u32 old;
	u32 cur;
};

/* If in the old state two registers had the same id, then they need to have
 * the same id in the new state as well.  But that id could be different from
 * the old state, so we need to track the mapping from old to new ids.
 * Once we have seen that, say, a reg with old id 5 had new id 9, any subsequent
 * regs with old id 5 must also have new id 9 for the new state to be safe.  But
 * regs with a different old id could still have new id 9, we don't care about
 * that.
 * So we look through our idmap to see if this old id has been seen before.  If
 * so, we require the new id to match; otherwise, we add the id pair to the map.
 */
static bool check_ids(u32 old_id, u32 cur_id, struct idpair *idmap)
{
	unsigned int i;

	for (i = 0; i < ID_MAP_SIZE; i++) {
		if (!idmap[i].old) {
			/* Reached an empty slot; haven't seen this id before */
			idmap[i].old = old_id;
			idmap[i].cur = cur_id;
			return true;
		}
		if (idmap[i].old == old_id)
			return idmap[i].cur == cur_id;
	}
	/* We ran out of idmap slots, which should be impossible */
	WARN_ON_ONCE(1);
	return false;
}

static void clean_func_state(struct bpf_verifier_env *env,
			     struct bpf_func_state *st)
{
	enum bpf_reg_liveness live;
	int i, j;

	for (i = 0; i < BPF_REG_FP; i++) {
		live = st->regs[i].live;
		/* liveness must not touch this register anymore */
		st->regs[i].live |= REG_LIVE_DONE;
		if (!(live & REG_LIVE_READ))
			/* since the register is unused, clear its state
			 * to make further comparison simpler
			 */
			__mark_reg_not_init(env, &st->regs[i]);
	}

	for (i = 0; i < st->allocated_stack / BPF_REG_SIZE; i++) {
		live = st->stack[i].spilled_ptr.live;
		/* liveness must not touch this stack slot anymore */
		st->stack[i].spilled_ptr.live |= REG_LIVE_DONE;
		if (!(live & REG_LIVE_READ)) {
			__mark_reg_not_init(env, &st->stack[i].spilled_ptr);
			for (j = 0; j < BPF_REG_SIZE; j++)
				st->stack[i].slot_type[j] = STACK_INVALID;
		}
	}
}

static void clean_verifier_state(struct bpf_verifier_env *env,
				 struct bpf_verifier_state *st)
{
	int i;

	if (st->frame[0]->regs[0].live & REG_LIVE_DONE)
		/* all regs in this state in all frames were already marked */
		return;

	for (i = 0; i <= st->curframe; i++)
		clean_func_state(env, st->frame[i]);
}

/* the parentage chains form a tree.
 * the verifier states are added to state lists at given insn and
 * pushed into state stack for future exploration.
 * when the verifier reaches bpf_exit insn some of the verifer states
 * stored in the state lists have their final liveness state already,
 * but a lot of states will get revised from liveness point of view when
 * the verifier explores other branches.
 * Example:
 * 1: r0 = 1
 * 2: if r1 == 100 goto pc+1
 * 3: r0 = 2
 * 4: exit
 * when the verifier reaches exit insn the register r0 in the state list of
 * insn 2 will be seen as !REG_LIVE_READ. Then the verifier pops the other_branch
 * of insn 2 and goes exploring further. At the insn 4 it will walk the
 * parentage chain from insn 4 into insn 2 and will mark r0 as REG_LIVE_READ.
 *
 * Since the verifier pushes the branch states as it sees them while exploring
 * the program the condition of walking the branch instruction for the second
 * time means that all states below this branch were already explored and
 * their final liveness markes are already propagated.
 * Hence when the verifier completes the search of state list in is_state_visited()
 * we can call this clean_live_states() function to mark all liveness states
 * as REG_LIVE_DONE to indicate that 'parent' pointers of 'struct bpf_reg_state'
 * will not be used.
 * This function also clears the registers and stack for states that !READ
 * to simplify state merging.
 *
 * Important note here that walking the same branch instruction in the callee
 * doesn't meant that the states are DONE. The verifier has to compare
 * the callsites
 */
static void clean_live_states(struct bpf_verifier_env *env, int insn,
			      struct bpf_verifier_state *cur)
{
	struct bpf_verifier_state_list *sl;
	int i;

	sl = *explored_state(env, insn);
	while (sl) {
		if (sl->state.branches)
			goto next;
		if (sl->state.insn_idx != insn ||
		    sl->state.curframe != cur->curframe)
			goto next;
		for (i = 0; i <= cur->curframe; i++)
			if (sl->state.frame[i]->callsite != cur->frame[i]->callsite)
				goto next;
		clean_verifier_state(env, &sl->state);
next:
		sl = sl->next;
	}
}

/* Returns true if (rold safe implies rcur safe) */
static bool regsafe(struct bpf_reg_state *rold, struct bpf_reg_state *rcur,
		    struct idpair *idmap)
{
	bool equal;

	if (!(rold->live & REG_LIVE_READ))
		/* explored state didn't use this */
		return true;

	equal = memcmp(rold, rcur, offsetof(struct bpf_reg_state, parent)) == 0;

	if (rold->type == PTR_TO_STACK)
		/* two stack pointers are equal only if they're pointing to
		 * the same stack frame, since fp-8 in foo != fp-8 in bar
		 */
		return equal && rold->frameno == rcur->frameno;

	if (equal)
		return true;

	if (rold->type == NOT_INIT)
		/* explored state can't have used this */
		return true;
	if (rcur->type == NOT_INIT)
		return false;
	switch (rold->type) {
	case SCALAR_VALUE:
		if (rcur->type == SCALAR_VALUE) {
			if (!rold->precise && !rcur->precise)
				return true;
			/* new val must satisfy old val knowledge */
			return range_within(rold, rcur) &&
			       tnum_in(rold->var_off, rcur->var_off);
		} else {
			/* We're trying to use a pointer in place of a scalar.
			 * Even if the scalar was unbounded, this could lead to
			 * pointer leaks because scalars are allowed to leak
			 * while pointers are not. We could make this safe in
			 * special cases if root is calling us, but it's
			 * probably not worth the hassle.
			 */
			return false;
		}
	case PTR_TO_MAP_VALUE:
		/* If the new min/max/var_off satisfy the old ones and
		 * everything else matches, we are OK.
		 * 'id' is not compared, since it's only used for maps with
		 * bpf_spin_lock inside map element and in such cases if
		 * the rest of the prog is valid for one map element then
		 * it's valid for all map elements regardless of the key
		 * used in bpf_map_lookup()
		 */
		return memcmp(rold, rcur, offsetof(struct bpf_reg_state, id)) == 0 &&
		       range_within(rold, rcur) &&
		       tnum_in(rold->var_off, rcur->var_off);
	case PTR_TO_MAP_VALUE_OR_NULL:
		/* a PTR_TO_MAP_VALUE could be safe to use as a
		 * PTR_TO_MAP_VALUE_OR_NULL into the same map.
		 * However, if the old PTR_TO_MAP_VALUE_OR_NULL then got NULL-
		 * checked, doing so could have affected others with the same
		 * id, and we can't check for that because we lost the id when
		 * we converted to a PTR_TO_MAP_VALUE.
		 */
		if (rcur->type != PTR_TO_MAP_VALUE_OR_NULL)
			return false;
		if (memcmp(rold, rcur, offsetof(struct bpf_reg_state, id)))
			return false;
		/* Check our ids match any regs they're supposed to */
		return check_ids(rold->id, rcur->id, idmap);
	case PTR_TO_PACKET_META:
	case PTR_TO_PACKET:
		if (rcur->type != rold->type)
			return false;
		/* We must have at least as much range as the old ptr
		 * did, so that any accesses which were safe before are
		 * still safe.  This is true even if old range < old off,
		 * since someone could have accessed through (ptr - k), or
		 * even done ptr -= k in a register, to get a safe access.
		 */
		if (rold->range > rcur->range)
			return false;
		/* If the offsets don't match, we can't trust our alignment;
		 * nor can we be sure that we won't fall out of range.
		 */
		if (rold->off != rcur->off)
			return false;
		/* id relations must be preserved */
		if (rold->id && !check_ids(rold->id, rcur->id, idmap))
			return false;
		/* new val must satisfy old val knowledge */
		return range_within(rold, rcur) &&
		       tnum_in(rold->var_off, rcur->var_off);
	case PTR_TO_CTX:
	case CONST_PTR_TO_MAP:
	case PTR_TO_PACKET_END:
	case PTR_TO_FLOW_KEYS:
	case PTR_TO_SOCKET:
	case PTR_TO_SOCKET_OR_NULL:
	case PTR_TO_SOCK_COMMON:
	case PTR_TO_SOCK_COMMON_OR_NULL:
	case PTR_TO_TCP_SOCK:
	case PTR_TO_TCP_SOCK_OR_NULL:
	case PTR_TO_XDP_SOCK:
		/* Only valid matches are exact, which memcmp() above
		 * would have accepted
		 */
	default:
		/* Don't know what's going on, just say it's not safe */
		return false;
	}

	/* Shouldn't get here; if we do, say it's not safe */
	WARN_ON_ONCE(1);
	return false;
}

static bool stacksafe(struct bpf_func_state *old,
		      struct bpf_func_state *cur,
		      struct idpair *idmap)
{
	int i, spi;

	/* walk slots of the explored stack and ignore any additional
	 * slots in the current stack, since explored(safe) state
	 * didn't use them
	 */
	for (i = 0; i < old->allocated_stack; i++) {
		spi = i / BPF_REG_SIZE;

		if (!(old->stack[spi].spilled_ptr.live & REG_LIVE_READ)) {
			i += BPF_REG_SIZE - 1;
			/* explored state didn't use this */
			continue;
		}

		if (old->stack[spi].slot_type[i % BPF_REG_SIZE] == STACK_INVALID)
			continue;

		/* explored stack has more populated slots than current stack
		 * and these slots were used
		 */
		if (i >= cur->allocated_stack)
			return false;

		/* if old state was safe with misc data in the stack
		 * it will be safe with zero-initialized stack.
		 * The opposite is not true
		 */
		if (old->stack[spi].slot_type[i % BPF_REG_SIZE] == STACK_MISC &&
		    cur->stack[spi].slot_type[i % BPF_REG_SIZE] == STACK_ZERO)
			continue;
		if (old->stack[spi].slot_type[i % BPF_REG_SIZE] !=
		    cur->stack[spi].slot_type[i % BPF_REG_SIZE])
			/* Ex: old explored (safe) state has STACK_SPILL in
			 * this stack slot, but current has STACK_MISC ->
			 * this verifier states are not equivalent,
			 * return false to continue verification of this path
			 */
			return false;
		if (i % BPF_REG_SIZE)
			continue;
		if (old->stack[spi].slot_type[0] != STACK_SPILL)
			continue;
		if (!regsafe(&old->stack[spi].spilled_ptr,
			     &cur->stack[spi].spilled_ptr,
			     idmap))
			/* when explored and current stack slot are both storing
			 * spilled registers, check that stored pointers types
			 * are the same as well.
			 * Ex: explored safe path could have stored
			 * (bpf_reg_state) {.type = PTR_TO_STACK, .off = -8}
			 * but current path has stored:
			 * (bpf_reg_state) {.type = PTR_TO_STACK, .off = -16}
			 * such verifier states are not equivalent.
			 * return false to continue verification of this path
			 */
			return false;
	}
	return true;
}

static bool refsafe(struct bpf_func_state *old, struct bpf_func_state *cur)
{
	if (old->acquired_refs != cur->acquired_refs)
		return false;
	return !memcmp(old->refs, cur->refs,
		       sizeof(*old->refs) * old->acquired_refs);
}

/* compare two verifier states
 *
 * all states stored in state_list are known to be valid, since
 * verifier reached 'bpf_exit' instruction through them
 *
 * this function is called when verifier exploring different branches of
 * execution popped from the state stack. If it sees an old state that has
 * more strict register state and more strict stack state then this execution
 * branch doesn't need to be explored further, since verifier already
 * concluded that more strict state leads to valid finish.
 *
 * Therefore two states are equivalent if register state is more conservative
 * and explored stack state is more conservative than the current one.
 * Example:
 *       explored                   current
 * (slot1=INV slot2=MISC) == (slot1=MISC slot2=MISC)
 * (slot1=MISC slot2=MISC) != (slot1=INV slot2=MISC)
 *
 * In other words if current stack state (one being explored) has more
 * valid slots than old one that already passed validation, it means
 * the verifier can stop exploring and conclude that current state is valid too
 *
 * Similarly with registers. If explored state has register type as invalid
 * whereas register type in current state is meaningful, it means that
 * the current state will reach 'bpf_exit' instruction safely
 */
static bool func_states_equal(struct bpf_func_state *old,
			      struct bpf_func_state *cur)
{
	struct idpair *idmap;
	bool ret = false;
	int i;

	idmap = kcalloc(ID_MAP_SIZE, sizeof(struct idpair), GFP_KERNEL);
	/* If we failed to allocate the idmap, just say it's not safe */
	if (!idmap)
		return false;

	for (i = 0; i < MAX_BPF_REG; i++) {
		if (!regsafe(&old->regs[i], &cur->regs[i], idmap))
			goto out_free;
	}

	if (!stacksafe(old, cur, idmap))
		goto out_free;

	if (!refsafe(old, cur))
		goto out_free;
	ret = true;
out_free:
	kfree(idmap);
	return ret;
}

static bool states_equal(struct bpf_verifier_env *env,
			 struct bpf_verifier_state *old,
			 struct bpf_verifier_state *cur)
{
	int i;

	if (old->curframe != cur->curframe)
		return false;

	/* Verification state from speculative execution simulation
	 * must never prune a non-speculative execution one.
	 */
	if (old->speculative && !cur->speculative)
		return false;

	if (old->active_spin_lock != cur->active_spin_lock)
		return false;

	/* for states to be equal callsites have to be the same
	 * and all frame states need to be equivalent
	 */
	for (i = 0; i <= old->curframe; i++) {
		if (old->frame[i]->callsite != cur->frame[i]->callsite)
			return false;
		if (!func_states_equal(old->frame[i], cur->frame[i]))
			return false;
	}
	return true;
}

/* Return 0 if no propagation happened. Return negative error code if error
 * happened. Otherwise, return the propagated bit.
 */
static int propagate_liveness_reg(struct bpf_verifier_env *env,
				  struct bpf_reg_state *reg,
				  struct bpf_reg_state *parent_reg)
{
	u8 parent_flag = parent_reg->live & REG_LIVE_READ;
	u8 flag = reg->live & REG_LIVE_READ;
	int err;

	/* When comes here, read flags of PARENT_REG or REG could be any of
	 * REG_LIVE_READ64, REG_LIVE_READ32, REG_LIVE_NONE. There is no need
	 * of propagation if PARENT_REG has strongest REG_LIVE_READ64.
	 */
	if (parent_flag == REG_LIVE_READ64 ||
	    /* Or if there is no read flag from REG. */
	    !flag ||
	    /* Or if the read flag from REG is the same as PARENT_REG. */
	    parent_flag == flag)
		return 0;

	err = mark_reg_read(env, reg, parent_reg, flag);
	if (err)
		return err;

	return flag;
}

/* A write screens off any subsequent reads; but write marks come from the
 * straight-line code between a state and its parent.  When we arrive at an
 * equivalent state (jump target or such) we didn't arrive by the straight-line
 * code, so read marks in the state must propagate to the parent regardless
 * of the state's write marks. That's what 'parent == state->parent' comparison
 * in mark_reg_read() is for.
 */
static int propagate_liveness(struct bpf_verifier_env *env,
			      const struct bpf_verifier_state *vstate,
			      struct bpf_verifier_state *vparent)
{
	struct bpf_reg_state *state_reg, *parent_reg;
	struct bpf_func_state *state, *parent;
	int i, frame, err = 0;

	if (vparent->curframe != vstate->curframe) {
		WARN(1, "propagate_live: parent frame %d current frame %d\n",
		     vparent->curframe, vstate->curframe);
		return -EFAULT;
	}
	/* Propagate read liveness of registers... */
	BUILD_BUG_ON(BPF_REG_FP + 1 != MAX_BPF_REG);
	for (frame = 0; frame <= vstate->curframe; frame++) {
		parent = vparent->frame[frame];
		state = vstate->frame[frame];
		parent_reg = parent->regs;
		state_reg = state->regs;
		/* We don't need to worry about FP liveness, it's read-only */
		for (i = frame < vstate->curframe ? BPF_REG_6 : 0; i < BPF_REG_FP; i++) {
			err = propagate_liveness_reg(env, &state_reg[i],
						     &parent_reg[i]);
			if (err < 0)
				return err;
			if (err == REG_LIVE_READ64)
				mark_insn_zext(env, &parent_reg[i]);
		}

		/* Propagate stack slots. */
		for (i = 0; i < state->allocated_stack / BPF_REG_SIZE &&
			    i < parent->allocated_stack / BPF_REG_SIZE; i++) {
			parent_reg = &parent->stack[i].spilled_ptr;
			state_reg = &state->stack[i].spilled_ptr;
			err = propagate_liveness_reg(env, state_reg,
						     parent_reg);
			if (err < 0)
				return err;
		}
	}
	return 0;
}

/* find precise scalars in the previous equivalent state and
 * propagate them into the current state
 */
static int propagate_precision(struct bpf_verifier_env *env,
			       const struct bpf_verifier_state *old)
{
	struct bpf_reg_state *state_reg;
	struct bpf_func_state *state;
	int i, err = 0;

	state = old->frame[old->curframe];
	state_reg = state->regs;
	for (i = 0; i < BPF_REG_FP; i++, state_reg++) {
		if (state_reg->type != SCALAR_VALUE ||
		    !state_reg->precise)
			continue;
		if (env->log.level & BPF_LOG_LEVEL2)
			verbose(env, "propagating r%d\n", i);
		err = mark_chain_precision(env, i);
		if (err < 0)
			return err;
	}

	for (i = 0; i < state->allocated_stack / BPF_REG_SIZE; i++) {
		if (state->stack[i].slot_type[0] != STACK_SPILL)
			continue;
		state_reg = &state->stack[i].spilled_ptr;
		if (state_reg->type != SCALAR_VALUE ||
		    !state_reg->precise)
			continue;
		if (env->log.level & BPF_LOG_LEVEL2)
			verbose(env, "propagating fp%d\n",
				(-i - 1) * BPF_REG_SIZE);
		err = mark_chain_precision_stack(env, i);
		if (err < 0)
			return err;
	}
	return 0;
}

static bool states_maybe_looping(struct bpf_verifier_state *old,
				 struct bpf_verifier_state *cur)
{
	struct bpf_func_state *fold, *fcur;
	int i, fr = cur->curframe;

	if (old->curframe != fr)
		return false;

	fold = old->frame[fr];
	fcur = cur->frame[fr];
	for (i = 0; i < MAX_BPF_REG; i++)
		if (memcmp(&fold->regs[i], &fcur->regs[i],
			   offsetof(struct bpf_reg_state, parent)))
			return false;
	return true;
}


static int is_state_visited(struct bpf_verifier_env *env, int insn_idx)
{
	struct bpf_verifier_state_list *new_sl;
	struct bpf_verifier_state_list *sl, **pprev;
	struct bpf_verifier_state *cur = env->cur_state, *new;
	int i, j, err, states_cnt = 0;
	bool add_new_state = env->test_state_freq ? true : false;

	cur->last_insn_idx = env->prev_insn_idx;
	if (!env->insn_aux_data[insn_idx].prune_point)
		/* this 'insn_idx' instruction wasn't marked, so we will not
		 * be doing state search here
		 */
		return 0;

	/* bpf progs typically have pruning point every 4 instructions
	 * http://vger.kernel.org/bpfconf2019.html#session-1
	 * Do not add new state for future pruning if the verifier hasn't seen
	 * at least 2 jumps and at least 8 instructions.
	 * This heuristics helps decrease 'total_states' and 'peak_states' metric.
	 * In tests that amounts to up to 50% reduction into total verifier
	 * memory consumption and 20% verifier time speedup.
	 */
	if (env->jmps_processed - env->prev_jmps_processed >= 2 &&
	    env->insn_processed - env->prev_insn_processed >= 8)
		add_new_state = true;

	pprev = explored_state(env, insn_idx);
	sl = *pprev;

	clean_live_states(env, insn_idx, cur);

	while (sl) {
		states_cnt++;
		if (sl->state.insn_idx != insn_idx)
			goto next;
		if (sl->state.branches) {
			if (states_maybe_looping(&sl->state, cur) &&
			    states_equal(env, &sl->state, cur)) {
				verbose_linfo(env, insn_idx, "; ");
				verbose(env, "infinite loop detected at insn %d\n", insn_idx);
				return -EINVAL;
			}
			/* if the verifier is processing a loop, avoid adding new state
			 * too often, since different loop iterations have distinct
			 * states and may not help future pruning.
			 * This threshold shouldn't be too low to make sure that
			 * a loop with large bound will be rejected quickly.
			 * The most abusive loop will be:
			 * r1 += 1
			 * if r1 < 1000000 goto pc-2
			 * 1M insn_procssed limit / 100 == 10k peak states.
			 * This threshold shouldn't be too high either, since states
			 * at the end of the loop are likely to be useful in pruning.
			 */
			if (env->jmps_processed - env->prev_jmps_processed < 20 &&
			    env->insn_processed - env->prev_insn_processed < 100)
				add_new_state = false;
			goto miss;
		}
		if (states_equal(env, &sl->state, cur)) {
			sl->hit_cnt++;
			/* reached equivalent register/stack state,
			 * prune the search.
			 * Registers read by the continuation are read by us.
			 * If we have any write marks in env->cur_state, they
			 * will prevent corresponding reads in the continuation
			 * from reaching our parent (an explored_state).  Our
			 * own state will get the read marks recorded, but
			 * they'll be immediately forgotten as we're pruning
			 * this state and will pop a new one.
			 */
			err = propagate_liveness(env, &sl->state, cur);

			/* if previous state reached the exit with precision and
			 * current state is equivalent to it (except precsion marks)
			 * the precision needs to be propagated back in
			 * the current state.
			 */
			err = err ? : push_jmp_history(env, cur);
			err = err ? : propagate_precision(env, &sl->state);
			if (err)
				return err;
			return 1;
		}
miss:
		/* when new state is not going to be added do not increase miss count.
		 * Otherwise several loop iterations will remove the state
		 * recorded earlier. The goal of these heuristics is to have
		 * states from some iterations of the loop (some in the beginning
		 * and some at the end) to help pruning.
		 */
		if (add_new_state)
			sl->miss_cnt++;
		/* heuristic to determine whether this state is beneficial
		 * to keep checking from state equivalence point of view.
		 * Higher numbers increase max_states_per_insn and verification time,
		 * but do not meaningfully decrease insn_processed.
		 */
		if (sl->miss_cnt > sl->hit_cnt * 3 + 3) {
			/* the state is unlikely to be useful. Remove it to
			 * speed up verification
			 */
			*pprev = sl->next;
			if (sl->state.frame[0]->regs[0].live & REG_LIVE_DONE) {
				u32 br = sl->state.branches;

				WARN_ONCE(br,
					  "BUG live_done but branches_to_explore %d\n",
					  br);
				free_verifier_state(&sl->state, false);
				kfree(sl);
				env->peak_states--;
			} else {
				/* cannot free this state, since parentage chain may
				 * walk it later. Add it for free_list instead to
				 * be freed at the end of verification
				 */
				sl->next = env->free_list;
				env->free_list = sl;
			}
			sl = *pprev;
			continue;
		}
next:
		pprev = &sl->next;
		sl = *pprev;
	}

	if (env->max_states_per_insn < states_cnt)
		env->max_states_per_insn = states_cnt;

	if (!env->bpf_capable && states_cnt > BPF_COMPLEXITY_LIMIT_STATES)
		return push_jmp_history(env, cur);

	if (!add_new_state)
		return push_jmp_history(env, cur);

	/* There were no equivalent states, remember the current one.
	 * Technically the current state is not proven to be safe yet,
	 * but it will either reach outer most bpf_exit (which means it's safe)
	 * or it will be rejected. When there are no loops the verifier won't be
	 * seeing this tuple (frame[0].callsite, frame[1].callsite, .. insn_idx)
	 * again on the way to bpf_exit.
	 * When looping the sl->state.branches will be > 0 and this state
	 * will not be considered for equivalence until branches == 0.
	 */
	new_sl = kzalloc(sizeof(struct bpf_verifier_state_list), GFP_KERNEL);
	if (!new_sl)
		return -ENOMEM;
	env->total_states++;
	env->peak_states++;
	env->prev_jmps_processed = env->jmps_processed;
	env->prev_insn_processed = env->insn_processed;

	/* add new state to the head of linked list */
	new = &new_sl->state;
	err = copy_verifier_state(new, cur);
	if (err) {
		free_verifier_state(new, false);
		kfree(new_sl);
		return err;
	}
	new->insn_idx = insn_idx;
	WARN_ONCE(new->branches != 1,
		  "BUG is_state_visited:branches_to_explore=%d insn %d\n", new->branches, insn_idx);

	cur->parent = new;
	cur->first_insn_idx = insn_idx;
	clear_jmp_history(cur);
	new_sl->next = *explored_state(env, insn_idx);
	*explored_state(env, insn_idx) = new_sl;
	/* connect new state to parentage chain. Current frame needs all
	 * registers connected. Only r6 - r9 of the callers are alive (pushed
	 * to the stack implicitly by JITs) so in callers' frames connect just
	 * r6 - r9 as an optimization. Callers will have r1 - r5 connected to
	 * the state of the call instruction (with WRITTEN set), and r0 comes
	 * from callee with its full parentage chain, anyway.
	 */
	/* clear write marks in current state: the writes we did are not writes
	 * our child did, so they don't screen off its reads from us.
	 * (There are no read marks in current state, because reads always mark
	 * their parent and current state never has children yet.  Only
	 * explored_states can get read marks.)
	 */
	for (j = 0; j <= cur->curframe; j++) {
		for (i = j < cur->curframe ? BPF_REG_6 : 0; i < BPF_REG_FP; i++)
			cur->frame[j]->regs[i].parent = &new->frame[j]->regs[i];
		for (i = 0; i < BPF_REG_FP; i++)
			cur->frame[j]->regs[i].live = REG_LIVE_NONE;
	}

	/* all stack frames are accessible from callee, clear them all */
	for (j = 0; j <= cur->curframe; j++) {
		struct bpf_func_state *frame = cur->frame[j];
		struct bpf_func_state *newframe = new->frame[j];

		for (i = 0; i < frame->allocated_stack / BPF_REG_SIZE; i++) {
			frame->stack[i].spilled_ptr.live = REG_LIVE_NONE;
			frame->stack[i].spilled_ptr.parent =
						&newframe->stack[i].spilled_ptr;
		}
	}
	return 0;
}

/* Return true if it's OK to have the same insn return a different type. */
static bool reg_type_mismatch_ok(enum bpf_reg_type type)
{
	switch (type) {
	case PTR_TO_CTX:
	case PTR_TO_SOCKET:
	case PTR_TO_SOCKET_OR_NULL:
	case PTR_TO_SOCK_COMMON:
	case PTR_TO_SOCK_COMMON_OR_NULL:
	case PTR_TO_TCP_SOCK:
	case PTR_TO_TCP_SOCK_OR_NULL:
	case PTR_TO_XDP_SOCK:
	case PTR_TO_BTF_ID:
	case PTR_TO_BTF_ID_OR_NULL:
		return false;
	default:
		return true;
	}
}

/* If an instruction was previously used with particular pointer types, then we
 * need to be careful to avoid cases such as the below, where it may be ok
 * for one branch accessing the pointer, but not ok for the other branch:
 *
 * R1 = sock_ptr
 * goto X;
 * ...
 * R1 = some_other_valid_ptr;
 * goto X;
 * ...
 * R2 = *(u32 *)(R1 + 0);
 */
static bool reg_type_mismatch(enum bpf_reg_type src, enum bpf_reg_type prev)
{
	return src != prev && (!reg_type_mismatch_ok(src) ||
			       !reg_type_mismatch_ok(prev));
}

static int do_check(struct bpf_verifier_env *env)
{
	bool pop_log = !(env->log.level & BPF_LOG_LEVEL2);
	struct bpf_verifier_state *state = env->cur_state;
	struct bpf_insn *insns = env->prog->insnsi;
	struct bpf_reg_state *regs;
	int insn_cnt = env->prog->len;
	bool do_print_state = false;
	int prev_insn_idx = -1;

	for (;;) {
		struct bpf_insn *insn;
		u8 class;
		int err;

		env->prev_insn_idx = prev_insn_idx;
		if (env->insn_idx >= insn_cnt) {
			verbose(env, "invalid insn idx %d insn_cnt %d\n",
				env->insn_idx, insn_cnt);
			return -EFAULT;
		}

		insn = &insns[env->insn_idx];
		class = BPF_CLASS(insn->code);

		if (++env->insn_processed > BPF_COMPLEXITY_LIMIT_INSNS) {
			verbose(env,
				"BPF program is too large. Processed %d insn\n",
				env->insn_processed);
			return -E2BIG;
		}

		err = is_state_visited(env, env->insn_idx);
		if (err < 0)
			return err;
		if (err == 1) {
			/* found equivalent state, can prune the search */
			if (env->log.level & BPF_LOG_LEVEL) {
				if (do_print_state)
					verbose(env, "\nfrom %d to %d%s: safe\n",
						env->prev_insn_idx, env->insn_idx,
						env->cur_state->speculative ?
						" (speculative execution)" : "");
				else
					verbose(env, "%d: safe\n", env->insn_idx);
			}
			goto process_bpf_exit;
		}

		if (signal_pending(current))
			return -EAGAIN;

		if (need_resched())
			cond_resched();

		if (env->log.level & BPF_LOG_LEVEL2 ||
		    (env->log.level & BPF_LOG_LEVEL && do_print_state)) {
			if (env->log.level & BPF_LOG_LEVEL2)
				verbose(env, "%d:", env->insn_idx);
			else
				verbose(env, "\nfrom %d to %d%s:",
					env->prev_insn_idx, env->insn_idx,
					env->cur_state->speculative ?
					" (speculative execution)" : "");
			print_verifier_state(env, state->frame[state->curframe]);
			do_print_state = false;
		}

		if (env->log.level & BPF_LOG_LEVEL) {
			const struct bpf_insn_cbs cbs = {
				.cb_print	= verbose,
				.private_data	= env,
			};

			verbose_linfo(env, env->insn_idx, "; ");
			verbose(env, "%d: ", env->insn_idx);
			print_bpf_insn(&cbs, insn, env->allow_ptr_leaks);
		}

		if (bpf_prog_is_dev_bound(env->prog->aux)) {
			err = bpf_prog_offload_verify_insn(env, env->insn_idx,
							   env->prev_insn_idx);
			if (err)
				return err;
		}

		regs = cur_regs(env);
		env->insn_aux_data[env->insn_idx].seen = env->pass_cnt;
		prev_insn_idx = env->insn_idx;

		if (class == BPF_ALU || class == BPF_ALU64) {
			err = check_alu_op(env, insn);
			if (err)
				return err;

		} else if (class == BPF_LDX) {
			enum bpf_reg_type *prev_src_type, src_reg_type;

			/* check for reserved fields is already done */

			/* check src operand */
			err = check_reg_arg(env, insn->src_reg, SRC_OP);
			if (err)
				return err;

			err = check_reg_arg(env, insn->dst_reg, DST_OP_NO_MARK);
			if (err)
				return err;

			src_reg_type = regs[insn->src_reg].type;

			/* check that memory (src_reg + off) is readable,
			 * the state of dst_reg will be updated by this func
			 */
			err = check_mem_access(env, env->insn_idx, insn->src_reg,
					       insn->off, BPF_SIZE(insn->code),
					       BPF_READ, insn->dst_reg, false);
			if (err)
				return err;

			prev_src_type = &env->insn_aux_data[env->insn_idx].ptr_type;

			if (*prev_src_type == NOT_INIT) {
				/* saw a valid insn
				 * dst_reg = *(u32 *)(src_reg + off)
				 * save type to validate intersecting paths
				 */
				*prev_src_type = src_reg_type;

			} else if (reg_type_mismatch(src_reg_type, *prev_src_type)) {
				/* ABuser program is trying to use the same insn
				 * dst_reg = *(u32*) (src_reg + off)
				 * with different pointer types:
				 * src_reg == ctx in one branch and
				 * src_reg == stack|map in some other branch.
				 * Reject it.
				 */
				verbose(env, "same insn cannot be used with different pointers\n");
				return -EINVAL;
			}

		} else if (class == BPF_STX) {
			enum bpf_reg_type *prev_dst_type, dst_reg_type;

			if (BPF_MODE(insn->code) == BPF_ATOMIC) {
				err = check_atomic(env, env->insn_idx, insn);
				if (err)
					return err;
				env->insn_idx++;
				continue;
			}

			if (BPF_MODE(insn->code) != BPF_MEM || insn->imm != 0) {
				verbose(env, "BPF_STX uses reserved fields\n");
				return -EINVAL;
			}

			/* check src1 operand */
			err = check_reg_arg(env, insn->src_reg, SRC_OP);
			if (err)
				return err;
			/* check src2 operand */
			err = check_reg_arg(env, insn->dst_reg, SRC_OP);
			if (err)
				return err;

			dst_reg_type = regs[insn->dst_reg].type;

			/* check that memory (dst_reg + off) is writeable */
			err = check_mem_access(env, env->insn_idx, insn->dst_reg,
					       insn->off, BPF_SIZE(insn->code),
					       BPF_WRITE, insn->src_reg, false);
			if (err)
				return err;

			prev_dst_type = &env->insn_aux_data[env->insn_idx].ptr_type;

			if (*prev_dst_type == NOT_INIT) {
				*prev_dst_type = dst_reg_type;
			} else if (reg_type_mismatch(dst_reg_type, *prev_dst_type)) {
				verbose(env, "same insn cannot be used with different pointers\n");
				return -EINVAL;
			}

		} else if (class == BPF_ST) {
			if (BPF_MODE(insn->code) != BPF_MEM ||
			    insn->src_reg != BPF_REG_0) {
				verbose(env, "BPF_ST uses reserved fields\n");
				return -EINVAL;
			}
			/* check src operand */
			err = check_reg_arg(env, insn->dst_reg, SRC_OP);
			if (err)
				return err;

			if (is_ctx_reg(env, insn->dst_reg)) {
				verbose(env, "BPF_ST stores into R%d %s is not allowed\n",
					insn->dst_reg,
					reg_type_str[reg_state(env, insn->dst_reg)->type]);
				return -EACCES;
			}

			/* check that memory (dst_reg + off) is writeable */
			err = check_mem_access(env, env->insn_idx, insn->dst_reg,
					       insn->off, BPF_SIZE(insn->code),
					       BPF_WRITE, -1, false);
			if (err)
				return err;

		} else if (class == BPF_JMP || class == BPF_JMP32) {
			u8 opcode = BPF_OP(insn->code);

			env->jmps_processed++;
			if (opcode == BPF_CALL) {
				if (BPF_SRC(insn->code) != BPF_K ||
				    insn->off != 0 ||
				    (insn->src_reg != BPF_REG_0 &&
				     insn->src_reg != BPF_PSEUDO_CALL) ||
				    insn->dst_reg != BPF_REG_0 ||
				    class == BPF_JMP32) {
					verbose(env, "BPF_CALL uses reserved fields\n");
					return -EINVAL;
				}

				if (env->cur_state->active_spin_lock &&
				    (insn->src_reg == BPF_PSEUDO_CALL ||
				     insn->imm != BPF_FUNC_spin_unlock)) {
					verbose(env, "function calls are not allowed while holding a lock\n");
					return -EINVAL;
				}
				if (insn->src_reg == BPF_PSEUDO_CALL)
					err = check_func_call(env, insn, &env->insn_idx);
				else
					err = check_helper_call(env, insn->imm, env->insn_idx);
				if (err)
					return err;

			} else if (opcode == BPF_JA) {
				if (BPF_SRC(insn->code) != BPF_K ||
				    insn->imm != 0 ||
				    insn->src_reg != BPF_REG_0 ||
				    insn->dst_reg != BPF_REG_0 ||
				    class == BPF_JMP32) {
					verbose(env, "BPF_JA uses reserved fields\n");
					return -EINVAL;
				}

				env->insn_idx += insn->off + 1;
				continue;

			} else if (opcode == BPF_EXIT) {
				if (BPF_SRC(insn->code) != BPF_K ||
				    insn->imm != 0 ||
				    insn->src_reg != BPF_REG_0 ||
				    insn->dst_reg != BPF_REG_0 ||
				    class == BPF_JMP32) {
					verbose(env, "BPF_EXIT uses reserved fields\n");
					return -EINVAL;
				}

				if (env->cur_state->active_spin_lock) {
					verbose(env, "bpf_spin_unlock is missing\n");
					return -EINVAL;
				}

				if (state->curframe) {
					/* exit from nested function */
					err = prepare_func_exit(env, &env->insn_idx);
					if (err)
						return err;
					do_print_state = true;
					continue;
				}

				err = check_reference_leak(env);
				if (err)
					return err;

				err = check_return_code(env);
				if (err)
					return err;
process_bpf_exit:
				update_branch_counts(env, env->cur_state);
				err = pop_stack(env, &prev_insn_idx,
						&env->insn_idx, pop_log);
				if (err < 0) {
					if (err != -ENOENT)
						return err;
					break;
				} else {
					do_print_state = true;
					continue;
				}
			} else {
				err = check_cond_jmp_op(env, insn, &env->insn_idx);
				if (err)
					return err;
			}
		} else if (class == BPF_LD) {
			u8 mode = BPF_MODE(insn->code);

			if (mode == BPF_ABS || mode == BPF_IND) {
				err = check_ld_abs(env, insn);
				if (err)
					return err;

			} else if (mode == BPF_IMM) {
				err = check_ld_imm(env, insn);
				if (err)
					return err;

				env->insn_idx++;
				env->insn_aux_data[env->insn_idx].seen = env->pass_cnt;
			} else {
				verbose(env, "invalid BPF_LD mode\n");
				return -EINVAL;
			}
		} else {
			verbose(env, "unknown insn class %d\n", class);
			return -EINVAL;
		}

		env->insn_idx++;
	}

	return 0;
}

static int find_btf_percpu_datasec(struct btf *btf)
{
	const struct btf_type *t;
	const char *tname;
	int i, n;

	/*
	 * Both vmlinux and module each have their own ".data..percpu"
	 * DATASECs in BTF. So for module's case, we need to skip vmlinux BTF
	 * types to look at only module's own BTF types.
	 */
	n = btf_nr_types(btf);
	if (btf_is_module(btf))
		i = btf_nr_types(btf_vmlinux);
	else
		i = 1;

	for(; i < n; i++) {
		t = btf_type_by_id(btf, i);
		if (BTF_INFO_KIND(t->info) != BTF_KIND_DATASEC)
			continue;

		tname = btf_name_by_offset(btf, t->name_off);
		if (!strcmp(tname, ".data..percpu"))
			return i;
	}

	return -ENOENT;
}

/* replace pseudo btf_id with kernel symbol address */
static int check_pseudo_btf_id(struct bpf_verifier_env *env,
			       struct bpf_insn *insn,
			       struct bpf_insn_aux_data *aux)
{
	const struct btf_var_secinfo *vsi;
	const struct btf_type *datasec;
	struct btf_mod_pair *btf_mod;
	const struct btf_type *t;
	const char *sym_name;
	bool percpu = false;
	u32 type, id = insn->imm;
	struct btf *btf;
	s32 datasec_id;
	u64 addr;
	int i, btf_fd, err;

	btf_fd = insn[1].imm;
	if (btf_fd) {
		btf = btf_get_by_fd(btf_fd);
		if (IS_ERR(btf)) {
			verbose(env, "invalid module BTF object FD specified.\n");
			return -EINVAL;
		}
	} else {
		if (!btf_vmlinux) {
			verbose(env, "kernel is missing BTF, make sure CONFIG_DEBUG_INFO_BTF=y is specified in Kconfig.\n");
			return -EINVAL;
		}
		btf = btf_vmlinux;
		btf_get(btf);
	}

	t = btf_type_by_id(btf, id);
	if (!t) {
		verbose(env, "ldimm64 insn specifies invalid btf_id %d.\n", id);
		err = -ENOENT;
		goto err_put;
	}

	if (!btf_type_is_var(t)) {
		verbose(env, "pseudo btf_id %d in ldimm64 isn't KIND_VAR.\n", id);
		err = -EINVAL;
		goto err_put;
	}

	sym_name = btf_name_by_offset(btf, t->name_off);
	addr = kallsyms_lookup_name(sym_name);
	if (!addr) {
		verbose(env, "ldimm64 failed to find the address for kernel symbol '%s'.\n",
			sym_name);
		err = -ENOENT;
		goto err_put;
	}

	datasec_id = find_btf_percpu_datasec(btf);
	if (datasec_id > 0) {
		datasec = btf_type_by_id(btf, datasec_id);
		for_each_vsi(i, datasec, vsi) {
			if (vsi->type == id) {
				percpu = true;
				break;
			}
		}
	}

	insn[0].imm = (u32)addr;
	insn[1].imm = addr >> 32;

	type = t->type;
	t = btf_type_skip_modifiers(btf, type, NULL);
	if (percpu) {
		aux->btf_var.reg_type = PTR_TO_PERCPU_BTF_ID;
		aux->btf_var.btf = btf;
		aux->btf_var.btf_id = type;
	} else if (!btf_type_is_struct(t)) {
		const struct btf_type *ret;
		const char *tname;
		u32 tsize;

		/* resolve the type size of ksym. */
		ret = btf_resolve_size(btf, t, &tsize);
		if (IS_ERR(ret)) {
			tname = btf_name_by_offset(btf, t->name_off);
			verbose(env, "ldimm64 unable to resolve the size of type '%s': %ld\n",
				tname, PTR_ERR(ret));
			err = -EINVAL;
			goto err_put;
		}
		aux->btf_var.reg_type = PTR_TO_MEM;
		aux->btf_var.mem_size = tsize;
	} else {
		aux->btf_var.reg_type = PTR_TO_BTF_ID;
		aux->btf_var.btf = btf;
		aux->btf_var.btf_id = type;
	}

	/* check whether we recorded this BTF (and maybe module) already */
	for (i = 0; i < env->used_btf_cnt; i++) {
		if (env->used_btfs[i].btf == btf) {
			btf_put(btf);
			return 0;
		}
	}

	if (env->used_btf_cnt >= MAX_USED_BTFS) {
		err = -E2BIG;
		goto err_put;
	}

	btf_mod = &env->used_btfs[env->used_btf_cnt];
	btf_mod->btf = btf;
	btf_mod->module = NULL;

	/* if we reference variables from kernel module, bump its refcount */
	if (btf_is_module(btf)) {
		btf_mod->module = btf_try_get_module(btf);
		if (!btf_mod->module) {
			err = -ENXIO;
			goto err_put;
		}
	}

	env->used_btf_cnt++;

	return 0;
err_put:
	btf_put(btf);
	return err;
}

static int check_map_prealloc(struct bpf_map *map)
{
	return (map->map_type != BPF_MAP_TYPE_HASH &&
		map->map_type != BPF_MAP_TYPE_PERCPU_HASH &&
		map->map_type != BPF_MAP_TYPE_HASH_OF_MAPS) ||
		!(map->map_flags & BPF_F_NO_PREALLOC);
}

static bool is_tracing_prog_type(enum bpf_prog_type type)
{
	switch (type) {
	case BPF_PROG_TYPE_KPROBE:
	case BPF_PROG_TYPE_TRACEPOINT:
	case BPF_PROG_TYPE_PERF_EVENT:
	case BPF_PROG_TYPE_RAW_TRACEPOINT:
		return true;
	default:
		return false;
	}
}

static bool is_preallocated_map(struct bpf_map *map)
{
	if (!check_map_prealloc(map))
		return false;
	if (map->inner_map_meta && !check_map_prealloc(map->inner_map_meta))
		return false;
	return true;
}

static int check_map_prog_compatibility(struct bpf_verifier_env *env,
					struct bpf_map *map,
					struct bpf_prog *prog)

{
	enum bpf_prog_type prog_type = resolve_prog_type(prog);
	/*
	 * Validate that trace type programs use preallocated hash maps.
	 *
	 * For programs attached to PERF events this is mandatory as the
	 * perf NMI can hit any arbitrary code sequence.
	 *
	 * All other trace types using preallocated hash maps are unsafe as
	 * well because tracepoint or kprobes can be inside locked regions
	 * of the memory allocator or at a place where a recursion into the
	 * memory allocator would see inconsistent state.
	 *
	 * On RT enabled kernels run-time allocation of all trace type
	 * programs is strictly prohibited due to lock type constraints. On
	 * !RT kernels it is allowed for backwards compatibility reasons for
	 * now, but warnings are emitted so developers are made aware of
	 * the unsafety and can fix their programs before this is enforced.
	 */
	if (is_tracing_prog_type(prog_type) && !is_preallocated_map(map)) {
		if (prog_type == BPF_PROG_TYPE_PERF_EVENT) {
			verbose(env, "perf_event programs can only use preallocated hash map\n");
			return -EINVAL;
		}
		if (IS_ENABLED(CONFIG_PREEMPT_RT)) {
			verbose(env, "trace type programs can only use preallocated hash map\n");
			return -EINVAL;
		}
		WARN_ONCE(1, "trace type BPF program uses run-time allocation\n");
		verbose(env, "trace type programs with run-time allocated hash maps are unsafe. Switch to preallocated hash maps.\n");
	}

	if (map_value_has_spin_lock(map)) {
		if (prog_type == BPF_PROG_TYPE_SOCKET_FILTER) {
			verbose(env, "socket filter progs cannot use bpf_spin_lock yet\n");
			return -EINVAL;
		}

		if (is_tracing_prog_type(prog_type)) {
			verbose(env, "tracing progs cannot use bpf_spin_lock yet\n");
			return -EINVAL;
		}

		if (prog->aux->sleepable) {
			verbose(env, "sleepable progs cannot use bpf_spin_lock yet\n");
			return -EINVAL;
		}
	}

	if ((bpf_prog_is_dev_bound(prog->aux) || bpf_map_is_dev_bound(map)) &&
	    !bpf_offload_prog_map_match(prog, map)) {
		verbose(env, "offload device mismatch between prog and map\n");
		return -EINVAL;
	}

	if (map->map_type == BPF_MAP_TYPE_STRUCT_OPS) {
		verbose(env, "bpf_struct_ops map cannot be used in prog\n");
		return -EINVAL;
	}

	if (prog->aux->sleepable)
		switch (map->map_type) {
		case BPF_MAP_TYPE_HASH:
		case BPF_MAP_TYPE_LRU_HASH:
		case BPF_MAP_TYPE_ARRAY:
		case BPF_MAP_TYPE_PERCPU_HASH:
		case BPF_MAP_TYPE_PERCPU_ARRAY:
		case BPF_MAP_TYPE_LRU_PERCPU_HASH:
		case BPF_MAP_TYPE_ARRAY_OF_MAPS:
		case BPF_MAP_TYPE_HASH_OF_MAPS:
			if (!is_preallocated_map(map)) {
				verbose(env,
					"Sleepable programs can only use preallocated maps\n");
				return -EINVAL;
			}
			break;
		case BPF_MAP_TYPE_RINGBUF:
			break;
		default:
			verbose(env,
				"Sleepable programs can only use array, hash, and ringbuf maps\n");
			return -EINVAL;
		}

	return 0;
}

static bool bpf_map_is_cgroup_storage(struct bpf_map *map)
{
	return (map->map_type == BPF_MAP_TYPE_CGROUP_STORAGE ||
		map->map_type == BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE);
}

/* find and rewrite pseudo imm in ld_imm64 instructions:
 *
 * 1. if it accesses map FD, replace it with actual map pointer.
 * 2. if it accesses btf_id of a VAR, replace it with pointer to the var.
 *
 * NOTE: btf_vmlinux is required for converting pseudo btf_id.
 */
static int resolve_pseudo_ldimm64(struct bpf_verifier_env *env)
{
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	int i, j, err;

	err = bpf_prog_calc_tag(env->prog);
	if (err)
		return err;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (BPF_CLASS(insn->code) == BPF_LDX &&
		    (BPF_MODE(insn->code) != BPF_MEM || insn->imm != 0)) {
			verbose(env, "BPF_LDX uses reserved fields\n");
			return -EINVAL;
		}

		if (insn[0].code == (BPF_LD | BPF_IMM | BPF_DW)) {
			struct bpf_insn_aux_data *aux;
			struct bpf_map *map;
			struct fd f;
			u64 addr;

			if (i == insn_cnt - 1 || insn[1].code != 0 ||
			    insn[1].dst_reg != 0 || insn[1].src_reg != 0 ||
			    insn[1].off != 0) {
				verbose(env, "invalid bpf_ld_imm64 insn\n");
				return -EINVAL;
			}

			if (insn[0].src_reg == 0)
				/* valid generic load 64-bit imm */
				goto next_insn;

			if (insn[0].src_reg == BPF_PSEUDO_BTF_ID) {
				aux = &env->insn_aux_data[i];
				err = check_pseudo_btf_id(env, insn, aux);
				if (err)
					return err;
				goto next_insn;
			}

			/* In final convert_pseudo_ld_imm64() step, this is
			 * converted into regular 64-bit imm load insn.
			 */
			if ((insn[0].src_reg != BPF_PSEUDO_MAP_FD &&
			     insn[0].src_reg != BPF_PSEUDO_MAP_VALUE) ||
			    (insn[0].src_reg == BPF_PSEUDO_MAP_FD &&
			     insn[1].imm != 0)) {
				verbose(env,
					"unrecognized bpf_ld_imm64 insn\n");
				return -EINVAL;
			}

			f = fdget(insn[0].imm);
			map = __bpf_map_get(f);
			if (IS_ERR(map)) {
				verbose(env, "fd %d is not pointing to valid bpf_map\n",
					insn[0].imm);
				return PTR_ERR(map);
			}

			err = check_map_prog_compatibility(env, map, env->prog);
			if (err) {
				fdput(f);
				return err;
			}

			aux = &env->insn_aux_data[i];
			if (insn->src_reg == BPF_PSEUDO_MAP_FD) {
				addr = (unsigned long)map;
			} else {
				u32 off = insn[1].imm;

				if (off >= BPF_MAX_VAR_OFF) {
					verbose(env, "direct value offset of %u is not allowed\n", off);
					fdput(f);
					return -EINVAL;
				}

				if (!map->ops->map_direct_value_addr) {
					verbose(env, "no direct value access support for this map type\n");
					fdput(f);
					return -EINVAL;
				}

				err = map->ops->map_direct_value_addr(map, &addr, off);
				if (err) {
					verbose(env, "invalid access to map value pointer, value_size=%u off=%u\n",
						map->value_size, off);
					fdput(f);
					return err;
				}

				aux->map_off = off;
				addr += off;
			}

			insn[0].imm = (u32)addr;
			insn[1].imm = addr >> 32;

			/* check whether we recorded this map already */
			for (j = 0; j < env->used_map_cnt; j++) {
				if (env->used_maps[j] == map) {
					aux->map_index = j;
					fdput(f);
					goto next_insn;
				}
			}

			if (env->used_map_cnt >= MAX_USED_MAPS) {
				fdput(f);
				return -E2BIG;
			}

			/* hold the map. If the program is rejected by verifier,
			 * the map will be released by release_maps() or it
			 * will be used by the valid program until it's unloaded
			 * and all maps are released in free_used_maps()
			 */
			bpf_map_inc(map);

			aux->map_index = env->used_map_cnt;
			env->used_maps[env->used_map_cnt++] = map;

			if (bpf_map_is_cgroup_storage(map) &&
			    bpf_cgroup_storage_assign(env->prog->aux, map)) {
				verbose(env, "only one cgroup storage of each type is allowed\n");
				fdput(f);
				return -EBUSY;
			}

			fdput(f);
next_insn:
			insn++;
			i++;
			continue;
		}

		/* Basic sanity check before we invest more work here. */
		if (!bpf_opcode_in_insntable(insn->code)) {
			verbose(env, "unknown opcode %02x\n", insn->code);
			return -EINVAL;
		}
	}

	/* now all pseudo BPF_LD_IMM64 instructions load valid
	 * 'struct bpf_map *' into a register instead of user map_fd.
	 * These pointers will be used later by verifier to validate map access.
	 */
	return 0;
}

/* drop refcnt of maps used by the rejected program */
static void release_maps(struct bpf_verifier_env *env)
{
	__bpf_free_used_maps(env->prog->aux, env->used_maps,
			     env->used_map_cnt);
}

/* drop refcnt of maps used by the rejected program */
static void release_btfs(struct bpf_verifier_env *env)
{
	__bpf_free_used_btfs(env->prog->aux, env->used_btfs,
			     env->used_btf_cnt);
}

/* convert pseudo BPF_LD_IMM64 into generic BPF_LD_IMM64 */
static void convert_pseudo_ld_imm64(struct bpf_verifier_env *env)
{
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	int i;

	for (i = 0; i < insn_cnt; i++, insn++)
		if (insn->code == (BPF_LD | BPF_IMM | BPF_DW))
			insn->src_reg = 0;
}

/* single env->prog->insni[off] instruction was replaced with the range
 * insni[off, off + cnt).  Adjust corresponding insn_aux_data by copying
 * [0, off) and [off, end) to new locations, so the patched range stays zero
 */
static int adjust_insn_aux_data(struct bpf_verifier_env *env,
				struct bpf_prog *new_prog, u32 off, u32 cnt)
{
	struct bpf_insn_aux_data *new_data, *old_data = env->insn_aux_data;
	struct bpf_insn *insn = new_prog->insnsi;
	u32 prog_len;
	int i;

	/* aux info at OFF always needs adjustment, no matter fast path
	 * (cnt == 1) is taken or not. There is no guarantee INSN at OFF is the
	 * original insn at old prog.
	 */
	old_data[off].zext_dst = insn_has_def32(env, insn + off + cnt - 1);

	if (cnt == 1)
		return 0;
	prog_len = new_prog->len;
	new_data = vzalloc(array_size(prog_len,
				      sizeof(struct bpf_insn_aux_data)));
	if (!new_data)
		return -ENOMEM;
	memcpy(new_data, old_data, sizeof(struct bpf_insn_aux_data) * off);
	memcpy(new_data + off + cnt - 1, old_data + off,
	       sizeof(struct bpf_insn_aux_data) * (prog_len - off - cnt + 1));
	for (i = off; i < off + cnt - 1; i++) {
		new_data[i].seen = env->pass_cnt;
		new_data[i].zext_dst = insn_has_def32(env, insn + i);
	}
	env->insn_aux_data = new_data;
	vfree(old_data);
	return 0;
}

static void adjust_subprog_starts(struct bpf_verifier_env *env, u32 off, u32 len)
{
	int i;

	if (len == 1)
		return;
	/* NOTE: fake 'exit' subprog should be updated as well. */
	for (i = 0; i <= env->subprog_cnt; i++) {
		if (env->subprog_info[i].start <= off)
			continue;
		env->subprog_info[i].start += len - 1;
	}
}

static void adjust_poke_descs(struct bpf_prog *prog, u32 len)
{
	struct bpf_jit_poke_descriptor *tab = prog->aux->poke_tab;
	int i, sz = prog->aux->size_poke_tab;
	struct bpf_jit_poke_descriptor *desc;

	for (i = 0; i < sz; i++) {
		desc = &tab[i];
		desc->insn_idx += len - 1;
	}
}

static struct bpf_prog *bpf_patch_insn_data(struct bpf_verifier_env *env, u32 off,
					    const struct bpf_insn *patch, u32 len)
{
	struct bpf_prog *new_prog;

	new_prog = bpf_patch_insn_single(env->prog, off, patch, len);
	if (IS_ERR(new_prog)) {
		if (PTR_ERR(new_prog) == -ERANGE)
			verbose(env,
				"insn %d cannot be patched due to 16-bit range\n",
				env->insn_aux_data[off].orig_idx);
		return NULL;
	}
	if (adjust_insn_aux_data(env, new_prog, off, len))
		return NULL;
	adjust_subprog_starts(env, off, len);
	adjust_poke_descs(new_prog, len);
	return new_prog;
}

static int adjust_subprog_starts_after_remove(struct bpf_verifier_env *env,
					      u32 off, u32 cnt)
{
	int i, j;

	/* find first prog starting at or after off (first to remove) */
	for (i = 0; i < env->subprog_cnt; i++)
		if (env->subprog_info[i].start >= off)
			break;
	/* find first prog starting at or after off + cnt (first to stay) */
	for (j = i; j < env->subprog_cnt; j++)
		if (env->subprog_info[j].start >= off + cnt)
			break;
	/* if j doesn't start exactly at off + cnt, we are just removing
	 * the front of previous prog
	 */
	if (env->subprog_info[j].start != off + cnt)
		j--;

	if (j > i) {
		struct bpf_prog_aux *aux = env->prog->aux;
		int move;

		/* move fake 'exit' subprog as well */
		move = env->subprog_cnt + 1 - j;

		memmove(env->subprog_info + i,
			env->subprog_info + j,
			sizeof(*env->subprog_info) * move);
		env->subprog_cnt -= j - i;

		/* remove func_info */
		if (aux->func_info) {
			move = aux->func_info_cnt - j;

			memmove(aux->func_info + i,
				aux->func_info + j,
				sizeof(*aux->func_info) * move);
			aux->func_info_cnt -= j - i;
			/* func_info->insn_off is set after all code rewrites,
			 * in adjust_btf_func() - no need to adjust
			 */
		}
	} else {
		/* convert i from "first prog to remove" to "first to adjust" */
		if (env->subprog_info[i].start == off)
			i++;
	}

	/* update fake 'exit' subprog as well */
	for (; i <= env->subprog_cnt; i++)
		env->subprog_info[i].start -= cnt;

	return 0;
}

static int bpf_adj_linfo_after_remove(struct bpf_verifier_env *env, u32 off,
				      u32 cnt)
{
	struct bpf_prog *prog = env->prog;
	u32 i, l_off, l_cnt, nr_linfo;
	struct bpf_line_info *linfo;

	nr_linfo = prog->aux->nr_linfo;
	if (!nr_linfo)
		return 0;

	linfo = prog->aux->linfo;

	/* find first line info to remove, count lines to be removed */
	for (i = 0; i < nr_linfo; i++)
		if (linfo[i].insn_off >= off)
			break;

	l_off = i;
	l_cnt = 0;
	for (; i < nr_linfo; i++)
		if (linfo[i].insn_off < off + cnt)
			l_cnt++;
		else
			break;

	/* First live insn doesn't match first live linfo, it needs to "inherit"
	 * last removed linfo.  prog is already modified, so prog->len == off
	 * means no live instructions after (tail of the program was removed).
	 */
	if (prog->len != off && l_cnt &&
	    (i == nr_linfo || linfo[i].insn_off != off + cnt)) {
		l_cnt--;
		linfo[--i].insn_off = off + cnt;
	}

	/* remove the line info which refer to the removed instructions */
	if (l_cnt) {
		memmove(linfo + l_off, linfo + i,
			sizeof(*linfo) * (nr_linfo - i));

		prog->aux->nr_linfo -= l_cnt;
		nr_linfo = prog->aux->nr_linfo;
	}

	/* pull all linfo[i].insn_off >= off + cnt in by cnt */
	for (i = l_off; i < nr_linfo; i++)
		linfo[i].insn_off -= cnt;

	/* fix up all subprogs (incl. 'exit') which start >= off */
	for (i = 0; i <= env->subprog_cnt; i++)
		if (env->subprog_info[i].linfo_idx > l_off) {
			/* program may have started in the removed region but
			 * may not be fully removed
			 */
			if (env->subprog_info[i].linfo_idx >= l_off + l_cnt)
				env->subprog_info[i].linfo_idx -= l_cnt;
			else
				env->subprog_info[i].linfo_idx = l_off;
		}

	return 0;
}

static int verifier_remove_insns(struct bpf_verifier_env *env, u32 off, u32 cnt)
{
	struct bpf_insn_aux_data *aux_data = env->insn_aux_data;
	unsigned int orig_prog_len = env->prog->len;
	int err;

	if (bpf_prog_is_dev_bound(env->prog->aux))
		bpf_prog_offload_remove_insns(env, off, cnt);

	err = bpf_remove_insns(env->prog, off, cnt);
	if (err)
		return err;

	err = adjust_subprog_starts_after_remove(env, off, cnt);
	if (err)
		return err;

	err = bpf_adj_linfo_after_remove(env, off, cnt);
	if (err)
		return err;

	memmove(aux_data + off,	aux_data + off + cnt,
		sizeof(*aux_data) * (orig_prog_len - off - cnt));

	return 0;
}

/* The verifier does more data flow analysis than llvm and will not
 * explore branches that are dead at run time. Malicious programs can
 * have dead code too. Therefore replace all dead at-run-time code
 * with 'ja -1'.
 *
 * Just nops are not optimal, e.g. if they would sit at the end of the
 * program and through another bug we would manage to jump there, then
 * we'd execute beyond program memory otherwise. Returning exception
 * code also wouldn't work since we can have subprogs where the dead
 * code could be located.
 */
static void sanitize_dead_code(struct bpf_verifier_env *env)
{
	struct bpf_insn_aux_data *aux_data = env->insn_aux_data;
	struct bpf_insn trap = BPF_JMP_IMM(BPF_JA, 0, 0, -1);
	struct bpf_insn *insn = env->prog->insnsi;
	const int insn_cnt = env->prog->len;
	int i;

	for (i = 0; i < insn_cnt; i++) {
		if (aux_data[i].seen)
			continue;
		memcpy(insn + i, &trap, sizeof(trap));
	}
}

static bool insn_is_cond_jump(u8 code)
{
	u8 op;

	if (BPF_CLASS(code) == BPF_JMP32)
		return true;

	if (BPF_CLASS(code) != BPF_JMP)
		return false;

	op = BPF_OP(code);
	return op != BPF_JA && op != BPF_EXIT && op != BPF_CALL;
}

static void opt_hard_wire_dead_code_branches(struct bpf_verifier_env *env)
{
	struct bpf_insn_aux_data *aux_data = env->insn_aux_data;
	struct bpf_insn ja = BPF_JMP_IMM(BPF_JA, 0, 0, 0);
	struct bpf_insn *insn = env->prog->insnsi;
	const int insn_cnt = env->prog->len;
	int i;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (!insn_is_cond_jump(insn->code))
			continue;

		if (!aux_data[i + 1].seen)
			ja.off = insn->off;
		else if (!aux_data[i + 1 + insn->off].seen)
			ja.off = 0;
		else
			continue;

		if (bpf_prog_is_dev_bound(env->prog->aux))
			bpf_prog_offload_replace_insn(env, i, &ja);

		memcpy(insn, &ja, sizeof(ja));
	}
}

static int opt_remove_dead_code(struct bpf_verifier_env *env)
{
	struct bpf_insn_aux_data *aux_data = env->insn_aux_data;
	int insn_cnt = env->prog->len;
	int i, err;

	for (i = 0; i < insn_cnt; i++) {
		int j;

		j = 0;
		while (i + j < insn_cnt && !aux_data[i + j].seen)
			j++;
		if (!j)
			continue;

		err = verifier_remove_insns(env, i, j);
		if (err)
			return err;
		insn_cnt = env->prog->len;
	}

	return 0;
}

static int opt_remove_nops(struct bpf_verifier_env *env)
{
	const struct bpf_insn ja = BPF_JMP_IMM(BPF_JA, 0, 0, 0);
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	int i, err;

	for (i = 0; i < insn_cnt; i++) {
		if (memcmp(&insn[i], &ja, sizeof(ja)))
			continue;

		err = verifier_remove_insns(env, i, 1);
		if (err)
			return err;
		insn_cnt--;
		i--;
	}

	return 0;
}

static int opt_subreg_zext_lo32_rnd_hi32(struct bpf_verifier_env *env,
					 const union bpf_attr *attr)
{
	struct bpf_insn *patch, zext_patch[2], rnd_hi32_patch[4];
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	int i, patch_len, delta = 0, len = env->prog->len;
	struct bpf_insn *insns = env->prog->insnsi;
	struct bpf_prog *new_prog;
	bool rnd_hi32;

	rnd_hi32 = attr->prog_flags & BPF_F_TEST_RND_HI32;
	zext_patch[1] = BPF_ZEXT_REG(0);
	rnd_hi32_patch[1] = BPF_ALU64_IMM(BPF_MOV, BPF_REG_AX, 0);
	rnd_hi32_patch[2] = BPF_ALU64_IMM(BPF_LSH, BPF_REG_AX, 32);
	rnd_hi32_patch[3] = BPF_ALU64_REG(BPF_OR, 0, BPF_REG_AX);
	for (i = 0; i < len; i++) {
		int adj_idx = i + delta;
		struct bpf_insn insn;
		int load_reg;

		insn = insns[adj_idx];
		load_reg = insn_def_regno(&insn);
		if (!aux[adj_idx].zext_dst) {
			u8 code, class;
			u32 imm_rnd;

			if (!rnd_hi32)
				continue;

			code = insn.code;
			class = BPF_CLASS(code);
			if (load_reg == -1)
				continue;

			/* NOTE: arg "reg" (the fourth one) is only used for
			 *       BPF_STX + SRC_OP, so it is safe to pass NULL
			 *       here.
			 */
			if (is_reg64(env, &insn, load_reg, NULL, DST_OP)) {
				if (class == BPF_LD &&
				    BPF_MODE(code) == BPF_IMM)
					i++;
				continue;
			}

			/* ctx load could be transformed into wider load. */
			if (class == BPF_LDX &&
			    aux[adj_idx].ptr_type == PTR_TO_CTX)
				continue;

			imm_rnd = get_random_int();
			rnd_hi32_patch[0] = insn;
			rnd_hi32_patch[1].imm = imm_rnd;
			rnd_hi32_patch[3].dst_reg = load_reg;
			patch = rnd_hi32_patch;
			patch_len = 4;
			goto apply_patch_buffer;
		}

		/* Add in an zero-extend instruction if a) the JIT has requested
		 * it or b) it's a CMPXCHG.
		 *
		 * The latter is because: BPF_CMPXCHG always loads a value into
		 * R0, therefore always zero-extends. However some archs'
		 * equivalent instruction only does this load when the
		 * comparison is successful. This detail of CMPXCHG is
		 * orthogonal to the general zero-extension behaviour of the
		 * CPU, so it's treated independently of bpf_jit_needs_zext.
		 */
		if (!bpf_jit_needs_zext() && !is_cmpxchg_insn(&insn))
			continue;

		if (WARN_ON(load_reg == -1)) {
			verbose(env, "verifier bug. zext_dst is set, but no reg is defined\n");
			return -EFAULT;
		}

		zext_patch[0] = insn;
		zext_patch[1].dst_reg = load_reg;
		zext_patch[1].src_reg = load_reg;
		patch = zext_patch;
		patch_len = 2;
apply_patch_buffer:
		new_prog = bpf_patch_insn_data(env, adj_idx, patch, patch_len);
		if (!new_prog)
			return -ENOMEM;
		env->prog = new_prog;
		insns = new_prog->insnsi;
		aux = env->insn_aux_data;
		delta += patch_len - 1;
	}

	return 0;
}

/* convert load instructions that access fields of a context type into a
 * sequence of instructions that access fields of the underlying structure:
 *     struct __sk_buff    -> struct sk_buff
 *     struct bpf_sock_ops -> struct sock
 */
static int convert_ctx_accesses(struct bpf_verifier_env *env)
{
	const struct bpf_verifier_ops *ops = env->ops;
	int i, cnt, size, ctx_field_size, delta = 0;
	const int insn_cnt = env->prog->len;
	struct bpf_insn insn_buf[16], *insn;
	u32 target_size, size_default, off;
	struct bpf_prog *new_prog;
	enum bpf_access_type type;
	bool is_narrower_load;

	if (ops->gen_prologue || env->seen_direct_write) {
		if (!ops->gen_prologue) {
			verbose(env, "bpf verifier is misconfigured\n");
			return -EINVAL;
		}
		cnt = ops->gen_prologue(insn_buf, env->seen_direct_write,
					env->prog);
		if (cnt >= ARRAY_SIZE(insn_buf)) {
			verbose(env, "bpf verifier is misconfigured\n");
			return -EINVAL;
		} else if (cnt) {
			new_prog = bpf_patch_insn_data(env, 0, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			env->prog = new_prog;
			delta += cnt - 1;
		}
	}

	if (bpf_prog_is_dev_bound(env->prog->aux))
		return 0;

	insn = env->prog->insnsi + delta;

	for (i = 0; i < insn_cnt; i++, insn++) {
		bpf_convert_ctx_access_t convert_ctx_access;

		if (insn->code == (BPF_LDX | BPF_MEM | BPF_B) ||
		    insn->code == (BPF_LDX | BPF_MEM | BPF_H) ||
		    insn->code == (BPF_LDX | BPF_MEM | BPF_W) ||
		    insn->code == (BPF_LDX | BPF_MEM | BPF_DW))
			type = BPF_READ;
		else if (insn->code == (BPF_STX | BPF_MEM | BPF_B) ||
			 insn->code == (BPF_STX | BPF_MEM | BPF_H) ||
			 insn->code == (BPF_STX | BPF_MEM | BPF_W) ||
			 insn->code == (BPF_STX | BPF_MEM | BPF_DW))
			type = BPF_WRITE;
		else
			continue;

		if (type == BPF_WRITE &&
		    env->insn_aux_data[i + delta].sanitize_stack_off) {
			struct bpf_insn patch[] = {
				/* Sanitize suspicious stack slot with zero.
				 * There are no memory dependencies for this store,
				 * since it's only using frame pointer and immediate
				 * constant of zero
				 */
				BPF_ST_MEM(BPF_DW, BPF_REG_FP,
					   env->insn_aux_data[i + delta].sanitize_stack_off,
					   0),
				/* the original STX instruction will immediately
				 * overwrite the same stack slot with appropriate value
				 */
				*insn,
			};

			cnt = ARRAY_SIZE(patch);
			new_prog = bpf_patch_insn_data(env, i + delta, patch, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			continue;
		}

		switch (env->insn_aux_data[i + delta].ptr_type) {
		case PTR_TO_CTX:
			if (!ops->convert_ctx_access)
				continue;
			convert_ctx_access = ops->convert_ctx_access;
			break;
		case PTR_TO_SOCKET:
		case PTR_TO_SOCK_COMMON:
			convert_ctx_access = bpf_sock_convert_ctx_access;
			break;
		case PTR_TO_TCP_SOCK:
			convert_ctx_access = bpf_tcp_sock_convert_ctx_access;
			break;
		case PTR_TO_XDP_SOCK:
			convert_ctx_access = bpf_xdp_sock_convert_ctx_access;
			break;
		case PTR_TO_BTF_ID:
			if (type == BPF_READ) {
				insn->code = BPF_LDX | BPF_PROBE_MEM |
					BPF_SIZE((insn)->code);
				env->prog->aux->num_exentries++;
			} else if (resolve_prog_type(env->prog) != BPF_PROG_TYPE_STRUCT_OPS) {
				verbose(env, "Writes through BTF pointers are not allowed\n");
				return -EINVAL;
			}
			continue;
		default:
			continue;
		}

		ctx_field_size = env->insn_aux_data[i + delta].ctx_field_size;
		size = BPF_LDST_BYTES(insn);

		/* If the read access is a narrower load of the field,
		 * convert to a 4/8-byte load, to minimum program type specific
		 * convert_ctx_access changes. If conversion is successful,
		 * we will apply proper mask to the result.
		 */
		is_narrower_load = size < ctx_field_size;
		size_default = bpf_ctx_off_adjust_machine(ctx_field_size);
		off = insn->off;
		if (is_narrower_load) {
			u8 size_code;

			if (type == BPF_WRITE) {
				verbose(env, "bpf verifier narrow ctx access misconfigured\n");
				return -EINVAL;
			}

			size_code = BPF_H;
			if (ctx_field_size == 4)
				size_code = BPF_W;
			else if (ctx_field_size == 8)
				size_code = BPF_DW;

			insn->off = off & ~(size_default - 1);
			insn->code = BPF_LDX | BPF_MEM | size_code;
		}

		target_size = 0;
		cnt = convert_ctx_access(type, insn, insn_buf, env->prog,
					 &target_size);
		if (cnt == 0 || cnt >= ARRAY_SIZE(insn_buf) ||
		    (ctx_field_size && !target_size)) {
			verbose(env, "bpf verifier is misconfigured\n");
			return -EINVAL;
		}

		if (is_narrower_load && size < target_size) {
			u8 shift = bpf_ctx_narrow_access_offset(
				off, size, size_default) * 8;
			if (ctx_field_size <= 4) {
				if (shift)
					insn_buf[cnt++] = BPF_ALU32_IMM(BPF_RSH,
									insn->dst_reg,
									shift);
				insn_buf[cnt++] = BPF_ALU32_IMM(BPF_AND, insn->dst_reg,
								(1 << size * 8) - 1);
			} else {
				if (shift)
					insn_buf[cnt++] = BPF_ALU64_IMM(BPF_RSH,
									insn->dst_reg,
									shift);
				insn_buf[cnt++] = BPF_ALU64_IMM(BPF_AND, insn->dst_reg,
								(1ULL << size * 8) - 1);
			}
		}

		new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
		if (!new_prog)
			return -ENOMEM;

		delta += cnt - 1;

		/* keep walking new program and skip insns we just inserted */
		env->prog = new_prog;
		insn      = new_prog->insnsi + i + delta;
	}

	return 0;
}

static int jit_subprogs(struct bpf_verifier_env *env)
{
	struct bpf_prog *prog = env->prog, **func, *tmp;
	int i, j, subprog_start, subprog_end = 0, len, subprog;
	struct bpf_map *map_ptr;
	struct bpf_insn *insn;
	void *old_bpf_func;
	int err, num_exentries;

	if (env->subprog_cnt <= 1)
		return 0;

	for (i = 0, insn = prog->insnsi; i < prog->len; i++, insn++) {
		if (!bpf_pseudo_call(insn))
			continue;
		/* Upon error here we cannot fall back to interpreter but
		 * need a hard reject of the program. Thus -EFAULT is
		 * propagated in any case.
		 */
		subprog = find_subprog(env, i + insn->imm + 1);
		if (subprog < 0) {
			WARN_ONCE(1, "verifier bug. No program starts at insn %d\n",
				  i + insn->imm + 1);
			return -EFAULT;
		}
		/* temporarily remember subprog id inside insn instead of
		 * aux_data, since next loop will split up all insns into funcs
		 */
		insn->off = subprog;
		/* remember original imm in case JIT fails and fallback
		 * to interpreter will be needed
		 */
		env->insn_aux_data[i].call_imm = insn->imm;
		/* point imm to __bpf_call_base+1 from JITs point of view */
		insn->imm = 1;
	}

	err = bpf_prog_alloc_jited_linfo(prog);
	if (err)
		goto out_undo_insn;

	err = -ENOMEM;
	func = kcalloc(env->subprog_cnt, sizeof(prog), GFP_KERNEL);
	if (!func)
		goto out_undo_insn;

	for (i = 0; i < env->subprog_cnt; i++) {
		subprog_start = subprog_end;
		subprog_end = env->subprog_info[i + 1].start;

		len = subprog_end - subprog_start;
		/* BPF_PROG_RUN doesn't call subprogs directly,
		 * hence main prog stats include the runtime of subprogs.
		 * subprogs don't have IDs and not reachable via prog_get_next_id
		 * func[i]->stats will never be accessed and stays NULL
		 */
		func[i] = bpf_prog_alloc_no_stats(bpf_prog_size(len), GFP_USER);
		if (!func[i])
			goto out_free;
		memcpy(func[i]->insnsi, &prog->insnsi[subprog_start],
		       len * sizeof(struct bpf_insn));
		func[i]->type = prog->type;
		func[i]->len = len;
		if (bpf_prog_calc_tag(func[i]))
			goto out_free;
		func[i]->is_func = 1;
		func[i]->aux->func_idx = i;
		/* the btf and func_info will be freed only at prog->aux */
		func[i]->aux->btf = prog->aux->btf;
		func[i]->aux->func_info = prog->aux->func_info;

		for (j = 0; j < prog->aux->size_poke_tab; j++) {
			u32 insn_idx = prog->aux->poke_tab[j].insn_idx;
			int ret;

			if (!(insn_idx >= subprog_start &&
			      insn_idx <= subprog_end))
				continue;

			ret = bpf_jit_add_poke_descriptor(func[i],
							  &prog->aux->poke_tab[j]);
			if (ret < 0) {
				verbose(env, "adding tail call poke descriptor failed\n");
				goto out_free;
			}

			func[i]->insnsi[insn_idx - subprog_start].imm = ret + 1;

			map_ptr = func[i]->aux->poke_tab[ret].tail_call.map;
			ret = map_ptr->ops->map_poke_track(map_ptr, func[i]->aux);
			if (ret < 0) {
				verbose(env, "tracking tail call prog failed\n");
				goto out_free;
			}
		}

		/* Use bpf_prog_F_tag to indicate functions in stack traces.
		 * Long term would need debug info to populate names
		 */
		func[i]->aux->name[0] = 'F';
		func[i]->aux->stack_depth = env->subprog_info[i].stack_depth;
		func[i]->jit_requested = 1;
		func[i]->aux->linfo = prog->aux->linfo;
		func[i]->aux->nr_linfo = prog->aux->nr_linfo;
		func[i]->aux->jited_linfo = prog->aux->jited_linfo;
		func[i]->aux->linfo_idx = env->subprog_info[i].linfo_idx;
		num_exentries = 0;
		insn = func[i]->insnsi;
		for (j = 0; j < func[i]->len; j++, insn++) {
			if (BPF_CLASS(insn->code) == BPF_LDX &&
			    BPF_MODE(insn->code) == BPF_PROBE_MEM)
				num_exentries++;
		}
		func[i]->aux->num_exentries = num_exentries;
		func[i]->aux->tail_call_reachable = env->subprog_info[i].tail_call_reachable;
		func[i] = bpf_int_jit_compile(func[i]);
		if (!func[i]->jited) {
			err = -ENOTSUPP;
			goto out_free;
		}
		cond_resched();
	}

	/* Untrack main program's aux structs so that during map_poke_run()
	 * we will not stumble upon the unfilled poke descriptors; each
	 * of the main program's poke descs got distributed across subprogs
	 * and got tracked onto map, so we are sure that none of them will
	 * be missed after the operation below
	 */
	for (i = 0; i < prog->aux->size_poke_tab; i++) {
		map_ptr = prog->aux->poke_tab[i].tail_call.map;

		map_ptr->ops->map_poke_untrack(map_ptr, prog->aux);
	}

	/* at this point all bpf functions were successfully JITed
	 * now populate all bpf_calls with correct addresses and
	 * run last pass of JIT
	 */
	for (i = 0; i < env->subprog_cnt; i++) {
		insn = func[i]->insnsi;
		for (j = 0; j < func[i]->len; j++, insn++) {
			if (!bpf_pseudo_call(insn))
				continue;
			subprog = insn->off;
			insn->imm = BPF_CAST_CALL(func[subprog]->bpf_func) -
				    __bpf_call_base;
		}

		/* we use the aux data to keep a list of the start addresses
		 * of the JITed images for each function in the program
		 *
		 * for some architectures, such as powerpc64, the imm field
		 * might not be large enough to hold the offset of the start
		 * address of the callee's JITed image from __bpf_call_base
		 *
		 * in such cases, we can lookup the start address of a callee
		 * by using its subprog id, available from the off field of
		 * the call instruction, as an index for this list
		 */
		func[i]->aux->func = func;
		func[i]->aux->func_cnt = env->subprog_cnt;
	}
	for (i = 0; i < env->subprog_cnt; i++) {
		old_bpf_func = func[i]->bpf_func;
		tmp = bpf_int_jit_compile(func[i]);
		if (tmp != func[i] || func[i]->bpf_func != old_bpf_func) {
			verbose(env, "JIT doesn't support bpf-to-bpf calls\n");
			err = -ENOTSUPP;
			goto out_free;
		}
		cond_resched();
	}

	/* finally lock prog and jit images for all functions and
	 * populate kallsysm
	 */
	for (i = 0; i < env->subprog_cnt; i++) {
		bpf_prog_lock_ro(func[i]);
		bpf_prog_kallsyms_add(func[i]);
	}

	/* Last step: make now unused interpreter insns from main
	 * prog consistent for later dump requests, so they can
	 * later look the same as if they were interpreted only.
	 */
	for (i = 0, insn = prog->insnsi; i < prog->len; i++, insn++) {
		if (!bpf_pseudo_call(insn))
			continue;
		insn->off = env->insn_aux_data[i].call_imm;
		subprog = find_subprog(env, i + insn->off + 1);
		insn->imm = subprog;
	}

	prog->jited = 1;
	prog->bpf_func = func[0]->bpf_func;
	prog->aux->func = func;
	prog->aux->func_cnt = env->subprog_cnt;
	bpf_prog_free_unused_jited_linfo(prog);
	return 0;
out_free:
	for (i = 0; i < env->subprog_cnt; i++) {
		if (!func[i])
			continue;

		for (j = 0; j < func[i]->aux->size_poke_tab; j++) {
			map_ptr = func[i]->aux->poke_tab[j].tail_call.map;
			map_ptr->ops->map_poke_untrack(map_ptr, func[i]->aux);
		}
		bpf_jit_free(func[i]);
	}
	kfree(func);
out_undo_insn:
	/* cleanup main prog to be interpreted */
	prog->jit_requested = 0;
	for (i = 0, insn = prog->insnsi; i < prog->len; i++, insn++) {
		if (!bpf_pseudo_call(insn))
			continue;
		insn->off = 0;
		insn->imm = env->insn_aux_data[i].call_imm;
	}
	bpf_prog_free_jited_linfo(prog);
	return err;
}

static int fixup_call_args(struct bpf_verifier_env *env)
{
#ifndef CONFIG_BPF_JIT_ALWAYS_ON
	struct bpf_prog *prog = env->prog;
	struct bpf_insn *insn = prog->insnsi;
	int i, depth;
#endif
	int err = 0;

	if (env->prog->jit_requested &&
	    !bpf_prog_is_dev_bound(env->prog->aux)) {
		err = jit_subprogs(env);
		if (err == 0)
			return 0;
		if (err == -EFAULT)
			return err;
	}
#ifndef CONFIG_BPF_JIT_ALWAYS_ON
	if (env->subprog_cnt > 1 && env->prog->aux->tail_call_reachable) {
		/* When JIT fails the progs with bpf2bpf calls and tail_calls
		 * have to be rejected, since interpreter doesn't support them yet.
		 */
		verbose(env, "tail_calls are not allowed in non-JITed programs with bpf-to-bpf calls\n");
		return -EINVAL;
	}
	for (i = 0; i < prog->len; i++, insn++) {
		if (!bpf_pseudo_call(insn))
			continue;
		depth = get_callee_stack_depth(env, insn, i);
		if (depth < 0)
			return depth;
		bpf_patch_call_args(insn, depth);
	}
	err = 0;
#endif
	return err;
}

/* fixup insn->imm field of bpf_call instructions
 * and inline eligible helpers as explicit sequence of BPF instructions
 *
 * this function is called after eBPF program passed verification
 */
static int fixup_bpf_calls(struct bpf_verifier_env *env)
{
	struct bpf_prog *prog = env->prog;
	bool expect_blinding = bpf_jit_blinding_enabled(prog);
	struct bpf_insn *insn = prog->insnsi;
	const struct bpf_func_proto *fn;
	const int insn_cnt = prog->len;
	const struct bpf_map_ops *ops;
	struct bpf_insn_aux_data *aux;
	struct bpf_insn insn_buf[16];
	struct bpf_prog *new_prog;
	struct bpf_map *map_ptr;
	int i, ret, cnt, delta = 0;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (insn->code == (BPF_ALU64 | BPF_MOD | BPF_X) ||
		    insn->code == (BPF_ALU64 | BPF_DIV | BPF_X) ||
		    insn->code == (BPF_ALU | BPF_MOD | BPF_X) ||
		    insn->code == (BPF_ALU | BPF_DIV | BPF_X)) {
			bool is64 = BPF_CLASS(insn->code) == BPF_ALU64;
			bool isdiv = BPF_OP(insn->code) == BPF_DIV;
			struct bpf_insn *patchlet;
			struct bpf_insn chk_and_div[] = {
				/* [R,W]x div 0 -> 0 */
				BPF_RAW_INSN((is64 ? BPF_JMP : BPF_JMP32) |
					     BPF_JNE | BPF_K, insn->src_reg,
					     0, 2, 0),
				BPF_ALU32_REG(BPF_XOR, insn->dst_reg, insn->dst_reg),
				BPF_JMP_IMM(BPF_JA, 0, 0, 1),
				*insn,
			};
			struct bpf_insn chk_and_mod[] = {
				/* [R,W]x mod 0 -> [R,W]x */
				BPF_RAW_INSN((is64 ? BPF_JMP : BPF_JMP32) |
					     BPF_JEQ | BPF_K, insn->src_reg,
					     0, 1 + (is64 ? 0 : 1), 0),
				*insn,
				BPF_JMP_IMM(BPF_JA, 0, 0, 1),
				BPF_MOV32_REG(insn->dst_reg, insn->dst_reg),
			};

			patchlet = isdiv ? chk_and_div : chk_and_mod;
			cnt = isdiv ? ARRAY_SIZE(chk_and_div) :
				      ARRAY_SIZE(chk_and_mod) - (is64 ? 2 : 0);

			new_prog = bpf_patch_insn_data(env, i + delta, patchlet, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			continue;
		}

		if (BPF_CLASS(insn->code) == BPF_LD &&
		    (BPF_MODE(insn->code) == BPF_ABS ||
		     BPF_MODE(insn->code) == BPF_IND)) {
			cnt = env->ops->gen_ld_abs(insn, insn_buf);
			if (cnt == 0 || cnt >= ARRAY_SIZE(insn_buf)) {
				verbose(env, "bpf verifier is misconfigured\n");
				return -EINVAL;
			}

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			continue;
		}

		if (insn->code == (BPF_ALU64 | BPF_ADD | BPF_X) ||
		    insn->code == (BPF_ALU64 | BPF_SUB | BPF_X)) {
			const u8 code_add = BPF_ALU64 | BPF_ADD | BPF_X;
			const u8 code_sub = BPF_ALU64 | BPF_SUB | BPF_X;
			struct bpf_insn insn_buf[16];
			struct bpf_insn *patch = &insn_buf[0];
			bool issrc, isneg;
			u32 off_reg;

			aux = &env->insn_aux_data[i + delta];
			if (!aux->alu_state ||
			    aux->alu_state == BPF_ALU_NON_POINTER)
				continue;

			isneg = aux->alu_state & BPF_ALU_NEG_VALUE;
			issrc = (aux->alu_state & BPF_ALU_SANITIZE) ==
				BPF_ALU_SANITIZE_SRC;

			off_reg = issrc ? insn->src_reg : insn->dst_reg;
			if (isneg)
				*patch++ = BPF_ALU64_IMM(BPF_MUL, off_reg, -1);
			*patch++ = BPF_MOV32_IMM(BPF_REG_AX, aux->alu_limit);
			*patch++ = BPF_ALU64_REG(BPF_SUB, BPF_REG_AX, off_reg);
			*patch++ = BPF_ALU64_REG(BPF_OR, BPF_REG_AX, off_reg);
			*patch++ = BPF_ALU64_IMM(BPF_NEG, BPF_REG_AX, 0);
			*patch++ = BPF_ALU64_IMM(BPF_ARSH, BPF_REG_AX, 63);
			if (issrc) {
				*patch++ = BPF_ALU64_REG(BPF_AND, BPF_REG_AX,
							 off_reg);
				insn->src_reg = BPF_REG_AX;
			} else {
				*patch++ = BPF_ALU64_REG(BPF_AND, off_reg,
							 BPF_REG_AX);
			}
			if (isneg)
				insn->code = insn->code == code_add ?
					     code_sub : code_add;
			*patch++ = *insn;
			if (issrc && isneg)
				*patch++ = BPF_ALU64_IMM(BPF_MUL, off_reg, -1);
			cnt = patch - insn_buf;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			continue;
		}

		if (insn->code != (BPF_JMP | BPF_CALL))
			continue;
		if (insn->src_reg == BPF_PSEUDO_CALL)
			continue;

		if (insn->imm == BPF_FUNC_get_route_realm)
			prog->dst_needed = 1;
		if (insn->imm == BPF_FUNC_get_prandom_u32)
			bpf_user_rnd_init_once();
		if (insn->imm == BPF_FUNC_override_return)
			prog->kprobe_override = 1;
		if (insn->imm == BPF_FUNC_tail_call) {
			/* If we tail call into other programs, we
			 * cannot make any assumptions since they can
			 * be replaced dynamically during runtime in
			 * the program array.
			 */
			prog->cb_access = 1;
			if (!allow_tail_call_in_subprogs(env))
				prog->aux->stack_depth = MAX_BPF_STACK;
			prog->aux->max_pkt_offset = MAX_PACKET_OFF;

			/* mark bpf_tail_call as different opcode to avoid
			 * conditional branch in the interpeter for every normal
			 * call and to prevent accidental JITing by JIT compiler
			 * that doesn't support bpf_tail_call yet
			 */
			insn->imm = 0;
			insn->code = BPF_JMP | BPF_TAIL_CALL;

			aux = &env->insn_aux_data[i + delta];
			if (env->bpf_capable && !expect_blinding &&
			    prog->jit_requested &&
			    !bpf_map_key_poisoned(aux) &&
			    !bpf_map_ptr_poisoned(aux) &&
			    !bpf_map_ptr_unpriv(aux)) {
				struct bpf_jit_poke_descriptor desc = {
					.reason = BPF_POKE_REASON_TAIL_CALL,
					.tail_call.map = BPF_MAP_PTR(aux->map_ptr_state),
					.tail_call.key = bpf_map_key_immediate(aux),
					.insn_idx = i + delta,
				};

				ret = bpf_jit_add_poke_descriptor(prog, &desc);
				if (ret < 0) {
					verbose(env, "adding tail call poke descriptor failed\n");
					return ret;
				}

				insn->imm = ret + 1;
				continue;
			}

			if (!bpf_map_ptr_unpriv(aux))
				continue;

			/* instead of changing every JIT dealing with tail_call
			 * emit two extra insns:
			 * if (index >= max_entries) goto out;
			 * index &= array->index_mask;
			 * to avoid out-of-bounds cpu speculation
			 */
			if (bpf_map_ptr_poisoned(aux)) {
				verbose(env, "tail_call abusing map_ptr\n");
				return -EINVAL;
			}

			map_ptr = BPF_MAP_PTR(aux->map_ptr_state);
			insn_buf[0] = BPF_JMP_IMM(BPF_JGE, BPF_REG_3,
						  map_ptr->max_entries, 2);
			insn_buf[1] = BPF_ALU32_IMM(BPF_AND, BPF_REG_3,
						    container_of(map_ptr,
								 struct bpf_array,
								 map)->index_mask);
			insn_buf[2] = *insn;
			cnt = 3;
			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			continue;
		}

		/* BPF_EMIT_CALL() assumptions in some of the map_gen_lookup
		 * and other inlining handlers are currently limited to 64 bit
		 * only.
		 */
		if (prog->jit_requested && BITS_PER_LONG == 64 &&
		    (insn->imm == BPF_FUNC_map_lookup_elem ||
		     insn->imm == BPF_FUNC_map_update_elem ||
		     insn->imm == BPF_FUNC_map_delete_elem ||
		     insn->imm == BPF_FUNC_map_push_elem   ||
		     insn->imm == BPF_FUNC_map_pop_elem    ||
		     insn->imm == BPF_FUNC_map_peek_elem)) {
			aux = &env->insn_aux_data[i + delta];
			if (bpf_map_ptr_poisoned(aux))
				goto patch_call_imm;

			map_ptr = BPF_MAP_PTR(aux->map_ptr_state);
			ops = map_ptr->ops;
			if (insn->imm == BPF_FUNC_map_lookup_elem &&
			    ops->map_gen_lookup) {
				cnt = ops->map_gen_lookup(map_ptr, insn_buf);
				if (cnt == -EOPNOTSUPP)
					goto patch_map_ops_generic;
				if (cnt <= 0 || cnt >= ARRAY_SIZE(insn_buf)) {
					verbose(env, "bpf verifier is misconfigured\n");
					return -EINVAL;
				}

				new_prog = bpf_patch_insn_data(env, i + delta,
							       insn_buf, cnt);
				if (!new_prog)
					return -ENOMEM;

				delta    += cnt - 1;
				env->prog = prog = new_prog;
				insn      = new_prog->insnsi + i + delta;
				continue;
			}

			BUILD_BUG_ON(!__same_type(ops->map_lookup_elem,
				     (void *(*)(struct bpf_map *map, void *key))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_delete_elem,
				     (int (*)(struct bpf_map *map, void *key))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_update_elem,
				     (int (*)(struct bpf_map *map, void *key, void *value,
					      u64 flags))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_push_elem,
				     (int (*)(struct bpf_map *map, void *value,
					      u64 flags))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_pop_elem,
				     (int (*)(struct bpf_map *map, void *value))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_peek_elem,
				     (int (*)(struct bpf_map *map, void *value))NULL));
patch_map_ops_generic:
			switch (insn->imm) {
			case BPF_FUNC_map_lookup_elem:
				insn->imm = BPF_CAST_CALL(ops->map_lookup_elem) -
					    __bpf_call_base;
				continue;
			case BPF_FUNC_map_update_elem:
				insn->imm = BPF_CAST_CALL(ops->map_update_elem) -
					    __bpf_call_base;
				continue;
			case BPF_FUNC_map_delete_elem:
				insn->imm = BPF_CAST_CALL(ops->map_delete_elem) -
					    __bpf_call_base;
				continue;
			case BPF_FUNC_map_push_elem:
				insn->imm = BPF_CAST_CALL(ops->map_push_elem) -
					    __bpf_call_base;
				continue;
			case BPF_FUNC_map_pop_elem:
				insn->imm = BPF_CAST_CALL(ops->map_pop_elem) -
					    __bpf_call_base;
				continue;
			case BPF_FUNC_map_peek_elem:
				insn->imm = BPF_CAST_CALL(ops->map_peek_elem) -
					    __bpf_call_base;
				continue;
			}

			goto patch_call_imm;
		}

		if (prog->jit_requested && BITS_PER_LONG == 64 &&
		    insn->imm == BPF_FUNC_jiffies64) {
			struct bpf_insn ld_jiffies_addr[2] = {
				BPF_LD_IMM64(BPF_REG_0,
					     (unsigned long)&jiffies),
			};

			insn_buf[0] = ld_jiffies_addr[0];
			insn_buf[1] = ld_jiffies_addr[1];
			insn_buf[2] = BPF_LDX_MEM(BPF_DW, BPF_REG_0,
						  BPF_REG_0, 0);
			cnt = 3;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf,
						       cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			continue;
		}

patch_call_imm:
		fn = env->ops->get_func_proto(insn->imm, env->prog);
		/* all functions that have prototype and verifier allowed
		 * programs to call them, must be real in-kernel functions
		 */
		if (!fn->func) {
			verbose(env,
				"kernel subsystem misconfigured func %s#%d\n",
				func_id_name(insn->imm), insn->imm);
			return -EFAULT;
		}
		insn->imm = fn->func - __bpf_call_base;
	}

	/* Since poke tab is now finalized, publish aux to tracker. */
	for (i = 0; i < prog->aux->size_poke_tab; i++) {
		map_ptr = prog->aux->poke_tab[i].tail_call.map;
		if (!map_ptr->ops->map_poke_track ||
		    !map_ptr->ops->map_poke_untrack ||
		    !map_ptr->ops->map_poke_run) {
			verbose(env, "bpf verifier is misconfigured\n");
			return -EINVAL;
		}

		ret = map_ptr->ops->map_poke_track(map_ptr, prog->aux);
		if (ret < 0) {
			verbose(env, "tracking tail call prog failed\n");
			return ret;
		}
	}

	return 0;
}

static void free_states(struct bpf_verifier_env *env)
{
	struct bpf_verifier_state_list *sl, *sln;
	int i;

	sl = env->free_list;
	while (sl) {
		sln = sl->next;
		free_verifier_state(&sl->state, false);
		kfree(sl);
		sl = sln;
	}
	env->free_list = NULL;

	if (!env->explored_states)
		return;

	for (i = 0; i < state_htab_size(env); i++) {
		sl = env->explored_states[i];

		while (sl) {
			sln = sl->next;
			free_verifier_state(&sl->state, false);
			kfree(sl);
			sl = sln;
		}
		env->explored_states[i] = NULL;
	}
}

/* The verifier is using insn_aux_data[] to store temporary data during
 * verification and to store information for passes that run after the
 * verification like dead code sanitization. do_check_common() for subprogram N
 * may analyze many other subprograms. sanitize_insn_aux_data() clears all
 * temporary data after do_check_common() finds that subprogram N cannot be
 * verified independently. pass_cnt counts the number of times
 * do_check_common() was run and insn->aux->seen tells the pass number
 * insn_aux_data was touched. These variables are compared to clear temporary
 * data from failed pass. For testing and experiments do_check_common() can be
 * run multiple times even when prior attempt to verify is unsuccessful.
 */
static void sanitize_insn_aux_data(struct bpf_verifier_env *env)
{
	struct bpf_insn *insn = env->prog->insnsi;
	struct bpf_insn_aux_data *aux;
	int i, class;

	for (i = 0; i < env->prog->len; i++) {
		class = BPF_CLASS(insn[i].code);
		if (class != BPF_LDX && class != BPF_STX)
			continue;
		aux = &env->insn_aux_data[i];
		if (aux->seen != env->pass_cnt)
			continue;
		memset(aux, 0, offsetof(typeof(*aux), orig_idx));
	}
}

static int do_check_common(struct bpf_verifier_env *env, int subprog)
{
	bool pop_log = !(env->log.level & BPF_LOG_LEVEL2);
	struct bpf_verifier_state *state;
	struct bpf_reg_state *regs;
	int ret, i;

	env->prev_linfo = NULL;
	env->pass_cnt++;

	state = kzalloc(sizeof(struct bpf_verifier_state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;
	state->curframe = 0;
	state->speculative = false;
	state->branches = 1;
	state->frame[0] = kzalloc(sizeof(struct bpf_func_state), GFP_KERNEL);
	if (!state->frame[0]) {
		kfree(state);
		return -ENOMEM;
	}
	env->cur_state = state;
	init_func_state(env, state->frame[0],
			BPF_MAIN_FUNC /* callsite */,
			0 /* frameno */,
			subprog);

	regs = state->frame[state->curframe]->regs;
	if (subprog || env->prog->type == BPF_PROG_TYPE_EXT) {
		ret = btf_prepare_func_args(env, subprog, regs);
		if (ret)
			goto out;
		for (i = BPF_REG_1; i <= BPF_REG_5; i++) {
			if (regs[i].type == PTR_TO_CTX)
				mark_reg_known_zero(env, regs, i);
			else if (regs[i].type == SCALAR_VALUE)
				mark_reg_unknown(env, regs, i);
			else if (regs[i].type == PTR_TO_MEM_OR_NULL) {
				const u32 mem_size = regs[i].mem_size;

				mark_reg_known_zero(env, regs, i);
				regs[i].mem_size = mem_size;
				regs[i].id = ++env->id_gen;
			}
		}
	} else {
		/* 1st arg to a function */
		regs[BPF_REG_1].type = PTR_TO_CTX;
		mark_reg_known_zero(env, regs, BPF_REG_1);
		ret = btf_check_func_arg_match(env, subprog, regs);
		if (ret == -EFAULT)
			/* unlikely verifier bug. abort.
			 * ret == 0 and ret < 0 are sadly acceptable for
			 * main() function due to backward compatibility.
			 * Like socket filter program may be written as:
			 * int bpf_prog(struct pt_regs *ctx)
			 * and never dereference that ctx in the program.
			 * 'struct pt_regs' is a type mismatch for socket
			 * filter that should be using 'struct __sk_buff'.
			 */
			goto out;
	}

	ret = do_check(env);
out:
	/* check for NULL is necessary, since cur_state can be freed inside
	 * do_check() under memory pressure.
	 */
	if (env->cur_state) {
		free_verifier_state(env->cur_state, true);
		env->cur_state = NULL;
	}
	while (!pop_stack(env, NULL, NULL, false));
	if (!ret && pop_log)
		bpf_vlog_reset(&env->log, 0);
	free_states(env);
	if (ret)
		/* clean aux data in case subprog was rejected */
		sanitize_insn_aux_data(env);
	return ret;
}

/* Verify all global functions in a BPF program one by one based on their BTF.
 * All global functions must pass verification. Otherwise the whole program is rejected.
 * Consider:
 * int bar(int);
 * int foo(int f)
 * {
 *    return bar(f);
 * }
 * int bar(int b)
 * {
 *    ...
 * }
 * foo() will be verified first for R1=any_scalar_value. During verification it
 * will be assumed that bar() already verified successfully and call to bar()
 * from foo() will be checked for type match only. Later bar() will be verified
 * independently to check that it's safe for R1=any_scalar_value.
 */
static int do_check_subprogs(struct bpf_verifier_env *env)
{
	struct bpf_prog_aux *aux = env->prog->aux;
	int i, ret;

	if (!aux->func_info)
		return 0;

	for (i = 1; i < env->subprog_cnt; i++) {
		if (aux->func_info_aux[i].linkage != BTF_FUNC_GLOBAL)
			continue;
		env->insn_idx = env->subprog_info[i].start;
		WARN_ON_ONCE(env->insn_idx == 0);
		ret = do_check_common(env, i);
		if (ret) {
			return ret;
		} else if (env->log.level & BPF_LOG_LEVEL) {
			verbose(env,
				"Func#%d is safe for any args that match its prototype\n",
				i);
		}
	}
	return 0;
}

static int do_check_main(struct bpf_verifier_env *env)
{
	int ret;

	env->insn_idx = 0;
	ret = do_check_common(env, 0);
	if (!ret)
		env->prog->aux->stack_depth = env->subprog_info[0].stack_depth;
	return ret;
}


static void print_verification_stats(struct bpf_verifier_env *env)
{
	int i;

	if (env->log.level & BPF_LOG_STATS) {
		verbose(env, "verification time %lld usec\n",
			div_u64(env->verification_time, 1000));
		verbose(env, "stack depth ");
		for (i = 0; i < env->subprog_cnt; i++) {
			u32 depth = env->subprog_info[i].stack_depth;

			verbose(env, "%d", depth);
			if (i + 1 < env->subprog_cnt)
				verbose(env, "+");
		}
		verbose(env, "\n");
	}
	verbose(env, "processed %d insns (limit %d) max_states_per_insn %d "
		"total_states %d peak_states %d mark_read %d\n",
		env->insn_processed, BPF_COMPLEXITY_LIMIT_INSNS,
		env->max_states_per_insn, env->total_states,
		env->peak_states, env->longest_mark_read_walk);
}

static int check_struct_ops_btf_id(struct bpf_verifier_env *env)
{
	const struct btf_type *t, *func_proto;
	const struct bpf_struct_ops *st_ops;
	const struct btf_member *member;
	struct bpf_prog *prog = env->prog;
	u32 btf_id, member_idx;
	const char *mname;

	if (!prog->gpl_compatible) {
		verbose(env, "struct ops programs must have a GPL compatible license\n");
		return -EINVAL;
	}

	btf_id = prog->aux->attach_btf_id;
	st_ops = bpf_struct_ops_find(btf_id);
	if (!st_ops) {
		verbose(env, "attach_btf_id %u is not a supported struct\n",
			btf_id);
		return -ENOTSUPP;
	}

	t = st_ops->type;
	member_idx = prog->expected_attach_type;
	if (member_idx >= btf_type_vlen(t)) {
		verbose(env, "attach to invalid member idx %u of struct %s\n",
			member_idx, st_ops->name);
		return -EINVAL;
	}

	member = &btf_type_member(t)[member_idx];
	mname = btf_name_by_offset(btf_vmlinux, member->name_off);
	func_proto = btf_type_resolve_func_ptr(btf_vmlinux, member->type,
					       NULL);
	if (!func_proto) {
		verbose(env, "attach to invalid member %s(@idx %u) of struct %s\n",
			mname, member_idx, st_ops->name);
		return -EINVAL;
	}

	if (st_ops->check_member) {
		int err = st_ops->check_member(t, member);

		if (err) {
			verbose(env, "attach to unsupported member %s of struct %s\n",
				mname, st_ops->name);
			return err;
		}
	}

	prog->aux->attach_func_proto = func_proto;
	prog->aux->attach_func_name = mname;
	env->ops = st_ops->verifier_ops;

	return 0;
}
#define SECURITY_PREFIX "security_"

static int check_attach_modify_return(unsigned long addr, const char *func_name)
{
	if (within_error_injection_list(addr) ||
	    !strncmp(SECURITY_PREFIX, func_name, sizeof(SECURITY_PREFIX) - 1))
		return 0;

	return -EINVAL;
}

/* list of non-sleepable functions that are otherwise on
 * ALLOW_ERROR_INJECTION list
 */
BTF_SET_START(btf_non_sleepable_error_inject)
/* Three functions below can be called from sleepable and non-sleepable context.
 * Assume non-sleepable from bpf safety point of view.
 */
BTF_ID(func, __add_to_page_cache_locked)
BTF_ID(func, should_fail_alloc_page)
BTF_ID(func, should_failslab)
BTF_SET_END(btf_non_sleepable_error_inject)

static int check_non_sleepable_error_inject(u32 btf_id)
{
	return btf_id_set_contains(&btf_non_sleepable_error_inject, btf_id);
}

int bpf_check_attach_target(struct bpf_verifier_log *log,
			    const struct bpf_prog *prog,
			    const struct bpf_prog *tgt_prog,
			    u32 btf_id,
			    struct bpf_attach_target_info *tgt_info)
{
	bool prog_extension = prog->type == BPF_PROG_TYPE_EXT;
	const char prefix[] = "btf_trace_";
	int ret = 0, subprog = -1, i;
	const struct btf_type *t;
	bool conservative = true;
	const char *tname;
	struct btf *btf;
	long addr = 0;

	if (!btf_id) {
		bpf_log(log, "Tracing programs must provide btf_id\n");
		return -EINVAL;
	}
	btf = tgt_prog ? tgt_prog->aux->btf : prog->aux->attach_btf;
	if (!btf) {
		bpf_log(log,
			"FENTRY/FEXIT program can only be attached to another program annotated with BTF\n");
		return -EINVAL;
	}
	t = btf_type_by_id(btf, btf_id);
	if (!t) {
		bpf_log(log, "attach_btf_id %u is invalid\n", btf_id);
		return -EINVAL;
	}
	tname = btf_name_by_offset(btf, t->name_off);
	if (!tname) {
		bpf_log(log, "attach_btf_id %u doesn't have a name\n", btf_id);
		return -EINVAL;
	}
	if (tgt_prog) {
		struct bpf_prog_aux *aux = tgt_prog->aux;

		for (i = 0; i < aux->func_info_cnt; i++)
			if (aux->func_info[i].type_id == btf_id) {
				subprog = i;
				break;
			}
		if (subprog == -1) {
			bpf_log(log, "Subprog %s doesn't exist\n", tname);
			return -EINVAL;
		}
		conservative = aux->func_info_aux[subprog].unreliable;
		if (prog_extension) {
			if (conservative) {
				bpf_log(log,
					"Cannot replace static functions\n");
				return -EINVAL;
			}
			if (!prog->jit_requested) {
				bpf_log(log,
					"Extension programs should be JITed\n");
				return -EINVAL;
			}
		}
		if (!tgt_prog->jited) {
			bpf_log(log, "Can attach to only JITed progs\n");
			return -EINVAL;
		}
		if (tgt_prog->type == prog->type) {
			/* Cannot fentry/fexit another fentry/fexit program.
			 * Cannot attach program extension to another extension.
			 * It's ok to attach fentry/fexit to extension program.
			 */
			bpf_log(log, "Cannot recursively attach\n");
			return -EINVAL;
		}
		if (tgt_prog->type == BPF_PROG_TYPE_TRACING &&
		    prog_extension &&
		    (tgt_prog->expected_attach_type == BPF_TRACE_FENTRY ||
		     tgt_prog->expected_attach_type == BPF_TRACE_FEXIT)) {
			/* Program extensions can extend all program types
			 * except fentry/fexit. The reason is the following.
			 * The fentry/fexit programs are used for performance
			 * analysis, stats and can be attached to any program
			 * type except themselves. When extension program is
			 * replacing XDP function it is necessary to allow
			 * performance analysis of all functions. Both original
			 * XDP program and its program extension. Hence
			 * attaching fentry/fexit to BPF_PROG_TYPE_EXT is
			 * allowed. If extending of fentry/fexit was allowed it
			 * would be possible to create long call chain
			 * fentry->extension->fentry->extension beyond
			 * reasonable stack size. Hence extending fentry is not
			 * allowed.
			 */
			bpf_log(log, "Cannot extend fentry/fexit\n");
			return -EINVAL;
		}
	} else {
		if (prog_extension) {
			bpf_log(log, "Cannot replace kernel functions\n");
			return -EINVAL;
		}
	}

	switch (prog->expected_attach_type) {
	case BPF_TRACE_RAW_TP:
		if (tgt_prog) {
			bpf_log(log,
				"Only FENTRY/FEXIT progs are attachable to another BPF prog\n");
			return -EINVAL;
		}
		if (!btf_type_is_typedef(t)) {
			bpf_log(log, "attach_btf_id %u is not a typedef\n",
				btf_id);
			return -EINVAL;
		}
		if (strncmp(prefix, tname, sizeof(prefix) - 1)) {
			bpf_log(log, "attach_btf_id %u points to wrong type name %s\n",
				btf_id, tname);
			return -EINVAL;
		}
		tname += sizeof(prefix) - 1;
		t = btf_type_by_id(btf, t->type);
		if (!btf_type_is_ptr(t))
			/* should never happen in valid vmlinux build */
			return -EINVAL;
		t = btf_type_by_id(btf, t->type);
		if (!btf_type_is_func_proto(t))
			/* should never happen in valid vmlinux build */
			return -EINVAL;

		break;
	case BPF_TRACE_ITER:
		if (!btf_type_is_func(t)) {
			bpf_log(log, "attach_btf_id %u is not a function\n",
				btf_id);
			return -EINVAL;
		}
		t = btf_type_by_id(btf, t->type);
		if (!btf_type_is_func_proto(t))
			return -EINVAL;
		ret = btf_distill_func_proto(log, btf, t, tname, &tgt_info->fmodel);
		if (ret)
			return ret;
		break;
	default:
		if (!prog_extension)
			return -EINVAL;
		fallthrough;
	case BPF_MODIFY_RETURN:
	case BPF_LSM_MAC:
	case BPF_TRACE_FENTRY:
	case BPF_TRACE_FEXIT:
		if (!btf_type_is_func(t)) {
			bpf_log(log, "attach_btf_id %u is not a function\n",
				btf_id);
			return -EINVAL;
		}
		if (prog_extension &&
		    btf_check_type_match(log, prog, btf, t))
			return -EINVAL;
		t = btf_type_by_id(btf, t->type);
		if (!btf_type_is_func_proto(t))
			return -EINVAL;

		if ((prog->aux->saved_dst_prog_type || prog->aux->saved_dst_attach_type) &&
		    (!tgt_prog || prog->aux->saved_dst_prog_type != tgt_prog->type ||
		     prog->aux->saved_dst_attach_type != tgt_prog->expected_attach_type))
			return -EINVAL;

		if (tgt_prog && conservative)
			t = NULL;

		ret = btf_distill_func_proto(log, btf, t, tname, &tgt_info->fmodel);
		if (ret < 0)
			return ret;

		if (tgt_prog) {
			if (subprog == 0)
				addr = (long) tgt_prog->bpf_func;
			else
				addr = (long) tgt_prog->aux->func[subprog]->bpf_func;
		} else {
			addr = kallsyms_lookup_name(tname);
			if (!addr) {
				bpf_log(log,
					"The address of function %s cannot be found\n",
					tname);
				return -ENOENT;
			}
		}

		if (prog->aux->sleepable) {
			ret = -EINVAL;
			switch (prog->type) {
			case BPF_PROG_TYPE_TRACING:
				/* fentry/fexit/fmod_ret progs can be sleepable only if they are
				 * attached to ALLOW_ERROR_INJECTION and are not in denylist.
				 */
				if (!check_non_sleepable_error_inject(btf_id) &&
				    within_error_injection_list(addr))
					ret = 0;
				break;
			case BPF_PROG_TYPE_LSM:
				/* LSM progs check that they are attached to bpf_lsm_*() funcs.
				 * Only some of them are sleepable.
				 */
				if (bpf_lsm_is_sleepable_hook(btf_id))
					ret = 0;
				break;
			default:
				break;
			}
			if (ret) {
				bpf_log(log, "%s is not sleepable\n", tname);
				return ret;
			}
		} else if (prog->expected_attach_type == BPF_MODIFY_RETURN) {
			if (tgt_prog) {
				bpf_log(log, "can't modify return codes of BPF programs\n");
				return -EINVAL;
			}
			ret = check_attach_modify_return(addr, tname);
			if (ret) {
				bpf_log(log, "%s() is not modifiable\n", tname);
				return ret;
			}
		}

		break;
	}
	tgt_info->tgt_addr = addr;
	tgt_info->tgt_name = tname;
	tgt_info->tgt_type = t;
	return 0;
}

static int check_attach_btf_id(struct bpf_verifier_env *env)
{
	struct bpf_prog *prog = env->prog;
	struct bpf_prog *tgt_prog = prog->aux->dst_prog;
	struct bpf_attach_target_info tgt_info = {};
	u32 btf_id = prog->aux->attach_btf_id;
	struct bpf_trampoline *tr;
	int ret;
	u64 key;

	if (prog->aux->sleepable && prog->type != BPF_PROG_TYPE_TRACING &&
	    prog->type != BPF_PROG_TYPE_LSM) {
		verbose(env, "Only fentry/fexit/fmod_ret and lsm programs can be sleepable\n");
		return -EINVAL;
	}

	if (prog->type == BPF_PROG_TYPE_STRUCT_OPS)
		return check_struct_ops_btf_id(env);

	if (prog->type != BPF_PROG_TYPE_TRACING &&
	    prog->type != BPF_PROG_TYPE_LSM &&
	    prog->type != BPF_PROG_TYPE_EXT)
		return 0;

	ret = bpf_check_attach_target(&env->log, prog, tgt_prog, btf_id, &tgt_info);
	if (ret)
		return ret;

	if (tgt_prog && prog->type == BPF_PROG_TYPE_EXT) {
		/* to make freplace equivalent to their targets, they need to
		 * inherit env->ops and expected_attach_type for the rest of the
		 * verification
		 */
		env->ops = bpf_verifier_ops[tgt_prog->type];
		prog->expected_attach_type = tgt_prog->expected_attach_type;
	}

	/* store info about the attachment target that will be used later */
	prog->aux->attach_func_proto = tgt_info.tgt_type;
	prog->aux->attach_func_name = tgt_info.tgt_name;

	if (tgt_prog) {
		prog->aux->saved_dst_prog_type = tgt_prog->type;
		prog->aux->saved_dst_attach_type = tgt_prog->expected_attach_type;
	}

	if (prog->expected_attach_type == BPF_TRACE_RAW_TP) {
		prog->aux->attach_btf_trace = true;
		return 0;
	} else if (prog->expected_attach_type == BPF_TRACE_ITER) {
		if (!bpf_iter_prog_supported(prog))
			return -EINVAL;
		return 0;
	}

	if (prog->type == BPF_PROG_TYPE_LSM) {
		ret = bpf_lsm_verify_prog(&env->log, prog);
		if (ret < 0)
			return ret;
	}

	key = bpf_trampoline_compute_key(tgt_prog, prog->aux->attach_btf, btf_id);
	tr = bpf_trampoline_get(key, &tgt_info);
	if (!tr)
		return -ENOMEM;

	prog->aux->dst_trampoline = tr;
	return 0;
}

struct btf *bpf_get_btf_vmlinux(void)
{
	if (!btf_vmlinux && IS_ENABLED(CONFIG_DEBUG_INFO_BTF)) {
		mutex_lock(&bpf_verifier_lock);
		if (!btf_vmlinux)
			btf_vmlinux = btf_parse_vmlinux();
		mutex_unlock(&bpf_verifier_lock);
	}
	return btf_vmlinux;
}

int bpf_check(struct bpf_prog **prog, union bpf_attr *attr,
	      union bpf_attr __user *uattr)
{
	u64 start_time = ktime_get_ns();
	struct bpf_verifier_env *env;
	struct bpf_verifier_log *log;
	int i, len, ret = -EINVAL;
	bool is_priv;

	/* no program is valid */
	if (ARRAY_SIZE(bpf_verifier_ops) == 0)
		return -EINVAL;

	/* 'struct bpf_verifier_env' can be global, but since it's not small,
	 * allocate/free it every time bpf_check() is called
	 */
	env = kzalloc(sizeof(struct bpf_verifier_env), GFP_KERNEL);
	if (!env)
		return -ENOMEM;
	log = &env->log;

	len = (*prog)->len;
	env->insn_aux_data =
		vzalloc(array_size(sizeof(struct bpf_insn_aux_data), len));
	ret = -ENOMEM;
	if (!env->insn_aux_data)
		goto err_free_env;
	for (i = 0; i < len; i++)
		env->insn_aux_data[i].orig_idx = i;
	env->prog = *prog;
	env->ops = bpf_verifier_ops[env->prog->type];
	is_priv = bpf_capable();

	bpf_get_btf_vmlinux();

	/* grab the mutex to protect few globals used by verifier */
	if (!is_priv)
		mutex_lock(&bpf_verifier_lock);

	if (attr->log_level || attr->log_buf || attr->log_size) {
		/* user requested verbose verifier output
		 * and supplied buffer to store the verification trace
		 */
		log->level = attr->log_level;
		log->ubuf = (char __user *) (unsigned long) attr->log_buf;
		log->len_total = attr->log_size;

		ret = -EINVAL;
		/* log attributes have to be sane */
		if (log->len_total < 128 || log->len_total > UINT_MAX >> 2 ||
		    !log->level || !log->ubuf || log->level & ~BPF_LOG_MASK)
			goto err_unlock;
	}

	if (IS_ERR(btf_vmlinux)) {
		/* Either gcc or pahole or kernel are broken. */
		verbose(env, "in-kernel BTF is malformed\n");
		ret = PTR_ERR(btf_vmlinux);
		goto skip_full_check;
	}

	env->strict_alignment = !!(attr->prog_flags & BPF_F_STRICT_ALIGNMENT);
	if (!IS_ENABLED(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS))
		env->strict_alignment = true;
	if (attr->prog_flags & BPF_F_ANY_ALIGNMENT)
		env->strict_alignment = false;

	env->allow_ptr_leaks = bpf_allow_ptr_leaks();
	env->allow_uninit_stack = bpf_allow_uninit_stack();
	env->allow_ptr_to_map_access = bpf_allow_ptr_to_map_access();
	env->bypass_spec_v1 = bpf_bypass_spec_v1();
	env->bypass_spec_v4 = bpf_bypass_spec_v4();
	env->bpf_capable = bpf_capable();

	if (is_priv)
		env->test_state_freq = attr->prog_flags & BPF_F_TEST_STATE_FREQ;

	if (bpf_prog_is_dev_bound(env->prog->aux)) {
		ret = bpf_prog_offload_verifier_prep(env->prog);
		if (ret)
			goto skip_full_check;
	}

	env->explored_states = kvcalloc(state_htab_size(env),
				       sizeof(struct bpf_verifier_state_list *),
				       GFP_USER);
	ret = -ENOMEM;
	if (!env->explored_states)
		goto skip_full_check;

	ret = check_subprogs(env);
	if (ret < 0)
		goto skip_full_check;

	ret = check_btf_info(env, attr, uattr);
	if (ret < 0)
		goto skip_full_check;

	ret = check_attach_btf_id(env);
	if (ret)
		goto skip_full_check;

	ret = resolve_pseudo_ldimm64(env);
	if (ret < 0)
		goto skip_full_check;

	ret = check_cfg(env);
	if (ret < 0)
		goto skip_full_check;

	ret = do_check_subprogs(env);
	ret = ret ?: do_check_main(env);

	if (ret == 0 && bpf_prog_is_dev_bound(env->prog->aux))
		ret = bpf_prog_offload_finalize(env);

skip_full_check:
	kvfree(env->explored_states);

	if (ret == 0)
		ret = check_max_stack_depth(env);

	/* instruction rewrites happen after this point */
	if (is_priv) {
		if (ret == 0)
			opt_hard_wire_dead_code_branches(env);
		if (ret == 0)
			ret = opt_remove_dead_code(env);
		if (ret == 0)
			ret = opt_remove_nops(env);
	} else {
		if (ret == 0)
			sanitize_dead_code(env);
	}

	if (ret == 0)
		/* program is valid, convert *(u32*)(ctx + off) accesses */
		ret = convert_ctx_accesses(env);

	if (ret == 0)
		ret = fixup_bpf_calls(env);

	/* do 32-bit optimization after insn patching has done so those patched
	 * insns could be handled correctly.
	 */
	if (ret == 0 && !bpf_prog_is_dev_bound(env->prog->aux)) {
		ret = opt_subreg_zext_lo32_rnd_hi32(env, attr);
		env->prog->aux->verifier_zext = bpf_jit_needs_zext() ? !ret
								     : false;
	}

	if (ret == 0)
		ret = fixup_call_args(env);

	env->verification_time = ktime_get_ns() - start_time;
	print_verification_stats(env);

	if (log->level && bpf_verifier_log_full(log))
		ret = -ENOSPC;
	if (log->level && !log->ubuf) {
		ret = -EFAULT;
		goto err_release_maps;
	}

	if (ret)
		goto err_release_maps;

	if (env->used_map_cnt) {
		/* if program passed verifier, update used_maps in bpf_prog_info */
		env->prog->aux->used_maps = kmalloc_array(env->used_map_cnt,
							  sizeof(env->used_maps[0]),
							  GFP_KERNEL);

		if (!env->prog->aux->used_maps) {
			ret = -ENOMEM;
			goto err_release_maps;
		}

		memcpy(env->prog->aux->used_maps, env->used_maps,
		       sizeof(env->used_maps[0]) * env->used_map_cnt);
		env->prog->aux->used_map_cnt = env->used_map_cnt;
	}
	if (env->used_btf_cnt) {
		/* if program passed verifier, update used_btfs in bpf_prog_aux */
		env->prog->aux->used_btfs = kmalloc_array(env->used_btf_cnt,
							  sizeof(env->used_btfs[0]),
							  GFP_KERNEL);
		if (!env->prog->aux->used_btfs) {
			ret = -ENOMEM;
			goto err_release_maps;
		}

		memcpy(env->prog->aux->used_btfs, env->used_btfs,
		       sizeof(env->used_btfs[0]) * env->used_btf_cnt);
		env->prog->aux->used_btf_cnt = env->used_btf_cnt;
	}
	if (env->used_map_cnt || env->used_btf_cnt) {
		/* program is valid. Convert pseudo bpf_ld_imm64 into generic
		 * bpf_ld_imm64 instructions
		 */
		convert_pseudo_ld_imm64(env);
	}

	adjust_btf_func(env);

err_release_maps:
	if (!env->prog->aux->used_maps)
		/* if we didn't copy map pointers into bpf_prog_info, release
		 * them now. Otherwise free_used_maps() will release them.
		 */
		release_maps(env);
	if (!env->prog->aux->used_btfs)
		release_btfs(env);

	/* extension progs temporarily inherit the attach_type of their targets
	   for verification purposes, so set it back to zero before returning
	 */
	if (env->prog->type == BPF_PROG_TYPE_EXT)
		env->prog->expected_attach_type = 0;

	*prog = env->prog;
err_unlock:
	if (!is_priv)
		mutex_unlock(&bpf_verifier_lock);
	vfree(env->insn_aux_data);
err_free_env:
	kfree(env);
	return ret;
}
