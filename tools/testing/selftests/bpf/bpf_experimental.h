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
				 unsigned long addr) __ksym;
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
 *	Assert that LHS is equal to RHS. This statement updates the known value
 *	of LHS during verification. Note that RHS must be a constant value, and
 *	must fit within the data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the value zero when the assertion fails.
 */
#define bpf_assert_eq(LHS, RHS)						\
	({								\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, ==, RHS, 0, true);			\
	})

/* Description
 *	Assert that LHS is equal to RHS. This statement updates the known value
 *	of LHS during verification. Note that RHS must be a constant value, and
 *	must fit within the data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the specified value when the assertion fails.
 */
#define bpf_assert_eq_with(LHS, RHS, value)				\
	({								\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, ==, RHS, value, true);		\
	})

/* Description
 *	Assert that LHS is less than RHS. This statement updates the known
 *	bounds of LHS during verification. Note that RHS must be a constant
 *	value, and must fit within the data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the value zero when the assertion fails.
 */
#define bpf_assert_lt(LHS, RHS)						\
	({								\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, <, RHS, 0, false);			\
	})

/* Description
 *	Assert that LHS is less than RHS. This statement updates the known
 *	bounds of LHS during verification. Note that RHS must be a constant
 *	value, and must fit within the data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the specified value when the assertion fails.
 */
#define bpf_assert_lt_with(LHS, RHS, value)				\
	({								\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, <, RHS, value, false);		\
	})

/* Description
 *	Assert that LHS is greater than RHS. This statement updates the known
 *	bounds of LHS during verification. Note that RHS must be a constant
 *	value, and must fit within the data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the value zero when the assertion fails.
 */
#define bpf_assert_gt(LHS, RHS)						\
	({								\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, >, RHS, 0, false);			\
	})

/* Description
 *	Assert that LHS is greater than RHS. This statement updates the known
 *	bounds of LHS during verification. Note that RHS must be a constant
 *	value, and must fit within the data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the specified value when the assertion fails.
 */
#define bpf_assert_gt_with(LHS, RHS, value)				\
	({								\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, >, RHS, value, false);		\
	})

/* Description
 *	Assert that LHS is less than or equal to RHS. This statement updates the
 *	known bounds of LHS during verification. Note that RHS must be a
 *	constant value, and must fit within the data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the value zero when the assertion fails.
 */
#define bpf_assert_le(LHS, RHS)						\
	({								\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, <=, RHS, 0, false);		\
	})

/* Description
 *	Assert that LHS is less than or equal to RHS. This statement updates the
 *	known bounds of LHS during verification. Note that RHS must be a
 *	constant value, and must fit within the data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the specified value when the assertion fails.
 */
#define bpf_assert_le_with(LHS, RHS, value)				\
	({								\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, <=, RHS, value, false);		\
	})

/* Description
 *	Assert that LHS is greater than or equal to RHS. This statement updates
 *	the known bounds of LHS during verification. Note that RHS must be a
 *	constant value, and must fit within the data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the value zero when the assertion fails.
 */
#define bpf_assert_ge(LHS, RHS)						\
	({								\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, >=, RHS, 0, false);		\
	})

/* Description
 *	Assert that LHS is greater than or equal to RHS. This statement updates
 *	the known bounds of LHS during verification. Note that RHS must be a
 *	constant value, and must fit within the data type of LHS.
 * Returns
 *	Void.
 * Throws
 *	An exception with the specified value when the assertion fails.
 */
#define bpf_assert_ge_with(LHS, RHS, value)				\
	({								\
		barrier_var(LHS);					\
		__bpf_assert_op(LHS, >=, RHS, value, false);		\
	})

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

#endif
