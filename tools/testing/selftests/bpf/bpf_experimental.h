#ifndef __BPF_EXPERIMENTAL__
#define __BPF_EXPERIMENTAL__

#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>

#define __contains(name, node) __attribute__((btf_decl_tag("contains:" #name ":" #node)))

/* Description
 *	Allocates an object of the type represented by 'local_type_id' in
 *	program BTF. User may use the bpf_core_type_id_local macro to pass the
 *	type ID of a struct in program BTF.
 *
 *	The 'local_type_id' parameter must be a known constant.
 *	The 'meta' parameter is rewritten by the verifier, no need for BPF
 *	program to set it.
 * Returns
 *	A pointer to an object of the type corresponding to the passed in
 *	'local_type_id', or NULL on failure.
 */
extern void *bpf_obj_new_impl(__u64 local_type_id, void *meta) __ksym;

/* Convenience macro to wrap over bpf_obj_new_impl */
#define bpf_obj_new(type) ((type *)bpf_obj_new_impl(bpf_core_type_id_local(type), NULL))

/* Description
 *	Free an allocated object. All fields of the object that require
 *	destruction will be destructed before the storage is freed.
 *
 *	The 'meta' parameter is rewritten by the verifier, no need for BPF
 *	program to set it.
 * Returns
 *	Void.
 */
extern void bpf_obj_drop_impl(void *kptr, void *meta) __ksym;

/* Convenience macro to wrap over bpf_obj_drop_impl */
#define bpf_obj_drop(kptr) bpf_obj_drop_impl(kptr, NULL)

/* Description
 *	Increment the refcount on a refcounted local kptr, turning the
 *	non-owning reference input into an owning reference in the process.
 *
 *	The 'meta' parameter is rewritten by the verifier, no need for BPF
 *	program to set it.
 * Returns
 *	An owning reference to the object pointed to by 'kptr'
 */
extern void *bpf_refcount_acquire_impl(void *kptr, void *meta) __ksym;

/* Convenience macro to wrap over bpf_refcount_acquire_impl */
#define bpf_refcount_acquire(kptr) bpf_refcount_acquire_impl(kptr, NULL)

/* Description
 *	Add a new entry to the beginning of the BPF linked list.
 *
 *	The 'meta' and 'off' parameters are rewritten by the verifier, no need
 *	for BPF programs to set them
 * Returns
 *	0 if the node was successfully added
 *	-EINVAL if the node wasn't added because it's already in a list
 */
extern int bpf_list_push_front_impl(struct bpf_list_head *head,
				    struct bpf_list_node *node,
				    void *meta, __u64 off) __ksym;

/* Convenience macro to wrap over bpf_list_push_front_impl */
#define bpf_list_push_front(head, node) bpf_list_push_front_impl(head, node, NULL, 0)

/* Description
 *	Add a new entry to the end of the BPF linked list.
 *
 *	The 'meta' and 'off' parameters are rewritten by the verifier, no need
 *	for BPF programs to set them
 * Returns
 *	0 if the node was successfully added
 *	-EINVAL if the node wasn't added because it's already in a list
 */
extern int bpf_list_push_back_impl(struct bpf_list_head *head,
				   struct bpf_list_node *node,
				   void *meta, __u64 off) __ksym;

/* Convenience macro to wrap over bpf_list_push_back_impl */
#define bpf_list_push_back(head, node) bpf_list_push_back_impl(head, node, NULL, 0)

/* Description
 *	Remove the entry at the beginning of the BPF linked list.
 * Returns
 *	Pointer to bpf_list_node of deleted entry, or NULL if list is empty.
 */
extern struct bpf_list_node *bpf_list_pop_front(struct bpf_list_head *head) __ksym;

/* Description
 *	Remove the entry at the end of the BPF linked list.
 * Returns
 *	Pointer to bpf_list_node of deleted entry, or NULL if list is empty.
 */
extern struct bpf_list_node *bpf_list_pop_back(struct bpf_list_head *head) __ksym;

/* Description
 *	Remove 'node' from rbtree with root 'root'
 * Returns
 * 	Pointer to the removed node, or NULL if 'root' didn't contain 'node'
 */
extern struct bpf_rb_node *bpf_rbtree_remove(struct bpf_rb_root *root,
					     struct bpf_rb_node *node) __ksym;

/* Description
 *	Add 'node' to rbtree with root 'root' using comparator 'less'
 *
 *	The 'meta' and 'off' parameters are rewritten by the verifier, no need
 *	for BPF programs to set them
 * Returns
 *	0 if the node was successfully added
 *	-EINVAL if the node wasn't added because it's already in a tree
 */
extern int bpf_rbtree_add_impl(struct bpf_rb_root *root, struct bpf_rb_node *node,
			       bool (less)(struct bpf_rb_node *a, const struct bpf_rb_node *b),
			       void *meta, __u64 off) __ksym;

/* Convenience macro to wrap over bpf_rbtree_add_impl */
#define bpf_rbtree_add(head, node, less) bpf_rbtree_add_impl(head, node, less, NULL, 0)

/* Description
 *	Return the first (leftmost) node in input tree
 * Returns
 *	Pointer to the node, which is _not_ removed from the tree. If the tree
 *	contains no nodes, returns NULL.
 */
extern struct bpf_rb_node *bpf_rbtree_first(struct bpf_rb_root *root) __ksym;

/* Description
 *	Allocates a percpu object of the type represented by 'local_type_id' in
 *	program BTF. User may use the bpf_core_type_id_local macro to pass the
 *	type ID of a struct in program BTF.
 *
 *	The 'local_type_id' parameter must be a known constant.
 *	The 'meta' parameter is rewritten by the verifier, no need for BPF
 *	program to set it.
 * Returns
 *	A pointer to a percpu object of the type corresponding to the passed in
 *	'local_type_id', or NULL on failure.
 */
extern void *bpf_percpu_obj_new_impl(__u64 local_type_id, void *meta) __ksym;

/* Convenience macro to wrap over bpf_percpu_obj_new_impl */
#define bpf_percpu_obj_new(type) ((type __percpu_kptr *)bpf_percpu_obj_new_impl(bpf_core_type_id_local(type), NULL))

/* Description
 *	Free an allocated percpu object. All fields of the object that require
 *	destruction will be destructed before the storage is freed.
 *
 *	The 'meta' parameter is rewritten by the verifier, no need for BPF
 *	program to set it.
 * Returns
 *	Void.
 */
extern void bpf_percpu_obj_drop_impl(void *kptr, void *meta) __ksym;

struct bpf_iter_task_vma;

extern int bpf_iter_task_vma_new(struct bpf_iter_task_vma *it,
				 struct task_struct *task,
				 __u64 addr) __ksym;
extern struct vm_area_struct *bpf_iter_task_vma_next(struct bpf_iter_task_vma *it) __ksym;
extern void bpf_iter_task_vma_destroy(struct bpf_iter_task_vma *it) __ksym;

/* Convenience macro to wrap over bpf_obj_drop_impl */
#define bpf_percpu_obj_drop(kptr) bpf_percpu_obj_drop_impl(kptr, NULL)

/* Description
 *	Throw a BPF exception from the program, immediately terminating its
 *	execution and unwinding the stack. The supplied 'cookie' parameter
 *	will be the return value of the program when an exception is thrown,
 *	and the default exception callback is used. Otherwise, if an exception
 *	callback is set using the '__exception_cb(callback)' declaration tag
 *	on the main program, the 'cookie' parameter will be the callback's only
 *	input argument.
 *
 *	Thus, in case of default exception callback, 'cookie' is subjected to
 *	constraints on the program's return value (as with R0 on exit).
 *	Otherwise, the return value of the marked exception callback will be
 *	subjected to the same checks.
 *
 *	Note that throwing an exception with lingering resources (locks,
 *	references, etc.) will lead to a verification error.
 *
 *	Note that callbacks *cannot* call this helper.
 * Returns
 *	Never.
 * Throws
 *	An exception with the specified 'cookie' value.
 */
extern void bpf_throw(u64 cookie) __ksym;

/* Description
 *	Acquire a reference on the exe_file member field belonging to the
 *	mm_struct that is nested within the supplied task_struct. The supplied
 *	task_struct must be trusted/referenced.
 * Returns
 *	A referenced file pointer pointing to the exe_file member field of the
 *	mm_struct nested in the supplied task_struct, or NULL.
 */
extern struct file *bpf_get_task_exe_file(struct task_struct *task) __ksym;

/* Description
 *	Release a reference on the supplied file. The supplied file must be
 *	acquired.
 */
extern void bpf_put_file(struct file *file) __ksym;

/* Description
 *	Resolve a pathname for the supplied path and store it in the supplied
 *	buffer. The supplied path must be trusted/referenced.
 * Returns
 *	A positive integer corresponding to the length of the resolved pathname,
 *	including the NULL termination character, stored in the supplied
 *	buffer. On error, a negative integer is returned.
 */
extern int bpf_path_d_path(const struct path *path, char *buf, size_t buf__sz) __ksym;

/* This macro must be used to mark the exception callback corresponding to the
 * main program. For example:
 *
 * int exception_cb(u64 cookie) {
 *	return cookie;
 * }
 *
 * SEC("tc")
 * __exception_cb(exception_cb)
 * int main_prog(struct __sk_buff *ctx) {
 *	...
 *	return TC_ACT_OK;
 * }
 *
 * Here, exception callback for the main program will be 'exception_cb'. Note
 * that this attribute can only be used once, and multiple exception callbacks
 * specified for the main program will lead to verification error.
 */
#define __exception_cb(name) __attribute__((btf_decl_tag("exception_callback:" #name)))

#define __bpf_assert_signed(x) _Generic((x), \
    unsigned long: 0,       \
    unsigned long long: 0,  \
    signed long: 1,         \
    signed long long: 1     \
)

#define __bpf_assert_check(LHS, op, RHS)								 \
	_Static_assert(sizeof(&(LHS)), "1st argument must be an lvalue expression");			 \
	_Static_assert(sizeof(LHS) == 8, "Only 8-byte integers are supported\n");			 \
	_Static_assert(__builtin_constant_p(__bpf_assert_signed(LHS)), "internal static assert");	 \
	_Static_assert(__builtin_constant_p((RHS)), "2nd argument must be a constant expression")

#define __bpf_assert(LHS, op, cons, RHS, VAL)							\
	({											\
		(void)bpf_throw;								\
		asm volatile ("if %[lhs] " op " %[rhs] goto +2; r1 = %[value]; call bpf_throw"	\
			       : : [lhs] "r"(LHS), [rhs] cons(RHS), [value] "ri"(VAL) : );	\
	})

#define __bpf_assert_op_sign(LHS, op, cons, RHS, VAL, supp_sign)			\
	({										\
		__bpf_assert_check(LHS, op, RHS);					\
		if (__bpf_assert_signed(LHS) && !(supp_sign))				\
			__bpf_assert(LHS, "s" #op, cons, RHS, VAL);			\
		else									\
			__bpf_assert(LHS, #op, cons, RHS, VAL);				\
	 })

#define __bpf_assert_op(LHS, op, RHS, VAL, supp_sign)					\
	({										\
		if (sizeof(typeof(RHS)) == 8) {						\
			const typeof(RHS) rhs_var = (RHS);				\
			__bpf_assert_op_sign(LHS, op, "r", rhs_var, VAL, supp_sign);	\
		} else {								\
			__bpf_assert_op_sign(LHS, op, "i", RHS, VAL, supp_sign);	\
		}									\
	 })

#define __cmp_cannot_be_signed(x) \
	__builtin_strcmp(#x, "==") == 0 || __builtin_strcmp(#x, "!=") == 0 || \
	__builtin_strcmp(#x, "&") == 0

#define __is_signed_type(type) (((type)(-1)) < (type)1)

#define __bpf_cmp(LHS, OP, PRED, RHS, DEFAULT)						\
	({											\
		__label__ l_true;								\
		bool ret = DEFAULT;								\
		asm volatile goto("if %[lhs] " OP " %[rhs] goto %l[l_true]"		\
				  :: [lhs] "r"((short)LHS), [rhs] PRED (RHS) :: l_true);	\
		ret = !DEFAULT;									\
l_true:												\
		ret;										\
       })

/* C type conversions coupled with comparison operator are tricky.
 * Make sure BPF program is compiled with -Wsign-compare then
 * __lhs OP __rhs below will catch the mistake.
 * Be aware that we check only __lhs to figure out the sign of compare.
 */
#define _bpf_cmp(LHS, OP, RHS, UNLIKELY)								\
	({											\
		typeof(LHS) __lhs = (LHS);							\
		typeof(RHS) __rhs = (RHS);							\
		bool ret;									\
		_Static_assert(sizeof(&(LHS)), "1st argument must be an lvalue expression");	\
		(void)(__lhs OP __rhs);								\
		if (__cmp_cannot_be_signed(OP) || !__is_signed_type(typeof(__lhs))) {		\
			if (sizeof(__rhs) == 8)							\
				/* "i" will truncate 64-bit constant into s32,			\
				 * so we have to use extra register via "r".			\
				 */								\
				ret = __bpf_cmp(__lhs, #OP, "r", __rhs, UNLIKELY);		\
			else									\
				ret = __bpf_cmp(__lhs, #OP, "ri", __rhs, UNLIKELY);		\
		} else {									\
			if (sizeof(__rhs) == 8)							\
				ret = __bpf_cmp(__lhs, "s"#OP, "r", __rhs, UNLIKELY);		\
			else									\
				ret = __bpf_cmp(__lhs, "s"#OP, "ri", __rhs, UNLIKELY);		\
		}										\
		ret;										\
       })

#ifndef bpf_cmp_unlikely
#define bpf_cmp_unlikely(LHS, OP, RHS) _bpf_cmp(LHS, OP, RHS, true)
#endif

#ifndef bpf_cmp_likely
#define bpf_cmp_likely(LHS, OP, RHS)								\
	({											\
		bool ret = 0;									\
		if (__builtin_strcmp(#OP, "==") == 0)						\
			ret = _bpf_cmp(LHS, !=, RHS, false);					\
		else if (__builtin_strcmp(#OP, "!=") == 0)					\
			ret = _bpf_cmp(LHS, ==, RHS, false);					\
		else if (__builtin_strcmp(#OP, "<=") == 0)					\
			ret = _bpf_cmp(LHS, >, RHS, false);					\
		else if (__builtin_strcmp(#OP, "<") == 0)					\
			ret = _bpf_cmp(LHS, >=, RHS, false);					\
		else if (__builtin_strcmp(#OP, ">") == 0)					\
			ret = _bpf_cmp(LHS, <=, RHS, false);					\
		else if (__builtin_strcmp(#OP, ">=") == 0)					\
			ret = _bpf_cmp(LHS, <, RHS, false);					\
		else										\
			asm volatile("r0 " #OP " invalid compare");				\
		ret;										\
       })
#endif

/*
 * Note that cond_break can only be portably used in the body of a breakable
 * construct, whereas can_loop can be used anywhere.
 */
#ifdef __BPF_FEATURE_MAY_GOTO
#define can_loop					\
	({ __label__ l_break, l_continue;		\
	bool ret = true;				\
	asm volatile goto("may_goto %l[l_break]"	\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: ret = false;				\
	l_continue:;					\
	ret;						\
	})

#define __cond_break(expr)				\
	({ __label__ l_break, l_continue;		\
	asm volatile goto("may_goto %l[l_break]"	\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: expr;					\
	l_continue:;					\
	})
#else
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define can_loop					\
	({ __label__ l_break, l_continue;		\
	bool ret = true;				\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long ((%l[l_break] - 1b - 8) / 8) & 0xffff;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: ret = false;				\
	l_continue:;					\
	ret;						\
	})

#define __cond_break(expr)				\
	({ __label__ l_break, l_continue;		\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long ((%l[l_break] - 1b - 8) / 8) & 0xffff;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: expr;					\
	l_continue:;					\
	})
#else
#define can_loop					\
	({ __label__ l_break, l_continue;		\
	bool ret = true;				\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long (((%l[l_break] - 1b - 8) / 8) & 0xffff) << 16;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: ret = false;				\
	l_continue:;					\
	ret;						\
	})

#define __cond_break(expr)				\
	({ __label__ l_break, l_continue;		\
	asm volatile goto("1:.byte 0xe5;		\
		      .byte 0;				\
		      .long (((%l[l_break] - 1b - 8) / 8) & 0xffff) << 16;	\
		      .short 0"				\
		      :::: l_break);			\
	goto l_continue;				\
	l_break: expr;					\
	l_continue:;					\
	})
#endif
#endif

#define cond_break __cond_break(break)
#define cond_break_label(label) __cond_break(goto label)

#ifndef bpf_nop_mov
#define bpf_nop_mov(var) \
	asm volatile("%[reg]=%[reg]"::[reg]"r"((short)var))
#endif

/* emit instruction:
 * rX = rX .off = BPF_ADDR_SPACE_CAST .imm32 = (dst_as << 16) | src_as
 */
#ifndef bpf_addr_space_cast
#define bpf_addr_space_cast(var, dst_as, src_as)\
	asm volatile(".byte 0xBF;		\
		     .ifc %[reg], r0;		\
		     .byte 0x00;		\
		     .endif;			\
		     .ifc %[reg], r1;		\
		     .byte 0x11;		\
		     .endif;			\
		     .ifc %[reg], r2;		\
		     .byte 0x22;		\
		     .endif;			\
		     .ifc %[reg], r3;		\
		     .byte 0x33;		\
		     .endif;			\
		     .ifc %[reg], r4;		\
		     .byte 0x44;		\
		     .endif;			\
		     .ifc %[reg], r5;		\
		     .byte 0x55;		\
		     .endif;			\
		     .ifc %[reg], r6;		\
		     .byte 0x66;		\
		     .endif;			\
		     .ifc %[reg], r7;		\
		     .byte 0x77;		\
		     .endif;			\
		     .ifc %[reg], r8;		\
		     .byte 0x88;		\
		     .endif;			\
		     .ifc %[reg], r9;		\
		     .byte 0x99;		\
		     .endif;			\
		     .short %[off];		\
		     .long %[as]"		\
		     : [reg]"+r"(var)		\
		     : [off]"i"(BPF_ADDR_SPACE_CAST) \
		     , [as]"i"((dst_as << 16) | src_as));
#endif

void bpf_preempt_disable(void) __weak __ksym;
void bpf_preempt_enable(void) __weak __ksym;

typedef struct {
} __bpf_preempt_t;

static inline __bpf_preempt_t __bpf_preempt_constructor(void)
{
	__bpf_preempt_t ret = {};

	bpf_preempt_disable();
	return ret;
}
static inline void __bpf_preempt_destructor(__bpf_preempt_t *t)
{
	bpf_preempt_enable();
}
#define bpf_guard_preempt() \
	__bpf_preempt_t ___bpf_apply(preempt, __COUNTER__)			\
	__attribute__((__unused__, __cleanup__(__bpf_preempt_destructor))) =	\
	__bpf_preempt_constructor()

/* Description
 *	Assert that a conditional expression is true.
 * Returns
 *	Void.
 * Throws
 *	An exception with the value zero when the assertion fails.
 */
#define bpf_assert(cond) if (!(cond)) bpf_throw(0);

/* Description
 *	Assert that a conditional expression is true.
 * Returns
 *	Void.
 * Throws
 *	An exception with the specified value when the assertion fails.
 */
#define bpf_assert_with(cond, value) if (!(cond)) bpf_throw(value);

/* Description
 *	Assert that LHS is in the range [BEG, END] (inclusive of both). This
 *	statement updates the known bounds of LHS during verification. Note
 *	that both BEG and END must be constant values, and must fit within the
 *	data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the value zero when the assertion fails.
 */
#define bpf_assert_range(LHS, BEG, END)					\
	({								\
		_Static_assert(BEG <= END, "BEG must be <= END");	\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, >=, BEG, 0, false);		\
		__bpf_assert_op(LHS, <=, END, 0, false);		\
	})

/* Description
 *	Assert that LHS is in the range [BEG, END] (inclusive of both). This
 *	statement updates the known bounds of LHS during verification. Note
 *	that both BEG and END must be constant values, and must fit within the
 *	data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the specified value when the assertion fails.
 */
#define bpf_assert_range_with(LHS, BEG, END, value)			\
	({								\
		_Static_assert(BEG <= END, "BEG must be <= END");	\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, >=, BEG, value, false);		\
		__bpf_assert_op(LHS, <=, END, value, false);		\
	})

struct bpf_iter_css_task;
struct cgroup_subsys_state;
extern int bpf_iter_css_task_new(struct bpf_iter_css_task *it,
		struct cgroup_subsys_state *css, unsigned int flags) __weak __ksym;
extern struct task_struct *bpf_iter_css_task_next(struct bpf_iter_css_task *it) __weak __ksym;
extern void bpf_iter_css_task_destroy(struct bpf_iter_css_task *it) __weak __ksym;

struct bpf_iter_task;
extern int bpf_iter_task_new(struct bpf_iter_task *it,
		struct task_struct *task, unsigned int flags) __weak __ksym;
extern struct task_struct *bpf_iter_task_next(struct bpf_iter_task *it) __weak __ksym;
extern void bpf_iter_task_destroy(struct bpf_iter_task *it) __weak __ksym;

struct bpf_iter_css;
extern int bpf_iter_css_new(struct bpf_iter_css *it,
				struct cgroup_subsys_state *start, unsigned int flags) __weak __ksym;
extern struct cgroup_subsys_state *bpf_iter_css_next(struct bpf_iter_css *it) __weak __ksym;
extern void bpf_iter_css_destroy(struct bpf_iter_css *it) __weak __ksym;

extern int bpf_wq_init(struct bpf_wq *wq, void *p__map, unsigned int flags) __weak __ksym;
extern int bpf_wq_start(struct bpf_wq *wq, unsigned int flags) __weak __ksym;
extern int bpf_wq_set_callback_impl(struct bpf_wq *wq,
		int (callback_fn)(void *map, int *key, void *value),
		unsigned int flags__k, void *aux__ign) __ksym;
#define bpf_wq_set_callback(timer, cb, flags) \
	bpf_wq_set_callback_impl(timer, cb, flags, NULL)

struct bpf_iter_kmem_cache;
extern int bpf_iter_kmem_cache_new(struct bpf_iter_kmem_cache *it) __weak __ksym;
extern struct kmem_cache *bpf_iter_kmem_cache_next(struct bpf_iter_kmem_cache *it) __weak __ksym;
extern void bpf_iter_kmem_cache_destroy(struct bpf_iter_kmem_cache *it) __weak __ksym;

struct bpf_iter_dmabuf;
extern int bpf_iter_dmabuf_new(struct bpf_iter_dmabuf *it) __weak __ksym;
extern struct dma_buf *bpf_iter_dmabuf_next(struct bpf_iter_dmabuf *it) __weak __ksym;
extern void bpf_iter_dmabuf_destroy(struct bpf_iter_dmabuf *it) __weak __ksym;

extern int bpf_cgroup_read_xattr(struct cgroup *cgroup, const char *name__str,
				 struct bpf_dynptr *value_p) __weak __ksym;

#define PREEMPT_BITS	8
#define SOFTIRQ_BITS	8
#define HARDIRQ_BITS	4
#define NMI_BITS	4

#define PREEMPT_SHIFT	0
#define SOFTIRQ_SHIFT	(PREEMPT_SHIFT + PREEMPT_BITS)
#define HARDIRQ_SHIFT	(SOFTIRQ_SHIFT + SOFTIRQ_BITS)
#define NMI_SHIFT	(HARDIRQ_SHIFT + HARDIRQ_BITS)

#define __IRQ_MASK(x)	((1UL << (x))-1)

#define SOFTIRQ_MASK	(__IRQ_MASK(SOFTIRQ_BITS) << SOFTIRQ_SHIFT)
#define HARDIRQ_MASK	(__IRQ_MASK(HARDIRQ_BITS) << HARDIRQ_SHIFT)
#define NMI_MASK	(__IRQ_MASK(NMI_BITS)     << NMI_SHIFT)

extern bool CONFIG_PREEMPT_RT __kconfig __weak;
#ifdef bpf_target_x86
extern const int __preempt_count __ksym;
#endif

struct task_struct___preempt_rt {
	int softirq_disable_cnt;
} __attribute__((preserve_access_index));

static inline int get_preempt_count(void)
{
#if defined(bpf_target_x86)
	return *(int *) bpf_this_cpu_ptr(&__preempt_count);
#elif defined(bpf_target_arm64)
	return bpf_get_current_task_btf()->thread_info.preempt.count;
#endif
	return 0;
}

/* Description
 *	Report whether it is in interrupt context. Only works on the following archs:
 *	* x86
 *	* arm64
 */
static inline int bpf_in_interrupt(void)
{
	struct task_struct___preempt_rt *tsk;
	int pcnt;

	pcnt = get_preempt_count();
	if (!CONFIG_PREEMPT_RT)
		return pcnt & (NMI_MASK | HARDIRQ_MASK | SOFTIRQ_MASK);

	tsk = (void *) bpf_get_current_task_btf();
	return (pcnt & (NMI_MASK | HARDIRQ_MASK)) |
	       (tsk->softirq_disable_cnt & SOFTIRQ_MASK);
}

#endif
