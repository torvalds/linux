// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 * Copyright (c) 2018 Covalent IO, Inc. http://covalent.io
 */
#include <uapi/linux/btf.h>
#include <linux/bpf-cgroup.h>
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
#include <linux/poison.h>
#include <linux/module.h>
#include <linux/cpumask.h>
#include <linux/bpf_mem_alloc.h>
#include <net/xdp.h>
#include <linux/trace_events.h>
#include <linux/kallsyms.h>

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

struct bpf_mem_alloc bpf_global_percpu_ma;
static bool bpf_global_percpu_ma_set;

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
 * Since it's analyzing all paths through the program, the length of the
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
 * returns either pointer to map value or NULL.
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
	/* verifier state is 'st'
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

#define BPF_GLOBAL_PERCPU_MA_MAX_SIZE  512

#define BPF_PRIV_STACK_MIN_SIZE		64

static int acquire_reference_state(struct bpf_verifier_env *env, int insn_idx);
static int release_reference(struct bpf_verifier_env *env, int ref_obj_id);
static void invalidate_non_owning_refs(struct bpf_verifier_env *env);
static bool in_rbtree_lock_required_cb(struct bpf_verifier_env *env);
static int ref_set_non_owning(struct bpf_verifier_env *env,
			      struct bpf_reg_state *reg);
static void specialize_kfunc(struct bpf_verifier_env *env,
			     u32 func_id, u16 offset, unsigned long *addr);
static bool is_trusted_reg(const struct bpf_reg_state *reg);

static bool bpf_map_ptr_poisoned(const struct bpf_insn_aux_data *aux)
{
	return aux->map_ptr_state.poison;
}

static bool bpf_map_ptr_unpriv(const struct bpf_insn_aux_data *aux)
{
	return aux->map_ptr_state.unpriv;
}

static void bpf_map_ptr_store(struct bpf_insn_aux_data *aux,
			      struct bpf_map *map,
			      bool unpriv, bool poison)
{
	unpriv |= bpf_map_ptr_unpriv(aux);
	aux->map_ptr_state.unpriv = unpriv;
	aux->map_ptr_state.poison = poison;
	aux->map_ptr_state.map_ptr = map;
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

static bool bpf_helper_call(const struct bpf_insn *insn)
{
	return insn->code == (BPF_JMP | BPF_CALL) &&
	       insn->src_reg == 0;
}

static bool bpf_pseudo_call(const struct bpf_insn *insn)
{
	return insn->code == (BPF_JMP | BPF_CALL) &&
	       insn->src_reg == BPF_PSEUDO_CALL;
}

static bool bpf_pseudo_kfunc_call(const struct bpf_insn *insn)
{
	return insn->code == (BPF_JMP | BPF_CALL) &&
	       insn->src_reg == BPF_PSEUDO_KFUNC_CALL;
}

struct bpf_call_arg_meta {
	struct bpf_map *map_ptr;
	bool raw_mode;
	bool pkt_access;
	u8 release_regno;
	int regno;
	int access_size;
	int mem_size;
	u64 msize_max_value;
	int ref_obj_id;
	int dynptr_id;
	int map_uid;
	int func_id;
	struct btf *btf;
	u32 btf_id;
	struct btf *ret_btf;
	u32 ret_btf_id;
	u32 subprogno;
	struct btf_field *kptr_field;
};

struct bpf_kfunc_call_arg_meta {
	/* In parameters */
	struct btf *btf;
	u32 func_id;
	u32 kfunc_flags;
	const struct btf_type *func_proto;
	const char *func_name;
	/* Out parameters */
	u32 ref_obj_id;
	u8 release_regno;
	bool r0_rdonly;
	u32 ret_btf_id;
	u64 r0_size;
	u32 subprogno;
	struct {
		u64 value;
		bool found;
	} arg_constant;

	/* arg_{btf,btf_id,owning_ref} are used by kfunc-specific handling,
	 * generally to pass info about user-defined local kptr types to later
	 * verification logic
	 *   bpf_obj_drop/bpf_percpu_obj_drop
	 *     Record the local kptr type to be drop'd
	 *   bpf_refcount_acquire (via KF_ARG_PTR_TO_REFCOUNTED_KPTR arg type)
	 *     Record the local kptr type to be refcount_incr'd and use
	 *     arg_owning_ref to determine whether refcount_acquire should be
	 *     fallible
	 */
	struct btf *arg_btf;
	u32 arg_btf_id;
	bool arg_owning_ref;

	struct {
		struct btf_field *field;
	} arg_list_head;
	struct {
		struct btf_field *field;
	} arg_rbtree_root;
	struct {
		enum bpf_dynptr_type type;
		u32 id;
		u32 ref_obj_id;
	} initialized_dynptr;
	struct {
		u8 spi;
		u8 frameno;
	} iter;
	struct {
		struct bpf_map *ptr;
		int uid;
	} map;
	u64 mem_size;
};

struct btf *btf_vmlinux;

static const char *btf_type_name(const struct btf *btf, u32 id)
{
	return btf_name_by_offset(btf, btf_type_by_id(btf, id)->name_off);
}

static DEFINE_MUTEX(bpf_verifier_lock);
static DEFINE_MUTEX(bpf_percpu_ma_lock);

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

static void verbose_invalid_scalar(struct bpf_verifier_env *env,
				   struct bpf_reg_state *reg,
				   struct bpf_retval_range range, const char *ctx,
				   const char *reg_name)
{
	bool unknown = true;

	verbose(env, "%s the register %s has", ctx, reg_name);
	if (reg->smin_value > S64_MIN) {
		verbose(env, " smin=%lld", reg->smin_value);
		unknown = false;
	}
	if (reg->smax_value < S64_MAX) {
		verbose(env, " smax=%lld", reg->smax_value);
		unknown = false;
	}
	if (unknown)
		verbose(env, " unknown scalar value");
	verbose(env, " should have been in [%d, %d]\n", range.minval, range.maxval);
}

static bool reg_not_null(const struct bpf_reg_state *reg)
{
	enum bpf_reg_type type;

	type = reg->type;
	if (type_may_be_null(type))
		return false;

	type = base_type(type);
	return type == PTR_TO_SOCKET ||
		type == PTR_TO_TCP_SOCK ||
		type == PTR_TO_MAP_VALUE ||
		type == PTR_TO_MAP_KEY ||
		type == PTR_TO_SOCK_COMMON ||
		(type == PTR_TO_BTF_ID && is_trusted_reg(reg)) ||
		type == PTR_TO_MEM;
}

static struct btf_record *reg_btf_record(const struct bpf_reg_state *reg)
{
	struct btf_record *rec = NULL;
	struct btf_struct_meta *meta;

	if (reg->type == PTR_TO_MAP_VALUE) {
		rec = reg->map_ptr->record;
	} else if (type_is_ptr_alloc_obj(reg->type)) {
		meta = btf_find_struct_meta(reg->btf, reg->btf_id);
		if (meta)
			rec = meta->record;
	}
	return rec;
}

static bool mask_raw_tp_reg_cond(const struct bpf_verifier_env *env, struct bpf_reg_state *reg) {
	return reg->type == (PTR_TO_BTF_ID | PTR_TRUSTED | PTR_MAYBE_NULL) &&
	       bpf_prog_is_raw_tp(env->prog) && !reg->ref_obj_id;
}

static bool mask_raw_tp_reg(const struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	if (!mask_raw_tp_reg_cond(env, reg))
		return false;
	reg->type &= ~PTR_MAYBE_NULL;
	return true;
}

static void unmask_raw_tp_reg(struct bpf_reg_state *reg, bool result)
{
	if (result)
		reg->type |= PTR_MAYBE_NULL;
}

static bool subprog_is_global(const struct bpf_verifier_env *env, int subprog)
{
	struct bpf_func_info_aux *aux = env->prog->aux->func_info_aux;

	return aux && aux[subprog].linkage == BTF_FUNC_GLOBAL;
}

static const char *subprog_name(const struct bpf_verifier_env *env, int subprog)
{
	struct bpf_func_info *info;

	if (!env->prog->aux->func_info)
		return "";

	info = &env->prog->aux->func_info[subprog];
	return btf_type_name(env->prog->aux->btf, info->type_id);
}

static void mark_subprog_exc_cb(struct bpf_verifier_env *env, int subprog)
{
	struct bpf_subprog_info *info = subprog_info(env, subprog);

	info->is_cb = true;
	info->is_async_cb = true;
	info->is_exception_cb = true;
}

static bool subprog_is_exc_cb(struct bpf_verifier_env *env, int subprog)
{
	return subprog_info(env, subprog)->is_exception_cb;
}

static bool reg_may_point_to_spin_lock(const struct bpf_reg_state *reg)
{
	return btf_record_has_field(reg_btf_record(reg), BPF_SPIN_LOCK);
}

static bool type_is_rdonly_mem(u32 type)
{
	return type & MEM_RDONLY;
}

static bool is_acquire_function(enum bpf_func_id func_id,
				const struct bpf_map *map)
{
	enum bpf_map_type map_type = map ? map->map_type : BPF_MAP_TYPE_UNSPEC;

	if (func_id == BPF_FUNC_sk_lookup_tcp ||
	    func_id == BPF_FUNC_sk_lookup_udp ||
	    func_id == BPF_FUNC_skc_lookup_tcp ||
	    func_id == BPF_FUNC_ringbuf_reserve ||
	    func_id == BPF_FUNC_kptr_xchg)
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
		func_id == BPF_FUNC_skc_to_mptcp_sock ||
		func_id == BPF_FUNC_skc_to_tcp_timewait_sock ||
		func_id == BPF_FUNC_skc_to_tcp_request_sock;
}

static bool is_dynptr_ref_function(enum bpf_func_id func_id)
{
	return func_id == BPF_FUNC_dynptr_data;
}

static bool is_sync_callback_calling_kfunc(u32 btf_id);
static bool is_async_callback_calling_kfunc(u32 btf_id);
static bool is_callback_calling_kfunc(u32 btf_id);
static bool is_bpf_throw_kfunc(struct bpf_insn *insn);

static bool is_bpf_wq_set_callback_impl_kfunc(u32 btf_id);

static bool is_sync_callback_calling_function(enum bpf_func_id func_id)
{
	return func_id == BPF_FUNC_for_each_map_elem ||
	       func_id == BPF_FUNC_find_vma ||
	       func_id == BPF_FUNC_loop ||
	       func_id == BPF_FUNC_user_ringbuf_drain;
}

static bool is_async_callback_calling_function(enum bpf_func_id func_id)
{
	return func_id == BPF_FUNC_timer_set_callback;
}

static bool is_callback_calling_function(enum bpf_func_id func_id)
{
	return is_sync_callback_calling_function(func_id) ||
	       is_async_callback_calling_function(func_id);
}

static bool is_sync_callback_calling_insn(struct bpf_insn *insn)
{
	return (bpf_helper_call(insn) && is_sync_callback_calling_function(insn->imm)) ||
	       (bpf_pseudo_kfunc_call(insn) && is_sync_callback_calling_kfunc(insn->imm));
}

static bool is_async_callback_calling_insn(struct bpf_insn *insn)
{
	return (bpf_helper_call(insn) && is_async_callback_calling_function(insn->imm)) ||
	       (bpf_pseudo_kfunc_call(insn) && is_async_callback_calling_kfunc(insn->imm));
}

static bool is_may_goto_insn(struct bpf_insn *insn)
{
	return insn->code == (BPF_JMP | BPF_JCOND) && insn->src_reg == BPF_MAY_GOTO;
}

static bool is_may_goto_insn_at(struct bpf_verifier_env *env, int insn_idx)
{
	return is_may_goto_insn(&env->prog->insnsi[insn_idx]);
}

static bool is_storage_get_function(enum bpf_func_id func_id)
{
	return func_id == BPF_FUNC_sk_storage_get ||
	       func_id == BPF_FUNC_inode_storage_get ||
	       func_id == BPF_FUNC_task_storage_get ||
	       func_id == BPF_FUNC_cgrp_storage_get;
}

static bool helper_multiple_ref_obj_use(enum bpf_func_id func_id,
					const struct bpf_map *map)
{
	int ref_obj_uses = 0;

	if (is_ptr_cast_function(func_id))
		ref_obj_uses++;
	if (is_acquire_function(func_id, map))
		ref_obj_uses++;
	if (is_dynptr_ref_function(func_id))
		ref_obj_uses++;

	return ref_obj_uses > 1;
}

static bool is_cmpxchg_insn(const struct bpf_insn *insn)
{
	return BPF_CLASS(insn->code) == BPF_STX &&
	       BPF_MODE(insn->code) == BPF_ATOMIC &&
	       insn->imm == BPF_CMPXCHG;
}

static int __get_spi(s32 off)
{
	return (-off - 1) / BPF_REG_SIZE;
}

static struct bpf_func_state *func(struct bpf_verifier_env *env,
				   const struct bpf_reg_state *reg)
{
	struct bpf_verifier_state *cur = env->cur_state;

	return cur->frame[reg->frameno];
}

static bool is_spi_bounds_valid(struct bpf_func_state *state, int spi, int nr_slots)
{
       int allocated_slots = state->allocated_stack / BPF_REG_SIZE;

       /* We need to check that slots between [spi - nr_slots + 1, spi] are
	* within [0, allocated_stack).
	*
	* Please note that the spi grows downwards. For example, a dynptr
	* takes the size of two stack slots; the first slot will be at
	* spi and the second slot will be at spi - 1.
	*/
       return spi - nr_slots + 1 >= 0 && spi < allocated_slots;
}

static int stack_slot_obj_get_spi(struct bpf_verifier_env *env, struct bpf_reg_state *reg,
			          const char *obj_kind, int nr_slots)
{
	int off, spi;

	if (!tnum_is_const(reg->var_off)) {
		verbose(env, "%s has to be at a constant offset\n", obj_kind);
		return -EINVAL;
	}

	off = reg->off + reg->var_off.value;
	if (off % BPF_REG_SIZE) {
		verbose(env, "cannot pass in %s at an offset=%d\n", obj_kind, off);
		return -EINVAL;
	}

	spi = __get_spi(off);
	if (spi + 1 < nr_slots) {
		verbose(env, "cannot pass in %s at an offset=%d\n", obj_kind, off);
		return -EINVAL;
	}

	if (!is_spi_bounds_valid(func(env, reg), spi, nr_slots))
		return -ERANGE;
	return spi;
}

static int dynptr_get_spi(struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	return stack_slot_obj_get_spi(env, reg, "dynptr", BPF_DYNPTR_NR_SLOTS);
}

static int iter_get_spi(struct bpf_verifier_env *env, struct bpf_reg_state *reg, int nr_slots)
{
	return stack_slot_obj_get_spi(env, reg, "iter", nr_slots);
}

static enum bpf_dynptr_type arg_to_dynptr_type(enum bpf_arg_type arg_type)
{
	switch (arg_type & DYNPTR_TYPE_FLAG_MASK) {
	case DYNPTR_TYPE_LOCAL:
		return BPF_DYNPTR_TYPE_LOCAL;
	case DYNPTR_TYPE_RINGBUF:
		return BPF_DYNPTR_TYPE_RINGBUF;
	case DYNPTR_TYPE_SKB:
		return BPF_DYNPTR_TYPE_SKB;
	case DYNPTR_TYPE_XDP:
		return BPF_DYNPTR_TYPE_XDP;
	default:
		return BPF_DYNPTR_TYPE_INVALID;
	}
}

static enum bpf_type_flag get_dynptr_type_flag(enum bpf_dynptr_type type)
{
	switch (type) {
	case BPF_DYNPTR_TYPE_LOCAL:
		return DYNPTR_TYPE_LOCAL;
	case BPF_DYNPTR_TYPE_RINGBUF:
		return DYNPTR_TYPE_RINGBUF;
	case BPF_DYNPTR_TYPE_SKB:
		return DYNPTR_TYPE_SKB;
	case BPF_DYNPTR_TYPE_XDP:
		return DYNPTR_TYPE_XDP;
	default:
		return 0;
	}
}

static bool dynptr_type_refcounted(enum bpf_dynptr_type type)
{
	return type == BPF_DYNPTR_TYPE_RINGBUF;
}

static void __mark_dynptr_reg(struct bpf_reg_state *reg,
			      enum bpf_dynptr_type type,
			      bool first_slot, int dynptr_id);

static void __mark_reg_not_init(const struct bpf_verifier_env *env,
				struct bpf_reg_state *reg);

static void mark_dynptr_stack_regs(struct bpf_verifier_env *env,
				   struct bpf_reg_state *sreg1,
				   struct bpf_reg_state *sreg2,
				   enum bpf_dynptr_type type)
{
	int id = ++env->id_gen;

	__mark_dynptr_reg(sreg1, type, true, id);
	__mark_dynptr_reg(sreg2, type, false, id);
}

static void mark_dynptr_cb_reg(struct bpf_verifier_env *env,
			       struct bpf_reg_state *reg,
			       enum bpf_dynptr_type type)
{
	__mark_dynptr_reg(reg, type, true, ++env->id_gen);
}

static int destroy_if_dynptr_stack_slot(struct bpf_verifier_env *env,
				        struct bpf_func_state *state, int spi);

static int mark_stack_slots_dynptr(struct bpf_verifier_env *env, struct bpf_reg_state *reg,
				   enum bpf_arg_type arg_type, int insn_idx, int clone_ref_obj_id)
{
	struct bpf_func_state *state = func(env, reg);
	enum bpf_dynptr_type type;
	int spi, i, err;

	spi = dynptr_get_spi(env, reg);
	if (spi < 0)
		return spi;

	/* We cannot assume both spi and spi - 1 belong to the same dynptr,
	 * hence we need to call destroy_if_dynptr_stack_slot twice for both,
	 * to ensure that for the following example:
	 *	[d1][d1][d2][d2]
	 * spi    3   2   1   0
	 * So marking spi = 2 should lead to destruction of both d1 and d2. In
	 * case they do belong to same dynptr, second call won't see slot_type
	 * as STACK_DYNPTR and will simply skip destruction.
	 */
	err = destroy_if_dynptr_stack_slot(env, state, spi);
	if (err)
		return err;
	err = destroy_if_dynptr_stack_slot(env, state, spi - 1);
	if (err)
		return err;

	for (i = 0; i < BPF_REG_SIZE; i++) {
		state->stack[spi].slot_type[i] = STACK_DYNPTR;
		state->stack[spi - 1].slot_type[i] = STACK_DYNPTR;
	}

	type = arg_to_dynptr_type(arg_type);
	if (type == BPF_DYNPTR_TYPE_INVALID)
		return -EINVAL;

	mark_dynptr_stack_regs(env, &state->stack[spi].spilled_ptr,
			       &state->stack[spi - 1].spilled_ptr, type);

	if (dynptr_type_refcounted(type)) {
		/* The id is used to track proper releasing */
		int id;

		if (clone_ref_obj_id)
			id = clone_ref_obj_id;
		else
			id = acquire_reference_state(env, insn_idx);

		if (id < 0)
			return id;

		state->stack[spi].spilled_ptr.ref_obj_id = id;
		state->stack[spi - 1].spilled_ptr.ref_obj_id = id;
	}

	state->stack[spi].spilled_ptr.live |= REG_LIVE_WRITTEN;
	state->stack[spi - 1].spilled_ptr.live |= REG_LIVE_WRITTEN;

	return 0;
}

static void invalidate_dynptr(struct bpf_verifier_env *env, struct bpf_func_state *state, int spi)
{
	int i;

	for (i = 0; i < BPF_REG_SIZE; i++) {
		state->stack[spi].slot_type[i] = STACK_INVALID;
		state->stack[spi - 1].slot_type[i] = STACK_INVALID;
	}

	__mark_reg_not_init(env, &state->stack[spi].spilled_ptr);
	__mark_reg_not_init(env, &state->stack[spi - 1].spilled_ptr);

	/* Why do we need to set REG_LIVE_WRITTEN for STACK_INVALID slot?
	 *
	 * While we don't allow reading STACK_INVALID, it is still possible to
	 * do <8 byte writes marking some but not all slots as STACK_MISC. Then,
	 * helpers or insns can do partial read of that part without failing,
	 * but check_stack_range_initialized, check_stack_read_var_off, and
	 * check_stack_read_fixed_off will do mark_reg_read for all 8-bytes of
	 * the slot conservatively. Hence we need to prevent those liveness
	 * marking walks.
	 *
	 * This was not a problem before because STACK_INVALID is only set by
	 * default (where the default reg state has its reg->parent as NULL), or
	 * in clean_live_states after REG_LIVE_DONE (at which point
	 * mark_reg_read won't walk reg->parent chain), but not randomly during
	 * verifier state exploration (like we did above). Hence, for our case
	 * parentage chain will still be live (i.e. reg->parent may be
	 * non-NULL), while earlier reg->parent was NULL, so we need
	 * REG_LIVE_WRITTEN to screen off read marker propagation when it is
	 * done later on reads or by mark_dynptr_read as well to unnecessary
	 * mark registers in verifier state.
	 */
	state->stack[spi].spilled_ptr.live |= REG_LIVE_WRITTEN;
	state->stack[spi - 1].spilled_ptr.live |= REG_LIVE_WRITTEN;
}

static int unmark_stack_slots_dynptr(struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	struct bpf_func_state *state = func(env, reg);
	int spi, ref_obj_id, i;

	spi = dynptr_get_spi(env, reg);
	if (spi < 0)
		return spi;

	if (!dynptr_type_refcounted(state->stack[spi].spilled_ptr.dynptr.type)) {
		invalidate_dynptr(env, state, spi);
		return 0;
	}

	ref_obj_id = state->stack[spi].spilled_ptr.ref_obj_id;

	/* If the dynptr has a ref_obj_id, then we need to invalidate
	 * two things:
	 *
	 * 1) Any dynptrs with a matching ref_obj_id (clones)
	 * 2) Any slices derived from this dynptr.
	 */

	/* Invalidate any slices associated with this dynptr */
	WARN_ON_ONCE(release_reference(env, ref_obj_id));

	/* Invalidate any dynptr clones */
	for (i = 1; i < state->allocated_stack / BPF_REG_SIZE; i++) {
		if (state->stack[i].spilled_ptr.ref_obj_id != ref_obj_id)
			continue;

		/* it should always be the case that if the ref obj id
		 * matches then the stack slot also belongs to a
		 * dynptr
		 */
		if (state->stack[i].slot_type[0] != STACK_DYNPTR) {
			verbose(env, "verifier internal error: misconfigured ref_obj_id\n");
			return -EFAULT;
		}
		if (state->stack[i].spilled_ptr.dynptr.first_slot)
			invalidate_dynptr(env, state, i);
	}

	return 0;
}

static void __mark_reg_unknown(const struct bpf_verifier_env *env,
			       struct bpf_reg_state *reg);

static void mark_reg_invalid(const struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	if (!env->allow_ptr_leaks)
		__mark_reg_not_init(env, reg);
	else
		__mark_reg_unknown(env, reg);
}

static int destroy_if_dynptr_stack_slot(struct bpf_verifier_env *env,
				        struct bpf_func_state *state, int spi)
{
	struct bpf_func_state *fstate;
	struct bpf_reg_state *dreg;
	int i, dynptr_id;

	/* We always ensure that STACK_DYNPTR is never set partially,
	 * hence just checking for slot_type[0] is enough. This is
	 * different for STACK_SPILL, where it may be only set for
	 * 1 byte, so code has to use is_spilled_reg.
	 */
	if (state->stack[spi].slot_type[0] != STACK_DYNPTR)
		return 0;

	/* Reposition spi to first slot */
	if (!state->stack[spi].spilled_ptr.dynptr.first_slot)
		spi = spi + 1;

	if (dynptr_type_refcounted(state->stack[spi].spilled_ptr.dynptr.type)) {
		verbose(env, "cannot overwrite referenced dynptr\n");
		return -EINVAL;
	}

	mark_stack_slot_scratched(env, spi);
	mark_stack_slot_scratched(env, spi - 1);

	/* Writing partially to one dynptr stack slot destroys both. */
	for (i = 0; i < BPF_REG_SIZE; i++) {
		state->stack[spi].slot_type[i] = STACK_INVALID;
		state->stack[spi - 1].slot_type[i] = STACK_INVALID;
	}

	dynptr_id = state->stack[spi].spilled_ptr.id;
	/* Invalidate any slices associated with this dynptr */
	bpf_for_each_reg_in_vstate(env->cur_state, fstate, dreg, ({
		/* Dynptr slices are only PTR_TO_MEM_OR_NULL and PTR_TO_MEM */
		if (dreg->type != (PTR_TO_MEM | PTR_MAYBE_NULL) && dreg->type != PTR_TO_MEM)
			continue;
		if (dreg->dynptr_id == dynptr_id)
			mark_reg_invalid(env, dreg);
	}));

	/* Do not release reference state, we are destroying dynptr on stack,
	 * not using some helper to release it. Just reset register.
	 */
	__mark_reg_not_init(env, &state->stack[spi].spilled_ptr);
	__mark_reg_not_init(env, &state->stack[spi - 1].spilled_ptr);

	/* Same reason as unmark_stack_slots_dynptr above */
	state->stack[spi].spilled_ptr.live |= REG_LIVE_WRITTEN;
	state->stack[spi - 1].spilled_ptr.live |= REG_LIVE_WRITTEN;

	return 0;
}

static bool is_dynptr_reg_valid_uninit(struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	int spi;

	if (reg->type == CONST_PTR_TO_DYNPTR)
		return false;

	spi = dynptr_get_spi(env, reg);

	/* -ERANGE (i.e. spi not falling into allocated stack slots) isn't an
	 * error because this just means the stack state hasn't been updated yet.
	 * We will do check_mem_access to check and update stack bounds later.
	 */
	if (spi < 0 && spi != -ERANGE)
		return false;

	/* We don't need to check if the stack slots are marked by previous
	 * dynptr initializations because we allow overwriting existing unreferenced
	 * STACK_DYNPTR slots, see mark_stack_slots_dynptr which calls
	 * destroy_if_dynptr_stack_slot to ensure dynptr objects at the slots we are
	 * touching are completely destructed before we reinitialize them for a new
	 * one. For referenced ones, destroy_if_dynptr_stack_slot returns an error early
	 * instead of delaying it until the end where the user will get "Unreleased
	 * reference" error.
	 */
	return true;
}

static bool is_dynptr_reg_valid_init(struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	struct bpf_func_state *state = func(env, reg);
	int i, spi;

	/* This already represents first slot of initialized bpf_dynptr.
	 *
	 * CONST_PTR_TO_DYNPTR already has fixed and var_off as 0 due to
	 * check_func_arg_reg_off's logic, so we don't need to check its
	 * offset and alignment.
	 */
	if (reg->type == CONST_PTR_TO_DYNPTR)
		return true;

	spi = dynptr_get_spi(env, reg);
	if (spi < 0)
		return false;
	if (!state->stack[spi].spilled_ptr.dynptr.first_slot)
		return false;

	for (i = 0; i < BPF_REG_SIZE; i++) {
		if (state->stack[spi].slot_type[i] != STACK_DYNPTR ||
		    state->stack[spi - 1].slot_type[i] != STACK_DYNPTR)
			return false;
	}

	return true;
}

static bool is_dynptr_type_expected(struct bpf_verifier_env *env, struct bpf_reg_state *reg,
				    enum bpf_arg_type arg_type)
{
	struct bpf_func_state *state = func(env, reg);
	enum bpf_dynptr_type dynptr_type;
	int spi;

	/* ARG_PTR_TO_DYNPTR takes any type of dynptr */
	if (arg_type == ARG_PTR_TO_DYNPTR)
		return true;

	dynptr_type = arg_to_dynptr_type(arg_type);
	if (reg->type == CONST_PTR_TO_DYNPTR) {
		return reg->dynptr.type == dynptr_type;
	} else {
		spi = dynptr_get_spi(env, reg);
		if (spi < 0)
			return false;
		return state->stack[spi].spilled_ptr.dynptr.type == dynptr_type;
	}
}

static void __mark_reg_known_zero(struct bpf_reg_state *reg);

static bool in_rcu_cs(struct bpf_verifier_env *env);

static bool is_kfunc_rcu_protected(struct bpf_kfunc_call_arg_meta *meta);

static int mark_stack_slots_iter(struct bpf_verifier_env *env,
				 struct bpf_kfunc_call_arg_meta *meta,
				 struct bpf_reg_state *reg, int insn_idx,
				 struct btf *btf, u32 btf_id, int nr_slots)
{
	struct bpf_func_state *state = func(env, reg);
	int spi, i, j, id;

	spi = iter_get_spi(env, reg, nr_slots);
	if (spi < 0)
		return spi;

	id = acquire_reference_state(env, insn_idx);
	if (id < 0)
		return id;

	for (i = 0; i < nr_slots; i++) {
		struct bpf_stack_state *slot = &state->stack[spi - i];
		struct bpf_reg_state *st = &slot->spilled_ptr;

		__mark_reg_known_zero(st);
		st->type = PTR_TO_STACK; /* we don't have dedicated reg type */
		if (is_kfunc_rcu_protected(meta)) {
			if (in_rcu_cs(env))
				st->type |= MEM_RCU;
			else
				st->type |= PTR_UNTRUSTED;
		}
		st->live |= REG_LIVE_WRITTEN;
		st->ref_obj_id = i == 0 ? id : 0;
		st->iter.btf = btf;
		st->iter.btf_id = btf_id;
		st->iter.state = BPF_ITER_STATE_ACTIVE;
		st->iter.depth = 0;

		for (j = 0; j < BPF_REG_SIZE; j++)
			slot->slot_type[j] = STACK_ITER;

		mark_stack_slot_scratched(env, spi - i);
	}

	return 0;
}

static int unmark_stack_slots_iter(struct bpf_verifier_env *env,
				   struct bpf_reg_state *reg, int nr_slots)
{
	struct bpf_func_state *state = func(env, reg);
	int spi, i, j;

	spi = iter_get_spi(env, reg, nr_slots);
	if (spi < 0)
		return spi;

	for (i = 0; i < nr_slots; i++) {
		struct bpf_stack_state *slot = &state->stack[spi - i];
		struct bpf_reg_state *st = &slot->spilled_ptr;

		if (i == 0)
			WARN_ON_ONCE(release_reference(env, st->ref_obj_id));

		__mark_reg_not_init(env, st);

		/* see unmark_stack_slots_dynptr() for why we need to set REG_LIVE_WRITTEN */
		st->live |= REG_LIVE_WRITTEN;

		for (j = 0; j < BPF_REG_SIZE; j++)
			slot->slot_type[j] = STACK_INVALID;

		mark_stack_slot_scratched(env, spi - i);
	}

	return 0;
}

static bool is_iter_reg_valid_uninit(struct bpf_verifier_env *env,
				     struct bpf_reg_state *reg, int nr_slots)
{
	struct bpf_func_state *state = func(env, reg);
	int spi, i, j;

	/* For -ERANGE (i.e. spi not falling into allocated stack slots), we
	 * will do check_mem_access to check and update stack bounds later, so
	 * return true for that case.
	 */
	spi = iter_get_spi(env, reg, nr_slots);
	if (spi == -ERANGE)
		return true;
	if (spi < 0)
		return false;

	for (i = 0; i < nr_slots; i++) {
		struct bpf_stack_state *slot = &state->stack[spi - i];

		for (j = 0; j < BPF_REG_SIZE; j++)
			if (slot->slot_type[j] == STACK_ITER)
				return false;
	}

	return true;
}

static int is_iter_reg_valid_init(struct bpf_verifier_env *env, struct bpf_reg_state *reg,
				   struct btf *btf, u32 btf_id, int nr_slots)
{
	struct bpf_func_state *state = func(env, reg);
	int spi, i, j;

	spi = iter_get_spi(env, reg, nr_slots);
	if (spi < 0)
		return -EINVAL;

	for (i = 0; i < nr_slots; i++) {
		struct bpf_stack_state *slot = &state->stack[spi - i];
		struct bpf_reg_state *st = &slot->spilled_ptr;

		if (st->type & PTR_UNTRUSTED)
			return -EPROTO;
		/* only main (first) slot has ref_obj_id set */
		if (i == 0 && !st->ref_obj_id)
			return -EINVAL;
		if (i != 0 && st->ref_obj_id)
			return -EINVAL;
		if (st->iter.btf != btf || st->iter.btf_id != btf_id)
			return -EINVAL;

		for (j = 0; j < BPF_REG_SIZE; j++)
			if (slot->slot_type[j] != STACK_ITER)
				return -EINVAL;
	}

	return 0;
}

/* Check if given stack slot is "special":
 *   - spilled register state (STACK_SPILL);
 *   - dynptr state (STACK_DYNPTR);
 *   - iter state (STACK_ITER).
 */
static bool is_stack_slot_special(const struct bpf_stack_state *stack)
{
	enum bpf_stack_slot_type type = stack->slot_type[BPF_REG_SIZE - 1];

	switch (type) {
	case STACK_SPILL:
	case STACK_DYNPTR:
	case STACK_ITER:
		return true;
	case STACK_INVALID:
	case STACK_MISC:
	case STACK_ZERO:
		return false;
	default:
		WARN_ONCE(1, "unknown stack slot type %d\n", type);
		return true;
	}
}

/* The reg state of a pointer or a bounded scalar was saved when
 * it was spilled to the stack.
 */
static bool is_spilled_reg(const struct bpf_stack_state *stack)
{
	return stack->slot_type[BPF_REG_SIZE - 1] == STACK_SPILL;
}

static bool is_spilled_scalar_reg(const struct bpf_stack_state *stack)
{
	return stack->slot_type[BPF_REG_SIZE - 1] == STACK_SPILL &&
	       stack->spilled_ptr.type == SCALAR_VALUE;
}

static bool is_spilled_scalar_reg64(const struct bpf_stack_state *stack)
{
	return stack->slot_type[0] == STACK_SPILL &&
	       stack->spilled_ptr.type == SCALAR_VALUE;
}

/* Mark stack slot as STACK_MISC, unless it is already STACK_INVALID, in which
 * case they are equivalent, or it's STACK_ZERO, in which case we preserve
 * more precise STACK_ZERO.
 * Regardless of allow_ptr_leaks setting (i.e., privileged or unprivileged
 * mode), we won't promote STACK_INVALID to STACK_MISC. In privileged case it is
 * unnecessary as both are considered equivalent when loading data and pruning,
 * in case of unprivileged mode it will be incorrect to allow reads of invalid
 * slots.
 */
static void mark_stack_slot_misc(struct bpf_verifier_env *env, u8 *stype)
{
	if (*stype == STACK_ZERO)
		return;
	if (*stype == STACK_INVALID)
		return;
	*stype = STACK_MISC;
}

static void scrub_spilled_slot(u8 *stype)
{
	if (*stype != STACK_INVALID)
		*stype = STACK_MISC;
}

/* copy array src of length n * size bytes to dst. dst is reallocated if it's too
 * small to hold src. This is different from krealloc since we don't want to preserve
 * the contents of dst.
 *
 * Leaves dst untouched if src is NULL or length is zero. Returns NULL if memory could
 * not be allocated.
 */
static void *copy_array(void *dst, const void *src, size_t n, size_t size, gfp_t flags)
{
	size_t alloc_bytes;
	void *orig = dst;
	size_t bytes;

	if (ZERO_OR_NULL_PTR(src))
		goto out;

	if (unlikely(check_mul_overflow(n, size, &bytes)))
		return NULL;

	alloc_bytes = max(ksize(orig), kmalloc_size_roundup(bytes));
	dst = krealloc(orig, alloc_bytes, flags);
	if (!dst) {
		kfree(orig);
		return NULL;
	}

	memcpy(dst, src, bytes);
out:
	return dst ? dst : ZERO_SIZE_PTR;
}

/* resize an array from old_n items to new_n items. the array is reallocated if it's too
 * small to hold new_n items. new items are zeroed out if the array grows.
 *
 * Contrary to krealloc_array, does not free arr if new_n is zero.
 */
static void *realloc_array(void *arr, size_t old_n, size_t new_n, size_t size)
{
	size_t alloc_size;
	void *new_arr;

	if (!new_n || old_n == new_n)
		goto out;

	alloc_size = kmalloc_size_roundup(size_mul(new_n, size));
	new_arr = krealloc(arr, alloc_size, GFP_KERNEL);
	if (!new_arr) {
		kfree(arr);
		return NULL;
	}
	arr = new_arr;

	if (new_n > old_n)
		memset(arr + old_n * size, 0, (new_n - old_n) * size);

out:
	return arr ? arr : ZERO_SIZE_PTR;
}

static int copy_reference_state(struct bpf_func_state *dst, const struct bpf_func_state *src)
{
	dst->refs = copy_array(dst->refs, src->refs, src->acquired_refs,
			       sizeof(struct bpf_reference_state), GFP_KERNEL);
	if (!dst->refs)
		return -ENOMEM;

	dst->active_locks = src->active_locks;
	dst->acquired_refs = src->acquired_refs;
	return 0;
}

static int copy_stack_state(struct bpf_func_state *dst, const struct bpf_func_state *src)
{
	size_t n = src->allocated_stack / BPF_REG_SIZE;

	dst->stack = copy_array(dst->stack, src->stack, n, sizeof(struct bpf_stack_state),
				GFP_KERNEL);
	if (!dst->stack)
		return -ENOMEM;

	dst->allocated_stack = src->allocated_stack;
	return 0;
}

static int resize_reference_state(struct bpf_func_state *state, size_t n)
{
	state->refs = realloc_array(state->refs, state->acquired_refs, n,
				    sizeof(struct bpf_reference_state));
	if (!state->refs)
		return -ENOMEM;

	state->acquired_refs = n;
	return 0;
}

/* Possibly update state->allocated_stack to be at least size bytes. Also
 * possibly update the function's high-water mark in its bpf_subprog_info.
 */
static int grow_stack_state(struct bpf_verifier_env *env, struct bpf_func_state *state, int size)
{
	size_t old_n = state->allocated_stack / BPF_REG_SIZE, n;

	/* The stack size is always a multiple of BPF_REG_SIZE. */
	size = round_up(size, BPF_REG_SIZE);
	n = size / BPF_REG_SIZE;

	if (old_n >= n)
		return 0;

	state->stack = realloc_array(state->stack, old_n, n, sizeof(struct bpf_stack_state));
	if (!state->stack)
		return -ENOMEM;

	state->allocated_stack = size;

	/* update known max for given subprogram */
	if (env->subprog_info[state->subprogno].stack_depth < size)
		env->subprog_info[state->subprogno].stack_depth = size;

	return 0;
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

	err = resize_reference_state(state, state->acquired_refs + 1);
	if (err)
		return err;
	id = ++env->id_gen;
	state->refs[new_ofs].type = REF_TYPE_PTR;
	state->refs[new_ofs].id = id;
	state->refs[new_ofs].insn_idx = insn_idx;

	return id;
}

static int acquire_lock_state(struct bpf_verifier_env *env, int insn_idx, enum ref_state_type type,
			      int id, void *ptr)
{
	struct bpf_func_state *state = cur_func(env);
	int new_ofs = state->acquired_refs;
	int err;

	err = resize_reference_state(state, state->acquired_refs + 1);
	if (err)
		return err;
	state->refs[new_ofs].type = type;
	state->refs[new_ofs].id = id;
	state->refs[new_ofs].insn_idx = insn_idx;
	state->refs[new_ofs].ptr = ptr;

	state->active_locks++;
	return 0;
}

/* release function corresponding to acquire_reference_state(). Idempotent. */
static int release_reference_state(struct bpf_func_state *state, int ptr_id)
{
	int i, last_idx;

	last_idx = state->acquired_refs - 1;
	for (i = 0; i < state->acquired_refs; i++) {
		if (state->refs[i].type != REF_TYPE_PTR)
			continue;
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

static int release_lock_state(struct bpf_func_state *state, int type, int id, void *ptr)
{
	int i, last_idx;

	last_idx = state->acquired_refs - 1;
	for (i = 0; i < state->acquired_refs; i++) {
		if (state->refs[i].type != type)
			continue;
		if (state->refs[i].id == id && state->refs[i].ptr == ptr) {
			if (last_idx && i != last_idx)
				memcpy(&state->refs[i], &state->refs[last_idx],
				       sizeof(*state->refs));
			memset(&state->refs[last_idx], 0, sizeof(*state->refs));
			state->acquired_refs--;
			state->active_locks--;
			return 0;
		}
	}
	return -EINVAL;
}

static struct bpf_reference_state *find_lock_state(struct bpf_verifier_env *env, enum ref_state_type type,
						   int id, void *ptr)
{
	struct bpf_func_state *state = cur_func(env);
	int i;

	for (i = 0; i < state->acquired_refs; i++) {
		struct bpf_reference_state *s = &state->refs[i];

		if (s->type == REF_TYPE_PTR || s->type != type)
			continue;

		if (s->id == id && s->ptr == ptr)
			return s;
	}
	return NULL;
}

static void free_func_state(struct bpf_func_state *state)
{
	if (!state)
		return;
	kfree(state->refs);
	kfree(state->stack);
	kfree(state);
}

static void free_verifier_state(struct bpf_verifier_state *state,
				bool free_self)
{
	int i;

	for (i = 0; i <= state->curframe; i++) {
		free_func_state(state->frame[i]);
		state->frame[i] = NULL;
	}
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
	int i, err;

	/* if dst has more stack frames then src frame, free them, this is also
	 * necessary in case of exceptional exits using bpf_throw.
	 */
	for (i = src->curframe + 1; i <= dst_state->curframe; i++) {
		free_func_state(dst_state->frame[i]);
		dst_state->frame[i] = NULL;
	}
	dst_state->speculative = src->speculative;
	dst_state->active_rcu_lock = src->active_rcu_lock;
	dst_state->active_preempt_lock = src->active_preempt_lock;
	dst_state->in_sleepable = src->in_sleepable;
	dst_state->curframe = src->curframe;
	dst_state->branches = src->branches;
	dst_state->parent = src->parent;
	dst_state->first_insn_idx = src->first_insn_idx;
	dst_state->last_insn_idx = src->last_insn_idx;
	dst_state->insn_hist_start = src->insn_hist_start;
	dst_state->insn_hist_end = src->insn_hist_end;
	dst_state->dfs_depth = src->dfs_depth;
	dst_state->callback_unroll_depth = src->callback_unroll_depth;
	dst_state->used_as_loop_entry = src->used_as_loop_entry;
	dst_state->may_goto_depth = src->may_goto_depth;
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

static u32 state_htab_size(struct bpf_verifier_env *env)
{
	return env->prog->len;
}

static struct bpf_verifier_state_list **explored_state(struct bpf_verifier_env *env, int idx)
{
	struct bpf_verifier_state *cur = env->cur_state;
	struct bpf_func_state *state = cur->frame[cur->curframe];

	return &env->explored_states[(idx ^ state->callsite) % state_htab_size(env)];
}

static bool same_callsites(struct bpf_verifier_state *a, struct bpf_verifier_state *b)
{
	int fr;

	if (a->curframe != b->curframe)
		return false;

	for (fr = a->curframe; fr >= 0; fr--)
		if (a->frame[fr]->callsite != b->frame[fr]->callsite)
			return false;

	return true;
}

/* Open coded iterators allow back-edges in the state graph in order to
 * check unbounded loops that iterators.
 *
 * In is_state_visited() it is necessary to know if explored states are
 * part of some loops in order to decide whether non-exact states
 * comparison could be used:
 * - non-exact states comparison establishes sub-state relation and uses
 *   read and precision marks to do so, these marks are propagated from
 *   children states and thus are not guaranteed to be final in a loop;
 * - exact states comparison just checks if current and explored states
 *   are identical (and thus form a back-edge).
 *
 * Paper "A New Algorithm for Identifying Loops in Decompilation"
 * by Tao Wei, Jian Mao, Wei Zou and Yu Chen [1] presents a convenient
 * algorithm for loop structure detection and gives an overview of
 * relevant terminology. It also has helpful illustrations.
 *
 * [1] https://api.semanticscholar.org/CorpusID:15784067
 *
 * We use a similar algorithm but because loop nested structure is
 * irrelevant for verifier ours is significantly simpler and resembles
 * strongly connected components algorithm from Sedgewick's textbook.
 *
 * Define topmost loop entry as a first node of the loop traversed in a
 * depth first search starting from initial state. The goal of the loop
 * tracking algorithm is to associate topmost loop entries with states
 * derived from these entries.
 *
 * For each step in the DFS states traversal algorithm needs to identify
 * the following situations:
 *
 *          initial                     initial                   initial
 *            |                           |                         |
 *            V                           V                         V
 *           ...                         ...           .---------> hdr
 *            |                           |            |            |
 *            V                           V            |            V
 *           cur                     .-> succ          |    .------...
 *            |                      |    |            |    |       |
 *            V                      |    V            |    V       V
 *           succ                    '-- cur           |   ...     ...
 *                                                     |    |       |
 *                                                     |    V       V
 *                                                     |   succ <- cur
 *                                                     |    |
 *                                                     |    V
 *                                                     |   ...
 *                                                     |    |
 *                                                     '----'
 *
 *  (A) successor state of cur   (B) successor state of cur or it's entry
 *      not yet traversed            are in current DFS path, thus cur and succ
 *                                   are members of the same outermost loop
 *
 *                      initial                  initial
 *                        |                        |
 *                        V                        V
 *                       ...                      ...
 *                        |                        |
 *                        V                        V
 *                .------...               .------...
 *                |       |                |       |
 *                V       V                V       V
 *           .-> hdr     ...              ...     ...
 *           |    |       |                |       |
 *           |    V       V                V       V
 *           |   succ <- cur              succ <- cur
 *           |    |                        |
 *           |    V                        V
 *           |   ...                      ...
 *           |    |                        |
 *           '----'                       exit
 *
 * (C) successor state of cur is a part of some loop but this loop
 *     does not include cur or successor state is not in a loop at all.
 *
 * Algorithm could be described as the following python code:
 *
 *     traversed = set()   # Set of traversed nodes
 *     entries = {}        # Mapping from node to loop entry
 *     depths = {}         # Depth level assigned to graph node
 *     path = set()        # Current DFS path
 *
 *     # Find outermost loop entry known for n
 *     def get_loop_entry(n):
 *         h = entries.get(n, None)
 *         while h in entries and entries[h] != h:
 *             h = entries[h]
 *         return h
 *
 *     # Update n's loop entry if h's outermost entry comes
 *     # before n's outermost entry in current DFS path.
 *     def update_loop_entry(n, h):
 *         n1 = get_loop_entry(n) or n
 *         h1 = get_loop_entry(h) or h
 *         if h1 in path and depths[h1] <= depths[n1]:
 *             entries[n] = h1
 *
 *     def dfs(n, depth):
 *         traversed.add(n)
 *         path.add(n)
 *         depths[n] = depth
 *         for succ in G.successors(n):
 *             if succ not in traversed:
 *                 # Case A: explore succ and update cur's loop entry
 *                 #         only if succ's entry is in current DFS path.
 *                 dfs(succ, depth + 1)
 *                 h = get_loop_entry(succ)
 *                 update_loop_entry(n, h)
 *             else:
 *                 # Case B or C depending on `h1 in path` check in update_loop_entry().
 *                 update_loop_entry(n, succ)
 *         path.remove(n)
 *
 * To adapt this algorithm for use with verifier:
 * - use st->branch == 0 as a signal that DFS of succ had been finished
 *   and cur's loop entry has to be updated (case A), handle this in
 *   update_branch_counts();
 * - use st->branch > 0 as a signal that st is in the current DFS path;
 * - handle cases B and C in is_state_visited();
 * - update topmost loop entry for intermediate states in get_loop_entry().
 */
static struct bpf_verifier_state *get_loop_entry(struct bpf_verifier_state *st)
{
	struct bpf_verifier_state *topmost = st->loop_entry, *old;

	while (topmost && topmost->loop_entry && topmost != topmost->loop_entry)
		topmost = topmost->loop_entry;
	/* Update loop entries for intermediate states to avoid this
	 * traversal in future get_loop_entry() calls.
	 */
	while (st && st->loop_entry != topmost) {
		old = st->loop_entry;
		st->loop_entry = topmost;
		st = old;
	}
	return topmost;
}

static void update_loop_entry(struct bpf_verifier_state *cur, struct bpf_verifier_state *hdr)
{
	struct bpf_verifier_state *cur1, *hdr1;

	cur1 = get_loop_entry(cur) ?: cur;
	hdr1 = get_loop_entry(hdr) ?: hdr;
	/* The head1->branches check decides between cases B and C in
	 * comment for get_loop_entry(). If hdr1->branches == 0 then
	 * head's topmost loop entry is not in current DFS path,
	 * hence 'cur' and 'hdr' are not in the same loop and there is
	 * no need to update cur->loop_entry.
	 */
	if (hdr1->branches && hdr1->dfs_depth <= cur1->dfs_depth) {
		cur->loop_entry = hdr;
		hdr->used_as_loop_entry = true;
	}
}

static void update_branch_counts(struct bpf_verifier_env *env, struct bpf_verifier_state *st)
{
	while (st) {
		u32 br = --st->branches;

		/* br == 0 signals that DFS exploration for 'st' is finished,
		 * thus it is necessary to update parent's loop entry if it
		 * turned out that st is a part of some loop.
		 * This is a part of 'case A' in get_loop_entry() comment.
		 */
		if (br == 0 && st->parent && st->loop_entry)
			update_loop_entry(st->parent, st->loop_entry);

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
	elem->log_pos = env->log.end_pos;
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
	/* Clear off and union(map_ptr, range) */
	memset(((u8 *)reg) + sizeof(reg->type), 0,
	       offsetof(struct bpf_reg_state, var_off) - sizeof(reg->type));
	reg->id = 0;
	reg->ref_obj_id = 0;
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

static void __mark_reg_const_zero(const struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	__mark_reg_known(reg, 0);
	reg->type = SCALAR_VALUE;
	/* all scalars are assumed imprecise initially (unless unprivileged,
	 * in which case everything is forced to be precise)
	 */
	reg->precise = !env->bpf_capable;
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

static void __mark_dynptr_reg(struct bpf_reg_state *reg, enum bpf_dynptr_type type,
			      bool first_slot, int dynptr_id)
{
	/* reg->type has no meaning for STACK_DYNPTR, but when we set reg for
	 * callback arguments, it does need to be CONST_PTR_TO_DYNPTR, so simply
	 * set it unconditionally as it is ignored for STACK_DYNPTR anyway.
	 */
	__mark_reg_known_zero(reg);
	reg->type = CONST_PTR_TO_DYNPTR;
	/* Give each dynptr a unique id to uniquely associate slices to it. */
	reg->id = dynptr_id;
	reg->dynptr.type = type;
	reg->dynptr.first_slot = first_slot;
}

static void mark_ptr_not_null_reg(struct bpf_reg_state *reg)
{
	if (base_type(reg->type) == PTR_TO_MAP_VALUE) {
		const struct bpf_map *map = reg->map_ptr;

		if (map->inner_map_meta) {
			reg->type = CONST_PTR_TO_MAP;
			reg->map_ptr = map->inner_map_meta;
			/* transfer reg's id which is unique for every map_lookup_elem
			 * as UID of the inner map.
			 */
			if (btf_record_has_field(map->inner_map_meta->record, BPF_TIMER))
				reg->map_uid = reg->id;
			if (btf_record_has_field(map->inner_map_meta->record, BPF_WORKQUEUE))
				reg->map_uid = reg->id;
		} else if (map->map_type == BPF_MAP_TYPE_XSKMAP) {
			reg->type = PTR_TO_XDP_SOCK;
		} else if (map->map_type == BPF_MAP_TYPE_SOCKMAP ||
			   map->map_type == BPF_MAP_TYPE_SOCKHASH) {
			reg->type = PTR_TO_SOCKET;
		} else {
			reg->type = PTR_TO_MAP_VALUE;
		}
		return;
	}

	reg->type &= ~PTR_MAYBE_NULL;
}

static void mark_reg_graph_node(struct bpf_reg_state *regs, u32 regno,
				struct btf_field_graph_root *ds_head)
{
	__mark_reg_known_zero(&regs[regno]);
	regs[regno].type = PTR_TO_BTF_ID | MEM_ALLOC;
	regs[regno].btf = ds_head->btf;
	regs[regno].btf_id = ds_head->value_btf_id;
	regs[regno].off = ds_head->node_offset;
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

static bool reg_is_dynptr_slice_pkt(const struct bpf_reg_state *reg)
{
	return base_type(reg->type) == PTR_TO_MEM &&
		(reg->type & DYNPTR_TYPE_SKB || reg->type & DYNPTR_TYPE_XDP);
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
	/* If upper 32 bits of u64/s64 range don't change, we can use lower 32
	 * bits to improve our u32/s32 boundaries.
	 *
	 * E.g., the case where we have upper 32 bits as zero ([10, 20] in
	 * u64) is pretty trivial, it's obvious that in u32 we'll also have
	 * [10, 20] range. But this property holds for any 64-bit range as
	 * long as upper 32 bits in that entire range of values stay the same.
	 *
	 * E.g., u64 range [0x10000000A, 0x10000000F] ([4294967306, 4294967311]
	 * in decimal) has the same upper 32 bits throughout all the values in
	 * that range. As such, lower 32 bits form a valid [0xA, 0xF] ([10, 15])
	 * range.
	 *
	 * Note also, that [0xA, 0xF] is a valid range both in u32 and in s32,
	 * following the rules outlined below about u64/s64 correspondence
	 * (which equally applies to u32 vs s32 correspondence). In general it
	 * depends on actual hexadecimal values of 32-bit range. They can form
	 * only valid u32, or only valid s32 ranges in some cases.
	 *
	 * So we use all these insights to derive bounds for subregisters here.
	 */
	if ((reg->umin_value >> 32) == (reg->umax_value >> 32)) {
		/* u64 to u32 casting preserves validity of low 32 bits as
		 * a range, if upper 32 bits are the same
		 */
		reg->u32_min_value = max_t(u32, reg->u32_min_value, (u32)reg->umin_value);
		reg->u32_max_value = min_t(u32, reg->u32_max_value, (u32)reg->umax_value);

		if ((s32)reg->umin_value <= (s32)reg->umax_value) {
			reg->s32_min_value = max_t(s32, reg->s32_min_value, (s32)reg->umin_value);
			reg->s32_max_value = min_t(s32, reg->s32_max_value, (s32)reg->umax_value);
		}
	}
	if ((reg->smin_value >> 32) == (reg->smax_value >> 32)) {
		/* low 32 bits should form a proper u32 range */
		if ((u32)reg->smin_value <= (u32)reg->smax_value) {
			reg->u32_min_value = max_t(u32, reg->u32_min_value, (u32)reg->smin_value);
			reg->u32_max_value = min_t(u32, reg->u32_max_value, (u32)reg->smax_value);
		}
		/* low 32 bits should form a proper s32 range */
		if ((s32)reg->smin_value <= (s32)reg->smax_value) {
			reg->s32_min_value = max_t(s32, reg->s32_min_value, (s32)reg->smin_value);
			reg->s32_max_value = min_t(s32, reg->s32_max_value, (s32)reg->smax_value);
		}
	}
	/* Special case where upper bits form a small sequence of two
	 * sequential numbers (in 32-bit unsigned space, so 0xffffffff to
	 * 0x00000000 is also valid), while lower bits form a proper s32 range
	 * going from negative numbers to positive numbers. E.g., let's say we
	 * have s64 range [-1, 1] ([0xffffffffffffffff, 0x0000000000000001]).
	 * Possible s64 values are {-1, 0, 1} ({0xffffffffffffffff,
	 * 0x0000000000000000, 0x00000000000001}). Ignoring upper 32 bits,
	 * we still get a valid s32 range [-1, 1] ([0xffffffff, 0x00000001]).
	 * Note that it doesn't have to be 0xffffffff going to 0x00000000 in
	 * upper 32 bits. As a random example, s64 range
	 * [0xfffffff0fffffff0; 0xfffffff100000010], forms a valid s32 range
	 * [-16, 16] ([0xfffffff0; 0x00000010]) in its 32 bit subregister.
	 */
	if ((u32)(reg->umin_value >> 32) + 1 == (u32)(reg->umax_value >> 32) &&
	    (s32)reg->umin_value < 0 && (s32)reg->umax_value >= 0) {
		reg->s32_min_value = max_t(s32, reg->s32_min_value, (s32)reg->umin_value);
		reg->s32_max_value = min_t(s32, reg->s32_max_value, (s32)reg->umax_value);
	}
	if ((u32)(reg->smin_value >> 32) + 1 == (u32)(reg->smax_value >> 32) &&
	    (s32)reg->smin_value < 0 && (s32)reg->smax_value >= 0) {
		reg->s32_min_value = max_t(s32, reg->s32_min_value, (s32)reg->smin_value);
		reg->s32_max_value = min_t(s32, reg->s32_max_value, (s32)reg->smax_value);
	}
	/* if u32 range forms a valid s32 range (due to matching sign bit),
	 * try to learn from that
	 */
	if ((s32)reg->u32_min_value <= (s32)reg->u32_max_value) {
		reg->s32_min_value = max_t(s32, reg->s32_min_value, reg->u32_min_value);
		reg->s32_max_value = min_t(s32, reg->s32_max_value, reg->u32_max_value);
	}
	/* If we cannot cross the sign boundary, then signed and unsigned bounds
	 * are the same, so combine.  This works even in the negative case, e.g.
	 * -3 s<= x s<= -1 implies 0xf...fd u<= x u<= 0xf...ff.
	 */
	if ((u32)reg->s32_min_value <= (u32)reg->s32_max_value) {
		reg->u32_min_value = max_t(u32, reg->s32_min_value, reg->u32_min_value);
		reg->u32_max_value = min_t(u32, reg->s32_max_value, reg->u32_max_value);
	}
}

static void __reg64_deduce_bounds(struct bpf_reg_state *reg)
{
	/* If u64 range forms a valid s64 range (due to matching sign bit),
	 * try to learn from that. Let's do a bit of ASCII art to see when
	 * this is happening. Let's take u64 range first:
	 *
	 * 0             0x7fffffffffffffff 0x8000000000000000        U64_MAX
	 * |-------------------------------|--------------------------------|
	 *
	 * Valid u64 range is formed when umin and umax are anywhere in the
	 * range [0, U64_MAX], and umin <= umax. u64 case is simple and
	 * straightforward. Let's see how s64 range maps onto the same range
	 * of values, annotated below the line for comparison:
	 *
	 * 0             0x7fffffffffffffff 0x8000000000000000        U64_MAX
	 * |-------------------------------|--------------------------------|
	 * 0                        S64_MAX S64_MIN                        -1
	 *
	 * So s64 values basically start in the middle and they are logically
	 * contiguous to the right of it, wrapping around from -1 to 0, and
	 * then finishing as S64_MAX (0x7fffffffffffffff) right before
	 * S64_MIN. We can try drawing the continuity of u64 vs s64 values
	 * more visually as mapped to sign-agnostic range of hex values.
	 *
	 *  u64 start                                               u64 end
	 *  _______________________________________________________________
	 * /                                                               \
	 * 0             0x7fffffffffffffff 0x8000000000000000        U64_MAX
	 * |-------------------------------|--------------------------------|
	 * 0                        S64_MAX S64_MIN                        -1
	 *                                / \
	 * >------------------------------   ------------------------------->
	 * s64 continues...        s64 end   s64 start          s64 "midpoint"
	 *
	 * What this means is that, in general, we can't always derive
	 * something new about u64 from any random s64 range, and vice versa.
	 *
	 * But we can do that in two particular cases. One is when entire
	 * u64/s64 range is *entirely* contained within left half of the above
	 * diagram or when it is *entirely* contained in the right half. I.e.:
	 *
	 * |-------------------------------|--------------------------------|
	 *     ^                   ^            ^                 ^
	 *     A                   B            C                 D
	 *
	 * [A, B] and [C, D] are contained entirely in their respective halves
	 * and form valid contiguous ranges as both u64 and s64 values. [A, B]
	 * will be non-negative both as u64 and s64 (and in fact it will be
	 * identical ranges no matter the signedness). [C, D] treated as s64
	 * will be a range of negative values, while in u64 it will be
	 * non-negative range of values larger than 0x8000000000000000.
	 *
	 * Now, any other range here can't be represented in both u64 and s64
	 * simultaneously. E.g., [A, C], [A, D], [B, C], [B, D] are valid
	 * contiguous u64 ranges, but they are discontinuous in s64. [B, C]
	 * in s64 would be properly presented as [S64_MIN, C] and [B, S64_MAX],
	 * for example. Similarly, valid s64 range [D, A] (going from negative
	 * to positive values), would be two separate [D, U64_MAX] and [0, A]
	 * ranges as u64. Currently reg_state can't represent two segments per
	 * numeric domain, so in such situations we can only derive maximal
	 * possible range ([0, U64_MAX] for u64, and [S64_MIN, S64_MAX] for s64).
	 *
	 * So we use these facts to derive umin/umax from smin/smax and vice
	 * versa only if they stay within the same "half". This is equivalent
	 * to checking sign bit: lower half will have sign bit as zero, upper
	 * half have sign bit 1. Below in code we simplify this by just
	 * casting umin/umax as smin/smax and checking if they form valid
	 * range, and vice versa. Those are equivalent checks.
	 */
	if ((s64)reg->umin_value <= (s64)reg->umax_value) {
		reg->smin_value = max_t(s64, reg->smin_value, reg->umin_value);
		reg->smax_value = min_t(s64, reg->smax_value, reg->umax_value);
	}
	/* If we cannot cross the sign boundary, then signed and unsigned bounds
	 * are the same, so combine.  This works even in the negative case, e.g.
	 * -3 s<= x s<= -1 implies 0xf...fd u<= x u<= 0xf...ff.
	 */
	if ((u64)reg->smin_value <= (u64)reg->smax_value) {
		reg->umin_value = max_t(u64, reg->smin_value, reg->umin_value);
		reg->umax_value = min_t(u64, reg->smax_value, reg->umax_value);
	}
}

static void __reg_deduce_mixed_bounds(struct bpf_reg_state *reg)
{
	/* Try to tighten 64-bit bounds from 32-bit knowledge, using 32-bit
	 * values on both sides of 64-bit range in hope to have tighter range.
	 * E.g., if r1 is [0x1'00000000, 0x3'80000000], and we learn from
	 * 32-bit signed > 0 operation that s32 bounds are now [1; 0x7fffffff].
	 * With this, we can substitute 1 as low 32-bits of _low_ 64-bit bound
	 * (0x100000000 -> 0x100000001) and 0x7fffffff as low 32-bits of
	 * _high_ 64-bit bound (0x380000000 -> 0x37fffffff) and arrive at a
	 * better overall bounds for r1 as [0x1'000000001; 0x3'7fffffff].
	 * We just need to make sure that derived bounds we are intersecting
	 * with are well-formed ranges in respective s64 or u64 domain, just
	 * like we do with similar kinds of 32-to-64 or 64-to-32 adjustments.
	 */
	__u64 new_umin, new_umax;
	__s64 new_smin, new_smax;

	/* u32 -> u64 tightening, it's always well-formed */
	new_umin = (reg->umin_value & ~0xffffffffULL) | reg->u32_min_value;
	new_umax = (reg->umax_value & ~0xffffffffULL) | reg->u32_max_value;
	reg->umin_value = max_t(u64, reg->umin_value, new_umin);
	reg->umax_value = min_t(u64, reg->umax_value, new_umax);
	/* u32 -> s64 tightening, u32 range embedded into s64 preserves range validity */
	new_smin = (reg->smin_value & ~0xffffffffULL) | reg->u32_min_value;
	new_smax = (reg->smax_value & ~0xffffffffULL) | reg->u32_max_value;
	reg->smin_value = max_t(s64, reg->smin_value, new_smin);
	reg->smax_value = min_t(s64, reg->smax_value, new_smax);

	/* if s32 can be treated as valid u32 range, we can use it as well */
	if ((u32)reg->s32_min_value <= (u32)reg->s32_max_value) {
		/* s32 -> u64 tightening */
		new_umin = (reg->umin_value & ~0xffffffffULL) | (u32)reg->s32_min_value;
		new_umax = (reg->umax_value & ~0xffffffffULL) | (u32)reg->s32_max_value;
		reg->umin_value = max_t(u64, reg->umin_value, new_umin);
		reg->umax_value = min_t(u64, reg->umax_value, new_umax);
		/* s32 -> s64 tightening */
		new_smin = (reg->smin_value & ~0xffffffffULL) | (u32)reg->s32_min_value;
		new_smax = (reg->smax_value & ~0xffffffffULL) | (u32)reg->s32_max_value;
		reg->smin_value = max_t(s64, reg->smin_value, new_smin);
		reg->smax_value = min_t(s64, reg->smax_value, new_smax);
	}

	/* Here we would like to handle a special case after sign extending load,
	 * when upper bits for a 64-bit range are all 1s or all 0s.
	 *
	 * Upper bits are all 1s when register is in a range:
	 *   [0xffff_ffff_0000_0000, 0xffff_ffff_ffff_ffff]
	 * Upper bits are all 0s when register is in a range:
	 *   [0x0000_0000_0000_0000, 0x0000_0000_ffff_ffff]
	 * Together this forms are continuous range:
	 *   [0xffff_ffff_0000_0000, 0x0000_0000_ffff_ffff]
	 *
	 * Now, suppose that register range is in fact tighter:
	 *   [0xffff_ffff_8000_0000, 0x0000_0000_ffff_ffff] (R)
	 * Also suppose that it's 32-bit range is positive,
	 * meaning that lower 32-bits of the full 64-bit register
	 * are in the range:
	 *   [0x0000_0000, 0x7fff_ffff] (W)
	 *
	 * If this happens, then any value in a range:
	 *   [0xffff_ffff_0000_0000, 0xffff_ffff_7fff_ffff]
	 * is smaller than a lowest bound of the range (R):
	 *   0xffff_ffff_8000_0000
	 * which means that upper bits of the full 64-bit register
	 * can't be all 1s, when lower bits are in range (W).
	 *
	 * Note that:
	 *  - 0xffff_ffff_8000_0000 == (s64)S32_MIN
	 *  - 0x0000_0000_7fff_ffff == (s64)S32_MAX
	 * These relations are used in the conditions below.
	 */
	if (reg->s32_min_value >= 0 && reg->smin_value >= S32_MIN && reg->smax_value <= S32_MAX) {
		reg->smin_value = reg->s32_min_value;
		reg->smax_value = reg->s32_max_value;
		reg->umin_value = reg->s32_min_value;
		reg->umax_value = reg->s32_max_value;
		reg->var_off = tnum_intersect(reg->var_off,
					      tnum_range(reg->smin_value, reg->smax_value));
	}
}

static void __reg_deduce_bounds(struct bpf_reg_state *reg)
{
	__reg32_deduce_bounds(reg);
	__reg64_deduce_bounds(reg);
	__reg_deduce_mixed_bounds(reg);
}

/* Attempts to improve var_off based on unsigned min/max information */
static void __reg_bound_offset(struct bpf_reg_state *reg)
{
	struct tnum var64_off = tnum_intersect(reg->var_off,
					       tnum_range(reg->umin_value,
							  reg->umax_value));
	struct tnum var32_off = tnum_intersect(tnum_subreg(var64_off),
					       tnum_range(reg->u32_min_value,
							  reg->u32_max_value));

	reg->var_off = tnum_or(tnum_clear_subreg(var64_off), var32_off);
}

static void reg_bounds_sync(struct bpf_reg_state *reg)
{
	/* We might have learned new bounds from the var_off. */
	__update_reg_bounds(reg);
	/* We might have learned something about the sign bit. */
	__reg_deduce_bounds(reg);
	__reg_deduce_bounds(reg);
	/* We might have learned some bits from the bounds. */
	__reg_bound_offset(reg);
	/* Intersecting with the old var_off might have improved our bounds
	 * slightly, e.g. if umax was 0x7f...f and var_off was (0; 0xf...fc),
	 * then new var_off is (0; 0x7f...fc) which improves our umax.
	 */
	__update_reg_bounds(reg);
}

static int reg_bounds_sanity_check(struct bpf_verifier_env *env,
				   struct bpf_reg_state *reg, const char *ctx)
{
	const char *msg;

	if (reg->umin_value > reg->umax_value ||
	    reg->smin_value > reg->smax_value ||
	    reg->u32_min_value > reg->u32_max_value ||
	    reg->s32_min_value > reg->s32_max_value) {
		    msg = "range bounds violation";
		    goto out;
	}

	if (tnum_is_const(reg->var_off)) {
		u64 uval = reg->var_off.value;
		s64 sval = (s64)uval;

		if (reg->umin_value != uval || reg->umax_value != uval ||
		    reg->smin_value != sval || reg->smax_value != sval) {
			msg = "const tnum out of sync with range bounds";
			goto out;
		}
	}

	if (tnum_subreg_is_const(reg->var_off)) {
		u32 uval32 = tnum_subreg(reg->var_off).value;
		s32 sval32 = (s32)uval32;

		if (reg->u32_min_value != uval32 || reg->u32_max_value != uval32 ||
		    reg->s32_min_value != sval32 || reg->s32_max_value != sval32) {
			msg = "const subreg tnum out of sync with range bounds";
			goto out;
		}
	}

	return 0;
out:
	verbose(env, "REG INVARIANTS VIOLATION (%s): %s u64=[%#llx, %#llx] "
		"s64=[%#llx, %#llx] u32=[%#x, %#x] s32=[%#x, %#x] var_off=(%#llx, %#llx)\n",
		ctx, msg, reg->umin_value, reg->umax_value,
		reg->smin_value, reg->smax_value,
		reg->u32_min_value, reg->u32_max_value,
		reg->s32_min_value, reg->s32_max_value,
		reg->var_off.value, reg->var_off.mask);
	if (env->test_reg_invariants)
		return -EFAULT;
	__mark_reg_unbounded(reg);
	return 0;
}

static bool __reg32_bound_s64(s32 a)
{
	return a >= 0 && a <= S32_MAX;
}

static void __reg_assign_32_into_64(struct bpf_reg_state *reg)
{
	reg->umin_value = reg->u32_min_value;
	reg->umax_value = reg->u32_max_value;

	/* Attempt to pull 32-bit signed bounds into 64-bit bounds but must
	 * be positive otherwise set to worse case bounds and refine later
	 * from tnum.
	 */
	if (__reg32_bound_s64(reg->s32_min_value) &&
	    __reg32_bound_s64(reg->s32_max_value)) {
		reg->smin_value = reg->s32_min_value;
		reg->smax_value = reg->s32_max_value;
	} else {
		reg->smin_value = 0;
		reg->smax_value = U32_MAX;
	}
}

/* Mark a register as having a completely unknown (scalar) value. */
static void __mark_reg_unknown_imprecise(struct bpf_reg_state *reg)
{
	/*
	 * Clear type, off, and union(map_ptr, range) and
	 * padding between 'type' and union
	 */
	memset(reg, 0, offsetof(struct bpf_reg_state, var_off));
	reg->type = SCALAR_VALUE;
	reg->id = 0;
	reg->ref_obj_id = 0;
	reg->var_off = tnum_unknown;
	reg->frameno = 0;
	reg->precise = false;
	__mark_reg_unbounded(reg);
}

/* Mark a register as having a completely unknown (scalar) value,
 * initialize .precise as true when not bpf capable.
 */
static void __mark_reg_unknown(const struct bpf_verifier_env *env,
			       struct bpf_reg_state *reg)
{
	__mark_reg_unknown_imprecise(reg);
	reg->precise = !env->bpf_capable;
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

static int __mark_reg_s32_range(struct bpf_verifier_env *env,
				struct bpf_reg_state *regs,
				u32 regno,
				s32 s32_min,
				s32 s32_max)
{
	struct bpf_reg_state *reg = regs + regno;

	reg->s32_min_value = max_t(s32, reg->s32_min_value, s32_min);
	reg->s32_max_value = min_t(s32, reg->s32_max_value, s32_max);

	reg->smin_value = max_t(s64, reg->smin_value, s32_min);
	reg->smax_value = min_t(s64, reg->smax_value, s32_max);

	reg_bounds_sync(reg);

	return reg_bounds_sanity_check(env, reg, "s32_range");
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
			    struct btf *btf, u32 btf_id,
			    enum bpf_type_flag flag)
{
	if (reg_type == SCALAR_VALUE) {
		mark_reg_unknown(env, regs, regno);
		return;
	}
	mark_reg_known_zero(env, regs, regno);
	regs[regno].type = PTR_TO_BTF_ID | flag;
	regs[regno].btf = btf;
	regs[regno].btf_id = btf_id;
	if (type_may_be_null(flag))
		regs[regno].id = ++env->id_gen;
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

static struct bpf_retval_range retval_range(s32 minval, s32 maxval)
{
	return (struct bpf_retval_range){ minval, maxval };
}

#define BPF_MAIN_FUNC (-1)
static void init_func_state(struct bpf_verifier_env *env,
			    struct bpf_func_state *state,
			    int callsite, int frameno, int subprogno)
{
	state->callsite = callsite;
	state->frameno = frameno;
	state->subprogno = subprogno;
	state->callback_ret_range = retval_range(0, 0);
	init_reg_state(env, state);
	mark_verifier_state_scratched(env);
}

/* Similar to push_stack(), but for async callbacks */
static struct bpf_verifier_state *push_async_cb(struct bpf_verifier_env *env,
						int insn_idx, int prev_insn_idx,
						int subprog, bool is_sleepable)
{
	struct bpf_verifier_stack_elem *elem;
	struct bpf_func_state *frame;

	elem = kzalloc(sizeof(struct bpf_verifier_stack_elem), GFP_KERNEL);
	if (!elem)
		goto err;

	elem->insn_idx = insn_idx;
	elem->prev_insn_idx = prev_insn_idx;
	elem->next = env->head;
	elem->log_pos = env->log.end_pos;
	env->head = elem;
	env->stack_size++;
	if (env->stack_size > BPF_COMPLEXITY_LIMIT_JMP_SEQ) {
		verbose(env,
			"The sequence of %d jumps is too complex for async cb.\n",
			env->stack_size);
		goto err;
	}
	/* Unlike push_stack() do not copy_verifier_state().
	 * The caller state doesn't matter.
	 * This is async callback. It starts in a fresh stack.
	 * Initialize it similar to do_check_common().
	 * But we do need to make sure to not clobber insn_hist, so we keep
	 * chaining insn_hist_start/insn_hist_end indices as for a normal
	 * child state.
	 */
	elem->st.branches = 1;
	elem->st.in_sleepable = is_sleepable;
	elem->st.insn_hist_start = env->cur_state->insn_hist_end;
	elem->st.insn_hist_end = elem->st.insn_hist_start;
	frame = kzalloc(sizeof(*frame), GFP_KERNEL);
	if (!frame)
		goto err;
	init_func_state(env, frame,
			BPF_MAIN_FUNC /* callsite */,
			0 /* frameno within this callchain */,
			subprog /* subprog number within this prog */);
	elem->st.frame[0] = frame;
	return &elem->st;
err:
	free_verifier_state(env->cur_state, true);
	env->cur_state = NULL;
	/* pop all elements and return */
	while (!pop_stack(env, NULL, NULL, false));
	return NULL;
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
		return ret;
	if (env->subprog_cnt >= BPF_MAX_SUBPROGS) {
		verbose(env, "too many subprograms\n");
		return -E2BIG;
	}
	/* determine subprog starts. The end is one before the next starts */
	env->subprog_info[env->subprog_cnt++].start = off;
	sort(env->subprog_info, env->subprog_cnt,
	     sizeof(env->subprog_info[0]), cmp_subprogs, NULL);
	return env->subprog_cnt - 1;
}

static int bpf_find_exception_callback_insn_off(struct bpf_verifier_env *env)
{
	struct bpf_prog_aux *aux = env->prog->aux;
	struct btf *btf = aux->btf;
	const struct btf_type *t;
	u32 main_btf_id, id;
	const char *name;
	int ret, i;

	/* Non-zero func_info_cnt implies valid btf */
	if (!aux->func_info_cnt)
		return 0;
	main_btf_id = aux->func_info[0].type_id;

	t = btf_type_by_id(btf, main_btf_id);
	if (!t) {
		verbose(env, "invalid btf id for main subprog in func_info\n");
		return -EINVAL;
	}

	name = btf_find_decl_tag_value(btf, t, -1, "exception_callback:");
	if (IS_ERR(name)) {
		ret = PTR_ERR(name);
		/* If there is no tag present, there is no exception callback */
		if (ret == -ENOENT)
			ret = 0;
		else if (ret == -EEXIST)
			verbose(env, "multiple exception callback tags for main subprog\n");
		return ret;
	}

	ret = btf_find_by_name_kind(btf, name, BTF_KIND_FUNC);
	if (ret < 0) {
		verbose(env, "exception callback '%s' could not be found in BTF\n", name);
		return ret;
	}
	id = ret;
	t = btf_type_by_id(btf, id);
	if (btf_func_linkage(t) != BTF_FUNC_GLOBAL) {
		verbose(env, "exception callback '%s' must have global linkage\n", name);
		return -EINVAL;
	}
	ret = 0;
	for (i = 0; i < aux->func_info_cnt; i++) {
		if (aux->func_info[i].type_id != id)
			continue;
		ret = aux->func_info[i].insn_off;
		/* Further func_info and subprog checks will also happen
		 * later, so assume this is the right insn_off for now.
		 */
		if (!ret) {
			verbose(env, "invalid exception callback insn_off in func_info: 0\n");
			ret = -EINVAL;
		}
	}
	if (!ret) {
		verbose(env, "exception callback type id not found in func_info\n");
		ret = -EINVAL;
	}
	return ret;
}

#define MAX_KFUNC_DESCS 256
#define MAX_KFUNC_BTFS	256

struct bpf_kfunc_desc {
	struct btf_func_model func_model;
	u32 func_id;
	s32 imm;
	u16 offset;
	unsigned long addr;
};

struct bpf_kfunc_btf {
	struct btf *btf;
	struct module *module;
	u16 offset;
};

struct bpf_kfunc_desc_tab {
	/* Sorted by func_id (BTF ID) and offset (fd_array offset) during
	 * verification. JITs do lookups by bpf_insn, where func_id may not be
	 * available, therefore at the end of verification do_misc_fixups()
	 * sorts this by imm and offset.
	 */
	struct bpf_kfunc_desc descs[MAX_KFUNC_DESCS];
	u32 nr_descs;
};

struct bpf_kfunc_btf_tab {
	struct bpf_kfunc_btf descs[MAX_KFUNC_BTFS];
	u32 nr_descs;
};

static int kfunc_desc_cmp_by_id_off(const void *a, const void *b)
{
	const struct bpf_kfunc_desc *d0 = a;
	const struct bpf_kfunc_desc *d1 = b;

	/* func_id is not greater than BTF_MAX_TYPE */
	return d0->func_id - d1->func_id ?: d0->offset - d1->offset;
}

static int kfunc_btf_cmp_by_off(const void *a, const void *b)
{
	const struct bpf_kfunc_btf *d0 = a;
	const struct bpf_kfunc_btf *d1 = b;

	return d0->offset - d1->offset;
}

static const struct bpf_kfunc_desc *
find_kfunc_desc(const struct bpf_prog *prog, u32 func_id, u16 offset)
{
	struct bpf_kfunc_desc desc = {
		.func_id = func_id,
		.offset = offset,
	};
	struct bpf_kfunc_desc_tab *tab;

	tab = prog->aux->kfunc_tab;
	return bsearch(&desc, tab->descs, tab->nr_descs,
		       sizeof(tab->descs[0]), kfunc_desc_cmp_by_id_off);
}

int bpf_get_kfunc_addr(const struct bpf_prog *prog, u32 func_id,
		       u16 btf_fd_idx, u8 **func_addr)
{
	const struct bpf_kfunc_desc *desc;

	desc = find_kfunc_desc(prog, func_id, btf_fd_idx);
	if (!desc)
		return -EFAULT;

	*func_addr = (u8 *)desc->addr;
	return 0;
}

static struct btf *__find_kfunc_desc_btf(struct bpf_verifier_env *env,
					 s16 offset)
{
	struct bpf_kfunc_btf kf_btf = { .offset = offset };
	struct bpf_kfunc_btf_tab *tab;
	struct bpf_kfunc_btf *b;
	struct module *mod;
	struct btf *btf;
	int btf_fd;

	tab = env->prog->aux->kfunc_btf_tab;
	b = bsearch(&kf_btf, tab->descs, tab->nr_descs,
		    sizeof(tab->descs[0]), kfunc_btf_cmp_by_off);
	if (!b) {
		if (tab->nr_descs == MAX_KFUNC_BTFS) {
			verbose(env, "too many different module BTFs\n");
			return ERR_PTR(-E2BIG);
		}

		if (bpfptr_is_null(env->fd_array)) {
			verbose(env, "kfunc offset > 0 without fd_array is invalid\n");
			return ERR_PTR(-EPROTO);
		}

		if (copy_from_bpfptr_offset(&btf_fd, env->fd_array,
					    offset * sizeof(btf_fd),
					    sizeof(btf_fd)))
			return ERR_PTR(-EFAULT);

		btf = btf_get_by_fd(btf_fd);
		if (IS_ERR(btf)) {
			verbose(env, "invalid module BTF fd specified\n");
			return btf;
		}

		if (!btf_is_module(btf)) {
			verbose(env, "BTF fd for kfunc is not a module BTF\n");
			btf_put(btf);
			return ERR_PTR(-EINVAL);
		}

		mod = btf_try_get_module(btf);
		if (!mod) {
			btf_put(btf);
			return ERR_PTR(-ENXIO);
		}

		b = &tab->descs[tab->nr_descs++];
		b->btf = btf;
		b->module = mod;
		b->offset = offset;

		/* sort() reorders entries by value, so b may no longer point
		 * to the right entry after this
		 */
		sort(tab->descs, tab->nr_descs, sizeof(tab->descs[0]),
		     kfunc_btf_cmp_by_off, NULL);
	} else {
		btf = b->btf;
	}

	return btf;
}

void bpf_free_kfunc_btf_tab(struct bpf_kfunc_btf_tab *tab)
{
	if (!tab)
		return;

	while (tab->nr_descs--) {
		module_put(tab->descs[tab->nr_descs].module);
		btf_put(tab->descs[tab->nr_descs].btf);
	}
	kfree(tab);
}

static struct btf *find_kfunc_desc_btf(struct bpf_verifier_env *env, s16 offset)
{
	if (offset) {
		if (offset < 0) {
			/* In the future, this can be allowed to increase limit
			 * of fd index into fd_array, interpreted as u16.
			 */
			verbose(env, "negative offset disallowed for kernel module function call\n");
			return ERR_PTR(-EINVAL);
		}

		return __find_kfunc_desc_btf(env, offset);
	}
	return btf_vmlinux ?: ERR_PTR(-ENOENT);
}

static int add_kfunc_call(struct bpf_verifier_env *env, u32 func_id, s16 offset)
{
	const struct btf_type *func, *func_proto;
	struct bpf_kfunc_btf_tab *btf_tab;
	struct bpf_kfunc_desc_tab *tab;
	struct bpf_prog_aux *prog_aux;
	struct bpf_kfunc_desc *desc;
	const char *func_name;
	struct btf *desc_btf;
	unsigned long call_imm;
	unsigned long addr;
	int err;

	prog_aux = env->prog->aux;
	tab = prog_aux->kfunc_tab;
	btf_tab = prog_aux->kfunc_btf_tab;
	if (!tab) {
		if (!btf_vmlinux) {
			verbose(env, "calling kernel function is not supported without CONFIG_DEBUG_INFO_BTF\n");
			return -ENOTSUPP;
		}

		if (!env->prog->jit_requested) {
			verbose(env, "JIT is required for calling kernel function\n");
			return -ENOTSUPP;
		}

		if (!bpf_jit_supports_kfunc_call()) {
			verbose(env, "JIT does not support calling kernel function\n");
			return -ENOTSUPP;
		}

		if (!env->prog->gpl_compatible) {
			verbose(env, "cannot call kernel function from non-GPL compatible program\n");
			return -EINVAL;
		}

		tab = kzalloc(sizeof(*tab), GFP_KERNEL);
		if (!tab)
			return -ENOMEM;
		prog_aux->kfunc_tab = tab;
	}

	/* func_id == 0 is always invalid, but instead of returning an error, be
	 * conservative and wait until the code elimination pass before returning
	 * error, so that invalid calls that get pruned out can be in BPF programs
	 * loaded from userspace.  It is also required that offset be untouched
	 * for such calls.
	 */
	if (!func_id && !offset)
		return 0;

	if (!btf_tab && offset) {
		btf_tab = kzalloc(sizeof(*btf_tab), GFP_KERNEL);
		if (!btf_tab)
			return -ENOMEM;
		prog_aux->kfunc_btf_tab = btf_tab;
	}

	desc_btf = find_kfunc_desc_btf(env, offset);
	if (IS_ERR(desc_btf)) {
		verbose(env, "failed to find BTF for kernel function\n");
		return PTR_ERR(desc_btf);
	}

	if (find_kfunc_desc(env->prog, func_id, offset))
		return 0;

	if (tab->nr_descs == MAX_KFUNC_DESCS) {
		verbose(env, "too many different kernel function calls\n");
		return -E2BIG;
	}

	func = btf_type_by_id(desc_btf, func_id);
	if (!func || !btf_type_is_func(func)) {
		verbose(env, "kernel btf_id %u is not a function\n",
			func_id);
		return -EINVAL;
	}
	func_proto = btf_type_by_id(desc_btf, func->type);
	if (!func_proto || !btf_type_is_func_proto(func_proto)) {
		verbose(env, "kernel function btf_id %u does not have a valid func_proto\n",
			func_id);
		return -EINVAL;
	}

	func_name = btf_name_by_offset(desc_btf, func->name_off);
	addr = kallsyms_lookup_name(func_name);
	if (!addr) {
		verbose(env, "cannot find address for kernel function %s\n",
			func_name);
		return -EINVAL;
	}
	specialize_kfunc(env, func_id, offset, &addr);

	if (bpf_jit_supports_far_kfunc_call()) {
		call_imm = func_id;
	} else {
		call_imm = BPF_CALL_IMM(addr);
		/* Check whether the relative offset overflows desc->imm */
		if ((unsigned long)(s32)call_imm != call_imm) {
			verbose(env, "address of kernel function %s is out of range\n",
				func_name);
			return -EINVAL;
		}
	}

	if (bpf_dev_bound_kfunc_id(func_id)) {
		err = bpf_dev_bound_kfunc_check(&env->log, prog_aux);
		if (err)
			return err;
	}

	desc = &tab->descs[tab->nr_descs++];
	desc->func_id = func_id;
	desc->imm = call_imm;
	desc->offset = offset;
	desc->addr = addr;
	err = btf_distill_func_proto(&env->log, desc_btf,
				     func_proto, func_name,
				     &desc->func_model);
	if (!err)
		sort(tab->descs, tab->nr_descs, sizeof(tab->descs[0]),
		     kfunc_desc_cmp_by_id_off, NULL);
	return err;
}

static int kfunc_desc_cmp_by_imm_off(const void *a, const void *b)
{
	const struct bpf_kfunc_desc *d0 = a;
	const struct bpf_kfunc_desc *d1 = b;

	if (d0->imm != d1->imm)
		return d0->imm < d1->imm ? -1 : 1;
	if (d0->offset != d1->offset)
		return d0->offset < d1->offset ? -1 : 1;
	return 0;
}

static void sort_kfunc_descs_by_imm_off(struct bpf_prog *prog)
{
	struct bpf_kfunc_desc_tab *tab;

	tab = prog->aux->kfunc_tab;
	if (!tab)
		return;

	sort(tab->descs, tab->nr_descs, sizeof(tab->descs[0]),
	     kfunc_desc_cmp_by_imm_off, NULL);
}

bool bpf_prog_has_kfunc_call(const struct bpf_prog *prog)
{
	return !!prog->aux->kfunc_tab;
}

const struct btf_func_model *
bpf_jit_find_kfunc_model(const struct bpf_prog *prog,
			 const struct bpf_insn *insn)
{
	const struct bpf_kfunc_desc desc = {
		.imm = insn->imm,
		.offset = insn->off,
	};
	const struct bpf_kfunc_desc *res;
	struct bpf_kfunc_desc_tab *tab;

	tab = prog->aux->kfunc_tab;
	res = bsearch(&desc, tab->descs, tab->nr_descs,
		      sizeof(tab->descs[0]), kfunc_desc_cmp_by_imm_off);

	return res ? &res->func_model : NULL;
}

static int add_subprog_and_kfunc(struct bpf_verifier_env *env)
{
	struct bpf_subprog_info *subprog = env->subprog_info;
	int i, ret, insn_cnt = env->prog->len, ex_cb_insn;
	struct bpf_insn *insn = env->prog->insnsi;

	/* Add entry function. */
	ret = add_subprog(env, 0);
	if (ret)
		return ret;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (!bpf_pseudo_func(insn) && !bpf_pseudo_call(insn) &&
		    !bpf_pseudo_kfunc_call(insn))
			continue;

		if (!env->bpf_capable) {
			verbose(env, "loading/calling other bpf or kernel functions are allowed for CAP_BPF and CAP_SYS_ADMIN\n");
			return -EPERM;
		}

		if (bpf_pseudo_func(insn) || bpf_pseudo_call(insn))
			ret = add_subprog(env, i + insn->imm + 1);
		else
			ret = add_kfunc_call(env, insn->imm, insn->off);

		if (ret < 0)
			return ret;
	}

	ret = bpf_find_exception_callback_insn_off(env);
	if (ret < 0)
		return ret;
	ex_cb_insn = ret;

	/* If ex_cb_insn > 0, this means that the main program has a subprog
	 * marked using BTF decl tag to serve as the exception callback.
	 */
	if (ex_cb_insn) {
		ret = add_subprog(env, ex_cb_insn);
		if (ret < 0)
			return ret;
		for (i = 1; i < env->subprog_cnt; i++) {
			if (env->subprog_info[i].start != ex_cb_insn)
				continue;
			env->exception_callback_subprog = i;
			mark_subprog_exc_cb(env, i);
			break;
		}
	}

	/* Add a fake 'exit' subprog which could simplify subprog iteration
	 * logic. 'subprog_cnt' should not be increased.
	 */
	subprog[env->subprog_cnt].start = insn_cnt;

	if (env->log.level & BPF_LOG_LEVEL2)
		for (i = 0; i < env->subprog_cnt; i++)
			verbose(env, "func#%d @%d\n", i, subprog[i].start);

	return 0;
}

static int check_subprogs(struct bpf_verifier_env *env)
{
	int i, subprog_start, subprog_end, off, cur_subprog = 0;
	struct bpf_subprog_info *subprog = env->subprog_info;
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;

	/* now check that all jumps are within the same subprog */
	subprog_start = subprog[cur_subprog].start;
	subprog_end = subprog[cur_subprog + 1].start;
	for (i = 0; i < insn_cnt; i++) {
		u8 code = insn[i].code;

		if (code == (BPF_JMP | BPF_CALL) &&
		    insn[i].src_reg == 0 &&
		    insn[i].imm == BPF_FUNC_tail_call) {
			subprog[cur_subprog].has_tail_call = true;
			subprog[cur_subprog].tail_call_reachable = true;
		}
		if (BPF_CLASS(code) == BPF_LD &&
		    (BPF_MODE(code) == BPF_ABS || BPF_MODE(code) == BPF_IND))
			subprog[cur_subprog].has_ld_abs = true;
		if (BPF_CLASS(code) != BPF_JMP && BPF_CLASS(code) != BPF_JMP32)
			goto next;
		if (BPF_OP(code) == BPF_EXIT || BPF_OP(code) == BPF_CALL)
			goto next;
		if (code == (BPF_JMP32 | BPF_JA))
			off = i + insn[i].imm + 1;
		else
			off = i + insn[i].off + 1;
		if (off < subprog_start || off >= subprog_end) {
			verbose(env, "jump out of range from insn %d to %d\n", i, off);
			return -EINVAL;
		}
next:
		if (i == subprog_end - 1) {
			/* to avoid fall-through from one subprog into another
			 * the last insn of the subprog should be either exit
			 * or unconditional jump back or bpf_throw call
			 */
			if (code != (BPF_JMP | BPF_EXIT) &&
			    code != (BPF_JMP32 | BPF_JA) &&
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
				reg_type_str(env, parent->type),
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

static int mark_dynptr_read(struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	struct bpf_func_state *state = func(env, reg);
	int spi, ret;

	/* For CONST_PTR_TO_DYNPTR, it must have already been done by
	 * check_reg_arg in check_helper_call and mark_btf_func_reg_size in
	 * check_kfunc_call.
	 */
	if (reg->type == CONST_PTR_TO_DYNPTR)
		return 0;
	spi = dynptr_get_spi(env, reg);
	if (spi < 0)
		return spi;
	/* Caller ensures dynptr is valid and initialized, which means spi is in
	 * bounds and spi is the first dynptr slot. Simply mark stack slot as
	 * read.
	 */
	ret = mark_reg_read(env, &state->stack[spi].spilled_ptr,
			    state->stack[spi].spilled_ptr.parent, REG_LIVE_READ64);
	if (ret)
		return ret;
	return mark_reg_read(env, &state->stack[spi - 1].spilled_ptr,
			     state->stack[spi - 1].spilled_ptr.parent, REG_LIVE_READ64);
}

static int mark_iter_read(struct bpf_verifier_env *env, struct bpf_reg_state *reg,
			  int spi, int nr_slots)
{
	struct bpf_func_state *state = func(env, reg);
	int err, i;

	for (i = 0; i < nr_slots; i++) {
		struct bpf_reg_state *st = &state->stack[spi - i].spilled_ptr;

		err = mark_reg_read(env, st, st->parent, REG_LIVE_READ64);
		if (err)
			return err;

		mark_stack_slot_scratched(env, spi - i);
	}

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

	if (class == BPF_ALU64 && op == BPF_END && (insn->imm == 16 || insn->imm == 32))
		return false;

	if (class == BPF_ALU64 || class == BPF_JMP ||
	    (class == BPF_ALU && op == BPF_END && insn->imm == 64))
		return true;

	if (class == BPF_ALU || class == BPF_JMP32)
		return false;

	if (class == BPF_LDX) {
		if (t != SRC_OP)
			return BPF_SIZE(code) == BPF_DW || BPF_MODE(code) == BPF_MEMSX;
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
		if ((BPF_MODE(insn->code) == BPF_ATOMIC ||
		     BPF_MODE(insn->code) == BPF_PROBE_ATOMIC) &&
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

static int __check_reg_arg(struct bpf_verifier_env *env, struct bpf_reg_state *regs, u32 regno,
			   enum reg_arg_type t)
{
	struct bpf_insn *insn = env->prog->insnsi + env->insn_idx;
	struct bpf_reg_state *reg;
	bool rw64;

	if (regno >= MAX_BPF_REG) {
		verbose(env, "R%d is invalid\n", regno);
		return -EINVAL;
	}

	mark_reg_scratched(env, regno);

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

static int check_reg_arg(struct bpf_verifier_env *env, u32 regno,
			 enum reg_arg_type t)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	struct bpf_func_state *state = vstate->frame[vstate->curframe];

	return __check_reg_arg(env, state->regs, regno, t);
}

static int insn_stack_access_flags(int frameno, int spi)
{
	return INSN_F_STACK_ACCESS | (spi << INSN_F_SPI_SHIFT) | frameno;
}

static int insn_stack_access_spi(int insn_flags)
{
	return (insn_flags >> INSN_F_SPI_SHIFT) & INSN_F_SPI_MASK;
}

static int insn_stack_access_frameno(int insn_flags)
{
	return insn_flags & INSN_F_FRAMENO_MASK;
}

static void mark_jmp_point(struct bpf_verifier_env *env, int idx)
{
	env->insn_aux_data[idx].jmp_point = true;
}

static bool is_jmp_point(struct bpf_verifier_env *env, int insn_idx)
{
	return env->insn_aux_data[insn_idx].jmp_point;
}

#define LR_FRAMENO_BITS	3
#define LR_SPI_BITS	6
#define LR_ENTRY_BITS	(LR_SPI_BITS + LR_FRAMENO_BITS + 1)
#define LR_SIZE_BITS	4
#define LR_FRAMENO_MASK	((1ull << LR_FRAMENO_BITS) - 1)
#define LR_SPI_MASK	((1ull << LR_SPI_BITS)     - 1)
#define LR_SIZE_MASK	((1ull << LR_SIZE_BITS)    - 1)
#define LR_SPI_OFF	LR_FRAMENO_BITS
#define LR_IS_REG_OFF	(LR_SPI_BITS + LR_FRAMENO_BITS)
#define LINKED_REGS_MAX	6

struct linked_reg {
	u8 frameno;
	union {
		u8 spi;
		u8 regno;
	};
	bool is_reg;
};

struct linked_regs {
	int cnt;
	struct linked_reg entries[LINKED_REGS_MAX];
};

static struct linked_reg *linked_regs_push(struct linked_regs *s)
{
	if (s->cnt < LINKED_REGS_MAX)
		return &s->entries[s->cnt++];

	return NULL;
}

/* Use u64 as a vector of 6 10-bit values, use first 4-bits to track
 * number of elements currently in stack.
 * Pack one history entry for linked registers as 10 bits in the following format:
 * - 3-bits frameno
 * - 6-bits spi_or_reg
 * - 1-bit  is_reg
 */
static u64 linked_regs_pack(struct linked_regs *s)
{
	u64 val = 0;
	int i;

	for (i = 0; i < s->cnt; ++i) {
		struct linked_reg *e = &s->entries[i];
		u64 tmp = 0;

		tmp |= e->frameno;
		tmp |= e->spi << LR_SPI_OFF;
		tmp |= (e->is_reg ? 1 : 0) << LR_IS_REG_OFF;

		val <<= LR_ENTRY_BITS;
		val |= tmp;
	}
	val <<= LR_SIZE_BITS;
	val |= s->cnt;
	return val;
}

static void linked_regs_unpack(u64 val, struct linked_regs *s)
{
	int i;

	s->cnt = val & LR_SIZE_MASK;
	val >>= LR_SIZE_BITS;

	for (i = 0; i < s->cnt; ++i) {
		struct linked_reg *e = &s->entries[i];

		e->frameno =  val & LR_FRAMENO_MASK;
		e->spi     = (val >> LR_SPI_OFF) & LR_SPI_MASK;
		e->is_reg  = (val >> LR_IS_REG_OFF) & 0x1;
		val >>= LR_ENTRY_BITS;
	}
}

/* for any branch, call, exit record the history of jmps in the given state */
static int push_insn_history(struct bpf_verifier_env *env, struct bpf_verifier_state *cur,
			     int insn_flags, u64 linked_regs)
{
	struct bpf_insn_hist_entry *p;
	size_t alloc_size;

	/* combine instruction flags if we already recorded this instruction */
	if (env->cur_hist_ent) {
		/* atomic instructions push insn_flags twice, for READ and
		 * WRITE sides, but they should agree on stack slot
		 */
		WARN_ONCE((env->cur_hist_ent->flags & insn_flags) &&
			  (env->cur_hist_ent->flags & insn_flags) != insn_flags,
			  "verifier insn history bug: insn_idx %d cur flags %x new flags %x\n",
			  env->insn_idx, env->cur_hist_ent->flags, insn_flags);
		env->cur_hist_ent->flags |= insn_flags;
		WARN_ONCE(env->cur_hist_ent->linked_regs != 0,
			  "verifier insn history bug: insn_idx %d linked_regs != 0: %#llx\n",
			  env->insn_idx, env->cur_hist_ent->linked_regs);
		env->cur_hist_ent->linked_regs = linked_regs;
		return 0;
	}

	if (cur->insn_hist_end + 1 > env->insn_hist_cap) {
		alloc_size = size_mul(cur->insn_hist_end + 1, sizeof(*p));
		p = kvrealloc(env->insn_hist, alloc_size, GFP_USER);
		if (!p)
			return -ENOMEM;
		env->insn_hist = p;
		env->insn_hist_cap = alloc_size / sizeof(*p);
	}

	p = &env->insn_hist[cur->insn_hist_end];
	p->idx = env->insn_idx;
	p->prev_idx = env->prev_insn_idx;
	p->flags = insn_flags;
	p->linked_regs = linked_regs;

	cur->insn_hist_end++;
	env->cur_hist_ent = p;

	return 0;
}

static struct bpf_insn_hist_entry *get_insn_hist_entry(struct bpf_verifier_env *env,
						       u32 hist_start, u32 hist_end, int insn_idx)
{
	if (hist_end > hist_start && env->insn_hist[hist_end - 1].idx == insn_idx)
		return &env->insn_hist[hist_end - 1];
	return NULL;
}

/* Backtrack one insn at a time. If idx is not at the top of recorded
 * history then previous instruction came from straight line execution.
 * Return -ENOENT if we exhausted all instructions within given state.
 *
 * It's legal to have a bit of a looping with the same starting and ending
 * insn index within the same state, e.g.: 3->4->5->3, so just because current
 * instruction index is the same as state's first_idx doesn't mean we are
 * done. If there is still some jump history left, we should keep going. We
 * need to take into account that we might have a jump history between given
 * state's parent and itself, due to checkpointing. In this case, we'll have
 * history entry recording a jump from last instruction of parent state and
 * first instruction of given state.
 */
static int get_prev_insn_idx(const struct bpf_verifier_env *env,
			     struct bpf_verifier_state *st,
			     int insn_idx, u32 hist_start, u32 *hist_endp)
{
	u32 hist_end = *hist_endp;
	u32 cnt = hist_end - hist_start;

	if (insn_idx == st->first_insn_idx) {
		if (cnt == 0)
			return -ENOENT;
		if (cnt == 1 && env->insn_hist[hist_start].idx == insn_idx)
			return -ENOENT;
	}

	if (cnt && env->insn_hist[hist_end - 1].idx == insn_idx) {
		(*hist_endp)--;
		return env->insn_hist[hist_end - 1].prev_idx;
	} else {
		return insn_idx - 1;
	}
}

static const char *disasm_kfunc_name(void *data, const struct bpf_insn *insn)
{
	const struct btf_type *func;
	struct btf *desc_btf;

	if (insn->src_reg != BPF_PSEUDO_KFUNC_CALL)
		return NULL;

	desc_btf = find_kfunc_desc_btf(data, insn->off);
	if (IS_ERR(desc_btf))
		return "<error>";

	func = btf_type_by_id(desc_btf, insn->imm);
	return btf_name_by_offset(desc_btf, func->name_off);
}

static inline void bt_init(struct backtrack_state *bt, u32 frame)
{
	bt->frame = frame;
}

static inline void bt_reset(struct backtrack_state *bt)
{
	struct bpf_verifier_env *env = bt->env;

	memset(bt, 0, sizeof(*bt));
	bt->env = env;
}

static inline u32 bt_empty(struct backtrack_state *bt)
{
	u64 mask = 0;
	int i;

	for (i = 0; i <= bt->frame; i++)
		mask |= bt->reg_masks[i] | bt->stack_masks[i];

	return mask == 0;
}

static inline int bt_subprog_enter(struct backtrack_state *bt)
{
	if (bt->frame == MAX_CALL_FRAMES - 1) {
		verbose(bt->env, "BUG subprog enter from frame %d\n", bt->frame);
		WARN_ONCE(1, "verifier backtracking bug");
		return -EFAULT;
	}
	bt->frame++;
	return 0;
}

static inline int bt_subprog_exit(struct backtrack_state *bt)
{
	if (bt->frame == 0) {
		verbose(bt->env, "BUG subprog exit from frame 0\n");
		WARN_ONCE(1, "verifier backtracking bug");
		return -EFAULT;
	}
	bt->frame--;
	return 0;
}

static inline void bt_set_frame_reg(struct backtrack_state *bt, u32 frame, u32 reg)
{
	bt->reg_masks[frame] |= 1 << reg;
}

static inline void bt_clear_frame_reg(struct backtrack_state *bt, u32 frame, u32 reg)
{
	bt->reg_masks[frame] &= ~(1 << reg);
}

static inline void bt_set_reg(struct backtrack_state *bt, u32 reg)
{
	bt_set_frame_reg(bt, bt->frame, reg);
}

static inline void bt_clear_reg(struct backtrack_state *bt, u32 reg)
{
	bt_clear_frame_reg(bt, bt->frame, reg);
}

static inline void bt_set_frame_slot(struct backtrack_state *bt, u32 frame, u32 slot)
{
	bt->stack_masks[frame] |= 1ull << slot;
}

static inline void bt_clear_frame_slot(struct backtrack_state *bt, u32 frame, u32 slot)
{
	bt->stack_masks[frame] &= ~(1ull << slot);
}

static inline u32 bt_frame_reg_mask(struct backtrack_state *bt, u32 frame)
{
	return bt->reg_masks[frame];
}

static inline u32 bt_reg_mask(struct backtrack_state *bt)
{
	return bt->reg_masks[bt->frame];
}

static inline u64 bt_frame_stack_mask(struct backtrack_state *bt, u32 frame)
{
	return bt->stack_masks[frame];
}

static inline u64 bt_stack_mask(struct backtrack_state *bt)
{
	return bt->stack_masks[bt->frame];
}

static inline bool bt_is_reg_set(struct backtrack_state *bt, u32 reg)
{
	return bt->reg_masks[bt->frame] & (1 << reg);
}

static inline bool bt_is_frame_reg_set(struct backtrack_state *bt, u32 frame, u32 reg)
{
	return bt->reg_masks[frame] & (1 << reg);
}

static inline bool bt_is_frame_slot_set(struct backtrack_state *bt, u32 frame, u32 slot)
{
	return bt->stack_masks[frame] & (1ull << slot);
}

/* format registers bitmask, e.g., "r0,r2,r4" for 0x15 mask */
static void fmt_reg_mask(char *buf, ssize_t buf_sz, u32 reg_mask)
{
	DECLARE_BITMAP(mask, 64);
	bool first = true;
	int i, n;

	buf[0] = '\0';

	bitmap_from_u64(mask, reg_mask);
	for_each_set_bit(i, mask, 32) {
		n = snprintf(buf, buf_sz, "%sr%d", first ? "" : ",", i);
		first = false;
		buf += n;
		buf_sz -= n;
		if (buf_sz < 0)
			break;
	}
}
/* format stack slots bitmask, e.g., "-8,-24,-40" for 0x15 mask */
static void fmt_stack_mask(char *buf, ssize_t buf_sz, u64 stack_mask)
{
	DECLARE_BITMAP(mask, 64);
	bool first = true;
	int i, n;

	buf[0] = '\0';

	bitmap_from_u64(mask, stack_mask);
	for_each_set_bit(i, mask, 64) {
		n = snprintf(buf, buf_sz, "%s%d", first ? "" : ",", -(i + 1) * 8);
		first = false;
		buf += n;
		buf_sz -= n;
		if (buf_sz < 0)
			break;
	}
}

/* If any register R in hist->linked_regs is marked as precise in bt,
 * do bt_set_frame_{reg,slot}(bt, R) for all registers in hist->linked_regs.
 */
static void bt_sync_linked_regs(struct backtrack_state *bt, struct bpf_insn_hist_entry *hist)
{
	struct linked_regs linked_regs;
	bool some_precise = false;
	int i;

	if (!hist || hist->linked_regs == 0)
		return;

	linked_regs_unpack(hist->linked_regs, &linked_regs);
	for (i = 0; i < linked_regs.cnt; ++i) {
		struct linked_reg *e = &linked_regs.entries[i];

		if ((e->is_reg && bt_is_frame_reg_set(bt, e->frameno, e->regno)) ||
		    (!e->is_reg && bt_is_frame_slot_set(bt, e->frameno, e->spi))) {
			some_precise = true;
			break;
		}
	}

	if (!some_precise)
		return;

	for (i = 0; i < linked_regs.cnt; ++i) {
		struct linked_reg *e = &linked_regs.entries[i];

		if (e->is_reg)
			bt_set_frame_reg(bt, e->frameno, e->regno);
		else
			bt_set_frame_slot(bt, e->frameno, e->spi);
	}
}

static bool calls_callback(struct bpf_verifier_env *env, int insn_idx);

/* For given verifier state backtrack_insn() is called from the last insn to
 * the first insn. Its purpose is to compute a bitmask of registers and
 * stack slots that needs precision in the parent verifier state.
 *
 * @idx is an index of the instruction we are currently processing;
 * @subseq_idx is an index of the subsequent instruction that:
 *   - *would be* executed next, if jump history is viewed in forward order;
 *   - *was* processed previously during backtracking.
 */
static int backtrack_insn(struct bpf_verifier_env *env, int idx, int subseq_idx,
			  struct bpf_insn_hist_entry *hist, struct backtrack_state *bt)
{
	const struct bpf_insn_cbs cbs = {
		.cb_call	= disasm_kfunc_name,
		.cb_print	= verbose,
		.private_data	= env,
	};
	struct bpf_insn *insn = env->prog->insnsi + idx;
	u8 class = BPF_CLASS(insn->code);
	u8 opcode = BPF_OP(insn->code);
	u8 mode = BPF_MODE(insn->code);
	u32 dreg = insn->dst_reg;
	u32 sreg = insn->src_reg;
	u32 spi, i, fr;

	if (insn->code == 0)
		return 0;
	if (env->log.level & BPF_LOG_LEVEL2) {
		fmt_reg_mask(env->tmp_str_buf, TMP_STR_BUF_LEN, bt_reg_mask(bt));
		verbose(env, "mark_precise: frame%d: regs=%s ",
			bt->frame, env->tmp_str_buf);
		fmt_stack_mask(env->tmp_str_buf, TMP_STR_BUF_LEN, bt_stack_mask(bt));
		verbose(env, "stack=%s before ", env->tmp_str_buf);
		verbose(env, "%d: ", idx);
		print_bpf_insn(&cbs, insn, env->allow_ptr_leaks);
	}

	/* If there is a history record that some registers gained range at this insn,
	 * propagate precision marks to those registers, so that bt_is_reg_set()
	 * accounts for these registers.
	 */
	bt_sync_linked_regs(bt, hist);

	if (class == BPF_ALU || class == BPF_ALU64) {
		if (!bt_is_reg_set(bt, dreg))
			return 0;
		if (opcode == BPF_END || opcode == BPF_NEG) {
			/* sreg is reserved and unused
			 * dreg still need precision before this insn
			 */
			return 0;
		} else if (opcode == BPF_MOV) {
			if (BPF_SRC(insn->code) == BPF_X) {
				/* dreg = sreg or dreg = (s8, s16, s32)sreg
				 * dreg needs precision after this insn
				 * sreg needs precision before this insn
				 */
				bt_clear_reg(bt, dreg);
				if (sreg != BPF_REG_FP)
					bt_set_reg(bt, sreg);
			} else {
				/* dreg = K
				 * dreg needs precision after this insn.
				 * Corresponding register is already marked
				 * as precise=true in this verifier state.
				 * No further markings in parent are necessary
				 */
				bt_clear_reg(bt, dreg);
			}
		} else {
			if (BPF_SRC(insn->code) == BPF_X) {
				/* dreg += sreg
				 * both dreg and sreg need precision
				 * before this insn
				 */
				if (sreg != BPF_REG_FP)
					bt_set_reg(bt, sreg);
			} /* else dreg += K
			   * dreg still needs precision before this insn
			   */
		}
	} else if (class == BPF_LDX) {
		if (!bt_is_reg_set(bt, dreg))
			return 0;
		bt_clear_reg(bt, dreg);

		/* scalars can only be spilled into stack w/o losing precision.
		 * Load from any other memory can be zero extended.
		 * The desire to keep that precision is already indicated
		 * by 'precise' mark in corresponding register of this state.
		 * No further tracking necessary.
		 */
		if (!hist || !(hist->flags & INSN_F_STACK_ACCESS))
			return 0;
		/* dreg = *(u64 *)[fp - off] was a fill from the stack.
		 * that [fp - off] slot contains scalar that needs to be
		 * tracked with precision
		 */
		spi = insn_stack_access_spi(hist->flags);
		fr = insn_stack_access_frameno(hist->flags);
		bt_set_frame_slot(bt, fr, spi);
	} else if (class == BPF_STX || class == BPF_ST) {
		if (bt_is_reg_set(bt, dreg))
			/* stx & st shouldn't be using _scalar_ dst_reg
			 * to access memory. It means backtracking
			 * encountered a case of pointer subtraction.
			 */
			return -ENOTSUPP;
		/* scalars can only be spilled into stack */
		if (!hist || !(hist->flags & INSN_F_STACK_ACCESS))
			return 0;
		spi = insn_stack_access_spi(hist->flags);
		fr = insn_stack_access_frameno(hist->flags);
		if (!bt_is_frame_slot_set(bt, fr, spi))
			return 0;
		bt_clear_frame_slot(bt, fr, spi);
		if (class == BPF_STX)
			bt_set_reg(bt, sreg);
	} else if (class == BPF_JMP || class == BPF_JMP32) {
		if (bpf_pseudo_call(insn)) {
			int subprog_insn_idx, subprog;

			subprog_insn_idx = idx + insn->imm + 1;
			subprog = find_subprog(env, subprog_insn_idx);
			if (subprog < 0)
				return -EFAULT;

			if (subprog_is_global(env, subprog)) {
				/* check that jump history doesn't have any
				 * extra instructions from subprog; the next
				 * instruction after call to global subprog
				 * should be literally next instruction in
				 * caller program
				 */
				WARN_ONCE(idx + 1 != subseq_idx, "verifier backtracking bug");
				/* r1-r5 are invalidated after subprog call,
				 * so for global func call it shouldn't be set
				 * anymore
				 */
				if (bt_reg_mask(bt) & BPF_REGMASK_ARGS) {
					verbose(env, "BUG regs %x\n", bt_reg_mask(bt));
					WARN_ONCE(1, "verifier backtracking bug");
					return -EFAULT;
				}
				/* global subprog always sets R0 */
				bt_clear_reg(bt, BPF_REG_0);
				return 0;
			} else {
				/* static subprog call instruction, which
				 * means that we are exiting current subprog,
				 * so only r1-r5 could be still requested as
				 * precise, r0 and r6-r10 or any stack slot in
				 * the current frame should be zero by now
				 */
				if (bt_reg_mask(bt) & ~BPF_REGMASK_ARGS) {
					verbose(env, "BUG regs %x\n", bt_reg_mask(bt));
					WARN_ONCE(1, "verifier backtracking bug");
					return -EFAULT;
				}
				/* we are now tracking register spills correctly,
				 * so any instance of leftover slots is a bug
				 */
				if (bt_stack_mask(bt) != 0) {
					verbose(env, "BUG stack slots %llx\n", bt_stack_mask(bt));
					WARN_ONCE(1, "verifier backtracking bug (subprog leftover stack slots)");
					return -EFAULT;
				}
				/* propagate r1-r5 to the caller */
				for (i = BPF_REG_1; i <= BPF_REG_5; i++) {
					if (bt_is_reg_set(bt, i)) {
						bt_clear_reg(bt, i);
						bt_set_frame_reg(bt, bt->frame - 1, i);
					}
				}
				if (bt_subprog_exit(bt))
					return -EFAULT;
				return 0;
			}
		} else if (is_sync_callback_calling_insn(insn) && idx != subseq_idx - 1) {
			/* exit from callback subprog to callback-calling helper or
			 * kfunc call. Use idx/subseq_idx check to discern it from
			 * straight line code backtracking.
			 * Unlike the subprog call handling above, we shouldn't
			 * propagate precision of r1-r5 (if any requested), as they are
			 * not actually arguments passed directly to callback subprogs
			 */
			if (bt_reg_mask(bt) & ~BPF_REGMASK_ARGS) {
				verbose(env, "BUG regs %x\n", bt_reg_mask(bt));
				WARN_ONCE(1, "verifier backtracking bug");
				return -EFAULT;
			}
			if (bt_stack_mask(bt) != 0) {
				verbose(env, "BUG stack slots %llx\n", bt_stack_mask(bt));
				WARN_ONCE(1, "verifier backtracking bug (callback leftover stack slots)");
				return -EFAULT;
			}
			/* clear r1-r5 in callback subprog's mask */
			for (i = BPF_REG_1; i <= BPF_REG_5; i++)
				bt_clear_reg(bt, i);
			if (bt_subprog_exit(bt))
				return -EFAULT;
			return 0;
		} else if (opcode == BPF_CALL) {
			/* kfunc with imm==0 is invalid and fixup_kfunc_call will
			 * catch this error later. Make backtracking conservative
			 * with ENOTSUPP.
			 */
			if (insn->src_reg == BPF_PSEUDO_KFUNC_CALL && insn->imm == 0)
				return -ENOTSUPP;
			/* regular helper call sets R0 */
			bt_clear_reg(bt, BPF_REG_0);
			if (bt_reg_mask(bt) & BPF_REGMASK_ARGS) {
				/* if backtracing was looking for registers R1-R5
				 * they should have been found already.
				 */
				verbose(env, "BUG regs %x\n", bt_reg_mask(bt));
				WARN_ONCE(1, "verifier backtracking bug");
				return -EFAULT;
			}
		} else if (opcode == BPF_EXIT) {
			bool r0_precise;

			/* Backtracking to a nested function call, 'idx' is a part of
			 * the inner frame 'subseq_idx' is a part of the outer frame.
			 * In case of a regular function call, instructions giving
			 * precision to registers R1-R5 should have been found already.
			 * In case of a callback, it is ok to have R1-R5 marked for
			 * backtracking, as these registers are set by the function
			 * invoking callback.
			 */
			if (subseq_idx >= 0 && calls_callback(env, subseq_idx))
				for (i = BPF_REG_1; i <= BPF_REG_5; i++)
					bt_clear_reg(bt, i);
			if (bt_reg_mask(bt) & BPF_REGMASK_ARGS) {
				verbose(env, "BUG regs %x\n", bt_reg_mask(bt));
				WARN_ONCE(1, "verifier backtracking bug");
				return -EFAULT;
			}

			/* BPF_EXIT in subprog or callback always returns
			 * right after the call instruction, so by checking
			 * whether the instruction at subseq_idx-1 is subprog
			 * call or not we can distinguish actual exit from
			 * *subprog* from exit from *callback*. In the former
			 * case, we need to propagate r0 precision, if
			 * necessary. In the former we never do that.
			 */
			r0_precise = subseq_idx - 1 >= 0 &&
				     bpf_pseudo_call(&env->prog->insnsi[subseq_idx - 1]) &&
				     bt_is_reg_set(bt, BPF_REG_0);

			bt_clear_reg(bt, BPF_REG_0);
			if (bt_subprog_enter(bt))
				return -EFAULT;

			if (r0_precise)
				bt_set_reg(bt, BPF_REG_0);
			/* r6-r9 and stack slots will stay set in caller frame
			 * bitmasks until we return back from callee(s)
			 */
			return 0;
		} else if (BPF_SRC(insn->code) == BPF_X) {
			if (!bt_is_reg_set(bt, dreg) && !bt_is_reg_set(bt, sreg))
				return 0;
			/* dreg <cond> sreg
			 * Both dreg and sreg need precision before
			 * this insn. If only sreg was marked precise
			 * before it would be equally necessary to
			 * propagate it to dreg.
			 */
			bt_set_reg(bt, dreg);
			bt_set_reg(bt, sreg);
		} else if (BPF_SRC(insn->code) == BPF_K) {
			 /* dreg <cond> K
			  * Only dreg still needs precision before
			  * this insn, so for the K-based conditional
			  * there is nothing new to be marked.
			  */
		}
	} else if (class == BPF_LD) {
		if (!bt_is_reg_set(bt, dreg))
			return 0;
		bt_clear_reg(bt, dreg);
		/* It's ld_imm64 or ld_abs or ld_ind.
		 * For ld_imm64 no further tracking of precision
		 * into parent is necessary
		 */
		if (mode == BPF_IND || mode == BPF_ABS)
			/* to be analyzed */
			return -ENOTSUPP;
	}
	/* Propagate precision marks to linked registers, to account for
	 * registers marked as precise in this function.
	 */
	bt_sync_linked_regs(bt, hist);
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

	if (env->log.level & BPF_LOG_LEVEL2) {
		verbose(env, "mark_precise: frame%d: falling back to forcing all scalars precise\n",
			st->curframe);
	}

	/* big hammer: mark all scalars precise in this path.
	 * pop_stack may still get !precise scalars.
	 * We also skip current state and go straight to first parent state,
	 * because precision markings in current non-checkpointed state are
	 * not needed. See why in the comment in __mark_chain_precision below.
	 */
	for (st = st->parent; st; st = st->parent) {
		for (i = 0; i <= st->curframe; i++) {
			func = st->frame[i];
			for (j = 0; j < BPF_REG_FP; j++) {
				reg = &func->regs[j];
				if (reg->type != SCALAR_VALUE || reg->precise)
					continue;
				reg->precise = true;
				if (env->log.level & BPF_LOG_LEVEL2) {
					verbose(env, "force_precise: frame%d: forcing r%d to be precise\n",
						i, j);
				}
			}
			for (j = 0; j < func->allocated_stack / BPF_REG_SIZE; j++) {
				if (!is_spilled_reg(&func->stack[j]))
					continue;
				reg = &func->stack[j].spilled_ptr;
				if (reg->type != SCALAR_VALUE || reg->precise)
					continue;
				reg->precise = true;
				if (env->log.level & BPF_LOG_LEVEL2) {
					verbose(env, "force_precise: frame%d: forcing fp%d to be precise\n",
						i, -(j + 1) * 8);
				}
			}
		}
	}
}

static void mark_all_scalars_imprecise(struct bpf_verifier_env *env, struct bpf_verifier_state *st)
{
	struct bpf_func_state *func;
	struct bpf_reg_state *reg;
	int i, j;

	for (i = 0; i <= st->curframe; i++) {
		func = st->frame[i];
		for (j = 0; j < BPF_REG_FP; j++) {
			reg = &func->regs[j];
			if (reg->type != SCALAR_VALUE)
				continue;
			reg->precise = false;
		}
		for (j = 0; j < func->allocated_stack / BPF_REG_SIZE; j++) {
			if (!is_spilled_reg(&func->stack[j]))
				continue;
			reg = &func->stack[j].spilled_ptr;
			if (reg->type != SCALAR_VALUE)
				continue;
			reg->precise = false;
		}
	}
}

/*
 * __mark_chain_precision() backtracks BPF program instruction sequence and
 * chain of verifier states making sure that register *regno* (if regno >= 0)
 * and/or stack slot *spi* (if spi >= 0) are marked as precisely tracked
 * SCALARS, as well as any other registers and slots that contribute to
 * a tracked state of given registers/stack slots, depending on specific BPF
 * assembly instructions (see backtrack_insns() for exact instruction handling
 * logic). This backtracking relies on recorded insn_hist and is able to
 * traverse entire chain of parent states. This process ends only when all the
 * necessary registers/slots and their transitive dependencies are marked as
 * precise.
 *
 * One important and subtle aspect is that precise marks *do not matter* in
 * the currently verified state (current state). It is important to understand
 * why this is the case.
 *
 * First, note that current state is the state that is not yet "checkpointed",
 * i.e., it is not yet put into env->explored_states, and it has no children
 * states as well. It's ephemeral, and can end up either a) being discarded if
 * compatible explored state is found at some point or BPF_EXIT instruction is
 * reached or b) checkpointed and put into env->explored_states, branching out
 * into one or more children states.
 *
 * In the former case, precise markings in current state are completely
 * ignored by state comparison code (see regsafe() for details). Only
 * checkpointed ("old") state precise markings are important, and if old
 * state's register/slot is precise, regsafe() assumes current state's
 * register/slot as precise and checks value ranges exactly and precisely. If
 * states turn out to be compatible, current state's necessary precise
 * markings and any required parent states' precise markings are enforced
 * after the fact with propagate_precision() logic, after the fact. But it's
 * important to realize that in this case, even after marking current state
 * registers/slots as precise, we immediately discard current state. So what
 * actually matters is any of the precise markings propagated into current
 * state's parent states, which are always checkpointed (due to b) case above).
 * As such, for scenario a) it doesn't matter if current state has precise
 * markings set or not.
 *
 * Now, for the scenario b), checkpointing and forking into child(ren)
 * state(s). Note that before current state gets to checkpointing step, any
 * processed instruction always assumes precise SCALAR register/slot
 * knowledge: if precise value or range is useful to prune jump branch, BPF
 * verifier takes this opportunity enthusiastically. Similarly, when
 * register's value is used to calculate offset or memory address, exact
 * knowledge of SCALAR range is assumed, checked, and enforced. So, similar to
 * what we mentioned above about state comparison ignoring precise markings
 * during state comparison, BPF verifier ignores and also assumes precise
 * markings *at will* during instruction verification process. But as verifier
 * assumes precision, it also propagates any precision dependencies across
 * parent states, which are not yet finalized, so can be further restricted
 * based on new knowledge gained from restrictions enforced by their children
 * states. This is so that once those parent states are finalized, i.e., when
 * they have no more active children state, state comparison logic in
 * is_state_visited() would enforce strict and precise SCALAR ranges, if
 * required for correctness.
 *
 * To build a bit more intuition, note also that once a state is checkpointed,
 * the path we took to get to that state is not important. This is crucial
 * property for state pruning. When state is checkpointed and finalized at
 * some instruction index, it can be correctly and safely used to "short
 * circuit" any *compatible* state that reaches exactly the same instruction
 * index. I.e., if we jumped to that instruction from a completely different
 * code path than original finalized state was derived from, it doesn't
 * matter, current state can be discarded because from that instruction
 * forward having a compatible state will ensure we will safely reach the
 * exit. States describe preconditions for further exploration, but completely
 * forget the history of how we got here.
 *
 * This also means that even if we needed precise SCALAR range to get to
 * finalized state, but from that point forward *that same* SCALAR register is
 * never used in a precise context (i.e., it's precise value is not needed for
 * correctness), it's correct and safe to mark such register as "imprecise"
 * (i.e., precise marking set to false). This is what we rely on when we do
 * not set precise marking in current state. If no child state requires
 * precision for any given SCALAR register, it's safe to dictate that it can
 * be imprecise. If any child state does require this register to be precise,
 * we'll mark it precise later retroactively during precise markings
 * propagation from child state to parent states.
 *
 * Skipping precise marking setting in current state is a mild version of
 * relying on the above observation. But we can utilize this property even
 * more aggressively by proactively forgetting any precise marking in the
 * current state (which we inherited from the parent state), right before we
 * checkpoint it and branch off into new child state. This is done by
 * mark_all_scalars_imprecise() to hopefully get more permissive and generic
 * finalized states which help in short circuiting more future states.
 */
static int __mark_chain_precision(struct bpf_verifier_env *env, int regno)
{
	struct backtrack_state *bt = &env->bt;
	struct bpf_verifier_state *st = env->cur_state;
	int first_idx = st->first_insn_idx;
	int last_idx = env->insn_idx;
	int subseq_idx = -1;
	struct bpf_func_state *func;
	struct bpf_reg_state *reg;
	bool skip_first = true;
	int i, fr, err;

	if (!env->bpf_capable)
		return 0;

	/* set frame number from which we are starting to backtrack */
	bt_init(bt, env->cur_state->curframe);

	/* Do sanity checks against current state of register and/or stack
	 * slot, but don't set precise flag in current state, as precision
	 * tracking in the current state is unnecessary.
	 */
	func = st->frame[bt->frame];
	if (regno >= 0) {
		reg = &func->regs[regno];
		if (reg->type != SCALAR_VALUE) {
			WARN_ONCE(1, "backtracing misuse");
			return -EFAULT;
		}
		bt_set_reg(bt, regno);
	}

	if (bt_empty(bt))
		return 0;

	for (;;) {
		DECLARE_BITMAP(mask, 64);
		u32 hist_start = st->insn_hist_start;
		u32 hist_end = st->insn_hist_end;
		struct bpf_insn_hist_entry *hist;

		if (env->log.level & BPF_LOG_LEVEL2) {
			verbose(env, "mark_precise: frame%d: last_idx %d first_idx %d subseq_idx %d \n",
				bt->frame, last_idx, first_idx, subseq_idx);
		}

		if (last_idx < 0) {
			/* we are at the entry into subprog, which
			 * is expected for global funcs, but only if
			 * requested precise registers are R1-R5
			 * (which are global func's input arguments)
			 */
			if (st->curframe == 0 &&
			    st->frame[0]->subprogno > 0 &&
			    st->frame[0]->callsite == BPF_MAIN_FUNC &&
			    bt_stack_mask(bt) == 0 &&
			    (bt_reg_mask(bt) & ~BPF_REGMASK_ARGS) == 0) {
				bitmap_from_u64(mask, bt_reg_mask(bt));
				for_each_set_bit(i, mask, 32) {
					reg = &st->frame[0]->regs[i];
					bt_clear_reg(bt, i);
					if (reg->type == SCALAR_VALUE)
						reg->precise = true;
				}
				return 0;
			}

			verbose(env, "BUG backtracking func entry subprog %d reg_mask %x stack_mask %llx\n",
				st->frame[0]->subprogno, bt_reg_mask(bt), bt_stack_mask(bt));
			WARN_ONCE(1, "verifier backtracking bug");
			return -EFAULT;
		}

		for (i = last_idx;;) {
			if (skip_first) {
				err = 0;
				skip_first = false;
			} else {
				hist = get_insn_hist_entry(env, hist_start, hist_end, i);
				err = backtrack_insn(env, i, subseq_idx, hist, bt);
			}
			if (err == -ENOTSUPP) {
				mark_all_scalars_precise(env, env->cur_state);
				bt_reset(bt);
				return 0;
			} else if (err) {
				return err;
			}
			if (bt_empty(bt))
				/* Found assignment(s) into tracked register in this state.
				 * Since this state is already marked, just return.
				 * Nothing to be tracked further in the parent state.
				 */
				return 0;
			subseq_idx = i;
			i = get_prev_insn_idx(env, st, i, hist_start, &hist_end);
			if (i == -ENOENT)
				break;
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

		for (fr = bt->frame; fr >= 0; fr--) {
			func = st->frame[fr];
			bitmap_from_u64(mask, bt_frame_reg_mask(bt, fr));
			for_each_set_bit(i, mask, 32) {
				reg = &func->regs[i];
				if (reg->type != SCALAR_VALUE) {
					bt_clear_frame_reg(bt, fr, i);
					continue;
				}
				if (reg->precise)
					bt_clear_frame_reg(bt, fr, i);
				else
					reg->precise = true;
			}

			bitmap_from_u64(mask, bt_frame_stack_mask(bt, fr));
			for_each_set_bit(i, mask, 64) {
				if (i >= func->allocated_stack / BPF_REG_SIZE) {
					verbose(env, "BUG backtracking (stack slot %d, total slots %d)\n",
						i, func->allocated_stack / BPF_REG_SIZE);
					WARN_ONCE(1, "verifier backtracking bug (stack slot out of bounds)");
					return -EFAULT;
				}

				if (!is_spilled_scalar_reg(&func->stack[i])) {
					bt_clear_frame_slot(bt, fr, i);
					continue;
				}
				reg = &func->stack[i].spilled_ptr;
				if (reg->precise)
					bt_clear_frame_slot(bt, fr, i);
				else
					reg->precise = true;
			}
			if (env->log.level & BPF_LOG_LEVEL2) {
				fmt_reg_mask(env->tmp_str_buf, TMP_STR_BUF_LEN,
					     bt_frame_reg_mask(bt, fr));
				verbose(env, "mark_precise: frame%d: parent state regs=%s ",
					fr, env->tmp_str_buf);
				fmt_stack_mask(env->tmp_str_buf, TMP_STR_BUF_LEN,
					       bt_frame_stack_mask(bt, fr));
				verbose(env, "stack=%s: ", env->tmp_str_buf);
				print_verifier_state(env, func, true);
			}
		}

		if (bt_empty(bt))
			return 0;

		subseq_idx = first_idx;
		last_idx = st->last_insn_idx;
		first_idx = st->first_insn_idx;
	}

	/* if we still have requested precise regs or slots, we missed
	 * something (e.g., stack access through non-r10 register), so
	 * fallback to marking all precise
	 */
	if (!bt_empty(bt)) {
		mark_all_scalars_precise(env, env->cur_state);
		bt_reset(bt);
	}

	return 0;
}

int mark_chain_precision(struct bpf_verifier_env *env, int regno)
{
	return __mark_chain_precision(env, regno);
}

/* mark_chain_precision_batch() assumes that env->bt is set in the caller to
 * desired reg and stack masks across all relevant frames
 */
static int mark_chain_precision_batch(struct bpf_verifier_env *env)
{
	return __mark_chain_precision(env, -1);
}

static bool is_spillable_regtype(enum bpf_reg_type type)
{
	switch (base_type(type)) {
	case PTR_TO_MAP_VALUE:
	case PTR_TO_STACK:
	case PTR_TO_CTX:
	case PTR_TO_PACKET:
	case PTR_TO_PACKET_META:
	case PTR_TO_PACKET_END:
	case PTR_TO_FLOW_KEYS:
	case CONST_PTR_TO_MAP:
	case PTR_TO_SOCKET:
	case PTR_TO_SOCK_COMMON:
	case PTR_TO_TCP_SOCK:
	case PTR_TO_XDP_SOCK:
	case PTR_TO_BTF_ID:
	case PTR_TO_BUF:
	case PTR_TO_MEM:
	case PTR_TO_FUNC:
	case PTR_TO_MAP_KEY:
	case PTR_TO_ARENA:
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

/* check if register is a constant scalar value */
static bool is_reg_const(struct bpf_reg_state *reg, bool subreg32)
{
	return reg->type == SCALAR_VALUE &&
	       tnum_is_const(subreg32 ? tnum_subreg(reg->var_off) : reg->var_off);
}

/* assuming is_reg_const() is true, return constant value of a register */
static u64 reg_const_value(struct bpf_reg_state *reg, bool subreg32)
{
	return subreg32 ? tnum_subreg(reg->var_off).value : reg->var_off.value;
}

static bool __is_pointer_value(bool allow_ptr_leaks,
			       const struct bpf_reg_state *reg)
{
	if (allow_ptr_leaks)
		return false;

	return reg->type != SCALAR_VALUE;
}

static void assign_scalar_id_before_mov(struct bpf_verifier_env *env,
					struct bpf_reg_state *src_reg)
{
	if (src_reg->type != SCALAR_VALUE)
		return;

	if (src_reg->id & BPF_ADD_CONST) {
		/*
		 * The verifier is processing rX = rY insn and
		 * rY->id has special linked register already.
		 * Cleared it, since multiple rX += const are not supported.
		 */
		src_reg->id = 0;
		src_reg->off = 0;
	}

	if (!src_reg->id && !tnum_is_const(src_reg->var_off))
		/* Ensure that src_reg has a valid ID that will be copied to
		 * dst_reg and then will be used by sync_linked_regs() to
		 * propagate min/max range.
		 */
		src_reg->id = ++env->id_gen;
}

/* Copy src state preserving dst->parent and dst->live fields */
static void copy_register_state(struct bpf_reg_state *dst, const struct bpf_reg_state *src)
{
	struct bpf_reg_state *parent = dst->parent;
	enum bpf_reg_liveness live = dst->live;

	*dst = *src;
	dst->parent = parent;
	dst->live = live;
}

static void save_register_state(struct bpf_verifier_env *env,
				struct bpf_func_state *state,
				int spi, struct bpf_reg_state *reg,
				int size)
{
	int i;

	copy_register_state(&state->stack[spi].spilled_ptr, reg);
	if (size == BPF_REG_SIZE)
		state->stack[spi].spilled_ptr.live |= REG_LIVE_WRITTEN;

	for (i = BPF_REG_SIZE; i > BPF_REG_SIZE - size; i--)
		state->stack[spi].slot_type[i - 1] = STACK_SPILL;

	/* size < 8 bytes spill */
	for (; i; i--)
		mark_stack_slot_misc(env, &state->stack[spi].slot_type[i - 1]);
}

static bool is_bpf_st_mem(struct bpf_insn *insn)
{
	return BPF_CLASS(insn->code) == BPF_ST && BPF_MODE(insn->code) == BPF_MEM;
}

static int get_reg_width(struct bpf_reg_state *reg)
{
	return fls64(reg->umax_value);
}

/* See comment for mark_fastcall_pattern_for_call() */
static void check_fastcall_stack_contract(struct bpf_verifier_env *env,
					  struct bpf_func_state *state, int insn_idx, int off)
{
	struct bpf_subprog_info *subprog = &env->subprog_info[state->subprogno];
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	int i;

	if (subprog->fastcall_stack_off <= off || aux[insn_idx].fastcall_pattern)
		return;
	/* access to the region [max_stack_depth .. fastcall_stack_off)
	 * from something that is not a part of the fastcall pattern,
	 * disable fastcall rewrites for current subprogram by setting
	 * fastcall_stack_off to a value smaller than any possible offset.
	 */
	subprog->fastcall_stack_off = S16_MIN;
	/* reset fastcall aux flags within subprogram,
	 * happens at most once per subprogram
	 */
	for (i = subprog->start; i < (subprog + 1)->start; ++i) {
		aux[i].fastcall_spills_num = 0;
		aux[i].fastcall_pattern = 0;
	}
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
	struct bpf_insn *insn = &env->prog->insnsi[insn_idx];
	struct bpf_reg_state *reg = NULL;
	int insn_flags = insn_stack_access_flags(state->frameno, spi);

	/* caller checked that off % size == 0 and -MAX_BPF_STACK <= off < 0,
	 * so it's aligned access and [off, off + size) are within stack limits
	 */
	if (!env->allow_ptr_leaks &&
	    is_spilled_reg(&state->stack[spi]) &&
	    !is_spilled_scalar_reg(&state->stack[spi]) &&
	    size != BPF_REG_SIZE) {
		verbose(env, "attempt to corrupt spilled pointer on stack\n");
		return -EACCES;
	}

	cur = env->cur_state->frame[env->cur_state->curframe];
	if (value_regno >= 0)
		reg = &cur->regs[value_regno];
	if (!env->bypass_spec_v4) {
		bool sanitize = reg && is_spillable_regtype(reg->type);

		for (i = 0; i < size; i++) {
			u8 type = state->stack[spi].slot_type[i];

			if (type != STACK_MISC && type != STACK_ZERO) {
				sanitize = true;
				break;
			}
		}

		if (sanitize)
			env->insn_aux_data[insn_idx].sanitize_stack_spill = true;
	}

	err = destroy_if_dynptr_stack_slot(env, state, spi);
	if (err)
		return err;

	check_fastcall_stack_contract(env, state, insn_idx, off);
	mark_stack_slot_scratched(env, spi);
	if (reg && !(off % BPF_REG_SIZE) && reg->type == SCALAR_VALUE && env->bpf_capable) {
		bool reg_value_fits;

		reg_value_fits = get_reg_width(reg) <= BITS_PER_BYTE * size;
		/* Make sure that reg had an ID to build a relation on spill. */
		if (reg_value_fits)
			assign_scalar_id_before_mov(env, reg);
		save_register_state(env, state, spi, reg, size);
		/* Break the relation on a narrowing spill. */
		if (!reg_value_fits)
			state->stack[spi].spilled_ptr.id = 0;
	} else if (!reg && !(off % BPF_REG_SIZE) && is_bpf_st_mem(insn) &&
		   env->bpf_capable) {
		struct bpf_reg_state *tmp_reg = &env->fake_reg[0];

		memset(tmp_reg, 0, sizeof(*tmp_reg));
		__mark_reg_known(tmp_reg, insn->imm);
		tmp_reg->type = SCALAR_VALUE;
		save_register_state(env, state, spi, tmp_reg, size);
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
		save_register_state(env, state, spi, reg, size);
	} else {
		u8 type = STACK_MISC;

		/* regular write of data into stack destroys any spilled ptr */
		state->stack[spi].spilled_ptr.type = NOT_INIT;
		/* Mark slots as STACK_MISC if they belonged to spilled ptr/dynptr/iter. */
		if (is_stack_slot_special(&state->stack[spi]))
			for (i = 0; i < BPF_REG_SIZE; i++)
				scrub_spilled_slot(&state->stack[spi].slot_type[i]);

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
		if ((reg && register_is_null(reg)) ||
		    (!reg && is_bpf_st_mem(insn) && insn->imm == 0)) {
			/* STACK_ZERO case happened because register spill
			 * wasn't properly aligned at the stack slot boundary,
			 * so it's not a register spill anymore; force
			 * originating register to be precise to make
			 * STACK_ZERO correct for subsequent states
			 */
			err = mark_chain_precision(env, value_regno);
			if (err)
				return err;
			type = STACK_ZERO;
		}

		/* Mark slots affected by this stack write. */
		for (i = 0; i < size; i++)
			state->stack[spi].slot_type[(slot - i) % BPF_REG_SIZE] = type;
		insn_flags = 0; /* not a register spill */
	}

	if (insn_flags)
		return push_insn_history(env, env->cur_state, insn_flags, 0);
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
	struct bpf_insn *insn = &env->prog->insnsi[insn_idx];
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
	if ((value_reg && register_is_null(value_reg)) ||
	    (!value_reg && is_bpf_st_mem(insn) && insn->imm == 0))
		writing_zero = true;

	for (i = min_off; i < max_off; i++) {
		int spi;

		spi = __get_spi(i);
		err = destroy_if_dynptr_stack_slot(env, state, spi);
		if (err)
			return err;
	}

	check_fastcall_stack_contract(env, state, insn_idx, min_off);
	/* Variable offset writes destroy any spilled pointers in range. */
	for (i = min_off; i < max_off; i++) {
		u8 new_type, *stype;
		int slot, spi;

		slot = -i - 1;
		spi = slot / BPF_REG_SIZE;
		stype = &state->stack[spi].slot_type[slot % BPF_REG_SIZE];
		mark_stack_slot_scratched(env, spi);

		if (!env->allow_ptr_leaks && *stype != STACK_MISC && *stype != STACK_ZERO) {
			/* Reject the write if range we may write to has not
			 * been initialized beforehand. If we didn't reject
			 * here, the ptr status would be erased below (even
			 * though not all slots are actually overwritten),
			 * possibly opening the door to leaks.
			 *
			 * We do however catch STACK_INVALID case below, and
			 * only allow reading possibly uninitialized memory
			 * later for CAP_PERFMON, as the write may not happen to
			 * that slot.
			 */
			verbose(env, "spilled ptr in range of var-offset stack write; insn %d, ptr off: %d",
				insn_idx, i);
			return -EINVAL;
		}

		/* If writing_zero and the spi slot contains a spill of value 0,
		 * maintain the spill type.
		 */
		if (writing_zero && *stype == STACK_SPILL &&
		    is_spilled_scalar_reg(&state->stack[spi])) {
			struct bpf_reg_state *spill_reg = &state->stack[spi].spilled_ptr;

			if (tnum_is_const(spill_reg->var_off) && spill_reg->var_off.value == 0) {
				zero_used = true;
				continue;
			}
		}

		/* Erase all other spilled pointers. */
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
		mark_stack_slot_scratched(env, spi);
		stype = ptr_state->stack[spi].slot_type;
		if (stype[slot % BPF_REG_SIZE] != STACK_ZERO)
			break;
		zeros++;
	}
	if (zeros == max_off - min_off) {
		/* Any access_size read into register is zero extended,
		 * so the whole register == const_zero.
		 */
		__mark_reg_const_zero(env, &state->regs[dst_regno]);
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
	u8 *stype, type;
	int insn_flags = insn_stack_access_flags(reg_state->frameno, spi);

	stype = reg_state->stack[spi].slot_type;
	reg = &reg_state->stack[spi].spilled_ptr;

	mark_stack_slot_scratched(env, spi);
	check_fastcall_stack_contract(env, state, env->insn_idx, off);

	if (is_spilled_reg(&reg_state->stack[spi])) {
		u8 spill_size = 1;

		for (i = BPF_REG_SIZE - 1; i > 0 && stype[i - 1] == STACK_SPILL; i--)
			spill_size++;

		if (size != BPF_REG_SIZE || spill_size != BPF_REG_SIZE) {
			if (reg->type != SCALAR_VALUE) {
				verbose_linfo(env, env->insn_idx, "; ");
				verbose(env, "invalid size of register fill\n");
				return -EACCES;
			}

			mark_reg_read(env, reg, reg->parent, REG_LIVE_READ64);
			if (dst_regno < 0)
				return 0;

			if (size <= spill_size &&
			    bpf_stack_narrow_access_ok(off, size, spill_size)) {
				/* The earlier check_reg_arg() has decided the
				 * subreg_def for this insn.  Save it first.
				 */
				s32 subreg_def = state->regs[dst_regno].subreg_def;

				copy_register_state(&state->regs[dst_regno], reg);
				state->regs[dst_regno].subreg_def = subreg_def;

				/* Break the relation on a narrowing fill.
				 * coerce_reg_to_size will adjust the boundaries.
				 */
				if (get_reg_width(reg) > size * BITS_PER_BYTE)
					state->regs[dst_regno].id = 0;
			} else {
				int spill_cnt = 0, zero_cnt = 0;

				for (i = 0; i < size; i++) {
					type = stype[(slot - i) % BPF_REG_SIZE];
					if (type == STACK_SPILL) {
						spill_cnt++;
						continue;
					}
					if (type == STACK_MISC)
						continue;
					if (type == STACK_ZERO) {
						zero_cnt++;
						continue;
					}
					if (type == STACK_INVALID && env->allow_uninit_stack)
						continue;
					verbose(env, "invalid read from stack off %d+%d size %d\n",
						off, i, size);
					return -EACCES;
				}

				if (spill_cnt == size &&
				    tnum_is_const(reg->var_off) && reg->var_off.value == 0) {
					__mark_reg_const_zero(env, &state->regs[dst_regno]);
					/* this IS register fill, so keep insn_flags */
				} else if (zero_cnt == size) {
					/* similarly to mark_reg_stack_read(), preserve zeroes */
					__mark_reg_const_zero(env, &state->regs[dst_regno]);
					insn_flags = 0; /* not restoring original register state */
				} else {
					mark_reg_unknown(env, state->regs, dst_regno);
					insn_flags = 0; /* not restoring original register state */
				}
			}
			state->regs[dst_regno].live |= REG_LIVE_WRITTEN;
		} else if (dst_regno >= 0) {
			/* restore register state from stack */
			copy_register_state(&state->regs[dst_regno], reg);
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
		for (i = 0; i < size; i++) {
			type = stype[(slot - i) % BPF_REG_SIZE];
			if (type == STACK_MISC)
				continue;
			if (type == STACK_ZERO)
				continue;
			if (type == STACK_INVALID && env->allow_uninit_stack)
				continue;
			verbose(env, "invalid read from stack off %d+%d size %d\n",
				off, i, size);
			return -EACCES;
		}
		mark_reg_read(env, reg, reg->parent, REG_LIVE_READ64);
		if (dst_regno >= 0)
			mark_reg_stack_read(env, reg_state, off, off + size, dst_regno);
		insn_flags = 0; /* we are not restoring spilled register */
	}
	if (insn_flags)
		return push_insn_history(env, env->cur_state, insn_flags, 0);
	return 0;
}

enum bpf_access_src {
	ACCESS_DIRECT = 1,  /* the access is performed by an instruction */
	ACCESS_HELPER = 2,  /* the access is performed by a helper */
};

static int check_stack_range_initialized(struct bpf_verifier_env *env,
					 int regno, int off, int access_size,
					 bool zero_size_allowed,
					 enum bpf_access_src type,
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
	check_fastcall_stack_contract(env, ptr_state, env->insn_idx, min_off);
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
	 * ALU. See also retrieve_ptr_limit(). The check in
	 * check_stack_access_for_ptr_arithmetic() called by
	 * adjust_ptr_min_max_vals() prevents users from creating stack pointers
	 * with variable offsets, therefore no check is required here. Further,
	 * just checking it here would be insufficient as speculative stack
	 * writes could still lead to unsafe speculative behaviour.
	 */
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
	case PTR_TO_MAP_KEY:
		verbose(env, "invalid access to map key, key_size=%d off=%d size=%d\n",
			mem_size, off, size);
		break;
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
	 *
	 * The minimum value is only important with signed
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

static int __check_ptr_off_reg(struct bpf_verifier_env *env,
			       const struct bpf_reg_state *reg, int regno,
			       bool fixed_off_ok)
{
	/* Access to this pointer-typed register or passing it to a helper
	 * is only allowed in its original, unmodified form.
	 */

	if (reg->off < 0) {
		verbose(env, "negative offset %s ptr R%d off=%d disallowed\n",
			reg_type_str(env, reg->type), regno, reg->off);
		return -EACCES;
	}

	if (!fixed_off_ok && reg->off) {
		verbose(env, "dereference of modified %s ptr R%d off=%d disallowed\n",
			reg_type_str(env, reg->type), regno, reg->off);
		return -EACCES;
	}

	if (!tnum_is_const(reg->var_off) || reg->var_off.value) {
		char tn_buf[48];

		tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
		verbose(env, "variable %s access var_off=%s disallowed\n",
			reg_type_str(env, reg->type), tn_buf);
		return -EACCES;
	}

	return 0;
}

static int check_ptr_off_reg(struct bpf_verifier_env *env,
		             const struct bpf_reg_state *reg, int regno)
{
	return __check_ptr_off_reg(env, reg, regno, false);
}

static int map_kptr_match_type(struct bpf_verifier_env *env,
			       struct btf_field *kptr_field,
			       struct bpf_reg_state *reg, u32 regno)
{
	const char *targ_name = btf_type_name(kptr_field->kptr.btf, kptr_field->kptr.btf_id);
	int perm_flags;
	const char *reg_name = "";

	if (btf_is_kernel(reg->btf)) {
		perm_flags = PTR_MAYBE_NULL | PTR_TRUSTED | MEM_RCU;

		/* Only unreferenced case accepts untrusted pointers */
		if (kptr_field->type == BPF_KPTR_UNREF)
			perm_flags |= PTR_UNTRUSTED;
	} else {
		perm_flags = PTR_MAYBE_NULL | MEM_ALLOC;
		if (kptr_field->type == BPF_KPTR_PERCPU)
			perm_flags |= MEM_PERCPU;
	}

	if (base_type(reg->type) != PTR_TO_BTF_ID || (type_flag(reg->type) & ~perm_flags))
		goto bad_type;

	/* We need to verify reg->type and reg->btf, before accessing reg->btf */
	reg_name = btf_type_name(reg->btf, reg->btf_id);

	/* For ref_ptr case, release function check should ensure we get one
	 * referenced PTR_TO_BTF_ID, and that its fixed offset is 0. For the
	 * normal store of unreferenced kptr, we must ensure var_off is zero.
	 * Since ref_ptr cannot be accessed directly by BPF insns, checks for
	 * reg->off and reg->ref_obj_id are not needed here.
	 */
	if (__check_ptr_off_reg(env, reg, regno, true))
		return -EACCES;

	/* A full type match is needed, as BTF can be vmlinux, module or prog BTF, and
	 * we also need to take into account the reg->off.
	 *
	 * We want to support cases like:
	 *
	 * struct foo {
	 *         struct bar br;
	 *         struct baz bz;
	 * };
	 *
	 * struct foo *v;
	 * v = func();	      // PTR_TO_BTF_ID
	 * val->foo = v;      // reg->off is zero, btf and btf_id match type
	 * val->bar = &v->br; // reg->off is still zero, but we need to retry with
	 *                    // first member type of struct after comparison fails
	 * val->baz = &v->bz; // reg->off is non-zero, so struct needs to be walked
	 *                    // to match type
	 *
	 * In the kptr_ref case, check_func_arg_reg_off already ensures reg->off
	 * is zero. We must also ensure that btf_struct_ids_match does not walk
	 * the struct to match type against first member of struct, i.e. reject
	 * second case from above. Hence, when type is BPF_KPTR_REF, we set
	 * strict mode to true for type match.
	 */
	if (!btf_struct_ids_match(&env->log, reg->btf, reg->btf_id, reg->off,
				  kptr_field->kptr.btf, kptr_field->kptr.btf_id,
				  kptr_field->type != BPF_KPTR_UNREF))
		goto bad_type;
	return 0;
bad_type:
	verbose(env, "invalid kptr access, R%d type=%s%s ", regno,
		reg_type_str(env, reg->type), reg_name);
	verbose(env, "expected=%s%s", reg_type_str(env, PTR_TO_BTF_ID), targ_name);
	if (kptr_field->type == BPF_KPTR_UNREF)
		verbose(env, " or %s%s\n", reg_type_str(env, PTR_TO_BTF_ID | PTR_UNTRUSTED),
			targ_name);
	else
		verbose(env, "\n");
	return -EINVAL;
}

static bool in_sleepable(struct bpf_verifier_env *env)
{
	return env->prog->sleepable ||
	       (env->cur_state && env->cur_state->in_sleepable);
}

/* The non-sleepable programs and sleepable programs with explicit bpf_rcu_read_lock()
 * can dereference RCU protected pointers and result is PTR_TRUSTED.
 */
static bool in_rcu_cs(struct bpf_verifier_env *env)
{
	return env->cur_state->active_rcu_lock ||
	       cur_func(env)->active_locks ||
	       !in_sleepable(env);
}

/* Once GCC supports btf_type_tag the following mechanism will be replaced with tag check */
BTF_SET_START(rcu_protected_types)
BTF_ID(struct, prog_test_ref_kfunc)
#ifdef CONFIG_CGROUPS
BTF_ID(struct, cgroup)
#endif
#ifdef CONFIG_BPF_JIT
BTF_ID(struct, bpf_cpumask)
#endif
BTF_ID(struct, task_struct)
BTF_ID(struct, bpf_crypto_ctx)
BTF_SET_END(rcu_protected_types)

static bool rcu_protected_object(const struct btf *btf, u32 btf_id)
{
	if (!btf_is_kernel(btf))
		return true;
	return btf_id_set_contains(&rcu_protected_types, btf_id);
}

static struct btf_record *kptr_pointee_btf_record(struct btf_field *kptr_field)
{
	struct btf_struct_meta *meta;

	if (btf_is_kernel(kptr_field->kptr.btf))
		return NULL;

	meta = btf_find_struct_meta(kptr_field->kptr.btf,
				    kptr_field->kptr.btf_id);

	return meta ? meta->record : NULL;
}

static bool rcu_safe_kptr(const struct btf_field *field)
{
	const struct btf_field_kptr *kptr = &field->kptr;

	return field->type == BPF_KPTR_PERCPU ||
	       (field->type == BPF_KPTR_REF && rcu_protected_object(kptr->btf, kptr->btf_id));
}

static u32 btf_ld_kptr_type(struct bpf_verifier_env *env, struct btf_field *kptr_field)
{
	struct btf_record *rec;
	u32 ret;

	ret = PTR_MAYBE_NULL;
	if (rcu_safe_kptr(kptr_field) && in_rcu_cs(env)) {
		ret |= MEM_RCU;
		if (kptr_field->type == BPF_KPTR_PERCPU)
			ret |= MEM_PERCPU;
		else if (!btf_is_kernel(kptr_field->kptr.btf))
			ret |= MEM_ALLOC;

		rec = kptr_pointee_btf_record(kptr_field);
		if (rec && btf_record_has_field(rec, BPF_GRAPH_NODE))
			ret |= NON_OWN_REF;
	} else {
		ret |= PTR_UNTRUSTED;
	}

	return ret;
}

static int mark_uptr_ld_reg(struct bpf_verifier_env *env, u32 regno,
			    struct btf_field *field)
{
	struct bpf_reg_state *reg;
	const struct btf_type *t;

	t = btf_type_by_id(field->kptr.btf, field->kptr.btf_id);
	mark_reg_known_zero(env, cur_regs(env), regno);
	reg = reg_state(env, regno);
	reg->type = PTR_TO_MEM | PTR_MAYBE_NULL;
	reg->mem_size = t->size;
	reg->id = ++env->id_gen;

	return 0;
}

static int check_map_kptr_access(struct bpf_verifier_env *env, u32 regno,
				 int value_regno, int insn_idx,
				 struct btf_field *kptr_field)
{
	struct bpf_insn *insn = &env->prog->insnsi[insn_idx];
	int class = BPF_CLASS(insn->code);
	struct bpf_reg_state *val_reg;

	/* Things we already checked for in check_map_access and caller:
	 *  - Reject cases where variable offset may touch kptr
	 *  - size of access (must be BPF_DW)
	 *  - tnum_is_const(reg->var_off)
	 *  - kptr_field->offset == off + reg->var_off.value
	 */
	/* Only BPF_[LDX,STX,ST] | BPF_MEM | BPF_DW is supported */
	if (BPF_MODE(insn->code) != BPF_MEM) {
		verbose(env, "kptr in map can only be accessed using BPF_MEM instruction mode\n");
		return -EACCES;
	}

	/* We only allow loading referenced kptr, since it will be marked as
	 * untrusted, similar to unreferenced kptr.
	 */
	if (class != BPF_LDX &&
	    (kptr_field->type == BPF_KPTR_REF || kptr_field->type == BPF_KPTR_PERCPU)) {
		verbose(env, "store to referenced kptr disallowed\n");
		return -EACCES;
	}
	if (class != BPF_LDX && kptr_field->type == BPF_UPTR) {
		verbose(env, "store to uptr disallowed\n");
		return -EACCES;
	}

	if (class == BPF_LDX) {
		if (kptr_field->type == BPF_UPTR)
			return mark_uptr_ld_reg(env, value_regno, kptr_field);

		/* We can simply mark the value_regno receiving the pointer
		 * value from map as PTR_TO_BTF_ID, with the correct type.
		 */
		mark_btf_ld_reg(env, cur_regs(env), value_regno, PTR_TO_BTF_ID, kptr_field->kptr.btf,
				kptr_field->kptr.btf_id, btf_ld_kptr_type(env, kptr_field));
	} else if (class == BPF_STX) {
		val_reg = reg_state(env, value_regno);
		if (!register_is_null(val_reg) &&
		    map_kptr_match_type(env, kptr_field, val_reg, value_regno))
			return -EACCES;
	} else if (class == BPF_ST) {
		if (insn->imm) {
			verbose(env, "BPF_ST imm must be 0 when storing to kptr at off=%u\n",
				kptr_field->offset);
			return -EACCES;
		}
	} else {
		verbose(env, "kptr in map can only be accessed using BPF_LDX/BPF_STX/BPF_ST\n");
		return -EACCES;
	}
	return 0;
}

/* check read/write into a map element with possible variable offset */
static int check_map_access(struct bpf_verifier_env *env, u32 regno,
			    int off, int size, bool zero_size_allowed,
			    enum bpf_access_src src)
{
	struct bpf_verifier_state *vstate = env->cur_state;
	struct bpf_func_state *state = vstate->frame[vstate->curframe];
	struct bpf_reg_state *reg = &state->regs[regno];
	struct bpf_map *map = reg->map_ptr;
	struct btf_record *rec;
	int err, i;

	err = check_mem_region_access(env, regno, off, size, map->value_size,
				      zero_size_allowed);
	if (err)
		return err;

	if (IS_ERR_OR_NULL(map->record))
		return 0;
	rec = map->record;
	for (i = 0; i < rec->cnt; i++) {
		struct btf_field *field = &rec->fields[i];
		u32 p = field->offset;

		/* If any part of a field  can be touched by load/store, reject
		 * this program. To check that [x1, x2) overlaps with [y1, y2),
		 * it is sufficient to check x1 < y2 && y1 < x2.
		 */
		if (reg->smin_value + off < p + field->size &&
		    p < reg->umax_value + off + size) {
			switch (field->type) {
			case BPF_KPTR_UNREF:
			case BPF_KPTR_REF:
			case BPF_KPTR_PERCPU:
			case BPF_UPTR:
				if (src != ACCESS_DIRECT) {
					verbose(env, "%s cannot be accessed indirectly by helper\n",
						btf_field_type_name(field->type));
					return -EACCES;
				}
				if (!tnum_is_const(reg->var_off)) {
					verbose(env, "%s access cannot have variable offset\n",
						btf_field_type_name(field->type));
					return -EACCES;
				}
				if (p != off + reg->var_off.value) {
					verbose(env, "%s access misaligned expected=%u off=%llu\n",
						btf_field_type_name(field->type),
						p, off + reg->var_off.value);
					return -EACCES;
				}
				if (size != bpf_size_to_bytes(BPF_DW)) {
					verbose(env, "%s access size must be BPF_DW\n",
						btf_field_type_name(field->type));
					return -EACCES;
				}
				break;
			default:
				verbose(env, "%s cannot be accessed directly by load/store\n",
					btf_field_type_name(field->type));
				return -EACCES;
			}
		}
	}
	return 0;
}

#define MAX_PACKET_OFF 0xffff

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
			    struct btf **btf, u32 *btf_id, bool *is_retval, bool is_ldsx)
{
	struct bpf_insn_access_aux info = {
		.reg_type = *reg_type,
		.log = &env->log,
		.is_retval = false,
		.is_ldsx = is_ldsx,
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
		*is_retval = info.is_retval;

		if (base_type(*reg_type) == PTR_TO_BTF_ID) {
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
		regno, reg_type_str(env, reg->type), off, size);

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

static bool is_arena_reg(struct bpf_verifier_env *env, int regno)
{
	const struct bpf_reg_state *reg = reg_state(env, regno);

	return reg->type == PTR_TO_ARENA;
}

static u32 *reg2btf_ids[__BPF_REG_TYPE_MAX] = {
#ifdef CONFIG_NET
	[PTR_TO_SOCKET] = &btf_sock_ids[BTF_SOCK_TYPE_SOCK],
	[PTR_TO_SOCK_COMMON] = &btf_sock_ids[BTF_SOCK_TYPE_SOCK_COMMON],
	[PTR_TO_TCP_SOCK] = &btf_sock_ids[BTF_SOCK_TYPE_TCP],
#endif
	[CONST_PTR_TO_MAP] = btf_bpf_map_id,
};

static bool is_trusted_reg(const struct bpf_reg_state *reg)
{
	/* A referenced register is always trusted. */
	if (reg->ref_obj_id)
		return true;

	/* Types listed in the reg2btf_ids are always trusted */
	if (reg2btf_ids[base_type(reg->type)] &&
	    !bpf_type_has_unsafe_modifiers(reg->type))
		return true;

	/* If a register is not referenced, it is trusted if it has the
	 * MEM_ALLOC or PTR_TRUSTED type modifiers, and no others. Some of the
	 * other type modifiers may be safe, but we elect to take an opt-in
	 * approach here as some (e.g. PTR_UNTRUSTED and PTR_MAYBE_NULL) are
	 * not.
	 *
	 * Eventually, we should make PTR_TRUSTED the single source of truth
	 * for whether a register is trusted.
	 */
	return type_flag(reg->type) & BPF_REG_TRUSTED_MODIFIERS &&
	       !bpf_type_has_unsafe_modifiers(reg->type);
}

static bool is_rcu_reg(const struct bpf_reg_state *reg)
{
	return reg->type & MEM_RCU;
}

static void clear_trusted_flags(enum bpf_type_flag *flag)
{
	*flag &= ~(BPF_REG_TRUSTED_MODIFIERS | MEM_RCU);
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
	case PTR_TO_MAP_KEY:
		pointer_desc = "key ";
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
	case PTR_TO_ARENA:
		return 0;
	default:
		break;
	}
	return check_generic_ptr_alignment(env, reg, pointer_desc, off, size,
					   strict);
}

static enum priv_stack_mode bpf_enable_priv_stack(struct bpf_prog *prog)
{
	if (!bpf_jit_supports_private_stack())
		return NO_PRIV_STACK;

	/* bpf_prog_check_recur() checks all prog types that use bpf trampoline
	 * while kprobe/tp/perf_event/raw_tp don't use trampoline hence checked
	 * explicitly.
	 */
	switch (prog->type) {
	case BPF_PROG_TYPE_KPROBE:
	case BPF_PROG_TYPE_TRACEPOINT:
	case BPF_PROG_TYPE_PERF_EVENT:
	case BPF_PROG_TYPE_RAW_TRACEPOINT:
		return PRIV_STACK_ADAPTIVE;
	case BPF_PROG_TYPE_TRACING:
	case BPF_PROG_TYPE_LSM:
	case BPF_PROG_TYPE_STRUCT_OPS:
		if (prog->aux->priv_stack_requested || bpf_prog_check_recur(prog))
			return PRIV_STACK_ADAPTIVE;
		fallthrough;
	default:
		break;
	}

	return NO_PRIV_STACK;
}

static int round_up_stack_depth(struct bpf_verifier_env *env, int stack_depth)
{
	if (env->prog->jit_requested)
		return round_up(stack_depth, 16);

	/* round up to 32-bytes, since this is granularity
	 * of interpreter stack size
	 */
	return round_up(max_t(u32, stack_depth, 1), 32);
}

/* starting from main bpf function walk all instructions of the function
 * and recursively walk all callees that given function can call.
 * Ignore jump and exit insns.
 * Since recursion is prevented by check_cfg() this algorithm
 * only needs a local stack of MAX_CALL_FRAMES to remember callsites
 */
static int check_max_stack_depth_subprog(struct bpf_verifier_env *env, int idx,
					 bool priv_stack_supported)
{
	struct bpf_subprog_info *subprog = env->subprog_info;
	struct bpf_insn *insn = env->prog->insnsi;
	int depth = 0, frame = 0, i, subprog_end, subprog_depth;
	bool tail_call_reachable = false;
	int ret_insn[MAX_CALL_FRAMES];
	int ret_prog[MAX_CALL_FRAMES];
	int j;

	i = subprog[idx].start;
	if (!priv_stack_supported)
		subprog[idx].priv_stack_mode = NO_PRIV_STACK;
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

	subprog_depth = round_up_stack_depth(env, subprog[idx].stack_depth);
	if (priv_stack_supported) {
		/* Request private stack support only if the subprog stack
		 * depth is no less than BPF_PRIV_STACK_MIN_SIZE. This is to
		 * avoid jit penalty if the stack usage is small.
		 */
		if (subprog[idx].priv_stack_mode == PRIV_STACK_UNKNOWN &&
		    subprog_depth >= BPF_PRIV_STACK_MIN_SIZE)
			subprog[idx].priv_stack_mode = PRIV_STACK_ADAPTIVE;
	}

	if (subprog[idx].priv_stack_mode == PRIV_STACK_ADAPTIVE) {
		if (subprog_depth > MAX_BPF_STACK) {
			verbose(env, "stack size of subprog %d is %d. Too large\n",
				idx, subprog_depth);
			return -EACCES;
		}
	} else {
		depth += subprog_depth;
		if (depth > MAX_BPF_STACK) {
			verbose(env, "combined stack size of %d calls is %d. Too large\n",
				frame + 1, depth);
			return -EACCES;
		}
	}
continue_func:
	subprog_end = subprog[idx + 1].start;
	for (; i < subprog_end; i++) {
		int next_insn, sidx;

		if (bpf_pseudo_kfunc_call(insn + i) && !insn[i].off) {
			bool err = false;

			if (!is_bpf_throw_kfunc(insn + i))
				continue;
			if (subprog[idx].is_cb)
				err = true;
			for (int c = 0; c < frame && !err; c++) {
				if (subprog[ret_prog[c]].is_cb) {
					err = true;
					break;
				}
			}
			if (!err)
				continue;
			verbose(env,
				"bpf_throw kfunc (insn %d) cannot be called from callback subprog %d\n",
				i, idx);
			return -EINVAL;
		}

		if (!bpf_pseudo_call(insn + i) && !bpf_pseudo_func(insn + i))
			continue;
		/* remember insn and function to return to */
		ret_insn[frame] = i + 1;
		ret_prog[frame] = idx;

		/* find the callee */
		next_insn = i + insn[i].imm + 1;
		sidx = find_subprog(env, next_insn);
		if (sidx < 0) {
			WARN_ONCE(1, "verifier bug. No program starts at insn %d\n",
				  next_insn);
			return -EFAULT;
		}
		if (subprog[sidx].is_async_cb) {
			if (subprog[sidx].has_tail_call) {
				verbose(env, "verifier bug. subprog has tail_call and async cb\n");
				return -EFAULT;
			}
			/* async callbacks don't increase bpf prog stack size unless called directly */
			if (!bpf_pseudo_call(insn + i))
				continue;
			if (subprog[sidx].is_exception_cb) {
				verbose(env, "insn %d cannot call exception cb directly\n", i);
				return -EINVAL;
			}
		}
		i = next_insn;
		idx = sidx;
		if (!priv_stack_supported)
			subprog[idx].priv_stack_mode = NO_PRIV_STACK;

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
		for (j = 0; j < frame; j++) {
			if (subprog[ret_prog[j]].is_exception_cb) {
				verbose(env, "cannot tail call within exception cb\n");
				return -EINVAL;
			}
			subprog[ret_prog[j]].tail_call_reachable = true;
		}
	if (subprog[0].tail_call_reachable)
		env->prog->aux->tail_call_reachable = true;

	/* end of for() loop means the last insn of the 'subprog'
	 * was reached. Doesn't matter whether it was JA or EXIT
	 */
	if (frame == 0)
		return 0;
	if (subprog[idx].priv_stack_mode != PRIV_STACK_ADAPTIVE)
		depth -= round_up_stack_depth(env, subprog[idx].stack_depth);
	frame--;
	i = ret_insn[frame];
	idx = ret_prog[frame];
	goto continue_func;
}

static int check_max_stack_depth(struct bpf_verifier_env *env)
{
	enum priv_stack_mode priv_stack_mode = PRIV_STACK_UNKNOWN;
	struct bpf_subprog_info *si = env->subprog_info;
	bool priv_stack_supported;
	int ret;

	for (int i = 0; i < env->subprog_cnt; i++) {
		if (si[i].has_tail_call) {
			priv_stack_mode = NO_PRIV_STACK;
			break;
		}
	}

	if (priv_stack_mode == PRIV_STACK_UNKNOWN)
		priv_stack_mode = bpf_enable_priv_stack(env->prog);

	/* All async_cb subprogs use normal kernel stack. If a particular
	 * subprog appears in both main prog and async_cb subtree, that
	 * subprog will use normal kernel stack to avoid potential nesting.
	 * The reverse subprog traversal ensures when main prog subtree is
	 * checked, the subprogs appearing in async_cb subtrees are already
	 * marked as using normal kernel stack, so stack size checking can
	 * be done properly.
	 */
	for (int i = env->subprog_cnt - 1; i >= 0; i--) {
		if (!i || si[i].is_async_cb) {
			priv_stack_supported = !i && priv_stack_mode == PRIV_STACK_ADAPTIVE;
			ret = check_max_stack_depth_subprog(env, i, priv_stack_supported);
			if (ret < 0)
				return ret;
		}
	}

	for (int i = 0; i < env->subprog_cnt; i++) {
		if (si[i].priv_stack_mode == PRIV_STACK_ADAPTIVE) {
			env->prog->aux->jits_use_priv_stack = true;
			break;
		}
	}

	return 0;
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
			       u32 *max_access)
{
	const char *buf_info = type_is_rdonly_mem(reg->type) ? "rdonly" : "rdwr";
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
	if (size < 4)
		__mark_reg32_unbounded(reg);

	reg_bounds_sync(reg);
}

static void set_sext64_default_val(struct bpf_reg_state *reg, int size)
{
	if (size == 1) {
		reg->smin_value = reg->s32_min_value = S8_MIN;
		reg->smax_value = reg->s32_max_value = S8_MAX;
	} else if (size == 2) {
		reg->smin_value = reg->s32_min_value = S16_MIN;
		reg->smax_value = reg->s32_max_value = S16_MAX;
	} else {
		/* size == 4 */
		reg->smin_value = reg->s32_min_value = S32_MIN;
		reg->smax_value = reg->s32_max_value = S32_MAX;
	}
	reg->umin_value = reg->u32_min_value = 0;
	reg->umax_value = U64_MAX;
	reg->u32_max_value = U32_MAX;
	reg->var_off = tnum_unknown;
}

static void coerce_reg_to_size_sx(struct bpf_reg_state *reg, int size)
{
	s64 init_s64_max, init_s64_min, s64_max, s64_min, u64_cval;
	u64 top_smax_value, top_smin_value;
	u64 num_bits = size * 8;

	if (tnum_is_const(reg->var_off)) {
		u64_cval = reg->var_off.value;
		if (size == 1)
			reg->var_off = tnum_const((s8)u64_cval);
		else if (size == 2)
			reg->var_off = tnum_const((s16)u64_cval);
		else
			/* size == 4 */
			reg->var_off = tnum_const((s32)u64_cval);

		u64_cval = reg->var_off.value;
		reg->smax_value = reg->smin_value = u64_cval;
		reg->umax_value = reg->umin_value = u64_cval;
		reg->s32_max_value = reg->s32_min_value = u64_cval;
		reg->u32_max_value = reg->u32_min_value = u64_cval;
		return;
	}

	top_smax_value = ((u64)reg->smax_value >> num_bits) << num_bits;
	top_smin_value = ((u64)reg->smin_value >> num_bits) << num_bits;

	if (top_smax_value != top_smin_value)
		goto out;

	/* find the s64_min and s64_min after sign extension */
	if (size == 1) {
		init_s64_max = (s8)reg->smax_value;
		init_s64_min = (s8)reg->smin_value;
	} else if (size == 2) {
		init_s64_max = (s16)reg->smax_value;
		init_s64_min = (s16)reg->smin_value;
	} else {
		init_s64_max = (s32)reg->smax_value;
		init_s64_min = (s32)reg->smin_value;
	}

	s64_max = max(init_s64_max, init_s64_min);
	s64_min = min(init_s64_max, init_s64_min);

	/* both of s64_max/s64_min positive or negative */
	if ((s64_max >= 0) == (s64_min >= 0)) {
		reg->s32_min_value = reg->smin_value = s64_min;
		reg->s32_max_value = reg->smax_value = s64_max;
		reg->u32_min_value = reg->umin_value = s64_min;
		reg->u32_max_value = reg->umax_value = s64_max;
		reg->var_off = tnum_range(s64_min, s64_max);
		return;
	}

out:
	set_sext64_default_val(reg, size);
}

static void set_sext32_default_val(struct bpf_reg_state *reg, int size)
{
	if (size == 1) {
		reg->s32_min_value = S8_MIN;
		reg->s32_max_value = S8_MAX;
	} else {
		/* size == 2 */
		reg->s32_min_value = S16_MIN;
		reg->s32_max_value = S16_MAX;
	}
	reg->u32_min_value = 0;
	reg->u32_max_value = U32_MAX;
	reg->var_off = tnum_subreg(tnum_unknown);
}

static void coerce_subreg_to_size_sx(struct bpf_reg_state *reg, int size)
{
	s32 init_s32_max, init_s32_min, s32_max, s32_min, u32_val;
	u32 top_smax_value, top_smin_value;
	u32 num_bits = size * 8;

	if (tnum_is_const(reg->var_off)) {
		u32_val = reg->var_off.value;
		if (size == 1)
			reg->var_off = tnum_const((s8)u32_val);
		else
			reg->var_off = tnum_const((s16)u32_val);

		u32_val = reg->var_off.value;
		reg->s32_min_value = reg->s32_max_value = u32_val;
		reg->u32_min_value = reg->u32_max_value = u32_val;
		return;
	}

	top_smax_value = ((u32)reg->s32_max_value >> num_bits) << num_bits;
	top_smin_value = ((u32)reg->s32_min_value >> num_bits) << num_bits;

	if (top_smax_value != top_smin_value)
		goto out;

	/* find the s32_min and s32_min after sign extension */
	if (size == 1) {
		init_s32_max = (s8)reg->s32_max_value;
		init_s32_min = (s8)reg->s32_min_value;
	} else {
		/* size == 2 */
		init_s32_max = (s16)reg->s32_max_value;
		init_s32_min = (s16)reg->s32_min_value;
	}
	s32_max = max(init_s32_max, init_s32_min);
	s32_min = min(init_s32_max, init_s32_min);

	if ((s32_min >= 0) == (s32_max >= 0)) {
		reg->s32_min_value = s32_min;
		reg->s32_max_value = s32_max;
		reg->u32_min_value = (u32)s32_min;
		reg->u32_max_value = (u32)s32_max;
		reg->var_off = tnum_subreg(tnum_range(s32_min, s32_max));
		return;
	}

out:
	set_sext32_default_val(reg, size);
}

static bool bpf_map_is_rdonly(const struct bpf_map *map)
{
	/* A map is considered read-only if the following condition are true:
	 *
	 * 1) BPF program side cannot change any of the map content. The
	 *    BPF_F_RDONLY_PROG flag is throughout the lifetime of a map
	 *    and was set at map creation time.
	 * 2) The map value(s) have been initialized from user space by a
	 *    loader and then "frozen", such that no new map update/delete
	 *    operations from syscall side are possible for the rest of
	 *    the map's lifetime from that point onwards.
	 * 3) Any parallel/pending map update/delete operations from syscall
	 *    side have been completed. Only after that point, it's safe to
	 *    assume that map value(s) are immutable.
	 */
	return (map->map_flags & BPF_F_RDONLY_PROG) &&
	       READ_ONCE(map->frozen) &&
	       !bpf_map_write_active(map);
}

static int bpf_map_direct_read(struct bpf_map *map, int off, int size, u64 *val,
			       bool is_ldsx)
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
		*val = is_ldsx ? (s64)*(s8 *)ptr : (u64)*(u8 *)ptr;
		break;
	case sizeof(u16):
		*val = is_ldsx ? (s64)*(s16 *)ptr : (u64)*(u16 *)ptr;
		break;
	case sizeof(u32):
		*val = is_ldsx ? (s64)*(s32 *)ptr : (u64)*(u32 *)ptr;
		break;
	case sizeof(u64):
		*val = *(u64 *)ptr;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#define BTF_TYPE_SAFE_RCU(__type)  __PASTE(__type, __safe_rcu)
#define BTF_TYPE_SAFE_RCU_OR_NULL(__type)  __PASTE(__type, __safe_rcu_or_null)
#define BTF_TYPE_SAFE_TRUSTED(__type)  __PASTE(__type, __safe_trusted)
#define BTF_TYPE_SAFE_TRUSTED_OR_NULL(__type)  __PASTE(__type, __safe_trusted_or_null)

/*
 * Allow list few fields as RCU trusted or full trusted.
 * This logic doesn't allow mix tagging and will be removed once GCC supports
 * btf_type_tag.
 */

/* RCU trusted: these fields are trusted in RCU CS and never NULL */
BTF_TYPE_SAFE_RCU(struct task_struct) {
	const cpumask_t *cpus_ptr;
	struct css_set __rcu *cgroups;
	struct task_struct __rcu *real_parent;
	struct task_struct *group_leader;
};

BTF_TYPE_SAFE_RCU(struct cgroup) {
	/* cgrp->kn is always accessible as documented in kernel/cgroup/cgroup.c */
	struct kernfs_node *kn;
};

BTF_TYPE_SAFE_RCU(struct css_set) {
	struct cgroup *dfl_cgrp;
};

/* RCU trusted: these fields are trusted in RCU CS and can be NULL */
BTF_TYPE_SAFE_RCU_OR_NULL(struct mm_struct) {
	struct file __rcu *exe_file;
};

/* skb->sk, req->sk are not RCU protected, but we mark them as such
 * because bpf prog accessible sockets are SOCK_RCU_FREE.
 */
BTF_TYPE_SAFE_RCU_OR_NULL(struct sk_buff) {
	struct sock *sk;
};

BTF_TYPE_SAFE_RCU_OR_NULL(struct request_sock) {
	struct sock *sk;
};

/* full trusted: these fields are trusted even outside of RCU CS and never NULL */
BTF_TYPE_SAFE_TRUSTED(struct bpf_iter_meta) {
	struct seq_file *seq;
};

BTF_TYPE_SAFE_TRUSTED(struct bpf_iter__task) {
	struct bpf_iter_meta *meta;
	struct task_struct *task;
};

BTF_TYPE_SAFE_TRUSTED(struct linux_binprm) {
	struct file *file;
};

BTF_TYPE_SAFE_TRUSTED(struct file) {
	struct inode *f_inode;
};

BTF_TYPE_SAFE_TRUSTED(struct dentry) {
	/* no negative dentry-s in places where bpf can see it */
	struct inode *d_inode;
};

BTF_TYPE_SAFE_TRUSTED_OR_NULL(struct socket) {
	struct sock *sk;
};

static bool type_is_rcu(struct bpf_verifier_env *env,
			struct bpf_reg_state *reg,
			const char *field_name, u32 btf_id)
{
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_RCU(struct task_struct));
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_RCU(struct cgroup));
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_RCU(struct css_set));

	return btf_nested_type_is_trusted(&env->log, reg, field_name, btf_id, "__safe_rcu");
}

static bool type_is_rcu_or_null(struct bpf_verifier_env *env,
				struct bpf_reg_state *reg,
				const char *field_name, u32 btf_id)
{
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_RCU_OR_NULL(struct mm_struct));
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_RCU_OR_NULL(struct sk_buff));
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_RCU_OR_NULL(struct request_sock));

	return btf_nested_type_is_trusted(&env->log, reg, field_name, btf_id, "__safe_rcu_or_null");
}

static bool type_is_trusted(struct bpf_verifier_env *env,
			    struct bpf_reg_state *reg,
			    const char *field_name, u32 btf_id)
{
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_TRUSTED(struct bpf_iter_meta));
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_TRUSTED(struct bpf_iter__task));
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_TRUSTED(struct linux_binprm));
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_TRUSTED(struct file));
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_TRUSTED(struct dentry));

	return btf_nested_type_is_trusted(&env->log, reg, field_name, btf_id, "__safe_trusted");
}

static bool type_is_trusted_or_null(struct bpf_verifier_env *env,
				    struct bpf_reg_state *reg,
				    const char *field_name, u32 btf_id)
{
	BTF_TYPE_EMIT(BTF_TYPE_SAFE_TRUSTED_OR_NULL(struct socket));

	return btf_nested_type_is_trusted(&env->log, reg, field_name, btf_id,
					  "__safe_trusted_or_null");
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
	const char *field_name = NULL;
	enum bpf_type_flag flag = 0;
	u32 btf_id = 0;
	bool mask;
	int ret;

	if (!env->allow_ptr_leaks) {
		verbose(env,
			"'struct %s' access is allowed only to CAP_PERFMON and CAP_SYS_ADMIN\n",
			tname);
		return -EPERM;
	}
	if (!env->prog->gpl_compatible && btf_is_kernel(reg->btf)) {
		verbose(env,
			"Cannot access kernel 'struct %s' from non-GPL compatible program\n",
			tname);
		return -EINVAL;
	}
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

	if (reg->type & MEM_USER) {
		verbose(env,
			"R%d is ptr_%s access user memory: off=%d\n",
			regno, tname, off);
		return -EACCES;
	}

	if (reg->type & MEM_PERCPU) {
		verbose(env,
			"R%d is ptr_%s access percpu memory: off=%d\n",
			regno, tname, off);
		return -EACCES;
	}

	if (env->ops->btf_struct_access && !type_is_alloc(reg->type) && atype == BPF_WRITE) {
		if (!btf_is_kernel(reg->btf)) {
			verbose(env, "verifier internal error: reg->btf must be kernel btf\n");
			return -EFAULT;
		}
		ret = env->ops->btf_struct_access(&env->log, reg, off, size);
	} else {
		/* Writes are permitted with default btf_struct_access for
		 * program allocated objects (which always have ref_obj_id > 0),
		 * but not for untrusted PTR_TO_BTF_ID | MEM_ALLOC.
		 */
		if (atype != BPF_READ && !type_is_ptr_alloc_obj(reg->type)) {
			verbose(env, "only read is supported\n");
			return -EACCES;
		}

		if (type_is_alloc(reg->type) && !type_is_non_owning_ref(reg->type) &&
		    !(reg->type & MEM_RCU) && !reg->ref_obj_id) {
			verbose(env, "verifier internal error: ref_obj_id for allocated object must be non-zero\n");
			return -EFAULT;
		}

		ret = btf_struct_access(&env->log, reg, off, size, atype, &btf_id, &flag, &field_name);
	}

	if (ret < 0)
		return ret;
	/* For raw_tp progs, we allow dereference of PTR_MAYBE_NULL
	 * trusted PTR_TO_BTF_ID, these are the ones that are possibly
	 * arguments to the raw_tp. Since internal checks in for trusted
	 * reg in check_ptr_to_btf_access would consider PTR_MAYBE_NULL
	 * modifier as problematic, mask it out temporarily for the
	 * check. Don't apply this to pointers with ref_obj_id > 0, as
	 * those won't be raw_tp args.
	 *
	 * We may end up applying this relaxation to other trusted
	 * PTR_TO_BTF_ID with maybe null flag, since we cannot
	 * distinguish PTR_MAYBE_NULL tagged for arguments vs normal
	 * tagging, but that should expand allowed behavior, and not
	 * cause regression for existing behavior.
	 */
	mask = mask_raw_tp_reg(env, reg);
	if (ret != PTR_TO_BTF_ID) {
		/* just mark; */

	} else if (type_flag(reg->type) & PTR_UNTRUSTED) {
		/* If this is an untrusted pointer, all pointers formed by walking it
		 * also inherit the untrusted flag.
		 */
		flag = PTR_UNTRUSTED;

	} else if (is_trusted_reg(reg) || is_rcu_reg(reg)) {
		/* By default any pointer obtained from walking a trusted pointer is no
		 * longer trusted, unless the field being accessed has explicitly been
		 * marked as inheriting its parent's state of trust (either full or RCU).
		 * For example:
		 * 'cgroups' pointer is untrusted if task->cgroups dereference
		 * happened in a sleepable program outside of bpf_rcu_read_lock()
		 * section. In a non-sleepable program it's trusted while in RCU CS (aka MEM_RCU).
		 * Note bpf_rcu_read_unlock() converts MEM_RCU pointers to PTR_UNTRUSTED.
		 *
		 * A regular RCU-protected pointer with __rcu tag can also be deemed
		 * trusted if we are in an RCU CS. Such pointer can be NULL.
		 */
		if (type_is_trusted(env, reg, field_name, btf_id)) {
			flag |= PTR_TRUSTED;
		} else if (type_is_trusted_or_null(env, reg, field_name, btf_id)) {
			flag |= PTR_TRUSTED | PTR_MAYBE_NULL;
		} else if (in_rcu_cs(env) && !type_may_be_null(reg->type)) {
			if (type_is_rcu(env, reg, field_name, btf_id)) {
				/* ignore __rcu tag and mark it MEM_RCU */
				flag |= MEM_RCU;
			} else if (flag & MEM_RCU ||
				   type_is_rcu_or_null(env, reg, field_name, btf_id)) {
				/* __rcu tagged pointers can be NULL */
				flag |= MEM_RCU | PTR_MAYBE_NULL;

				/* We always trust them */
				if (type_is_rcu_or_null(env, reg, field_name, btf_id) &&
				    flag & PTR_UNTRUSTED)
					flag &= ~PTR_UNTRUSTED;
			} else if (flag & (MEM_PERCPU | MEM_USER)) {
				/* keep as-is */
			} else {
				/* walking unknown pointers yields old deprecated PTR_TO_BTF_ID */
				clear_trusted_flags(&flag);
			}
		} else {
			/*
			 * If not in RCU CS or MEM_RCU pointer can be NULL then
			 * aggressively mark as untrusted otherwise such
			 * pointers will be plain PTR_TO_BTF_ID without flags
			 * and will be allowed to be passed into helpers for
			 * compat reasons.
			 */
			flag = PTR_UNTRUSTED;
		}
	} else {
		/* Old compat. Deprecated */
		clear_trusted_flags(&flag);
	}

	if (atype == BPF_READ && value_regno >= 0) {
		mark_btf_ld_reg(env, regs, value_regno, ret, reg->btf, btf_id, flag);
		/* We've assigned a new type to regno, so don't undo masking. */
		if (regno == value_regno)
			mask = false;
	}
	unmask_raw_tp_reg(reg, mask);

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
	struct bpf_reg_state map_reg;
	enum bpf_type_flag flag = 0;
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

	if (!env->allow_ptr_leaks) {
		verbose(env,
			"'struct %s' access is allowed only to CAP_PERFMON and CAP_SYS_ADMIN\n",
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

	/* Simulate access to a PTR_TO_BTF_ID */
	memset(&map_reg, 0, sizeof(map_reg));
	mark_btf_ld_reg(env, &map_reg, 0, PTR_TO_BTF_ID, btf_vmlinux, *map->ops->map_btf_id, 0);
	ret = btf_struct_access(&env->log, &map_reg, off, size, atype, &btf_id, &flag, NULL);
	if (ret < 0)
		return ret;

	if (value_regno >= 0)
		mark_btf_ld_reg(env, regs, value_regno, ret, btf_vmlinux, btf_id, flag);

	return 0;
}

/* Check that the stack access at the given offset is within bounds. The
 * maximum valid offset is -1.
 *
 * The minimum valid offset is -MAX_BPF_STACK for writes, and
 * -state->allocated_stack for reads.
 */
static int check_stack_slot_within_bounds(struct bpf_verifier_env *env,
                                          s64 off,
                                          struct bpf_func_state *state,
                                          enum bpf_access_type t)
{
	int min_valid_off;

	if (t == BPF_WRITE || env->allow_uninit_stack)
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
		enum bpf_access_src src, enum bpf_access_type type)
{
	struct bpf_reg_state *regs = cur_regs(env);
	struct bpf_reg_state *reg = regs + regno;
	struct bpf_func_state *state = func(env, reg);
	s64 min_off, max_off;
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
		min_off = (s64)reg->var_off.value + off;
		max_off = min_off + access_size;
	} else {
		if (reg->smax_value >= BPF_MAX_VAR_OFF ||
		    reg->smin_value <= -BPF_MAX_VAR_OFF) {
			verbose(env, "invalid unbounded variable-offset%s stack R%d\n",
				err_extra, regno);
			return -EACCES;
		}
		min_off = reg->smin_value + off;
		max_off = reg->smax_value + off + access_size;
	}

	err = check_stack_slot_within_bounds(env, min_off, state, type);
	if (!err && max_off > 0)
		err = -EINVAL; /* out of stack access into non-negative offsets */
	if (!err && access_size < 0)
		/* access_size should not be negative (or overflow an int); others checks
		 * along the way should have prevented such an access.
		 */
		err = -EFAULT; /* invalid negative access size; integer overflow? */

	if (err) {
		if (tnum_is_const(reg->var_off)) {
			verbose(env, "invalid%s stack R%d off=%d size=%d\n",
				err_extra, regno, off, access_size);
		} else {
			char tn_buf[48];

			tnum_strn(tn_buf, sizeof(tn_buf), reg->var_off);
			verbose(env, "invalid variable-offset%s stack R%d var_off=%s off=%d size=%d\n",
				err_extra, regno, tn_buf, off, access_size);
		}
		return err;
	}

	/* Note that there is no stack access with offset zero, so the needed stack
	 * size is -min_off, not -min_off+1.
	 */
	return grow_stack_state(env, state, -min_off /* size */);
}

static bool get_func_retval_range(struct bpf_prog *prog,
				  struct bpf_retval_range *range)
{
	if (prog->type == BPF_PROG_TYPE_LSM &&
		prog->expected_attach_type == BPF_LSM_MAC &&
		!bpf_lsm_get_retval_range(prog, range)) {
		return true;
	}
	return false;
}

/* check whether memory at (regno + off) is accessible for t = (read | write)
 * if t==write, value_regno is a register which value is stored into memory
 * if t==read, value_regno is a register which will receive the value from memory
 * if t==write && value_regno==-1, some unknown value is stored into memory
 * if t==read && value_regno==-1, don't care what we read from memory
 */
static int check_mem_access(struct bpf_verifier_env *env, int insn_idx, u32 regno,
			    int off, int bpf_size, enum bpf_access_type t,
			    int value_regno, bool strict_alignment_once, bool is_ldsx)
{
	struct bpf_reg_state *regs = cur_regs(env);
	struct bpf_reg_state *reg = regs + regno;
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

	if (reg->type == PTR_TO_MAP_KEY) {
		if (t == BPF_WRITE) {
			verbose(env, "write to change key R%d not allowed\n", regno);
			return -EACCES;
		}

		err = check_mem_region_access(env, regno, off, size,
					      reg->map_ptr->key_size, false);
		if (err)
			return err;
		if (value_regno >= 0)
			mark_reg_unknown(env, regs, value_regno);
	} else if (reg->type == PTR_TO_MAP_VALUE) {
		struct btf_field *kptr_field = NULL;

		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose(env, "R%d leaks addr into map\n", value_regno);
			return -EACCES;
		}
		err = check_map_access_type(env, regno, off, size, t);
		if (err)
			return err;
		err = check_map_access(env, regno, off, size, false, ACCESS_DIRECT);
		if (err)
			return err;
		if (tnum_is_const(reg->var_off))
			kptr_field = btf_record_find(reg->map_ptr->record,
						     off + reg->var_off.value, BPF_KPTR | BPF_UPTR);
		if (kptr_field) {
			err = check_map_kptr_access(env, regno, value_regno, insn_idx, kptr_field);
		} else if (t == BPF_READ && value_regno >= 0) {
			struct bpf_map *map = reg->map_ptr;

			/* if map is read-only, track its contents as scalars */
			if (tnum_is_const(reg->var_off) &&
			    bpf_map_is_rdonly(map) &&
			    map->ops->map_direct_value_addr) {
				int map_off = off + reg->var_off.value;
				u64 val = 0;

				err = bpf_map_direct_read(map, map_off, size,
							  &val, is_ldsx);
				if (err)
					return err;

				regs[value_regno].type = SCALAR_VALUE;
				__mark_reg_known(&regs[value_regno], val);
			} else {
				mark_reg_unknown(env, regs, value_regno);
			}
		}
	} else if (base_type(reg->type) == PTR_TO_MEM) {
		bool rdonly_mem = type_is_rdonly_mem(reg->type);

		if (type_may_be_null(reg->type)) {
			verbose(env, "R%d invalid mem access '%s'\n", regno,
				reg_type_str(env, reg->type));
			return -EACCES;
		}

		if (t == BPF_WRITE && rdonly_mem) {
			verbose(env, "R%d cannot write into %s\n",
				regno, reg_type_str(env, reg->type));
			return -EACCES;
		}

		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose(env, "R%d leaks addr into mem\n", value_regno);
			return -EACCES;
		}

		err = check_mem_region_access(env, regno, off, size,
					      reg->mem_size, false);
		if (!err && value_regno >= 0 && (t == BPF_READ || rdonly_mem))
			mark_reg_unknown(env, regs, value_regno);
	} else if (reg->type == PTR_TO_CTX) {
		bool is_retval = false;
		struct bpf_retval_range range;
		enum bpf_reg_type reg_type = SCALAR_VALUE;
		struct btf *btf = NULL;
		u32 btf_id = 0;

		if (t == BPF_WRITE && value_regno >= 0 &&
		    is_pointer_value(env, value_regno)) {
			verbose(env, "R%d leaks addr into ctx\n", value_regno);
			return -EACCES;
		}

		err = check_ptr_off_reg(env, reg, regno);
		if (err < 0)
			return err;

		err = check_ctx_access(env, insn_idx, off, size, t, &reg_type, &btf,
				       &btf_id, &is_retval, is_ldsx);
		if (err)
			verbose_linfo(env, insn_idx, "; ");
		if (!err && t == BPF_READ && value_regno >= 0) {
			/* ctx access returns either a scalar, or a
			 * PTR_TO_PACKET[_META,_END]. In the latter
			 * case, we know the offset is zero.
			 */
			if (reg_type == SCALAR_VALUE) {
				if (is_retval && get_func_retval_range(env->prog, &range)) {
					err = __mark_reg_s32_range(env, regs, value_regno,
								   range.minval, range.maxval);
					if (err)
						return err;
				} else {
					mark_reg_unknown(env, regs, value_regno);
				}
			} else {
				mark_reg_known_zero(env, regs,
						    value_regno);
				if (type_may_be_null(reg_type))
					regs[value_regno].id = ++env->id_gen;
				/* A load of ctx field could have different
				 * actual load size with the one encoded in the
				 * insn. When the dst is PTR, it is for sure not
				 * a sub-register.
				 */
				regs[value_regno].subreg_def = DEF_NOT_SUBREG;
				if (base_type(reg_type) == PTR_TO_BTF_ID) {
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
				regno, reg_type_str(env, reg->type));
			return -EACCES;
		}
		err = check_sock_access(env, insn_idx, regno, off, size, t);
		if (!err && value_regno >= 0)
			mark_reg_unknown(env, regs, value_regno);
	} else if (reg->type == PTR_TO_TP_BUFFER) {
		err = check_tp_buffer_access(env, reg, regno, off, size);
		if (!err && t == BPF_READ && value_regno >= 0)
			mark_reg_unknown(env, regs, value_regno);
	} else if (base_type(reg->type) == PTR_TO_BTF_ID &&
		   (mask_raw_tp_reg_cond(env, reg) || !type_may_be_null(reg->type))) {
		err = check_ptr_to_btf_access(env, regs, regno, off, size, t,
					      value_regno);
	} else if (reg->type == CONST_PTR_TO_MAP) {
		err = check_ptr_to_map_access(env, regs, regno, off, size, t,
					      value_regno);
	} else if (base_type(reg->type) == PTR_TO_BUF) {
		bool rdonly_mem = type_is_rdonly_mem(reg->type);
		u32 *max_access;

		if (rdonly_mem) {
			if (t == BPF_WRITE) {
				verbose(env, "R%d cannot write into %s\n",
					regno, reg_type_str(env, reg->type));
				return -EACCES;
			}
			max_access = &env->prog->aux->max_rdonly_access;
		} else {
			max_access = &env->prog->aux->max_rdwr_access;
		}

		err = check_buffer_access(env, reg, regno, off, size, false,
					  max_access);

		if (!err && value_regno >= 0 && (rdonly_mem || t == BPF_READ))
			mark_reg_unknown(env, regs, value_regno);
	} else if (reg->type == PTR_TO_ARENA) {
		if (t == BPF_READ && value_regno >= 0)
			mark_reg_unknown(env, regs, value_regno);
	} else {
		verbose(env, "R%d invalid mem access '%s'\n", regno,
			reg_type_str(env, reg->type));
		return -EACCES;
	}

	if (!err && size < BPF_REG_SIZE && value_regno >= 0 && t == BPF_READ &&
	    regs[value_regno].type == SCALAR_VALUE) {
		if (!is_ldsx)
			/* b/h/w load zero-extends, mark upper bits as known 0 */
			coerce_reg_to_size(&regs[value_regno], size);
		else
			coerce_reg_to_size_sx(&regs[value_regno], size);
	}
	return err;
}

static int save_aux_ptr_type(struct bpf_verifier_env *env, enum bpf_reg_type type,
			     bool allow_trust_mismatch);

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
		const u32 aux_reg = BPF_REG_0;

		err = check_reg_arg(env, aux_reg, SRC_OP);
		if (err)
			return err;

		if (is_pointer_value(env, aux_reg)) {
			verbose(env, "R%d leaks addr into mem\n", aux_reg);
			return -EACCES;
		}
	}

	if (is_pointer_value(env, insn->src_reg)) {
		verbose(env, "R%d leaks addr into mem\n", insn->src_reg);
		return -EACCES;
	}

	if (is_ctx_reg(env, insn->dst_reg) ||
	    is_pkt_reg(env, insn->dst_reg) ||
	    is_flow_key_reg(env, insn->dst_reg) ||
	    is_sk_reg(env, insn->dst_reg) ||
	    (is_arena_reg(env, insn->dst_reg) && !bpf_jit_supports_insn(insn, true))) {
		verbose(env, "BPF_ATOMIC stores into R%d %s is not allowed\n",
			insn->dst_reg,
			reg_type_str(env, reg_state(env, insn->dst_reg)->type));
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

	/* Check whether we can read the memory, with second call for fetch
	 * case to simulate the register fill.
	 */
	err = check_mem_access(env, insn_idx, insn->dst_reg, insn->off,
			       BPF_SIZE(insn->code), BPF_READ, -1, true, false);
	if (!err && load_reg >= 0)
		err = check_mem_access(env, insn_idx, insn->dst_reg, insn->off,
				       BPF_SIZE(insn->code), BPF_READ, load_reg,
				       true, false);
	if (err)
		return err;

	if (is_arena_reg(env, insn->dst_reg)) {
		err = save_aux_ptr_type(env, PTR_TO_ARENA, false);
		if (err)
			return err;
	}
	/* Check whether we can write into the same memory. */
	err = check_mem_access(env, insn_idx, insn->dst_reg, insn->off,
			       BPF_SIZE(insn->code), BPF_WRITE, -1, true, false);
	if (err)
		return err;
	return 0;
}

/* When register 'regno' is used to read the stack (either directly or through
 * a helper function) make sure that it's within stack boundary and, depending
 * on the access type and privileges, that all elements of the stack are
 * initialized.
 *
 * 'off' includes 'regno->off', but not its dynamic part (if any).
 *
 * All registers that have been spilled on the stack in the slots within the
 * read offsets are marked as read.
 */
static int check_stack_range_initialized(
		struct bpf_verifier_env *env, int regno, int off,
		int access_size, bool zero_size_allowed,
		enum bpf_access_src type, struct bpf_call_arg_meta *meta)
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
		/* Ensure we won't be overwriting dynptrs when simulating byte
		 * by byte access in check_helper_call using meta.access_size.
		 * This would be a problem if we have a helper in the future
		 * which takes:
		 *
		 *	helper(uninit_mem, len, dynptr)
		 *
		 * Now, uninint_mem may overlap with dynptr pointer. Hence, it
		 * may end up writing to dynptr itself when touching memory from
		 * arg 1. This can be relaxed on a case by case basis for known
		 * safe cases, but reject due to the possibilitiy of aliasing by
		 * default.
		 */
		for (i = min_off; i < max_off + access_size; i++) {
			int stack_off = -i - 1;

			spi = __get_spi(i);
			/* raw_mode may write past allocated_stack */
			if (state->allocated_stack <= stack_off)
				continue;
			if (state->stack[spi].slot_type[stack_off % BPF_REG_SIZE] == STACK_DYNPTR) {
				verbose(env, "potential write to dynptr at off=%d disallowed\n", i);
				return -EACCES;
			}
		}
		meta->access_size = access_size;
		meta->regno = regno;
		return 0;
	}

	for (i = min_off; i < max_off + access_size; i++) {
		u8 *stype;

		slot = -i - 1;
		spi = slot / BPF_REG_SIZE;
		if (state->allocated_stack <= slot) {
			verbose(env, "verifier bug: allocated_stack too small");
			return -EFAULT;
		}

		stype = &state->stack[spi].slot_type[slot % BPF_REG_SIZE];
		if (*stype == STACK_MISC)
			goto mark;
		if ((*stype == STACK_ZERO) ||
		    (*stype == STACK_INVALID && env->allow_uninit_stack)) {
			if (clobber) {
				/* helper can write anything into the stack */
				*stype = STACK_MISC;
			}
			goto mark;
		}

		if (is_spilled_reg(&state->stack[spi]) &&
		    (state->stack[spi].spilled_ptr.type == SCALAR_VALUE ||
		     env->allow_ptr_leaks)) {
			if (clobber) {
				__mark_reg_unknown(env, &state->stack[spi].spilled_ptr);
				for (j = 0; j < BPF_REG_SIZE; j++)
					scrub_spilled_slot(&state->stack[spi].slot_type[j]);
			}
			goto mark;
		}

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
		/* We do not set REG_LIVE_WRITTEN for stack slot, as we can not
		 * be sure that whether stack slot is written to or not. Hence,
		 * we must still conservatively propagate reads upwards even if
		 * helper may write to the entire memory range.
		 */
	}
	return 0;
}

static int check_helper_mem_access(struct bpf_verifier_env *env, int regno,
				   int access_size, enum bpf_access_type access_type,
				   bool zero_size_allowed,
				   struct bpf_call_arg_meta *meta)
{
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	u32 *max_access;

	switch (base_type(reg->type)) {
	case PTR_TO_PACKET:
	case PTR_TO_PACKET_META:
		return check_packet_access(env, regno, reg->off, access_size,
					   zero_size_allowed);
	case PTR_TO_MAP_KEY:
		if (access_type == BPF_WRITE) {
			verbose(env, "R%d cannot write into %s\n", regno,
				reg_type_str(env, reg->type));
			return -EACCES;
		}
		return check_mem_region_access(env, regno, reg->off, access_size,
					       reg->map_ptr->key_size, false);
	case PTR_TO_MAP_VALUE:
		if (check_map_access_type(env, regno, reg->off, access_size, access_type))
			return -EACCES;
		return check_map_access(env, regno, reg->off, access_size,
					zero_size_allowed, ACCESS_HELPER);
	case PTR_TO_MEM:
		if (type_is_rdonly_mem(reg->type)) {
			if (access_type == BPF_WRITE) {
				verbose(env, "R%d cannot write into %s\n", regno,
					reg_type_str(env, reg->type));
				return -EACCES;
			}
		}
		return check_mem_region_access(env, regno, reg->off,
					       access_size, reg->mem_size,
					       zero_size_allowed);
	case PTR_TO_BUF:
		if (type_is_rdonly_mem(reg->type)) {
			if (access_type == BPF_WRITE) {
				verbose(env, "R%d cannot write into %s\n", regno,
					reg_type_str(env, reg->type));
				return -EACCES;
			}

			max_access = &env->prog->aux->max_rdonly_access;
		} else {
			max_access = &env->prog->aux->max_rdwr_access;
		}
		return check_buffer_access(env, reg, regno, reg->off,
					   access_size, zero_size_allowed,
					   max_access);
	case PTR_TO_STACK:
		return check_stack_range_initialized(
				env,
				regno, reg->off, access_size,
				zero_size_allowed, ACCESS_HELPER, meta);
	case PTR_TO_BTF_ID:
		return check_ptr_to_btf_access(env, regs, regno, reg->off,
					       access_size, BPF_READ, -1);
	case PTR_TO_CTX:
		/* in case the function doesn't know how to access the context,
		 * (because we are in a program of type SYSCALL for example), we
		 * can not statically check its size.
		 * Dynamically check it now.
		 */
		if (!env->ops->convert_ctx_access) {
			int offset = access_size - 1;

			/* Allow zero-byte read from PTR_TO_CTX */
			if (access_size == 0)
				return zero_size_allowed ? 0 : -EACCES;

			return check_mem_access(env, env->insn_idx, regno, offset, BPF_B,
						access_type, -1, false, false);
		}

		fallthrough;
	default: /* scalar_value or invalid ptr */
		/* Allow zero-byte read from NULL, regardless of pointer type */
		if (zero_size_allowed && access_size == 0 &&
		    register_is_null(reg))
			return 0;

		verbose(env, "R%d type=%s ", regno,
			reg_type_str(env, reg->type));
		verbose(env, "expected=%s\n", reg_type_str(env, PTR_TO_STACK));
		return -EACCES;
	}
}

/* verify arguments to helpers or kfuncs consisting of a pointer and an access
 * size.
 *
 * @regno is the register containing the access size. regno-1 is the register
 * containing the pointer.
 */
static int check_mem_size_reg(struct bpf_verifier_env *env,
			      struct bpf_reg_state *reg, u32 regno,
			      enum bpf_access_type access_type,
			      bool zero_size_allowed,
			      struct bpf_call_arg_meta *meta)
{
	int err;

	/* This is used to refine r0 return value bounds for helpers
	 * that enforce this value as an upper bound on return values.
	 * See do_refine_retval_range() for helpers that can refine
	 * the return value. C type of helper is u32 so we pull register
	 * bound from umax_value however, if negative verifier errors
	 * out. Only upper bounds can be learned because retval is an
	 * int type and negative retvals are allowed.
	 */
	meta->msize_max_value = reg->umax_value;

	/* The register is SCALAR_VALUE; the access check happens using
	 * its boundaries. For unprivileged variable accesses, disable
	 * raw mode so that the program is required to initialize all
	 * the memory that the helper could just partially fill up.
	 */
	if (!tnum_is_const(reg->var_off))
		meta = NULL;

	if (reg->smin_value < 0) {
		verbose(env, "R%d min value is negative, either use unsigned or 'var &= const'\n",
			regno);
		return -EACCES;
	}

	if (reg->umin_value == 0 && !zero_size_allowed) {
		verbose(env, "R%d invalid zero-sized read: u64=[%lld,%lld]\n",
			regno, reg->umin_value, reg->umax_value);
		return -EACCES;
	}

	if (reg->umax_value >= BPF_MAX_VAR_SIZ) {
		verbose(env, "R%d unbounded memory access, use 'var &= const' or 'if (var < const)'\n",
			regno);
		return -EACCES;
	}
	err = check_helper_mem_access(env, regno - 1, reg->umax_value,
				      access_type, zero_size_allowed, meta);
	if (!err)
		err = mark_chain_precision(env, regno);
	return err;
}

static int check_mem_reg(struct bpf_verifier_env *env, struct bpf_reg_state *reg,
			 u32 regno, u32 mem_size)
{
	bool may_be_null = type_may_be_null(reg->type);
	struct bpf_reg_state saved_reg;
	int err;

	if (register_is_null(reg))
		return 0;

	/* Assuming that the register contains a value check if the memory
	 * access is safe. Temporarily save and restore the register's state as
	 * the conversion shouldn't be visible to a caller.
	 */
	if (may_be_null) {
		saved_reg = *reg;
		mark_ptr_not_null_reg(reg);
	}

	err = check_helper_mem_access(env, regno, mem_size, BPF_READ, true, NULL);
	err = err ?: check_helper_mem_access(env, regno, mem_size, BPF_WRITE, true, NULL);

	if (may_be_null)
		*reg = saved_reg;

	return err;
}

static int check_kfunc_mem_size_reg(struct bpf_verifier_env *env, struct bpf_reg_state *reg,
				    u32 regno)
{
	struct bpf_reg_state *mem_reg = &cur_regs(env)[regno - 1];
	bool may_be_null = type_may_be_null(mem_reg->type);
	struct bpf_reg_state saved_reg;
	struct bpf_call_arg_meta meta;
	int err;

	WARN_ON_ONCE(regno < BPF_REG_2 || regno > BPF_REG_5);

	memset(&meta, 0, sizeof(meta));

	if (may_be_null) {
		saved_reg = *mem_reg;
		mark_ptr_not_null_reg(mem_reg);
	}

	err = check_mem_size_reg(env, reg, regno, BPF_READ, true, &meta);
	err = err ?: check_mem_size_reg(env, reg, regno, BPF_WRITE, true, &meta);

	if (may_be_null)
		*mem_reg = saved_reg;

	return err;
}

/* Implementation details:
 * bpf_map_lookup returns PTR_TO_MAP_VALUE_OR_NULL.
 * bpf_obj_new returns PTR_TO_BTF_ID | MEM_ALLOC | PTR_MAYBE_NULL.
 * Two bpf_map_lookups (even with the same key) will have different reg->id.
 * Two separate bpf_obj_new will also have different reg->id.
 * For traditional PTR_TO_MAP_VALUE or PTR_TO_BTF_ID | MEM_ALLOC, the verifier
 * clears reg->id after value_or_null->value transition, since the verifier only
 * cares about the range of access to valid map value pointer and doesn't care
 * about actual address of the map element.
 * For maps with 'struct bpf_spin_lock' inside map value the verifier keeps
 * reg->id > 0 after value_or_null->value transition. By doing so
 * two bpf_map_lookups will be considered two different pointers that
 * point to different bpf_spin_locks. Likewise for pointers to allocated objects
 * returned from bpf_obj_new.
 * The verifier allows taking only one bpf_spin_lock at a time to avoid
 * dead-locks.
 * Since only one bpf_spin_lock is allowed the checks are simpler than
 * reg_is_refcounted() logic. The verifier needs to remember only
 * one spin_lock instead of array of acquired_refs.
 * cur_func(env)->active_locks remembers which map value element or allocated
 * object got locked and clears it after bpf_spin_unlock.
 */
static int process_spin_lock(struct bpf_verifier_env *env, int regno,
			     bool is_lock)
{
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	bool is_const = tnum_is_const(reg->var_off);
	struct bpf_func_state *cur = cur_func(env);
	u64 val = reg->var_off.value;
	struct bpf_map *map = NULL;
	struct btf *btf = NULL;
	struct btf_record *rec;
	int err;

	if (!is_const) {
		verbose(env,
			"R%d doesn't have constant offset. bpf_spin_lock has to be at the constant offset\n",
			regno);
		return -EINVAL;
	}
	if (reg->type == PTR_TO_MAP_VALUE) {
		map = reg->map_ptr;
		if (!map->btf) {
			verbose(env,
				"map '%s' has to have BTF in order to use bpf_spin_lock\n",
				map->name);
			return -EINVAL;
		}
	} else {
		btf = reg->btf;
	}

	rec = reg_btf_record(reg);
	if (!btf_record_has_field(rec, BPF_SPIN_LOCK)) {
		verbose(env, "%s '%s' has no valid bpf_spin_lock\n", map ? "map" : "local",
			map ? map->name : "kptr");
		return -EINVAL;
	}
	if (rec->spin_lock_off != val + reg->off) {
		verbose(env, "off %lld doesn't point to 'struct bpf_spin_lock' that is at %d\n",
			val + reg->off, rec->spin_lock_off);
		return -EINVAL;
	}
	if (is_lock) {
		void *ptr;

		if (map)
			ptr = map;
		else
			ptr = btf;

		if (cur->active_locks) {
			verbose(env,
				"Locking two bpf_spin_locks are not allowed\n");
			return -EINVAL;
		}
		err = acquire_lock_state(env, env->insn_idx, REF_TYPE_LOCK, reg->id, ptr);
		if (err < 0) {
			verbose(env, "Failed to acquire lock state\n");
			return err;
		}
	} else {
		void *ptr;

		if (map)
			ptr = map;
		else
			ptr = btf;

		if (!cur->active_locks) {
			verbose(env, "bpf_spin_unlock without taking a lock\n");
			return -EINVAL;
		}

		if (release_lock_state(cur_func(env), REF_TYPE_LOCK, reg->id, ptr)) {
			verbose(env, "bpf_spin_unlock of different lock\n");
			return -EINVAL;
		}

		invalidate_non_owning_refs(env);
	}
	return 0;
}

static int process_timer_func(struct bpf_verifier_env *env, int regno,
			      struct bpf_call_arg_meta *meta)
{
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	bool is_const = tnum_is_const(reg->var_off);
	struct bpf_map *map = reg->map_ptr;
	u64 val = reg->var_off.value;

	if (!is_const) {
		verbose(env,
			"R%d doesn't have constant offset. bpf_timer has to be at the constant offset\n",
			regno);
		return -EINVAL;
	}
	if (!map->btf) {
		verbose(env, "map '%s' has to have BTF in order to use bpf_timer\n",
			map->name);
		return -EINVAL;
	}
	if (!btf_record_has_field(map->record, BPF_TIMER)) {
		verbose(env, "map '%s' has no valid bpf_timer\n", map->name);
		return -EINVAL;
	}
	if (map->record->timer_off != val + reg->off) {
		verbose(env, "off %lld doesn't point to 'struct bpf_timer' that is at %d\n",
			val + reg->off, map->record->timer_off);
		return -EINVAL;
	}
	if (meta->map_ptr) {
		verbose(env, "verifier bug. Two map pointers in a timer helper\n");
		return -EFAULT;
	}
	meta->map_uid = reg->map_uid;
	meta->map_ptr = map;
	return 0;
}

static int process_wq_func(struct bpf_verifier_env *env, int regno,
			   struct bpf_kfunc_call_arg_meta *meta)
{
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	struct bpf_map *map = reg->map_ptr;
	u64 val = reg->var_off.value;

	if (map->record->wq_off != val + reg->off) {
		verbose(env, "off %lld doesn't point to 'struct bpf_wq' that is at %d\n",
			val + reg->off, map->record->wq_off);
		return -EINVAL;
	}
	meta->map.uid = reg->map_uid;
	meta->map.ptr = map;
	return 0;
}

static int process_kptr_func(struct bpf_verifier_env *env, int regno,
			     struct bpf_call_arg_meta *meta)
{
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	struct btf_field *kptr_field;
	struct bpf_map *map_ptr;
	struct btf_record *rec;
	u32 kptr_off;

	if (type_is_ptr_alloc_obj(reg->type)) {
		rec = reg_btf_record(reg);
	} else { /* PTR_TO_MAP_VALUE */
		map_ptr = reg->map_ptr;
		if (!map_ptr->btf) {
			verbose(env, "map '%s' has to have BTF in order to use bpf_kptr_xchg\n",
				map_ptr->name);
			return -EINVAL;
		}
		rec = map_ptr->record;
		meta->map_ptr = map_ptr;
	}

	if (!tnum_is_const(reg->var_off)) {
		verbose(env,
			"R%d doesn't have constant offset. kptr has to be at the constant offset\n",
			regno);
		return -EINVAL;
	}

	if (!btf_record_has_field(rec, BPF_KPTR)) {
		verbose(env, "R%d has no valid kptr\n", regno);
		return -EINVAL;
	}

	kptr_off = reg->off + reg->var_off.value;
	kptr_field = btf_record_find(rec, kptr_off, BPF_KPTR);
	if (!kptr_field) {
		verbose(env, "off=%d doesn't point to kptr\n", kptr_off);
		return -EACCES;
	}
	if (kptr_field->type != BPF_KPTR_REF && kptr_field->type != BPF_KPTR_PERCPU) {
		verbose(env, "off=%d kptr isn't referenced kptr\n", kptr_off);
		return -EACCES;
	}
	meta->kptr_field = kptr_field;
	return 0;
}

/* There are two register types representing a bpf_dynptr, one is PTR_TO_STACK
 * which points to a stack slot, and the other is CONST_PTR_TO_DYNPTR.
 *
 * In both cases we deal with the first 8 bytes, but need to mark the next 8
 * bytes as STACK_DYNPTR in case of PTR_TO_STACK. In case of
 * CONST_PTR_TO_DYNPTR, we are guaranteed to get the beginning of the object.
 *
 * Mutability of bpf_dynptr is at two levels, one is at the level of struct
 * bpf_dynptr itself, i.e. whether the helper is receiving a pointer to struct
 * bpf_dynptr or pointer to const struct bpf_dynptr. In the former case, it can
 * mutate the view of the dynptr and also possibly destroy it. In the latter
 * case, it cannot mutate the bpf_dynptr itself but it can still mutate the
 * memory that dynptr points to.
 *
 * The verifier will keep track both levels of mutation (bpf_dynptr's in
 * reg->type and the memory's in reg->dynptr.type), but there is no support for
 * readonly dynptr view yet, hence only the first case is tracked and checked.
 *
 * This is consistent with how C applies the const modifier to a struct object,
 * where the pointer itself inside bpf_dynptr becomes const but not what it
 * points to.
 *
 * Helpers which do not mutate the bpf_dynptr set MEM_RDONLY in their argument
 * type, and declare it as 'const struct bpf_dynptr *' in their prototype.
 */
static int process_dynptr_func(struct bpf_verifier_env *env, int regno, int insn_idx,
			       enum bpf_arg_type arg_type, int clone_ref_obj_id)
{
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	int err;

	if (reg->type != PTR_TO_STACK && reg->type != CONST_PTR_TO_DYNPTR) {
		verbose(env,
			"arg#%d expected pointer to stack or const struct bpf_dynptr\n",
			regno - 1);
		return -EINVAL;
	}

	/* MEM_UNINIT and MEM_RDONLY are exclusive, when applied to an
	 * ARG_PTR_TO_DYNPTR (or ARG_PTR_TO_DYNPTR | DYNPTR_TYPE_*):
	 */
	if ((arg_type & (MEM_UNINIT | MEM_RDONLY)) == (MEM_UNINIT | MEM_RDONLY)) {
		verbose(env, "verifier internal error: misconfigured dynptr helper type flags\n");
		return -EFAULT;
	}

	/*  MEM_UNINIT - Points to memory that is an appropriate candidate for
	 *		 constructing a mutable bpf_dynptr object.
	 *
	 *		 Currently, this is only possible with PTR_TO_STACK
	 *		 pointing to a region of at least 16 bytes which doesn't
	 *		 contain an existing bpf_dynptr.
	 *
	 *  MEM_RDONLY - Points to a initialized bpf_dynptr that will not be
	 *		 mutated or destroyed. However, the memory it points to
	 *		 may be mutated.
	 *
	 *  None       - Points to a initialized dynptr that can be mutated and
	 *		 destroyed, including mutation of the memory it points
	 *		 to.
	 */
	if (arg_type & MEM_UNINIT) {
		int i;

		if (!is_dynptr_reg_valid_uninit(env, reg)) {
			verbose(env, "Dynptr has to be an uninitialized dynptr\n");
			return -EINVAL;
		}

		/* we write BPF_DW bits (8 bytes) at a time */
		for (i = 0; i < BPF_DYNPTR_SIZE; i += 8) {
			err = check_mem_access(env, insn_idx, regno,
					       i, BPF_DW, BPF_WRITE, -1, false, false);
			if (err)
				return err;
		}

		err = mark_stack_slots_dynptr(env, reg, arg_type, insn_idx, clone_ref_obj_id);
	} else /* MEM_RDONLY and None case from above */ {
		/* For the reg->type == PTR_TO_STACK case, bpf_dynptr is never const */
		if (reg->type == CONST_PTR_TO_DYNPTR && !(arg_type & MEM_RDONLY)) {
			verbose(env, "cannot pass pointer to const bpf_dynptr, the helper mutates it\n");
			return -EINVAL;
		}

		if (!is_dynptr_reg_valid_init(env, reg)) {
			verbose(env,
				"Expected an initialized dynptr as arg #%d\n",
				regno - 1);
			return -EINVAL;
		}

		/* Fold modifiers (in this case, MEM_RDONLY) when checking expected type */
		if (!is_dynptr_type_expected(env, reg, arg_type & ~MEM_RDONLY)) {
			verbose(env,
				"Expected a dynptr of type %s as arg #%d\n",
				dynptr_type_str(arg_to_dynptr_type(arg_type)), regno - 1);
			return -EINVAL;
		}

		err = mark_dynptr_read(env, reg);
	}
	return err;
}

static u32 iter_ref_obj_id(struct bpf_verifier_env *env, struct bpf_reg_state *reg, int spi)
{
	struct bpf_func_state *state = func(env, reg);

	return state->stack[spi].spilled_ptr.ref_obj_id;
}

static bool is_iter_kfunc(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->kfunc_flags & (KF_ITER_NEW | KF_ITER_NEXT | KF_ITER_DESTROY);
}

static bool is_iter_new_kfunc(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_ITER_NEW;
}

static bool is_iter_next_kfunc(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_ITER_NEXT;
}

static bool is_iter_destroy_kfunc(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_ITER_DESTROY;
}

static bool is_kfunc_arg_iter(struct bpf_kfunc_call_arg_meta *meta, int arg_idx,
			      const struct btf_param *arg)
{
	/* btf_check_iter_kfuncs() guarantees that first argument of any iter
	 * kfunc is iter state pointer
	 */
	if (is_iter_kfunc(meta))
		return arg_idx == 0;

	/* iter passed as an argument to a generic kfunc */
	return btf_param_match_suffix(meta->btf, arg, "__iter");
}

static int process_iter_arg(struct bpf_verifier_env *env, int regno, int insn_idx,
			    struct bpf_kfunc_call_arg_meta *meta)
{
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	const struct btf_type *t;
	int spi, err, i, nr_slots, btf_id;

	if (reg->type != PTR_TO_STACK) {
		verbose(env, "arg#%d expected pointer to an iterator on stack\n", regno - 1);
		return -EINVAL;
	}

	/* For iter_{new,next,destroy} functions, btf_check_iter_kfuncs()
	 * ensures struct convention, so we wouldn't need to do any BTF
	 * validation here. But given iter state can be passed as a parameter
	 * to any kfunc, if arg has "__iter" suffix, we need to be a bit more
	 * conservative here.
	 */
	btf_id = btf_check_iter_arg(meta->btf, meta->func_proto, regno - 1);
	if (btf_id < 0) {
		verbose(env, "expected valid iter pointer as arg #%d\n", regno - 1);
		return -EINVAL;
	}
	t = btf_type_by_id(meta->btf, btf_id);
	nr_slots = t->size / BPF_REG_SIZE;

	if (is_iter_new_kfunc(meta)) {
		/* bpf_iter_<type>_new() expects pointer to uninit iter state */
		if (!is_iter_reg_valid_uninit(env, reg, nr_slots)) {
			verbose(env, "expected uninitialized iter_%s as arg #%d\n",
				iter_type_str(meta->btf, btf_id), regno - 1);
			return -EINVAL;
		}

		for (i = 0; i < nr_slots * 8; i += BPF_REG_SIZE) {
			err = check_mem_access(env, insn_idx, regno,
					       i, BPF_DW, BPF_WRITE, -1, false, false);
			if (err)
				return err;
		}

		err = mark_stack_slots_iter(env, meta, reg, insn_idx, meta->btf, btf_id, nr_slots);
		if (err)
			return err;
	} else {
		/* iter_next() or iter_destroy(), as well as any kfunc
		 * accepting iter argument, expect initialized iter state
		 */
		err = is_iter_reg_valid_init(env, reg, meta->btf, btf_id, nr_slots);
		switch (err) {
		case 0:
			break;
		case -EINVAL:
			verbose(env, "expected an initialized iter_%s as arg #%d\n",
				iter_type_str(meta->btf, btf_id), regno - 1);
			return err;
		case -EPROTO:
			verbose(env, "expected an RCU CS when using %s\n", meta->func_name);
			return err;
		default:
			return err;
		}

		spi = iter_get_spi(env, reg, nr_slots);
		if (spi < 0)
			return spi;

		err = mark_iter_read(env, reg, spi, nr_slots);
		if (err)
			return err;

		/* remember meta->iter info for process_iter_next_call() */
		meta->iter.spi = spi;
		meta->iter.frameno = reg->frameno;
		meta->ref_obj_id = iter_ref_obj_id(env, reg, spi);

		if (is_iter_destroy_kfunc(meta)) {
			err = unmark_stack_slots_iter(env, reg, nr_slots);
			if (err)
				return err;
		}
	}

	return 0;
}

/* Look for a previous loop entry at insn_idx: nearest parent state
 * stopped at insn_idx with callsites matching those in cur->frame.
 */
static struct bpf_verifier_state *find_prev_entry(struct bpf_verifier_env *env,
						  struct bpf_verifier_state *cur,
						  int insn_idx)
{
	struct bpf_verifier_state_list *sl;
	struct bpf_verifier_state *st;

	/* Explored states are pushed in stack order, most recent states come first */
	sl = *explored_state(env, insn_idx);
	for (; sl; sl = sl->next) {
		/* If st->branches != 0 state is a part of current DFS verification path,
		 * hence cur & st for a loop.
		 */
		st = &sl->state;
		if (st->insn_idx == insn_idx && st->branches && same_callsites(st, cur) &&
		    st->dfs_depth < cur->dfs_depth)
			return st;
	}

	return NULL;
}

static void reset_idmap_scratch(struct bpf_verifier_env *env);
static bool regs_exact(const struct bpf_reg_state *rold,
		       const struct bpf_reg_state *rcur,
		       struct bpf_idmap *idmap);

static void maybe_widen_reg(struct bpf_verifier_env *env,
			    struct bpf_reg_state *rold, struct bpf_reg_state *rcur,
			    struct bpf_idmap *idmap)
{
	if (rold->type != SCALAR_VALUE)
		return;
	if (rold->type != rcur->type)
		return;
	if (rold->precise || rcur->precise || regs_exact(rold, rcur, idmap))
		return;
	__mark_reg_unknown(env, rcur);
}

static int widen_imprecise_scalars(struct bpf_verifier_env *env,
				   struct bpf_verifier_state *old,
				   struct bpf_verifier_state *cur)
{
	struct bpf_func_state *fold, *fcur;
	int i, fr;

	reset_idmap_scratch(env);
	for (fr = old->curframe; fr >= 0; fr--) {
		fold = old->frame[fr];
		fcur = cur->frame[fr];

		for (i = 0; i < MAX_BPF_REG; i++)
			maybe_widen_reg(env,
					&fold->regs[i],
					&fcur->regs[i],
					&env->idmap_scratch);

		for (i = 0; i < fold->allocated_stack / BPF_REG_SIZE; i++) {
			if (!is_spilled_reg(&fold->stack[i]) ||
			    !is_spilled_reg(&fcur->stack[i]))
				continue;

			maybe_widen_reg(env,
					&fold->stack[i].spilled_ptr,
					&fcur->stack[i].spilled_ptr,
					&env->idmap_scratch);
		}
	}
	return 0;
}

static struct bpf_reg_state *get_iter_from_state(struct bpf_verifier_state *cur_st,
						 struct bpf_kfunc_call_arg_meta *meta)
{
	int iter_frameno = meta->iter.frameno;
	int iter_spi = meta->iter.spi;

	return &cur_st->frame[iter_frameno]->stack[iter_spi].spilled_ptr;
}

/* process_iter_next_call() is called when verifier gets to iterator's next
 * "method" (e.g., bpf_iter_num_next() for numbers iterator) call. We'll refer
 * to it as just "iter_next()" in comments below.
 *
 * BPF verifier relies on a crucial contract for any iter_next()
 * implementation: it should *eventually* return NULL, and once that happens
 * it should keep returning NULL. That is, once iterator exhausts elements to
 * iterate, it should never reset or spuriously return new elements.
 *
 * With the assumption of such contract, process_iter_next_call() simulates
 * a fork in the verifier state to validate loop logic correctness and safety
 * without having to simulate infinite amount of iterations.
 *
 * In current state, we first assume that iter_next() returned NULL and
 * iterator state is set to DRAINED (BPF_ITER_STATE_DRAINED). In such
 * conditions we should not form an infinite loop and should eventually reach
 * exit.
 *
 * Besides that, we also fork current state and enqueue it for later
 * verification. In a forked state we keep iterator state as ACTIVE
 * (BPF_ITER_STATE_ACTIVE) and assume non-NULL return from iter_next(). We
 * also bump iteration depth to prevent erroneous infinite loop detection
 * later on (see iter_active_depths_differ() comment for details). In this
 * state we assume that we'll eventually loop back to another iter_next()
 * calls (it could be in exactly same location or in some other instruction,
 * it doesn't matter, we don't make any unnecessary assumptions about this,
 * everything revolves around iterator state in a stack slot, not which
 * instruction is calling iter_next()). When that happens, we either will come
 * to iter_next() with equivalent state and can conclude that next iteration
 * will proceed in exactly the same way as we just verified, so it's safe to
 * assume that loop converges. If not, we'll go on another iteration
 * simulation with a different input state, until all possible starting states
 * are validated or we reach maximum number of instructions limit.
 *
 * This way, we will either exhaustively discover all possible input states
 * that iterator loop can start with and eventually will converge, or we'll
 * effectively regress into bounded loop simulation logic and either reach
 * maximum number of instructions if loop is not provably convergent, or there
 * is some statically known limit on number of iterations (e.g., if there is
 * an explicit `if n > 100 then break;` statement somewhere in the loop).
 *
 * Iteration convergence logic in is_state_visited() relies on exact
 * states comparison, which ignores read and precision marks.
 * This is necessary because read and precision marks are not finalized
 * while in the loop. Exact comparison might preclude convergence for
 * simple programs like below:
 *
 *     i = 0;
 *     while(iter_next(&it))
 *       i++;
 *
 * At each iteration step i++ would produce a new distinct state and
 * eventually instruction processing limit would be reached.
 *
 * To avoid such behavior speculatively forget (widen) range for
 * imprecise scalar registers, if those registers were not precise at the
 * end of the previous iteration and do not match exactly.
 *
 * This is a conservative heuristic that allows to verify wide range of programs,
 * however it precludes verification of programs that conjure an
 * imprecise value on the first loop iteration and use it as precise on a second.
 * For example, the following safe program would fail to verify:
 *
 *     struct bpf_num_iter it;
 *     int arr[10];
 *     int i = 0, a = 0;
 *     bpf_iter_num_new(&it, 0, 10);
 *     while (bpf_iter_num_next(&it)) {
 *       if (a == 0) {
 *         a = 1;
 *         i = 7; // Because i changed verifier would forget
 *                // it's range on second loop entry.
 *       } else {
 *         arr[i] = 42; // This would fail to verify.
 *       }
 *     }
 *     bpf_iter_num_destroy(&it);
 */
static int process_iter_next_call(struct bpf_verifier_env *env, int insn_idx,
				  struct bpf_kfunc_call_arg_meta *meta)
{
	struct bpf_verifier_state *cur_st = env->cur_state, *queued_st, *prev_st;
	struct bpf_func_state *cur_fr = cur_st->frame[cur_st->curframe], *queued_fr;
	struct bpf_reg_state *cur_iter, *queued_iter;

	BTF_TYPE_EMIT(struct bpf_iter);

	cur_iter = get_iter_from_state(cur_st, meta);

	if (cur_iter->iter.state != BPF_ITER_STATE_ACTIVE &&
	    cur_iter->iter.state != BPF_ITER_STATE_DRAINED) {
		verbose(env, "verifier internal error: unexpected iterator state %d (%s)\n",
			cur_iter->iter.state, iter_state_str(cur_iter->iter.state));
		return -EFAULT;
	}

	if (cur_iter->iter.state == BPF_ITER_STATE_ACTIVE) {
		/* Because iter_next() call is a checkpoint is_state_visitied()
		 * should guarantee parent state with same call sites and insn_idx.
		 */
		if (!cur_st->parent || cur_st->parent->insn_idx != insn_idx ||
		    !same_callsites(cur_st->parent, cur_st)) {
			verbose(env, "bug: bad parent state for iter next call");
			return -EFAULT;
		}
		/* Note cur_st->parent in the call below, it is necessary to skip
		 * checkpoint created for cur_st by is_state_visited()
		 * right at this instruction.
		 */
		prev_st = find_prev_entry(env, cur_st->parent, insn_idx);
		/* branch out active iter state */
		queued_st = push_stack(env, insn_idx + 1, insn_idx, false);
		if (!queued_st)
			return -ENOMEM;

		queued_iter = get_iter_from_state(queued_st, meta);
		queued_iter->iter.state = BPF_ITER_STATE_ACTIVE;
		queued_iter->iter.depth++;
		if (prev_st)
			widen_imprecise_scalars(env, prev_st, queued_st);

		queued_fr = queued_st->frame[queued_st->curframe];
		mark_ptr_not_null_reg(&queued_fr->regs[BPF_REG_0]);
	}

	/* switch to DRAINED state, but keep the depth unchanged */
	/* mark current iter state as drained and assume returned NULL */
	cur_iter->iter.state = BPF_ITER_STATE_DRAINED;
	__mark_reg_const_zero(env, &cur_fr->regs[BPF_REG_0]);

	return 0;
}

static bool arg_type_is_mem_size(enum bpf_arg_type type)
{
	return type == ARG_CONST_SIZE ||
	       type == ARG_CONST_SIZE_OR_ZERO;
}

static bool arg_type_is_raw_mem(enum bpf_arg_type type)
{
	return base_type(type) == ARG_PTR_TO_MEM &&
	       type & MEM_UNINIT;
}

static bool arg_type_is_release(enum bpf_arg_type type)
{
	return type & OBJ_RELEASE;
}

static bool arg_type_is_dynptr(enum bpf_arg_type type)
{
	return base_type(type) == ARG_PTR_TO_DYNPTR;
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
	case BPF_MAP_TYPE_BLOOM_FILTER:
		if (meta->func_id == BPF_FUNC_map_peek_elem)
			*arg_type = ARG_PTR_TO_MAP_VALUE;
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
		PTR_TO_BTF_ID | PTR_TRUSTED,
	},
	.btf_id = &btf_sock_ids[BTF_SOCK_TYPE_SOCK_COMMON],
};
#endif

static const struct bpf_reg_types mem_types = {
	.types = {
		PTR_TO_STACK,
		PTR_TO_PACKET,
		PTR_TO_PACKET_META,
		PTR_TO_MAP_KEY,
		PTR_TO_MAP_VALUE,
		PTR_TO_MEM,
		PTR_TO_MEM | MEM_RINGBUF,
		PTR_TO_BUF,
		PTR_TO_BTF_ID | PTR_TRUSTED,
	},
};

static const struct bpf_reg_types spin_lock_types = {
	.types = {
		PTR_TO_MAP_VALUE,
		PTR_TO_BTF_ID | MEM_ALLOC,
	}
};

static const struct bpf_reg_types fullsock_types = { .types = { PTR_TO_SOCKET } };
static const struct bpf_reg_types scalar_types = { .types = { SCALAR_VALUE } };
static const struct bpf_reg_types context_types = { .types = { PTR_TO_CTX } };
static const struct bpf_reg_types ringbuf_mem_types = { .types = { PTR_TO_MEM | MEM_RINGBUF } };
static const struct bpf_reg_types const_map_ptr_types = { .types = { CONST_PTR_TO_MAP } };
static const struct bpf_reg_types btf_ptr_types = {
	.types = {
		PTR_TO_BTF_ID,
		PTR_TO_BTF_ID | PTR_TRUSTED,
		PTR_TO_BTF_ID | MEM_RCU,
	},
};
static const struct bpf_reg_types percpu_btf_ptr_types = {
	.types = {
		PTR_TO_BTF_ID | MEM_PERCPU,
		PTR_TO_BTF_ID | MEM_PERCPU | MEM_RCU,
		PTR_TO_BTF_ID | MEM_PERCPU | PTR_TRUSTED,
	}
};
static const struct bpf_reg_types func_ptr_types = { .types = { PTR_TO_FUNC } };
static const struct bpf_reg_types stack_ptr_types = { .types = { PTR_TO_STACK } };
static const struct bpf_reg_types const_str_ptr_types = { .types = { PTR_TO_MAP_VALUE } };
static const struct bpf_reg_types timer_types = { .types = { PTR_TO_MAP_VALUE } };
static const struct bpf_reg_types kptr_xchg_dest_types = {
	.types = {
		PTR_TO_MAP_VALUE,
		PTR_TO_BTF_ID | MEM_ALLOC
	}
};
static const struct bpf_reg_types dynptr_types = {
	.types = {
		PTR_TO_STACK,
		CONST_PTR_TO_DYNPTR,
	}
};

static const struct bpf_reg_types *compatible_reg_types[__BPF_ARG_TYPE_MAX] = {
	[ARG_PTR_TO_MAP_KEY]		= &mem_types,
	[ARG_PTR_TO_MAP_VALUE]		= &mem_types,
	[ARG_CONST_SIZE]		= &scalar_types,
	[ARG_CONST_SIZE_OR_ZERO]	= &scalar_types,
	[ARG_CONST_ALLOC_SIZE_OR_ZERO]	= &scalar_types,
	[ARG_CONST_MAP_PTR]		= &const_map_ptr_types,
	[ARG_PTR_TO_CTX]		= &context_types,
	[ARG_PTR_TO_SOCK_COMMON]	= &sock_types,
#ifdef CONFIG_NET
	[ARG_PTR_TO_BTF_ID_SOCK_COMMON]	= &btf_id_sock_common_types,
#endif
	[ARG_PTR_TO_SOCKET]		= &fullsock_types,
	[ARG_PTR_TO_BTF_ID]		= &btf_ptr_types,
	[ARG_PTR_TO_SPIN_LOCK]		= &spin_lock_types,
	[ARG_PTR_TO_MEM]		= &mem_types,
	[ARG_PTR_TO_RINGBUF_MEM]	= &ringbuf_mem_types,
	[ARG_PTR_TO_PERCPU_BTF_ID]	= &percpu_btf_ptr_types,
	[ARG_PTR_TO_FUNC]		= &func_ptr_types,
	[ARG_PTR_TO_STACK]		= &stack_ptr_types,
	[ARG_PTR_TO_CONST_STR]		= &const_str_ptr_types,
	[ARG_PTR_TO_TIMER]		= &timer_types,
	[ARG_KPTR_XCHG_DEST]		= &kptr_xchg_dest_types,
	[ARG_PTR_TO_DYNPTR]		= &dynptr_types,
};

static int check_reg_type(struct bpf_verifier_env *env, u32 regno,
			  enum bpf_arg_type arg_type,
			  const u32 *arg_btf_id,
			  struct bpf_call_arg_meta *meta)
{
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	enum bpf_reg_type expected, type = reg->type;
	const struct bpf_reg_types *compatible;
	int i, j;

	compatible = compatible_reg_types[base_type(arg_type)];
	if (!compatible) {
		verbose(env, "verifier internal error: unsupported arg type %d\n", arg_type);
		return -EFAULT;
	}

	/* ARG_PTR_TO_MEM + RDONLY is compatible with PTR_TO_MEM and PTR_TO_MEM + RDONLY,
	 * but ARG_PTR_TO_MEM is compatible only with PTR_TO_MEM and NOT with PTR_TO_MEM + RDONLY
	 *
	 * Same for MAYBE_NULL:
	 *
	 * ARG_PTR_TO_MEM + MAYBE_NULL is compatible with PTR_TO_MEM and PTR_TO_MEM + MAYBE_NULL,
	 * but ARG_PTR_TO_MEM is compatible only with PTR_TO_MEM but NOT with PTR_TO_MEM + MAYBE_NULL
	 *
	 * ARG_PTR_TO_MEM is compatible with PTR_TO_MEM that is tagged with a dynptr type.
	 *
	 * Therefore we fold these flags depending on the arg_type before comparison.
	 */
	if (arg_type & MEM_RDONLY)
		type &= ~MEM_RDONLY;
	if (arg_type & PTR_MAYBE_NULL)
		type &= ~PTR_MAYBE_NULL;
	if (base_type(arg_type) == ARG_PTR_TO_MEM)
		type &= ~DYNPTR_TYPE_FLAG_MASK;

	/* Local kptr types are allowed as the source argument of bpf_kptr_xchg */
	if (meta->func_id == BPF_FUNC_kptr_xchg && type_is_alloc(type) && regno == BPF_REG_2) {
		type &= ~MEM_ALLOC;
		type &= ~MEM_PERCPU;
	}

	for (i = 0; i < ARRAY_SIZE(compatible->types); i++) {
		expected = compatible->types[i];
		if (expected == NOT_INIT)
			break;

		if (type == expected)
			goto found;
	}

	verbose(env, "R%d type=%s expected=", regno, reg_type_str(env, reg->type));
	for (j = 0; j + 1 < i; j++)
		verbose(env, "%s, ", reg_type_str(env, compatible->types[j]));
	verbose(env, "%s\n", reg_type_str(env, compatible->types[j]));
	return -EACCES;

found:
	if (base_type(reg->type) != PTR_TO_BTF_ID)
		return 0;

	if (compatible == &mem_types) {
		if (!(arg_type & MEM_RDONLY)) {
			verbose(env,
				"%s() may write into memory pointed by R%d type=%s\n",
				func_id_name(meta->func_id),
				regno, reg_type_str(env, reg->type));
			return -EACCES;
		}
		return 0;
	}

	switch ((int)reg->type) {
	case PTR_TO_BTF_ID:
	case PTR_TO_BTF_ID | PTR_TRUSTED:
	case PTR_TO_BTF_ID | PTR_TRUSTED | PTR_MAYBE_NULL:
	case PTR_TO_BTF_ID | MEM_RCU:
	case PTR_TO_BTF_ID | PTR_MAYBE_NULL:
	case PTR_TO_BTF_ID | PTR_MAYBE_NULL | MEM_RCU:
	{
		/* For bpf_sk_release, it needs to match against first member
		 * 'struct sock_common', hence make an exception for it. This
		 * allows bpf_sk_release to work for multiple socket types.
		 */
		bool strict_type_match = arg_type_is_release(arg_type) &&
					 meta->func_id != BPF_FUNC_sk_release;

		if (type_may_be_null(reg->type) &&
		    (!type_may_be_null(arg_type) || arg_type_is_release(arg_type))) {
			verbose(env, "Possibly NULL pointer passed to helper arg%d\n", regno);
			return -EACCES;
		}

		if (!arg_btf_id) {
			if (!compatible->btf_id) {
				verbose(env, "verifier internal error: missing arg compatible BTF ID\n");
				return -EFAULT;
			}
			arg_btf_id = compatible->btf_id;
		}

		if (meta->func_id == BPF_FUNC_kptr_xchg) {
			if (map_kptr_match_type(env, meta->kptr_field, reg, regno))
				return -EACCES;
		} else {
			if (arg_btf_id == BPF_PTR_POISON) {
				verbose(env, "verifier internal error:");
				verbose(env, "R%d has non-overwritten BPF_PTR_POISON type\n",
					regno);
				return -EACCES;
			}

			if (!btf_struct_ids_match(&env->log, reg->btf, reg->btf_id, reg->off,
						  btf_vmlinux, *arg_btf_id,
						  strict_type_match)) {
				verbose(env, "R%d is of type %s but %s is expected\n",
					regno, btf_type_name(reg->btf, reg->btf_id),
					btf_type_name(btf_vmlinux, *arg_btf_id));
				return -EACCES;
			}
		}
		break;
	}
	case PTR_TO_BTF_ID | MEM_ALLOC:
	case PTR_TO_BTF_ID | MEM_PERCPU | MEM_ALLOC:
		if (meta->func_id != BPF_FUNC_spin_lock && meta->func_id != BPF_FUNC_spin_unlock &&
		    meta->func_id != BPF_FUNC_kptr_xchg) {
			verbose(env, "verifier internal error: unimplemented handling of MEM_ALLOC\n");
			return -EFAULT;
		}
		/* Check if local kptr in src arg matches kptr in dst arg */
		if (meta->func_id == BPF_FUNC_kptr_xchg && regno == BPF_REG_2) {
			if (map_kptr_match_type(env, meta->kptr_field, reg, regno))
				return -EACCES;
		}
		break;
	case PTR_TO_BTF_ID | MEM_PERCPU:
	case PTR_TO_BTF_ID | MEM_PERCPU | MEM_RCU:
	case PTR_TO_BTF_ID | MEM_PERCPU | PTR_TRUSTED:
		/* Handled by helper specific checks */
		break;
	default:
		verbose(env, "verifier internal error: invalid PTR_TO_BTF_ID register for type match\n");
		return -EFAULT;
	}
	return 0;
}

static struct btf_field *
reg_find_field_offset(const struct bpf_reg_state *reg, s32 off, u32 fields)
{
	struct btf_field *field;
	struct btf_record *rec;

	rec = reg_btf_record(reg);
	if (!rec)
		return NULL;

	field = btf_record_find(rec, off, fields);
	if (!field)
		return NULL;

	return field;
}

static int check_func_arg_reg_off(struct bpf_verifier_env *env,
				  const struct bpf_reg_state *reg, int regno,
				  enum bpf_arg_type arg_type)
{
	u32 type = reg->type;

	/* When referenced register is passed to release function, its fixed
	 * offset must be 0.
	 *
	 * We will check arg_type_is_release reg has ref_obj_id when storing
	 * meta->release_regno.
	 */
	if (arg_type_is_release(arg_type)) {
		/* ARG_PTR_TO_DYNPTR with OBJ_RELEASE is a bit special, as it
		 * may not directly point to the object being released, but to
		 * dynptr pointing to such object, which might be at some offset
		 * on the stack. In that case, we simply to fallback to the
		 * default handling.
		 */
		if (arg_type_is_dynptr(arg_type) && type == PTR_TO_STACK)
			return 0;

		/* Doing check_ptr_off_reg check for the offset will catch this
		 * because fixed_off_ok is false, but checking here allows us
		 * to give the user a better error message.
		 */
		if (reg->off) {
			verbose(env, "R%d must have zero offset when passed to release func or trusted arg to kfunc\n",
				regno);
			return -EINVAL;
		}
		return __check_ptr_off_reg(env, reg, regno, false);
	}

	switch (type) {
	/* Pointer types where both fixed and variable offset is explicitly allowed: */
	case PTR_TO_STACK:
	case PTR_TO_PACKET:
	case PTR_TO_PACKET_META:
	case PTR_TO_MAP_KEY:
	case PTR_TO_MAP_VALUE:
	case PTR_TO_MEM:
	case PTR_TO_MEM | MEM_RDONLY:
	case PTR_TO_MEM | MEM_RINGBUF:
	case PTR_TO_BUF:
	case PTR_TO_BUF | MEM_RDONLY:
	case PTR_TO_ARENA:
	case SCALAR_VALUE:
		return 0;
	/* All the rest must be rejected, except PTR_TO_BTF_ID which allows
	 * fixed offset.
	 */
	case PTR_TO_BTF_ID:
	case PTR_TO_BTF_ID | MEM_ALLOC:
	case PTR_TO_BTF_ID | PTR_TRUSTED:
	case PTR_TO_BTF_ID | MEM_RCU:
	case PTR_TO_BTF_ID | MEM_ALLOC | NON_OWN_REF:
	case PTR_TO_BTF_ID | MEM_ALLOC | NON_OWN_REF | MEM_RCU:
		/* When referenced PTR_TO_BTF_ID is passed to release function,
		 * its fixed offset must be 0. In the other cases, fixed offset
		 * can be non-zero. This was already checked above. So pass
		 * fixed_off_ok as true to allow fixed offset for all other
		 * cases. var_off always must be 0 for PTR_TO_BTF_ID, hence we
		 * still need to do checks instead of returning.
		 */
		return __check_ptr_off_reg(env, reg, regno, true);
	default:
		return __check_ptr_off_reg(env, reg, regno, false);
	}
}

static struct bpf_reg_state *get_dynptr_arg_reg(struct bpf_verifier_env *env,
						const struct bpf_func_proto *fn,
						struct bpf_reg_state *regs)
{
	struct bpf_reg_state *state = NULL;
	int i;

	for (i = 0; i < MAX_BPF_FUNC_REG_ARGS; i++)
		if (arg_type_is_dynptr(fn->arg_type[i])) {
			if (state) {
				verbose(env, "verifier internal error: multiple dynptr args\n");
				return NULL;
			}
			state = &regs[BPF_REG_1 + i];
		}

	if (!state)
		verbose(env, "verifier internal error: no dynptr arg found\n");

	return state;
}

static int dynptr_id(struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	struct bpf_func_state *state = func(env, reg);
	int spi;

	if (reg->type == CONST_PTR_TO_DYNPTR)
		return reg->id;
	spi = dynptr_get_spi(env, reg);
	if (spi < 0)
		return spi;
	return state->stack[spi].spilled_ptr.id;
}

static int dynptr_ref_obj_id(struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	struct bpf_func_state *state = func(env, reg);
	int spi;

	if (reg->type == CONST_PTR_TO_DYNPTR)
		return reg->ref_obj_id;
	spi = dynptr_get_spi(env, reg);
	if (spi < 0)
		return spi;
	return state->stack[spi].spilled_ptr.ref_obj_id;
}

static enum bpf_dynptr_type dynptr_get_type(struct bpf_verifier_env *env,
					    struct bpf_reg_state *reg)
{
	struct bpf_func_state *state = func(env, reg);
	int spi;

	if (reg->type == CONST_PTR_TO_DYNPTR)
		return reg->dynptr.type;

	spi = __get_spi(reg->off);
	if (spi < 0) {
		verbose(env, "verifier internal error: invalid spi when querying dynptr type\n");
		return BPF_DYNPTR_TYPE_INVALID;
	}

	return state->stack[spi].spilled_ptr.dynptr.type;
}

static int check_reg_const_str(struct bpf_verifier_env *env,
			       struct bpf_reg_state *reg, u32 regno)
{
	struct bpf_map *map = reg->map_ptr;
	int err;
	int map_off;
	u64 map_addr;
	char *str_ptr;

	if (reg->type != PTR_TO_MAP_VALUE)
		return -EINVAL;

	if (!bpf_map_is_rdonly(map)) {
		verbose(env, "R%d does not point to a readonly map'\n", regno);
		return -EACCES;
	}

	if (!tnum_is_const(reg->var_off)) {
		verbose(env, "R%d is not a constant address'\n", regno);
		return -EACCES;
	}

	if (!map->ops->map_direct_value_addr) {
		verbose(env, "no direct value access support for this map type\n");
		return -EACCES;
	}

	err = check_map_access(env, regno, reg->off,
			       map->value_size - reg->off, false,
			       ACCESS_HELPER);
	if (err)
		return err;

	map_off = reg->off + reg->var_off.value;
	err = map->ops->map_direct_value_addr(map, &map_addr, map_off);
	if (err) {
		verbose(env, "direct value access on string failed\n");
		return err;
	}

	str_ptr = (char *)(long)(map_addr);
	if (!strnchr(str_ptr + map_off, map->value_size - map_off, 0)) {
		verbose(env, "string is not zero-terminated\n");
		return -EINVAL;
	}
	return 0;
}

static int check_func_arg(struct bpf_verifier_env *env, u32 arg,
			  struct bpf_call_arg_meta *meta,
			  const struct bpf_func_proto *fn,
			  int insn_idx)
{
	u32 regno = BPF_REG_1 + arg;
	struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[regno];
	enum bpf_arg_type arg_type = fn->arg_type[arg];
	enum bpf_reg_type type = reg->type;
	u32 *arg_btf_id = NULL;
	int err = 0;
	bool mask;

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

	if (base_type(arg_type) == ARG_PTR_TO_MAP_VALUE) {
		err = resolve_map_arg_type(env, meta, &arg_type);
		if (err)
			return err;
	}

	if (register_is_null(reg) && type_may_be_null(arg_type))
		/* A NULL register has a SCALAR_VALUE type, so skip
		 * type checking.
		 */
		goto skip_type_check;

	/* arg_btf_id and arg_size are in a union. */
	if (base_type(arg_type) == ARG_PTR_TO_BTF_ID ||
	    base_type(arg_type) == ARG_PTR_TO_SPIN_LOCK)
		arg_btf_id = fn->arg_btf_id[arg];

	mask = mask_raw_tp_reg(env, reg);
	err = check_reg_type(env, regno, arg_type, arg_btf_id, meta);

	err = err ?: check_func_arg_reg_off(env, reg, regno, arg_type);
	unmask_raw_tp_reg(reg, mask);
	if (err)
		return err;

skip_type_check:
	if (arg_type_is_release(arg_type)) {
		if (arg_type_is_dynptr(arg_type)) {
			struct bpf_func_state *state = func(env, reg);
			int spi;

			/* Only dynptr created on stack can be released, thus
			 * the get_spi and stack state checks for spilled_ptr
			 * should only be done before process_dynptr_func for
			 * PTR_TO_STACK.
			 */
			if (reg->type == PTR_TO_STACK) {
				spi = dynptr_get_spi(env, reg);
				if (spi < 0 || !state->stack[spi].spilled_ptr.ref_obj_id) {
					verbose(env, "arg %d is an unacquired reference\n", regno);
					return -EINVAL;
				}
			} else {
				verbose(env, "cannot release unowned const bpf_dynptr\n");
				return -EINVAL;
			}
		} else if (!reg->ref_obj_id && !register_is_null(reg)) {
			verbose(env, "R%d must be referenced when passed to release function\n",
				regno);
			return -EINVAL;
		}
		if (meta->release_regno) {
			verbose(env, "verifier internal error: more than one release argument\n");
			return -EFAULT;
		}
		meta->release_regno = regno;
	}

	if (reg->ref_obj_id && base_type(arg_type) != ARG_KPTR_XCHG_DEST) {
		if (meta->ref_obj_id) {
			verbose(env, "verifier internal error: more than one arg with ref_obj_id R%d %u %u\n",
				regno, reg->ref_obj_id,
				meta->ref_obj_id);
			return -EFAULT;
		}
		meta->ref_obj_id = reg->ref_obj_id;
	}

	switch (base_type(arg_type)) {
	case ARG_CONST_MAP_PTR:
		/* bpf_map_xxx(map_ptr) call: remember that map_ptr */
		if (meta->map_ptr) {
			/* Use map_uid (which is unique id of inner map) to reject:
			 * inner_map1 = bpf_map_lookup_elem(outer_map, key1)
			 * inner_map2 = bpf_map_lookup_elem(outer_map, key2)
			 * if (inner_map1 && inner_map2) {
			 *     timer = bpf_map_lookup_elem(inner_map1);
			 *     if (timer)
			 *         // mismatch would have been allowed
			 *         bpf_timer_init(timer, inner_map2);
			 * }
			 *
			 * Comparing map_ptr is enough to distinguish normal and outer maps.
			 */
			if (meta->map_ptr != reg->map_ptr ||
			    meta->map_uid != reg->map_uid) {
				verbose(env,
					"timer pointer in R1 map_uid=%d doesn't match map pointer in R2 map_uid=%d\n",
					meta->map_uid, reg->map_uid);
				return -EINVAL;
			}
		}
		meta->map_ptr = reg->map_ptr;
		meta->map_uid = reg->map_uid;
		break;
	case ARG_PTR_TO_MAP_KEY:
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
		err = check_helper_mem_access(env, regno, meta->map_ptr->key_size,
					      BPF_READ, false, NULL);
		break;
	case ARG_PTR_TO_MAP_VALUE:
		if (type_may_be_null(arg_type) && register_is_null(reg))
			return 0;

		/* bpf_map_xxx(..., map_ptr, ..., value) call:
		 * check [value, value + map->value_size) validity
		 */
		if (!meta->map_ptr) {
			/* kernel subsystem misconfigured verifier */
			verbose(env, "invalid map_ptr to access map->value\n");
			return -EACCES;
		}
		meta->raw_mode = arg_type & MEM_UNINIT;
		err = check_helper_mem_access(env, regno, meta->map_ptr->value_size,
					      arg_type & MEM_WRITE ? BPF_WRITE : BPF_READ,
					      false, meta);
		break;
	case ARG_PTR_TO_PERCPU_BTF_ID:
		if (!reg->btf_id) {
			verbose(env, "Helper has invalid btf_id in R%d\n", regno);
			return -EACCES;
		}
		meta->ret_btf = reg->btf;
		meta->ret_btf_id = reg->btf_id;
		break;
	case ARG_PTR_TO_SPIN_LOCK:
		if (in_rbtree_lock_required_cb(env)) {
			verbose(env, "can't spin_{lock,unlock} in rbtree cb\n");
			return -EACCES;
		}
		if (meta->func_id == BPF_FUNC_spin_lock) {
			err = process_spin_lock(env, regno, true);
			if (err)
				return err;
		} else if (meta->func_id == BPF_FUNC_spin_unlock) {
			err = process_spin_lock(env, regno, false);
			if (err)
				return err;
		} else {
			verbose(env, "verifier internal error\n");
			return -EFAULT;
		}
		break;
	case ARG_PTR_TO_TIMER:
		err = process_timer_func(env, regno, meta);
		if (err)
			return err;
		break;
	case ARG_PTR_TO_FUNC:
		meta->subprogno = reg->subprogno;
		break;
	case ARG_PTR_TO_MEM:
		/* The access to this pointer is only checked when we hit the
		 * next is_mem_size argument below.
		 */
		meta->raw_mode = arg_type & MEM_UNINIT;
		if (arg_type & MEM_FIXED_SIZE) {
			err = check_helper_mem_access(env, regno, fn->arg_size[arg],
						      arg_type & MEM_WRITE ? BPF_WRITE : BPF_READ,
						      false, meta);
			if (err)
				return err;
			if (arg_type & MEM_ALIGNED)
				err = check_ptr_alignment(env, reg, 0, fn->arg_size[arg], true);
		}
		break;
	case ARG_CONST_SIZE:
		err = check_mem_size_reg(env, reg, regno,
					 fn->arg_type[arg - 1] & MEM_WRITE ?
					 BPF_WRITE : BPF_READ,
					 false, meta);
		break;
	case ARG_CONST_SIZE_OR_ZERO:
		err = check_mem_size_reg(env, reg, regno,
					 fn->arg_type[arg - 1] & MEM_WRITE ?
					 BPF_WRITE : BPF_READ,
					 true, meta);
		break;
	case ARG_PTR_TO_DYNPTR:
		err = process_dynptr_func(env, regno, insn_idx, arg_type, 0);
		if (err)
			return err;
		break;
	case ARG_CONST_ALLOC_SIZE_OR_ZERO:
		if (!tnum_is_const(reg->var_off)) {
			verbose(env, "R%d is not a known constant'\n",
				regno);
			return -EACCES;
		}
		meta->mem_size = reg->var_off.value;
		err = mark_chain_precision(env, regno);
		if (err)
			return err;
		break;
	case ARG_PTR_TO_CONST_STR:
	{
		err = check_reg_const_str(env, reg, regno);
		if (err)
			return err;
		break;
	}
	case ARG_KPTR_XCHG_DEST:
		err = process_kptr_func(env, regno, meta);
		if (err)
			return err;
		break;
	}

	return err;
}

static bool may_update_sockmap(struct bpf_verifier_env *env, int func_id)
{
	enum bpf_attach_type eatype = env->prog->expected_attach_type;
	enum bpf_prog_type type = resolve_prog_type(env->prog);

	if (func_id != BPF_FUNC_map_update_elem &&
	    func_id != BPF_FUNC_map_delete_elem)
		return false;

	/* It's not possible to get access to a locked struct sock in these
	 * contexts, so updating is safe.
	 */
	switch (type) {
	case BPF_PROG_TYPE_TRACING:
		if (eatype == BPF_TRACE_ITER)
			return true;
		break;
	case BPF_PROG_TYPE_SOCK_OPS:
		/* map_update allowed only via dedicated helpers with event type checks */
		if (func_id == BPF_FUNC_map_delete_elem)
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
	return env->prog->jit_requested &&
	       bpf_jit_supports_subprog_tailcalls();
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
		    func_id != BPF_FUNC_ringbuf_query &&
		    func_id != BPF_FUNC_ringbuf_reserve_dynptr &&
		    func_id != BPF_FUNC_ringbuf_submit_dynptr &&
		    func_id != BPF_FUNC_ringbuf_discard_dynptr)
			goto error;
		break;
	case BPF_MAP_TYPE_USER_RINGBUF:
		if (func_id != BPF_FUNC_user_ringbuf_drain)
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
		    func_id != BPF_FUNC_msg_redirect_map &&
		    func_id != BPF_FUNC_sk_select_reuseport &&
		    func_id != BPF_FUNC_map_lookup_elem &&
		    !may_update_sockmap(env, func_id))
			goto error;
		break;
	case BPF_MAP_TYPE_SOCKHASH:
		if (func_id != BPF_FUNC_sk_redirect_hash &&
		    func_id != BPF_FUNC_sock_hash_update &&
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
		    func_id != BPF_FUNC_sk_storage_delete &&
		    func_id != BPF_FUNC_kptr_xchg)
			goto error;
		break;
	case BPF_MAP_TYPE_INODE_STORAGE:
		if (func_id != BPF_FUNC_inode_storage_get &&
		    func_id != BPF_FUNC_inode_storage_delete &&
		    func_id != BPF_FUNC_kptr_xchg)
			goto error;
		break;
	case BPF_MAP_TYPE_TASK_STORAGE:
		if (func_id != BPF_FUNC_task_storage_get &&
		    func_id != BPF_FUNC_task_storage_delete &&
		    func_id != BPF_FUNC_kptr_xchg)
			goto error;
		break;
	case BPF_MAP_TYPE_CGRP_STORAGE:
		if (func_id != BPF_FUNC_cgrp_storage_get &&
		    func_id != BPF_FUNC_cgrp_storage_delete &&
		    func_id != BPF_FUNC_kptr_xchg)
			goto error;
		break;
	case BPF_MAP_TYPE_BLOOM_FILTER:
		if (func_id != BPF_FUNC_map_peek_elem &&
		    func_id != BPF_FUNC_map_push_elem)
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
	case BPF_FUNC_ringbuf_output:
	case BPF_FUNC_ringbuf_reserve:
	case BPF_FUNC_ringbuf_query:
	case BPF_FUNC_ringbuf_reserve_dynptr:
	case BPF_FUNC_ringbuf_submit_dynptr:
	case BPF_FUNC_ringbuf_discard_dynptr:
		if (map->map_type != BPF_MAP_TYPE_RINGBUF)
			goto error;
		break;
	case BPF_FUNC_user_ringbuf_drain:
		if (map->map_type != BPF_MAP_TYPE_USER_RINGBUF)
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
	case BPF_FUNC_map_pop_elem:
		if (map->map_type != BPF_MAP_TYPE_QUEUE &&
		    map->map_type != BPF_MAP_TYPE_STACK)
			goto error;
		break;
	case BPF_FUNC_map_peek_elem:
	case BPF_FUNC_map_push_elem:
		if (map->map_type != BPF_MAP_TYPE_QUEUE &&
		    map->map_type != BPF_MAP_TYPE_STACK &&
		    map->map_type != BPF_MAP_TYPE_BLOOM_FILTER)
			goto error;
		break;
	case BPF_FUNC_map_lookup_percpu_elem:
		if (map->map_type != BPF_MAP_TYPE_PERCPU_ARRAY &&
		    map->map_type != BPF_MAP_TYPE_PERCPU_HASH &&
		    map->map_type != BPF_MAP_TYPE_LRU_PERCPU_HASH)
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
	case BPF_FUNC_cgrp_storage_get:
	case BPF_FUNC_cgrp_storage_delete:
		if (map->map_type != BPF_MAP_TYPE_CGRP_STORAGE)
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

	if (arg_type_is_raw_mem(fn->arg1_type))
		count++;
	if (arg_type_is_raw_mem(fn->arg2_type))
		count++;
	if (arg_type_is_raw_mem(fn->arg3_type))
		count++;
	if (arg_type_is_raw_mem(fn->arg4_type))
		count++;
	if (arg_type_is_raw_mem(fn->arg5_type))
		count++;

	/* We only support one arg being in raw mode at the moment,
	 * which is sufficient for the helper functions we have
	 * right now.
	 */
	return count <= 1;
}

static bool check_args_pair_invalid(const struct bpf_func_proto *fn, int arg)
{
	bool is_fixed = fn->arg_type[arg] & MEM_FIXED_SIZE;
	bool has_size = fn->arg_size[arg] != 0;
	bool is_next_size = false;

	if (arg + 1 < ARRAY_SIZE(fn->arg_type))
		is_next_size = arg_type_is_mem_size(fn->arg_type[arg + 1]);

	if (base_type(fn->arg_type[arg]) != ARG_PTR_TO_MEM)
		return is_next_size;

	return has_size == is_next_size || is_next_size == is_fixed;
}

static bool check_arg_pair_ok(const struct bpf_func_proto *fn)
{
	/* bpf_xxx(..., buf, len) call will access 'len'
	 * bytes from memory 'buf'. Both arg types need
	 * to be paired, so make sure there's no buggy
	 * helper function specification.
	 */
	if (arg_type_is_mem_size(fn->arg1_type) ||
	    check_args_pair_invalid(fn, 0) ||
	    check_args_pair_invalid(fn, 1) ||
	    check_args_pair_invalid(fn, 2) ||
	    check_args_pair_invalid(fn, 3) ||
	    check_args_pair_invalid(fn, 4))
		return false;

	return true;
}

static bool check_btf_id_ok(const struct bpf_func_proto *fn)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fn->arg_type); i++) {
		if (base_type(fn->arg_type[i]) == ARG_PTR_TO_BTF_ID)
			return !!fn->arg_btf_id[i];
		if (base_type(fn->arg_type[i]) == ARG_PTR_TO_SPIN_LOCK)
			return fn->arg_btf_id[i] == BPF_PTR_POISON;
		if (base_type(fn->arg_type[i]) != ARG_PTR_TO_BTF_ID && fn->arg_btf_id[i] &&
		    /* arg_btf_id and arg_size are in a union. */
		    (base_type(fn->arg_type[i]) != ARG_PTR_TO_MEM ||
		     !(fn->arg_type[i] & MEM_FIXED_SIZE)))
			return false;
	}

	return true;
}

static int check_func_proto(const struct bpf_func_proto *fn, int func_id)
{
	return check_raw_mode_ok(fn) &&
	       check_arg_pair_ok(fn) &&
	       check_btf_id_ok(fn) ? 0 : -EINVAL;
}

/* Packet data might have moved, any old PTR_TO_PACKET[_META,_END]
 * are now invalid, so turn them into unknown SCALAR_VALUE.
 *
 * This also applies to dynptr slices belonging to skb and xdp dynptrs,
 * since these slices point to packet data.
 */
static void clear_all_pkt_pointers(struct bpf_verifier_env *env)
{
	struct bpf_func_state *state;
	struct bpf_reg_state *reg;

	bpf_for_each_reg_in_vstate(env->cur_state, state, reg, ({
		if (reg_is_pkt_pointer_any(reg) || reg_is_dynptr_slice_pkt(reg))
			mark_reg_invalid(env, reg);
	}));
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

/* The pointer with the specified id has released its reference to kernel
 * resources. Identify all copies of the same pointer and clear the reference.
 */
static int release_reference(struct bpf_verifier_env *env,
			     int ref_obj_id)
{
	struct bpf_func_state *state;
	struct bpf_reg_state *reg;
	int err;

	err = release_reference_state(cur_func(env), ref_obj_id);
	if (err)
		return err;

	bpf_for_each_reg_in_vstate(env->cur_state, state, reg, ({
		if (reg->ref_obj_id == ref_obj_id)
			mark_reg_invalid(env, reg);
	}));

	return 0;
}

static void invalidate_non_owning_refs(struct bpf_verifier_env *env)
{
	struct bpf_func_state *unused;
	struct bpf_reg_state *reg;

	bpf_for_each_reg_in_vstate(env->cur_state, unused, reg, ({
		if (type_is_non_owning_ref(reg->type))
			mark_reg_invalid(env, reg);
	}));
}

static void clear_caller_saved_regs(struct bpf_verifier_env *env,
				    struct bpf_reg_state *regs)
{
	int i;

	/* after the call registers r0 - r5 were scratched */
	for (i = 0; i < CALLER_SAVED_REGS; i++) {
		mark_reg_not_init(env, regs, caller_saved[i]);
		__check_reg_arg(env, regs, caller_saved[i], DST_OP_NO_MARK);
	}
}

typedef int (*set_callee_state_fn)(struct bpf_verifier_env *env,
				   struct bpf_func_state *caller,
				   struct bpf_func_state *callee,
				   int insn_idx);

static int set_callee_state(struct bpf_verifier_env *env,
			    struct bpf_func_state *caller,
			    struct bpf_func_state *callee, int insn_idx);

static int setup_func_entry(struct bpf_verifier_env *env, int subprog, int callsite,
			    set_callee_state_fn set_callee_state_cb,
			    struct bpf_verifier_state *state)
{
	struct bpf_func_state *caller, *callee;
	int err;

	if (state->curframe + 1 >= MAX_CALL_FRAMES) {
		verbose(env, "the call stack of %d frames is too deep\n",
			state->curframe + 2);
		return -E2BIG;
	}

	if (state->frame[state->curframe + 1]) {
		verbose(env, "verifier bug. Frame %d already allocated\n",
			state->curframe + 1);
		return -EFAULT;
	}

	caller = state->frame[state->curframe];
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
			callsite,
			state->curframe + 1 /* frameno within this callchain */,
			subprog /* subprog number within this prog */);
	/* Transfer references to the callee */
	err = copy_reference_state(callee, caller);
	err = err ?: set_callee_state_cb(env, caller, callee, callsite);
	if (err)
		goto err_out;

	/* only increment it after check_reg_arg() finished */
	state->curframe++;

	return 0;

err_out:
	free_func_state(callee);
	state->frame[state->curframe + 1] = NULL;
	return err;
}

static int btf_check_func_arg_match(struct bpf_verifier_env *env, int subprog,
				    const struct btf *btf,
				    struct bpf_reg_state *regs)
{
	struct bpf_subprog_info *sub = subprog_info(env, subprog);
	struct bpf_verifier_log *log = &env->log;
	u32 i;
	int ret;

	ret = btf_prepare_func_args(env, subprog);
	if (ret)
		return ret;

	/* check that BTF function arguments match actual types that the
	 * verifier sees.
	 */
	for (i = 0; i < sub->arg_cnt; i++) {
		u32 regno = i + 1;
		struct bpf_reg_state *reg = &regs[regno];
		struct bpf_subprog_arg_info *arg = &sub->args[i];

		if (arg->arg_type == ARG_ANYTHING) {
			if (reg->type != SCALAR_VALUE) {
				bpf_log(log, "R%d is not a scalar\n", regno);
				return -EINVAL;
			}
		} else if (arg->arg_type == ARG_PTR_TO_CTX) {
			ret = check_func_arg_reg_off(env, reg, regno, ARG_DONTCARE);
			if (ret < 0)
				return ret;
			/* If function expects ctx type in BTF check that caller
			 * is passing PTR_TO_CTX.
			 */
			if (reg->type != PTR_TO_CTX) {
				bpf_log(log, "arg#%d expects pointer to ctx\n", i);
				return -EINVAL;
			}
		} else if (base_type(arg->arg_type) == ARG_PTR_TO_MEM) {
			ret = check_func_arg_reg_off(env, reg, regno, ARG_DONTCARE);
			if (ret < 0)
				return ret;
			if (check_mem_reg(env, reg, regno, arg->mem_size))
				return -EINVAL;
			if (!(arg->arg_type & PTR_MAYBE_NULL) && (reg->type & PTR_MAYBE_NULL)) {
				bpf_log(log, "arg#%d is expected to be non-NULL\n", i);
				return -EINVAL;
			}
		} else if (base_type(arg->arg_type) == ARG_PTR_TO_ARENA) {
			/*
			 * Can pass any value and the kernel won't crash, but
			 * only PTR_TO_ARENA or SCALAR make sense. Everything
			 * else is a bug in the bpf program. Point it out to
			 * the user at the verification time instead of
			 * run-time debug nightmare.
			 */
			if (reg->type != PTR_TO_ARENA && reg->type != SCALAR_VALUE) {
				bpf_log(log, "R%d is not a pointer to arena or scalar.\n", regno);
				return -EINVAL;
			}
		} else if (arg->arg_type == (ARG_PTR_TO_DYNPTR | MEM_RDONLY)) {
			ret = check_func_arg_reg_off(env, reg, regno, ARG_PTR_TO_DYNPTR);
			if (ret)
				return ret;

			ret = process_dynptr_func(env, regno, -1, arg->arg_type, 0);
			if (ret)
				return ret;
		} else if (base_type(arg->arg_type) == ARG_PTR_TO_BTF_ID) {
			struct bpf_call_arg_meta meta;
			bool mask;
			int err;

			if (register_is_null(reg) && type_may_be_null(arg->arg_type))
				continue;

			memset(&meta, 0, sizeof(meta)); /* leave func_id as zero */
			mask = mask_raw_tp_reg(env, reg);
			err = check_reg_type(env, regno, arg->arg_type, &arg->btf_id, &meta);
			err = err ?: check_func_arg_reg_off(env, reg, regno, arg->arg_type);
			unmask_raw_tp_reg(reg, mask);
			if (err)
				return err;
		} else {
			bpf_log(log, "verifier bug: unrecognized arg#%d type %d\n",
				i, arg->arg_type);
			return -EFAULT;
		}
	}

	return 0;
}

/* Compare BTF of a function call with given bpf_reg_state.
 * Returns:
 * EFAULT - there is a verifier bug. Abort verification.
 * EINVAL - there is a type mismatch or BTF is not available.
 * 0 - BTF matches with what bpf_reg_state expects.
 * Only PTR_TO_CTX and SCALAR_VALUE states are recognized.
 */
static int btf_check_subprog_call(struct bpf_verifier_env *env, int subprog,
				  struct bpf_reg_state *regs)
{
	struct bpf_prog *prog = env->prog;
	struct btf *btf = prog->aux->btf;
	u32 btf_id;
	int err;

	if (!prog->aux->func_info)
		return -EINVAL;

	btf_id = prog->aux->func_info[subprog].type_id;
	if (!btf_id)
		return -EFAULT;

	if (prog->aux->func_info_aux[subprog].unreliable)
		return -EINVAL;

	err = btf_check_func_arg_match(env, subprog, btf, regs);
	/* Compiler optimizations can remove arguments from static functions
	 * or mismatched type can be passed into a global function.
	 * In such cases mark the function as unreliable from BTF point of view.
	 */
	if (err)
		prog->aux->func_info_aux[subprog].unreliable = true;
	return err;
}

static int push_callback_call(struct bpf_verifier_env *env, struct bpf_insn *insn,
			      int insn_idx, int subprog,
			      set_callee_state_fn set_callee_state_cb)
{
	struct bpf_verifier_state *state = env->cur_state, *callback_state;
	struct bpf_func_state *caller, *callee;
	int err;

	caller = state->frame[state->curframe];
	err = btf_check_subprog_call(env, subprog, caller->regs);
	if (err == -EFAULT)
		return err;

	/* set_callee_state is used for direct subprog calls, but we are
	 * interested in validating only BPF helpers that can call subprogs as
	 * callbacks
	 */
	env->subprog_info[subprog].is_cb = true;
	if (bpf_pseudo_kfunc_call(insn) &&
	    !is_callback_calling_kfunc(insn->imm)) {
		verbose(env, "verifier bug: kfunc %s#%d not marked as callback-calling\n",
			func_id_name(insn->imm), insn->imm);
		return -EFAULT;
	} else if (!bpf_pseudo_kfunc_call(insn) &&
		   !is_callback_calling_function(insn->imm)) { /* helper */
		verbose(env, "verifier bug: helper %s#%d not marked as callback-calling\n",
			func_id_name(insn->imm), insn->imm);
		return -EFAULT;
	}

	if (is_async_callback_calling_insn(insn)) {
		struct bpf_verifier_state *async_cb;

		/* there is no real recursion here. timer and workqueue callbacks are async */
		env->subprog_info[subprog].is_async_cb = true;
		async_cb = push_async_cb(env, env->subprog_info[subprog].start,
					 insn_idx, subprog,
					 is_bpf_wq_set_callback_impl_kfunc(insn->imm));
		if (!async_cb)
			return -EFAULT;
		callee = async_cb->frame[0];
		callee->async_entry_cnt = caller->async_entry_cnt + 1;

		/* Convert bpf_timer_set_callback() args into timer callback args */
		err = set_callee_state_cb(env, caller, callee, insn_idx);
		if (err)
			return err;

		return 0;
	}

	/* for callback functions enqueue entry to callback and
	 * proceed with next instruction within current frame.
	 */
	callback_state = push_stack(env, env->subprog_info[subprog].start, insn_idx, false);
	if (!callback_state)
		return -ENOMEM;

	err = setup_func_entry(env, subprog, insn_idx, set_callee_state_cb,
			       callback_state);
	if (err)
		return err;

	callback_state->callback_unroll_depth++;
	callback_state->frame[callback_state->curframe - 1]->callback_depth++;
	caller->callback_depth = 0;
	return 0;
}

static int check_func_call(struct bpf_verifier_env *env, struct bpf_insn *insn,
			   int *insn_idx)
{
	struct bpf_verifier_state *state = env->cur_state;
	struct bpf_func_state *caller;
	int err, subprog, target_insn;

	target_insn = *insn_idx + insn->imm + 1;
	subprog = find_subprog(env, target_insn);
	if (subprog < 0) {
		verbose(env, "verifier bug. No program starts at insn %d\n", target_insn);
		return -EFAULT;
	}

	caller = state->frame[state->curframe];
	err = btf_check_subprog_call(env, subprog, caller->regs);
	if (err == -EFAULT)
		return err;
	if (subprog_is_global(env, subprog)) {
		const char *sub_name = subprog_name(env, subprog);

		/* Only global subprogs cannot be called with a lock held. */
		if (cur_func(env)->active_locks) {
			verbose(env, "global function calls are not allowed while holding a lock,\n"
				     "use static function instead\n");
			return -EINVAL;
		}

		/* Only global subprogs cannot be called with preemption disabled. */
		if (env->cur_state->active_preempt_lock) {
			verbose(env, "global function calls are not allowed with preemption disabled,\n"
				     "use static function instead\n");
			return -EINVAL;
		}

		if (err) {
			verbose(env, "Caller passes invalid args into func#%d ('%s')\n",
				subprog, sub_name);
			return err;
		}

		verbose(env, "Func#%d ('%s') is global and assumed valid.\n",
			subprog, sub_name);
		/* mark global subprog for verifying after main prog */
		subprog_aux(env, subprog)->called = true;
		clear_caller_saved_regs(env, caller->regs);

		/* All global functions return a 64-bit SCALAR_VALUE */
		mark_reg_unknown(env, caller->regs, BPF_REG_0);
		caller->regs[BPF_REG_0].subreg_def = DEF_NOT_SUBREG;

		/* continue with next insn after call */
		return 0;
	}

	/* for regular function entry setup new frame and continue
	 * from that frame.
	 */
	err = setup_func_entry(env, subprog, *insn_idx, set_callee_state, state);
	if (err)
		return err;

	clear_caller_saved_regs(env, caller->regs);

	/* and go analyze first insn of the callee */
	*insn_idx = env->subprog_info[subprog].start - 1;

	if (env->log.level & BPF_LOG_LEVEL) {
		verbose(env, "caller:\n");
		print_verifier_state(env, caller, true);
		verbose(env, "callee:\n");
		print_verifier_state(env, state->frame[state->curframe], true);
	}

	return 0;
}

int map_set_for_each_callback_args(struct bpf_verifier_env *env,
				   struct bpf_func_state *caller,
				   struct bpf_func_state *callee)
{
	/* bpf_for_each_map_elem(struct bpf_map *map, void *callback_fn,
	 *      void *callback_ctx, u64 flags);
	 * callback_fn(struct bpf_map *map, void *key, void *value,
	 *      void *callback_ctx);
	 */
	callee->regs[BPF_REG_1] = caller->regs[BPF_REG_1];

	callee->regs[BPF_REG_2].type = PTR_TO_MAP_KEY;
	__mark_reg_known_zero(&callee->regs[BPF_REG_2]);
	callee->regs[BPF_REG_2].map_ptr = caller->regs[BPF_REG_1].map_ptr;

	callee->regs[BPF_REG_3].type = PTR_TO_MAP_VALUE;
	__mark_reg_known_zero(&callee->regs[BPF_REG_3]);
	callee->regs[BPF_REG_3].map_ptr = caller->regs[BPF_REG_1].map_ptr;

	/* pointer to stack or null */
	callee->regs[BPF_REG_4] = caller->regs[BPF_REG_3];

	/* unused */
	__mark_reg_not_init(env, &callee->regs[BPF_REG_5]);
	return 0;
}

static int set_callee_state(struct bpf_verifier_env *env,
			    struct bpf_func_state *caller,
			    struct bpf_func_state *callee, int insn_idx)
{
	int i;

	/* copy r1 - r5 args that callee can access.  The copy includes parent
	 * pointers, which connects us up to the liveness chain
	 */
	for (i = BPF_REG_1; i <= BPF_REG_5; i++)
		callee->regs[i] = caller->regs[i];
	return 0;
}

static int set_map_elem_callback_state(struct bpf_verifier_env *env,
				       struct bpf_func_state *caller,
				       struct bpf_func_state *callee,
				       int insn_idx)
{
	struct bpf_insn_aux_data *insn_aux = &env->insn_aux_data[insn_idx];
	struct bpf_map *map;
	int err;

	/* valid map_ptr and poison value does not matter */
	map = insn_aux->map_ptr_state.map_ptr;
	if (!map->ops->map_set_for_each_callback_args ||
	    !map->ops->map_for_each_callback) {
		verbose(env, "callback function not allowed for map\n");
		return -ENOTSUPP;
	}

	err = map->ops->map_set_for_each_callback_args(env, caller, callee);
	if (err)
		return err;

	callee->in_callback_fn = true;
	callee->callback_ret_range = retval_range(0, 1);
	return 0;
}

static int set_loop_callback_state(struct bpf_verifier_env *env,
				   struct bpf_func_state *caller,
				   struct bpf_func_state *callee,
				   int insn_idx)
{
	/* bpf_loop(u32 nr_loops, void *callback_fn, void *callback_ctx,
	 *	    u64 flags);
	 * callback_fn(u64 index, void *callback_ctx);
	 */
	callee->regs[BPF_REG_1].type = SCALAR_VALUE;
	callee->regs[BPF_REG_2] = caller->regs[BPF_REG_3];

	/* unused */
	__mark_reg_not_init(env, &callee->regs[BPF_REG_3]);
	__mark_reg_not_init(env, &callee->regs[BPF_REG_4]);
	__mark_reg_not_init(env, &callee->regs[BPF_REG_5]);

	callee->in_callback_fn = true;
	callee->callback_ret_range = retval_range(0, 1);
	return 0;
}

static int set_timer_callback_state(struct bpf_verifier_env *env,
				    struct bpf_func_state *caller,
				    struct bpf_func_state *callee,
				    int insn_idx)
{
	struct bpf_map *map_ptr = caller->regs[BPF_REG_1].map_ptr;

	/* bpf_timer_set_callback(struct bpf_timer *timer, void *callback_fn);
	 * callback_fn(struct bpf_map *map, void *key, void *value);
	 */
	callee->regs[BPF_REG_1].type = CONST_PTR_TO_MAP;
	__mark_reg_known_zero(&callee->regs[BPF_REG_1]);
	callee->regs[BPF_REG_1].map_ptr = map_ptr;

	callee->regs[BPF_REG_2].type = PTR_TO_MAP_KEY;
	__mark_reg_known_zero(&callee->regs[BPF_REG_2]);
	callee->regs[BPF_REG_2].map_ptr = map_ptr;

	callee->regs[BPF_REG_3].type = PTR_TO_MAP_VALUE;
	__mark_reg_known_zero(&callee->regs[BPF_REG_3]);
	callee->regs[BPF_REG_3].map_ptr = map_ptr;

	/* unused */
	__mark_reg_not_init(env, &callee->regs[BPF_REG_4]);
	__mark_reg_not_init(env, &callee->regs[BPF_REG_5]);
	callee->in_async_callback_fn = true;
	callee->callback_ret_range = retval_range(0, 1);
	return 0;
}

static int set_find_vma_callback_state(struct bpf_verifier_env *env,
				       struct bpf_func_state *caller,
				       struct bpf_func_state *callee,
				       int insn_idx)
{
	/* bpf_find_vma(struct task_struct *task, u64 addr,
	 *               void *callback_fn, void *callback_ctx, u64 flags)
	 * (callback_fn)(struct task_struct *task,
	 *               struct vm_area_struct *vma, void *callback_ctx);
	 */
	callee->regs[BPF_REG_1] = caller->regs[BPF_REG_1];

	callee->regs[BPF_REG_2].type = PTR_TO_BTF_ID;
	__mark_reg_known_zero(&callee->regs[BPF_REG_2]);
	callee->regs[BPF_REG_2].btf =  btf_vmlinux;
	callee->regs[BPF_REG_2].btf_id = btf_tracing_ids[BTF_TRACING_TYPE_VMA];

	/* pointer to stack or null */
	callee->regs[BPF_REG_3] = caller->regs[BPF_REG_4];

	/* unused */
	__mark_reg_not_init(env, &callee->regs[BPF_REG_4]);
	__mark_reg_not_init(env, &callee->regs[BPF_REG_5]);
	callee->in_callback_fn = true;
	callee->callback_ret_range = retval_range(0, 1);
	return 0;
}

static int set_user_ringbuf_callback_state(struct bpf_verifier_env *env,
					   struct bpf_func_state *caller,
					   struct bpf_func_state *callee,
					   int insn_idx)
{
	/* bpf_user_ringbuf_drain(struct bpf_map *map, void *callback_fn, void
	 *			  callback_ctx, u64 flags);
	 * callback_fn(const struct bpf_dynptr_t* dynptr, void *callback_ctx);
	 */
	__mark_reg_not_init(env, &callee->regs[BPF_REG_0]);
	mark_dynptr_cb_reg(env, &callee->regs[BPF_REG_1], BPF_DYNPTR_TYPE_LOCAL);
	callee->regs[BPF_REG_2] = caller->regs[BPF_REG_3];

	/* unused */
	__mark_reg_not_init(env, &callee->regs[BPF_REG_3]);
	__mark_reg_not_init(env, &callee->regs[BPF_REG_4]);
	__mark_reg_not_init(env, &callee->regs[BPF_REG_5]);

	callee->in_callback_fn = true;
	callee->callback_ret_range = retval_range(0, 1);
	return 0;
}

static int set_rbtree_add_callback_state(struct bpf_verifier_env *env,
					 struct bpf_func_state *caller,
					 struct bpf_func_state *callee,
					 int insn_idx)
{
	/* void bpf_rbtree_add_impl(struct bpf_rb_root *root, struct bpf_rb_node *node,
	 *                     bool (less)(struct bpf_rb_node *a, const struct bpf_rb_node *b));
	 *
	 * 'struct bpf_rb_node *node' arg to bpf_rbtree_add_impl is the same PTR_TO_BTF_ID w/ offset
	 * that 'less' callback args will be receiving. However, 'node' arg was release_reference'd
	 * by this point, so look at 'root'
	 */
	struct btf_field *field;

	field = reg_find_field_offset(&caller->regs[BPF_REG_1], caller->regs[BPF_REG_1].off,
				      BPF_RB_ROOT);
	if (!field || !field->graph_root.value_btf_id)
		return -EFAULT;

	mark_reg_graph_node(callee->regs, BPF_REG_1, &field->graph_root);
	ref_set_non_owning(env, &callee->regs[BPF_REG_1]);
	mark_reg_graph_node(callee->regs, BPF_REG_2, &field->graph_root);
	ref_set_non_owning(env, &callee->regs[BPF_REG_2]);

	__mark_reg_not_init(env, &callee->regs[BPF_REG_3]);
	__mark_reg_not_init(env, &callee->regs[BPF_REG_4]);
	__mark_reg_not_init(env, &callee->regs[BPF_REG_5]);
	callee->in_callback_fn = true;
	callee->callback_ret_range = retval_range(0, 1);
	return 0;
}

static bool is_rbtree_lock_required_kfunc(u32 btf_id);

/* Are we currently verifying the callback for a rbtree helper that must
 * be called with lock held? If so, no need to complain about unreleased
 * lock
 */
static bool in_rbtree_lock_required_cb(struct bpf_verifier_env *env)
{
	struct bpf_verifier_state *state = env->cur_state;
	struct bpf_insn *insn = env->prog->insnsi;
	struct bpf_func_state *callee;
	int kfunc_btf_id;

	if (!state->curframe)
		return false;

	callee = state->frame[state->curframe];

	if (!callee->in_callback_fn)
		return false;

	kfunc_btf_id = insn[callee->callsite].imm;
	return is_rbtree_lock_required_kfunc(kfunc_btf_id);
}

static bool retval_range_within(struct bpf_retval_range range, const struct bpf_reg_state *reg,
				bool return_32bit)
{
	if (return_32bit)
		return range.minval <= reg->s32_min_value && reg->s32_max_value <= range.maxval;
	else
		return range.minval <= reg->smin_value && reg->smax_value <= range.maxval;
}

static int prepare_func_exit(struct bpf_verifier_env *env, int *insn_idx)
{
	struct bpf_verifier_state *state = env->cur_state, *prev_st;
	struct bpf_func_state *caller, *callee;
	struct bpf_reg_state *r0;
	bool in_callback_fn;
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

	caller = state->frame[state->curframe - 1];
	if (callee->in_callback_fn) {
		if (r0->type != SCALAR_VALUE) {
			verbose(env, "R0 not a scalar value\n");
			return -EACCES;
		}

		/* we are going to rely on register's precise value */
		err = mark_reg_read(env, r0, r0->parent, REG_LIVE_READ64);
		err = err ?: mark_chain_precision(env, BPF_REG_0);
		if (err)
			return err;

		/* enforce R0 return value range, and bpf_callback_t returns 64bit */
		if (!retval_range_within(callee->callback_ret_range, r0, false)) {
			verbose_invalid_scalar(env, r0, callee->callback_ret_range,
					       "At callback return", "R0");
			return -EINVAL;
		}
		if (!calls_callback(env, callee->callsite)) {
			verbose(env, "BUG: in callback at %d, callsite %d !calls_callback\n",
				*insn_idx, callee->callsite);
			return -EFAULT;
		}
	} else {
		/* return to the caller whatever r0 had in the callee */
		caller->regs[BPF_REG_0] = *r0;
	}

	/* Transfer references to the caller */
	err = copy_reference_state(caller, callee);
	if (err)
		return err;

	/* for callbacks like bpf_loop or bpf_for_each_map_elem go back to callsite,
	 * there function call logic would reschedule callback visit. If iteration
	 * converges is_state_visited() would prune that visit eventually.
	 */
	in_callback_fn = callee->in_callback_fn;
	if (in_callback_fn)
		*insn_idx = callee->callsite;
	else
		*insn_idx = callee->callsite + 1;

	if (env->log.level & BPF_LOG_LEVEL) {
		verbose(env, "returning from callee:\n");
		print_verifier_state(env, callee, true);
		verbose(env, "to caller at %d:\n", *insn_idx);
		print_verifier_state(env, caller, true);
	}
	/* clear everything in the callee. In case of exceptional exits using
	 * bpf_throw, this will be done by copy_verifier_state for extra frames. */
	free_func_state(callee);
	state->frame[state->curframe--] = NULL;

	/* for callbacks widen imprecise scalars to make programs like below verify:
	 *
	 *   struct ctx { int i; }
	 *   void cb(int idx, struct ctx *ctx) { ctx->i++; ... }
	 *   ...
	 *   struct ctx = { .i = 0; }
	 *   bpf_loop(100, cb, &ctx, 0);
	 *
	 * This is similar to what is done in process_iter_next_call() for open
	 * coded iterators.
	 */
	prev_st = in_callback_fn ? find_prev_entry(env, state, *insn_idx) : NULL;
	if (prev_st) {
		err = widen_imprecise_scalars(env, prev_st, state);
		if (err)
			return err;
	}
	return 0;
}

static int do_refine_retval_range(struct bpf_verifier_env *env,
				  struct bpf_reg_state *regs, int ret_type,
				  int func_id,
				  struct bpf_call_arg_meta *meta)
{
	struct bpf_reg_state *ret_reg = &regs[BPF_REG_0];

	if (ret_type != RET_INTEGER)
		return 0;

	switch (func_id) {
	case BPF_FUNC_get_stack:
	case BPF_FUNC_get_task_stack:
	case BPF_FUNC_probe_read_str:
	case BPF_FUNC_probe_read_kernel_str:
	case BPF_FUNC_probe_read_user_str:
		ret_reg->smax_value = meta->msize_max_value;
		ret_reg->s32_max_value = meta->msize_max_value;
		ret_reg->smin_value = -MAX_ERRNO;
		ret_reg->s32_min_value = -MAX_ERRNO;
		reg_bounds_sync(ret_reg);
		break;
	case BPF_FUNC_get_smp_processor_id:
		ret_reg->umax_value = nr_cpu_ids - 1;
		ret_reg->u32_max_value = nr_cpu_ids - 1;
		ret_reg->smax_value = nr_cpu_ids - 1;
		ret_reg->s32_max_value = nr_cpu_ids - 1;
		ret_reg->umin_value = 0;
		ret_reg->u32_min_value = 0;
		ret_reg->smin_value = 0;
		ret_reg->s32_min_value = 0;
		reg_bounds_sync(ret_reg);
		break;
	}

	return reg_bounds_sanity_check(env, ret_reg, "retval");
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
	    func_id != BPF_FUNC_map_peek_elem &&
	    func_id != BPF_FUNC_for_each_map_elem &&
	    func_id != BPF_FUNC_redirect_map &&
	    func_id != BPF_FUNC_map_lookup_percpu_elem)
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

	if (!aux->map_ptr_state.map_ptr)
		bpf_map_ptr_store(aux, meta->map_ptr,
				  !meta->map_ptr->bypass_spec_v1, false);
	else if (aux->map_ptr_state.map_ptr != meta->map_ptr)
		bpf_map_ptr_store(aux, meta->map_ptr,
				  !meta->map_ptr->bypass_spec_v1, true);
	return 0;
}

static int
record_func_key(struct bpf_verifier_env *env, struct bpf_call_arg_meta *meta,
		int func_id, int insn_idx)
{
	struct bpf_insn_aux_data *aux = &env->insn_aux_data[insn_idx];
	struct bpf_reg_state *regs = cur_regs(env), *reg;
	struct bpf_map *map = meta->map_ptr;
	u64 val, max;
	int err;

	if (func_id != BPF_FUNC_tail_call)
		return 0;
	if (!map || map->map_type != BPF_MAP_TYPE_PROG_ARRAY) {
		verbose(env, "kernel subsystem misconfigured verifier\n");
		return -EINVAL;
	}

	reg = &regs[BPF_REG_3];
	val = reg->var_off.value;
	max = map->max_entries;

	if (!(is_reg_const(reg, false) && val < max)) {
		bpf_map_key_store(aux, BPF_MAP_KEY_POISON);
		return 0;
	}

	err = mark_chain_precision(env, BPF_REG_3);
	if (err)
		return err;
	if (bpf_map_key_unseen(aux))
		bpf_map_key_store(aux, val);
	else if (!bpf_map_key_poisoned(aux) &&
		  bpf_map_key_immediate(aux) != val)
		bpf_map_key_store(aux, BPF_MAP_KEY_POISON);
	return 0;
}

static int check_reference_leak(struct bpf_verifier_env *env, bool exception_exit)
{
	struct bpf_func_state *state = cur_func(env);
	bool refs_lingering = false;
	int i;

	if (!exception_exit && state->frameno)
		return 0;

	for (i = 0; i < state->acquired_refs; i++) {
		if (state->refs[i].type != REF_TYPE_PTR)
			continue;
		verbose(env, "Unreleased reference id=%d alloc_insn=%d\n",
			state->refs[i].id, state->refs[i].insn_idx);
		refs_lingering = true;
	}
	return refs_lingering ? -EINVAL : 0;
}

static int check_resource_leak(struct bpf_verifier_env *env, bool exception_exit, bool check_lock, const char *prefix)
{
	int err;

	if (check_lock && cur_func(env)->active_locks) {
		verbose(env, "%s cannot be used inside bpf_spin_lock-ed region\n", prefix);
		return -EINVAL;
	}

	err = check_reference_leak(env, exception_exit);
	if (err) {
		verbose(env, "%s would lead to reference leak\n", prefix);
		return err;
	}

	if (check_lock && env->cur_state->active_rcu_lock) {
		verbose(env, "%s cannot be used inside bpf_rcu_read_lock-ed region\n", prefix);
		return -EINVAL;
	}

	if (check_lock && env->cur_state->active_preempt_lock) {
		verbose(env, "%s cannot be used inside bpf_preempt_disable-ed region\n", prefix);
		return -EINVAL;
	}

	return 0;
}

static int check_bpf_snprintf_call(struct bpf_verifier_env *env,
				   struct bpf_reg_state *regs)
{
	struct bpf_reg_state *fmt_reg = &regs[BPF_REG_3];
	struct bpf_reg_state *data_len_reg = &regs[BPF_REG_5];
	struct bpf_map *fmt_map = fmt_reg->map_ptr;
	struct bpf_bprintf_data data = {};
	int err, fmt_map_off, num_args;
	u64 fmt_addr;
	char *fmt;

	/* data must be an array of u64 */
	if (data_len_reg->var_off.value % 8)
		return -EINVAL;
	num_args = data_len_reg->var_off.value / 8;

	/* fmt being ARG_PTR_TO_CONST_STR guarantees that var_off is const
	 * and map_direct_value_addr is set.
	 */
	fmt_map_off = fmt_reg->off + fmt_reg->var_off.value;
	err = fmt_map->ops->map_direct_value_addr(fmt_map, &fmt_addr,
						  fmt_map_off);
	if (err) {
		verbose(env, "verifier bug\n");
		return -EFAULT;
	}
	fmt = (char *)(long)fmt_addr + fmt_map_off;

	/* We are also guaranteed that fmt+fmt_map_off is NULL terminated, we
	 * can focus on validating the format specifiers.
	 */
	err = bpf_bprintf_prepare(fmt, UINT_MAX, NULL, num_args, &data);
	if (err < 0)
		verbose(env, "Invalid format string\n");

	return err;
}

static int check_get_func_ip(struct bpf_verifier_env *env)
{
	enum bpf_prog_type type = resolve_prog_type(env->prog);
	int func_id = BPF_FUNC_get_func_ip;

	if (type == BPF_PROG_TYPE_TRACING) {
		if (!bpf_prog_has_trampoline(env->prog)) {
			verbose(env, "func %s#%d supported only for fentry/fexit/fmod_ret programs\n",
				func_id_name(func_id), func_id);
			return -ENOTSUPP;
		}
		return 0;
	} else if (type == BPF_PROG_TYPE_KPROBE) {
		return 0;
	}

	verbose(env, "func %s#%d not supported for program type %d\n",
		func_id_name(func_id), func_id, type);
	return -ENOTSUPP;
}

static struct bpf_insn_aux_data *cur_aux(struct bpf_verifier_env *env)
{
	return &env->insn_aux_data[env->insn_idx];
}

static bool loop_flag_is_zero(struct bpf_verifier_env *env)
{
	struct bpf_reg_state *regs = cur_regs(env);
	struct bpf_reg_state *reg = &regs[BPF_REG_4];
	bool reg_is_null = register_is_null(reg);

	if (reg_is_null)
		mark_chain_precision(env, BPF_REG_4);

	return reg_is_null;
}

static void update_loop_inline_state(struct bpf_verifier_env *env, u32 subprogno)
{
	struct bpf_loop_inline_state *state = &cur_aux(env)->loop_inline_state;

	if (!state->initialized) {
		state->initialized = 1;
		state->fit_for_inline = loop_flag_is_zero(env);
		state->callback_subprogno = subprogno;
		return;
	}

	if (!state->fit_for_inline)
		return;

	state->fit_for_inline = (loop_flag_is_zero(env) &&
				 state->callback_subprogno == subprogno);
}

static int get_helper_proto(struct bpf_verifier_env *env, int func_id,
			    const struct bpf_func_proto **ptr)
{
	if (func_id < 0 || func_id >= __BPF_FUNC_MAX_ID)
		return -ERANGE;

	if (!env->ops->get_func_proto)
		return -EINVAL;

	*ptr = env->ops->get_func_proto(func_id, env->prog);
	return *ptr ? 0 : -EINVAL;
}

static int check_helper_call(struct bpf_verifier_env *env, struct bpf_insn *insn,
			     int *insn_idx_p)
{
	enum bpf_prog_type prog_type = resolve_prog_type(env->prog);
	bool returns_cpu_specific_alloc_ptr = false;
	const struct bpf_func_proto *fn = NULL;
	enum bpf_return_type ret_type;
	enum bpf_type_flag ret_flag;
	struct bpf_reg_state *regs;
	struct bpf_call_arg_meta meta;
	int insn_idx = *insn_idx_p;
	bool changes_data;
	int i, err, func_id;

	/* find function prototype */
	func_id = insn->imm;
	err = get_helper_proto(env, insn->imm, &fn);
	if (err == -ERANGE) {
		verbose(env, "invalid func %s#%d\n", func_id_name(func_id), func_id);
		return -EINVAL;
	}

	if (err) {
		verbose(env, "program of this type cannot use helper %s#%d\n",
			func_id_name(func_id), func_id);
		return err;
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

	if (!in_sleepable(env) && fn->might_sleep) {
		verbose(env, "helper call might sleep in a non-sleepable prog\n");
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

	if (env->cur_state->active_rcu_lock) {
		if (fn->might_sleep) {
			verbose(env, "sleepable helper %s#%d in rcu_read_lock region\n",
				func_id_name(func_id), func_id);
			return -EINVAL;
		}

		if (in_sleepable(env) && is_storage_get_function(func_id))
			env->insn_aux_data[insn_idx].storage_get_func_atomic = true;
	}

	if (env->cur_state->active_preempt_lock) {
		if (fn->might_sleep) {
			verbose(env, "sleepable helper %s#%d in non-preemptible region\n",
				func_id_name(func_id), func_id);
			return -EINVAL;
		}

		if (in_sleepable(env) && is_storage_get_function(func_id))
			env->insn_aux_data[insn_idx].storage_get_func_atomic = true;
	}

	meta.func_id = func_id;
	/* check args */
	for (i = 0; i < MAX_BPF_FUNC_REG_ARGS; i++) {
		err = check_func_arg(env, i, &meta, fn, insn_idx);
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
				       BPF_WRITE, -1, false, false);
		if (err)
			return err;
	}

	regs = cur_regs(env);

	if (meta.release_regno) {
		err = -EINVAL;
		/* This can only be set for PTR_TO_STACK, as CONST_PTR_TO_DYNPTR cannot
		 * be released by any dynptr helper. Hence, unmark_stack_slots_dynptr
		 * is safe to do directly.
		 */
		if (arg_type_is_dynptr(fn->arg_type[meta.release_regno - BPF_REG_1])) {
			if (regs[meta.release_regno].type == CONST_PTR_TO_DYNPTR) {
				verbose(env, "verifier internal error: CONST_PTR_TO_DYNPTR cannot be released\n");
				return -EFAULT;
			}
			err = unmark_stack_slots_dynptr(env, &regs[meta.release_regno]);
		} else if (func_id == BPF_FUNC_kptr_xchg && meta.ref_obj_id) {
			u32 ref_obj_id = meta.ref_obj_id;
			bool in_rcu = in_rcu_cs(env);
			struct bpf_func_state *state;
			struct bpf_reg_state *reg;

			err = release_reference_state(cur_func(env), ref_obj_id);
			if (!err) {
				bpf_for_each_reg_in_vstate(env->cur_state, state, reg, ({
					if (reg->ref_obj_id == ref_obj_id) {
						if (in_rcu && (reg->type & MEM_ALLOC) && (reg->type & MEM_PERCPU)) {
							reg->ref_obj_id = 0;
							reg->type &= ~MEM_ALLOC;
							reg->type |= MEM_RCU;
						} else {
							mark_reg_invalid(env, reg);
						}
					}
				}));
			}
		} else if (meta.ref_obj_id) {
			err = release_reference(env, meta.ref_obj_id);
		} else if (register_is_null(&regs[meta.release_regno])) {
			/* meta.ref_obj_id can only be 0 if register that is meant to be
			 * released is NULL, which must be > R0.
			 */
			err = 0;
		}
		if (err) {
			verbose(env, "func %s#%d reference has not been acquired before\n",
				func_id_name(func_id), func_id);
			return err;
		}
	}

	switch (func_id) {
	case BPF_FUNC_tail_call:
		err = check_resource_leak(env, false, true, "tail_call");
		if (err)
			return err;
		break;
	case BPF_FUNC_get_local_storage:
		/* check that flags argument in get_local_storage(map, flags) is 0,
		 * this is required because get_local_storage() can't return an error.
		 */
		if (!register_is_null(&regs[BPF_REG_2])) {
			verbose(env, "get_local_storage() doesn't support non-zero flags\n");
			return -EINVAL;
		}
		break;
	case BPF_FUNC_for_each_map_elem:
		err = push_callback_call(env, insn, insn_idx, meta.subprogno,
					 set_map_elem_callback_state);
		break;
	case BPF_FUNC_timer_set_callback:
		err = push_callback_call(env, insn, insn_idx, meta.subprogno,
					 set_timer_callback_state);
		break;
	case BPF_FUNC_find_vma:
		err = push_callback_call(env, insn, insn_idx, meta.subprogno,
					 set_find_vma_callback_state);
		break;
	case BPF_FUNC_snprintf:
		err = check_bpf_snprintf_call(env, regs);
		break;
	case BPF_FUNC_loop:
		update_loop_inline_state(env, meta.subprogno);
		/* Verifier relies on R1 value to determine if bpf_loop() iteration
		 * is finished, thus mark it precise.
		 */
		err = mark_chain_precision(env, BPF_REG_1);
		if (err)
			return err;
		if (cur_func(env)->callback_depth < regs[BPF_REG_1].umax_value) {
			err = push_callback_call(env, insn, insn_idx, meta.subprogno,
						 set_loop_callback_state);
		} else {
			cur_func(env)->callback_depth = 0;
			if (env->log.level & BPF_LOG_LEVEL2)
				verbose(env, "frame%d bpf_loop iteration limit reached\n",
					env->cur_state->curframe);
		}
		break;
	case BPF_FUNC_dynptr_from_mem:
		if (regs[BPF_REG_1].type != PTR_TO_MAP_VALUE) {
			verbose(env, "Unsupported reg type %s for bpf_dynptr_from_mem data\n",
				reg_type_str(env, regs[BPF_REG_1].type));
			return -EACCES;
		}
		break;
	case BPF_FUNC_set_retval:
		if (prog_type == BPF_PROG_TYPE_LSM &&
		    env->prog->expected_attach_type == BPF_LSM_CGROUP) {
			if (!env->prog->aux->attach_func_proto->type) {
				/* Make sure programs that attach to void
				 * hooks don't try to modify return value.
				 */
				verbose(env, "BPF_LSM_CGROUP that attach to void LSM hooks can't modify return value!\n");
				return -EINVAL;
			}
		}
		break;
	case BPF_FUNC_dynptr_data:
	{
		struct bpf_reg_state *reg;
		int id, ref_obj_id;

		reg = get_dynptr_arg_reg(env, fn, regs);
		if (!reg)
			return -EFAULT;


		if (meta.dynptr_id) {
			verbose(env, "verifier internal error: meta.dynptr_id already set\n");
			return -EFAULT;
		}
		if (meta.ref_obj_id) {
			verbose(env, "verifier internal error: meta.ref_obj_id already set\n");
			return -EFAULT;
		}

		id = dynptr_id(env, reg);
		if (id < 0) {
			verbose(env, "verifier internal error: failed to obtain dynptr id\n");
			return id;
		}

		ref_obj_id = dynptr_ref_obj_id(env, reg);
		if (ref_obj_id < 0) {
			verbose(env, "verifier internal error: failed to obtain dynptr ref_obj_id\n");
			return ref_obj_id;
		}

		meta.dynptr_id = id;
		meta.ref_obj_id = ref_obj_id;

		break;
	}
	case BPF_FUNC_dynptr_write:
	{
		enum bpf_dynptr_type dynptr_type;
		struct bpf_reg_state *reg;

		reg = get_dynptr_arg_reg(env, fn, regs);
		if (!reg)
			return -EFAULT;

		dynptr_type = dynptr_get_type(env, reg);
		if (dynptr_type == BPF_DYNPTR_TYPE_INVALID)
			return -EFAULT;

		if (dynptr_type == BPF_DYNPTR_TYPE_SKB)
			/* this will trigger clear_all_pkt_pointers(), which will
			 * invalidate all dynptr slices associated with the skb
			 */
			changes_data = true;

		break;
	}
	case BPF_FUNC_per_cpu_ptr:
	case BPF_FUNC_this_cpu_ptr:
	{
		struct bpf_reg_state *reg = &regs[BPF_REG_1];
		const struct btf_type *type;

		if (reg->type & MEM_RCU) {
			type = btf_type_by_id(reg->btf, reg->btf_id);
			if (!type || !btf_type_is_struct(type)) {
				verbose(env, "Helper has invalid btf/btf_id in R1\n");
				return -EFAULT;
			}
			returns_cpu_specific_alloc_ptr = true;
			env->insn_aux_data[insn_idx].call_with_percpu_alloc_ptr = true;
		}
		break;
	}
	case BPF_FUNC_user_ringbuf_drain:
		err = push_callback_call(env, insn, insn_idx, meta.subprogno,
					 set_user_ringbuf_callback_state);
		break;
	}

	if (err)
		return err;

	/* reset caller saved regs */
	for (i = 0; i < CALLER_SAVED_REGS; i++) {
		mark_reg_not_init(env, regs, caller_saved[i]);
		check_reg_arg(env, caller_saved[i], DST_OP_NO_MARK);
	}

	/* helper call returns 64-bit value. */
	regs[BPF_REG_0].subreg_def = DEF_NOT_SUBREG;

	/* update return register (already marked as written above) */
	ret_type = fn->ret_type;
	ret_flag = type_flag(ret_type);

	switch (base_type(ret_type)) {
	case RET_INTEGER:
		/* sets type to SCALAR_VALUE */
		mark_reg_unknown(env, regs, BPF_REG_0);
		break;
	case RET_VOID:
		regs[BPF_REG_0].type = NOT_INIT;
		break;
	case RET_PTR_TO_MAP_VALUE:
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
		regs[BPF_REG_0].map_uid = meta.map_uid;
		regs[BPF_REG_0].type = PTR_TO_MAP_VALUE | ret_flag;
		if (!type_may_be_null(ret_type) &&
		    btf_record_has_field(meta.map_ptr->record, BPF_SPIN_LOCK)) {
			regs[BPF_REG_0].id = ++env->id_gen;
		}
		break;
	case RET_PTR_TO_SOCKET:
		mark_reg_known_zero(env, regs, BPF_REG_0);
		regs[BPF_REG_0].type = PTR_TO_SOCKET | ret_flag;
		break;
	case RET_PTR_TO_SOCK_COMMON:
		mark_reg_known_zero(env, regs, BPF_REG_0);
		regs[BPF_REG_0].type = PTR_TO_SOCK_COMMON | ret_flag;
		break;
	case RET_PTR_TO_TCP_SOCK:
		mark_reg_known_zero(env, regs, BPF_REG_0);
		regs[BPF_REG_0].type = PTR_TO_TCP_SOCK | ret_flag;
		break;
	case RET_PTR_TO_MEM:
		mark_reg_known_zero(env, regs, BPF_REG_0);
		regs[BPF_REG_0].type = PTR_TO_MEM | ret_flag;
		regs[BPF_REG_0].mem_size = meta.mem_size;
		break;
	case RET_PTR_TO_MEM_OR_BTF_ID:
	{
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
			regs[BPF_REG_0].type = PTR_TO_MEM | ret_flag;
			regs[BPF_REG_0].mem_size = tsize;
		} else {
			if (returns_cpu_specific_alloc_ptr) {
				regs[BPF_REG_0].type = PTR_TO_BTF_ID | MEM_ALLOC | MEM_RCU;
			} else {
				/* MEM_RDONLY may be carried from ret_flag, but it
				 * doesn't apply on PTR_TO_BTF_ID. Fold it, otherwise
				 * it will confuse the check of PTR_TO_BTF_ID in
				 * check_mem_access().
				 */
				ret_flag &= ~MEM_RDONLY;
				regs[BPF_REG_0].type = PTR_TO_BTF_ID | ret_flag;
			}

			regs[BPF_REG_0].btf = meta.ret_btf;
			regs[BPF_REG_0].btf_id = meta.ret_btf_id;
		}
		break;
	}
	case RET_PTR_TO_BTF_ID:
	{
		struct btf *ret_btf;
		int ret_btf_id;

		mark_reg_known_zero(env, regs, BPF_REG_0);
		regs[BPF_REG_0].type = PTR_TO_BTF_ID | ret_flag;
		if (func_id == BPF_FUNC_kptr_xchg) {
			ret_btf = meta.kptr_field->kptr.btf;
			ret_btf_id = meta.kptr_field->kptr.btf_id;
			if (!btf_is_kernel(ret_btf)) {
				regs[BPF_REG_0].type |= MEM_ALLOC;
				if (meta.kptr_field->type == BPF_KPTR_PERCPU)
					regs[BPF_REG_0].type |= MEM_PERCPU;
			}
		} else {
			if (fn->ret_btf_id == BPF_PTR_POISON) {
				verbose(env, "verifier internal error:");
				verbose(env, "func %s has non-overwritten BPF_PTR_POISON return type\n",
					func_id_name(func_id));
				return -EINVAL;
			}
			ret_btf = btf_vmlinux;
			ret_btf_id = *fn->ret_btf_id;
		}
		if (ret_btf_id == 0) {
			verbose(env, "invalid return type %u of func %s#%d\n",
				base_type(ret_type), func_id_name(func_id),
				func_id);
			return -EINVAL;
		}
		regs[BPF_REG_0].btf = ret_btf;
		regs[BPF_REG_0].btf_id = ret_btf_id;
		break;
	}
	default:
		verbose(env, "unknown return type %u of func %s#%d\n",
			base_type(ret_type), func_id_name(func_id), func_id);
		return -EINVAL;
	}

	if (type_may_be_null(regs[BPF_REG_0].type))
		regs[BPF_REG_0].id = ++env->id_gen;

	if (helper_multiple_ref_obj_use(func_id, meta.map_ptr)) {
		verbose(env, "verifier internal error: func %s#%d sets ref_obj_id more than once\n",
			func_id_name(func_id), func_id);
		return -EFAULT;
	}

	if (is_dynptr_ref_function(func_id))
		regs[BPF_REG_0].dynptr_id = meta.dynptr_id;

	if (is_ptr_cast_function(func_id) || is_dynptr_ref_function(func_id)) {
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

	err = do_refine_retval_range(env, regs, fn->ret_type, func_id, &meta);
	if (err)
		return err;

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

	if (func_id == BPF_FUNC_get_func_ip) {
		if (check_get_func_ip(env))
			return -ENOTSUPP;
		env->prog->call_get_func_ip = true;
	}

	if (changes_data)
		clear_all_pkt_pointers(env);
	return 0;
}

/* mark_btf_func_reg_size() is used when the reg size is determined by
 * the BTF func_proto's return value size and argument.
 */
static void mark_btf_func_reg_size(struct bpf_verifier_env *env, u32 regno,
				   size_t reg_size)
{
	struct bpf_reg_state *reg = &cur_regs(env)[regno];

	if (regno == BPF_REG_0) {
		/* Function return value */
		reg->live |= REG_LIVE_WRITTEN;
		reg->subreg_def = reg_size == sizeof(u64) ?
			DEF_NOT_SUBREG : env->insn_idx + 1;
	} else {
		/* Function argument */
		if (reg_size == sizeof(u64)) {
			mark_insn_zext(env, reg);
			mark_reg_read(env, reg, reg->parent, REG_LIVE_READ64);
		} else {
			mark_reg_read(env, reg, reg->parent, REG_LIVE_READ32);
		}
	}
}

static bool is_kfunc_acquire(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_ACQUIRE;
}

static bool is_kfunc_release(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_RELEASE;
}

static bool is_kfunc_trusted_args(struct bpf_kfunc_call_arg_meta *meta)
{
	return (meta->kfunc_flags & KF_TRUSTED_ARGS) || is_kfunc_release(meta);
}

static bool is_kfunc_sleepable(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_SLEEPABLE;
}

static bool is_kfunc_destructive(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_DESTRUCTIVE;
}

static bool is_kfunc_rcu(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_RCU;
}

static bool is_kfunc_rcu_protected(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_RCU_PROTECTED;
}

static bool is_kfunc_arg_mem_size(const struct btf *btf,
				  const struct btf_param *arg,
				  const struct bpf_reg_state *reg)
{
	const struct btf_type *t;

	t = btf_type_skip_modifiers(btf, arg->type, NULL);
	if (!btf_type_is_scalar(t) || reg->type != SCALAR_VALUE)
		return false;

	return btf_param_match_suffix(btf, arg, "__sz");
}

static bool is_kfunc_arg_const_mem_size(const struct btf *btf,
					const struct btf_param *arg,
					const struct bpf_reg_state *reg)
{
	const struct btf_type *t;

	t = btf_type_skip_modifiers(btf, arg->type, NULL);
	if (!btf_type_is_scalar(t) || reg->type != SCALAR_VALUE)
		return false;

	return btf_param_match_suffix(btf, arg, "__szk");
}

static bool is_kfunc_arg_optional(const struct btf *btf, const struct btf_param *arg)
{
	return btf_param_match_suffix(btf, arg, "__opt");
}

static bool is_kfunc_arg_constant(const struct btf *btf, const struct btf_param *arg)
{
	return btf_param_match_suffix(btf, arg, "__k");
}

static bool is_kfunc_arg_ignore(const struct btf *btf, const struct btf_param *arg)
{
	return btf_param_match_suffix(btf, arg, "__ign");
}

static bool is_kfunc_arg_map(const struct btf *btf, const struct btf_param *arg)
{
	return btf_param_match_suffix(btf, arg, "__map");
}

static bool is_kfunc_arg_alloc_obj(const struct btf *btf, const struct btf_param *arg)
{
	return btf_param_match_suffix(btf, arg, "__alloc");
}

static bool is_kfunc_arg_uninit(const struct btf *btf, const struct btf_param *arg)
{
	return btf_param_match_suffix(btf, arg, "__uninit");
}

static bool is_kfunc_arg_refcounted_kptr(const struct btf *btf, const struct btf_param *arg)
{
	return btf_param_match_suffix(btf, arg, "__refcounted_kptr");
}

static bool is_kfunc_arg_nullable(const struct btf *btf, const struct btf_param *arg)
{
	return btf_param_match_suffix(btf, arg, "__nullable");
}

static bool is_kfunc_arg_const_str(const struct btf *btf, const struct btf_param *arg)
{
	return btf_param_match_suffix(btf, arg, "__str");
}

static bool is_kfunc_arg_scalar_with_name(const struct btf *btf,
					  const struct btf_param *arg,
					  const char *name)
{
	int len, target_len = strlen(name);
	const char *param_name;

	param_name = btf_name_by_offset(btf, arg->name_off);
	if (str_is_empty(param_name))
		return false;
	len = strlen(param_name);
	if (len != target_len)
		return false;
	if (strcmp(param_name, name))
		return false;

	return true;
}

enum {
	KF_ARG_DYNPTR_ID,
	KF_ARG_LIST_HEAD_ID,
	KF_ARG_LIST_NODE_ID,
	KF_ARG_RB_ROOT_ID,
	KF_ARG_RB_NODE_ID,
	KF_ARG_WORKQUEUE_ID,
};

BTF_ID_LIST(kf_arg_btf_ids)
BTF_ID(struct, bpf_dynptr)
BTF_ID(struct, bpf_list_head)
BTF_ID(struct, bpf_list_node)
BTF_ID(struct, bpf_rb_root)
BTF_ID(struct, bpf_rb_node)
BTF_ID(struct, bpf_wq)

static bool __is_kfunc_ptr_arg_type(const struct btf *btf,
				    const struct btf_param *arg, int type)
{
	const struct btf_type *t;
	u32 res_id;

	t = btf_type_skip_modifiers(btf, arg->type, NULL);
	if (!t)
		return false;
	if (!btf_type_is_ptr(t))
		return false;
	t = btf_type_skip_modifiers(btf, t->type, &res_id);
	if (!t)
		return false;
	return btf_types_are_same(btf, res_id, btf_vmlinux, kf_arg_btf_ids[type]);
}

static bool is_kfunc_arg_dynptr(const struct btf *btf, const struct btf_param *arg)
{
	return __is_kfunc_ptr_arg_type(btf, arg, KF_ARG_DYNPTR_ID);
}

static bool is_kfunc_arg_list_head(const struct btf *btf, const struct btf_param *arg)
{
	return __is_kfunc_ptr_arg_type(btf, arg, KF_ARG_LIST_HEAD_ID);
}

static bool is_kfunc_arg_list_node(const struct btf *btf, const struct btf_param *arg)
{
	return __is_kfunc_ptr_arg_type(btf, arg, KF_ARG_LIST_NODE_ID);
}

static bool is_kfunc_arg_rbtree_root(const struct btf *btf, const struct btf_param *arg)
{
	return __is_kfunc_ptr_arg_type(btf, arg, KF_ARG_RB_ROOT_ID);
}

static bool is_kfunc_arg_rbtree_node(const struct btf *btf, const struct btf_param *arg)
{
	return __is_kfunc_ptr_arg_type(btf, arg, KF_ARG_RB_NODE_ID);
}

static bool is_kfunc_arg_wq(const struct btf *btf, const struct btf_param *arg)
{
	return __is_kfunc_ptr_arg_type(btf, arg, KF_ARG_WORKQUEUE_ID);
}

static bool is_kfunc_arg_callback(struct bpf_verifier_env *env, const struct btf *btf,
				  const struct btf_param *arg)
{
	const struct btf_type *t;

	t = btf_type_resolve_func_ptr(btf, arg->type, NULL);
	if (!t)
		return false;

	return true;
}

/* Returns true if struct is composed of scalars, 4 levels of nesting allowed */
static bool __btf_type_is_scalar_struct(struct bpf_verifier_env *env,
					const struct btf *btf,
					const struct btf_type *t, int rec)
{
	const struct btf_type *member_type;
	const struct btf_member *member;
	u32 i;

	if (!btf_type_is_struct(t))
		return false;

	for_each_member(i, t, member) {
		const struct btf_array *array;

		member_type = btf_type_skip_modifiers(btf, member->type, NULL);
		if (btf_type_is_struct(member_type)) {
			if (rec >= 3) {
				verbose(env, "max struct nesting depth exceeded\n");
				return false;
			}
			if (!__btf_type_is_scalar_struct(env, btf, member_type, rec + 1))
				return false;
			continue;
		}
		if (btf_type_is_array(member_type)) {
			array = btf_array(member_type);
			if (!array->nelems)
				return false;
			member_type = btf_type_skip_modifiers(btf, array->type, NULL);
			if (!btf_type_is_scalar(member_type))
				return false;
			continue;
		}
		if (!btf_type_is_scalar(member_type))
			return false;
	}
	return true;
}

enum kfunc_ptr_arg_type {
	KF_ARG_PTR_TO_CTX,
	KF_ARG_PTR_TO_ALLOC_BTF_ID,    /* Allocated object */
	KF_ARG_PTR_TO_REFCOUNTED_KPTR, /* Refcounted local kptr */
	KF_ARG_PTR_TO_DYNPTR,
	KF_ARG_PTR_TO_ITER,
	KF_ARG_PTR_TO_LIST_HEAD,
	KF_ARG_PTR_TO_LIST_NODE,
	KF_ARG_PTR_TO_BTF_ID,	       /* Also covers reg2btf_ids conversions */
	KF_ARG_PTR_TO_MEM,
	KF_ARG_PTR_TO_MEM_SIZE,	       /* Size derived from next argument, skip it */
	KF_ARG_PTR_TO_CALLBACK,
	KF_ARG_PTR_TO_RB_ROOT,
	KF_ARG_PTR_TO_RB_NODE,
	KF_ARG_PTR_TO_NULL,
	KF_ARG_PTR_TO_CONST_STR,
	KF_ARG_PTR_TO_MAP,
	KF_ARG_PTR_TO_WORKQUEUE,
};

enum special_kfunc_type {
	KF_bpf_obj_new_impl,
	KF_bpf_obj_drop_impl,
	KF_bpf_refcount_acquire_impl,
	KF_bpf_list_push_front_impl,
	KF_bpf_list_push_back_impl,
	KF_bpf_list_pop_front,
	KF_bpf_list_pop_back,
	KF_bpf_cast_to_kern_ctx,
	KF_bpf_rdonly_cast,
	KF_bpf_rcu_read_lock,
	KF_bpf_rcu_read_unlock,
	KF_bpf_rbtree_remove,
	KF_bpf_rbtree_add_impl,
	KF_bpf_rbtree_first,
	KF_bpf_dynptr_from_skb,
	KF_bpf_dynptr_from_xdp,
	KF_bpf_dynptr_slice,
	KF_bpf_dynptr_slice_rdwr,
	KF_bpf_dynptr_clone,
	KF_bpf_percpu_obj_new_impl,
	KF_bpf_percpu_obj_drop_impl,
	KF_bpf_throw,
	KF_bpf_wq_set_callback_impl,
	KF_bpf_preempt_disable,
	KF_bpf_preempt_enable,
	KF_bpf_iter_css_task_new,
	KF_bpf_session_cookie,
	KF_bpf_get_kmem_cache,
};

BTF_SET_START(special_kfunc_set)
BTF_ID(func, bpf_obj_new_impl)
BTF_ID(func, bpf_obj_drop_impl)
BTF_ID(func, bpf_refcount_acquire_impl)
BTF_ID(func, bpf_list_push_front_impl)
BTF_ID(func, bpf_list_push_back_impl)
BTF_ID(func, bpf_list_pop_front)
BTF_ID(func, bpf_list_pop_back)
BTF_ID(func, bpf_cast_to_kern_ctx)
BTF_ID(func, bpf_rdonly_cast)
BTF_ID(func, bpf_rbtree_remove)
BTF_ID(func, bpf_rbtree_add_impl)
BTF_ID(func, bpf_rbtree_first)
BTF_ID(func, bpf_dynptr_from_skb)
BTF_ID(func, bpf_dynptr_from_xdp)
BTF_ID(func, bpf_dynptr_slice)
BTF_ID(func, bpf_dynptr_slice_rdwr)
BTF_ID(func, bpf_dynptr_clone)
BTF_ID(func, bpf_percpu_obj_new_impl)
BTF_ID(func, bpf_percpu_obj_drop_impl)
BTF_ID(func, bpf_throw)
BTF_ID(func, bpf_wq_set_callback_impl)
#ifdef CONFIG_CGROUPS
BTF_ID(func, bpf_iter_css_task_new)
#endif
BTF_SET_END(special_kfunc_set)

BTF_ID_LIST(special_kfunc_list)
BTF_ID(func, bpf_obj_new_impl)
BTF_ID(func, bpf_obj_drop_impl)
BTF_ID(func, bpf_refcount_acquire_impl)
BTF_ID(func, bpf_list_push_front_impl)
BTF_ID(func, bpf_list_push_back_impl)
BTF_ID(func, bpf_list_pop_front)
BTF_ID(func, bpf_list_pop_back)
BTF_ID(func, bpf_cast_to_kern_ctx)
BTF_ID(func, bpf_rdonly_cast)
BTF_ID(func, bpf_rcu_read_lock)
BTF_ID(func, bpf_rcu_read_unlock)
BTF_ID(func, bpf_rbtree_remove)
BTF_ID(func, bpf_rbtree_add_impl)
BTF_ID(func, bpf_rbtree_first)
BTF_ID(func, bpf_dynptr_from_skb)
BTF_ID(func, bpf_dynptr_from_xdp)
BTF_ID(func, bpf_dynptr_slice)
BTF_ID(func, bpf_dynptr_slice_rdwr)
BTF_ID(func, bpf_dynptr_clone)
BTF_ID(func, bpf_percpu_obj_new_impl)
BTF_ID(func, bpf_percpu_obj_drop_impl)
BTF_ID(func, bpf_throw)
BTF_ID(func, bpf_wq_set_callback_impl)
BTF_ID(func, bpf_preempt_disable)
BTF_ID(func, bpf_preempt_enable)
#ifdef CONFIG_CGROUPS
BTF_ID(func, bpf_iter_css_task_new)
#else
BTF_ID_UNUSED
#endif
#ifdef CONFIG_BPF_EVENTS
BTF_ID(func, bpf_session_cookie)
#else
BTF_ID_UNUSED
#endif
BTF_ID(func, bpf_get_kmem_cache)

static bool is_kfunc_ret_null(struct bpf_kfunc_call_arg_meta *meta)
{
	if (meta->func_id == special_kfunc_list[KF_bpf_refcount_acquire_impl] &&
	    meta->arg_owning_ref) {
		return false;
	}

	return meta->kfunc_flags & KF_RET_NULL;
}

static bool is_kfunc_bpf_rcu_read_lock(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->func_id == special_kfunc_list[KF_bpf_rcu_read_lock];
}

static bool is_kfunc_bpf_rcu_read_unlock(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->func_id == special_kfunc_list[KF_bpf_rcu_read_unlock];
}

static bool is_kfunc_bpf_preempt_disable(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->func_id == special_kfunc_list[KF_bpf_preempt_disable];
}

static bool is_kfunc_bpf_preempt_enable(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->func_id == special_kfunc_list[KF_bpf_preempt_enable];
}

static enum kfunc_ptr_arg_type
get_kfunc_ptr_arg_type(struct bpf_verifier_env *env,
		       struct bpf_kfunc_call_arg_meta *meta,
		       const struct btf_type *t, const struct btf_type *ref_t,
		       const char *ref_tname, const struct btf_param *args,
		       int argno, int nargs)
{
	u32 regno = argno + 1;
	struct bpf_reg_state *regs = cur_regs(env);
	struct bpf_reg_state *reg = &regs[regno];
	bool arg_mem_size = false;

	if (meta->func_id == special_kfunc_list[KF_bpf_cast_to_kern_ctx])
		return KF_ARG_PTR_TO_CTX;

	/* In this function, we verify the kfunc's BTF as per the argument type,
	 * leaving the rest of the verification with respect to the register
	 * type to our caller. When a set of conditions hold in the BTF type of
	 * arguments, we resolve it to a known kfunc_ptr_arg_type.
	 */
	if (btf_is_prog_ctx_type(&env->log, meta->btf, t, resolve_prog_type(env->prog), argno))
		return KF_ARG_PTR_TO_CTX;

	if (is_kfunc_arg_nullable(meta->btf, &args[argno]) && register_is_null(reg))
		return KF_ARG_PTR_TO_NULL;

	if (is_kfunc_arg_alloc_obj(meta->btf, &args[argno]))
		return KF_ARG_PTR_TO_ALLOC_BTF_ID;

	if (is_kfunc_arg_refcounted_kptr(meta->btf, &args[argno]))
		return KF_ARG_PTR_TO_REFCOUNTED_KPTR;

	if (is_kfunc_arg_dynptr(meta->btf, &args[argno]))
		return KF_ARG_PTR_TO_DYNPTR;

	if (is_kfunc_arg_iter(meta, argno, &args[argno]))
		return KF_ARG_PTR_TO_ITER;

	if (is_kfunc_arg_list_head(meta->btf, &args[argno]))
		return KF_ARG_PTR_TO_LIST_HEAD;

	if (is_kfunc_arg_list_node(meta->btf, &args[argno]))
		return KF_ARG_PTR_TO_LIST_NODE;

	if (is_kfunc_arg_rbtree_root(meta->btf, &args[argno]))
		return KF_ARG_PTR_TO_RB_ROOT;

	if (is_kfunc_arg_rbtree_node(meta->btf, &args[argno]))
		return KF_ARG_PTR_TO_RB_NODE;

	if (is_kfunc_arg_const_str(meta->btf, &args[argno]))
		return KF_ARG_PTR_TO_CONST_STR;

	if (is_kfunc_arg_map(meta->btf, &args[argno]))
		return KF_ARG_PTR_TO_MAP;

	if (is_kfunc_arg_wq(meta->btf, &args[argno]))
		return KF_ARG_PTR_TO_WORKQUEUE;

	if ((base_type(reg->type) == PTR_TO_BTF_ID || reg2btf_ids[base_type(reg->type)])) {
		if (!btf_type_is_struct(ref_t)) {
			verbose(env, "kernel function %s args#%d pointer type %s %s is not supported\n",
				meta->func_name, argno, btf_type_str(ref_t), ref_tname);
			return -EINVAL;
		}
		return KF_ARG_PTR_TO_BTF_ID;
	}

	if (is_kfunc_arg_callback(env, meta->btf, &args[argno]))
		return KF_ARG_PTR_TO_CALLBACK;

	if (argno + 1 < nargs &&
	    (is_kfunc_arg_mem_size(meta->btf, &args[argno + 1], &regs[regno + 1]) ||
	     is_kfunc_arg_const_mem_size(meta->btf, &args[argno + 1], &regs[regno + 1])))
		arg_mem_size = true;

	/* This is the catch all argument type of register types supported by
	 * check_helper_mem_access. However, we only allow when argument type is
	 * pointer to scalar, or struct composed (recursively) of scalars. When
	 * arg_mem_size is true, the pointer can be void *.
	 */
	if (!btf_type_is_scalar(ref_t) && !__btf_type_is_scalar_struct(env, meta->btf, ref_t, 0) &&
	    (arg_mem_size ? !btf_type_is_void(ref_t) : 1)) {
		verbose(env, "arg#%d pointer type %s %s must point to %sscalar, or struct with scalar\n",
			argno, btf_type_str(ref_t), ref_tname, arg_mem_size ? "void, " : "");
		return -EINVAL;
	}
	return arg_mem_size ? KF_ARG_PTR_TO_MEM_SIZE : KF_ARG_PTR_TO_MEM;
}

static int process_kf_arg_ptr_to_btf_id(struct bpf_verifier_env *env,
					struct bpf_reg_state *reg,
					const struct btf_type *ref_t,
					const char *ref_tname, u32 ref_id,
					struct bpf_kfunc_call_arg_meta *meta,
					int argno)
{
	const struct btf_type *reg_ref_t;
	bool strict_type_match = false;
	const struct btf *reg_btf;
	const char *reg_ref_tname;
	bool taking_projection;
	bool struct_same;
	u32 reg_ref_id;

	if (base_type(reg->type) == PTR_TO_BTF_ID) {
		reg_btf = reg->btf;
		reg_ref_id = reg->btf_id;
	} else {
		reg_btf = btf_vmlinux;
		reg_ref_id = *reg2btf_ids[base_type(reg->type)];
	}

	/* Enforce strict type matching for calls to kfuncs that are acquiring
	 * or releasing a reference, or are no-cast aliases. We do _not_
	 * enforce strict matching for plain KF_TRUSTED_ARGS kfuncs by default,
	 * as we want to enable BPF programs to pass types that are bitwise
	 * equivalent without forcing them to explicitly cast with something
	 * like bpf_cast_to_kern_ctx().
	 *
	 * For example, say we had a type like the following:
	 *
	 * struct bpf_cpumask {
	 *	cpumask_t cpumask;
	 *	refcount_t usage;
	 * };
	 *
	 * Note that as specified in <linux/cpumask.h>, cpumask_t is typedef'ed
	 * to a struct cpumask, so it would be safe to pass a struct
	 * bpf_cpumask * to a kfunc expecting a struct cpumask *.
	 *
	 * The philosophy here is similar to how we allow scalars of different
	 * types to be passed to kfuncs as long as the size is the same. The
	 * only difference here is that we're simply allowing
	 * btf_struct_ids_match() to walk the struct at the 0th offset, and
	 * resolve types.
	 */
	if ((is_kfunc_release(meta) && reg->ref_obj_id) ||
	    btf_type_ids_nocast_alias(&env->log, reg_btf, reg_ref_id, meta->btf, ref_id))
		strict_type_match = true;

	WARN_ON_ONCE(is_kfunc_release(meta) &&
		     (reg->off || !tnum_is_const(reg->var_off) ||
		      reg->var_off.value));

	reg_ref_t = btf_type_skip_modifiers(reg_btf, reg_ref_id, &reg_ref_id);
	reg_ref_tname = btf_name_by_offset(reg_btf, reg_ref_t->name_off);
	struct_same = btf_struct_ids_match(&env->log, reg_btf, reg_ref_id, reg->off, meta->btf, ref_id, strict_type_match);
	/* If kfunc is accepting a projection type (ie. __sk_buff), it cannot
	 * actually use it -- it must cast to the underlying type. So we allow
	 * caller to pass in the underlying type.
	 */
	taking_projection = btf_is_projection_of(ref_tname, reg_ref_tname);
	if (!taking_projection && !struct_same) {
		verbose(env, "kernel function %s args#%d expected pointer to %s %s but R%d has a pointer to %s %s\n",
			meta->func_name, argno, btf_type_str(ref_t), ref_tname, argno + 1,
			btf_type_str(reg_ref_t), reg_ref_tname);
		return -EINVAL;
	}
	return 0;
}

static int ref_set_non_owning(struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	struct btf_record *rec = reg_btf_record(reg);

	if (!cur_func(env)->active_locks) {
		verbose(env, "verifier internal error: ref_set_non_owning w/o active lock\n");
		return -EFAULT;
	}

	if (type_flag(reg->type) & NON_OWN_REF) {
		verbose(env, "verifier internal error: NON_OWN_REF already set\n");
		return -EFAULT;
	}

	reg->type |= NON_OWN_REF;
	if (rec->refcount_off >= 0)
		reg->type |= MEM_RCU;

	return 0;
}

static int ref_convert_owning_non_owning(struct bpf_verifier_env *env, u32 ref_obj_id)
{
	struct bpf_func_state *state, *unused;
	struct bpf_reg_state *reg;
	int i;

	state = cur_func(env);

	if (!ref_obj_id) {
		verbose(env, "verifier internal error: ref_obj_id is zero for "
			     "owning -> non-owning conversion\n");
		return -EFAULT;
	}

	for (i = 0; i < state->acquired_refs; i++) {
		if (state->refs[i].id != ref_obj_id)
			continue;

		/* Clear ref_obj_id here so release_reference doesn't clobber
		 * the whole reg
		 */
		bpf_for_each_reg_in_vstate(env->cur_state, unused, reg, ({
			if (reg->ref_obj_id == ref_obj_id) {
				reg->ref_obj_id = 0;
				ref_set_non_owning(env, reg);
			}
		}));
		return 0;
	}

	verbose(env, "verifier internal error: ref state missing for ref_obj_id\n");
	return -EFAULT;
}

/* Implementation details:
 *
 * Each register points to some region of memory, which we define as an
 * allocation. Each allocation may embed a bpf_spin_lock which protects any
 * special BPF objects (bpf_list_head, bpf_rb_root, etc.) part of the same
 * allocation. The lock and the data it protects are colocated in the same
 * memory region.
 *
 * Hence, everytime a register holds a pointer value pointing to such
 * allocation, the verifier preserves a unique reg->id for it.
 *
 * The verifier remembers the lock 'ptr' and the lock 'id' whenever
 * bpf_spin_lock is called.
 *
 * To enable this, lock state in the verifier captures two values:
 *	active_lock.ptr = Register's type specific pointer
 *	active_lock.id  = A unique ID for each register pointer value
 *
 * Currently, PTR_TO_MAP_VALUE and PTR_TO_BTF_ID | MEM_ALLOC are the two
 * supported register types.
 *
 * The active_lock.ptr in case of map values is the reg->map_ptr, and in case of
 * allocated objects is the reg->btf pointer.
 *
 * The active_lock.id is non-unique for maps supporting direct_value_addr, as we
 * can establish the provenance of the map value statically for each distinct
 * lookup into such maps. They always contain a single map value hence unique
 * IDs for each pseudo load pessimizes the algorithm and rejects valid programs.
 *
 * So, in case of global variables, they use array maps with max_entries = 1,
 * hence their active_lock.ptr becomes map_ptr and id = 0 (since they all point
 * into the same map value as max_entries is 1, as described above).
 *
 * In case of inner map lookups, the inner map pointer has same map_ptr as the
 * outer map pointer (in verifier context), but each lookup into an inner map
 * assigns a fresh reg->id to the lookup, so while lookups into distinct inner
 * maps from the same outer map share the same map_ptr as active_lock.ptr, they
 * will get different reg->id assigned to each lookup, hence different
 * active_lock.id.
 *
 * In case of allocated objects, active_lock.ptr is the reg->btf, and the
 * reg->id is a unique ID preserved after the NULL pointer check on the pointer
 * returned from bpf_obj_new. Each allocation receives a new reg->id.
 */
static int check_reg_allocation_locked(struct bpf_verifier_env *env, struct bpf_reg_state *reg)
{
	struct bpf_reference_state *s;
	void *ptr;
	u32 id;

	switch ((int)reg->type) {
	case PTR_TO_MAP_VALUE:
		ptr = reg->map_ptr;
		break;
	case PTR_TO_BTF_ID | MEM_ALLOC:
		ptr = reg->btf;
		break;
	default:
		verbose(env, "verifier internal error: unknown reg type for lock check\n");
		return -EFAULT;
	}
	id = reg->id;

	if (!cur_func(env)->active_locks)
		return -EINVAL;
	s = find_lock_state(env, REF_TYPE_LOCK, id, ptr);
	if (!s) {
		verbose(env, "held lock and object are not in the same allocation\n");
		return -EINVAL;
	}
	return 0;
}

static bool is_bpf_list_api_kfunc(u32 btf_id)
{
	return btf_id == special_kfunc_list[KF_bpf_list_push_front_impl] ||
	       btf_id == special_kfunc_list[KF_bpf_list_push_back_impl] ||
	       btf_id == special_kfunc_list[KF_bpf_list_pop_front] ||
	       btf_id == special_kfunc_list[KF_bpf_list_pop_back];
}

static bool is_bpf_rbtree_api_kfunc(u32 btf_id)
{
	return btf_id == special_kfunc_list[KF_bpf_rbtree_add_impl] ||
	       btf_id == special_kfunc_list[KF_bpf_rbtree_remove] ||
	       btf_id == special_kfunc_list[KF_bpf_rbtree_first];
}

static bool is_bpf_graph_api_kfunc(u32 btf_id)
{
	return is_bpf_list_api_kfunc(btf_id) || is_bpf_rbtree_api_kfunc(btf_id) ||
	       btf_id == special_kfunc_list[KF_bpf_refcount_acquire_impl];
}

static bool is_sync_callback_calling_kfunc(u32 btf_id)
{
	return btf_id == special_kfunc_list[KF_bpf_rbtree_add_impl];
}

static bool is_async_callback_calling_kfunc(u32 btf_id)
{
	return btf_id == special_kfunc_list[KF_bpf_wq_set_callback_impl];
}

static bool is_bpf_throw_kfunc(struct bpf_insn *insn)
{
	return bpf_pseudo_kfunc_call(insn) && insn->off == 0 &&
	       insn->imm == special_kfunc_list[KF_bpf_throw];
}

static bool is_bpf_wq_set_callback_impl_kfunc(u32 btf_id)
{
	return btf_id == special_kfunc_list[KF_bpf_wq_set_callback_impl];
}

static bool is_callback_calling_kfunc(u32 btf_id)
{
	return is_sync_callback_calling_kfunc(btf_id) ||
	       is_async_callback_calling_kfunc(btf_id);
}

static bool is_rbtree_lock_required_kfunc(u32 btf_id)
{
	return is_bpf_rbtree_api_kfunc(btf_id);
}

static bool check_kfunc_is_graph_root_api(struct bpf_verifier_env *env,
					  enum btf_field_type head_field_type,
					  u32 kfunc_btf_id)
{
	bool ret;

	switch (head_field_type) {
	case BPF_LIST_HEAD:
		ret = is_bpf_list_api_kfunc(kfunc_btf_id);
		break;
	case BPF_RB_ROOT:
		ret = is_bpf_rbtree_api_kfunc(kfunc_btf_id);
		break;
	default:
		verbose(env, "verifier internal error: unexpected graph root argument type %s\n",
			btf_field_type_name(head_field_type));
		return false;
	}

	if (!ret)
		verbose(env, "verifier internal error: %s head arg for unknown kfunc\n",
			btf_field_type_name(head_field_type));
	return ret;
}

static bool check_kfunc_is_graph_node_api(struct bpf_verifier_env *env,
					  enum btf_field_type node_field_type,
					  u32 kfunc_btf_id)
{
	bool ret;

	switch (node_field_type) {
	case BPF_LIST_NODE:
		ret = (kfunc_btf_id == special_kfunc_list[KF_bpf_list_push_front_impl] ||
		       kfunc_btf_id == special_kfunc_list[KF_bpf_list_push_back_impl]);
		break;
	case BPF_RB_NODE:
		ret = (kfunc_btf_id == special_kfunc_list[KF_bpf_rbtree_remove] ||
		       kfunc_btf_id == special_kfunc_list[KF_bpf_rbtree_add_impl]);
		break;
	default:
		verbose(env, "verifier internal error: unexpected graph node argument type %s\n",
			btf_field_type_name(node_field_type));
		return false;
	}

	if (!ret)
		verbose(env, "verifier internal error: %s node arg for unknown kfunc\n",
			btf_field_type_name(node_field_type));
	return ret;
}

static int
__process_kf_arg_ptr_to_graph_root(struct bpf_verifier_env *env,
				   struct bpf_reg_state *reg, u32 regno,
				   struct bpf_kfunc_call_arg_meta *meta,
				   enum btf_field_type head_field_type,
				   struct btf_field **head_field)
{
	const char *head_type_name;
	struct btf_field *field;
	struct btf_record *rec;
	u32 head_off;

	if (meta->btf != btf_vmlinux) {
		verbose(env, "verifier internal error: unexpected btf mismatch in kfunc call\n");
		return -EFAULT;
	}

	if (!check_kfunc_is_graph_root_api(env, head_field_type, meta->func_id))
		return -EFAULT;

	head_type_name = btf_field_type_name(head_field_type);
	if (!tnum_is_const(reg->var_off)) {
		verbose(env,
			"R%d doesn't have constant offset. %s has to be at the constant offset\n",
			regno, head_type_name);
		return -EINVAL;
	}

	rec = reg_btf_record(reg);
	head_off = reg->off + reg->var_off.value;
	field = btf_record_find(rec, head_off, head_field_type);
	if (!field) {
		verbose(env, "%s not found at offset=%u\n", head_type_name, head_off);
		return -EINVAL;
	}

	/* All functions require bpf_list_head to be protected using a bpf_spin_lock */
	if (check_reg_allocation_locked(env, reg)) {
		verbose(env, "bpf_spin_lock at off=%d must be held for %s\n",
			rec->spin_lock_off, head_type_name);
		return -EINVAL;
	}

	if (*head_field) {
		verbose(env, "verifier internal error: repeating %s arg\n", head_type_name);
		return -EFAULT;
	}
	*head_field = field;
	return 0;
}

static int process_kf_arg_ptr_to_list_head(struct bpf_verifier_env *env,
					   struct bpf_reg_state *reg, u32 regno,
					   struct bpf_kfunc_call_arg_meta *meta)
{
	return __process_kf_arg_ptr_to_graph_root(env, reg, regno, meta, BPF_LIST_HEAD,
							  &meta->arg_list_head.field);
}

static int process_kf_arg_ptr_to_rbtree_root(struct bpf_verifier_env *env,
					     struct bpf_reg_state *reg, u32 regno,
					     struct bpf_kfunc_call_arg_meta *meta)
{
	return __process_kf_arg_ptr_to_graph_root(env, reg, regno, meta, BPF_RB_ROOT,
							  &meta->arg_rbtree_root.field);
}

static int
__process_kf_arg_ptr_to_graph_node(struct bpf_verifier_env *env,
				   struct bpf_reg_state *reg, u32 regno,
				   struct bpf_kfunc_call_arg_meta *meta,
				   enum btf_field_type head_field_type,
				   enum btf_field_type node_field_type,
				   struct btf_field **node_field)
{
	const char *node_type_name;
	const struct btf_type *et, *t;
	struct btf_field *field;
	u32 node_off;

	if (meta->btf != btf_vmlinux) {
		verbose(env, "verifier internal error: unexpected btf mismatch in kfunc call\n");
		return -EFAULT;
	}

	if (!check_kfunc_is_graph_node_api(env, node_field_type, meta->func_id))
		return -EFAULT;

	node_type_name = btf_field_type_name(node_field_type);
	if (!tnum_is_const(reg->var_off)) {
		verbose(env,
			"R%d doesn't have constant offset. %s has to be at the constant offset\n",
			regno, node_type_name);
		return -EINVAL;
	}

	node_off = reg->off + reg->var_off.value;
	field = reg_find_field_offset(reg, node_off, node_field_type);
	if (!field) {
		verbose(env, "%s not found at offset=%u\n", node_type_name, node_off);
		return -EINVAL;
	}

	field = *node_field;

	et = btf_type_by_id(field->graph_root.btf, field->graph_root.value_btf_id);
	t = btf_type_by_id(reg->btf, reg->btf_id);
	if (!btf_struct_ids_match(&env->log, reg->btf, reg->btf_id, 0, field->graph_root.btf,
				  field->graph_root.value_btf_id, true)) {
		verbose(env, "operation on %s expects arg#1 %s at offset=%d "
			"in struct %s, but arg is at offset=%d in struct %s\n",
			btf_field_type_name(head_field_type),
			btf_field_type_name(node_field_type),
			field->graph_root.node_offset,
			btf_name_by_offset(field->graph_root.btf, et->name_off),
			node_off, btf_name_by_offset(reg->btf, t->name_off));
		return -EINVAL;
	}
	meta->arg_btf = reg->btf;
	meta->arg_btf_id = reg->btf_id;

	if (node_off != field->graph_root.node_offset) {
		verbose(env, "arg#1 offset=%d, but expected %s at offset=%d in struct %s\n",
			node_off, btf_field_type_name(node_field_type),
			field->graph_root.node_offset,
			btf_name_by_offset(field->graph_root.btf, et->name_off));
		return -EINVAL;
	}

	return 0;
}

static int process_kf_arg_ptr_to_list_node(struct bpf_verifier_env *env,
					   struct bpf_reg_state *reg, u32 regno,
					   struct bpf_kfunc_call_arg_meta *meta)
{
	return __process_kf_arg_ptr_to_graph_node(env, reg, regno, meta,
						  BPF_LIST_HEAD, BPF_LIST_NODE,
						  &meta->arg_list_head.field);
}

static int process_kf_arg_ptr_to_rbtree_node(struct bpf_verifier_env *env,
					     struct bpf_reg_state *reg, u32 regno,
					     struct bpf_kfunc_call_arg_meta *meta)
{
	return __process_kf_arg_ptr_to_graph_node(env, reg, regno, meta,
						  BPF_RB_ROOT, BPF_RB_NODE,
						  &meta->arg_rbtree_root.field);
}

/*
 * css_task iter allowlist is needed to avoid dead locking on css_set_lock.
 * LSM hooks and iters (both sleepable and non-sleepable) are safe.
 * Any sleepable progs are also safe since bpf_check_attach_target() enforce
 * them can only be attached to some specific hook points.
 */
static bool check_css_task_iter_allowlist(struct bpf_verifier_env *env)
{
	enum bpf_prog_type prog_type = resolve_prog_type(env->prog);

	switch (prog_type) {
	case BPF_PROG_TYPE_LSM:
		return true;
	case BPF_PROG_TYPE_TRACING:
		if (env->prog->expected_attach_type == BPF_TRACE_ITER)
			return true;
		fallthrough;
	default:
		return in_sleepable(env);
	}
}

static int check_kfunc_args(struct bpf_verifier_env *env, struct bpf_kfunc_call_arg_meta *meta,
			    int insn_idx)
{
	const char *func_name = meta->func_name, *ref_tname;
	const struct btf *btf = meta->btf;
	const struct btf_param *args;
	struct btf_record *rec;
	u32 i, nargs;
	int ret;

	args = (const struct btf_param *)(meta->func_proto + 1);
	nargs = btf_type_vlen(meta->func_proto);
	if (nargs > MAX_BPF_FUNC_REG_ARGS) {
		verbose(env, "Function %s has %d > %d args\n", func_name, nargs,
			MAX_BPF_FUNC_REG_ARGS);
		return -EINVAL;
	}

	/* Check that BTF function arguments match actual types that the
	 * verifier sees.
	 */
	for (i = 0; i < nargs; i++) {
		struct bpf_reg_state *regs = cur_regs(env), *reg = &regs[i + 1];
		const struct btf_type *t, *ref_t, *resolve_ret;
		enum bpf_arg_type arg_type = ARG_DONTCARE;
		u32 regno = i + 1, ref_id, type_size;
		bool is_ret_buf_sz = false;
		bool mask = false;
		int kf_arg_type;

		t = btf_type_skip_modifiers(btf, args[i].type, NULL);

		if (is_kfunc_arg_ignore(btf, &args[i]))
			continue;

		if (btf_type_is_scalar(t)) {
			if (reg->type != SCALAR_VALUE) {
				verbose(env, "R%d is not a scalar\n", regno);
				return -EINVAL;
			}

			if (is_kfunc_arg_constant(meta->btf, &args[i])) {
				if (meta->arg_constant.found) {
					verbose(env, "verifier internal error: only one constant argument permitted\n");
					return -EFAULT;
				}
				if (!tnum_is_const(reg->var_off)) {
					verbose(env, "R%d must be a known constant\n", regno);
					return -EINVAL;
				}
				ret = mark_chain_precision(env, regno);
				if (ret < 0)
					return ret;
				meta->arg_constant.found = true;
				meta->arg_constant.value = reg->var_off.value;
			} else if (is_kfunc_arg_scalar_with_name(btf, &args[i], "rdonly_buf_size")) {
				meta->r0_rdonly = true;
				is_ret_buf_sz = true;
			} else if (is_kfunc_arg_scalar_with_name(btf, &args[i], "rdwr_buf_size")) {
				is_ret_buf_sz = true;
			}

			if (is_ret_buf_sz) {
				if (meta->r0_size) {
					verbose(env, "2 or more rdonly/rdwr_buf_size parameters for kfunc");
					return -EINVAL;
				}

				if (!tnum_is_const(reg->var_off)) {
					verbose(env, "R%d is not a const\n", regno);
					return -EINVAL;
				}

				meta->r0_size = reg->var_off.value;
				ret = mark_chain_precision(env, regno);
				if (ret)
					return ret;
			}
			continue;
		}

		if (!btf_type_is_ptr(t)) {
			verbose(env, "Unrecognized arg#%d type %s\n", i, btf_type_str(t));
			return -EINVAL;
		}

		mask = mask_raw_tp_reg(env, reg);
		if ((is_kfunc_trusted_args(meta) || is_kfunc_rcu(meta)) &&
		    (register_is_null(reg) || type_may_be_null(reg->type)) &&
			!is_kfunc_arg_nullable(meta->btf, &args[i])) {
			verbose(env, "Possibly NULL pointer passed to trusted arg%d\n", i);
			unmask_raw_tp_reg(reg, mask);
			return -EACCES;
		}
		unmask_raw_tp_reg(reg, mask);

		if (reg->ref_obj_id) {
			if (is_kfunc_release(meta) && meta->ref_obj_id) {
				verbose(env, "verifier internal error: more than one arg with ref_obj_id R%d %u %u\n",
					regno, reg->ref_obj_id,
					meta->ref_obj_id);
				return -EFAULT;
			}
			meta->ref_obj_id = reg->ref_obj_id;
			if (is_kfunc_release(meta))
				meta->release_regno = regno;
		}

		ref_t = btf_type_skip_modifiers(btf, t->type, &ref_id);
		ref_tname = btf_name_by_offset(btf, ref_t->name_off);

		kf_arg_type = get_kfunc_ptr_arg_type(env, meta, t, ref_t, ref_tname, args, i, nargs);
		if (kf_arg_type < 0)
			return kf_arg_type;

		switch (kf_arg_type) {
		case KF_ARG_PTR_TO_NULL:
			continue;
		case KF_ARG_PTR_TO_MAP:
			if (!reg->map_ptr) {
				verbose(env, "pointer in R%d isn't map pointer\n", regno);
				return -EINVAL;
			}
			if (meta->map.ptr && reg->map_ptr->record->wq_off >= 0) {
				/* Use map_uid (which is unique id of inner map) to reject:
				 * inner_map1 = bpf_map_lookup_elem(outer_map, key1)
				 * inner_map2 = bpf_map_lookup_elem(outer_map, key2)
				 * if (inner_map1 && inner_map2) {
				 *     wq = bpf_map_lookup_elem(inner_map1);
				 *     if (wq)
				 *         // mismatch would have been allowed
				 *         bpf_wq_init(wq, inner_map2);
				 * }
				 *
				 * Comparing map_ptr is enough to distinguish normal and outer maps.
				 */
				if (meta->map.ptr != reg->map_ptr ||
				    meta->map.uid != reg->map_uid) {
					verbose(env,
						"workqueue pointer in R1 map_uid=%d doesn't match map pointer in R2 map_uid=%d\n",
						meta->map.uid, reg->map_uid);
					return -EINVAL;
				}
			}
			meta->map.ptr = reg->map_ptr;
			meta->map.uid = reg->map_uid;
			fallthrough;
		case KF_ARG_PTR_TO_ALLOC_BTF_ID:
		case KF_ARG_PTR_TO_BTF_ID:
			if (!is_kfunc_trusted_args(meta) && !is_kfunc_rcu(meta))
				break;

			/* Allow passing maybe NULL raw_tp arguments to
			 * kfuncs for compatibility. Don't apply this to
			 * arguments with ref_obj_id > 0.
			 */
			mask = mask_raw_tp_reg(env, reg);
			if (!is_trusted_reg(reg)) {
				if (!is_kfunc_rcu(meta)) {
					verbose(env, "R%d must be referenced or trusted\n", regno);
					unmask_raw_tp_reg(reg, mask);
					return -EINVAL;
				}
				if (!is_rcu_reg(reg)) {
					verbose(env, "R%d must be a rcu pointer\n", regno);
					unmask_raw_tp_reg(reg, mask);
					return -EINVAL;
				}
			}
			unmask_raw_tp_reg(reg, mask);
			fallthrough;
		case KF_ARG_PTR_TO_CTX:
		case KF_ARG_PTR_TO_DYNPTR:
		case KF_ARG_PTR_TO_ITER:
		case KF_ARG_PTR_TO_LIST_HEAD:
		case KF_ARG_PTR_TO_LIST_NODE:
		case KF_ARG_PTR_TO_RB_ROOT:
		case KF_ARG_PTR_TO_RB_NODE:
		case KF_ARG_PTR_TO_MEM:
		case KF_ARG_PTR_TO_MEM_SIZE:
		case KF_ARG_PTR_TO_CALLBACK:
		case KF_ARG_PTR_TO_REFCOUNTED_KPTR:
		case KF_ARG_PTR_TO_CONST_STR:
		case KF_ARG_PTR_TO_WORKQUEUE:
			break;
		default:
			WARN_ON_ONCE(1);
			return -EFAULT;
		}

		if (is_kfunc_release(meta) && reg->ref_obj_id)
			arg_type |= OBJ_RELEASE;
		mask = mask_raw_tp_reg(env, reg);
		ret = check_func_arg_reg_off(env, reg, regno, arg_type);
		unmask_raw_tp_reg(reg, mask);
		if (ret < 0)
			return ret;

		switch (kf_arg_type) {
		case KF_ARG_PTR_TO_CTX:
			if (reg->type != PTR_TO_CTX) {
				verbose(env, "arg#%d expected pointer to ctx, but got %s\n",
					i, reg_type_str(env, reg->type));
				return -EINVAL;
			}

			if (meta->func_id == special_kfunc_list[KF_bpf_cast_to_kern_ctx]) {
				ret = get_kern_ctx_btf_id(&env->log, resolve_prog_type(env->prog));
				if (ret < 0)
					return -EINVAL;
				meta->ret_btf_id  = ret;
			}
			break;
		case KF_ARG_PTR_TO_ALLOC_BTF_ID:
			if (reg->type == (PTR_TO_BTF_ID | MEM_ALLOC)) {
				if (meta->func_id != special_kfunc_list[KF_bpf_obj_drop_impl]) {
					verbose(env, "arg#%d expected for bpf_obj_drop_impl()\n", i);
					return -EINVAL;
				}
			} else if (reg->type == (PTR_TO_BTF_ID | MEM_ALLOC | MEM_PERCPU)) {
				if (meta->func_id != special_kfunc_list[KF_bpf_percpu_obj_drop_impl]) {
					verbose(env, "arg#%d expected for bpf_percpu_obj_drop_impl()\n", i);
					return -EINVAL;
				}
			} else {
				verbose(env, "arg#%d expected pointer to allocated object\n", i);
				return -EINVAL;
			}
			if (!reg->ref_obj_id) {
				verbose(env, "allocated object must be referenced\n");
				return -EINVAL;
			}
			if (meta->btf == btf_vmlinux) {
				meta->arg_btf = reg->btf;
				meta->arg_btf_id = reg->btf_id;
			}
			break;
		case KF_ARG_PTR_TO_DYNPTR:
		{
			enum bpf_arg_type dynptr_arg_type = ARG_PTR_TO_DYNPTR;
			int clone_ref_obj_id = 0;

			if (reg->type == CONST_PTR_TO_DYNPTR)
				dynptr_arg_type |= MEM_RDONLY;

			if (is_kfunc_arg_uninit(btf, &args[i]))
				dynptr_arg_type |= MEM_UNINIT;

			if (meta->func_id == special_kfunc_list[KF_bpf_dynptr_from_skb]) {
				dynptr_arg_type |= DYNPTR_TYPE_SKB;
			} else if (meta->func_id == special_kfunc_list[KF_bpf_dynptr_from_xdp]) {
				dynptr_arg_type |= DYNPTR_TYPE_XDP;
			} else if (meta->func_id == special_kfunc_list[KF_bpf_dynptr_clone] &&
				   (dynptr_arg_type & MEM_UNINIT)) {
				enum bpf_dynptr_type parent_type = meta->initialized_dynptr.type;

				if (parent_type == BPF_DYNPTR_TYPE_INVALID) {
					verbose(env, "verifier internal error: no dynptr type for parent of clone\n");
					return -EFAULT;
				}

				dynptr_arg_type |= (unsigned int)get_dynptr_type_flag(parent_type);
				clone_ref_obj_id = meta->initialized_dynptr.ref_obj_id;
				if (dynptr_type_refcounted(parent_type) && !clone_ref_obj_id) {
					verbose(env, "verifier internal error: missing ref obj id for parent of clone\n");
					return -EFAULT;
				}
			}

			ret = process_dynptr_func(env, regno, insn_idx, dynptr_arg_type, clone_ref_obj_id);
			if (ret < 0)
				return ret;

			if (!(dynptr_arg_type & MEM_UNINIT)) {
				int id = dynptr_id(env, reg);

				if (id < 0) {
					verbose(env, "verifier internal error: failed to obtain dynptr id\n");
					return id;
				}
				meta->initialized_dynptr.id = id;
				meta->initialized_dynptr.type = dynptr_get_type(env, reg);
				meta->initialized_dynptr.ref_obj_id = dynptr_ref_obj_id(env, reg);
			}

			break;
		}
		case KF_ARG_PTR_TO_ITER:
			if (meta->func_id == special_kfunc_list[KF_bpf_iter_css_task_new]) {
				if (!check_css_task_iter_allowlist(env)) {
					verbose(env, "css_task_iter is only allowed in bpf_lsm, bpf_iter and sleepable progs\n");
					return -EINVAL;
				}
			}
			ret = process_iter_arg(env, regno, insn_idx, meta);
			if (ret < 0)
				return ret;
			break;
		case KF_ARG_PTR_TO_LIST_HEAD:
			if (reg->type != PTR_TO_MAP_VALUE &&
			    reg->type != (PTR_TO_BTF_ID | MEM_ALLOC)) {
				verbose(env, "arg#%d expected pointer to map value or allocated object\n", i);
				return -EINVAL;
			}
			if (reg->type == (PTR_TO_BTF_ID | MEM_ALLOC) && !reg->ref_obj_id) {
				verbose(env, "allocated object must be referenced\n");
				return -EINVAL;
			}
			ret = process_kf_arg_ptr_to_list_head(env, reg, regno, meta);
			if (ret < 0)
				return ret;
			break;
		case KF_ARG_PTR_TO_RB_ROOT:
			if (reg->type != PTR_TO_MAP_VALUE &&
			    reg->type != (PTR_TO_BTF_ID | MEM_ALLOC)) {
				verbose(env, "arg#%d expected pointer to map value or allocated object\n", i);
				return -EINVAL;
			}
			if (reg->type == (PTR_TO_BTF_ID | MEM_ALLOC) && !reg->ref_obj_id) {
				verbose(env, "allocated object must be referenced\n");
				return -EINVAL;
			}
			ret = process_kf_arg_ptr_to_rbtree_root(env, reg, regno, meta);
			if (ret < 0)
				return ret;
			break;
		case KF_ARG_PTR_TO_LIST_NODE:
			if (reg->type != (PTR_TO_BTF_ID | MEM_ALLOC)) {
				verbose(env, "arg#%d expected pointer to allocated object\n", i);
				return -EINVAL;
			}
			if (!reg->ref_obj_id) {
				verbose(env, "allocated object must be referenced\n");
				return -EINVAL;
			}
			ret = process_kf_arg_ptr_to_list_node(env, reg, regno, meta);
			if (ret < 0)
				return ret;
			break;
		case KF_ARG_PTR_TO_RB_NODE:
			if (meta->func_id == special_kfunc_list[KF_bpf_rbtree_remove]) {
				if (!type_is_non_owning_ref(reg->type) || reg->ref_obj_id) {
					verbose(env, "rbtree_remove node input must be non-owning ref\n");
					return -EINVAL;
				}
				if (in_rbtree_lock_required_cb(env)) {
					verbose(env, "rbtree_remove not allowed in rbtree cb\n");
					return -EINVAL;
				}
			} else {
				if (reg->type != (PTR_TO_BTF_ID | MEM_ALLOC)) {
					verbose(env, "arg#%d expected pointer to allocated object\n", i);
					return -EINVAL;
				}
				if (!reg->ref_obj_id) {
					verbose(env, "allocated object must be referenced\n");
					return -EINVAL;
				}
			}

			ret = process_kf_arg_ptr_to_rbtree_node(env, reg, regno, meta);
			if (ret < 0)
				return ret;
			break;
		case KF_ARG_PTR_TO_MAP:
			/* If argument has '__map' suffix expect 'struct bpf_map *' */
			ref_id = *reg2btf_ids[CONST_PTR_TO_MAP];
			ref_t = btf_type_by_id(btf_vmlinux, ref_id);
			ref_tname = btf_name_by_offset(btf, ref_t->name_off);
			fallthrough;
		case KF_ARG_PTR_TO_BTF_ID:
			mask = mask_raw_tp_reg(env, reg);
			/* Only base_type is checked, further checks are done here */
			if ((base_type(reg->type) != PTR_TO_BTF_ID ||
			     (bpf_type_has_unsafe_modifiers(reg->type) && !is_rcu_reg(reg))) &&
			    !reg2btf_ids[base_type(reg->type)]) {
				verbose(env, "arg#%d is %s ", i, reg_type_str(env, reg->type));
				verbose(env, "expected %s or socket\n",
					reg_type_str(env, base_type(reg->type) |
							  (type_flag(reg->type) & BPF_REG_TRUSTED_MODIFIERS)));
				unmask_raw_tp_reg(reg, mask);
				return -EINVAL;
			}
			ret = process_kf_arg_ptr_to_btf_id(env, reg, ref_t, ref_tname, ref_id, meta, i);
			unmask_raw_tp_reg(reg, mask);
			if (ret < 0)
				return ret;
			break;
		case KF_ARG_PTR_TO_MEM:
			resolve_ret = btf_resolve_size(btf, ref_t, &type_size);
			if (IS_ERR(resolve_ret)) {
				verbose(env, "arg#%d reference type('%s %s') size cannot be determined: %ld\n",
					i, btf_type_str(ref_t), ref_tname, PTR_ERR(resolve_ret));
				return -EINVAL;
			}
			ret = check_mem_reg(env, reg, regno, type_size);
			if (ret < 0)
				return ret;
			break;
		case KF_ARG_PTR_TO_MEM_SIZE:
		{
			struct bpf_reg_state *buff_reg = &regs[regno];
			const struct btf_param *buff_arg = &args[i];
			struct bpf_reg_state *size_reg = &regs[regno + 1];
			const struct btf_param *size_arg = &args[i + 1];

			if (!register_is_null(buff_reg) || !is_kfunc_arg_optional(meta->btf, buff_arg)) {
				ret = check_kfunc_mem_size_reg(env, size_reg, regno + 1);
				if (ret < 0) {
					verbose(env, "arg#%d arg#%d memory, len pair leads to invalid memory access\n", i, i + 1);
					return ret;
				}
			}

			if (is_kfunc_arg_const_mem_size(meta->btf, size_arg, size_reg)) {
				if (meta->arg_constant.found) {
					verbose(env, "verifier internal error: only one constant argument permitted\n");
					return -EFAULT;
				}
				if (!tnum_is_const(size_reg->var_off)) {
					verbose(env, "R%d must be a known constant\n", regno + 1);
					return -EINVAL;
				}
				meta->arg_constant.found = true;
				meta->arg_constant.value = size_reg->var_off.value;
			}

			/* Skip next '__sz' or '__szk' argument */
			i++;
			break;
		}
		case KF_ARG_PTR_TO_CALLBACK:
			if (reg->type != PTR_TO_FUNC) {
				verbose(env, "arg%d expected pointer to func\n", i);
				return -EINVAL;
			}
			meta->subprogno = reg->subprogno;
			break;
		case KF_ARG_PTR_TO_REFCOUNTED_KPTR:
			if (!type_is_ptr_alloc_obj(reg->type)) {
				verbose(env, "arg#%d is neither owning or non-owning ref\n", i);
				return -EINVAL;
			}
			if (!type_is_non_owning_ref(reg->type))
				meta->arg_owning_ref = true;

			rec = reg_btf_record(reg);
			if (!rec) {
				verbose(env, "verifier internal error: Couldn't find btf_record\n");
				return -EFAULT;
			}

			if (rec->refcount_off < 0) {
				verbose(env, "arg#%d doesn't point to a type with bpf_refcount field\n", i);
				return -EINVAL;
			}

			meta->arg_btf = reg->btf;
			meta->arg_btf_id = reg->btf_id;
			break;
		case KF_ARG_PTR_TO_CONST_STR:
			if (reg->type != PTR_TO_MAP_VALUE) {
				verbose(env, "arg#%d doesn't point to a const string\n", i);
				return -EINVAL;
			}
			ret = check_reg_const_str(env, reg, regno);
			if (ret)
				return ret;
			break;
		case KF_ARG_PTR_TO_WORKQUEUE:
			if (reg->type != PTR_TO_MAP_VALUE) {
				verbose(env, "arg#%d doesn't point to a map value\n", i);
				return -EINVAL;
			}
			ret = process_wq_func(env, regno, meta);
			if (ret < 0)
				return ret;
			break;
		}
	}

	if (is_kfunc_release(meta) && !meta->release_regno) {
		verbose(env, "release kernel function %s expects refcounted PTR_TO_BTF_ID\n",
			func_name);
		return -EINVAL;
	}

	return 0;
}

static int fetch_kfunc_meta(struct bpf_verifier_env *env,
			    struct bpf_insn *insn,
			    struct bpf_kfunc_call_arg_meta *meta,
			    const char **kfunc_name)
{
	const struct btf_type *func, *func_proto;
	u32 func_id, *kfunc_flags;
	const char *func_name;
	struct btf *desc_btf;

	if (kfunc_name)
		*kfunc_name = NULL;

	if (!insn->imm)
		return -EINVAL;

	desc_btf = find_kfunc_desc_btf(env, insn->off);
	if (IS_ERR(desc_btf))
		return PTR_ERR(desc_btf);

	func_id = insn->imm;
	func = btf_type_by_id(desc_btf, func_id);
	func_name = btf_name_by_offset(desc_btf, func->name_off);
	if (kfunc_name)
		*kfunc_name = func_name;
	func_proto = btf_type_by_id(desc_btf, func->type);

	kfunc_flags = btf_kfunc_id_set_contains(desc_btf, func_id, env->prog);
	if (!kfunc_flags) {
		return -EACCES;
	}

	memset(meta, 0, sizeof(*meta));
	meta->btf = desc_btf;
	meta->func_id = func_id;
	meta->kfunc_flags = *kfunc_flags;
	meta->func_proto = func_proto;
	meta->func_name = func_name;

	return 0;
}

static int check_return_code(struct bpf_verifier_env *env, int regno, const char *reg_name);

static int check_kfunc_call(struct bpf_verifier_env *env, struct bpf_insn *insn,
			    int *insn_idx_p)
{
	bool sleepable, rcu_lock, rcu_unlock, preempt_disable, preempt_enable;
	u32 i, nargs, ptr_type_id, release_ref_obj_id;
	struct bpf_reg_state *regs = cur_regs(env);
	const char *func_name, *ptr_type_name;
	const struct btf_type *t, *ptr_type;
	struct bpf_kfunc_call_arg_meta meta;
	struct bpf_insn_aux_data *insn_aux;
	int err, insn_idx = *insn_idx_p;
	const struct btf_param *args;
	const struct btf_type *ret_t;
	struct btf *desc_btf;

	/* skip for now, but return error when we find this in fixup_kfunc_call */
	if (!insn->imm)
		return 0;

	err = fetch_kfunc_meta(env, insn, &meta, &func_name);
	if (err == -EACCES && func_name)
		verbose(env, "calling kernel function %s is not allowed\n", func_name);
	if (err)
		return err;
	desc_btf = meta.btf;
	insn_aux = &env->insn_aux_data[insn_idx];

	insn_aux->is_iter_next = is_iter_next_kfunc(&meta);

	if (is_kfunc_destructive(&meta) && !capable(CAP_SYS_BOOT)) {
		verbose(env, "destructive kfunc calls require CAP_SYS_BOOT capability\n");
		return -EACCES;
	}

	sleepable = is_kfunc_sleepable(&meta);
	if (sleepable && !in_sleepable(env)) {
		verbose(env, "program must be sleepable to call sleepable kfunc %s\n", func_name);
		return -EACCES;
	}

	/* Check the arguments */
	err = check_kfunc_args(env, &meta, insn_idx);
	if (err < 0)
		return err;

	if (meta.func_id == special_kfunc_list[KF_bpf_rbtree_add_impl]) {
		err = push_callback_call(env, insn, insn_idx, meta.subprogno,
					 set_rbtree_add_callback_state);
		if (err) {
			verbose(env, "kfunc %s#%d failed callback verification\n",
				func_name, meta.func_id);
			return err;
		}
	}

	if (meta.func_id == special_kfunc_list[KF_bpf_session_cookie]) {
		meta.r0_size = sizeof(u64);
		meta.r0_rdonly = false;
	}

	if (is_bpf_wq_set_callback_impl_kfunc(meta.func_id)) {
		err = push_callback_call(env, insn, insn_idx, meta.subprogno,
					 set_timer_callback_state);
		if (err) {
			verbose(env, "kfunc %s#%d failed callback verification\n",
				func_name, meta.func_id);
			return err;
		}
	}

	rcu_lock = is_kfunc_bpf_rcu_read_lock(&meta);
	rcu_unlock = is_kfunc_bpf_rcu_read_unlock(&meta);

	preempt_disable = is_kfunc_bpf_preempt_disable(&meta);
	preempt_enable = is_kfunc_bpf_preempt_enable(&meta);

	if (env->cur_state->active_rcu_lock) {
		struct bpf_func_state *state;
		struct bpf_reg_state *reg;
		u32 clear_mask = (1 << STACK_SPILL) | (1 << STACK_ITER);

		if (in_rbtree_lock_required_cb(env) && (rcu_lock || rcu_unlock)) {
			verbose(env, "Calling bpf_rcu_read_{lock,unlock} in unnecessary rbtree callback\n");
			return -EACCES;
		}

		if (rcu_lock) {
			verbose(env, "nested rcu read lock (kernel function %s)\n", func_name);
			return -EINVAL;
		} else if (rcu_unlock) {
			bpf_for_each_reg_in_vstate_mask(env->cur_state, state, reg, clear_mask, ({
				if (reg->type & MEM_RCU) {
					reg->type &= ~(MEM_RCU | PTR_MAYBE_NULL);
					reg->type |= PTR_UNTRUSTED;
				}
			}));
			env->cur_state->active_rcu_lock = false;
		} else if (sleepable) {
			verbose(env, "kernel func %s is sleepable within rcu_read_lock region\n", func_name);
			return -EACCES;
		}
	} else if (rcu_lock) {
		env->cur_state->active_rcu_lock = true;
	} else if (rcu_unlock) {
		verbose(env, "unmatched rcu read unlock (kernel function %s)\n", func_name);
		return -EINVAL;
	}

	if (env->cur_state->active_preempt_lock) {
		if (preempt_disable) {
			env->cur_state->active_preempt_lock++;
		} else if (preempt_enable) {
			env->cur_state->active_preempt_lock--;
		} else if (sleepable) {
			verbose(env, "kernel func %s is sleepable within non-preemptible region\n", func_name);
			return -EACCES;
		}
	} else if (preempt_disable) {
		env->cur_state->active_preempt_lock++;
	} else if (preempt_enable) {
		verbose(env, "unmatched attempt to enable preemption (kernel function %s)\n", func_name);
		return -EINVAL;
	}

	/* In case of release function, we get register number of refcounted
	 * PTR_TO_BTF_ID in bpf_kfunc_arg_meta, do the release now.
	 */
	if (meta.release_regno) {
		err = release_reference(env, regs[meta.release_regno].ref_obj_id);
		if (err) {
			verbose(env, "kfunc %s#%d reference has not been acquired before\n",
				func_name, meta.func_id);
			return err;
		}
	}

	if (meta.func_id == special_kfunc_list[KF_bpf_list_push_front_impl] ||
	    meta.func_id == special_kfunc_list[KF_bpf_list_push_back_impl] ||
	    meta.func_id == special_kfunc_list[KF_bpf_rbtree_add_impl]) {
		release_ref_obj_id = regs[BPF_REG_2].ref_obj_id;
		insn_aux->insert_off = regs[BPF_REG_2].off;
		insn_aux->kptr_struct_meta = btf_find_struct_meta(meta.arg_btf, meta.arg_btf_id);
		err = ref_convert_owning_non_owning(env, release_ref_obj_id);
		if (err) {
			verbose(env, "kfunc %s#%d conversion of owning ref to non-owning failed\n",
				func_name, meta.func_id);
			return err;
		}

		err = release_reference(env, release_ref_obj_id);
		if (err) {
			verbose(env, "kfunc %s#%d reference has not been acquired before\n",
				func_name, meta.func_id);
			return err;
		}
	}

	if (meta.func_id == special_kfunc_list[KF_bpf_throw]) {
		if (!bpf_jit_supports_exceptions()) {
			verbose(env, "JIT does not support calling kfunc %s#%d\n",
				func_name, meta.func_id);
			return -ENOTSUPP;
		}
		env->seen_exception = true;

		/* In the case of the default callback, the cookie value passed
		 * to bpf_throw becomes the return value of the program.
		 */
		if (!env->exception_callback_subprog) {
			err = check_return_code(env, BPF_REG_1, "R1");
			if (err < 0)
				return err;
		}
	}

	for (i = 0; i < CALLER_SAVED_REGS; i++)
		mark_reg_not_init(env, regs, caller_saved[i]);

	/* Check return type */
	t = btf_type_skip_modifiers(desc_btf, meta.func_proto->type, NULL);

	if (is_kfunc_acquire(&meta) && !btf_type_is_struct_ptr(meta.btf, t)) {
		/* Only exception is bpf_obj_new_impl */
		if (meta.btf != btf_vmlinux ||
		    (meta.func_id != special_kfunc_list[KF_bpf_obj_new_impl] &&
		     meta.func_id != special_kfunc_list[KF_bpf_percpu_obj_new_impl] &&
		     meta.func_id != special_kfunc_list[KF_bpf_refcount_acquire_impl])) {
			verbose(env, "acquire kernel function does not return PTR_TO_BTF_ID\n");
			return -EINVAL;
		}
	}

	if (btf_type_is_scalar(t)) {
		mark_reg_unknown(env, regs, BPF_REG_0);
		mark_btf_func_reg_size(env, BPF_REG_0, t->size);
	} else if (btf_type_is_ptr(t)) {
		ptr_type = btf_type_skip_modifiers(desc_btf, t->type, &ptr_type_id);

		if (meta.btf == btf_vmlinux && btf_id_set_contains(&special_kfunc_set, meta.func_id)) {
			if (meta.func_id == special_kfunc_list[KF_bpf_obj_new_impl] ||
			    meta.func_id == special_kfunc_list[KF_bpf_percpu_obj_new_impl]) {
				struct btf_struct_meta *struct_meta;
				struct btf *ret_btf;
				u32 ret_btf_id;

				if (meta.func_id == special_kfunc_list[KF_bpf_obj_new_impl] && !bpf_global_ma_set)
					return -ENOMEM;

				if (((u64)(u32)meta.arg_constant.value) != meta.arg_constant.value) {
					verbose(env, "local type ID argument must be in range [0, U32_MAX]\n");
					return -EINVAL;
				}

				ret_btf = env->prog->aux->btf;
				ret_btf_id = meta.arg_constant.value;

				/* This may be NULL due to user not supplying a BTF */
				if (!ret_btf) {
					verbose(env, "bpf_obj_new/bpf_percpu_obj_new requires prog BTF\n");
					return -EINVAL;
				}

				ret_t = btf_type_by_id(ret_btf, ret_btf_id);
				if (!ret_t || !__btf_type_is_struct(ret_t)) {
					verbose(env, "bpf_obj_new/bpf_percpu_obj_new type ID argument must be of a struct\n");
					return -EINVAL;
				}

				if (meta.func_id == special_kfunc_list[KF_bpf_percpu_obj_new_impl]) {
					if (ret_t->size > BPF_GLOBAL_PERCPU_MA_MAX_SIZE) {
						verbose(env, "bpf_percpu_obj_new type size (%d) is greater than %d\n",
							ret_t->size, BPF_GLOBAL_PERCPU_MA_MAX_SIZE);
						return -EINVAL;
					}

					if (!bpf_global_percpu_ma_set) {
						mutex_lock(&bpf_percpu_ma_lock);
						if (!bpf_global_percpu_ma_set) {
							/* Charge memory allocated with bpf_global_percpu_ma to
							 * root memcg. The obj_cgroup for root memcg is NULL.
							 */
							err = bpf_mem_alloc_percpu_init(&bpf_global_percpu_ma, NULL);
							if (!err)
								bpf_global_percpu_ma_set = true;
						}
						mutex_unlock(&bpf_percpu_ma_lock);
						if (err)
							return err;
					}

					mutex_lock(&bpf_percpu_ma_lock);
					err = bpf_mem_alloc_percpu_unit_init(&bpf_global_percpu_ma, ret_t->size);
					mutex_unlock(&bpf_percpu_ma_lock);
					if (err)
						return err;
				}

				struct_meta = btf_find_struct_meta(ret_btf, ret_btf_id);
				if (meta.func_id == special_kfunc_list[KF_bpf_percpu_obj_new_impl]) {
					if (!__btf_type_is_scalar_struct(env, ret_btf, ret_t, 0)) {
						verbose(env, "bpf_percpu_obj_new type ID argument must be of a struct of scalars\n");
						return -EINVAL;
					}

					if (struct_meta) {
						verbose(env, "bpf_percpu_obj_new type ID argument must not contain special fields\n");
						return -EINVAL;
					}
				}

				mark_reg_known_zero(env, regs, BPF_REG_0);
				regs[BPF_REG_0].type = PTR_TO_BTF_ID | MEM_ALLOC;
				regs[BPF_REG_0].btf = ret_btf;
				regs[BPF_REG_0].btf_id = ret_btf_id;
				if (meta.func_id == special_kfunc_list[KF_bpf_percpu_obj_new_impl])
					regs[BPF_REG_0].type |= MEM_PERCPU;

				insn_aux->obj_new_size = ret_t->size;
				insn_aux->kptr_struct_meta = struct_meta;
			} else if (meta.func_id == special_kfunc_list[KF_bpf_refcount_acquire_impl]) {
				mark_reg_known_zero(env, regs, BPF_REG_0);
				regs[BPF_REG_0].type = PTR_TO_BTF_ID | MEM_ALLOC;
				regs[BPF_REG_0].btf = meta.arg_btf;
				regs[BPF_REG_0].btf_id = meta.arg_btf_id;

				insn_aux->kptr_struct_meta =
					btf_find_struct_meta(meta.arg_btf,
							     meta.arg_btf_id);
			} else if (meta.func_id == special_kfunc_list[KF_bpf_list_pop_front] ||
				   meta.func_id == special_kfunc_list[KF_bpf_list_pop_back]) {
				struct btf_field *field = meta.arg_list_head.field;

				mark_reg_graph_node(regs, BPF_REG_0, &field->graph_root);
			} else if (meta.func_id == special_kfunc_list[KF_bpf_rbtree_remove] ||
				   meta.func_id == special_kfunc_list[KF_bpf_rbtree_first]) {
				struct btf_field *field = meta.arg_rbtree_root.field;

				mark_reg_graph_node(regs, BPF_REG_0, &field->graph_root);
			} else if (meta.func_id == special_kfunc_list[KF_bpf_cast_to_kern_ctx]) {
				mark_reg_known_zero(env, regs, BPF_REG_0);
				regs[BPF_REG_0].type = PTR_TO_BTF_ID | PTR_TRUSTED;
				regs[BPF_REG_0].btf = desc_btf;
				regs[BPF_REG_0].btf_id = meta.ret_btf_id;
			} else if (meta.func_id == special_kfunc_list[KF_bpf_rdonly_cast]) {
				ret_t = btf_type_by_id(desc_btf, meta.arg_constant.value);
				if (!ret_t || !btf_type_is_struct(ret_t)) {
					verbose(env,
						"kfunc bpf_rdonly_cast type ID argument must be of a struct\n");
					return -EINVAL;
				}

				mark_reg_known_zero(env, regs, BPF_REG_0);
				regs[BPF_REG_0].type = PTR_TO_BTF_ID | PTR_UNTRUSTED;
				regs[BPF_REG_0].btf = desc_btf;
				regs[BPF_REG_0].btf_id = meta.arg_constant.value;
			} else if (meta.func_id == special_kfunc_list[KF_bpf_dynptr_slice] ||
				   meta.func_id == special_kfunc_list[KF_bpf_dynptr_slice_rdwr]) {
				enum bpf_type_flag type_flag = get_dynptr_type_flag(meta.initialized_dynptr.type);

				mark_reg_known_zero(env, regs, BPF_REG_0);

				if (!meta.arg_constant.found) {
					verbose(env, "verifier internal error: bpf_dynptr_slice(_rdwr) no constant size\n");
					return -EFAULT;
				}

				regs[BPF_REG_0].mem_size = meta.arg_constant.value;

				/* PTR_MAYBE_NULL will be added when is_kfunc_ret_null is checked */
				regs[BPF_REG_0].type = PTR_TO_MEM | type_flag;

				if (meta.func_id == special_kfunc_list[KF_bpf_dynptr_slice]) {
					regs[BPF_REG_0].type |= MEM_RDONLY;
				} else {
					/* this will set env->seen_direct_write to true */
					if (!may_access_direct_pkt_data(env, NULL, BPF_WRITE)) {
						verbose(env, "the prog does not allow writes to packet data\n");
						return -EINVAL;
					}
				}

				if (!meta.initialized_dynptr.id) {
					verbose(env, "verifier internal error: no dynptr id\n");
					return -EFAULT;
				}
				regs[BPF_REG_0].dynptr_id = meta.initialized_dynptr.id;

				/* we don't need to set BPF_REG_0's ref obj id
				 * because packet slices are not refcounted (see
				 * dynptr_type_refcounted)
				 */
			} else {
				verbose(env, "kernel function %s unhandled dynamic return type\n",
					meta.func_name);
				return -EFAULT;
			}
		} else if (btf_type_is_void(ptr_type)) {
			/* kfunc returning 'void *' is equivalent to returning scalar */
			mark_reg_unknown(env, regs, BPF_REG_0);
		} else if (!__btf_type_is_struct(ptr_type)) {
			if (!meta.r0_size) {
				__u32 sz;

				if (!IS_ERR(btf_resolve_size(desc_btf, ptr_type, &sz))) {
					meta.r0_size = sz;
					meta.r0_rdonly = true;
				}
			}
			if (!meta.r0_size) {
				ptr_type_name = btf_name_by_offset(desc_btf,
								   ptr_type->name_off);
				verbose(env,
					"kernel function %s returns pointer type %s %s is not supported\n",
					func_name,
					btf_type_str(ptr_type),
					ptr_type_name);
				return -EINVAL;
			}

			mark_reg_known_zero(env, regs, BPF_REG_0);
			regs[BPF_REG_0].type = PTR_TO_MEM;
			regs[BPF_REG_0].mem_size = meta.r0_size;

			if (meta.r0_rdonly)
				regs[BPF_REG_0].type |= MEM_RDONLY;

			/* Ensures we don't access the memory after a release_reference() */
			if (meta.ref_obj_id)
				regs[BPF_REG_0].ref_obj_id = meta.ref_obj_id;
		} else {
			mark_reg_known_zero(env, regs, BPF_REG_0);
			regs[BPF_REG_0].btf = desc_btf;
			regs[BPF_REG_0].type = PTR_TO_BTF_ID;
			regs[BPF_REG_0].btf_id = ptr_type_id;

			if (meta.func_id == special_kfunc_list[KF_bpf_get_kmem_cache])
				regs[BPF_REG_0].type |= PTR_UNTRUSTED;

			if (is_iter_next_kfunc(&meta)) {
				struct bpf_reg_state *cur_iter;

				cur_iter = get_iter_from_state(env->cur_state, &meta);

				if (cur_iter->type & MEM_RCU) /* KF_RCU_PROTECTED */
					regs[BPF_REG_0].type |= MEM_RCU;
				else
					regs[BPF_REG_0].type |= PTR_TRUSTED;
			}
		}

		if (is_kfunc_ret_null(&meta)) {
			regs[BPF_REG_0].type |= PTR_MAYBE_NULL;
			/* For mark_ptr_or_null_reg, see 93c230e3f5bd6 */
			regs[BPF_REG_0].id = ++env->id_gen;
		}
		mark_btf_func_reg_size(env, BPF_REG_0, sizeof(void *));
		if (is_kfunc_acquire(&meta)) {
			int id = acquire_reference_state(env, insn_idx);

			if (id < 0)
				return id;
			if (is_kfunc_ret_null(&meta))
				regs[BPF_REG_0].id = id;
			regs[BPF_REG_0].ref_obj_id = id;
		} else if (meta.func_id == special_kfunc_list[KF_bpf_rbtree_first]) {
			ref_set_non_owning(env, &regs[BPF_REG_0]);
		}

		if (reg_may_point_to_spin_lock(&regs[BPF_REG_0]) && !regs[BPF_REG_0].id)
			regs[BPF_REG_0].id = ++env->id_gen;
	} else if (btf_type_is_void(t)) {
		if (meta.btf == btf_vmlinux && btf_id_set_contains(&special_kfunc_set, meta.func_id)) {
			if (meta.func_id == special_kfunc_list[KF_bpf_obj_drop_impl] ||
			    meta.func_id == special_kfunc_list[KF_bpf_percpu_obj_drop_impl]) {
				insn_aux->kptr_struct_meta =
					btf_find_struct_meta(meta.arg_btf,
							     meta.arg_btf_id);
			}
		}
	}

	nargs = btf_type_vlen(meta.func_proto);
	args = (const struct btf_param *)(meta.func_proto + 1);
	for (i = 0; i < nargs; i++) {
		u32 regno = i + 1;

		t = btf_type_skip_modifiers(desc_btf, args[i].type, NULL);
		if (btf_type_is_ptr(t))
			mark_btf_func_reg_size(env, regno, sizeof(void *));
		else
			/* scalar. ensured by btf_check_kfunc_arg_match() */
			mark_btf_func_reg_size(env, regno, t->size);
	}

	if (is_iter_next_kfunc(&meta)) {
		err = process_iter_next_call(env, insn_idx, &meta);
		if (err)
			return err;
	}

	return 0;
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
			reg_type_str(env, type), val);
		return false;
	}

	if (reg->off >= BPF_MAX_VAR_OFF || reg->off <= -BPF_MAX_VAR_OFF) {
		verbose(env, "%s pointer offset %d is not allowed\n",
			reg_type_str(env, type), reg->off);
		return false;
	}

	if (smin == S64_MIN) {
		verbose(env, "math between %s pointer and register with unbounded min value is not allowed\n",
			reg_type_str(env, type));
		return false;
	}

	if (smin >= BPF_MAX_VAR_OFF || smin <= -BPF_MAX_VAR_OFF) {
		verbose(env, "value %lld makes %s pointer be out of bounds\n",
			smin, reg_type_str(env, type));
		return false;
	}

	return true;
}

enum {
	REASON_BOUNDS	= -1,
	REASON_TYPE	= -2,
	REASON_PATHS	= -3,
	REASON_LIMIT	= -4,
	REASON_STACK	= -5,
};

static int retrieve_ptr_limit(const struct bpf_reg_state *ptr_reg,
			      u32 *alu_limit, bool mask_to_left)
{
	u32 max = 0, ptr_limit = 0;

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

	/* Corresponding fixup done in do_misc_fixups(). */
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

struct bpf_sanitize_info {
	struct bpf_insn_aux_data aux;
	bool mask_to_left;
};

static struct bpf_verifier_state *
sanitize_speculative_path(struct bpf_verifier_env *env,
			  const struct bpf_insn *insn,
			  u32 next_idx, u32 curr_idx)
{
	struct bpf_verifier_state *branch;
	struct bpf_reg_state *regs;

	branch = push_stack(env, next_idx, curr_idx, true);
	if (branch && insn) {
		regs = branch->frame[branch->curframe]->regs;
		if (BPF_SRC(insn->code) == BPF_K) {
			mark_reg_unknown(env, regs, insn->dst_reg);
		} else if (BPF_SRC(insn->code) == BPF_X) {
			mark_reg_unknown(env, regs, insn->dst_reg);
			mark_reg_unknown(env, regs, insn->src_reg);
		}
	}
	return branch;
}

static int sanitize_ptr_alu(struct bpf_verifier_env *env,
			    struct bpf_insn *insn,
			    const struct bpf_reg_state *ptr_reg,
			    const struct bpf_reg_state *off_reg,
			    struct bpf_reg_state *dst_reg,
			    struct bpf_sanitize_info *info,
			    const bool commit_window)
{
	struct bpf_insn_aux_data *aux = commit_window ? cur_aux(env) : &info->aux;
	struct bpf_verifier_state *vstate = env->cur_state;
	bool off_is_imm = tnum_is_const(off_reg->var_off);
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

	if (!commit_window) {
		if (!tnum_is_const(off_reg->var_off) &&
		    (off_reg->smin_value < 0) != (off_reg->smax_value < 0))
			return REASON_BOUNDS;

		info->mask_to_left = (opcode == BPF_ADD &&  off_is_neg) ||
				     (opcode == BPF_SUB && !off_is_neg);
	}

	err = retrieve_ptr_limit(ptr_reg, &alu_limit, info->mask_to_left);
	if (err < 0)
		return err;

	if (commit_window) {
		/* In commit phase we narrow the masking window based on
		 * the observed pointer move after the simulated operation.
		 */
		alu_state = info->aux.alu_state;
		alu_limit = abs(info->aux.alu_limit - alu_limit);
	} else {
		alu_state  = off_is_neg ? BPF_ALU_NEG_VALUE : 0;
		alu_state |= off_is_imm ? BPF_ALU_IMMEDIATE : 0;
		alu_state |= ptr_is_dst_reg ?
			     BPF_ALU_SANITIZE_SRC : BPF_ALU_SANITIZE_DST;

		/* Limit pruning on unknown scalars to enable deep search for
		 * potential masking differences from other program paths.
		 */
		if (!off_is_imm)
			env->explore_alu_limits = true;
	}

	err = update_alu_sanitation_state(aux, alu_state, alu_limit);
	if (err < 0)
		return err;
do_sim:
	/* If we're in commit phase, we're done here given we already
	 * pushed the truncated dst_reg into the speculative verification
	 * stack.
	 *
	 * Also, when register is a known constant, we rewrite register-based
	 * operation to immediate-based, and thus do not need masking (and as
	 * a consequence, do not need to simulate the zero-truncation either).
	 */
	if (commit_window || off_is_imm)
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
		copy_register_state(dst_reg, ptr_reg);
	}
	ret = sanitize_speculative_path(env, NULL, env->insn_idx + 1,
					env->insn_idx);
	if (!ptr_is_dst_reg && ret)
		*dst_reg = tmp;
	return !ret ? REASON_STACK : 0;
}

static void sanitize_mark_insn_seen(struct bpf_verifier_env *env)
{
	struct bpf_verifier_state *vstate = env->cur_state;

	/* If we simulate paths under speculation, we don't update the
	 * insn as 'seen' such that when we verify unreachable paths in
	 * the non-speculative domain, sanitize_dead_code() can still
	 * rewrite/sanitize them.
	 */
	if (!vstate->speculative)
		env->insn_aux_data[env->insn_idx].seen = env->pass_cnt;
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
		if (check_map_access(env, dst, dst_reg->off, 1, false, ACCESS_HELPER)) {
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
				   struct bpf_reg_state *ptr_reg,
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
	struct bpf_sanitize_info info = {};
	u8 opcode = BPF_OP(insn->code);
	u32 dst = insn->dst_reg;
	bool mask;
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

	mask = mask_raw_tp_reg(env, ptr_reg);
	if (ptr_reg->type & PTR_MAYBE_NULL) {
		verbose(env, "R%d pointer arithmetic on %s prohibited, null-check it first\n",
			dst, reg_type_str(env, ptr_reg->type));
		unmask_raw_tp_reg(ptr_reg, mask);
		return -EACCES;
	}
	unmask_raw_tp_reg(ptr_reg, mask);

	switch (base_type(ptr_reg->type)) {
	case PTR_TO_CTX:
	case PTR_TO_MAP_VALUE:
	case PTR_TO_MAP_KEY:
	case PTR_TO_STACK:
	case PTR_TO_PACKET_META:
	case PTR_TO_PACKET:
	case PTR_TO_TP_BUFFER:
	case PTR_TO_BTF_ID:
	case PTR_TO_MEM:
	case PTR_TO_BUF:
	case PTR_TO_FUNC:
	case CONST_PTR_TO_DYNPTR:
		break;
	case PTR_TO_FLOW_KEYS:
		if (known)
			break;
		fallthrough;
	case CONST_PTR_TO_MAP:
		/* smin_val represents the known value */
		if (known && smin_val == 0 && opcode == BPF_ADD)
			break;
		fallthrough;
	default:
		verbose(env, "R%d pointer arithmetic on %s prohibited\n",
			dst, reg_type_str(env, ptr_reg->type));
		return -EACCES;
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
				       &info, false);
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
		if (check_add_overflow(smin_ptr, smin_val, &dst_reg->smin_value) ||
		    check_add_overflow(smax_ptr, smax_val, &dst_reg->smax_value)) {
			dst_reg->smin_value = S64_MIN;
			dst_reg->smax_value = S64_MAX;
		}
		if (check_add_overflow(umin_ptr, umin_val, &dst_reg->umin_value) ||
		    check_add_overflow(umax_ptr, umax_val, &dst_reg->umax_value)) {
			dst_reg->umin_value = 0;
			dst_reg->umax_value = U64_MAX;
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
		if (check_sub_overflow(smin_ptr, smax_val, &dst_reg->smin_value) ||
		    check_sub_overflow(smax_ptr, smin_val, &dst_reg->smax_value)) {
			/* Overflow possible, we know nothing */
			dst_reg->smin_value = S64_MIN;
			dst_reg->smax_value = S64_MAX;
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
	reg_bounds_sync(dst_reg);
	if (sanitize_check_bounds(env, insn, dst_reg) < 0)
		return -EACCES;
	if (sanitize_needed(opcode)) {
		ret = sanitize_ptr_alu(env, insn, dst_reg, off_reg, dst_reg,
				       &info, true);
		if (ret < 0)
			return sanitize_err(env, insn, ret, off_reg, dst_reg);
	}

	return 0;
}

static void scalar32_min_max_add(struct bpf_reg_state *dst_reg,
				 struct bpf_reg_state *src_reg)
{
	s32 *dst_smin = &dst_reg->s32_min_value;
	s32 *dst_smax = &dst_reg->s32_max_value;
	u32 *dst_umin = &dst_reg->u32_min_value;
	u32 *dst_umax = &dst_reg->u32_max_value;

	if (check_add_overflow(*dst_smin, src_reg->s32_min_value, dst_smin) ||
	    check_add_overflow(*dst_smax, src_reg->s32_max_value, dst_smax)) {
		*dst_smin = S32_MIN;
		*dst_smax = S32_MAX;
	}
	if (check_add_overflow(*dst_umin, src_reg->u32_min_value, dst_umin) ||
	    check_add_overflow(*dst_umax, src_reg->u32_max_value, dst_umax)) {
		*dst_umin = 0;
		*dst_umax = U32_MAX;
	}
}

static void scalar_min_max_add(struct bpf_reg_state *dst_reg,
			       struct bpf_reg_state *src_reg)
{
	s64 *dst_smin = &dst_reg->smin_value;
	s64 *dst_smax = &dst_reg->smax_value;
	u64 *dst_umin = &dst_reg->umin_value;
	u64 *dst_umax = &dst_reg->umax_value;

	if (check_add_overflow(*dst_smin, src_reg->smin_value, dst_smin) ||
	    check_add_overflow(*dst_smax, src_reg->smax_value, dst_smax)) {
		*dst_smin = S64_MIN;
		*dst_smax = S64_MAX;
	}
	if (check_add_overflow(*dst_umin, src_reg->umin_value, dst_umin) ||
	    check_add_overflow(*dst_umax, src_reg->umax_value, dst_umax)) {
		*dst_umin = 0;
		*dst_umax = U64_MAX;
	}
}

static void scalar32_min_max_sub(struct bpf_reg_state *dst_reg,
				 struct bpf_reg_state *src_reg)
{
	s32 *dst_smin = &dst_reg->s32_min_value;
	s32 *dst_smax = &dst_reg->s32_max_value;
	u32 umin_val = src_reg->u32_min_value;
	u32 umax_val = src_reg->u32_max_value;

	if (check_sub_overflow(*dst_smin, src_reg->s32_max_value, dst_smin) ||
	    check_sub_overflow(*dst_smax, src_reg->s32_min_value, dst_smax)) {
		/* Overflow possible, we know nothing */
		*dst_smin = S32_MIN;
		*dst_smax = S32_MAX;
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
	s64 *dst_smin = &dst_reg->smin_value;
	s64 *dst_smax = &dst_reg->smax_value;
	u64 umin_val = src_reg->umin_value;
	u64 umax_val = src_reg->umax_value;

	if (check_sub_overflow(*dst_smin, src_reg->smax_value, dst_smin) ||
	    check_sub_overflow(*dst_smax, src_reg->smin_value, dst_smax)) {
		/* Overflow possible, we know nothing */
		*dst_smin = S64_MIN;
		*dst_smax = S64_MAX;
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
	u32 umax_val = src_reg->u32_max_value;

	if (src_known && dst_known) {
		__mark_reg32_known(dst_reg, var32_off.value);
		return;
	}

	/* We get our minimum from the var_off, since that's inherently
	 * bitwise.  Our maximum is the minimum of the operands' maxima.
	 */
	dst_reg->u32_min_value = var32_off.value;
	dst_reg->u32_max_value = min(dst_reg->u32_max_value, umax_val);

	/* Safe to set s32 bounds by casting u32 result into s32 when u32
	 * doesn't cross sign boundary. Otherwise set s32 bounds to unbounded.
	 */
	if ((s32)dst_reg->u32_min_value <= (s32)dst_reg->u32_max_value) {
		dst_reg->s32_min_value = dst_reg->u32_min_value;
		dst_reg->s32_max_value = dst_reg->u32_max_value;
	} else {
		dst_reg->s32_min_value = S32_MIN;
		dst_reg->s32_max_value = S32_MAX;
	}
}

static void scalar_min_max_and(struct bpf_reg_state *dst_reg,
			       struct bpf_reg_state *src_reg)
{
	bool src_known = tnum_is_const(src_reg->var_off);
	bool dst_known = tnum_is_const(dst_reg->var_off);
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

	/* Safe to set s64 bounds by casting u64 result into s64 when u64
	 * doesn't cross sign boundary. Otherwise set s64 bounds to unbounded.
	 */
	if ((s64)dst_reg->umin_value <= (s64)dst_reg->umax_value) {
		dst_reg->smin_value = dst_reg->umin_value;
		dst_reg->smax_value = dst_reg->umax_value;
	} else {
		dst_reg->smin_value = S64_MIN;
		dst_reg->smax_value = S64_MAX;
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
	u32 umin_val = src_reg->u32_min_value;

	if (src_known && dst_known) {
		__mark_reg32_known(dst_reg, var32_off.value);
		return;
	}

	/* We get our maximum from the var_off, and our minimum is the
	 * maximum of the operands' minima
	 */
	dst_reg->u32_min_value = max(dst_reg->u32_min_value, umin_val);
	dst_reg->u32_max_value = var32_off.value | var32_off.mask;

	/* Safe to set s32 bounds by casting u32 result into s32 when u32
	 * doesn't cross sign boundary. Otherwise set s32 bounds to unbounded.
	 */
	if ((s32)dst_reg->u32_min_value <= (s32)dst_reg->u32_max_value) {
		dst_reg->s32_min_value = dst_reg->u32_min_value;
		dst_reg->s32_max_value = dst_reg->u32_max_value;
	} else {
		dst_reg->s32_min_value = S32_MIN;
		dst_reg->s32_max_value = S32_MAX;
	}
}

static void scalar_min_max_or(struct bpf_reg_state *dst_reg,
			      struct bpf_reg_state *src_reg)
{
	bool src_known = tnum_is_const(src_reg->var_off);
	bool dst_known = tnum_is_const(dst_reg->var_off);
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

	/* Safe to set s64 bounds by casting u64 result into s64 when u64
	 * doesn't cross sign boundary. Otherwise set s64 bounds to unbounded.
	 */
	if ((s64)dst_reg->umin_value <= (s64)dst_reg->umax_value) {
		dst_reg->smin_value = dst_reg->umin_value;
		dst_reg->smax_value = dst_reg->umax_value;
	} else {
		dst_reg->smin_value = S64_MIN;
		dst_reg->smax_value = S64_MAX;
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

	if (src_known && dst_known) {
		__mark_reg32_known(dst_reg, var32_off.value);
		return;
	}

	/* We get both minimum and maximum from the var32_off. */
	dst_reg->u32_min_value = var32_off.value;
	dst_reg->u32_max_value = var32_off.value | var32_off.mask;

	/* Safe to set s32 bounds by casting u32 result into s32 when u32
	 * doesn't cross sign boundary. Otherwise set s32 bounds to unbounded.
	 */
	if ((s32)dst_reg->u32_min_value <= (s32)dst_reg->u32_max_value) {
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

	if (src_known && dst_known) {
		/* dst_reg->var_off.value has been updated earlier */
		__mark_reg_known(dst_reg, dst_reg->var_off.value);
		return;
	}

	/* We get both minimum and maximum from the var_off. */
	dst_reg->umin_value = dst_reg->var_off.value;
	dst_reg->umax_value = dst_reg->var_off.value | dst_reg->var_off.mask;

	/* Safe to set s64 bounds by casting u64 result into s64 when u64
	 * doesn't cross sign boundary. Otherwise set s64 bounds to unbounded.
	 */
	if ((s64)dst_reg->umin_value <= (s64)dst_reg->umax_value) {
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

static bool is_safe_to_compute_dst_reg_range(struct bpf_insn *insn,
					     const struct bpf_reg_state *src_reg)
{
	bool src_is_const = false;
	u64 insn_bitness = (BPF_CLASS(insn->code) == BPF_ALU64) ? 64 : 32;

	if (insn_bitness == 32) {
		if (tnum_subreg_is_const(src_reg->var_off)
		    && src_reg->s32_min_value == src_reg->s32_max_value
		    && src_reg->u32_min_value == src_reg->u32_max_value)
			src_is_const = true;
	} else {
		if (tnum_is_const(src_reg->var_off)
		    && src_reg->smin_value == src_reg->smax_value
		    && src_reg->umin_value == src_reg->umax_value)
			src_is_const = true;
	}

	switch (BPF_OP(insn->code)) {
	case BPF_ADD:
	case BPF_SUB:
	case BPF_AND:
	case BPF_XOR:
	case BPF_OR:
	case BPF_MUL:
		return true;

	/* Shift operators range is only computable if shift dimension operand
	 * is a constant. Shifts greater than 31 or 63 are undefined. This
	 * includes shifts by a negative number.
	 */
	case BPF_LSH:
	case BPF_RSH:
	case BPF_ARSH:
		return (src_is_const && src_reg->umax_value < insn_bitness);
	default:
		return false;
	}
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
	u8 opcode = BPF_OP(insn->code);
	bool alu32 = (BPF_CLASS(insn->code) != BPF_ALU64);
	int ret;

	if (!is_safe_to_compute_dst_reg_range(insn, &src_reg)) {
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
		if (alu32)
			scalar32_min_max_lsh(dst_reg, &src_reg);
		else
			scalar_min_max_lsh(dst_reg, &src_reg);
		break;
	case BPF_RSH:
		if (alu32)
			scalar32_min_max_rsh(dst_reg, &src_reg);
		else
			scalar_min_max_rsh(dst_reg, &src_reg);
		break;
	case BPF_ARSH:
		if (alu32)
			scalar32_min_max_arsh(dst_reg, &src_reg);
		else
			scalar_min_max_arsh(dst_reg, &src_reg);
		break;
	default:
		break;
	}

	/* ALU32 ops are zero extended into 64bit register */
	if (alu32)
		zext_32_to_64(dst_reg);
	reg_bounds_sync(dst_reg);
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
	bool alu32 = (BPF_CLASS(insn->code) != BPF_ALU64);
	u8 opcode = BPF_OP(insn->code);
	int err;

	dst_reg = &regs[insn->dst_reg];
	src_reg = NULL;

	if (dst_reg->type == PTR_TO_ARENA) {
		struct bpf_insn_aux_data *aux = cur_aux(env);

		if (BPF_CLASS(insn->code) == BPF_ALU64)
			/*
			 * 32-bit operations zero upper bits automatically.
			 * 64-bit operations need to be converted to 32.
			 */
			aux->needs_zext = true;

		/* Any arithmetic operations are allowed on arena pointers */
		return 0;
	}

	if (dst_reg->type != SCALAR_VALUE)
		ptr_reg = dst_reg;

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
		} else if (dst_reg->precise) {
			/* if dst_reg is precise, src_reg should be precise as well */
			err = mark_chain_precision(env, insn->src_reg);
			if (err)
				return err;
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
		print_verifier_state(env, state, true);
		verbose(env, "verifier internal error: unexpected ptr_reg\n");
		return -EINVAL;
	}
	if (WARN_ON(!src_reg)) {
		print_verifier_state(env, state, true);
		verbose(env, "verifier internal error: no src_reg\n");
		return -EINVAL;
	}
	err = adjust_scalar_min_max_vals(env, insn, dst_reg, *src_reg);
	if (err)
		return err;
	/*
	 * Compilers can generate the code
	 * r1 = r2
	 * r1 += 0x1
	 * if r2 < 1000 goto ...
	 * use r1 in memory access
	 * So for 64-bit alu remember constant delta between r2 and r1 and
	 * update r1 after 'if' condition.
	 */
	if (env->bpf_capable &&
	    BPF_OP(insn->code) == BPF_ADD && !alu32 &&
	    dst_reg->id && is_reg_const(src_reg, false)) {
		u64 val = reg_const_value(src_reg, false);

		if ((dst_reg->id & BPF_ADD_CONST) ||
		    /* prevent overflow in sync_linked_regs() later */
		    val > (u32)S32_MAX) {
			/*
			 * If the register already went through rX += val
			 * we cannot accumulate another val into rx->off.
			 */
			dst_reg->off = 0;
			dst_reg->id = 0;
		} else {
			dst_reg->id |= BPF_ADD_CONST;
			dst_reg->off = val;
		}
	} else {
		/*
		 * Make sure ID is cleared otherwise dst_reg min/max could be
		 * incorrectly propagated into other registers by sync_linked_regs()
		 */
		dst_reg->id = 0;
	}
	return 0;
}

/* check validity of 32-bit and 64-bit arithmetic operations */
static int check_alu_op(struct bpf_verifier_env *env, struct bpf_insn *insn)
{
	struct bpf_reg_state *regs = cur_regs(env);
	u8 opcode = BPF_OP(insn->code);
	int err;

	if (opcode == BPF_END || opcode == BPF_NEG) {
		if (opcode == BPF_NEG) {
			if (BPF_SRC(insn->code) != BPF_K ||
			    insn->src_reg != BPF_REG_0 ||
			    insn->off != 0 || insn->imm != 0) {
				verbose(env, "BPF_NEG uses reserved fields\n");
				return -EINVAL;
			}
		} else {
			if (insn->src_reg != BPF_REG_0 || insn->off != 0 ||
			    (insn->imm != 16 && insn->imm != 32 && insn->imm != 64) ||
			    (BPF_CLASS(insn->code) == BPF_ALU64 &&
			     BPF_SRC(insn->code) != BPF_TO_LE)) {
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
			if (BPF_CLASS(insn->code) == BPF_ALU) {
				if ((insn->off != 0 && insn->off != 8 && insn->off != 16) ||
				    insn->imm) {
					verbose(env, "BPF_MOV uses reserved fields\n");
					return -EINVAL;
				}
			} else if (insn->off == BPF_ADDR_SPACE_CAST) {
				if (insn->imm != 1 && insn->imm != 1u << 16) {
					verbose(env, "addr_space_cast insn can only convert between address space 1 and 0\n");
					return -EINVAL;
				}
				if (!env->prog->aux->arena) {
					verbose(env, "addr_space_cast insn can only be used in a program that has an associated arena\n");
					return -EINVAL;
				}
			} else {
				if ((insn->off != 0 && insn->off != 8 && insn->off != 16 &&
				     insn->off != 32) || insn->imm) {
					verbose(env, "BPF_MOV uses reserved fields\n");
					return -EINVAL;
				}
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
				if (insn->imm) {
					/* off == BPF_ADDR_SPACE_CAST */
					mark_reg_unknown(env, regs, insn->dst_reg);
					if (insn->imm == 1) { /* cast from as(1) to as(0) */
						dst_reg->type = PTR_TO_ARENA;
						/* PTR_TO_ARENA is 32-bit */
						dst_reg->subreg_def = env->insn_idx + 1;
					}
				} else if (insn->off == 0) {
					/* case: R1 = R2
					 * copy register state to dest reg
					 */
					assign_scalar_id_before_mov(env, src_reg);
					copy_register_state(dst_reg, src_reg);
					dst_reg->live |= REG_LIVE_WRITTEN;
					dst_reg->subreg_def = DEF_NOT_SUBREG;
				} else {
					/* case: R1 = (s8, s16 s32)R2 */
					if (is_pointer_value(env, insn->src_reg)) {
						verbose(env,
							"R%d sign-extension part of pointer\n",
							insn->src_reg);
						return -EACCES;
					} else if (src_reg->type == SCALAR_VALUE) {
						bool no_sext;

						no_sext = src_reg->umax_value < (1ULL << (insn->off - 1));
						if (no_sext)
							assign_scalar_id_before_mov(env, src_reg);
						copy_register_state(dst_reg, src_reg);
						if (!no_sext)
							dst_reg->id = 0;
						coerce_reg_to_size_sx(dst_reg, insn->off >> 3);
						dst_reg->live |= REG_LIVE_WRITTEN;
						dst_reg->subreg_def = DEF_NOT_SUBREG;
					} else {
						mark_reg_unknown(env, regs, insn->dst_reg);
					}
				}
			} else {
				/* R1 = (u32) R2 */
				if (is_pointer_value(env, insn->src_reg)) {
					verbose(env,
						"R%d partial copy of pointer\n",
						insn->src_reg);
					return -EACCES;
				} else if (src_reg->type == SCALAR_VALUE) {
					if (insn->off == 0) {
						bool is_src_reg_u32 = get_reg_width(src_reg) <= 32;

						if (is_src_reg_u32)
							assign_scalar_id_before_mov(env, src_reg);
						copy_register_state(dst_reg, src_reg);
						/* Make sure ID is cleared if src_reg is not in u32
						 * range otherwise dst_reg min/max could be incorrectly
						 * propagated into src_reg by sync_linked_regs()
						 */
						if (!is_src_reg_u32)
							dst_reg->id = 0;
						dst_reg->live |= REG_LIVE_WRITTEN;
						dst_reg->subreg_def = env->insn_idx + 1;
					} else {
						/* case: W1 = (s8, s16)W2 */
						bool no_sext = src_reg->umax_value < (1ULL << (insn->off - 1));

						if (no_sext)
							assign_scalar_id_before_mov(env, src_reg);
						copy_register_state(dst_reg, src_reg);
						if (!no_sext)
							dst_reg->id = 0;
						dst_reg->live |= REG_LIVE_WRITTEN;
						dst_reg->subreg_def = env->insn_idx + 1;
						coerce_subreg_to_size_sx(dst_reg, insn->off >> 3);
					}
				} else {
					mark_reg_unknown(env, regs,
							 insn->dst_reg);
				}
				zext_32_to_64(dst_reg);
				reg_bounds_sync(dst_reg);
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
			if (insn->imm != 0 || insn->off > 1 ||
			    (insn->off == 1 && opcode != BPF_MOD && opcode != BPF_DIV)) {
				verbose(env, "BPF_ALU uses reserved fields\n");
				return -EINVAL;
			}
			/* check src1 operand */
			err = check_reg_arg(env, insn->src_reg, SRC_OP);
			if (err)
				return err;
		} else {
			if (insn->src_reg != BPF_REG_0 || insn->off > 1 ||
			    (insn->off == 1 && opcode != BPF_MOD && opcode != BPF_DIV)) {
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
		err = err ?: adjust_reg_min_max_vals(env, insn);
		if (err)
			return err;
	}

	return reg_bounds_sanity_check(env, &regs[insn->dst_reg], "alu");
}

static void find_good_pkt_pointers(struct bpf_verifier_state *vstate,
				   struct bpf_reg_state *dst_reg,
				   enum bpf_reg_type type,
				   bool range_right_open)
{
	struct bpf_func_state *state;
	struct bpf_reg_state *reg;
	int new_range;

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
		new_range++;

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
	bpf_for_each_reg_in_vstate(vstate, state, reg, ({
		if (reg->type == type && reg->id == dst_reg->id)
			/* keep the maximum range already checked */
			reg->range = max(reg->range, new_range);
	}));
}

/*
 * <reg1> <op> <reg2>, currently assuming reg2 is a constant
 */
static int is_scalar_branch_taken(struct bpf_reg_state *reg1, struct bpf_reg_state *reg2,
				  u8 opcode, bool is_jmp32)
{
	struct tnum t1 = is_jmp32 ? tnum_subreg(reg1->var_off) : reg1->var_off;
	struct tnum t2 = is_jmp32 ? tnum_subreg(reg2->var_off) : reg2->var_off;
	u64 umin1 = is_jmp32 ? (u64)reg1->u32_min_value : reg1->umin_value;
	u64 umax1 = is_jmp32 ? (u64)reg1->u32_max_value : reg1->umax_value;
	s64 smin1 = is_jmp32 ? (s64)reg1->s32_min_value : reg1->smin_value;
	s64 smax1 = is_jmp32 ? (s64)reg1->s32_max_value : reg1->smax_value;
	u64 umin2 = is_jmp32 ? (u64)reg2->u32_min_value : reg2->umin_value;
	u64 umax2 = is_jmp32 ? (u64)reg2->u32_max_value : reg2->umax_value;
	s64 smin2 = is_jmp32 ? (s64)reg2->s32_min_value : reg2->smin_value;
	s64 smax2 = is_jmp32 ? (s64)reg2->s32_max_value : reg2->smax_value;

	switch (opcode) {
	case BPF_JEQ:
		/* constants, umin/umax and smin/smax checks would be
		 * redundant in this case because they all should match
		 */
		if (tnum_is_const(t1) && tnum_is_const(t2))
			return t1.value == t2.value;
		/* non-overlapping ranges */
		if (umin1 > umax2 || umax1 < umin2)
			return 0;
		if (smin1 > smax2 || smax1 < smin2)
			return 0;
		if (!is_jmp32) {
			/* if 64-bit ranges are inconclusive, see if we can
			 * utilize 32-bit subrange knowledge to eliminate
			 * branches that can't be taken a priori
			 */
			if (reg1->u32_min_value > reg2->u32_max_value ||
			    reg1->u32_max_value < reg2->u32_min_value)
				return 0;
			if (reg1->s32_min_value > reg2->s32_max_value ||
			    reg1->s32_max_value < reg2->s32_min_value)
				return 0;
		}
		break;
	case BPF_JNE:
		/* constants, umin/umax and smin/smax checks would be
		 * redundant in this case because they all should match
		 */
		if (tnum_is_const(t1) && tnum_is_const(t2))
			return t1.value != t2.value;
		/* non-overlapping ranges */
		if (umin1 > umax2 || umax1 < umin2)
			return 1;
		if (smin1 > smax2 || smax1 < smin2)
			return 1;
		if (!is_jmp32) {
			/* if 64-bit ranges are inconclusive, see if we can
			 * utilize 32-bit subrange knowledge to eliminate
			 * branches that can't be taken a priori
			 */
			if (reg1->u32_min_value > reg2->u32_max_value ||
			    reg1->u32_max_value < reg2->u32_min_value)
				return 1;
			if (reg1->s32_min_value > reg2->s32_max_value ||
			    reg1->s32_max_value < reg2->s32_min_value)
				return 1;
		}
		break;
	case BPF_JSET:
		if (!is_reg_const(reg2, is_jmp32)) {
			swap(reg1, reg2);
			swap(t1, t2);
		}
		if (!is_reg_const(reg2, is_jmp32))
			return -1;
		if ((~t1.mask & t1.value) & t2.value)
			return 1;
		if (!((t1.mask | t1.value) & t2.value))
			return 0;
		break;
	case BPF_JGT:
		if (umin1 > umax2)
			return 1;
		else if (umax1 <= umin2)
			return 0;
		break;
	case BPF_JSGT:
		if (smin1 > smax2)
			return 1;
		else if (smax1 <= smin2)
			return 0;
		break;
	case BPF_JLT:
		if (umax1 < umin2)
			return 1;
		else if (umin1 >= umax2)
			return 0;
		break;
	case BPF_JSLT:
		if (smax1 < smin2)
			return 1;
		else if (smin1 >= smax2)
			return 0;
		break;
	case BPF_JGE:
		if (umin1 >= umax2)
			return 1;
		else if (umax1 < umin2)
			return 0;
		break;
	case BPF_JSGE:
		if (smin1 >= smax2)
			return 1;
		else if (smax1 < smin2)
			return 0;
		break;
	case BPF_JLE:
		if (umax1 <= umin2)
			return 1;
		else if (umin1 > umax2)
			return 0;
		break;
	case BPF_JSLE:
		if (smax1 <= smin2)
			return 1;
		else if (smin1 > smax2)
			return 0;
		break;
	}

	return -1;
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

/* compute branch direction of the expression "if (<reg1> opcode <reg2>) goto target;"
 * and return:
 *  1 - branch will be taken and "goto target" will be executed
 *  0 - branch will not be taken and fall-through to next insn
 * -1 - unknown. Example: "if (reg1 < 5)" is unknown when register value
 *      range [0,10]
 */
static int is_branch_taken(struct bpf_reg_state *reg1, struct bpf_reg_state *reg2,
			   u8 opcode, bool is_jmp32)
{
	if (reg_is_pkt_pointer_any(reg1) && reg_is_pkt_pointer_any(reg2) && !is_jmp32)
		return is_pkt_ptr_branch_taken(reg1, reg2, opcode);

	if (__is_pointer_value(false, reg1) || __is_pointer_value(false, reg2)) {
		u64 val;

		/* arrange that reg2 is a scalar, and reg1 is a pointer */
		if (!is_reg_const(reg2, is_jmp32)) {
			opcode = flip_opcode(opcode);
			swap(reg1, reg2);
		}
		/* and ensure that reg2 is a constant */
		if (!is_reg_const(reg2, is_jmp32))
			return -1;

		if (!reg_not_null(reg1))
			return -1;

		/* If pointer is valid tests against zero will fail so we can
		 * use this to direct branch taken.
		 */
		val = reg_const_value(reg2, is_jmp32);
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

	/* now deal with two scalars, but not necessarily constants */
	return is_scalar_branch_taken(reg1, reg2, opcode, is_jmp32);
}

/* Opcode that corresponds to a *false* branch condition.
 * E.g., if r1 < r2, then reverse (false) condition is r1 >= r2
 */
static u8 rev_opcode(u8 opcode)
{
	switch (opcode) {
	case BPF_JEQ:		return BPF_JNE;
	case BPF_JNE:		return BPF_JEQ;
	/* JSET doesn't have it's reverse opcode in BPF, so add
	 * BPF_X flag to denote the reverse of that operation
	 */
	case BPF_JSET:		return BPF_JSET | BPF_X;
	case BPF_JSET | BPF_X:	return BPF_JSET;
	case BPF_JGE:		return BPF_JLT;
	case BPF_JGT:		return BPF_JLE;
	case BPF_JLE:		return BPF_JGT;
	case BPF_JLT:		return BPF_JGE;
	case BPF_JSGE:		return BPF_JSLT;
	case BPF_JSGT:		return BPF_JSLE;
	case BPF_JSLE:		return BPF_JSGT;
	case BPF_JSLT:		return BPF_JSGE;
	default:		return 0;
	}
}

/* Refine range knowledge for <reg1> <op> <reg>2 conditional operation. */
static void regs_refine_cond_op(struct bpf_reg_state *reg1, struct bpf_reg_state *reg2,
				u8 opcode, bool is_jmp32)
{
	struct tnum t;
	u64 val;

	/* In case of GE/GT/SGE/JST, reuse LE/LT/SLE/SLT logic from below */
	switch (opcode) {
	case BPF_JGE:
	case BPF_JGT:
	case BPF_JSGE:
	case BPF_JSGT:
		opcode = flip_opcode(opcode);
		swap(reg1, reg2);
		break;
	default:
		break;
	}

	switch (opcode) {
	case BPF_JEQ:
		if (is_jmp32) {
			reg1->u32_min_value = max(reg1->u32_min_value, reg2->u32_min_value);
			reg1->u32_max_value = min(reg1->u32_max_value, reg2->u32_max_value);
			reg1->s32_min_value = max(reg1->s32_min_value, reg2->s32_min_value);
			reg1->s32_max_value = min(reg1->s32_max_value, reg2->s32_max_value);
			reg2->u32_min_value = reg1->u32_min_value;
			reg2->u32_max_value = reg1->u32_max_value;
			reg2->s32_min_value = reg1->s32_min_value;
			reg2->s32_max_value = reg1->s32_max_value;

			t = tnum_intersect(tnum_subreg(reg1->var_off), tnum_subreg(reg2->var_off));
			reg1->var_off = tnum_with_subreg(reg1->var_off, t);
			reg2->var_off = tnum_with_subreg(reg2->var_off, t);
		} else {
			reg1->umin_value = max(reg1->umin_value, reg2->umin_value);
			reg1->umax_value = min(reg1->umax_value, reg2->umax_value);
			reg1->smin_value = max(reg1->smin_value, reg2->smin_value);
			reg1->smax_value = min(reg1->smax_value, reg2->smax_value);
			reg2->umin_value = reg1->umin_value;
			reg2->umax_value = reg1->umax_value;
			reg2->smin_value = reg1->smin_value;
			reg2->smax_value = reg1->smax_value;

			reg1->var_off = tnum_intersect(reg1->var_off, reg2->var_off);
			reg2->var_off = reg1->var_off;
		}
		break;
	case BPF_JNE:
		if (!is_reg_const(reg2, is_jmp32))
			swap(reg1, reg2);
		if (!is_reg_const(reg2, is_jmp32))
			break;

		/* try to recompute the bound of reg1 if reg2 is a const and
		 * is exactly the edge of reg1.
		 */
		val = reg_const_value(reg2, is_jmp32);
		if (is_jmp32) {
			/* u32_min_value is not equal to 0xffffffff at this point,
			 * because otherwise u32_max_value is 0xffffffff as well,
			 * in such a case both reg1 and reg2 would be constants,
			 * jump would be predicted and reg_set_min_max() won't
			 * be called.
			 *
			 * Same reasoning works for all {u,s}{min,max}{32,64} cases
			 * below.
			 */
			if (reg1->u32_min_value == (u32)val)
				reg1->u32_min_value++;
			if (reg1->u32_max_value == (u32)val)
				reg1->u32_max_value--;
			if (reg1->s32_min_value == (s32)val)
				reg1->s32_min_value++;
			if (reg1->s32_max_value == (s32)val)
				reg1->s32_max_value--;
		} else {
			if (reg1->umin_value == (u64)val)
				reg1->umin_value++;
			if (reg1->umax_value == (u64)val)
				reg1->umax_value--;
			if (reg1->smin_value == (s64)val)
				reg1->smin_value++;
			if (reg1->smax_value == (s64)val)
				reg1->smax_value--;
		}
		break;
	case BPF_JSET:
		if (!is_reg_const(reg2, is_jmp32))
			swap(reg1, reg2);
		if (!is_reg_const(reg2, is_jmp32))
			break;
		val = reg_const_value(reg2, is_jmp32);
		/* BPF_JSET (i.e., TRUE branch, *not* BPF_JSET | BPF_X)
		 * requires single bit to learn something useful. E.g., if we
		 * know that `r1 & 0x3` is true, then which bits (0, 1, or both)
		 * are actually set? We can learn something definite only if
		 * it's a single-bit value to begin with.
		 *
		 * BPF_JSET | BPF_X (i.e., negation of BPF_JSET) doesn't have
		 * this restriction. I.e., !(r1 & 0x3) means neither bit 0 nor
		 * bit 1 is set, which we can readily use in adjustments.
		 */
		if (!is_power_of_2(val))
			break;
		if (is_jmp32) {
			t = tnum_or(tnum_subreg(reg1->var_off), tnum_const(val));
			reg1->var_off = tnum_with_subreg(reg1->var_off, t);
		} else {
			reg1->var_off = tnum_or(reg1->var_off, tnum_const(val));
		}
		break;
	case BPF_JSET | BPF_X: /* reverse of BPF_JSET, see rev_opcode() */
		if (!is_reg_const(reg2, is_jmp32))
			swap(reg1, reg2);
		if (!is_reg_const(reg2, is_jmp32))
			break;
		val = reg_const_value(reg2, is_jmp32);
		if (is_jmp32) {
			t = tnum_and(tnum_subreg(reg1->var_off), tnum_const(~val));
			reg1->var_off = tnum_with_subreg(reg1->var_off, t);
		} else {
			reg1->var_off = tnum_and(reg1->var_off, tnum_const(~val));
		}
		break;
	case BPF_JLE:
		if (is_jmp32) {
			reg1->u32_max_value = min(reg1->u32_max_value, reg2->u32_max_value);
			reg2->u32_min_value = max(reg1->u32_min_value, reg2->u32_min_value);
		} else {
			reg1->umax_value = min(reg1->umax_value, reg2->umax_value);
			reg2->umin_value = max(reg1->umin_value, reg2->umin_value);
		}
		break;
	case BPF_JLT:
		if (is_jmp32) {
			reg1->u32_max_value = min(reg1->u32_max_value, reg2->u32_max_value - 1);
			reg2->u32_min_value = max(reg1->u32_min_value + 1, reg2->u32_min_value);
		} else {
			reg1->umax_value = min(reg1->umax_value, reg2->umax_value - 1);
			reg2->umin_value = max(reg1->umin_value + 1, reg2->umin_value);
		}
		break;
	case BPF_JSLE:
		if (is_jmp32) {
			reg1->s32_max_value = min(reg1->s32_max_value, reg2->s32_max_value);
			reg2->s32_min_value = max(reg1->s32_min_value, reg2->s32_min_value);
		} else {
			reg1->smax_value = min(reg1->smax_value, reg2->smax_value);
			reg2->smin_value = max(reg1->smin_value, reg2->smin_value);
		}
		break;
	case BPF_JSLT:
		if (is_jmp32) {
			reg1->s32_max_value = min(reg1->s32_max_value, reg2->s32_max_value - 1);
			reg2->s32_min_value = max(reg1->s32_min_value + 1, reg2->s32_min_value);
		} else {
			reg1->smax_value = min(reg1->smax_value, reg2->smax_value - 1);
			reg2->smin_value = max(reg1->smin_value + 1, reg2->smin_value);
		}
		break;
	default:
		return;
	}
}

/* Adjusts the register min/max values in the case that the dst_reg and
 * src_reg are both SCALAR_VALUE registers (or we are simply doing a BPF_K
 * check, in which case we have a fake SCALAR_VALUE representing insn->imm).
 * Technically we can do similar adjustments for pointers to the same object,
 * but we don't support that right now.
 */
static int reg_set_min_max(struct bpf_verifier_env *env,
			   struct bpf_reg_state *true_reg1,
			   struct bpf_reg_state *true_reg2,
			   struct bpf_reg_state *false_reg1,
			   struct bpf_reg_state *false_reg2,
			   u8 opcode, bool is_jmp32)
{
	int err;

	/* If either register is a pointer, we can't learn anything about its
	 * variable offset from the compare (unless they were a pointer into
	 * the same object, but we don't bother with that).
	 */
	if (false_reg1->type != SCALAR_VALUE || false_reg2->type != SCALAR_VALUE)
		return 0;

	/* fallthrough (FALSE) branch */
	regs_refine_cond_op(false_reg1, false_reg2, rev_opcode(opcode), is_jmp32);
	reg_bounds_sync(false_reg1);
	reg_bounds_sync(false_reg2);

	/* jump (TRUE) branch */
	regs_refine_cond_op(true_reg1, true_reg2, opcode, is_jmp32);
	reg_bounds_sync(true_reg1);
	reg_bounds_sync(true_reg2);

	err = reg_bounds_sanity_check(env, true_reg1, "true_reg1");
	err = err ?: reg_bounds_sanity_check(env, true_reg2, "true_reg2");
	err = err ?: reg_bounds_sanity_check(env, false_reg1, "false_reg1");
	err = err ?: reg_bounds_sanity_check(env, false_reg2, "false_reg2");
	return err;
}

static void mark_ptr_or_null_reg(struct bpf_func_state *state,
				 struct bpf_reg_state *reg, u32 id,
				 bool is_null)
{
	if (type_may_be_null(reg->type) && reg->id == id &&
	    (is_rcu_reg(reg) || !WARN_ON_ONCE(!reg->id))) {
		/* Old offset (both fixed and variable parts) should have been
		 * known-zero, because we don't allow pointer arithmetic on
		 * pointers that might be NULL. If we see this happening, don't
		 * convert the register.
		 *
		 * But in some cases, some helpers that return local kptrs
		 * advance offset for the returned pointer. In those cases, it
		 * is fine to expect to see reg->off.
		 */
		if (WARN_ON_ONCE(reg->smin_value || reg->smax_value || !tnum_equals_const(reg->var_off, 0)))
			return;
		if (!(type_is_ptr_alloc_obj(reg->type) || type_is_non_owning_ref(reg->type)) &&
		    WARN_ON_ONCE(reg->off))
			return;

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
			 * in release_reference().
			 *
			 * reg->id is still used by spin_lock ptr. Other
			 * than spin_lock ptr type, reg->id can be reset.
			 */
			reg->id = 0;
		}
	}
}

/* The logic is similar to find_good_pkt_pointers(), both could eventually
 * be folded together at some point.
 */
static void mark_ptr_or_null_regs(struct bpf_verifier_state *vstate, u32 regno,
				  bool is_null)
{
	struct bpf_func_state *state = vstate->frame[vstate->curframe];
	struct bpf_reg_state *regs = state->regs, *reg;
	u32 ref_obj_id = regs[regno].ref_obj_id;
	u32 id = regs[regno].id;

	if (ref_obj_id && ref_obj_id == id && is_null)
		/* regs[regno] is in the " == NULL" branch.
		 * No one could have freed the reference state before
		 * doing the NULL check.
		 */
		WARN_ON_ONCE(release_reference_state(state, id));

	bpf_for_each_reg_in_vstate(vstate, state, reg, ({
		mark_ptr_or_null_reg(state, reg, id, is_null);
	}));
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

static void __collect_linked_regs(struct linked_regs *reg_set, struct bpf_reg_state *reg,
				  u32 id, u32 frameno, u32 spi_or_reg, bool is_reg)
{
	struct linked_reg *e;

	if (reg->type != SCALAR_VALUE || (reg->id & ~BPF_ADD_CONST) != id)
		return;

	e = linked_regs_push(reg_set);
	if (e) {
		e->frameno = frameno;
		e->is_reg = is_reg;
		e->regno = spi_or_reg;
	} else {
		reg->id = 0;
	}
}

/* For all R being scalar registers or spilled scalar registers
 * in verifier state, save R in linked_regs if R->id == id.
 * If there are too many Rs sharing same id, reset id for leftover Rs.
 */
static void collect_linked_regs(struct bpf_verifier_state *vstate, u32 id,
				struct linked_regs *linked_regs)
{
	struct bpf_func_state *func;
	struct bpf_reg_state *reg;
	int i, j;

	id = id & ~BPF_ADD_CONST;
	for (i = vstate->curframe; i >= 0; i--) {
		func = vstate->frame[i];
		for (j = 0; j < BPF_REG_FP; j++) {
			reg = &func->regs[j];
			__collect_linked_regs(linked_regs, reg, id, i, j, true);
		}
		for (j = 0; j < func->allocated_stack / BPF_REG_SIZE; j++) {
			if (!is_spilled_reg(&func->stack[j]))
				continue;
			reg = &func->stack[j].spilled_ptr;
			__collect_linked_regs(linked_regs, reg, id, i, j, false);
		}
	}
}

/* For all R in linked_regs, copy known_reg range into R
 * if R->id == known_reg->id.
 */
static void sync_linked_regs(struct bpf_verifier_state *vstate, struct bpf_reg_state *known_reg,
			     struct linked_regs *linked_regs)
{
	struct bpf_reg_state fake_reg;
	struct bpf_reg_state *reg;
	struct linked_reg *e;
	int i;

	for (i = 0; i < linked_regs->cnt; ++i) {
		e = &linked_regs->entries[i];
		reg = e->is_reg ? &vstate->frame[e->frameno]->regs[e->regno]
				: &vstate->frame[e->frameno]->stack[e->spi].spilled_ptr;
		if (reg->type != SCALAR_VALUE || reg == known_reg)
			continue;
		if ((reg->id & ~BPF_ADD_CONST) != (known_reg->id & ~BPF_ADD_CONST))
			continue;
		if ((!(reg->id & BPF_ADD_CONST) && !(known_reg->id & BPF_ADD_CONST)) ||
		    reg->off == known_reg->off) {
			s32 saved_subreg_def = reg->subreg_def;

			copy_register_state(reg, known_reg);
			reg->subreg_def = saved_subreg_def;
		} else {
			s32 saved_subreg_def = reg->subreg_def;
			s32 saved_off = reg->off;

			fake_reg.type = SCALAR_VALUE;
			__mark_reg_known(&fake_reg, (s32)reg->off - (s32)known_reg->off);

			/* reg = known_reg; reg += delta */
			copy_register_state(reg, known_reg);
			/*
			 * Must preserve off, id and add_const flag,
			 * otherwise another sync_linked_regs() will be incorrect.
			 */
			reg->off = saved_off;
			reg->subreg_def = saved_subreg_def;

			scalar32_min_max_add(reg, &fake_reg);
			scalar_min_max_add(reg, &fake_reg);
			reg->var_off = tnum_add(reg->var_off, fake_reg.var_off);
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
	struct bpf_reg_state *eq_branch_regs;
	struct linked_regs linked_regs = {};
	u8 opcode = BPF_OP(insn->code);
	bool is_jmp32;
	int pred = -1;
	int err;

	/* Only conditional jumps are expected to reach here. */
	if (opcode == BPF_JA || opcode > BPF_JCOND) {
		verbose(env, "invalid BPF_JMP/JMP32 opcode %x\n", opcode);
		return -EINVAL;
	}

	if (opcode == BPF_JCOND) {
		struct bpf_verifier_state *cur_st = env->cur_state, *queued_st, *prev_st;
		int idx = *insn_idx;

		if (insn->code != (BPF_JMP | BPF_JCOND) ||
		    insn->src_reg != BPF_MAY_GOTO ||
		    insn->dst_reg || insn->imm || insn->off == 0) {
			verbose(env, "invalid may_goto off %d imm %d\n",
				insn->off, insn->imm);
			return -EINVAL;
		}
		prev_st = find_prev_entry(env, cur_st->parent, idx);

		/* branch out 'fallthrough' insn as a new state to explore */
		queued_st = push_stack(env, idx + 1, idx, false);
		if (!queued_st)
			return -ENOMEM;

		queued_st->may_goto_depth++;
		if (prev_st)
			widen_imprecise_scalars(env, prev_st, queued_st);
		*insn_idx += insn->off;
		return 0;
	}

	/* check src2 operand */
	err = check_reg_arg(env, insn->dst_reg, SRC_OP);
	if (err)
		return err;

	dst_reg = &regs[insn->dst_reg];
	if (BPF_SRC(insn->code) == BPF_X) {
		if (insn->imm != 0) {
			verbose(env, "BPF_JMP/JMP32 uses reserved fields\n");
			return -EINVAL;
		}

		/* check src1 operand */
		err = check_reg_arg(env, insn->src_reg, SRC_OP);
		if (err)
			return err;

		src_reg = &regs[insn->src_reg];
		if (!(reg_is_pkt_pointer_any(dst_reg) && reg_is_pkt_pointer_any(src_reg)) &&
		    is_pointer_value(env, insn->src_reg)) {
			verbose(env, "R%d pointer comparison prohibited\n",
				insn->src_reg);
			return -EACCES;
		}
	} else {
		if (insn->src_reg != BPF_REG_0) {
			verbose(env, "BPF_JMP/JMP32 uses reserved fields\n");
			return -EINVAL;
		}
		src_reg = &env->fake_reg[0];
		memset(src_reg, 0, sizeof(*src_reg));
		src_reg->type = SCALAR_VALUE;
		__mark_reg_known(src_reg, insn->imm);
	}

	is_jmp32 = BPF_CLASS(insn->code) == BPF_JMP32;
	pred = is_branch_taken(dst_reg, src_reg, opcode, is_jmp32);
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
		/* Only follow the goto, ignore fall-through. If needed, push
		 * the fall-through branch for simulation under speculative
		 * execution.
		 */
		if (!env->bypass_spec_v1 &&
		    !sanitize_speculative_path(env, insn, *insn_idx + 1,
					       *insn_idx))
			return -EFAULT;
		if (env->log.level & BPF_LOG_LEVEL)
			print_insn_state(env, this_branch->frame[this_branch->curframe]);
		*insn_idx += insn->off;
		return 0;
	} else if (pred == 0) {
		/* Only follow the fall-through branch, since that's where the
		 * program will go. If needed, push the goto branch for
		 * simulation under speculative execution.
		 */
		if (!env->bypass_spec_v1 &&
		    !sanitize_speculative_path(env, insn,
					       *insn_idx + insn->off + 1,
					       *insn_idx))
			return -EFAULT;
		if (env->log.level & BPF_LOG_LEVEL)
			print_insn_state(env, this_branch->frame[this_branch->curframe]);
		return 0;
	}

	/* Push scalar registers sharing same ID to jump history,
	 * do this before creating 'other_branch', so that both
	 * 'this_branch' and 'other_branch' share this history
	 * if parent state is created.
	 */
	if (BPF_SRC(insn->code) == BPF_X && src_reg->type == SCALAR_VALUE && src_reg->id)
		collect_linked_regs(this_branch, src_reg->id, &linked_regs);
	if (dst_reg->type == SCALAR_VALUE && dst_reg->id)
		collect_linked_regs(this_branch, dst_reg->id, &linked_regs);
	if (linked_regs.cnt > 1) {
		err = push_insn_history(env, this_branch, 0, linked_regs_pack(&linked_regs));
		if (err)
			return err;
	}

	other_branch = push_stack(env, *insn_idx + insn->off + 1, *insn_idx,
				  false);
	if (!other_branch)
		return -EFAULT;
	other_branch_regs = other_branch->frame[other_branch->curframe]->regs;

	if (BPF_SRC(insn->code) == BPF_X) {
		err = reg_set_min_max(env,
				      &other_branch_regs[insn->dst_reg],
				      &other_branch_regs[insn->src_reg],
				      dst_reg, src_reg, opcode, is_jmp32);
	} else /* BPF_SRC(insn->code) == BPF_K */ {
		/* reg_set_min_max() can mangle the fake_reg. Make a copy
		 * so that these are two different memory locations. The
		 * src_reg is not used beyond here in context of K.
		 */
		memcpy(&env->fake_reg[1], &env->fake_reg[0],
		       sizeof(env->fake_reg[0]));
		err = reg_set_min_max(env,
				      &other_branch_regs[insn->dst_reg],
				      &env->fake_reg[0],
				      dst_reg, &env->fake_reg[1],
				      opcode, is_jmp32);
	}
	if (err)
		return err;

	if (BPF_SRC(insn->code) == BPF_X &&
	    src_reg->type == SCALAR_VALUE && src_reg->id &&
	    !WARN_ON_ONCE(src_reg->id != other_branch_regs[insn->src_reg].id)) {
		sync_linked_regs(this_branch, src_reg, &linked_regs);
		sync_linked_regs(other_branch, &other_branch_regs[insn->src_reg], &linked_regs);
	}
	if (dst_reg->type == SCALAR_VALUE && dst_reg->id &&
	    !WARN_ON_ONCE(dst_reg->id != other_branch_regs[insn->dst_reg].id)) {
		sync_linked_regs(this_branch, dst_reg, &linked_regs);
		sync_linked_regs(other_branch, &other_branch_regs[insn->dst_reg], &linked_regs);
	}

	/* if one pointer register is compared to another pointer
	 * register check if PTR_MAYBE_NULL could be lifted.
	 * E.g. register A - maybe null
	 *      register B - not null
	 * for JNE A, B, ... - A is not null in the false branch;
	 * for JEQ A, B, ... - A is not null in the true branch.
	 *
	 * Since PTR_TO_BTF_ID points to a kernel struct that does
	 * not need to be null checked by the BPF program, i.e.,
	 * could be null even without PTR_MAYBE_NULL marking, so
	 * only propagate nullness when neither reg is that type.
	 */
	if (!is_jmp32 && BPF_SRC(insn->code) == BPF_X &&
	    __is_pointer_value(false, src_reg) && __is_pointer_value(false, dst_reg) &&
	    type_may_be_null(src_reg->type) != type_may_be_null(dst_reg->type) &&
	    base_type(src_reg->type) != PTR_TO_BTF_ID &&
	    base_type(dst_reg->type) != PTR_TO_BTF_ID) {
		eq_branch_regs = NULL;
		switch (opcode) {
		case BPF_JEQ:
			eq_branch_regs = other_branch_regs;
			break;
		case BPF_JNE:
			eq_branch_regs = regs;
			break;
		default:
			/* do nothing */
			break;
		}
		if (eq_branch_regs) {
			if (type_may_be_null(src_reg->type))
				mark_ptr_not_null_reg(&eq_branch_regs[insn->src_reg]);
			else
				mark_ptr_not_null_reg(&eq_branch_regs[insn->dst_reg]);
		}
	}

	/* detect if R == 0 where R is returned from bpf_map_lookup_elem().
	 * NOTE: these optimizations below are related with pointer comparison
	 *       which will never be JMP32.
	 */
	if (!is_jmp32 && BPF_SRC(insn->code) == BPF_K &&
	    insn->imm == 0 && (opcode == BPF_JEQ || opcode == BPF_JNE) &&
	    type_may_be_null(dst_reg->type)) {
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
		print_insn_state(env, this_branch->frame[this_branch->curframe]);
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

	/* All special src_reg cases are listed below. From this point onwards
	 * we either succeed and assign a corresponding dst_reg->type after
	 * zeroing the offset, or fail and reject the program.
	 */
	mark_reg_known_zero(env, regs, insn->dst_reg);

	if (insn->src_reg == BPF_PSEUDO_BTF_ID) {
		dst_reg->type = aux->btf_var.reg_type;
		switch (base_type(dst_reg->type)) {
		case PTR_TO_MEM:
			dst_reg->mem_size = aux->btf_var.mem_size;
			break;
		case PTR_TO_BTF_ID:
			dst_reg->btf = aux->btf_var.btf;
			dst_reg->btf_id = aux->btf_var.btf_id;
			break;
		default:
			verbose(env, "bpf verifier is misconfigured\n");
			return -EFAULT;
		}
		return 0;
	}

	if (insn->src_reg == BPF_PSEUDO_FUNC) {
		struct bpf_prog_aux *aux = env->prog->aux;
		u32 subprogno = find_subprog(env,
					     env->insn_idx + insn->imm + 1);

		if (!aux->func_info) {
			verbose(env, "missing btf func_info\n");
			return -EINVAL;
		}
		if (aux->func_info_aux[subprogno].linkage != BTF_FUNC_STATIC) {
			verbose(env, "callback function not static\n");
			return -EINVAL;
		}

		dst_reg->type = PTR_TO_FUNC;
		dst_reg->subprogno = subprogno;
		return 0;
	}

	map = env->used_maps[aux->map_index];
	dst_reg->map_ptr = map;

	if (insn->src_reg == BPF_PSEUDO_MAP_VALUE ||
	    insn->src_reg == BPF_PSEUDO_MAP_IDX_VALUE) {
		if (map->map_type == BPF_MAP_TYPE_ARENA) {
			__mark_reg_unknown(env, dst_reg);
			return 0;
		}
		dst_reg->type = PTR_TO_MAP_VALUE;
		dst_reg->off = aux->map_off;
		WARN_ON_ONCE(map->max_entries != 1);
		/* We want reg->id to be same (0) as map_value is not distinct */
	} else if (insn->src_reg == BPF_PSEUDO_MAP_FD ||
		   insn->src_reg == BPF_PSEUDO_MAP_IDX) {
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
	err = check_resource_leak(env, false, true, "BPF_LD_[ABS|IND]");
	if (err)
		return err;

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

	err = check_ptr_off_reg(env, &regs[ctx_reg], ctx_reg);
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

static int check_return_code(struct bpf_verifier_env *env, int regno, const char *reg_name)
{
	const char *exit_ctx = "At program exit";
	struct tnum enforce_attach_type_range = tnum_unknown;
	const struct bpf_prog *prog = env->prog;
	struct bpf_reg_state *reg;
	struct bpf_retval_range range = retval_range(0, 1);
	enum bpf_prog_type prog_type = resolve_prog_type(env->prog);
	int err;
	struct bpf_func_state *frame = env->cur_state->frame[0];
	const bool is_subprog = frame->subprogno;
	bool return_32bit = false;

	/* LSM and struct_ops func-ptr's return type could be "void" */
	if (!is_subprog || frame->in_exception_callback_fn) {
		switch (prog_type) {
		case BPF_PROG_TYPE_LSM:
			if (prog->expected_attach_type == BPF_LSM_CGROUP)
				/* See below, can be 0 or 0-1 depending on hook. */
				break;
			fallthrough;
		case BPF_PROG_TYPE_STRUCT_OPS:
			if (!prog->aux->attach_func_proto->type)
				return 0;
			break;
		default:
			break;
		}
	}

	/* eBPF calling convention is such that R0 is used
	 * to return the value from eBPF program.
	 * Make sure that it's readable at this time
	 * of bpf_exit, which means that program wrote
	 * something into it earlier
	 */
	err = check_reg_arg(env, regno, SRC_OP);
	if (err)
		return err;

	if (is_pointer_value(env, regno)) {
		verbose(env, "R%d leaks addr as return value\n", regno);
		return -EACCES;
	}

	reg = cur_regs(env) + regno;

	if (frame->in_async_callback_fn) {
		/* enforce return zero from async callbacks like timer */
		exit_ctx = "At async callback return";
		range = retval_range(0, 0);
		goto enforce_retval;
	}

	if (is_subprog && !frame->in_exception_callback_fn) {
		if (reg->type != SCALAR_VALUE) {
			verbose(env, "At subprogram exit the register R%d is not a scalar value (%s)\n",
				regno, reg_type_str(env, reg->type));
			return -EINVAL;
		}
		return 0;
	}

	switch (prog_type) {
	case BPF_PROG_TYPE_CGROUP_SOCK_ADDR:
		if (env->prog->expected_attach_type == BPF_CGROUP_UDP4_RECVMSG ||
		    env->prog->expected_attach_type == BPF_CGROUP_UDP6_RECVMSG ||
		    env->prog->expected_attach_type == BPF_CGROUP_UNIX_RECVMSG ||
		    env->prog->expected_attach_type == BPF_CGROUP_INET4_GETPEERNAME ||
		    env->prog->expected_attach_type == BPF_CGROUP_INET6_GETPEERNAME ||
		    env->prog->expected_attach_type == BPF_CGROUP_UNIX_GETPEERNAME ||
		    env->prog->expected_attach_type == BPF_CGROUP_INET4_GETSOCKNAME ||
		    env->prog->expected_attach_type == BPF_CGROUP_INET6_GETSOCKNAME ||
		    env->prog->expected_attach_type == BPF_CGROUP_UNIX_GETSOCKNAME)
			range = retval_range(1, 1);
		if (env->prog->expected_attach_type == BPF_CGROUP_INET4_BIND ||
		    env->prog->expected_attach_type == BPF_CGROUP_INET6_BIND)
			range = retval_range(0, 3);
		break;
	case BPF_PROG_TYPE_CGROUP_SKB:
		if (env->prog->expected_attach_type == BPF_CGROUP_INET_EGRESS) {
			range = retval_range(0, 3);
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
		range = retval_range(0, 0);
		break;
	case BPF_PROG_TYPE_TRACING:
		switch (env->prog->expected_attach_type) {
		case BPF_TRACE_FENTRY:
		case BPF_TRACE_FEXIT:
			range = retval_range(0, 0);
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
	case BPF_PROG_TYPE_KPROBE:
		switch (env->prog->expected_attach_type) {
		case BPF_TRACE_KPROBE_SESSION:
		case BPF_TRACE_UPROBE_SESSION:
			range = retval_range(0, 1);
			break;
		default:
			return 0;
		}
		break;
	case BPF_PROG_TYPE_SK_LOOKUP:
		range = retval_range(SK_DROP, SK_PASS);
		break;

	case BPF_PROG_TYPE_LSM:
		if (env->prog->expected_attach_type != BPF_LSM_CGROUP) {
			/* no range found, any return value is allowed */
			if (!get_func_retval_range(env->prog, &range))
				return 0;
			/* no restricted range, any return value is allowed */
			if (range.minval == S32_MIN && range.maxval == S32_MAX)
				return 0;
			return_32bit = true;
		} else if (!env->prog->aux->attach_func_proto->type) {
			/* Make sure programs that attach to void
			 * hooks don't try to modify return value.
			 */
			range = retval_range(1, 1);
		}
		break;

	case BPF_PROG_TYPE_NETFILTER:
		range = retval_range(NF_DROP, NF_ACCEPT);
		break;
	case BPF_PROG_TYPE_EXT:
		/* freplace program can return anything as its return value
		 * depends on the to-be-replaced kernel func or bpf program.
		 */
	default:
		return 0;
	}

enforce_retval:
	if (reg->type != SCALAR_VALUE) {
		verbose(env, "%s the register R%d is not a known value (%s)\n",
			exit_ctx, regno, reg_type_str(env, reg->type));
		return -EINVAL;
	}

	err = mark_chain_precision(env, regno);
	if (err)
		return err;

	if (!retval_range_within(range, reg, return_32bit)) {
		verbose_invalid_scalar(env, reg, range, exit_ctx, reg_name);
		if (!is_subprog &&
		    prog->expected_attach_type == BPF_LSM_CGROUP &&
		    prog_type == BPF_PROG_TYPE_LSM &&
		    !prog->aux->attach_func_proto->type)
			verbose(env, "Note, BPF_LSM_CGROUP that attach to void LSM hooks can't modify return value!\n");
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
 * 6            t <- S.peek()
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

static void mark_prune_point(struct bpf_verifier_env *env, int idx)
{
	env->insn_aux_data[idx].prune_point = true;
}

static bool is_prune_point(struct bpf_verifier_env *env, int insn_idx)
{
	return env->insn_aux_data[insn_idx].prune_point;
}

static void mark_force_checkpoint(struct bpf_verifier_env *env, int idx)
{
	env->insn_aux_data[idx].force_checkpoint = true;
}

static bool is_force_checkpoint(struct bpf_verifier_env *env, int insn_idx)
{
	return env->insn_aux_data[insn_idx].force_checkpoint;
}

static void mark_calls_callback(struct bpf_verifier_env *env, int idx)
{
	env->insn_aux_data[idx].calls_callback = true;
}

static bool calls_callback(struct bpf_verifier_env *env, int insn_idx)
{
	return env->insn_aux_data[insn_idx].calls_callback;
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
static int push_insn(int t, int w, int e, struct bpf_verifier_env *env)
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

	if (e == BRANCH) {
		/* mark branch target for state pruning */
		mark_prune_point(env, w);
		mark_jmp_point(env, w);
	}

	if (insn_state[w] == 0) {
		/* tree-edge */
		insn_state[t] = DISCOVERED | e;
		insn_state[w] = DISCOVERED;
		if (env->cfg.cur_stack >= env->prog->len)
			return -E2BIG;
		insn_stack[env->cfg.cur_stack++] = w;
		return KEEP_EXPLORING;
	} else if ((insn_state[w] & 0xF0) == DISCOVERED) {
		if (env->bpf_capable)
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

static int visit_func_call_insn(int t, struct bpf_insn *insns,
				struct bpf_verifier_env *env,
				bool visit_callee)
{
	int ret, insn_sz;

	insn_sz = bpf_is_ldimm64(&insns[t]) ? 2 : 1;
	ret = push_insn(t, t + insn_sz, FALLTHROUGH, env);
	if (ret)
		return ret;

	mark_prune_point(env, t + insn_sz);
	/* when we exit from subprog, we need to record non-linear history */
	mark_jmp_point(env, t + insn_sz);

	if (visit_callee) {
		mark_prune_point(env, t);
		ret = push_insn(t, t + insns[t].imm + 1, BRANCH, env);
	}
	return ret;
}

/* Bitmask with 1s for all caller saved registers */
#define ALL_CALLER_SAVED_REGS ((1u << CALLER_SAVED_REGS) - 1)

/* Return a bitmask specifying which caller saved registers are
 * clobbered by a call to a helper *as if* this helper follows
 * bpf_fastcall contract:
 * - includes R0 if function is non-void;
 * - includes R1-R5 if corresponding parameter has is described
 *   in the function prototype.
 */
static u32 helper_fastcall_clobber_mask(const struct bpf_func_proto *fn)
{
	u32 mask;
	int i;

	mask = 0;
	if (fn->ret_type != RET_VOID)
		mask |= BIT(BPF_REG_0);
	for (i = 0; i < ARRAY_SIZE(fn->arg_type); ++i)
		if (fn->arg_type[i] != ARG_DONTCARE)
			mask |= BIT(BPF_REG_1 + i);
	return mask;
}

/* True if do_misc_fixups() replaces calls to helper number 'imm',
 * replacement patch is presumed to follow bpf_fastcall contract
 * (see mark_fastcall_pattern_for_call() below).
 */
static bool verifier_inlines_helper_call(struct bpf_verifier_env *env, s32 imm)
{
	switch (imm) {
#ifdef CONFIG_X86_64
	case BPF_FUNC_get_smp_processor_id:
		return env->prog->jit_requested && bpf_jit_supports_percpu_insn();
#endif
	default:
		return false;
	}
}

/* Same as helper_fastcall_clobber_mask() but for kfuncs, see comment above */
static u32 kfunc_fastcall_clobber_mask(struct bpf_kfunc_call_arg_meta *meta)
{
	u32 vlen, i, mask;

	vlen = btf_type_vlen(meta->func_proto);
	mask = 0;
	if (!btf_type_is_void(btf_type_by_id(meta->btf, meta->func_proto->type)))
		mask |= BIT(BPF_REG_0);
	for (i = 0; i < vlen; ++i)
		mask |= BIT(BPF_REG_1 + i);
	return mask;
}

/* Same as verifier_inlines_helper_call() but for kfuncs, see comment above */
static bool is_fastcall_kfunc_call(struct bpf_kfunc_call_arg_meta *meta)
{
	return meta->kfunc_flags & KF_FASTCALL;
}

/* LLVM define a bpf_fastcall function attribute.
 * This attribute means that function scratches only some of
 * the caller saved registers defined by ABI.
 * For BPF the set of such registers could be defined as follows:
 * - R0 is scratched only if function is non-void;
 * - R1-R5 are scratched only if corresponding parameter type is defined
 *   in the function prototype.
 *
 * The contract between kernel and clang allows to simultaneously use
 * such functions and maintain backwards compatibility with old
 * kernels that don't understand bpf_fastcall calls:
 *
 * - for bpf_fastcall calls clang allocates registers as-if relevant r0-r5
 *   registers are not scratched by the call;
 *
 * - as a post-processing step, clang visits each bpf_fastcall call and adds
 *   spill/fill for every live r0-r5;
 *
 * - stack offsets used for the spill/fill are allocated as lowest
 *   stack offsets in whole function and are not used for any other
 *   purposes;
 *
 * - when kernel loads a program, it looks for such patterns
 *   (bpf_fastcall function surrounded by spills/fills) and checks if
 *   spill/fill stack offsets are used exclusively in fastcall patterns;
 *
 * - if so, and if verifier or current JIT inlines the call to the
 *   bpf_fastcall function (e.g. a helper call), kernel removes unnecessary
 *   spill/fill pairs;
 *
 * - when old kernel loads a program, presence of spill/fill pairs
 *   keeps BPF program valid, albeit slightly less efficient.
 *
 * For example:
 *
 *   r1 = 1;
 *   r2 = 2;
 *   *(u64 *)(r10 - 8)  = r1;            r1 = 1;
 *   *(u64 *)(r10 - 16) = r2;            r2 = 2;
 *   call %[to_be_inlined]         -->   call %[to_be_inlined]
 *   r2 = *(u64 *)(r10 - 16);            r0 = r1;
 *   r1 = *(u64 *)(r10 - 8);             r0 += r2;
 *   r0 = r1;                            exit;
 *   r0 += r2;
 *   exit;
 *
 * The purpose of mark_fastcall_pattern_for_call is to:
 * - look for such patterns;
 * - mark spill and fill instructions in env->insn_aux_data[*].fastcall_pattern;
 * - mark set env->insn_aux_data[*].fastcall_spills_num for call instruction;
 * - update env->subprog_info[*]->fastcall_stack_off to find an offset
 *   at which bpf_fastcall spill/fill stack slots start;
 * - update env->subprog_info[*]->keep_fastcall_stack.
 *
 * The .fastcall_pattern and .fastcall_stack_off are used by
 * check_fastcall_stack_contract() to check if every stack access to
 * fastcall spill/fill stack slot originates from spill/fill
 * instructions, members of fastcall patterns.
 *
 * If such condition holds true for a subprogram, fastcall patterns could
 * be rewritten by remove_fastcall_spills_fills().
 * Otherwise bpf_fastcall patterns are not changed in the subprogram
 * (code, presumably, generated by an older clang version).
 *
 * For example, it is *not* safe to remove spill/fill below:
 *
 *   r1 = 1;
 *   *(u64 *)(r10 - 8)  = r1;            r1 = 1;
 *   call %[to_be_inlined]         -->   call %[to_be_inlined]
 *   r1 = *(u64 *)(r10 - 8);             r0 = *(u64 *)(r10 - 8);  <---- wrong !!!
 *   r0 = *(u64 *)(r10 - 8);             r0 += r1;
 *   r0 += r1;                           exit;
 *   exit;
 */
static void mark_fastcall_pattern_for_call(struct bpf_verifier_env *env,
					   struct bpf_subprog_info *subprog,
					   int insn_idx, s16 lowest_off)
{
	struct bpf_insn *insns = env->prog->insnsi, *stx, *ldx;
	struct bpf_insn *call = &env->prog->insnsi[insn_idx];
	const struct bpf_func_proto *fn;
	u32 clobbered_regs_mask = ALL_CALLER_SAVED_REGS;
	u32 expected_regs_mask;
	bool can_be_inlined = false;
	s16 off;
	int i;

	if (bpf_helper_call(call)) {
		if (get_helper_proto(env, call->imm, &fn) < 0)
			/* error would be reported later */
			return;
		clobbered_regs_mask = helper_fastcall_clobber_mask(fn);
		can_be_inlined = fn->allow_fastcall &&
				 (verifier_inlines_helper_call(env, call->imm) ||
				  bpf_jit_inlines_helper_call(call->imm));
	}

	if (bpf_pseudo_kfunc_call(call)) {
		struct bpf_kfunc_call_arg_meta meta;
		int err;

		err = fetch_kfunc_meta(env, call, &meta, NULL);
		if (err < 0)
			/* error would be reported later */
			return;

		clobbered_regs_mask = kfunc_fastcall_clobber_mask(&meta);
		can_be_inlined = is_fastcall_kfunc_call(&meta);
	}

	if (clobbered_regs_mask == ALL_CALLER_SAVED_REGS)
		return;

	/* e.g. if helper call clobbers r{0,1}, expect r{2,3,4,5} in the pattern */
	expected_regs_mask = ~clobbered_regs_mask & ALL_CALLER_SAVED_REGS;

	/* match pairs of form:
	 *
	 * *(u64 *)(r10 - Y) = rX   (where Y % 8 == 0)
	 * ...
	 * call %[to_be_inlined]
	 * ...
	 * rX = *(u64 *)(r10 - Y)
	 */
	for (i = 1, off = lowest_off; i <= ARRAY_SIZE(caller_saved); ++i, off += BPF_REG_SIZE) {
		if (insn_idx - i < 0 || insn_idx + i >= env->prog->len)
			break;
		stx = &insns[insn_idx - i];
		ldx = &insns[insn_idx + i];
		/* must be a stack spill/fill pair */
		if (stx->code != (BPF_STX | BPF_MEM | BPF_DW) ||
		    ldx->code != (BPF_LDX | BPF_MEM | BPF_DW) ||
		    stx->dst_reg != BPF_REG_10 ||
		    ldx->src_reg != BPF_REG_10)
			break;
		/* must be a spill/fill for the same reg */
		if (stx->src_reg != ldx->dst_reg)
			break;
		/* must be one of the previously unseen registers */
		if ((BIT(stx->src_reg) & expected_regs_mask) == 0)
			break;
		/* must be a spill/fill for the same expected offset,
		 * no need to check offset alignment, BPF_DW stack access
		 * is always 8-byte aligned.
		 */
		if (stx->off != off || ldx->off != off)
			break;
		expected_regs_mask &= ~BIT(stx->src_reg);
		env->insn_aux_data[insn_idx - i].fastcall_pattern = 1;
		env->insn_aux_data[insn_idx + i].fastcall_pattern = 1;
	}
	if (i == 1)
		return;

	/* Conditionally set 'fastcall_spills_num' to allow forward
	 * compatibility when more helper functions are marked as
	 * bpf_fastcall at compile time than current kernel supports, e.g:
	 *
	 *   1: *(u64 *)(r10 - 8) = r1
	 *   2: call A                  ;; assume A is bpf_fastcall for current kernel
	 *   3: r1 = *(u64 *)(r10 - 8)
	 *   4: *(u64 *)(r10 - 8) = r1
	 *   5: call B                  ;; assume B is not bpf_fastcall for current kernel
	 *   6: r1 = *(u64 *)(r10 - 8)
	 *
	 * There is no need to block bpf_fastcall rewrite for such program.
	 * Set 'fastcall_pattern' for both calls to keep check_fastcall_stack_contract() happy,
	 * don't set 'fastcall_spills_num' for call B so that remove_fastcall_spills_fills()
	 * does not remove spill/fill pair {4,6}.
	 */
	if (can_be_inlined)
		env->insn_aux_data[insn_idx].fastcall_spills_num = i - 1;
	else
		subprog->keep_fastcall_stack = 1;
	subprog->fastcall_stack_off = min(subprog->fastcall_stack_off, off);
}

static int mark_fastcall_patterns(struct bpf_verifier_env *env)
{
	struct bpf_subprog_info *subprog = env->subprog_info;
	struct bpf_insn *insn;
	s16 lowest_off;
	int s, i;

	for (s = 0; s < env->subprog_cnt; ++s, ++subprog) {
		/* find lowest stack spill offset used in this subprog */
		lowest_off = 0;
		for (i = subprog->start; i < (subprog + 1)->start; ++i) {
			insn = env->prog->insnsi + i;
			if (insn->code != (BPF_STX | BPF_MEM | BPF_DW) ||
			    insn->dst_reg != BPF_REG_10)
				continue;
			lowest_off = min(lowest_off, insn->off);
		}
		/* use this offset to find fastcall patterns */
		for (i = subprog->start; i < (subprog + 1)->start; ++i) {
			insn = env->prog->insnsi + i;
			if (insn->code != (BPF_JMP | BPF_CALL))
				continue;
			mark_fastcall_pattern_for_call(env, subprog, i, lowest_off);
		}
	}
	return 0;
}

/* Visits the instruction at index t and returns one of the following:
 *  < 0 - an error occurred
 *  DONE_EXPLORING - the instruction was fully explored
 *  KEEP_EXPLORING - there is still work to be done before it is fully explored
 */
static int visit_insn(int t, struct bpf_verifier_env *env)
{
	struct bpf_insn *insns = env->prog->insnsi, *insn = &insns[t];
	int ret, off, insn_sz;

	if (bpf_pseudo_func(insn))
		return visit_func_call_insn(t, insns, env, true);

	/* All non-branch instructions have a single fall-through edge. */
	if (BPF_CLASS(insn->code) != BPF_JMP &&
	    BPF_CLASS(insn->code) != BPF_JMP32) {
		insn_sz = bpf_is_ldimm64(insn) ? 2 : 1;
		return push_insn(t, t + insn_sz, FALLTHROUGH, env);
	}

	switch (BPF_OP(insn->code)) {
	case BPF_EXIT:
		return DONE_EXPLORING;

	case BPF_CALL:
		if (is_async_callback_calling_insn(insn))
			/* Mark this call insn as a prune point to trigger
			 * is_state_visited() check before call itself is
			 * processed by __check_func_call(). Otherwise new
			 * async state will be pushed for further exploration.
			 */
			mark_prune_point(env, t);
		/* For functions that invoke callbacks it is not known how many times
		 * callback would be called. Verifier models callback calling functions
		 * by repeatedly visiting callback bodies and returning to origin call
		 * instruction.
		 * In order to stop such iteration verifier needs to identify when a
		 * state identical some state from a previous iteration is reached.
		 * Check below forces creation of checkpoint before callback calling
		 * instruction to allow search for such identical states.
		 */
		if (is_sync_callback_calling_insn(insn)) {
			mark_calls_callback(env, t);
			mark_force_checkpoint(env, t);
			mark_prune_point(env, t);
			mark_jmp_point(env, t);
		}
		if (insn->src_reg == BPF_PSEUDO_KFUNC_CALL) {
			struct bpf_kfunc_call_arg_meta meta;

			ret = fetch_kfunc_meta(env, insn, &meta, NULL);
			if (ret == 0 && is_iter_next_kfunc(&meta)) {
				mark_prune_point(env, t);
				/* Checking and saving state checkpoints at iter_next() call
				 * is crucial for fast convergence of open-coded iterator loop
				 * logic, so we need to force it. If we don't do that,
				 * is_state_visited() might skip saving a checkpoint, causing
				 * unnecessarily long sequence of not checkpointed
				 * instructions and jumps, leading to exhaustion of jump
				 * history buffer, and potentially other undesired outcomes.
				 * It is expected that with correct open-coded iterators
				 * convergence will happen quickly, so we don't run a risk of
				 * exhausting memory.
				 */
				mark_force_checkpoint(env, t);
			}
		}
		return visit_func_call_insn(t, insns, env, insn->src_reg == BPF_PSEUDO_CALL);

	case BPF_JA:
		if (BPF_SRC(insn->code) != BPF_K)
			return -EINVAL;

		if (BPF_CLASS(insn->code) == BPF_JMP)
			off = insn->off;
		else
			off = insn->imm;

		/* unconditional jump with single edge */
		ret = push_insn(t, t + off + 1, FALLTHROUGH, env);
		if (ret)
			return ret;

		mark_prune_point(env, t + off + 1);
		mark_jmp_point(env, t + off + 1);

		return ret;

	default:
		/* conditional jump with two edges */
		mark_prune_point(env, t);
		if (is_may_goto_insn(insn))
			mark_force_checkpoint(env, t);

		ret = push_insn(t, t + 1, FALLTHROUGH, env);
		if (ret)
			return ret;

		return push_insn(t, t + insn->off + 1, BRANCH, env);
	}
}

/* non-recursive depth-first-search to detect loops in BPF program
 * loop == back-edge in directed graph
 */
static int check_cfg(struct bpf_verifier_env *env)
{
	int insn_cnt = env->prog->len;
	int *insn_stack, *insn_state;
	int ex_insn_beg, i, ret = 0;
	bool ex_done = false;

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

walk_cfg:
	while (env->cfg.cur_stack > 0) {
		int t = insn_stack[env->cfg.cur_stack - 1];

		ret = visit_insn(t, env);
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

	if (env->exception_callback_subprog && !ex_done) {
		ex_insn_beg = env->subprog_info[env->exception_callback_subprog].start;

		insn_state[ex_insn_beg] = DISCOVERED;
		insn_stack[0] = ex_insn_beg;
		env->cfg.cur_stack = 1;
		ex_done = true;
		goto walk_cfg;
	}

	for (i = 0; i < insn_cnt; i++) {
		struct bpf_insn *insn = &env->prog->insnsi[i];

		if (insn_state[i] != EXPLORED) {
			verbose(env, "unreachable insn %d\n", i);
			ret = -EINVAL;
			goto err_free;
		}
		if (bpf_is_ldimm64(insn)) {
			if (insn_state[i + 1] != 0) {
				verbose(env, "jump into the middle of ldimm64 insn %d\n", i);
				ret = -EINVAL;
				goto err_free;
			}
			i++; /* skip second half of ldimm64 */
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

static int check_btf_func_early(struct bpf_verifier_env *env,
				const union bpf_attr *attr,
				bpfptr_t uattr)
{
	u32 krec_size = sizeof(struct bpf_func_info);
	const struct btf_type *type, *func_proto;
	u32 i, nfuncs, urec_size, min_size;
	struct bpf_func_info *krecord;
	struct bpf_prog *prog;
	const struct btf *btf;
	u32 prev_offset = 0;
	bpfptr_t urecord;
	int ret = -ENOMEM;

	nfuncs = attr->func_info_cnt;
	if (!nfuncs) {
		if (check_abnormal_return(env))
			return -EINVAL;
		return 0;
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

	urecord = make_bpfptr(attr->func_info, uattr.is_kernel);
	min_size = min_t(u32, krec_size, urec_size);

	krecord = kvcalloc(nfuncs, krec_size, GFP_KERNEL | __GFP_NOWARN);
	if (!krecord)
		return -ENOMEM;

	for (i = 0; i < nfuncs; i++) {
		ret = bpf_check_uarg_tail_zero(urecord, krec_size, urec_size);
		if (ret) {
			if (ret == -E2BIG) {
				verbose(env, "nonzero tailing record in func info");
				/* set the size kernel expects so loader can zero
				 * out the rest of the record.
				 */
				if (copy_to_bpfptr_offset(uattr,
							  offsetof(union bpf_attr, func_info_rec_size),
							  &min_size, sizeof(min_size)))
					ret = -EFAULT;
			}
			goto err_free;
		}

		if (copy_from_bpfptr(&krecord[i], urecord, min_size)) {
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

		/* check type_id */
		type = btf_type_by_id(btf, krecord[i].type_id);
		if (!type || !btf_type_is_func(type)) {
			verbose(env, "invalid type id %d in func info",
				krecord[i].type_id);
			goto err_free;
		}

		func_proto = btf_type_by_id(btf, type->type);
		if (unlikely(!func_proto || !btf_type_is_func_proto(func_proto)))
			/* btf_func_check() already verified it during BTF load */
			goto err_free;

		prev_offset = krecord[i].insn_off;
		bpfptr_add(&urecord, urec_size);
	}

	prog->aux->func_info = krecord;
	prog->aux->func_info_cnt = nfuncs;
	return 0;

err_free:
	kvfree(krecord);
	return ret;
}

static int check_btf_func(struct bpf_verifier_env *env,
			  const union bpf_attr *attr,
			  bpfptr_t uattr)
{
	const struct btf_type *type, *func_proto, *ret_type;
	u32 i, nfuncs, urec_size;
	struct bpf_func_info *krecord;
	struct bpf_func_info_aux *info_aux = NULL;
	struct bpf_prog *prog;
	const struct btf *btf;
	bpfptr_t urecord;
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

	prog = env->prog;
	btf = prog->aux->btf;

	urecord = make_bpfptr(attr->func_info, uattr.is_kernel);

	krecord = prog->aux->func_info;
	info_aux = kcalloc(nfuncs, sizeof(*info_aux), GFP_KERNEL | __GFP_NOWARN);
	if (!info_aux)
		return -ENOMEM;

	for (i = 0; i < nfuncs; i++) {
		/* check insn_off */
		ret = -EINVAL;

		if (env->subprog_info[i].start != krecord[i].insn_off) {
			verbose(env, "func_info BTF section doesn't match subprog layout in BPF program\n");
			goto err_free;
		}

		/* Already checked type_id */
		type = btf_type_by_id(btf, krecord[i].type_id);
		info_aux[i].linkage = BTF_INFO_VLEN(type->info);
		/* Already checked func_proto */
		func_proto = btf_type_by_id(btf, type->type);

		ret_type = btf_type_skip_modifiers(btf, func_proto->type, NULL);
		scalar_return =
			btf_type_is_small_int(ret_type) || btf_is_any_enum(ret_type);
		if (i && !scalar_return && env->subprog_info[i].has_ld_abs) {
			verbose(env, "LD_ABS is only allowed in functions that return 'int'.\n");
			goto err_free;
		}
		if (i && !scalar_return && env->subprog_info[i].has_tail_call) {
			verbose(env, "tail_call is only allowed in functions that return 'int'.\n");
			goto err_free;
		}

		bpfptr_add(&urecord, urec_size);
	}

	prog->aux->func_info_aux = info_aux;
	return 0;

err_free:
	kfree(info_aux);
	return ret;
}

static void adjust_btf_func(struct bpf_verifier_env *env)
{
	struct bpf_prog_aux *aux = env->prog->aux;
	int i;

	if (!aux->func_info)
		return;

	/* func_info is not available for hidden subprogs */
	for (i = 0; i < env->subprog_cnt - env->hidden_subprog_cnt; i++)
		aux->func_info[i].insn_off = env->subprog_info[i].start;
}

#define MIN_BPF_LINEINFO_SIZE	offsetofend(struct bpf_line_info, line_col)
#define MAX_LINEINFO_REC_SIZE	MAX_FUNCINFO_REC_SIZE

static int check_btf_line(struct bpf_verifier_env *env,
			  const union bpf_attr *attr,
			  bpfptr_t uattr)
{
	u32 i, s, nr_linfo, ncopy, expected_size, rec_size, prev_offset = 0;
	struct bpf_subprog_info *sub;
	struct bpf_line_info *linfo;
	struct bpf_prog *prog;
	const struct btf *btf;
	bpfptr_t ulinfo;
	int err;

	nr_linfo = attr->line_info_cnt;
	if (!nr_linfo)
		return 0;
	if (nr_linfo > INT_MAX / sizeof(struct bpf_line_info))
		return -EINVAL;

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
	ulinfo = make_bpfptr(attr->line_info, uattr.is_kernel);
	expected_size = sizeof(struct bpf_line_info);
	ncopy = min_t(u32, expected_size, rec_size);
	for (i = 0; i < nr_linfo; i++) {
		err = bpf_check_uarg_tail_zero(ulinfo, expected_size, rec_size);
		if (err) {
			if (err == -E2BIG) {
				verbose(env, "nonzero tailing record in line_info");
				if (copy_to_bpfptr_offset(uattr,
							  offsetof(union bpf_attr, line_info_rec_size),
							  &expected_size, sizeof(expected_size)))
					err = -EFAULT;
			}
			goto err_free;
		}

		if (copy_from_bpfptr(&linfo[i], ulinfo, ncopy)) {
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
		bpfptr_add(&ulinfo, rec_size);
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

#define MIN_CORE_RELO_SIZE	sizeof(struct bpf_core_relo)
#define MAX_CORE_RELO_SIZE	MAX_FUNCINFO_REC_SIZE

static int check_core_relo(struct bpf_verifier_env *env,
			   const union bpf_attr *attr,
			   bpfptr_t uattr)
{
	u32 i, nr_core_relo, ncopy, expected_size, rec_size;
	struct bpf_core_relo core_relo = {};
	struct bpf_prog *prog = env->prog;
	const struct btf *btf = prog->aux->btf;
	struct bpf_core_ctx ctx = {
		.log = &env->log,
		.btf = btf,
	};
	bpfptr_t u_core_relo;
	int err;

	nr_core_relo = attr->core_relo_cnt;
	if (!nr_core_relo)
		return 0;
	if (nr_core_relo > INT_MAX / sizeof(struct bpf_core_relo))
		return -EINVAL;

	rec_size = attr->core_relo_rec_size;
	if (rec_size < MIN_CORE_RELO_SIZE ||
	    rec_size > MAX_CORE_RELO_SIZE ||
	    rec_size % sizeof(u32))
		return -EINVAL;

	u_core_relo = make_bpfptr(attr->core_relos, uattr.is_kernel);
	expected_size = sizeof(struct bpf_core_relo);
	ncopy = min_t(u32, expected_size, rec_size);

	/* Unlike func_info and line_info, copy and apply each CO-RE
	 * relocation record one at a time.
	 */
	for (i = 0; i < nr_core_relo; i++) {
		/* future proofing when sizeof(bpf_core_relo) changes */
		err = bpf_check_uarg_tail_zero(u_core_relo, expected_size, rec_size);
		if (err) {
			if (err == -E2BIG) {
				verbose(env, "nonzero tailing record in core_relo");
				if (copy_to_bpfptr_offset(uattr,
							  offsetof(union bpf_attr, core_relo_rec_size),
							  &expected_size, sizeof(expected_size)))
					err = -EFAULT;
			}
			break;
		}

		if (copy_from_bpfptr(&core_relo, u_core_relo, ncopy)) {
			err = -EFAULT;
			break;
		}

		if (core_relo.insn_off % 8 || core_relo.insn_off / 8 >= prog->len) {
			verbose(env, "Invalid core_relo[%u].insn_off:%u prog->len:%u\n",
				i, core_relo.insn_off, prog->len);
			err = -EINVAL;
			break;
		}

		err = bpf_core_apply(&ctx, &core_relo, i,
				     &prog->insnsi[core_relo.insn_off / 8]);
		if (err)
			break;
		bpfptr_add(&u_core_relo, rec_size);
	}
	return err;
}

static int check_btf_info_early(struct bpf_verifier_env *env,
				const union bpf_attr *attr,
				bpfptr_t uattr)
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

	err = check_btf_func_early(env, attr, uattr);
	if (err)
		return err;
	return 0;
}

static int check_btf_info(struct bpf_verifier_env *env,
			  const union bpf_attr *attr,
			  bpfptr_t uattr)
{
	int err;

	if (!attr->func_info_cnt && !attr->line_info_cnt) {
		if (check_abnormal_return(env))
			return -EINVAL;
		return 0;
	}

	err = check_btf_func(env, attr, uattr);
	if (err)
		return err;

	err = check_btf_line(env, attr, uattr);
	if (err)
		return err;

	err = check_core_relo(env, attr, uattr);
	if (err)
		return err;

	return 0;
}

/* check %cur's range satisfies %old's */
static bool range_within(const struct bpf_reg_state *old,
			 const struct bpf_reg_state *cur)
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
static bool check_ids(u32 old_id, u32 cur_id, struct bpf_idmap *idmap)
{
	struct bpf_id_pair *map = idmap->map;
	unsigned int i;

	/* either both IDs should be set or both should be zero */
	if (!!old_id != !!cur_id)
		return false;

	if (old_id == 0) /* cur_id == 0 as well */
		return true;

	for (i = 0; i < BPF_ID_MAP_SIZE; i++) {
		if (!map[i].old) {
			/* Reached an empty slot; haven't seen this id before */
			map[i].old = old_id;
			map[i].cur = cur_id;
			return true;
		}
		if (map[i].old == old_id)
			return map[i].cur == cur_id;
		if (map[i].cur == cur_id)
			return false;
	}
	/* We ran out of idmap slots, which should be impossible */
	WARN_ON_ONCE(1);
	return false;
}

/* Similar to check_ids(), but allocate a unique temporary ID
 * for 'old_id' or 'cur_id' of zero.
 * This makes pairs like '0 vs unique ID', 'unique ID vs 0' valid.
 */
static bool check_scalar_ids(u32 old_id, u32 cur_id, struct bpf_idmap *idmap)
{
	old_id = old_id ? old_id : ++idmap->tmp_id_gen;
	cur_id = cur_id ? cur_id : ++idmap->tmp_id_gen;

	return check_ids(old_id, cur_id, idmap);
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
 * their final liveness marks are already propagated.
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

	sl = *explored_state(env, insn);
	while (sl) {
		if (sl->state.branches)
			goto next;
		if (sl->state.insn_idx != insn ||
		    !same_callsites(&sl->state, cur))
			goto next;
		clean_verifier_state(env, &sl->state);
next:
		sl = sl->next;
	}
}

static bool regs_exact(const struct bpf_reg_state *rold,
		       const struct bpf_reg_state *rcur,
		       struct bpf_idmap *idmap)
{
	return memcmp(rold, rcur, offsetof(struct bpf_reg_state, id)) == 0 &&
	       check_ids(rold->id, rcur->id, idmap) &&
	       check_ids(rold->ref_obj_id, rcur->ref_obj_id, idmap);
}

enum exact_level {
	NOT_EXACT,
	EXACT,
	RANGE_WITHIN
};

/* Returns true if (rold safe implies rcur safe) */
static bool regsafe(struct bpf_verifier_env *env, struct bpf_reg_state *rold,
		    struct bpf_reg_state *rcur, struct bpf_idmap *idmap,
		    enum exact_level exact)
{
	if (exact == EXACT)
		return regs_exact(rold, rcur, idmap);

	if (!(rold->live & REG_LIVE_READ) && exact == NOT_EXACT)
		/* explored state didn't use this */
		return true;
	if (rold->type == NOT_INIT) {
		if (exact == NOT_EXACT || rcur->type == NOT_INIT)
			/* explored state can't have used this */
			return true;
	}

	/* Enforce that register types have to match exactly, including their
	 * modifiers (like PTR_MAYBE_NULL, MEM_RDONLY, etc), as a general
	 * rule.
	 *
	 * One can make a point that using a pointer register as unbounded
	 * SCALAR would be technically acceptable, but this could lead to
	 * pointer leaks because scalars are allowed to leak while pointers
	 * are not. We could make this safe in special cases if root is
	 * calling us, but it's probably not worth the hassle.
	 *
	 * Also, register types that are *not* MAYBE_NULL could technically be
	 * safe to use as their MAYBE_NULL variants (e.g., PTR_TO_MAP_VALUE
	 * is safe to be used as PTR_TO_MAP_VALUE_OR_NULL, provided both point
	 * to the same map).
	 * However, if the old MAYBE_NULL register then got NULL checked,
	 * doing so could have affected others with the same id, and we can't
	 * check for that because we lost the id when we converted to
	 * a non-MAYBE_NULL variant.
	 * So, as a general rule we don't allow mixing MAYBE_NULL and
	 * non-MAYBE_NULL registers as well.
	 */
	if (rold->type != rcur->type)
		return false;

	switch (base_type(rold->type)) {
	case SCALAR_VALUE:
		if (env->explore_alu_limits) {
			/* explore_alu_limits disables tnum_in() and range_within()
			 * logic and requires everything to be strict
			 */
			return memcmp(rold, rcur, offsetof(struct bpf_reg_state, id)) == 0 &&
			       check_scalar_ids(rold->id, rcur->id, idmap);
		}
		if (!rold->precise && exact == NOT_EXACT)
			return true;
		if ((rold->id & BPF_ADD_CONST) != (rcur->id & BPF_ADD_CONST))
			return false;
		if ((rold->id & BPF_ADD_CONST) && (rold->off != rcur->off))
			return false;
		/* Why check_ids() for scalar registers?
		 *
		 * Consider the following BPF code:
		 *   1: r6 = ... unbound scalar, ID=a ...
		 *   2: r7 = ... unbound scalar, ID=b ...
		 *   3: if (r6 > r7) goto +1
		 *   4: r6 = r7
		 *   5: if (r6 > X) goto ...
		 *   6: ... memory operation using r7 ...
		 *
		 * First verification path is [1-6]:
		 * - at (4) same bpf_reg_state::id (b) would be assigned to r6 and r7;
		 * - at (5) r6 would be marked <= X, sync_linked_regs() would also mark
		 *   r7 <= X, because r6 and r7 share same id.
		 * Next verification path is [1-4, 6].
		 *
		 * Instruction (6) would be reached in two states:
		 *   I.  r6{.id=b}, r7{.id=b} via path 1-6;
		 *   II. r6{.id=a}, r7{.id=b} via path 1-4, 6.
		 *
		 * Use check_ids() to distinguish these states.
		 * ---
		 * Also verify that new value satisfies old value range knowledge.
		 */
		return range_within(rold, rcur) &&
		       tnum_in(rold->var_off, rcur->var_off) &&
		       check_scalar_ids(rold->id, rcur->id, idmap);
	case PTR_TO_MAP_KEY:
	case PTR_TO_MAP_VALUE:
	case PTR_TO_MEM:
	case PTR_TO_BUF:
	case PTR_TO_TP_BUFFER:
		/* If the new min/max/var_off satisfy the old ones and
		 * everything else matches, we are OK.
		 */
		return memcmp(rold, rcur, offsetof(struct bpf_reg_state, var_off)) == 0 &&
		       range_within(rold, rcur) &&
		       tnum_in(rold->var_off, rcur->var_off) &&
		       check_ids(rold->id, rcur->id, idmap) &&
		       check_ids(rold->ref_obj_id, rcur->ref_obj_id, idmap);
	case PTR_TO_PACKET_META:
	case PTR_TO_PACKET:
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
		if (!check_ids(rold->id, rcur->id, idmap))
			return false;
		/* new val must satisfy old val knowledge */
		return range_within(rold, rcur) &&
		       tnum_in(rold->var_off, rcur->var_off);
	case PTR_TO_STACK:
		/* two stack pointers are equal only if they're pointing to
		 * the same stack frame, since fp-8 in foo != fp-8 in bar
		 */
		return regs_exact(rold, rcur, idmap) && rold->frameno == rcur->frameno;
	case PTR_TO_ARENA:
		return true;
	default:
		return regs_exact(rold, rcur, idmap);
	}
}

static struct bpf_reg_state unbound_reg;

static __init int unbound_reg_init(void)
{
	__mark_reg_unknown_imprecise(&unbound_reg);
	unbound_reg.live |= REG_LIVE_READ;
	return 0;
}
late_initcall(unbound_reg_init);

static bool is_stack_all_misc(struct bpf_verifier_env *env,
			      struct bpf_stack_state *stack)
{
	u32 i;

	for (i = 0; i < ARRAY_SIZE(stack->slot_type); ++i) {
		if ((stack->slot_type[i] == STACK_MISC) ||
		    (stack->slot_type[i] == STACK_INVALID && env->allow_uninit_stack))
			continue;
		return false;
	}

	return true;
}

static struct bpf_reg_state *scalar_reg_for_stack(struct bpf_verifier_env *env,
						  struct bpf_stack_state *stack)
{
	if (is_spilled_scalar_reg64(stack))
		return &stack->spilled_ptr;

	if (is_stack_all_misc(env, stack))
		return &unbound_reg;

	return NULL;
}

static bool stacksafe(struct bpf_verifier_env *env, struct bpf_func_state *old,
		      struct bpf_func_state *cur, struct bpf_idmap *idmap,
		      enum exact_level exact)
{
	int i, spi;

	/* walk slots of the explored stack and ignore any additional
	 * slots in the current stack, since explored(safe) state
	 * didn't use them
	 */
	for (i = 0; i < old->allocated_stack; i++) {
		struct bpf_reg_state *old_reg, *cur_reg;

		spi = i / BPF_REG_SIZE;

		if (exact != NOT_EXACT &&
		    (i >= cur->allocated_stack ||
		     old->stack[spi].slot_type[i % BPF_REG_SIZE] !=
		     cur->stack[spi].slot_type[i % BPF_REG_SIZE]))
			return false;

		if (!(old->stack[spi].spilled_ptr.live & REG_LIVE_READ)
		    && exact == NOT_EXACT) {
			i += BPF_REG_SIZE - 1;
			/* explored state didn't use this */
			continue;
		}

		if (old->stack[spi].slot_type[i % BPF_REG_SIZE] == STACK_INVALID)
			continue;

		if (env->allow_uninit_stack &&
		    old->stack[spi].slot_type[i % BPF_REG_SIZE] == STACK_MISC)
			continue;

		/* explored stack has more populated slots than current stack
		 * and these slots were used
		 */
		if (i >= cur->allocated_stack)
			return false;

		/* 64-bit scalar spill vs all slots MISC and vice versa.
		 * Load from all slots MISC produces unbound scalar.
		 * Construct a fake register for such stack and call
		 * regsafe() to ensure scalar ids are compared.
		 */
		old_reg = scalar_reg_for_stack(env, &old->stack[spi]);
		cur_reg = scalar_reg_for_stack(env, &cur->stack[spi]);
		if (old_reg && cur_reg) {
			if (!regsafe(env, old_reg, cur_reg, idmap, exact))
				return false;
			i += BPF_REG_SIZE - 1;
			continue;
		}

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
		if (i % BPF_REG_SIZE != BPF_REG_SIZE - 1)
			continue;
		/* Both old and cur are having same slot_type */
		switch (old->stack[spi].slot_type[BPF_REG_SIZE - 1]) {
		case STACK_SPILL:
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
			if (!regsafe(env, &old->stack[spi].spilled_ptr,
				     &cur->stack[spi].spilled_ptr, idmap, exact))
				return false;
			break;
		case STACK_DYNPTR:
			old_reg = &old->stack[spi].spilled_ptr;
			cur_reg = &cur->stack[spi].spilled_ptr;
			if (old_reg->dynptr.type != cur_reg->dynptr.type ||
			    old_reg->dynptr.first_slot != cur_reg->dynptr.first_slot ||
			    !check_ids(old_reg->ref_obj_id, cur_reg->ref_obj_id, idmap))
				return false;
			break;
		case STACK_ITER:
			old_reg = &old->stack[spi].spilled_ptr;
			cur_reg = &cur->stack[spi].spilled_ptr;
			/* iter.depth is not compared between states as it
			 * doesn't matter for correctness and would otherwise
			 * prevent convergence; we maintain it only to prevent
			 * infinite loop check triggering, see
			 * iter_active_depths_differ()
			 */
			if (old_reg->iter.btf != cur_reg->iter.btf ||
			    old_reg->iter.btf_id != cur_reg->iter.btf_id ||
			    old_reg->iter.state != cur_reg->iter.state ||
			    /* ignore {old_reg,cur_reg}->iter.depth, see above */
			    !check_ids(old_reg->ref_obj_id, cur_reg->ref_obj_id, idmap))
				return false;
			break;
		case STACK_MISC:
		case STACK_ZERO:
		case STACK_INVALID:
			continue;
		/* Ensure that new unhandled slot types return false by default */
		default:
			return false;
		}
	}
	return true;
}

static bool refsafe(struct bpf_func_state *old, struct bpf_func_state *cur,
		    struct bpf_idmap *idmap)
{
	int i;

	if (old->acquired_refs != cur->acquired_refs)
		return false;

	for (i = 0; i < old->acquired_refs; i++) {
		if (!check_ids(old->refs[i].id, cur->refs[i].id, idmap) ||
		    old->refs[i].type != cur->refs[i].type)
			return false;
		switch (old->refs[i].type) {
		case REF_TYPE_PTR:
			break;
		case REF_TYPE_LOCK:
			if (old->refs[i].ptr != cur->refs[i].ptr)
				return false;
			break;
		default:
			WARN_ONCE(1, "Unhandled enum type for reference state: %d\n", old->refs[i].type);
			return false;
		}
	}

	return true;
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
static bool func_states_equal(struct bpf_verifier_env *env, struct bpf_func_state *old,
			      struct bpf_func_state *cur, enum exact_level exact)
{
	int i;

	if (old->callback_depth > cur->callback_depth)
		return false;

	for (i = 0; i < MAX_BPF_REG; i++)
		if (!regsafe(env, &old->regs[i], &cur->regs[i],
			     &env->idmap_scratch, exact))
			return false;

	if (!stacksafe(env, old, cur, &env->idmap_scratch, exact))
		return false;

	if (!refsafe(old, cur, &env->idmap_scratch))
		return false;

	return true;
}

static void reset_idmap_scratch(struct bpf_verifier_env *env)
{
	env->idmap_scratch.tmp_id_gen = env->id_gen;
	memset(&env->idmap_scratch.map, 0, sizeof(env->idmap_scratch.map));
}

static bool states_equal(struct bpf_verifier_env *env,
			 struct bpf_verifier_state *old,
			 struct bpf_verifier_state *cur,
			 enum exact_level exact)
{
	int i;

	if (old->curframe != cur->curframe)
		return false;

	reset_idmap_scratch(env);

	/* Verification state from speculative execution simulation
	 * must never prune a non-speculative execution one.
	 */
	if (old->speculative && !cur->speculative)
		return false;

	if (old->active_rcu_lock != cur->active_rcu_lock)
		return false;

	if (old->active_preempt_lock != cur->active_preempt_lock)
		return false;

	if (old->in_sleepable != cur->in_sleepable)
		return false;

	/* for states to be equal callsites have to be the same
	 * and all frame states need to be equivalent
	 */
	for (i = 0; i <= old->curframe; i++) {
		if (old->frame[i]->callsite != cur->frame[i]->callsite)
			return false;
		if (!func_states_equal(env, old->frame[i], cur->frame[i], exact))
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
	int i, err = 0, fr;
	bool first;

	for (fr = old->curframe; fr >= 0; fr--) {
		state = old->frame[fr];
		state_reg = state->regs;
		first = true;
		for (i = 0; i < BPF_REG_FP; i++, state_reg++) {
			if (state_reg->type != SCALAR_VALUE ||
			    !state_reg->precise ||
			    !(state_reg->live & REG_LIVE_READ))
				continue;
			if (env->log.level & BPF_LOG_LEVEL2) {
				if (first)
					verbose(env, "frame %d: propagating r%d", fr, i);
				else
					verbose(env, ",r%d", i);
			}
			bt_set_frame_reg(&env->bt, fr, i);
			first = false;
		}

		for (i = 0; i < state->allocated_stack / BPF_REG_SIZE; i++) {
			if (!is_spilled_reg(&state->stack[i]))
				continue;
			state_reg = &state->stack[i].spilled_ptr;
			if (state_reg->type != SCALAR_VALUE ||
			    !state_reg->precise ||
			    !(state_reg->live & REG_LIVE_READ))
				continue;
			if (env->log.level & BPF_LOG_LEVEL2) {
				if (first)
					verbose(env, "frame %d: propagating fp%d",
						fr, (-i - 1) * BPF_REG_SIZE);
				else
					verbose(env, ",fp%d", (-i - 1) * BPF_REG_SIZE);
			}
			bt_set_frame_slot(&env->bt, fr, i);
			first = false;
		}
		if (!first)
			verbose(env, "\n");
	}

	err = mark_chain_precision_batch(env);
	if (err < 0)
		return err;

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

static bool is_iter_next_insn(struct bpf_verifier_env *env, int insn_idx)
{
	return env->insn_aux_data[insn_idx].is_iter_next;
}

/* is_state_visited() handles iter_next() (see process_iter_next_call() for
 * terminology) calls specially: as opposed to bounded BPF loops, it *expects*
 * states to match, which otherwise would look like an infinite loop. So while
 * iter_next() calls are taken care of, we still need to be careful and
 * prevent erroneous and too eager declaration of "ininite loop", when
 * iterators are involved.
 *
 * Here's a situation in pseudo-BPF assembly form:
 *
 *   0: again:                          ; set up iter_next() call args
 *   1:   r1 = &it                      ; <CHECKPOINT HERE>
 *   2:   call bpf_iter_num_next        ; this is iter_next() call
 *   3:   if r0 == 0 goto done
 *   4:   ... something useful here ...
 *   5:   goto again                    ; another iteration
 *   6: done:
 *   7:   r1 = &it
 *   8:   call bpf_iter_num_destroy     ; clean up iter state
 *   9:   exit
 *
 * This is a typical loop. Let's assume that we have a prune point at 1:,
 * before we get to `call bpf_iter_num_next` (e.g., because of that `goto
 * again`, assuming other heuristics don't get in a way).
 *
 * When we first time come to 1:, let's say we have some state X. We proceed
 * to 2:, fork states, enqueue ACTIVE, validate NULL case successfully, exit.
 * Now we come back to validate that forked ACTIVE state. We proceed through
 * 3-5, come to goto, jump to 1:. Let's assume our state didn't change, so we
 * are converging. But the problem is that we don't know that yet, as this
 * convergence has to happen at iter_next() call site only. So if nothing is
 * done, at 1: verifier will use bounded loop logic and declare infinite
 * looping (and would be *technically* correct, if not for iterator's
 * "eventual sticky NULL" contract, see process_iter_next_call()). But we
 * don't want that. So what we do in process_iter_next_call() when we go on
 * another ACTIVE iteration, we bump slot->iter.depth, to mark that it's
 * a different iteration. So when we suspect an infinite loop, we additionally
 * check if any of the *ACTIVE* iterator states depths differ. If yes, we
 * pretend we are not looping and wait for next iter_next() call.
 *
 * This only applies to ACTIVE state. In DRAINED state we don't expect to
 * loop, because that would actually mean infinite loop, as DRAINED state is
 * "sticky", and so we'll keep returning into the same instruction with the
 * same state (at least in one of possible code paths).
 *
 * This approach allows to keep infinite loop heuristic even in the face of
 * active iterator. E.g., C snippet below is and will be detected as
 * inifintely looping:
 *
 *   struct bpf_iter_num it;
 *   int *p, x;
 *
 *   bpf_iter_num_new(&it, 0, 10);
 *   while ((p = bpf_iter_num_next(&t))) {
 *       x = p;
 *       while (x--) {} // <<-- infinite loop here
 *   }
 *
 */
static bool iter_active_depths_differ(struct bpf_verifier_state *old, struct bpf_verifier_state *cur)
{
	struct bpf_reg_state *slot, *cur_slot;
	struct bpf_func_state *state;
	int i, fr;

	for (fr = old->curframe; fr >= 0; fr--) {
		state = old->frame[fr];
		for (i = 0; i < state->allocated_stack / BPF_REG_SIZE; i++) {
			if (state->stack[i].slot_type[0] != STACK_ITER)
				continue;

			slot = &state->stack[i].spilled_ptr;
			if (slot->iter.state != BPF_ITER_STATE_ACTIVE)
				continue;

			cur_slot = &cur->frame[fr]->stack[i].spilled_ptr;
			if (cur_slot->iter.depth != slot->iter.depth)
				return true;
		}
	}
	return false;
}

static int is_state_visited(struct bpf_verifier_env *env, int insn_idx)
{
	struct bpf_verifier_state_list *new_sl;
	struct bpf_verifier_state_list *sl, **pprev;
	struct bpf_verifier_state *cur = env->cur_state, *new, *loop_entry;
	int i, j, n, err, states_cnt = 0;
	bool force_new_state, add_new_state, force_exact;

	force_new_state = env->test_state_freq || is_force_checkpoint(env, insn_idx) ||
			  /* Avoid accumulating infinitely long jmp history */
			  cur->insn_hist_end - cur->insn_hist_start > 40;

	/* bpf progs typically have pruning point every 4 instructions
	 * http://vger.kernel.org/bpfconf2019.html#session-1
	 * Do not add new state for future pruning if the verifier hasn't seen
	 * at least 2 jumps and at least 8 instructions.
	 * This heuristics helps decrease 'total_states' and 'peak_states' metric.
	 * In tests that amounts to up to 50% reduction into total verifier
	 * memory consumption and 20% verifier time speedup.
	 */
	add_new_state = force_new_state;
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
			struct bpf_func_state *frame = sl->state.frame[sl->state.curframe];

			if (frame->in_async_callback_fn &&
			    frame->async_entry_cnt != cur->frame[cur->curframe]->async_entry_cnt) {
				/* Different async_entry_cnt means that the verifier is
				 * processing another entry into async callback.
				 * Seeing the same state is not an indication of infinite
				 * loop or infinite recursion.
				 * But finding the same state doesn't mean that it's safe
				 * to stop processing the current state. The previous state
				 * hasn't yet reached bpf_exit, since state.branches > 0.
				 * Checking in_async_callback_fn alone is not enough either.
				 * Since the verifier still needs to catch infinite loops
				 * inside async callbacks.
				 */
				goto skip_inf_loop_check;
			}
			/* BPF open-coded iterators loop detection is special.
			 * states_maybe_looping() logic is too simplistic in detecting
			 * states that *might* be equivalent, because it doesn't know
			 * about ID remapping, so don't even perform it.
			 * See process_iter_next_call() and iter_active_depths_differ()
			 * for overview of the logic. When current and one of parent
			 * states are detected as equivalent, it's a good thing: we prove
			 * convergence and can stop simulating further iterations.
			 * It's safe to assume that iterator loop will finish, taking into
			 * account iter_next() contract of eventually returning
			 * sticky NULL result.
			 *
			 * Note, that states have to be compared exactly in this case because
			 * read and precision marks might not be finalized inside the loop.
			 * E.g. as in the program below:
			 *
			 *     1. r7 = -16
			 *     2. r6 = bpf_get_prandom_u32()
			 *     3. while (bpf_iter_num_next(&fp[-8])) {
			 *     4.   if (r6 != 42) {
			 *     5.     r7 = -32
			 *     6.     r6 = bpf_get_prandom_u32()
			 *     7.     continue
			 *     8.   }
			 *     9.   r0 = r10
			 *    10.   r0 += r7
			 *    11.   r8 = *(u64 *)(r0 + 0)
			 *    12.   r6 = bpf_get_prandom_u32()
			 *    13. }
			 *
			 * Here verifier would first visit path 1-3, create a checkpoint at 3
			 * with r7=-16, continue to 4-7,3. Existing checkpoint at 3 does
			 * not have read or precision mark for r7 yet, thus inexact states
			 * comparison would discard current state with r7=-32
			 * => unsafe memory access at 11 would not be caught.
			 */
			if (is_iter_next_insn(env, insn_idx)) {
				if (states_equal(env, &sl->state, cur, RANGE_WITHIN)) {
					struct bpf_func_state *cur_frame;
					struct bpf_reg_state *iter_state, *iter_reg;
					int spi;

					cur_frame = cur->frame[cur->curframe];
					/* btf_check_iter_kfuncs() enforces that
					 * iter state pointer is always the first arg
					 */
					iter_reg = &cur_frame->regs[BPF_REG_1];
					/* current state is valid due to states_equal(),
					 * so we can assume valid iter and reg state,
					 * no need for extra (re-)validations
					 */
					spi = __get_spi(iter_reg->off + iter_reg->var_off.value);
					iter_state = &func(env, iter_reg)->stack[spi].spilled_ptr;
					if (iter_state->iter.state == BPF_ITER_STATE_ACTIVE) {
						update_loop_entry(cur, &sl->state);
						goto hit;
					}
				}
				goto skip_inf_loop_check;
			}
			if (is_may_goto_insn_at(env, insn_idx)) {
				if (sl->state.may_goto_depth != cur->may_goto_depth &&
				    states_equal(env, &sl->state, cur, RANGE_WITHIN)) {
					update_loop_entry(cur, &sl->state);
					goto hit;
				}
			}
			if (calls_callback(env, insn_idx)) {
				if (states_equal(env, &sl->state, cur, RANGE_WITHIN))
					goto hit;
				goto skip_inf_loop_check;
			}
			/* attempt to detect infinite loop to avoid unnecessary doomed work */
			if (states_maybe_looping(&sl->state, cur) &&
			    states_equal(env, &sl->state, cur, EXACT) &&
			    !iter_active_depths_differ(&sl->state, cur) &&
			    sl->state.may_goto_depth == cur->may_goto_depth &&
			    sl->state.callback_unroll_depth == cur->callback_unroll_depth) {
				verbose_linfo(env, insn_idx, "; ");
				verbose(env, "infinite loop detected at insn %d\n", insn_idx);
				verbose(env, "cur state:");
				print_verifier_state(env, cur->frame[cur->curframe], true);
				verbose(env, "old state:");
				print_verifier_state(env, sl->state.frame[cur->curframe], true);
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
skip_inf_loop_check:
			if (!force_new_state &&
			    env->jmps_processed - env->prev_jmps_processed < 20 &&
			    env->insn_processed - env->prev_insn_processed < 100)
				add_new_state = false;
			goto miss;
		}
		/* If sl->state is a part of a loop and this loop's entry is a part of
		 * current verification path then states have to be compared exactly.
		 * 'force_exact' is needed to catch the following case:
		 *
		 *                initial     Here state 'succ' was processed first,
		 *                  |         it was eventually tracked to produce a
		 *                  V         state identical to 'hdr'.
		 *     .---------> hdr        All branches from 'succ' had been explored
		 *     |            |         and thus 'succ' has its .branches == 0.
		 *     |            V
		 *     |    .------...        Suppose states 'cur' and 'succ' correspond
		 *     |    |       |         to the same instruction + callsites.
		 *     |    V       V         In such case it is necessary to check
		 *     |   ...     ...        if 'succ' and 'cur' are states_equal().
		 *     |    |       |         If 'succ' and 'cur' are a part of the
		 *     |    V       V         same loop exact flag has to be set.
		 *     |   succ <- cur        To check if that is the case, verify
		 *     |    |                 if loop entry of 'succ' is in current
		 *     |    V                 DFS path.
		 *     |   ...
		 *     |    |
		 *     '----'
		 *
		 * Additional details are in the comment before get_loop_entry().
		 */
		loop_entry = get_loop_entry(&sl->state);
		force_exact = loop_entry && loop_entry->branches > 0;
		if (states_equal(env, &sl->state, cur, force_exact ? RANGE_WITHIN : NOT_EXACT)) {
			if (force_exact)
				update_loop_entry(cur, loop_entry);
hit:
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
			 * current state is equivalent to it (except precision marks)
			 * the precision needs to be propagated back in
			 * the current state.
			 */
			if (is_jmp_point(env, env->insn_idx))
				err = err ? : push_insn_history(env, cur, 0, 0);
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
		 * 'n' controls how many times state could miss before eviction.
		 * Use bigger 'n' for checkpoints because evicting checkpoint states
		 * too early would hinder iterator convergence.
		 */
		n = is_force_checkpoint(env, insn_idx) && sl->state.branches > 0 ? 64 : 3;
		if (sl->miss_cnt > sl->hit_cnt * n + n) {
			/* the state is unlikely to be useful. Remove it to
			 * speed up verification
			 */
			*pprev = sl->next;
			if (sl->state.frame[0]->regs[0].live & REG_LIVE_DONE &&
			    !sl->state.used_as_loop_entry) {
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
		return 0;

	if (!add_new_state)
		return 0;

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

	/* forget precise markings we inherited, see __mark_chain_precision */
	if (env->bpf_capable)
		mark_all_scalars_imprecise(env, cur);

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
	cur->insn_hist_start = cur->insn_hist_end;
	cur->dfs_depth = new->dfs_depth + 1;
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
	switch (base_type(type)) {
	case PTR_TO_CTX:
	case PTR_TO_SOCKET:
	case PTR_TO_SOCK_COMMON:
	case PTR_TO_TCP_SOCK:
	case PTR_TO_XDP_SOCK:
	case PTR_TO_BTF_ID:
	case PTR_TO_ARENA:
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

static int save_aux_ptr_type(struct bpf_verifier_env *env, enum bpf_reg_type type,
			     bool allow_trust_mismatch)
{
	enum bpf_reg_type *prev_type = &env->insn_aux_data[env->insn_idx].ptr_type;

	if (*prev_type == NOT_INIT) {
		/* Saw a valid insn
		 * dst_reg = *(u32 *)(src_reg + off)
		 * save type to validate intersecting paths
		 */
		*prev_type = type;
	} else if (reg_type_mismatch(type, *prev_type)) {
		/* Abuser program is trying to use the same insn
		 * dst_reg = *(u32*) (src_reg + off)
		 * with different pointer types:
		 * src_reg == ctx in one branch and
		 * src_reg == stack|map in some other branch.
		 * Reject it.
		 */
		if (allow_trust_mismatch &&
		    base_type(type) == PTR_TO_BTF_ID &&
		    base_type(*prev_type) == PTR_TO_BTF_ID) {
			/*
			 * Have to support a use case when one path through
			 * the program yields TRUSTED pointer while another
			 * is UNTRUSTED. Fallback to UNTRUSTED to generate
			 * BPF_PROBE_MEM/BPF_PROBE_MEMSX.
			 */
			*prev_type = PTR_TO_BTF_ID | PTR_UNTRUSTED;
		} else {
			verbose(env, "same insn cannot be used with different pointers\n");
			return -EINVAL;
		}
	}

	return 0;
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
		bool exception_exit = false;
		struct bpf_insn *insn;
		u8 class;
		int err;

		/* reset current history entry on each new instruction */
		env->cur_hist_ent = NULL;

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

		state->last_insn_idx = env->prev_insn_idx;

		if (is_prune_point(env, env->insn_idx)) {
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
		}

		if (is_jmp_point(env, env->insn_idx)) {
			err = push_insn_history(env, state, 0, 0);
			if (err)
				return err;
		}

		if (signal_pending(current))
			return -EAGAIN;

		if (need_resched())
			cond_resched();

		if (env->log.level & BPF_LOG_LEVEL2 && do_print_state) {
			verbose(env, "\nfrom %d to %d%s:",
				env->prev_insn_idx, env->insn_idx,
				env->cur_state->speculative ?
				" (speculative execution)" : "");
			print_verifier_state(env, state->frame[state->curframe], true);
			do_print_state = false;
		}

		if (env->log.level & BPF_LOG_LEVEL) {
			const struct bpf_insn_cbs cbs = {
				.cb_call	= disasm_kfunc_name,
				.cb_print	= verbose,
				.private_data	= env,
			};

			if (verifier_state_scratched(env))
				print_insn_state(env, state->frame[state->curframe]);

			verbose_linfo(env, env->insn_idx, "; ");
			env->prev_log_pos = env->log.end_pos;
			verbose(env, "%d: ", env->insn_idx);
			print_bpf_insn(&cbs, insn, env->allow_ptr_leaks);
			env->prev_insn_print_pos = env->log.end_pos - env->prev_log_pos;
			env->prev_log_pos = env->log.end_pos;
		}

		if (bpf_prog_is_offloaded(env->prog->aux)) {
			err = bpf_prog_offload_verify_insn(env, env->insn_idx,
							   env->prev_insn_idx);
			if (err)
				return err;
		}

		regs = cur_regs(env);
		sanitize_mark_insn_seen(env);
		prev_insn_idx = env->insn_idx;

		if (class == BPF_ALU || class == BPF_ALU64) {
			err = check_alu_op(env, insn);
			if (err)
				return err;

		} else if (class == BPF_LDX) {
			enum bpf_reg_type src_reg_type;

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
					       BPF_READ, insn->dst_reg, false,
					       BPF_MODE(insn->code) == BPF_MEMSX);
			err = err ?: save_aux_ptr_type(env, src_reg_type, true);
			err = err ?: reg_bounds_sanity_check(env, &regs[insn->dst_reg], "ldx");
			if (err)
				return err;
		} else if (class == BPF_STX) {
			enum bpf_reg_type dst_reg_type;

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
					       BPF_WRITE, insn->src_reg, false, false);
			if (err)
				return err;

			err = save_aux_ptr_type(env, dst_reg_type, false);
			if (err)
				return err;
		} else if (class == BPF_ST) {
			enum bpf_reg_type dst_reg_type;

			if (BPF_MODE(insn->code) != BPF_MEM ||
			    insn->src_reg != BPF_REG_0) {
				verbose(env, "BPF_ST uses reserved fields\n");
				return -EINVAL;
			}
			/* check src operand */
			err = check_reg_arg(env, insn->dst_reg, SRC_OP);
			if (err)
				return err;

			dst_reg_type = regs[insn->dst_reg].type;

			/* check that memory (dst_reg + off) is writeable */
			err = check_mem_access(env, env->insn_idx, insn->dst_reg,
					       insn->off, BPF_SIZE(insn->code),
					       BPF_WRITE, -1, false, false);
			if (err)
				return err;

			err = save_aux_ptr_type(env, dst_reg_type, false);
			if (err)
				return err;
		} else if (class == BPF_JMP || class == BPF_JMP32) {
			u8 opcode = BPF_OP(insn->code);

			env->jmps_processed++;
			if (opcode == BPF_CALL) {
				if (BPF_SRC(insn->code) != BPF_K ||
				    (insn->src_reg != BPF_PSEUDO_KFUNC_CALL
				     && insn->off != 0) ||
				    (insn->src_reg != BPF_REG_0 &&
				     insn->src_reg != BPF_PSEUDO_CALL &&
				     insn->src_reg != BPF_PSEUDO_KFUNC_CALL) ||
				    insn->dst_reg != BPF_REG_0 ||
				    class == BPF_JMP32) {
					verbose(env, "BPF_CALL uses reserved fields\n");
					return -EINVAL;
				}

				if (cur_func(env)->active_locks) {
					if ((insn->src_reg == BPF_REG_0 && insn->imm != BPF_FUNC_spin_unlock) ||
					    (insn->src_reg == BPF_PSEUDO_KFUNC_CALL &&
					     (insn->off != 0 || !is_bpf_graph_api_kfunc(insn->imm)))) {
						verbose(env, "function calls are not allowed while holding a lock\n");
						return -EINVAL;
					}
				}
				if (insn->src_reg == BPF_PSEUDO_CALL) {
					err = check_func_call(env, insn, &env->insn_idx);
				} else if (insn->src_reg == BPF_PSEUDO_KFUNC_CALL) {
					err = check_kfunc_call(env, insn, &env->insn_idx);
					if (!err && is_bpf_throw_kfunc(insn)) {
						exception_exit = true;
						goto process_bpf_exit_full;
					}
				} else {
					err = check_helper_call(env, insn, &env->insn_idx);
				}
				if (err)
					return err;

				mark_reg_scratched(env, BPF_REG_0);
			} else if (opcode == BPF_JA) {
				if (BPF_SRC(insn->code) != BPF_K ||
				    insn->src_reg != BPF_REG_0 ||
				    insn->dst_reg != BPF_REG_0 ||
				    (class == BPF_JMP && insn->imm != 0) ||
				    (class == BPF_JMP32 && insn->off != 0)) {
					verbose(env, "BPF_JA uses reserved fields\n");
					return -EINVAL;
				}

				if (class == BPF_JMP)
					env->insn_idx += insn->off + 1;
				else
					env->insn_idx += insn->imm + 1;
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
process_bpf_exit_full:
				/* We must do check_reference_leak here before
				 * prepare_func_exit to handle the case when
				 * state->curframe > 0, it may be a callback
				 * function, for which reference_state must
				 * match caller reference state when it exits.
				 */
				err = check_resource_leak(env, exception_exit, !env->cur_state->curframe,
							  "BPF_EXIT instruction");
				if (err)
					return err;

				/* The side effect of the prepare_func_exit
				 * which is being skipped is that it frees
				 * bpf_func_state. Typically, process_bpf_exit
				 * will only be hit with outermost exit.
				 * copy_verifier_state in pop_stack will handle
				 * freeing of any extra bpf_func_state left over
				 * from not processing all nested function
				 * exits. We also skip return code checks as
				 * they are not needed for exceptional exits.
				 */
				if (exception_exit)
					goto process_bpf_exit;

				if (state->curframe) {
					/* exit from nested function */
					err = prepare_func_exit(env, &env->insn_idx);
					if (err)
						return err;
					do_print_state = true;
					continue;
				}

				err = check_return_code(env, BPF_REG_0, "R0");
				if (err)
					return err;
process_bpf_exit:
				mark_verifier_state_scratched(env);
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
				sanitize_mark_insn_seen(env);
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

	if (!btf_type_is_var(t) && !btf_type_is_func(t)) {
		verbose(env, "pseudo btf_id %d in ldimm64 isn't KIND_VAR or KIND_FUNC\n", id);
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
	insn[0].imm = (u32)addr;
	insn[1].imm = addr >> 32;

	if (btf_type_is_func(t)) {
		aux->btf_var.reg_type = PTR_TO_MEM | MEM_RDONLY;
		aux->btf_var.mem_size = 0;
		goto check_btf;
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

	type = t->type;
	t = btf_type_skip_modifiers(btf, type, NULL);
	if (percpu) {
		aux->btf_var.reg_type = PTR_TO_BTF_ID | MEM_PERCPU;
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
		aux->btf_var.reg_type = PTR_TO_MEM | MEM_RDONLY;
		aux->btf_var.mem_size = tsize;
	} else {
		aux->btf_var.reg_type = PTR_TO_BTF_ID;
		aux->btf_var.btf = btf;
		aux->btf_var.btf_id = type;
	}
check_btf:
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

static bool is_tracing_prog_type(enum bpf_prog_type type)
{
	switch (type) {
	case BPF_PROG_TYPE_KPROBE:
	case BPF_PROG_TYPE_TRACEPOINT:
	case BPF_PROG_TYPE_PERF_EVENT:
	case BPF_PROG_TYPE_RAW_TRACEPOINT:
	case BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE:
		return true;
	default:
		return false;
	}
}

static int check_map_prog_compatibility(struct bpf_verifier_env *env,
					struct bpf_map *map,
					struct bpf_prog *prog)

{
	enum bpf_prog_type prog_type = resolve_prog_type(prog);

	if (btf_record_has_field(map->record, BPF_LIST_HEAD) ||
	    btf_record_has_field(map->record, BPF_RB_ROOT)) {
		if (is_tracing_prog_type(prog_type)) {
			verbose(env, "tracing progs cannot use bpf_{list_head,rb_root} yet\n");
			return -EINVAL;
		}
	}

	if (btf_record_has_field(map->record, BPF_SPIN_LOCK)) {
		if (prog_type == BPF_PROG_TYPE_SOCKET_FILTER) {
			verbose(env, "socket filter progs cannot use bpf_spin_lock yet\n");
			return -EINVAL;
		}

		if (is_tracing_prog_type(prog_type)) {
			verbose(env, "tracing progs cannot use bpf_spin_lock yet\n");
			return -EINVAL;
		}
	}

	if (btf_record_has_field(map->record, BPF_TIMER)) {
		if (is_tracing_prog_type(prog_type)) {
			verbose(env, "tracing progs cannot use bpf_timer yet\n");
			return -EINVAL;
		}
	}

	if (btf_record_has_field(map->record, BPF_WORKQUEUE)) {
		if (is_tracing_prog_type(prog_type)) {
			verbose(env, "tracing progs cannot use bpf_wq yet\n");
			return -EINVAL;
		}
	}

	if ((bpf_prog_is_offloaded(prog->aux) || bpf_map_is_offloaded(map)) &&
	    !bpf_offload_prog_map_match(prog, map)) {
		verbose(env, "offload device mismatch between prog and map\n");
		return -EINVAL;
	}

	if (map->map_type == BPF_MAP_TYPE_STRUCT_OPS) {
		verbose(env, "bpf_struct_ops map cannot be used in prog\n");
		return -EINVAL;
	}

	if (prog->sleepable)
		switch (map->map_type) {
		case BPF_MAP_TYPE_HASH:
		case BPF_MAP_TYPE_LRU_HASH:
		case BPF_MAP_TYPE_ARRAY:
		case BPF_MAP_TYPE_PERCPU_HASH:
		case BPF_MAP_TYPE_PERCPU_ARRAY:
		case BPF_MAP_TYPE_LRU_PERCPU_HASH:
		case BPF_MAP_TYPE_ARRAY_OF_MAPS:
		case BPF_MAP_TYPE_HASH_OF_MAPS:
		case BPF_MAP_TYPE_RINGBUF:
		case BPF_MAP_TYPE_USER_RINGBUF:
		case BPF_MAP_TYPE_INODE_STORAGE:
		case BPF_MAP_TYPE_SK_STORAGE:
		case BPF_MAP_TYPE_TASK_STORAGE:
		case BPF_MAP_TYPE_CGRP_STORAGE:
		case BPF_MAP_TYPE_QUEUE:
		case BPF_MAP_TYPE_STACK:
		case BPF_MAP_TYPE_ARENA:
			break;
		default:
			verbose(env,
				"Sleepable programs can only use array, hash, ringbuf and local storage maps\n");
			return -EINVAL;
		}

	return 0;
}

static bool bpf_map_is_cgroup_storage(struct bpf_map *map)
{
	return (map->map_type == BPF_MAP_TYPE_CGROUP_STORAGE ||
		map->map_type == BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE);
}

/* Add map behind fd to used maps list, if it's not already there, and return
 * its index. Also set *reused to true if this map was already in the list of
 * used maps.
 * Returns <0 on error, or >= 0 index, on success.
 */
static int add_used_map_from_fd(struct bpf_verifier_env *env, int fd, bool *reused)
{
	CLASS(fd, f)(fd);
	struct bpf_map *map;
	int i;

	map = __bpf_map_get(f);
	if (IS_ERR(map)) {
		verbose(env, "fd %d is not pointing to valid bpf_map\n", fd);
		return PTR_ERR(map);
	}

	/* check whether we recorded this map already */
	for (i = 0; i < env->used_map_cnt; i++) {
		if (env->used_maps[i] == map) {
			*reused = true;
			return i;
		}
	}

	if (env->used_map_cnt >= MAX_USED_MAPS) {
		verbose(env, "The total number of maps per program has reached the limit of %u\n",
			MAX_USED_MAPS);
		return -E2BIG;
	}

	if (env->prog->sleepable)
		atomic64_inc(&map->sleepable_refcnt);

	/* hold the map. If the program is rejected by verifier,
	 * the map will be released by release_maps() or it
	 * will be used by the valid program until it's unloaded
	 * and all maps are released in bpf_free_used_maps()
	 */
	bpf_map_inc(map);

	*reused = false;
	env->used_maps[env->used_map_cnt++] = map;

	return env->used_map_cnt - 1;
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
	int i, err;

	err = bpf_prog_calc_tag(env->prog);
	if (err)
		return err;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (BPF_CLASS(insn->code) == BPF_LDX &&
		    ((BPF_MODE(insn->code) != BPF_MEM && BPF_MODE(insn->code) != BPF_MEMSX) ||
		    insn->imm != 0)) {
			verbose(env, "BPF_LDX uses reserved fields\n");
			return -EINVAL;
		}

		if (insn[0].code == (BPF_LD | BPF_IMM | BPF_DW)) {
			struct bpf_insn_aux_data *aux;
			struct bpf_map *map;
			int map_idx;
			u64 addr;
			u32 fd;
			bool reused;

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

			if (insn[0].src_reg == BPF_PSEUDO_FUNC) {
				aux = &env->insn_aux_data[i];
				aux->ptr_type = PTR_TO_FUNC;
				goto next_insn;
			}

			/* In final convert_pseudo_ld_imm64() step, this is
			 * converted into regular 64-bit imm load insn.
			 */
			switch (insn[0].src_reg) {
			case BPF_PSEUDO_MAP_VALUE:
			case BPF_PSEUDO_MAP_IDX_VALUE:
				break;
			case BPF_PSEUDO_MAP_FD:
			case BPF_PSEUDO_MAP_IDX:
				if (insn[1].imm == 0)
					break;
				fallthrough;
			default:
				verbose(env, "unrecognized bpf_ld_imm64 insn\n");
				return -EINVAL;
			}

			switch (insn[0].src_reg) {
			case BPF_PSEUDO_MAP_IDX_VALUE:
			case BPF_PSEUDO_MAP_IDX:
				if (bpfptr_is_null(env->fd_array)) {
					verbose(env, "fd_idx without fd_array is invalid\n");
					return -EPROTO;
				}
				if (copy_from_bpfptr_offset(&fd, env->fd_array,
							    insn[0].imm * sizeof(fd),
							    sizeof(fd)))
					return -EFAULT;
				break;
			default:
				fd = insn[0].imm;
				break;
			}

			map_idx = add_used_map_from_fd(env, fd, &reused);
			if (map_idx < 0)
				return map_idx;
			map = env->used_maps[map_idx];

			aux = &env->insn_aux_data[i];
			aux->map_index = map_idx;

			err = check_map_prog_compatibility(env, map, env->prog);
			if (err)
				return err;

			if (insn[0].src_reg == BPF_PSEUDO_MAP_FD ||
			    insn[0].src_reg == BPF_PSEUDO_MAP_IDX) {
				addr = (unsigned long)map;
			} else {
				u32 off = insn[1].imm;

				if (off >= BPF_MAX_VAR_OFF) {
					verbose(env, "direct value offset of %u is not allowed\n", off);
					return -EINVAL;
				}

				if (!map->ops->map_direct_value_addr) {
					verbose(env, "no direct value access support for this map type\n");
					return -EINVAL;
				}

				err = map->ops->map_direct_value_addr(map, &addr, off);
				if (err) {
					verbose(env, "invalid access to map value pointer, value_size=%u off=%u\n",
						map->value_size, off);
					return err;
				}

				aux->map_off = off;
				addr += off;
			}

			insn[0].imm = (u32)addr;
			insn[1].imm = addr >> 32;

			/* proceed with extra checks only if its newly added used map */
			if (reused)
				goto next_insn;

			if (bpf_map_is_cgroup_storage(map) &&
			    bpf_cgroup_storage_assign(env->prog->aux, map)) {
				verbose(env, "only one cgroup storage of each type is allowed\n");
				return -EBUSY;
			}
			if (map->map_type == BPF_MAP_TYPE_ARENA) {
				if (env->prog->aux->arena) {
					verbose(env, "Only one arena per program\n");
					return -EBUSY;
				}
				if (!env->allow_ptr_leaks || !env->bpf_capable) {
					verbose(env, "CAP_BPF and CAP_PERFMON are required to use arena\n");
					return -EPERM;
				}
				if (!env->prog->jit_requested) {
					verbose(env, "JIT is required to use arena\n");
					return -EOPNOTSUPP;
				}
				if (!bpf_jit_supports_arena()) {
					verbose(env, "JIT doesn't support arena\n");
					return -EOPNOTSUPP;
				}
				env->prog->aux->arena = (void *)map;
				if (!bpf_arena_get_user_vm_start(env->prog->aux->arena)) {
					verbose(env, "arena's user address must be set via map_extra or mmap()\n");
					return -EINVAL;
				}
			}

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
	__bpf_free_used_btfs(env->used_btfs, env->used_btf_cnt);
}

/* convert pseudo BPF_LD_IMM64 into generic BPF_LD_IMM64 */
static void convert_pseudo_ld_imm64(struct bpf_verifier_env *env)
{
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	int i;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (insn->code != (BPF_LD | BPF_IMM | BPF_DW))
			continue;
		if (insn->src_reg == BPF_PSEUDO_FUNC)
			continue;
		insn->src_reg = 0;
	}
}

/* single env->prog->insni[off] instruction was replaced with the range
 * insni[off, off + cnt).  Adjust corresponding insn_aux_data by copying
 * [0, off) and [off, end) to new locations, so the patched range stays zero
 */
static void adjust_insn_aux_data(struct bpf_verifier_env *env,
				 struct bpf_insn_aux_data *new_data,
				 struct bpf_prog *new_prog, u32 off, u32 cnt)
{
	struct bpf_insn_aux_data *old_data = env->insn_aux_data;
	struct bpf_insn *insn = new_prog->insnsi;
	u32 old_seen = old_data[off].seen;
	u32 prog_len;
	int i;

	/* aux info at OFF always needs adjustment, no matter fast path
	 * (cnt == 1) is taken or not. There is no guarantee INSN at OFF is the
	 * original insn at old prog.
	 */
	old_data[off].zext_dst = insn_has_def32(env, insn + off + cnt - 1);

	if (cnt == 1)
		return;
	prog_len = new_prog->len;

	memcpy(new_data, old_data, sizeof(struct bpf_insn_aux_data) * off);
	memcpy(new_data + off + cnt - 1, old_data + off,
	       sizeof(struct bpf_insn_aux_data) * (prog_len - off - cnt + 1));
	for (i = off; i < off + cnt - 1; i++) {
		/* Expand insni[off]'s seen count to the patched range. */
		new_data[i].seen = old_seen;
		new_data[i].zext_dst = insn_has_def32(env, insn + i);
	}
	env->insn_aux_data = new_data;
	vfree(old_data);
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

static void adjust_poke_descs(struct bpf_prog *prog, u32 off, u32 len)
{
	struct bpf_jit_poke_descriptor *tab = prog->aux->poke_tab;
	int i, sz = prog->aux->size_poke_tab;
	struct bpf_jit_poke_descriptor *desc;

	for (i = 0; i < sz; i++) {
		desc = &tab[i];
		if (desc->insn_idx <= off)
			continue;
		desc->insn_idx += len - 1;
	}
}

static struct bpf_prog *bpf_patch_insn_data(struct bpf_verifier_env *env, u32 off,
					    const struct bpf_insn *patch, u32 len)
{
	struct bpf_prog *new_prog;
	struct bpf_insn_aux_data *new_data = NULL;

	if (len > 1) {
		new_data = vzalloc(array_size(env->prog->len + len - 1,
					      sizeof(struct bpf_insn_aux_data)));
		if (!new_data)
			return NULL;
	}

	new_prog = bpf_patch_insn_single(env->prog, off, patch, len);
	if (IS_ERR(new_prog)) {
		if (PTR_ERR(new_prog) == -ERANGE)
			verbose(env,
				"insn %d cannot be patched due to 16-bit range\n",
				env->insn_aux_data[off].orig_idx);
		vfree(new_data);
		return NULL;
	}
	adjust_insn_aux_data(env, new_data, new_prog, off, len);
	adjust_subprog_starts(env, off, len);
	adjust_poke_descs(new_prog, off, len);
	return new_prog;
}

/*
 * For all jmp insns in a given 'prog' that point to 'tgt_idx' insn adjust the
 * jump offset by 'delta'.
 */
static int adjust_jmp_off(struct bpf_prog *prog, u32 tgt_idx, u32 delta)
{
	struct bpf_insn *insn = prog->insnsi;
	u32 insn_cnt = prog->len, i;
	s32 imm;
	s16 off;

	for (i = 0; i < insn_cnt; i++, insn++) {
		u8 code = insn->code;

		if (tgt_idx <= i && i < tgt_idx + delta)
			continue;

		if ((BPF_CLASS(code) != BPF_JMP && BPF_CLASS(code) != BPF_JMP32) ||
		    BPF_OP(code) == BPF_CALL || BPF_OP(code) == BPF_EXIT)
			continue;

		if (insn->code == (BPF_JMP32 | BPF_JA)) {
			if (i + 1 + insn->imm != tgt_idx)
				continue;
			if (check_add_overflow(insn->imm, delta, &imm))
				return -ERANGE;
			insn->imm = imm;
		} else {
			if (i + 1 + insn->off != tgt_idx)
				continue;
			if (check_add_overflow(insn->off, delta, &off))
				return -ERANGE;
			insn->off = off;
		}
	}
	return 0;
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

	if (bpf_prog_is_offloaded(env->prog->aux))
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
		aux_data[i].zext_dst = false;
	}
}

static bool insn_is_cond_jump(u8 code)
{
	u8 op;

	op = BPF_OP(code);
	if (BPF_CLASS(code) == BPF_JMP32)
		return op != BPF_JA;

	if (BPF_CLASS(code) != BPF_JMP)
		return false;

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

		if (bpf_prog_is_offloaded(env->prog->aux))
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

static const struct bpf_insn NOP = BPF_JMP_IMM(BPF_JA, 0, 0, 0);

static int opt_remove_nops(struct bpf_verifier_env *env)
{
	const struct bpf_insn ja = NOP;
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

			imm_rnd = get_random_u32();
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

		/* Zero-extension is done by the caller. */
		if (bpf_pseudo_kfunc_call(&insn))
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
	struct bpf_subprog_info *subprogs = env->subprog_info;
	const struct bpf_verifier_ops *ops = env->ops;
	int i, cnt, size, ctx_field_size, delta = 0, epilogue_cnt = 0;
	const int insn_cnt = env->prog->len;
	struct bpf_insn *epilogue_buf = env->epilogue_buf;
	struct bpf_insn *insn_buf = env->insn_buf;
	struct bpf_insn *insn;
	u32 target_size, size_default, off;
	struct bpf_prog *new_prog;
	enum bpf_access_type type;
	bool is_narrower_load;
	int epilogue_idx = 0;

	if (ops->gen_epilogue) {
		epilogue_cnt = ops->gen_epilogue(epilogue_buf, env->prog,
						 -(subprogs[0].stack_depth + 8));
		if (epilogue_cnt >= INSN_BUF_SIZE) {
			verbose(env, "bpf verifier is misconfigured\n");
			return -EINVAL;
		} else if (epilogue_cnt) {
			/* Save the ARG_PTR_TO_CTX for the epilogue to use */
			cnt = 0;
			subprogs[0].stack_depth += 8;
			insn_buf[cnt++] = BPF_STX_MEM(BPF_DW, BPF_REG_FP, BPF_REG_1,
						      -subprogs[0].stack_depth);
			insn_buf[cnt++] = env->prog->insnsi[0];
			new_prog = bpf_patch_insn_data(env, 0, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;
			env->prog = new_prog;
			delta += cnt - 1;
		}
	}

	if (ops->gen_prologue || env->seen_direct_write) {
		if (!ops->gen_prologue) {
			verbose(env, "bpf verifier is misconfigured\n");
			return -EINVAL;
		}
		cnt = ops->gen_prologue(insn_buf, env->seen_direct_write,
					env->prog);
		if (cnt >= INSN_BUF_SIZE) {
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

	if (delta)
		WARN_ON(adjust_jmp_off(env->prog, 0, delta));

	if (bpf_prog_is_offloaded(env->prog->aux))
		return 0;

	insn = env->prog->insnsi + delta;

	for (i = 0; i < insn_cnt; i++, insn++) {
		bpf_convert_ctx_access_t convert_ctx_access;
		u8 mode;

		if (insn->code == (BPF_LDX | BPF_MEM | BPF_B) ||
		    insn->code == (BPF_LDX | BPF_MEM | BPF_H) ||
		    insn->code == (BPF_LDX | BPF_MEM | BPF_W) ||
		    insn->code == (BPF_LDX | BPF_MEM | BPF_DW) ||
		    insn->code == (BPF_LDX | BPF_MEMSX | BPF_B) ||
		    insn->code == (BPF_LDX | BPF_MEMSX | BPF_H) ||
		    insn->code == (BPF_LDX | BPF_MEMSX | BPF_W)) {
			type = BPF_READ;
		} else if (insn->code == (BPF_STX | BPF_MEM | BPF_B) ||
			   insn->code == (BPF_STX | BPF_MEM | BPF_H) ||
			   insn->code == (BPF_STX | BPF_MEM | BPF_W) ||
			   insn->code == (BPF_STX | BPF_MEM | BPF_DW) ||
			   insn->code == (BPF_ST | BPF_MEM | BPF_B) ||
			   insn->code == (BPF_ST | BPF_MEM | BPF_H) ||
			   insn->code == (BPF_ST | BPF_MEM | BPF_W) ||
			   insn->code == (BPF_ST | BPF_MEM | BPF_DW)) {
			type = BPF_WRITE;
		} else if ((insn->code == (BPF_STX | BPF_ATOMIC | BPF_W) ||
			    insn->code == (BPF_STX | BPF_ATOMIC | BPF_DW)) &&
			   env->insn_aux_data[i + delta].ptr_type == PTR_TO_ARENA) {
			insn->code = BPF_STX | BPF_PROBE_ATOMIC | BPF_SIZE(insn->code);
			env->prog->aux->num_exentries++;
			continue;
		} else if (insn->code == (BPF_JMP | BPF_EXIT) &&
			   epilogue_cnt &&
			   i + delta < subprogs[1].start) {
			/* Generate epilogue for the main prog */
			if (epilogue_idx) {
				/* jump back to the earlier generated epilogue */
				insn_buf[0] = BPF_JMP32_A(epilogue_idx - i - delta - 1);
				cnt = 1;
			} else {
				memcpy(insn_buf, epilogue_buf,
				       epilogue_cnt * sizeof(*epilogue_buf));
				cnt = epilogue_cnt;
				/* epilogue_idx cannot be 0. It must have at
				 * least one ctx ptr saving insn before the
				 * epilogue.
				 */
				epilogue_idx = i + delta;
			}
			goto patch_insn_buf;
		} else {
			continue;
		}

		if (type == BPF_WRITE &&
		    env->insn_aux_data[i + delta].sanitize_stack_spill) {
			struct bpf_insn patch[] = {
				*insn,
				BPF_ST_NOSPEC(),
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

		switch ((int)env->insn_aux_data[i + delta].ptr_type) {
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
		case PTR_TO_BTF_ID | PTR_UNTRUSTED:
		/* PTR_TO_BTF_ID | MEM_ALLOC always has a valid lifetime, unlike
		 * PTR_TO_BTF_ID, and an active ref_obj_id, but the same cannot
		 * be said once it is marked PTR_UNTRUSTED, hence we must handle
		 * any faults for loads into such types. BPF_WRITE is disallowed
		 * for this case.
		 */
		case PTR_TO_BTF_ID | MEM_ALLOC | PTR_UNTRUSTED:
		case PTR_TO_BTF_ID | PTR_TRUSTED | PTR_MAYBE_NULL:
			if (type == BPF_READ) {
				if (BPF_MODE(insn->code) == BPF_MEM)
					insn->code = BPF_LDX | BPF_PROBE_MEM |
						     BPF_SIZE((insn)->code);
				else
					insn->code = BPF_LDX | BPF_PROBE_MEMSX |
						     BPF_SIZE((insn)->code);
				env->prog->aux->num_exentries++;
			}
			continue;
		case PTR_TO_ARENA:
			if (BPF_MODE(insn->code) == BPF_MEMSX) {
				verbose(env, "sign extending loads from arena are not supported yet\n");
				return -EOPNOTSUPP;
			}
			insn->code = BPF_CLASS(insn->code) | BPF_PROBE_MEM32 | BPF_SIZE(insn->code);
			env->prog->aux->num_exentries++;
			continue;
		default:
			continue;
		}

		ctx_field_size = env->insn_aux_data[i + delta].ctx_field_size;
		size = BPF_LDST_BYTES(insn);
		mode = BPF_MODE(insn->code);

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
		if (cnt == 0 || cnt >= INSN_BUF_SIZE ||
		    (ctx_field_size && !target_size)) {
			verbose(env, "bpf verifier is misconfigured\n");
			return -EINVAL;
		}

		if (is_narrower_load && size < target_size) {
			u8 shift = bpf_ctx_narrow_access_offset(
				off, size, size_default) * 8;
			if (shift && cnt + 1 >= INSN_BUF_SIZE) {
				verbose(env, "bpf verifier narrow ctx load misconfigured\n");
				return -EINVAL;
			}
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
				insn_buf[cnt++] = BPF_ALU32_IMM(BPF_AND, insn->dst_reg,
								(1ULL << size * 8) - 1);
			}
		}
		if (mode == BPF_MEMSX)
			insn_buf[cnt++] = BPF_RAW_INSN(BPF_ALU64 | BPF_MOV | BPF_X,
						       insn->dst_reg, insn->dst_reg,
						       size * 8, 0);

patch_insn_buf:
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
		if (!bpf_pseudo_func(insn) && !bpf_pseudo_call(insn))
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
		if (bpf_pseudo_func(insn)) {
#if defined(MODULES_VADDR)
			u64 addr = MODULES_VADDR;
#else
			u64 addr = VMALLOC_START;
#endif
			/* jit (e.g. x86_64) may emit fewer instructions
			 * if it learns a u32 imm is the same as a u64 imm.
			 * Set close enough to possible prog address.
			 */
			insn[0].imm = (u32)addr;
			insn[1].imm = addr >> 32;
		}
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
		/* bpf_prog_run() doesn't call subprogs directly,
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
		func[i]->sleepable = prog->sleepable;
		func[i]->aux->func_idx = i;
		/* Below members will be freed only at prog->aux */
		func[i]->aux->btf = prog->aux->btf;
		func[i]->aux->func_info = prog->aux->func_info;
		func[i]->aux->func_info_cnt = prog->aux->func_info_cnt;
		func[i]->aux->poke_tab = prog->aux->poke_tab;
		func[i]->aux->size_poke_tab = prog->aux->size_poke_tab;

		for (j = 0; j < prog->aux->size_poke_tab; j++) {
			struct bpf_jit_poke_descriptor *poke;

			poke = &prog->aux->poke_tab[j];
			if (poke->insn_idx < subprog_end &&
			    poke->insn_idx >= subprog_start)
				poke->aux = func[i]->aux;
		}

		func[i]->aux->name[0] = 'F';
		func[i]->aux->stack_depth = env->subprog_info[i].stack_depth;
		if (env->subprog_info[i].priv_stack_mode == PRIV_STACK_ADAPTIVE)
			func[i]->aux->jits_use_priv_stack = true;

		func[i]->jit_requested = 1;
		func[i]->blinding_requested = prog->blinding_requested;
		func[i]->aux->kfunc_tab = prog->aux->kfunc_tab;
		func[i]->aux->kfunc_btf_tab = prog->aux->kfunc_btf_tab;
		func[i]->aux->linfo = prog->aux->linfo;
		func[i]->aux->nr_linfo = prog->aux->nr_linfo;
		func[i]->aux->jited_linfo = prog->aux->jited_linfo;
		func[i]->aux->linfo_idx = env->subprog_info[i].linfo_idx;
		func[i]->aux->arena = prog->aux->arena;
		num_exentries = 0;
		insn = func[i]->insnsi;
		for (j = 0; j < func[i]->len; j++, insn++) {
			if (BPF_CLASS(insn->code) == BPF_LDX &&
			    (BPF_MODE(insn->code) == BPF_PROBE_MEM ||
			     BPF_MODE(insn->code) == BPF_PROBE_MEM32 ||
			     BPF_MODE(insn->code) == BPF_PROBE_MEMSX))
				num_exentries++;
			if ((BPF_CLASS(insn->code) == BPF_STX ||
			     BPF_CLASS(insn->code) == BPF_ST) &&
			     BPF_MODE(insn->code) == BPF_PROBE_MEM32)
				num_exentries++;
			if (BPF_CLASS(insn->code) == BPF_STX &&
			     BPF_MODE(insn->code) == BPF_PROBE_ATOMIC)
				num_exentries++;
		}
		func[i]->aux->num_exentries = num_exentries;
		func[i]->aux->tail_call_reachable = env->subprog_info[i].tail_call_reachable;
		func[i]->aux->exception_cb = env->subprog_info[i].is_exception_cb;
		if (!i)
			func[i]->aux->exception_boundary = env->seen_exception;
		func[i] = bpf_int_jit_compile(func[i]);
		if (!func[i]->jited) {
			err = -ENOTSUPP;
			goto out_free;
		}
		cond_resched();
	}

	/* at this point all bpf functions were successfully JITed
	 * now populate all bpf_calls with correct addresses and
	 * run last pass of JIT
	 */
	for (i = 0; i < env->subprog_cnt; i++) {
		insn = func[i]->insnsi;
		for (j = 0; j < func[i]->len; j++, insn++) {
			if (bpf_pseudo_func(insn)) {
				subprog = insn->off;
				insn[0].imm = (u32)(long)func[subprog]->bpf_func;
				insn[1].imm = ((u64)(long)func[subprog]->bpf_func) >> 32;
				continue;
			}
			if (!bpf_pseudo_call(insn))
				continue;
			subprog = insn->off;
			insn->imm = BPF_CALL_IMM(func[subprog]->bpf_func);
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
		func[i]->aux->func_cnt = env->subprog_cnt - env->hidden_subprog_cnt;
		func[i]->aux->real_func_cnt = env->subprog_cnt;
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
	 * populate kallsysm. Begin at the first subprogram, since
	 * bpf_prog_load will add the kallsyms for the main program.
	 */
	for (i = 1; i < env->subprog_cnt; i++) {
		err = bpf_prog_lock_ro(func[i]);
		if (err)
			goto out_free;
	}

	for (i = 1; i < env->subprog_cnt; i++)
		bpf_prog_kallsyms_add(func[i]);

	/* Last step: make now unused interpreter insns from main
	 * prog consistent for later dump requests, so they can
	 * later look the same as if they were interpreted only.
	 */
	for (i = 0, insn = prog->insnsi; i < prog->len; i++, insn++) {
		if (bpf_pseudo_func(insn)) {
			insn[0].imm = env->insn_aux_data[i].call_imm;
			insn[1].imm = insn->off;
			insn->off = 0;
			continue;
		}
		if (!bpf_pseudo_call(insn))
			continue;
		insn->off = env->insn_aux_data[i].call_imm;
		subprog = find_subprog(env, i + insn->off + 1);
		insn->imm = subprog;
	}

	prog->jited = 1;
	prog->bpf_func = func[0]->bpf_func;
	prog->jited_len = func[0]->jited_len;
	prog->aux->extable = func[0]->aux->extable;
	prog->aux->num_exentries = func[0]->aux->num_exentries;
	prog->aux->func = func;
	prog->aux->func_cnt = env->subprog_cnt - env->hidden_subprog_cnt;
	prog->aux->real_func_cnt = env->subprog_cnt;
	prog->aux->bpf_exception_cb = (void *)func[env->exception_callback_subprog]->bpf_func;
	prog->aux->exception_boundary = func[0]->aux->exception_boundary;
	bpf_prog_jit_attempt_done(prog);
	return 0;
out_free:
	/* We failed JIT'ing, so at this point we need to unregister poke
	 * descriptors from subprogs, so that kernel is not attempting to
	 * patch it anymore as we're freeing the subprog JIT memory.
	 */
	for (i = 0; i < prog->aux->size_poke_tab; i++) {
		map_ptr = prog->aux->poke_tab[i].tail_call.map;
		map_ptr->ops->map_poke_untrack(map_ptr, prog->aux);
	}
	/* At this point we're guaranteed that poke descriptors are not
	 * live anymore. We can just unlink its descriptor table as it's
	 * released with the main prog.
	 */
	for (i = 0; i < env->subprog_cnt; i++) {
		if (!func[i])
			continue;
		func[i]->aux->poke_tab = NULL;
		bpf_jit_free(func[i]);
	}
	kfree(func);
out_undo_insn:
	/* cleanup main prog to be interpreted */
	prog->jit_requested = 0;
	prog->blinding_requested = 0;
	for (i = 0, insn = prog->insnsi; i < prog->len; i++, insn++) {
		if (!bpf_pseudo_call(insn))
			continue;
		insn->off = 0;
		insn->imm = env->insn_aux_data[i].call_imm;
	}
	bpf_prog_jit_attempt_done(prog);
	return err;
}

static int fixup_call_args(struct bpf_verifier_env *env)
{
#ifndef CONFIG_BPF_JIT_ALWAYS_ON
	struct bpf_prog *prog = env->prog;
	struct bpf_insn *insn = prog->insnsi;
	bool has_kfunc_call = bpf_prog_has_kfunc_call(prog);
	int i, depth;
#endif
	int err = 0;

	if (env->prog->jit_requested &&
	    !bpf_prog_is_offloaded(env->prog->aux)) {
		err = jit_subprogs(env);
		if (err == 0)
			return 0;
		if (err == -EFAULT)
			return err;
	}
#ifndef CONFIG_BPF_JIT_ALWAYS_ON
	if (has_kfunc_call) {
		verbose(env, "calling kernel functions are not allowed in non-JITed programs\n");
		return -EINVAL;
	}
	if (env->subprog_cnt > 1 && env->prog->aux->tail_call_reachable) {
		/* When JIT fails the progs with bpf2bpf calls and tail_calls
		 * have to be rejected, since interpreter doesn't support them yet.
		 */
		verbose(env, "tail_calls are not allowed in non-JITed programs with bpf-to-bpf calls\n");
		return -EINVAL;
	}
	for (i = 0; i < prog->len; i++, insn++) {
		if (bpf_pseudo_func(insn)) {
			/* When JIT fails the progs with callback calls
			 * have to be rejected, since interpreter doesn't support them yet.
			 */
			verbose(env, "callbacks are not allowed in non-JITed programs\n");
			return -EINVAL;
		}

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

/* replace a generic kfunc with a specialized version if necessary */
static void specialize_kfunc(struct bpf_verifier_env *env,
			     u32 func_id, u16 offset, unsigned long *addr)
{
	struct bpf_prog *prog = env->prog;
	bool seen_direct_write;
	void *xdp_kfunc;
	bool is_rdonly;

	if (bpf_dev_bound_kfunc_id(func_id)) {
		xdp_kfunc = bpf_dev_bound_resolve_kfunc(prog, func_id);
		if (xdp_kfunc) {
			*addr = (unsigned long)xdp_kfunc;
			return;
		}
		/* fallback to default kfunc when not supported by netdev */
	}

	if (offset)
		return;

	if (func_id == special_kfunc_list[KF_bpf_dynptr_from_skb]) {
		seen_direct_write = env->seen_direct_write;
		is_rdonly = !may_access_direct_pkt_data(env, NULL, BPF_WRITE);

		if (is_rdonly)
			*addr = (unsigned long)bpf_dynptr_from_skb_rdonly;

		/* restore env->seen_direct_write to its original value, since
		 * may_access_direct_pkt_data mutates it
		 */
		env->seen_direct_write = seen_direct_write;
	}
}

static void __fixup_collection_insert_kfunc(struct bpf_insn_aux_data *insn_aux,
					    u16 struct_meta_reg,
					    u16 node_offset_reg,
					    struct bpf_insn *insn,
					    struct bpf_insn *insn_buf,
					    int *cnt)
{
	struct btf_struct_meta *kptr_struct_meta = insn_aux->kptr_struct_meta;
	struct bpf_insn addr[2] = { BPF_LD_IMM64(struct_meta_reg, (long)kptr_struct_meta) };

	insn_buf[0] = addr[0];
	insn_buf[1] = addr[1];
	insn_buf[2] = BPF_MOV64_IMM(node_offset_reg, insn_aux->insert_off);
	insn_buf[3] = *insn;
	*cnt = 4;
}

static int fixup_kfunc_call(struct bpf_verifier_env *env, struct bpf_insn *insn,
			    struct bpf_insn *insn_buf, int insn_idx, int *cnt)
{
	const struct bpf_kfunc_desc *desc;

	if (!insn->imm) {
		verbose(env, "invalid kernel function call not eliminated in verifier pass\n");
		return -EINVAL;
	}

	*cnt = 0;

	/* insn->imm has the btf func_id. Replace it with an offset relative to
	 * __bpf_call_base, unless the JIT needs to call functions that are
	 * further than 32 bits away (bpf_jit_supports_far_kfunc_call()).
	 */
	desc = find_kfunc_desc(env->prog, insn->imm, insn->off);
	if (!desc) {
		verbose(env, "verifier internal error: kernel function descriptor not found for func_id %u\n",
			insn->imm);
		return -EFAULT;
	}

	if (!bpf_jit_supports_far_kfunc_call())
		insn->imm = BPF_CALL_IMM(desc->addr);
	if (insn->off)
		return 0;
	if (desc->func_id == special_kfunc_list[KF_bpf_obj_new_impl] ||
	    desc->func_id == special_kfunc_list[KF_bpf_percpu_obj_new_impl]) {
		struct btf_struct_meta *kptr_struct_meta = env->insn_aux_data[insn_idx].kptr_struct_meta;
		struct bpf_insn addr[2] = { BPF_LD_IMM64(BPF_REG_2, (long)kptr_struct_meta) };
		u64 obj_new_size = env->insn_aux_data[insn_idx].obj_new_size;

		if (desc->func_id == special_kfunc_list[KF_bpf_percpu_obj_new_impl] && kptr_struct_meta) {
			verbose(env, "verifier internal error: NULL kptr_struct_meta expected at insn_idx %d\n",
				insn_idx);
			return -EFAULT;
		}

		insn_buf[0] = BPF_MOV64_IMM(BPF_REG_1, obj_new_size);
		insn_buf[1] = addr[0];
		insn_buf[2] = addr[1];
		insn_buf[3] = *insn;
		*cnt = 4;
	} else if (desc->func_id == special_kfunc_list[KF_bpf_obj_drop_impl] ||
		   desc->func_id == special_kfunc_list[KF_bpf_percpu_obj_drop_impl] ||
		   desc->func_id == special_kfunc_list[KF_bpf_refcount_acquire_impl]) {
		struct btf_struct_meta *kptr_struct_meta = env->insn_aux_data[insn_idx].kptr_struct_meta;
		struct bpf_insn addr[2] = { BPF_LD_IMM64(BPF_REG_2, (long)kptr_struct_meta) };

		if (desc->func_id == special_kfunc_list[KF_bpf_percpu_obj_drop_impl] && kptr_struct_meta) {
			verbose(env, "verifier internal error: NULL kptr_struct_meta expected at insn_idx %d\n",
				insn_idx);
			return -EFAULT;
		}

		if (desc->func_id == special_kfunc_list[KF_bpf_refcount_acquire_impl] &&
		    !kptr_struct_meta) {
			verbose(env, "verifier internal error: kptr_struct_meta expected at insn_idx %d\n",
				insn_idx);
			return -EFAULT;
		}

		insn_buf[0] = addr[0];
		insn_buf[1] = addr[1];
		insn_buf[2] = *insn;
		*cnt = 3;
	} else if (desc->func_id == special_kfunc_list[KF_bpf_list_push_back_impl] ||
		   desc->func_id == special_kfunc_list[KF_bpf_list_push_front_impl] ||
		   desc->func_id == special_kfunc_list[KF_bpf_rbtree_add_impl]) {
		struct btf_struct_meta *kptr_struct_meta = env->insn_aux_data[insn_idx].kptr_struct_meta;
		int struct_meta_reg = BPF_REG_3;
		int node_offset_reg = BPF_REG_4;

		/* rbtree_add has extra 'less' arg, so args-to-fixup are in diff regs */
		if (desc->func_id == special_kfunc_list[KF_bpf_rbtree_add_impl]) {
			struct_meta_reg = BPF_REG_4;
			node_offset_reg = BPF_REG_5;
		}

		if (!kptr_struct_meta) {
			verbose(env, "verifier internal error: kptr_struct_meta expected at insn_idx %d\n",
				insn_idx);
			return -EFAULT;
		}

		__fixup_collection_insert_kfunc(&env->insn_aux_data[insn_idx], struct_meta_reg,
						node_offset_reg, insn, insn_buf, cnt);
	} else if (desc->func_id == special_kfunc_list[KF_bpf_cast_to_kern_ctx] ||
		   desc->func_id == special_kfunc_list[KF_bpf_rdonly_cast]) {
		insn_buf[0] = BPF_MOV64_REG(BPF_REG_0, BPF_REG_1);
		*cnt = 1;
	} else if (is_bpf_wq_set_callback_impl_kfunc(desc->func_id)) {
		struct bpf_insn ld_addrs[2] = { BPF_LD_IMM64(BPF_REG_4, (long)env->prog->aux) };

		insn_buf[0] = ld_addrs[0];
		insn_buf[1] = ld_addrs[1];
		insn_buf[2] = *insn;
		*cnt = 3;
	}
	return 0;
}

/* The function requires that first instruction in 'patch' is insnsi[prog->len - 1] */
static int add_hidden_subprog(struct bpf_verifier_env *env, struct bpf_insn *patch, int len)
{
	struct bpf_subprog_info *info = env->subprog_info;
	int cnt = env->subprog_cnt;
	struct bpf_prog *prog;

	/* We only reserve one slot for hidden subprogs in subprog_info. */
	if (env->hidden_subprog_cnt) {
		verbose(env, "verifier internal error: only one hidden subprog supported\n");
		return -EFAULT;
	}
	/* We're not patching any existing instruction, just appending the new
	 * ones for the hidden subprog. Hence all of the adjustment operations
	 * in bpf_patch_insn_data are no-ops.
	 */
	prog = bpf_patch_insn_data(env, env->prog->len - 1, patch, len);
	if (!prog)
		return -ENOMEM;
	env->prog = prog;
	info[cnt + 1].start = info[cnt].start;
	info[cnt].start = prog->len - len + 1;
	env->subprog_cnt++;
	env->hidden_subprog_cnt++;
	return 0;
}

/* Do various post-verification rewrites in a single program pass.
 * These rewrites simplify JIT and interpreter implementations.
 */
static int do_misc_fixups(struct bpf_verifier_env *env)
{
	struct bpf_prog *prog = env->prog;
	enum bpf_attach_type eatype = prog->expected_attach_type;
	enum bpf_prog_type prog_type = resolve_prog_type(prog);
	struct bpf_insn *insn = prog->insnsi;
	const struct bpf_func_proto *fn;
	const int insn_cnt = prog->len;
	const struct bpf_map_ops *ops;
	struct bpf_insn_aux_data *aux;
	struct bpf_insn *insn_buf = env->insn_buf;
	struct bpf_prog *new_prog;
	struct bpf_map *map_ptr;
	int i, ret, cnt, delta = 0, cur_subprog = 0;
	struct bpf_subprog_info *subprogs = env->subprog_info;
	u16 stack_depth = subprogs[cur_subprog].stack_depth;
	u16 stack_depth_extra = 0;

	if (env->seen_exception && !env->exception_callback_subprog) {
		struct bpf_insn patch[] = {
			env->prog->insnsi[insn_cnt - 1],
			BPF_MOV64_REG(BPF_REG_0, BPF_REG_1),
			BPF_EXIT_INSN(),
		};

		ret = add_hidden_subprog(env, patch, ARRAY_SIZE(patch));
		if (ret < 0)
			return ret;
		prog = env->prog;
		insn = prog->insnsi;

		env->exception_callback_subprog = env->subprog_cnt - 1;
		/* Don't update insn_cnt, as add_hidden_subprog always appends insns */
		mark_subprog_exc_cb(env, env->exception_callback_subprog);
	}

	for (i = 0; i < insn_cnt;) {
		if (insn->code == (BPF_ALU64 | BPF_MOV | BPF_X) && insn->imm) {
			if ((insn->off == BPF_ADDR_SPACE_CAST && insn->imm == 1) ||
			    (((struct bpf_map *)env->prog->aux->arena)->map_flags & BPF_F_NO_USER_CONV)) {
				/* convert to 32-bit mov that clears upper 32-bit */
				insn->code = BPF_ALU | BPF_MOV | BPF_X;
				/* clear off and imm, so it's a normal 'wX = wY' from JIT pov */
				insn->off = 0;
				insn->imm = 0;
			} /* cast from as(0) to as(1) should be handled by JIT */
			goto next_insn;
		}

		if (env->insn_aux_data[i + delta].needs_zext)
			/* Convert BPF_CLASS(insn->code) == BPF_ALU64 to 32-bit ALU */
			insn->code = BPF_ALU | BPF_OP(insn->code) | BPF_SRC(insn->code);

		/* Make sdiv/smod divide-by-minus-one exceptions impossible. */
		if ((insn->code == (BPF_ALU64 | BPF_MOD | BPF_K) ||
		     insn->code == (BPF_ALU64 | BPF_DIV | BPF_K) ||
		     insn->code == (BPF_ALU | BPF_MOD | BPF_K) ||
		     insn->code == (BPF_ALU | BPF_DIV | BPF_K)) &&
		    insn->off == 1 && insn->imm == -1) {
			bool is64 = BPF_CLASS(insn->code) == BPF_ALU64;
			bool isdiv = BPF_OP(insn->code) == BPF_DIV;
			struct bpf_insn *patchlet;
			struct bpf_insn chk_and_sdiv[] = {
				BPF_RAW_INSN((is64 ? BPF_ALU64 : BPF_ALU) |
					     BPF_NEG | BPF_K, insn->dst_reg,
					     0, 0, 0),
			};
			struct bpf_insn chk_and_smod[] = {
				BPF_MOV32_IMM(insn->dst_reg, 0),
			};

			patchlet = isdiv ? chk_and_sdiv : chk_and_smod;
			cnt = isdiv ? ARRAY_SIZE(chk_and_sdiv) : ARRAY_SIZE(chk_and_smod);

			new_prog = bpf_patch_insn_data(env, i + delta, patchlet, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		/* Make divide-by-zero and divide-by-minus-one exceptions impossible. */
		if (insn->code == (BPF_ALU64 | BPF_MOD | BPF_X) ||
		    insn->code == (BPF_ALU64 | BPF_DIV | BPF_X) ||
		    insn->code == (BPF_ALU | BPF_MOD | BPF_X) ||
		    insn->code == (BPF_ALU | BPF_DIV | BPF_X)) {
			bool is64 = BPF_CLASS(insn->code) == BPF_ALU64;
			bool isdiv = BPF_OP(insn->code) == BPF_DIV;
			bool is_sdiv = isdiv && insn->off == 1;
			bool is_smod = !isdiv && insn->off == 1;
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
			struct bpf_insn chk_and_sdiv[] = {
				/* [R,W]x sdiv 0 -> 0
				 * LLONG_MIN sdiv -1 -> LLONG_MIN
				 * INT_MIN sdiv -1 -> INT_MIN
				 */
				BPF_MOV64_REG(BPF_REG_AX, insn->src_reg),
				BPF_RAW_INSN((is64 ? BPF_ALU64 : BPF_ALU) |
					     BPF_ADD | BPF_K, BPF_REG_AX,
					     0, 0, 1),
				BPF_RAW_INSN((is64 ? BPF_JMP : BPF_JMP32) |
					     BPF_JGT | BPF_K, BPF_REG_AX,
					     0, 4, 1),
				BPF_RAW_INSN((is64 ? BPF_JMP : BPF_JMP32) |
					     BPF_JEQ | BPF_K, BPF_REG_AX,
					     0, 1, 0),
				BPF_RAW_INSN((is64 ? BPF_ALU64 : BPF_ALU) |
					     BPF_MOV | BPF_K, insn->dst_reg,
					     0, 0, 0),
				/* BPF_NEG(LLONG_MIN) == -LLONG_MIN == LLONG_MIN */
				BPF_RAW_INSN((is64 ? BPF_ALU64 : BPF_ALU) |
					     BPF_NEG | BPF_K, insn->dst_reg,
					     0, 0, 0),
				BPF_JMP_IMM(BPF_JA, 0, 0, 1),
				*insn,
			};
			struct bpf_insn chk_and_smod[] = {
				/* [R,W]x mod 0 -> [R,W]x */
				/* [R,W]x mod -1 -> 0 */
				BPF_MOV64_REG(BPF_REG_AX, insn->src_reg),
				BPF_RAW_INSN((is64 ? BPF_ALU64 : BPF_ALU) |
					     BPF_ADD | BPF_K, BPF_REG_AX,
					     0, 0, 1),
				BPF_RAW_INSN((is64 ? BPF_JMP : BPF_JMP32) |
					     BPF_JGT | BPF_K, BPF_REG_AX,
					     0, 3, 1),
				BPF_RAW_INSN((is64 ? BPF_JMP : BPF_JMP32) |
					     BPF_JEQ | BPF_K, BPF_REG_AX,
					     0, 3 + (is64 ? 0 : 1), 1),
				BPF_MOV32_IMM(insn->dst_reg, 0),
				BPF_JMP_IMM(BPF_JA, 0, 0, 1),
				*insn,
				BPF_JMP_IMM(BPF_JA, 0, 0, 1),
				BPF_MOV32_REG(insn->dst_reg, insn->dst_reg),
			};

			if (is_sdiv) {
				patchlet = chk_and_sdiv;
				cnt = ARRAY_SIZE(chk_and_sdiv);
			} else if (is_smod) {
				patchlet = chk_and_smod;
				cnt = ARRAY_SIZE(chk_and_smod) - (is64 ? 2 : 0);
			} else {
				patchlet = isdiv ? chk_and_div : chk_and_mod;
				cnt = isdiv ? ARRAY_SIZE(chk_and_div) :
					      ARRAY_SIZE(chk_and_mod) - (is64 ? 2 : 0);
			}

			new_prog = bpf_patch_insn_data(env, i + delta, patchlet, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		/* Make it impossible to de-reference a userspace address */
		if (BPF_CLASS(insn->code) == BPF_LDX &&
		    (BPF_MODE(insn->code) == BPF_PROBE_MEM ||
		     BPF_MODE(insn->code) == BPF_PROBE_MEMSX)) {
			struct bpf_insn *patch = &insn_buf[0];
			u64 uaddress_limit = bpf_arch_uaddress_limit();

			if (!uaddress_limit)
				goto next_insn;

			*patch++ = BPF_MOV64_REG(BPF_REG_AX, insn->src_reg);
			if (insn->off)
				*patch++ = BPF_ALU64_IMM(BPF_ADD, BPF_REG_AX, insn->off);
			*patch++ = BPF_ALU64_IMM(BPF_RSH, BPF_REG_AX, 32);
			*patch++ = BPF_JMP_IMM(BPF_JLE, BPF_REG_AX, uaddress_limit >> 32, 2);
			*patch++ = *insn;
			*patch++ = BPF_JMP_IMM(BPF_JA, 0, 0, 1);
			*patch++ = BPF_MOV64_IMM(insn->dst_reg, 0);

			cnt = patch - insn_buf;
			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		/* Implement LD_ABS and LD_IND with a rewrite, if supported by the program type. */
		if (BPF_CLASS(insn->code) == BPF_LD &&
		    (BPF_MODE(insn->code) == BPF_ABS ||
		     BPF_MODE(insn->code) == BPF_IND)) {
			cnt = env->ops->gen_ld_abs(insn, insn_buf);
			if (cnt == 0 || cnt >= INSN_BUF_SIZE) {
				verbose(env, "bpf verifier is misconfigured\n");
				return -EINVAL;
			}

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		/* Rewrite pointer arithmetic to mitigate speculation attacks. */
		if (insn->code == (BPF_ALU64 | BPF_ADD | BPF_X) ||
		    insn->code == (BPF_ALU64 | BPF_SUB | BPF_X)) {
			const u8 code_add = BPF_ALU64 | BPF_ADD | BPF_X;
			const u8 code_sub = BPF_ALU64 | BPF_SUB | BPF_X;
			struct bpf_insn *patch = &insn_buf[0];
			bool issrc, isneg, isimm;
			u32 off_reg;

			aux = &env->insn_aux_data[i + delta];
			if (!aux->alu_state ||
			    aux->alu_state == BPF_ALU_NON_POINTER)
				goto next_insn;

			isneg = aux->alu_state & BPF_ALU_NEG_VALUE;
			issrc = (aux->alu_state & BPF_ALU_SANITIZE) ==
				BPF_ALU_SANITIZE_SRC;
			isimm = aux->alu_state & BPF_ALU_IMMEDIATE;

			off_reg = issrc ? insn->src_reg : insn->dst_reg;
			if (isimm) {
				*patch++ = BPF_MOV32_IMM(BPF_REG_AX, aux->alu_limit);
			} else {
				if (isneg)
					*patch++ = BPF_ALU64_IMM(BPF_MUL, off_reg, -1);
				*patch++ = BPF_MOV32_IMM(BPF_REG_AX, aux->alu_limit);
				*patch++ = BPF_ALU64_REG(BPF_SUB, BPF_REG_AX, off_reg);
				*patch++ = BPF_ALU64_REG(BPF_OR, BPF_REG_AX, off_reg);
				*patch++ = BPF_ALU64_IMM(BPF_NEG, BPF_REG_AX, 0);
				*patch++ = BPF_ALU64_IMM(BPF_ARSH, BPF_REG_AX, 63);
				*patch++ = BPF_ALU64_REG(BPF_AND, BPF_REG_AX, off_reg);
			}
			if (!issrc)
				*patch++ = BPF_MOV64_REG(insn->dst_reg, insn->src_reg);
			insn->src_reg = BPF_REG_AX;
			if (isneg)
				insn->code = insn->code == code_add ?
					     code_sub : code_add;
			*patch++ = *insn;
			if (issrc && isneg && !isimm)
				*patch++ = BPF_ALU64_IMM(BPF_MUL, off_reg, -1);
			cnt = patch - insn_buf;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		if (is_may_goto_insn(insn)) {
			int stack_off = -stack_depth - 8;

			stack_depth_extra = 8;
			insn_buf[0] = BPF_LDX_MEM(BPF_DW, BPF_REG_AX, BPF_REG_10, stack_off);
			if (insn->off >= 0)
				insn_buf[1] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_AX, 0, insn->off + 2);
			else
				insn_buf[1] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_AX, 0, insn->off - 1);
			insn_buf[2] = BPF_ALU64_IMM(BPF_SUB, BPF_REG_AX, 1);
			insn_buf[3] = BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_AX, stack_off);
			cnt = 4;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta += cnt - 1;
			env->prog = prog = new_prog;
			insn = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		if (insn->code != (BPF_JMP | BPF_CALL))
			goto next_insn;
		if (insn->src_reg == BPF_PSEUDO_CALL)
			goto next_insn;
		if (insn->src_reg == BPF_PSEUDO_KFUNC_CALL) {
			ret = fixup_kfunc_call(env, insn, insn_buf, i + delta, &cnt);
			if (ret)
				return ret;
			if (cnt == 0)
				goto next_insn;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta	 += cnt - 1;
			env->prog = prog = new_prog;
			insn	  = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		/* Skip inlining the helper call if the JIT does it. */
		if (bpf_jit_inlines_helper_call(insn->imm))
			goto next_insn;

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
			 * conditional branch in the interpreter for every normal
			 * call and to prevent accidental JITing by JIT compiler
			 * that doesn't support bpf_tail_call yet
			 */
			insn->imm = 0;
			insn->code = BPF_JMP | BPF_TAIL_CALL;

			aux = &env->insn_aux_data[i + delta];
			if (env->bpf_capable && !prog->blinding_requested &&
			    prog->jit_requested &&
			    !bpf_map_key_poisoned(aux) &&
			    !bpf_map_ptr_poisoned(aux) &&
			    !bpf_map_ptr_unpriv(aux)) {
				struct bpf_jit_poke_descriptor desc = {
					.reason = BPF_POKE_REASON_TAIL_CALL,
					.tail_call.map = aux->map_ptr_state.map_ptr,
					.tail_call.key = bpf_map_key_immediate(aux),
					.insn_idx = i + delta,
				};

				ret = bpf_jit_add_poke_descriptor(prog, &desc);
				if (ret < 0) {
					verbose(env, "adding tail call poke descriptor failed\n");
					return ret;
				}

				insn->imm = ret + 1;
				goto next_insn;
			}

			if (!bpf_map_ptr_unpriv(aux))
				goto next_insn;

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

			map_ptr = aux->map_ptr_state.map_ptr;
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
			goto next_insn;
		}

		if (insn->imm == BPF_FUNC_timer_set_callback) {
			/* The verifier will process callback_fn as many times as necessary
			 * with different maps and the register states prepared by
			 * set_timer_callback_state will be accurate.
			 *
			 * The following use case is valid:
			 *   map1 is shared by prog1, prog2, prog3.
			 *   prog1 calls bpf_timer_init for some map1 elements
			 *   prog2 calls bpf_timer_set_callback for some map1 elements.
			 *     Those that were not bpf_timer_init-ed will return -EINVAL.
			 *   prog3 calls bpf_timer_start for some map1 elements.
			 *     Those that were not both bpf_timer_init-ed and
			 *     bpf_timer_set_callback-ed will return -EINVAL.
			 */
			struct bpf_insn ld_addrs[2] = {
				BPF_LD_IMM64(BPF_REG_3, (long)prog->aux),
			};

			insn_buf[0] = ld_addrs[0];
			insn_buf[1] = ld_addrs[1];
			insn_buf[2] = *insn;
			cnt = 3;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto patch_call_imm;
		}

		if (is_storage_get_function(insn->imm)) {
			if (!in_sleepable(env) ||
			    env->insn_aux_data[i + delta].storage_get_func_atomic)
				insn_buf[0] = BPF_MOV64_IMM(BPF_REG_5, (__force __s32)GFP_ATOMIC);
			else
				insn_buf[0] = BPF_MOV64_IMM(BPF_REG_5, (__force __s32)GFP_KERNEL);
			insn_buf[1] = *insn;
			cnt = 2;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta += cnt - 1;
			env->prog = prog = new_prog;
			insn = new_prog->insnsi + i + delta;
			goto patch_call_imm;
		}

		/* bpf_per_cpu_ptr() and bpf_this_cpu_ptr() */
		if (env->insn_aux_data[i + delta].call_with_percpu_alloc_ptr) {
			/* patch with 'r1 = *(u64 *)(r1 + 0)' since for percpu data,
			 * bpf_mem_alloc() returns a ptr to the percpu data ptr.
			 */
			insn_buf[0] = BPF_LDX_MEM(BPF_DW, BPF_REG_1, BPF_REG_1, 0);
			insn_buf[1] = *insn;
			cnt = 2;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta += cnt - 1;
			env->prog = prog = new_prog;
			insn = new_prog->insnsi + i + delta;
			goto patch_call_imm;
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
		     insn->imm == BPF_FUNC_map_peek_elem   ||
		     insn->imm == BPF_FUNC_redirect_map    ||
		     insn->imm == BPF_FUNC_for_each_map_elem ||
		     insn->imm == BPF_FUNC_map_lookup_percpu_elem)) {
			aux = &env->insn_aux_data[i + delta];
			if (bpf_map_ptr_poisoned(aux))
				goto patch_call_imm;

			map_ptr = aux->map_ptr_state.map_ptr;
			ops = map_ptr->ops;
			if (insn->imm == BPF_FUNC_map_lookup_elem &&
			    ops->map_gen_lookup) {
				cnt = ops->map_gen_lookup(map_ptr, insn_buf);
				if (cnt == -EOPNOTSUPP)
					goto patch_map_ops_generic;
				if (cnt <= 0 || cnt >= INSN_BUF_SIZE) {
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
				goto next_insn;
			}

			BUILD_BUG_ON(!__same_type(ops->map_lookup_elem,
				     (void *(*)(struct bpf_map *map, void *key))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_delete_elem,
				     (long (*)(struct bpf_map *map, void *key))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_update_elem,
				     (long (*)(struct bpf_map *map, void *key, void *value,
					      u64 flags))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_push_elem,
				     (long (*)(struct bpf_map *map, void *value,
					      u64 flags))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_pop_elem,
				     (long (*)(struct bpf_map *map, void *value))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_peek_elem,
				     (long (*)(struct bpf_map *map, void *value))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_redirect,
				     (long (*)(struct bpf_map *map, u64 index, u64 flags))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_for_each_callback,
				     (long (*)(struct bpf_map *map,
					      bpf_callback_t callback_fn,
					      void *callback_ctx,
					      u64 flags))NULL));
			BUILD_BUG_ON(!__same_type(ops->map_lookup_percpu_elem,
				     (void *(*)(struct bpf_map *map, void *key, u32 cpu))NULL));

patch_map_ops_generic:
			switch (insn->imm) {
			case BPF_FUNC_map_lookup_elem:
				insn->imm = BPF_CALL_IMM(ops->map_lookup_elem);
				goto next_insn;
			case BPF_FUNC_map_update_elem:
				insn->imm = BPF_CALL_IMM(ops->map_update_elem);
				goto next_insn;
			case BPF_FUNC_map_delete_elem:
				insn->imm = BPF_CALL_IMM(ops->map_delete_elem);
				goto next_insn;
			case BPF_FUNC_map_push_elem:
				insn->imm = BPF_CALL_IMM(ops->map_push_elem);
				goto next_insn;
			case BPF_FUNC_map_pop_elem:
				insn->imm = BPF_CALL_IMM(ops->map_pop_elem);
				goto next_insn;
			case BPF_FUNC_map_peek_elem:
				insn->imm = BPF_CALL_IMM(ops->map_peek_elem);
				goto next_insn;
			case BPF_FUNC_redirect_map:
				insn->imm = BPF_CALL_IMM(ops->map_redirect);
				goto next_insn;
			case BPF_FUNC_for_each_map_elem:
				insn->imm = BPF_CALL_IMM(ops->map_for_each_callback);
				goto next_insn;
			case BPF_FUNC_map_lookup_percpu_elem:
				insn->imm = BPF_CALL_IMM(ops->map_lookup_percpu_elem);
				goto next_insn;
			}

			goto patch_call_imm;
		}

		/* Implement bpf_jiffies64 inline. */
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
			goto next_insn;
		}

#if defined(CONFIG_X86_64) && !defined(CONFIG_UML)
		/* Implement bpf_get_smp_processor_id() inline. */
		if (insn->imm == BPF_FUNC_get_smp_processor_id &&
		    verifier_inlines_helper_call(env, insn->imm)) {
			/* BPF_FUNC_get_smp_processor_id inlining is an
			 * optimization, so if pcpu_hot.cpu_number is ever
			 * changed in some incompatible and hard to support
			 * way, it's fine to back out this inlining logic
			 */
			insn_buf[0] = BPF_MOV32_IMM(BPF_REG_0, (u32)(unsigned long)&pcpu_hot.cpu_number);
			insn_buf[1] = BPF_MOV64_PERCPU_REG(BPF_REG_0, BPF_REG_0);
			insn_buf[2] = BPF_LDX_MEM(BPF_W, BPF_REG_0, BPF_REG_0, 0);
			cnt = 3;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
		}
#endif
		/* Implement bpf_get_func_arg inline. */
		if (prog_type == BPF_PROG_TYPE_TRACING &&
		    insn->imm == BPF_FUNC_get_func_arg) {
			/* Load nr_args from ctx - 8 */
			insn_buf[0] = BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8);
			insn_buf[1] = BPF_JMP32_REG(BPF_JGE, BPF_REG_2, BPF_REG_0, 6);
			insn_buf[2] = BPF_ALU64_IMM(BPF_LSH, BPF_REG_2, 3);
			insn_buf[3] = BPF_ALU64_REG(BPF_ADD, BPF_REG_2, BPF_REG_1);
			insn_buf[4] = BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_2, 0);
			insn_buf[5] = BPF_STX_MEM(BPF_DW, BPF_REG_3, BPF_REG_0, 0);
			insn_buf[6] = BPF_MOV64_IMM(BPF_REG_0, 0);
			insn_buf[7] = BPF_JMP_A(1);
			insn_buf[8] = BPF_MOV64_IMM(BPF_REG_0, -EINVAL);
			cnt = 9;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		/* Implement bpf_get_func_ret inline. */
		if (prog_type == BPF_PROG_TYPE_TRACING &&
		    insn->imm == BPF_FUNC_get_func_ret) {
			if (eatype == BPF_TRACE_FEXIT ||
			    eatype == BPF_MODIFY_RETURN) {
				/* Load nr_args from ctx - 8 */
				insn_buf[0] = BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8);
				insn_buf[1] = BPF_ALU64_IMM(BPF_LSH, BPF_REG_0, 3);
				insn_buf[2] = BPF_ALU64_REG(BPF_ADD, BPF_REG_0, BPF_REG_1);
				insn_buf[3] = BPF_LDX_MEM(BPF_DW, BPF_REG_3, BPF_REG_0, 0);
				insn_buf[4] = BPF_STX_MEM(BPF_DW, BPF_REG_2, BPF_REG_3, 0);
				insn_buf[5] = BPF_MOV64_IMM(BPF_REG_0, 0);
				cnt = 6;
			} else {
				insn_buf[0] = BPF_MOV64_IMM(BPF_REG_0, -EOPNOTSUPP);
				cnt = 1;
			}

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		/* Implement get_func_arg_cnt inline. */
		if (prog_type == BPF_PROG_TYPE_TRACING &&
		    insn->imm == BPF_FUNC_get_func_arg_cnt) {
			/* Load nr_args from ctx - 8 */
			insn_buf[0] = BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -8);

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, 1);
			if (!new_prog)
				return -ENOMEM;

			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		/* Implement bpf_get_func_ip inline. */
		if (prog_type == BPF_PROG_TYPE_TRACING &&
		    insn->imm == BPF_FUNC_get_func_ip) {
			/* Load IP address from ctx - 16 */
			insn_buf[0] = BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_1, -16);

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, 1);
			if (!new_prog)
				return -ENOMEM;

			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		/* Implement bpf_get_branch_snapshot inline. */
		if (IS_ENABLED(CONFIG_PERF_EVENTS) &&
		    prog->jit_requested && BITS_PER_LONG == 64 &&
		    insn->imm == BPF_FUNC_get_branch_snapshot) {
			/* We are dealing with the following func protos:
			 * u64 bpf_get_branch_snapshot(void *buf, u32 size, u64 flags);
			 * int perf_snapshot_branch_stack(struct perf_branch_entry *entries, u32 cnt);
			 */
			const u32 br_entry_size = sizeof(struct perf_branch_entry);

			/* struct perf_branch_entry is part of UAPI and is
			 * used as an array element, so extremely unlikely to
			 * ever grow or shrink
			 */
			BUILD_BUG_ON(br_entry_size != 24);

			/* if (unlikely(flags)) return -EINVAL */
			insn_buf[0] = BPF_JMP_IMM(BPF_JNE, BPF_REG_3, 0, 7);

			/* Transform size (bytes) into number of entries (cnt = size / 24).
			 * But to avoid expensive division instruction, we implement
			 * divide-by-3 through multiplication, followed by further
			 * division by 8 through 3-bit right shift.
			 * Refer to book "Hacker's Delight, 2nd ed." by Henry S. Warren, Jr.,
			 * p. 227, chapter "Unsigned Division by 3" for details and proofs.
			 *
			 * N / 3 <=> M * N / 2^33, where M = (2^33 + 1) / 3 = 0xaaaaaaab.
			 */
			insn_buf[1] = BPF_MOV32_IMM(BPF_REG_0, 0xaaaaaaab);
			insn_buf[2] = BPF_ALU64_REG(BPF_MUL, BPF_REG_2, BPF_REG_0);
			insn_buf[3] = BPF_ALU64_IMM(BPF_RSH, BPF_REG_2, 36);

			/* call perf_snapshot_branch_stack implementation */
			insn_buf[4] = BPF_EMIT_CALL(static_call_query(perf_snapshot_branch_stack));
			/* if (entry_cnt == 0) return -ENOENT */
			insn_buf[5] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 4);
			/* return entry_cnt * sizeof(struct perf_branch_entry) */
			insn_buf[6] = BPF_ALU32_IMM(BPF_MUL, BPF_REG_0, br_entry_size);
			insn_buf[7] = BPF_JMP_A(3);
			/* return -EINVAL; */
			insn_buf[8] = BPF_MOV64_IMM(BPF_REG_0, -EINVAL);
			insn_buf[9] = BPF_JMP_A(1);
			/* return -ENOENT; */
			insn_buf[10] = BPF_MOV64_IMM(BPF_REG_0, -ENOENT);
			cnt = 11;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
		}

		/* Implement bpf_kptr_xchg inline */
		if (prog->jit_requested && BITS_PER_LONG == 64 &&
		    insn->imm == BPF_FUNC_kptr_xchg &&
		    bpf_jit_supports_ptr_xchg()) {
			insn_buf[0] = BPF_MOV64_REG(BPF_REG_0, BPF_REG_2);
			insn_buf[1] = BPF_ATOMIC_OP(BPF_DW, BPF_XCHG, BPF_REG_1, BPF_REG_0, 0);
			cnt = 2;

			new_prog = bpf_patch_insn_data(env, i + delta, insn_buf, cnt);
			if (!new_prog)
				return -ENOMEM;

			delta    += cnt - 1;
			env->prog = prog = new_prog;
			insn      = new_prog->insnsi + i + delta;
			goto next_insn;
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
next_insn:
		if (subprogs[cur_subprog + 1].start == i + delta + 1) {
			subprogs[cur_subprog].stack_depth += stack_depth_extra;
			subprogs[cur_subprog].stack_extra = stack_depth_extra;
			cur_subprog++;
			stack_depth = subprogs[cur_subprog].stack_depth;
			stack_depth_extra = 0;
		}
		i++;
		insn++;
	}

	env->prog->aux->stack_depth = subprogs[0].stack_depth;
	for (i = 0; i < env->subprog_cnt; i++) {
		int subprog_start = subprogs[i].start;
		int stack_slots = subprogs[i].stack_extra / 8;

		if (!stack_slots)
			continue;
		if (stack_slots > 1) {
			verbose(env, "verifier bug: stack_slots supports may_goto only\n");
			return -EFAULT;
		}

		/* Add ST insn to subprog prologue to init extra stack */
		insn_buf[0] = BPF_ST_MEM(BPF_DW, BPF_REG_FP,
					 -subprogs[i].stack_depth, BPF_MAX_LOOPS);
		/* Copy first actual insn to preserve it */
		insn_buf[1] = env->prog->insnsi[subprog_start];

		new_prog = bpf_patch_insn_data(env, subprog_start, insn_buf, 2);
		if (!new_prog)
			return -ENOMEM;
		env->prog = prog = new_prog;
		/*
		 * If may_goto is a first insn of a prog there could be a jmp
		 * insn that points to it, hence adjust all such jmps to point
		 * to insn after BPF_ST that inits may_goto count.
		 * Adjustment will succeed because bpf_patch_insn_data() didn't fail.
		 */
		WARN_ON(adjust_jmp_off(env->prog, subprog_start, 1));
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

	sort_kfunc_descs_by_imm_off(env->prog);

	return 0;
}

static struct bpf_prog *inline_bpf_loop(struct bpf_verifier_env *env,
					int position,
					s32 stack_base,
					u32 callback_subprogno,
					u32 *total_cnt)
{
	s32 r6_offset = stack_base + 0 * BPF_REG_SIZE;
	s32 r7_offset = stack_base + 1 * BPF_REG_SIZE;
	s32 r8_offset = stack_base + 2 * BPF_REG_SIZE;
	int reg_loop_max = BPF_REG_6;
	int reg_loop_cnt = BPF_REG_7;
	int reg_loop_ctx = BPF_REG_8;

	struct bpf_insn *insn_buf = env->insn_buf;
	struct bpf_prog *new_prog;
	u32 callback_start;
	u32 call_insn_offset;
	s32 callback_offset;
	u32 cnt = 0;

	/* This represents an inlined version of bpf_iter.c:bpf_loop,
	 * be careful to modify this code in sync.
	 */

	/* Return error and jump to the end of the patch if
	 * expected number of iterations is too big.
	 */
	insn_buf[cnt++] = BPF_JMP_IMM(BPF_JLE, BPF_REG_1, BPF_MAX_LOOPS, 2);
	insn_buf[cnt++] = BPF_MOV32_IMM(BPF_REG_0, -E2BIG);
	insn_buf[cnt++] = BPF_JMP_IMM(BPF_JA, 0, 0, 16);
	/* spill R6, R7, R8 to use these as loop vars */
	insn_buf[cnt++] = BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_6, r6_offset);
	insn_buf[cnt++] = BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_7, r7_offset);
	insn_buf[cnt++] = BPF_STX_MEM(BPF_DW, BPF_REG_10, BPF_REG_8, r8_offset);
	/* initialize loop vars */
	insn_buf[cnt++] = BPF_MOV64_REG(reg_loop_max, BPF_REG_1);
	insn_buf[cnt++] = BPF_MOV32_IMM(reg_loop_cnt, 0);
	insn_buf[cnt++] = BPF_MOV64_REG(reg_loop_ctx, BPF_REG_3);
	/* loop header,
	 * if reg_loop_cnt >= reg_loop_max skip the loop body
	 */
	insn_buf[cnt++] = BPF_JMP_REG(BPF_JGE, reg_loop_cnt, reg_loop_max, 5);
	/* callback call,
	 * correct callback offset would be set after patching
	 */
	insn_buf[cnt++] = BPF_MOV64_REG(BPF_REG_1, reg_loop_cnt);
	insn_buf[cnt++] = BPF_MOV64_REG(BPF_REG_2, reg_loop_ctx);
	insn_buf[cnt++] = BPF_CALL_REL(0);
	/* increment loop counter */
	insn_buf[cnt++] = BPF_ALU64_IMM(BPF_ADD, reg_loop_cnt, 1);
	/* jump to loop header if callback returned 0 */
	insn_buf[cnt++] = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, -6);
	/* return value of bpf_loop,
	 * set R0 to the number of iterations
	 */
	insn_buf[cnt++] = BPF_MOV64_REG(BPF_REG_0, reg_loop_cnt);
	/* restore original values of R6, R7, R8 */
	insn_buf[cnt++] = BPF_LDX_MEM(BPF_DW, BPF_REG_6, BPF_REG_10, r6_offset);
	insn_buf[cnt++] = BPF_LDX_MEM(BPF_DW, BPF_REG_7, BPF_REG_10, r7_offset);
	insn_buf[cnt++] = BPF_LDX_MEM(BPF_DW, BPF_REG_8, BPF_REG_10, r8_offset);

	*total_cnt = cnt;
	new_prog = bpf_patch_insn_data(env, position, insn_buf, cnt);
	if (!new_prog)
		return new_prog;

	/* callback start is known only after patching */
	callback_start = env->subprog_info[callback_subprogno].start;
	/* Note: insn_buf[12] is an offset of BPF_CALL_REL instruction */
	call_insn_offset = position + 12;
	callback_offset = callback_start - call_insn_offset - 1;
	new_prog->insnsi[call_insn_offset].imm = callback_offset;

	return new_prog;
}

static bool is_bpf_loop_call(struct bpf_insn *insn)
{
	return insn->code == (BPF_JMP | BPF_CALL) &&
		insn->src_reg == 0 &&
		insn->imm == BPF_FUNC_loop;
}

/* For all sub-programs in the program (including main) check
 * insn_aux_data to see if there are bpf_loop calls that require
 * inlining. If such calls are found the calls are replaced with a
 * sequence of instructions produced by `inline_bpf_loop` function and
 * subprog stack_depth is increased by the size of 3 registers.
 * This stack space is used to spill values of the R6, R7, R8.  These
 * registers are used to store the loop bound, counter and context
 * variables.
 */
static int optimize_bpf_loop(struct bpf_verifier_env *env)
{
	struct bpf_subprog_info *subprogs = env->subprog_info;
	int i, cur_subprog = 0, cnt, delta = 0;
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	u16 stack_depth = subprogs[cur_subprog].stack_depth;
	u16 stack_depth_roundup = round_up(stack_depth, 8) - stack_depth;
	u16 stack_depth_extra = 0;

	for (i = 0; i < insn_cnt; i++, insn++) {
		struct bpf_loop_inline_state *inline_state =
			&env->insn_aux_data[i + delta].loop_inline_state;

		if (is_bpf_loop_call(insn) && inline_state->fit_for_inline) {
			struct bpf_prog *new_prog;

			stack_depth_extra = BPF_REG_SIZE * 3 + stack_depth_roundup;
			new_prog = inline_bpf_loop(env,
						   i + delta,
						   -(stack_depth + stack_depth_extra),
						   inline_state->callback_subprogno,
						   &cnt);
			if (!new_prog)
				return -ENOMEM;

			delta     += cnt - 1;
			env->prog  = new_prog;
			insn       = new_prog->insnsi + i + delta;
		}

		if (subprogs[cur_subprog + 1].start == i + delta + 1) {
			subprogs[cur_subprog].stack_depth += stack_depth_extra;
			cur_subprog++;
			stack_depth = subprogs[cur_subprog].stack_depth;
			stack_depth_roundup = round_up(stack_depth, 8) - stack_depth;
			stack_depth_extra = 0;
		}
	}

	env->prog->aux->stack_depth = env->subprog_info[0].stack_depth;

	return 0;
}

/* Remove unnecessary spill/fill pairs, members of fastcall pattern,
 * adjust subprograms stack depth when possible.
 */
static int remove_fastcall_spills_fills(struct bpf_verifier_env *env)
{
	struct bpf_subprog_info *subprog = env->subprog_info;
	struct bpf_insn_aux_data *aux = env->insn_aux_data;
	struct bpf_insn *insn = env->prog->insnsi;
	int insn_cnt = env->prog->len;
	u32 spills_num;
	bool modified = false;
	int i, j;

	for (i = 0; i < insn_cnt; i++, insn++) {
		if (aux[i].fastcall_spills_num > 0) {
			spills_num = aux[i].fastcall_spills_num;
			/* NOPs would be removed by opt_remove_nops() */
			for (j = 1; j <= spills_num; ++j) {
				*(insn - j) = NOP;
				*(insn + j) = NOP;
			}
			modified = true;
		}
		if ((subprog + 1)->start == i + 1) {
			if (modified && !subprog->keep_fastcall_stack)
				subprog->stack_depth = -subprog->fastcall_stack_off;
			subprog++;
			modified = false;
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

static int do_check_common(struct bpf_verifier_env *env, int subprog)
{
	bool pop_log = !(env->log.level & BPF_LOG_LEVEL2);
	struct bpf_subprog_info *sub = subprog_info(env, subprog);
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
	state->first_insn_idx = env->subprog_info[subprog].start;
	state->last_insn_idx = -1;

	regs = state->frame[state->curframe]->regs;
	if (subprog || env->prog->type == BPF_PROG_TYPE_EXT) {
		const char *sub_name = subprog_name(env, subprog);
		struct bpf_subprog_arg_info *arg;
		struct bpf_reg_state *reg;

		verbose(env, "Validating %s() func#%d...\n", sub_name, subprog);
		ret = btf_prepare_func_args(env, subprog);
		if (ret)
			goto out;

		if (subprog_is_exc_cb(env, subprog)) {
			state->frame[0]->in_exception_callback_fn = true;
			/* We have already ensured that the callback returns an integer, just
			 * like all global subprogs. We need to determine it only has a single
			 * scalar argument.
			 */
			if (sub->arg_cnt != 1 || sub->args[0].arg_type != ARG_ANYTHING) {
				verbose(env, "exception cb only supports single integer argument\n");
				ret = -EINVAL;
				goto out;
			}
		}
		for (i = BPF_REG_1; i <= sub->arg_cnt; i++) {
			arg = &sub->args[i - BPF_REG_1];
			reg = &regs[i];

			if (arg->arg_type == ARG_PTR_TO_CTX) {
				reg->type = PTR_TO_CTX;
				mark_reg_known_zero(env, regs, i);
			} else if (arg->arg_type == ARG_ANYTHING) {
				reg->type = SCALAR_VALUE;
				mark_reg_unknown(env, regs, i);
			} else if (arg->arg_type == (ARG_PTR_TO_DYNPTR | MEM_RDONLY)) {
				/* assume unspecial LOCAL dynptr type */
				__mark_dynptr_reg(reg, BPF_DYNPTR_TYPE_LOCAL, true, ++env->id_gen);
			} else if (base_type(arg->arg_type) == ARG_PTR_TO_MEM) {
				reg->type = PTR_TO_MEM;
				if (arg->arg_type & PTR_MAYBE_NULL)
					reg->type |= PTR_MAYBE_NULL;
				mark_reg_known_zero(env, regs, i);
				reg->mem_size = arg->mem_size;
				reg->id = ++env->id_gen;
			} else if (base_type(arg->arg_type) == ARG_PTR_TO_BTF_ID) {
				reg->type = PTR_TO_BTF_ID;
				if (arg->arg_type & PTR_MAYBE_NULL)
					reg->type |= PTR_MAYBE_NULL;
				if (arg->arg_type & PTR_UNTRUSTED)
					reg->type |= PTR_UNTRUSTED;
				if (arg->arg_type & PTR_TRUSTED)
					reg->type |= PTR_TRUSTED;
				mark_reg_known_zero(env, regs, i);
				reg->btf = bpf_get_btf_vmlinux(); /* can't fail at this point */
				reg->btf_id = arg->btf_id;
				reg->id = ++env->id_gen;
			} else if (base_type(arg->arg_type) == ARG_PTR_TO_ARENA) {
				/* caller can pass either PTR_TO_ARENA or SCALAR */
				mark_reg_unknown(env, regs, i);
			} else {
				WARN_ONCE(1, "BUG: unhandled arg#%d type %d\n",
					  i - BPF_REG_1, arg->arg_type);
				ret = -EFAULT;
				goto out;
			}
		}
	} else {
		/* if main BPF program has associated BTF info, validate that
		 * it's matching expected signature, and otherwise mark BTF
		 * info for main program as unreliable
		 */
		if (env->prog->aux->func_info_aux) {
			ret = btf_prepare_func_args(env, 0);
			if (ret || sub->arg_cnt != 1 || sub->args[0].arg_type != ARG_PTR_TO_CTX)
				env->prog->aux->func_info_aux[0].unreliable = true;
		}

		/* 1st arg to a function */
		regs[BPF_REG_1].type = PTR_TO_CTX;
		mark_reg_known_zero(env, regs, BPF_REG_1);
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
	return ret;
}

/* Lazily verify all global functions based on their BTF, if they are called
 * from main BPF program or any of subprograms transitively.
 * BPF global subprogs called from dead code are not validated.
 * All callable global functions must pass verification.
 * Otherwise the whole program is rejected.
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
	struct bpf_func_info_aux *sub_aux;
	int i, ret, new_cnt;

	if (!aux->func_info)
		return 0;

	/* exception callback is presumed to be always called */
	if (env->exception_callback_subprog)
		subprog_aux(env, env->exception_callback_subprog)->called = true;

again:
	new_cnt = 0;
	for (i = 1; i < env->subprog_cnt; i++) {
		if (!subprog_is_global(env, i))
			continue;

		sub_aux = subprog_aux(env, i);
		if (!sub_aux->called || sub_aux->verified)
			continue;

		env->insn_idx = env->subprog_info[i].start;
		WARN_ON_ONCE(env->insn_idx == 0);
		ret = do_check_common(env, i);
		if (ret) {
			return ret;
		} else if (env->log.level & BPF_LOG_LEVEL) {
			verbose(env, "Func#%d ('%s') is safe for any args that match its prototype\n",
				i, subprog_name(env, i));
		}

		/* We verified new global subprog, it might have called some
		 * more global subprogs that we haven't verified yet, so we
		 * need to do another pass over subprogs to verify those.
		 */
		sub_aux->verified = true;
		new_cnt++;
	}

	/* We can't loop forever as we verify at least one global subprog on
	 * each pass.
	 */
	if (new_cnt)
		goto again;

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
	const struct bpf_struct_ops_desc *st_ops_desc;
	const struct bpf_struct_ops *st_ops;
	const struct btf_member *member;
	struct bpf_prog *prog = env->prog;
	u32 btf_id, member_idx;
	struct btf *btf;
	const char *mname;
	int err;

	if (!prog->gpl_compatible) {
		verbose(env, "struct ops programs must have a GPL compatible license\n");
		return -EINVAL;
	}

	if (!prog->aux->attach_btf_id)
		return -ENOTSUPP;

	btf = prog->aux->attach_btf;
	if (btf_is_module(btf)) {
		/* Make sure st_ops is valid through the lifetime of env */
		env->attach_btf_mod = btf_try_get_module(btf);
		if (!env->attach_btf_mod) {
			verbose(env, "struct_ops module %s is not found\n",
				btf_get_name(btf));
			return -ENOTSUPP;
		}
	}

	btf_id = prog->aux->attach_btf_id;
	st_ops_desc = bpf_struct_ops_find(btf, btf_id);
	if (!st_ops_desc) {
		verbose(env, "attach_btf_id %u is not a supported struct\n",
			btf_id);
		return -ENOTSUPP;
	}
	st_ops = st_ops_desc->st_ops;

	t = st_ops_desc->type;
	member_idx = prog->expected_attach_type;
	if (member_idx >= btf_type_vlen(t)) {
		verbose(env, "attach to invalid member idx %u of struct %s\n",
			member_idx, st_ops->name);
		return -EINVAL;
	}

	member = &btf_type_member(t)[member_idx];
	mname = btf_name_by_offset(btf, member->name_off);
	func_proto = btf_type_resolve_func_ptr(btf, member->type,
					       NULL);
	if (!func_proto) {
		verbose(env, "attach to invalid member %s(@idx %u) of struct %s\n",
			mname, member_idx, st_ops->name);
		return -EINVAL;
	}

	err = bpf_struct_ops_supported(st_ops, __btf_member_bit_offset(t, member) / 8);
	if (err) {
		verbose(env, "attach to unsupported member %s of struct %s\n",
			mname, st_ops->name);
		return err;
	}

	if (st_ops->check_member) {
		err = st_ops->check_member(t, member, prog);

		if (err) {
			verbose(env, "attach to unsupported member %s of struct %s\n",
				mname, st_ops->name);
			return err;
		}
	}

	if (prog->aux->priv_stack_requested && !bpf_jit_supports_private_stack()) {
		verbose(env, "Private stack not supported by jit\n");
		return -EACCES;
	}

	/* btf_ctx_access() used this to provide argument type info */
	prog->aux->ctx_arg_info =
		st_ops_desc->arg_info[member_idx].info;
	prog->aux->ctx_arg_info_size =
		st_ops_desc->arg_info[member_idx].cnt;

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
BTF_ID(func, __filemap_add_folio)
#ifdef CONFIG_FAIL_PAGE_ALLOC
BTF_ID(func, should_fail_alloc_page)
#endif
#ifdef CONFIG_FAILSLAB
BTF_ID(func, should_failslab)
#endif
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
	bool prog_tracing = prog->type == BPF_PROG_TYPE_TRACING;
	char trace_symbol[KSYM_SYMBOL_LEN];
	const char prefix[] = "btf_trace_";
	struct bpf_raw_event_map *btp;
	int ret = 0, subprog = -1, i;
	const struct btf_type *t;
	bool conservative = true;
	const char *tname, *fname;
	struct btf *btf;
	long addr = 0;
	struct module *mod = NULL;

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

		if (bpf_prog_is_dev_bound(prog->aux) &&
		    !bpf_prog_dev_bound_match(prog, tgt_prog)) {
			bpf_log(log, "Target program bound device mismatch");
			return -EINVAL;
		}

		for (i = 0; i < aux->func_info_cnt; i++)
			if (aux->func_info[i].type_id == btf_id) {
				subprog = i;
				break;
			}
		if (subprog == -1) {
			bpf_log(log, "Subprog %s doesn't exist\n", tname);
			return -EINVAL;
		}
		if (aux->func && aux->func[subprog]->aux->exception_cb) {
			bpf_log(log,
				"%s programs cannot attach to exception callback\n",
				prog_extension ? "Extension" : "FENTRY/FEXIT");
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
		if (prog_tracing) {
			if (aux->attach_tracing_prog) {
				/*
				 * Target program is an fentry/fexit which is already attached
				 * to another tracing program. More levels of nesting
				 * attachment are not allowed.
				 */
				bpf_log(log, "Cannot nest tracing program attach more than once\n");
				return -EINVAL;
			}
		} else if (tgt_prog->type == prog->type) {
			/*
			 * To avoid potential call chain cycles, prevent attaching of a
			 * program extension to another extension. It's ok to attach
			 * fentry/fexit to extension program.
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
			 * type. When extension program is replacing XDP function
			 * it is necessary to allow performance analysis of all
			 * functions. Both original XDP program and its program
			 * extension. Hence attaching fentry/fexit to
			 * BPF_PROG_TYPE_EXT is allowed. If extending of
			 * fentry/fexit was allowed it would be possible to create
			 * long call chain fentry->extension->fentry->extension
			 * beyond reasonable stack size. Hence extending fentry
			 * is not allowed.
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

		/* The func_proto of "btf_trace_##tname" is generated from typedef without argument
		 * names. Thus using bpf_raw_event_map to get argument names.
		 */
		btp = bpf_get_raw_tracepoint(tname);
		if (!btp)
			return -EINVAL;
		fname = kallsyms_lookup((unsigned long)btp->bpf_func, NULL, NULL, NULL,
					trace_symbol);
		bpf_put_raw_tracepoint(btp);

		if (fname)
			ret = btf_find_by_name_kind(btf, fname, BTF_KIND_FUNC);

		if (!fname || ret < 0) {
			bpf_log(log, "Cannot find btf of tracepoint template, fall back to %s%s.\n",
				prefix, tname);
			t = btf_type_by_id(btf, t->type);
			if (!btf_type_is_ptr(t))
				/* should never happen in valid vmlinux build */
				return -EINVAL;
		} else {
			t = btf_type_by_id(btf, ret);
			if (!btf_type_is_func(t))
				/* should never happen in valid vmlinux build */
				return -EINVAL;
		}

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
	case BPF_LSM_CGROUP:
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
			if (btf_is_module(btf)) {
				mod = btf_try_get_module(btf);
				if (mod)
					addr = find_kallsyms_symbol_value(mod, tname);
				else
					addr = 0;
			} else {
				addr = kallsyms_lookup_name(tname);
			}
			if (!addr) {
				module_put(mod);
				bpf_log(log,
					"The address of function %s cannot be found\n",
					tname);
				return -ENOENT;
			}
		}

		if (prog->sleepable) {
			ret = -EINVAL;
			switch (prog->type) {
			case BPF_PROG_TYPE_TRACING:

				/* fentry/fexit/fmod_ret progs can be sleepable if they are
				 * attached to ALLOW_ERROR_INJECTION and are not in denylist.
				 */
				if (!check_non_sleepable_error_inject(btf_id) &&
				    within_error_injection_list(addr))
					ret = 0;
				/* fentry/fexit/fmod_ret progs can also be sleepable if they are
				 * in the fmodret id set with the KF_SLEEPABLE flag.
				 */
				else {
					u32 *flags = btf_kfunc_is_modify_return(btf, btf_id,
										prog);

					if (flags && (*flags & KF_SLEEPABLE))
						ret = 0;
				}
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
				module_put(mod);
				bpf_log(log, "%s is not sleepable\n", tname);
				return ret;
			}
		} else if (prog->expected_attach_type == BPF_MODIFY_RETURN) {
			if (tgt_prog) {
				module_put(mod);
				bpf_log(log, "can't modify return codes of BPF programs\n");
				return -EINVAL;
			}
			ret = -EINVAL;
			if (btf_kfunc_is_modify_return(btf, btf_id, prog) ||
			    !check_attach_modify_return(addr, tname))
				ret = 0;
			if (ret) {
				module_put(mod);
				bpf_log(log, "%s() is not modifiable\n", tname);
				return ret;
			}
		}

		break;
	}
	tgt_info->tgt_addr = addr;
	tgt_info->tgt_name = tname;
	tgt_info->tgt_type = t;
	tgt_info->tgt_mod = mod;
	return 0;
}

BTF_SET_START(btf_id_deny)
BTF_ID_UNUSED
#ifdef CONFIG_SMP
BTF_ID(func, migrate_disable)
BTF_ID(func, migrate_enable)
#endif
#if !defined CONFIG_PREEMPT_RCU && !defined CONFIG_TINY_RCU
BTF_ID(func, rcu_read_unlock_strict)
#endif
#if defined(CONFIG_DEBUG_PREEMPT) || defined(CONFIG_TRACE_PREEMPT_TOGGLE)
BTF_ID(func, preempt_count_add)
BTF_ID(func, preempt_count_sub)
#endif
#ifdef CONFIG_PREEMPT_RCU
BTF_ID(func, __rcu_read_lock)
BTF_ID(func, __rcu_read_unlock)
#endif
BTF_SET_END(btf_id_deny)

static bool can_be_sleepable(struct bpf_prog *prog)
{
	if (prog->type == BPF_PROG_TYPE_TRACING) {
		switch (prog->expected_attach_type) {
		case BPF_TRACE_FENTRY:
		case BPF_TRACE_FEXIT:
		case BPF_MODIFY_RETURN:
		case BPF_TRACE_ITER:
			return true;
		default:
			return false;
		}
	}
	return prog->type == BPF_PROG_TYPE_LSM ||
	       prog->type == BPF_PROG_TYPE_KPROBE /* only for uprobes */ ||
	       prog->type == BPF_PROG_TYPE_STRUCT_OPS;
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

	if (prog->type == BPF_PROG_TYPE_SYSCALL) {
		if (prog->sleepable)
			/* attach_btf_id checked to be zero already */
			return 0;
		verbose(env, "Syscall programs can only be sleepable\n");
		return -EINVAL;
	}

	if (prog->sleepable && !can_be_sleepable(prog)) {
		verbose(env, "Only fentry/fexit/fmod_ret, lsm, iter, uprobe, and struct_ops programs can be sleepable\n");
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
	prog->aux->mod = tgt_info.tgt_mod;

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
	} else if (prog->type == BPF_PROG_TYPE_TRACING &&
		   btf_id_set_contains(&btf_id_deny, btf_id)) {
		return -EINVAL;
	}

	key = bpf_trampoline_compute_key(tgt_prog, prog->aux->attach_btf, btf_id);
	tr = bpf_trampoline_get(key, &tgt_info);
	if (!tr)
		return -ENOMEM;

	if (tgt_prog && tgt_prog->aux->tail_call_reachable)
		tr->flags = BPF_TRAMP_F_TAIL_CALL_CTX;

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

int bpf_check(struct bpf_prog **prog, union bpf_attr *attr, bpfptr_t uattr, __u32 uattr_size)
{
	u64 start_time = ktime_get_ns();
	struct bpf_verifier_env *env;
	int i, len, ret = -EINVAL, err;
	u32 log_true_size;
	bool is_priv;

	/* no program is valid */
	if (ARRAY_SIZE(bpf_verifier_ops) == 0)
		return -EINVAL;

	/* 'struct bpf_verifier_env' can be global, but since it's not small,
	 * allocate/free it every time bpf_check() is called
	 */
	env = kvzalloc(sizeof(struct bpf_verifier_env), GFP_KERNEL);
	if (!env)
		return -ENOMEM;

	env->bt.env = env;

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
	env->fd_array = make_bpfptr(attr->fd_array, uattr.is_kernel);

	env->allow_ptr_leaks = bpf_allow_ptr_leaks(env->prog->aux->token);
	env->allow_uninit_stack = bpf_allow_uninit_stack(env->prog->aux->token);
	env->bypass_spec_v1 = bpf_bypass_spec_v1(env->prog->aux->token);
	env->bypass_spec_v4 = bpf_bypass_spec_v4(env->prog->aux->token);
	env->bpf_capable = is_priv = bpf_token_capable(env->prog->aux->token, CAP_BPF);

	bpf_get_btf_vmlinux();

	/* grab the mutex to protect few globals used by verifier */
	if (!is_priv)
		mutex_lock(&bpf_verifier_lock);

	/* user could have requested verbose verifier output
	 * and supplied buffer to store the verification trace
	 */
	ret = bpf_vlog_init(&env->log, attr->log_level,
			    (char __user *) (unsigned long) attr->log_buf,
			    attr->log_size);
	if (ret)
		goto err_unlock;

	mark_verifier_state_clean(env);

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

	if (is_priv)
		env->test_state_freq = attr->prog_flags & BPF_F_TEST_STATE_FREQ;
	env->test_reg_invariants = attr->prog_flags & BPF_F_TEST_REG_INVARIANTS;

	env->explored_states = kvcalloc(state_htab_size(env),
				       sizeof(struct bpf_verifier_state_list *),
				       GFP_USER);
	ret = -ENOMEM;
	if (!env->explored_states)
		goto skip_full_check;

	ret = check_btf_info_early(env, attr, uattr);
	if (ret < 0)
		goto skip_full_check;

	ret = add_subprog_and_kfunc(env);
	if (ret < 0)
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

	if (bpf_prog_is_offloaded(env->prog->aux)) {
		ret = bpf_prog_offload_verifier_prep(env->prog);
		if (ret)
			goto skip_full_check;
	}

	ret = check_cfg(env);
	if (ret < 0)
		goto skip_full_check;

	ret = mark_fastcall_patterns(env);
	if (ret < 0)
		goto skip_full_check;

	ret = do_check_main(env);
	ret = ret ?: do_check_subprogs(env);

	if (ret == 0 && bpf_prog_is_offloaded(env->prog->aux))
		ret = bpf_prog_offload_finalize(env);

skip_full_check:
	kvfree(env->explored_states);

	/* might decrease stack depth, keep it before passes that
	 * allocate additional slots.
	 */
	if (ret == 0)
		ret = remove_fastcall_spills_fills(env);

	if (ret == 0)
		ret = check_max_stack_depth(env);

	/* instruction rewrites happen after this point */
	if (ret == 0)
		ret = optimize_bpf_loop(env);

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
		ret = do_misc_fixups(env);

	/* do 32-bit optimization after insn patching has done so those patched
	 * insns could be handled correctly.
	 */
	if (ret == 0 && !bpf_prog_is_offloaded(env->prog->aux)) {
		ret = opt_subreg_zext_lo32_rnd_hi32(env, attr);
		env->prog->aux->verifier_zext = bpf_jit_needs_zext() ? !ret
								     : false;
	}

	if (ret == 0)
		ret = fixup_call_args(env);

	env->verification_time = ktime_get_ns() - start_time;
	print_verification_stats(env);
	env->prog->aux->verified_insns = env->insn_processed;

	/* preserve original error even if log finalization is successful */
	err = bpf_vlog_finalize(&env->log, &log_true_size);
	if (err)
		ret = err;

	if (uattr_size >= offsetofend(union bpf_attr, log_true_size) &&
	    copy_to_bpfptr_offset(uattr, offsetof(union bpf_attr, log_true_size),
				  &log_true_size, sizeof(log_true_size))) {
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

	module_put(env->attach_btf_mod);
err_unlock:
	if (!is_priv)
		mutex_unlock(&bpf_verifier_lock);
	vfree(env->insn_aux_data);
	kvfree(env->insn_hist);
err_free_env:
	kvfree(env);
	return ret;
}
