/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BPF_TESTMOD_KFUNC_H
#define _BPF_TESTMOD_KFUNC_H

#ifndef __KERNEL__
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#else
#define __ksym
struct prog_test_member1 {
	int a;
};

struct prog_test_member {
	struct prog_test_member1 m;
	int c;
};

struct prog_test_ref_kfunc {
	int a;
	int b;
	struct prog_test_member memb;
	struct prog_test_ref_kfunc *next;
	refcount_t cnt;
};
#endif

#if __has_attribute(btf_type_tag)
#define __arena_tag __attribute__((btf_type_tag("arena")))
#else
#define __arena_tag
#endif

struct bpf_iter_testmod_seq;

struct prog_test_pass1 {
	int x0;
	struct {
		int x1;
		struct {
			int x2;
			struct {
				int x3;
			};
		};
	};
};

struct prog_test_pass2 {
	int len;
	short arr1[4];
	struct {
		char arr2[4];
		unsigned long arr3[8];
	} x;
};

struct prog_test_big_arg {
	__u64 a;
	__u64 b;
};

struct prog_test_ret_pair {	/* 16 bytes: R0:R2 */
	__u64 lo;
	__u64 hi;
};

struct prog_test_ret_ii {	/* 8 bytes: R0 only */
	int a;
	int b;
};

struct prog_test_ret_ptr {	/* 16 bytes: contains a pointer */
	void *p;
	__u64 tag;
};

struct prog_test_ret_arena {	/* 16 bytes: two arena pointers */
	void __arena_tag *a;
	void __arena_tag *b;
};

struct prog_test_ret_arena_mixed {	/* 16 bytes: an arena pointer and a scalar */
	void __arena_tag *p;
	__u64 tag;
};

struct prog_test_ret_arena_untagged {	/* 16 bytes: 'b' lacks the arena tag */
	void __arena_tag *a;
	void *b;
};

union prog_test_ret_arena_union {	/* 8 bytes: 'b' lacks the arena tag */
	void __arena_tag *a;
	void *b;
};

struct prog_test_ret_nested {	/* 16 bytes: the pointer hides one level down */
	struct {
		void *p;
	} in;
	__u64 tag;
};

struct prog_test_ret_ptr_arr {	/* 16 bytes: an array of pointers */
	void *p[2];
};

struct prog_test_ret_arr_struct {	/* 16 bytes: the pointer is under an array of structs */
	struct {
		struct {
			void *p;
		} in2;
	} in1[2];
};

struct prog_test_ret_arr2d {	/* 16 bytes: a two dimensional array of structs */
	struct {
		void *p;
	} a[1][2];
};

struct prog_test_ret_deep {	/* 8 bytes, but nested past the 4-level walk limit */
	struct {
		struct {
			struct {
				struct {
					__u64 v;
				} l4;
			} l3;
		} l2;
	} l1;
};

struct prog_test_ret_big {	/* 24 bytes: too large for R0:R2 */
	__u64 a;
	__u64 b;
	__u64 c;
};

struct prog_test_fail1 {
	void *p;
	int x;
};

struct prog_test_fail2 {
	int x8;
	struct prog_test_pass1 x;
};

struct prog_test_fail3 {
	int len;
	char arr1[2];
	char arr2[];
};

struct init_sock_args {
	int af;
	int type;
};

struct addr_args {
	char addr[sizeof(struct __kernel_sockaddr_storage)];
	int addrlen;
};

struct sendmsg_args {
	struct addr_args addr;
	char msg[10];
	int msglen;
};

struct bpf_testmod_ctx {
	struct callback_head	rcu;
	refcount_t		usage;
};

struct prog_test_ref_kfunc *
bpf_kfunc_call_test_acquire(unsigned long *scalar_ptr) __ksym;
void bpf_kfunc_call_test_release(struct prog_test_ref_kfunc *p) __ksym;
void bpf_kfunc_call_test_ref(struct prog_test_ref_kfunc *p) __ksym;

void bpf_kfunc_call_test_mem_len_pass1(void *mem, int len) __ksym;
__u64 bpf_kfunc_arena_arg_test(__u64 *val__arena) __ksym;
__u64 bpf_kfunc_arena_cap_test(__u64 *val__arena) __ksym;
__u64 bpf_kfunc_arena_cap_nullable_test(__u64 *val__arena__nullable) __ksym;
__u64 bpf_kfunc_arena_args5_test(__u64 *a__arena, __u64 *b__arena,
				 __u64 *c__arena, __u64 *d__arena,
				 __u64 *e__arena__nullable) __ksym;
__u64 bpf_kfunc_arena_stack_arg_test(__u64 a, __u64 b, __u64 c, __u64 d, __u64 e,
				     __u64 *f__arena) __ksym;
__u64 bpf_kfunc_arena_mixed_test(__u64 *a__arena, __u64 *b__arena__nullable) __ksym;
int *bpf_kfunc_call_test_get_rdwr_mem(struct prog_test_ref_kfunc *p, const int rdwr_buf_size) __ksym;
int *bpf_kfunc_call_test_get_rdonly_mem(struct prog_test_ref_kfunc *p, const int rdonly_buf_size) __ksym;
int *bpf_kfunc_call_test_acq_rdonly_mem(struct prog_test_ref_kfunc *p, const int rdonly_buf_size) __ksym;
void bpf_kfunc_call_int_mem_release(int *p) __ksym;

/* The bpf_kfunc_call_test_static_unused_arg is defined as static,
 * but bpf program compilation needs to see it as global symbol.
 */
#ifndef __KERNEL__
u32 bpf_kfunc_call_test_static_unused_arg(u32 arg, u32 unused) __ksym;
#endif

void bpf_testmod_test_mod_kfunc(int i) __ksym;
int bpf_testmod_ops3_call_test_arena(__u64 *ptr__arena) __ksym;
int bpf_testmod_ops3_call_test_arena_nullable(__u64 *ptr__arena__nullable) __ksym;
int bpf_testmod_ops3_call_test_arena_stack(__u64 *ptr__arena) __ksym;
int bpf_testmod_ops3_call_test_arena_multislot(__u64 *ptr__arena) __ksym;

__u64 bpf_kfunc_call_test1(struct sock *sk, __u32 a, __u64 b,
				__u32 c, __u64 d) __ksym;
int bpf_kfunc_call_test2(struct sock *sk, __u32 a, __u32 b) __ksym;
struct sock *bpf_kfunc_call_test3(struct sock *sk) __ksym;
long bpf_kfunc_call_test4(signed char a, short b, int c, long d) __ksym;
int bpf_kfunc_call_test5(__u8 a, __u16 b, __u32 c) __ksym;
#ifdef __SIZEOF_INT128__
__int128 bpf_kfunc_call_test_i128(__u64 a, __u64 b) __ksym;
#endif
struct prog_test_ret_pair bpf_kfunc_call_test_ret_pair(__u64 a, __u64 b) __ksym;
struct prog_test_ret_pair bpf_kfunc_call_test_ret_fastcall(__u64 a, __u64 b) __ksym;
struct prog_test_ret_ii bpf_kfunc_call_test_ret_ii(int a, int b) __ksym;
struct prog_test_ret_ptr bpf_kfunc_call_test_ret_ptr(__u64 tag) __ksym;
struct prog_test_ret_nested bpf_kfunc_call_test_ret_nested(__u64 tag) __ksym;
struct prog_test_ret_ptr_arr bpf_kfunc_call_test_ret_ptr_arr(void) __ksym;
struct prog_test_ret_arr_struct bpf_kfunc_call_test_ret_arr_struct(void) __ksym;
struct prog_test_ret_arr2d bpf_kfunc_call_test_ret_arr2d(void) __ksym;
struct prog_test_ret_arena bpf_kfunc_call_test_ret_arena(__u64 addr) __ksym;
struct prog_test_ret_arena_mixed bpf_kfunc_call_test_ret_arena_mixed(__u64 addr) __ksym;
struct prog_test_ret_arena_untagged bpf_kfunc_call_test_ret_arena_untagged(__u64 addr) __ksym;
union prog_test_ret_arena_union bpf_kfunc_call_test_ret_arena_union(__u64 addr) __ksym;
struct prog_test_ret_deep bpf_kfunc_call_test_ret_deep(__u64 v) __ksym;
struct prog_test_ret_big bpf_kfunc_call_test_ret_big(void) __ksym;
__u64 bpf_kfunc_call_stack_arg(__u64 a, __u64 b, __u64 c, __u64 d,
			       __u64 e, __u64 f, __u64 g, __u64 h,
			       __u64 i, __u64 j) __ksym;
__u64 bpf_kfunc_call_stack_arg_ptr(__u64 a, __u64 b, __u64 c, __u64 d, __u64 e,
				   __u64 f, __u64 g, __u64 h, __u64 i,
				   struct prog_test_pass1 *p) __ksym;
__u64 bpf_kfunc_call_stack_arg_mix(__u64 a, __u64 b, __u64 c, __u64 d, __u64 e,
				   __u64 f, __u64 g,
				   struct prog_test_pass1 *p, __u64 h,
				   struct prog_test_pass1 *q) __ksym;
__u64 bpf_kfunc_call_stack_arg_dynptr(__u64 a, __u64 b, __u64 c, __u64 d, __u64 e,
				      __u64 f, __u64 g, __u64 h, __u64 i,
				      struct bpf_dynptr *ptr) __ksym;
__u64 bpf_kfunc_call_stack_arg_mem(__u64 a, __u64 b, __u64 c, __u64 d, __u64 e,
				   void *mem, int mem__sz) __ksym;
__u64 bpf_kfunc_call_stack_arg_iter(__u64 a, __u64 b, __u64 c, __u64 d, __u64 e,
				    __u64 f, __u64 g, __u64 h, __u64 i,
				    struct bpf_iter_testmod_seq *it__iter) __ksym;
__u64 bpf_kfunc_call_stack_arg_const_str(__u64 a, __u64 b, __u64 c, __u64 d, __u64 e,
					 __u64 f, __u64 g, __u64 h, __u64 i,
					 const char *str__str) __ksym;
__u64 bpf_kfunc_call_stack_arg_timer(__u64 a, __u64 b, __u64 c, __u64 d, __u64 e,
				     __u64 f, __u64 g, __u64 h, __u64 i,
				     struct bpf_timer *timer) __ksym;
__u64 bpf_kfunc_call_stack_arg_big(__u64 a, __u64 b, __u64 c, __u64 d, __u64 e,
				   struct prog_test_big_arg s) __ksym;

void bpf_kfunc_call_test_pass_ctx(struct __sk_buff *skb) __ksym;
void bpf_kfunc_call_test_pass1(struct prog_test_pass1 *p) __ksym;
void bpf_kfunc_call_test_pass2(struct prog_test_pass2 *p) __ksym;
void bpf_kfunc_call_test_mem_len_fail2(__u64 *mem, int len) __ksym;

void bpf_kfunc_call_test_destructive(void) __ksym;
void bpf_kfunc_call_test_sleepable(void) __ksym;
int bpf_kfunc_call_test_call_rcu_tasks_trace(int *done) __ksym;

void bpf_kfunc_call_test_offset(struct prog_test_ref_kfunc *p);
struct prog_test_member *bpf_kfunc_call_memb_acquire(void);
void bpf_kfunc_call_memb1_release(struct prog_test_member1 *p);
void bpf_kfunc_call_test_fail1(struct prog_test_fail1 *p);
void bpf_kfunc_call_test_fail2(struct prog_test_fail2 *p);
void bpf_kfunc_call_test_fail3(struct prog_test_fail3 *p);
void bpf_kfunc_call_test_mem_len_fail1(void *mem, int len);

void bpf_kfunc_common_test(void) __ksym;

int bpf_kfunc_init_sock(struct init_sock_args *args) __ksym;
void bpf_kfunc_close_sock(void) __ksym;
int bpf_kfunc_call_kernel_connect(struct addr_args *args) __ksym;
int bpf_kfunc_call_kernel_bind(struct addr_args *args) __ksym;
int bpf_kfunc_call_kernel_listen(void) __ksym;
int bpf_kfunc_call_kernel_sendmsg(struct sendmsg_args *args) __ksym;
int bpf_kfunc_call_sock_sendmsg(struct sendmsg_args *args) __ksym;
int bpf_kfunc_call_kernel_getsockname(struct addr_args *args) __ksym;
int bpf_kfunc_call_kernel_getpeername(struct addr_args *args) __ksym;

void bpf_kfunc_dynptr_test(struct bpf_dynptr *ptr, struct bpf_dynptr *ptr__nullable) __ksym;

struct bpf_testmod_ctx *bpf_testmod_ctx_create(int *err) __ksym;
void bpf_testmod_ctx_release(struct bpf_testmod_ctx *ctx) __ksym;

struct sk_buff *bpf_kfunc_nested_acquire_nonzero_offset_test(struct sk_buff_head *ptr) __ksym;
struct sk_buff *bpf_kfunc_nested_acquire_zero_offset_test(struct sock_common *ptr) __ksym;
void bpf_kfunc_nested_release_test(struct sk_buff *ptr) __ksym;

struct st_ops_args;
int bpf_kfunc_st_ops_test_prologue(struct st_ops_args *args) __ksym;
int bpf_kfunc_st_ops_test_epilogue(struct st_ops_args *args) __ksym;
int bpf_kfunc_st_ops_test_pro_epilogue(struct st_ops_args *args) __ksym;
int bpf_kfunc_st_ops_inc10(struct st_ops_args *args) __ksym;

void bpf_kfunc_trusted_vma_test(struct vm_area_struct *ptr) __ksym;
void bpf_kfunc_trusted_task_test(struct task_struct *ptr) __ksym;
void bpf_kfunc_trusted_num_test(int *ptr) __ksym;
void bpf_kfunc_rcu_task_test(struct task_struct *ptr) __ksym;
struct task_struct *bpf_kfunc_ret_rcu_test(void) __ksym;
int *bpf_kfunc_ret_rcu_test_nostruct(int rdonly_buf_size) __ksym;

#ifndef __KERNEL__
extern int bpf_kfunc_multi_st_ops_test_1(struct st_ops_args *args, u32 id) __weak __ksym;
extern int bpf_kfunc_multi_st_ops_test_1_assoc(struct st_ops_args *args) __weak __ksym;
#endif

struct prog_test_member *bpf_kfunc_get_default_trusted_ptr_test(void) __ksym;
void bpf_kfunc_put_default_trusted_ptr_test(struct prog_test_member *trusted_ptr) __ksym;

void bpf_testmod_test_hardirq_fn(void);
void bpf_testmod_test_softirq_fn(void);
void bpf_kfunc_trigger_ctx_check(void) __ksym;

#endif /* _BPF_TESTMOD_KFUNC_H */
