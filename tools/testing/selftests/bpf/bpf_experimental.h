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

/* Convenience macro to wrap over bpf_obj_drop_impl */
#define bpf_percpu_obj_drop(kptr) bpf_percpu_obj_drop_impl(kptr, NULL)

#endif
