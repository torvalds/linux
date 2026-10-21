// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_core_read.h>
#include "bpf_misc.h"
#include "bpf_kfuncs.h"
#include "../test_kmods/bpf_testmod_kfunc.h"

SEC("tp_btf/sys_enter")
__success
__log_level(2)
__msg("r8 = *(u64 *)(r7 +0)          ; R7=ptr_nameidata(imm={{[0-9]+}}) R8=rdonly_untrusted_mem(sz=0)")
__msg("r9 = *(u8 *)(r8 +0)           ; R8=rdonly_untrusted_mem(sz=0) R9=scalar")
int btf_id_to_ptr_mem(void *ctx)
{
	struct task_struct *task;
	struct nameidata *idata;
	u64 ret, off;

	task = bpf_get_current_task_btf();
	idata = task->nameidata;
	off = bpf_core_field_offset(struct nameidata, pathname);
	/*
	 * asm block to have reliable match target for __msg, equivalent of:
	 *   ret = task->nameidata->pathname[0];
	 */
	asm volatile (
	"r7 = %[idata];"
	"r7 += %[off];"
	"r8 = *(u64 *)(r7 + 0);"
	"r9 = *(u8 *)(r8 + 0);"
	"%[ret] = r9;"
	: [ret]"=r"(ret)
	: [idata]"r"(idata),
	  [off]"r"(off)
	: "r7", "r8", "r9");
	return ret;
}

SEC("socket")
__success
__retval(0)
int ldx_is_ok_bad_addr(void *ctx)
{
	char *p;

	if (!bpf_core_enum_value_exists(enum bpf_features, BPF_FEAT_RDONLY_CAST_TO_VOID))
		return 42;

	p = bpf_rdonly_cast(0, 0);
	return p[0x7fff];
}

SEC("socket")
__success
__retval(1)
int ldx_is_ok_good_addr(void *ctx)
{
	int v, *p;

	v = 1;
	p = bpf_rdonly_cast(&v, 0);
	return *p;
}

SEC("socket")
__success
int offset_not_tracked(void *ctx)
{
	int *p, i, s;

	p = bpf_rdonly_cast(0, 0);
	s = 0;
	bpf_for(i, 0, 1000 * 1000 * 1000) {
		p++;
		s += *p;
	}
	return s;
}

SEC("socket")
__failure
__msg("cannot write into rdonly_untrusted_mem")
int stx_not_ok(void *ctx)
{
	int v, *p;

	v = 1;
	p = bpf_rdonly_cast(&v, 0);
	*p = 1;
	return 0;
}

SEC("socket")
__failure
__msg("cannot write into rdonly_untrusted_mem")
int atomic_not_ok(void *ctx)
{
	int v, *p;

	v = 1;
	p = bpf_rdonly_cast(&v, 0);
	__sync_fetch_and_add(p, 1);
	return 0;
}

SEC("socket")
__failure
__msg("cannot write into rdonly_untrusted_mem")
int atomic_rmw_not_ok(void *ctx)
{
	long v, *p;

	v = 1;
	p = bpf_rdonly_cast(&v, 0);
	return __sync_val_compare_and_swap(p, 0, 42);
}

SEC("socket")
__failure
__msg("invalid access to memory, mem_size=0 off=0 size=4")
__msg("R1 min value is outside of the allowed memory range")
int kfunc_param_not_ok(void *ctx)
{
	int *p;

	p = bpf_rdonly_cast(0, 0);
	bpf_kfunc_trusted_num_test(p);
	return 0;
}

SEC("?fentry.s/" SYS_PREFIX "sys_getpgid")
__failure
__msg("R1 type=rdonly_untrusted_mem expected=")
int helper_param_not_ok(void *ctx)
{
	char *p;

	p = bpf_rdonly_cast(0, 0);
	/*
	 * Any helper with ARG_MEM_SIZE_OR_ZERO constraint will do,
	 * the most permissive constraint
	 */
	bpf_copy_from_user(p, 0, (void *)42);
	return 0;
}

static __noinline u64 *get_some_addr(void)
{
	if (bpf_get_prandom_u32())
		return bpf_rdonly_cast(0, bpf_core_type_id_kernel(struct sock));
	else
		return bpf_rdonly_cast(0, 0);
}

SEC("socket")
__success
__retval(0)
int mixed_mem_type(void *ctx)
{
	u64 *p;

	/* Try to avoid compiler hoisting load to if branches by using __noinline func. */
	p = get_some_addr();
	return *p;
}

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
	__uint(max_entries, 4096);
} ringbuf SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, u32);
	__type(value, u64);
} array SEC(".maps");

char dynptr_data[8];

int zero;

SEC("socket")
__success
__log_level(2)
__msg("r8 = *(u64 *)(r7 +0){{.*}}R7=untrusted_ptr_sock")
__msg("r8 = *(u64 *)(r7 +0){{.*}}R7=ringbuf_mem")
__retval(0)
int mixed_mem_untrusted_btf_id_type(void *ctx)
{
	u64 *p, *q, v;

	p = bpf_ringbuf_reserve(&ringbuf, sizeof(*p), 0);
	if (!p)
		return 1;
	*p = 42;
	q = bpf_rdonly_cast(0, bpf_core_type_id_kernel(struct sock));
	/*
	 * The load below is reached with PTR_TO_MEM | MEM_RINGBUF on one
	 * path and with PTR_TO_BTF_ID | PTR_UNTRUSTED on the other. The
	 * merged type has to keep the BPF_PROBE_MEM rewrite, otherwise
	 * the NULL deref taken at runtime panics the kernel instead of
	 * returning 0.
	 */
	asm volatile (
	"r7 = %[p];"
	"if %[zero] != 0 goto +1;"
	"r7 = %[q];"
	"r8 = *(u64 *)(r7 + 0);"
	"%[v] = r8;"
	: [v]"=r"(v)
	: [p]"r"(p),
	  [q]"r"(q),
	  [zero]"r"(zero)
	: "r7", "r8");
	bpf_ringbuf_discard(p, 0);
	return v;
}

SEC("socket")
__success
__log_level(2)
__msg("r8 = *(u32 *)(r7 +0){{.*}}R7=ptr_nameidata")
__msg("r8 = *(u32 *)(r7 +0){{.*}}R7=ringbuf_mem")
__retval(0)
int mixed_mem_btf_id_type(void *ctx)
{
	struct task_struct *task;
	u32 *p, *q;
	u64 v;

	p = bpf_ringbuf_reserve(&ringbuf, sizeof(*p), 0);
	if (!p)
		return 1;
	*p = 42;
	task = bpf_get_current_task_btf();
	/*
	 * A plain BTF pointer walk yields a bare PTR_TO_BTF_ID, and
	 * task->nameidata is NULL unless the task currently is in the
	 * middle of a path lookup.
	 */
	q = (u32 *)&task->nameidata->flags;
	/*
	 * Same as above, except that the other path yields a bare
	 * PTR_TO_BTF_ID. Merging it with PTR_TO_MEM used to drop the
	 * BPF_PROBE_MEM rewrite the bare PTR_TO_BTF_ID would have
	 * gotten on its own.
	 */
	asm volatile (
	"r7 = %[p];"
	"if %[zero] != 0 goto +1;"
	"r7 = %[q];"
	"r8 = *(u32 *)(r7 + 0);"
	"%[v] = r8;"
	: [v]"=r"(v)
	: [p]"r"(p),
	  [q]"r"(q),
	  [zero]"r"(zero)
	: "r7", "r8");
	bpf_ringbuf_discard(p, 0);
	return v;
}

SEC("socket")
__success
__log_level(2)
__msg("r8 = *(u32 *)(r7 +0){{.*}}R7=ptr_nameidata")
__msg("r8 = *(u32 *)(r7 +0){{.*}}R7=rdonly_mem")
__retval(0)
int mixed_rdonly_mem_btf_id_type(void *ctx)
{
	struct task_struct *task;
	struct bpf_dynptr dptr;
	char buf[sizeof(u32)];
	u32 *p, *q;
	u64 v;

	if (bpf_dynptr_from_mem(dynptr_data, sizeof(dynptr_data), 0, &dptr))
		return 1;
	p = bpf_dynptr_slice(&dptr, 0, buf, sizeof(buf));
	if (!p)
		return 1;
	task = bpf_get_current_task_btf();
	q = (u32 *)&task->nameidata->flags;
	/*
	 * Same as above, except that the PTR_TO_MEM side already carries
	 * MEM_RDONLY. Merging it with a bare PTR_TO_BTF_ID used to yield
	 * PTR_TO_MEM | MEM_RDONLY, which is not rewritten either since
	 * only its PTR_UNTRUSTED variant is.
	 */
	asm volatile (
	"r7 = %[p];"
	"if %[zero] != 0 goto +1;"
	"r7 = %[q];"
	"r8 = *(u32 *)(r7 + 0);"
	"%[v] = r8;"
	: [v]"=r"(v)
	: [p]"r"(p),
	  [q]"r"(q),
	  [zero]"r"(zero)
	: "r7", "r8");
	return v;
}

SEC("socket")
__success
__log_level(2)
__msg("r8 = *(u64 *)(r7 +0){{.*}}R7=ringbuf_mem")
__msg("r8 = *(u64 *)(r7 +0){{.*}}R7=rdonly_untrusted_mem")
__retval(0)
int mixed_mem_mem_type(void *ctx)
{
	u64 *p, *q, v;

	p = bpf_ringbuf_reserve(&ringbuf, sizeof(*p), 0);
	if (!p)
		return 1;
	*p = 42;
	q = bpf_rdonly_cast(0, 0);
	/*
	 * Both paths are PTR_TO_MEM based, so they used to not trip the
	 * type mismatch check and skipped the merge altogether, leaving
	 * the insn with the PTR_TO_MEM | MEM_RINGBUF recorded first and
	 * hence without the BPF_PROBE_MEM rewrite the other path needs.
	 */
	asm volatile (
	"r7 = %[q];"
	"if %[zero] == 0 goto +1;"
	"r7 = %[p];"
	"r8 = *(u64 *)(r7 + 0);"
	"%[v] = r8;"
	: [v]"=r"(v)
	: [p]"r"(p),
	  [q]"r"(q),
	  [zero]"r"(zero)
	: "r7", "r8");
	bpf_ringbuf_discard(p, 0);
	return v;
}

SEC("socket")
__failure
__msg("same insn cannot be used with different pointers")
int mixed_map_value_mem_type(void *ctx)
{
	u64 *p, *q, v;
	u32 key = 0;

	p = bpf_map_lookup_elem(&array, &key);
	if (!p)
		return 1;
	q = bpf_rdonly_cast(0, 0);
	/*
	 * PTR_TO_MAP_VALUE is neither PTR_TO_MEM nor PTR_TO_BTF_ID based,
	 * so it cannot be merged into a type which keeps the BPF_PROBE_MEM
	 * rewrite the PTR_TO_MEM | MEM_RDONLY | PTR_UNTRUSTED of the other
	 * path needs. Both bases were mismatch ok, hence the load used to be
	 * accepted with the PTR_TO_MAP_VALUE recorded and the NULL deref on
	 * the second path panicked the kernel.
	 */
	asm volatile (
	"r7 = %[q];"
	"if %[zero] == 0 goto +1;"
	"r7 = %[p];"
	"r8 = *(u64 *)(r7 + 0);"
	"%[v] = r8;"
	: [v]"=r"(v)
	: [p]"r"(p),
	  [q]"r"(q),
	  [zero]"r"(zero)
	: "r7", "r8");
	return v;
}

SEC("socket")
__failure
__msg("same insn cannot be used with different pointers")
int mixed_stack_mem_type(void *ctx)
{
	u64 *p = bpf_rdonly_cast(0, 0);
	u64 s = 42, v;

	/*
	 * Same as above, but for a PTR_TO_STACK on the other path.
	 */
	asm volatile (
	"r7 = %[p];"
	"if %[zero] == 0 goto +1;"
	"r7 = %[s];"
	"r8 = *(u64 *)(r7 + 0);"
	"%[v] = r8;"
	: [v]"=r"(v)
	: [p]"r"(p),
	  [s]"r"(&s),
	  [zero]"r"(zero)
	: "r7", "r8");
	return v;
}

__attribute__((__aligned__(8)))
u8 global[] = {
	0x11, 0x22, 0x33, 0x44,
	0x55, 0x66, 0x77, 0x88,
	0x99
};

__always_inline
static u64 combine(void *p)
{
	u64 acc;

	acc = 0;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	acc |= (*(u64 *)p >> 56) << 24;
	acc |= (*(u32 *)p >> 24) << 16;
	acc |= (*(u16 *)p >> 8)  << 8;
	acc |= *(u8 *)p;
#else
	acc |= (*(u64 *)p & 0xff) << 24;
	acc |= (*(u32 *)p & 0xff) << 16;
	acc |= (*(u16 *)p & 0xff) << 8;
	acc |= *(u8 *)p;
#endif
	return acc;
}

SEC("socket")
__retval(0x88442211)
int diff_size_access(void *ctx)
{
	return combine(bpf_rdonly_cast(&global, 0));
}

SEC("socket")
__retval(0x99553322)
int misaligned_access(void *ctx)
{
	return combine(bpf_rdonly_cast(&global, 0) + 1);
}

__weak int return_one(void)
{
	return 1;
}

SEC("socket")
__success
__retval(1)
int null_check(void *ctx)
{
	int *p;

	p = bpf_rdonly_cast(0, 0);
	if (p == 0)
		/* make this a function call to avoid compiler
		 * moving r0 assignment before check.
		 */
		return return_one();
	return 0;
}

SEC("socket")
__success
__retval(1)
int ldx_is_ok_commuted_addr(void *ctx)
{
	int v, *p, *derived;

	v = 1;
	p = bpf_rdonly_cast(&v, 0);
	asm volatile ("%[dst] = 0;"
		"%[dst] += %[src];"
		: [dst]"=&r"(derived)
		: [src]"r"(p)
		: "memory");
	return *derived;
}

char _license[] SEC("license") = "GPL";
