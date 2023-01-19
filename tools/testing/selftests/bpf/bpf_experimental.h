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
 *	The 'meta' parameter is a hidden argument that is ignored.
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
 *	The 'meta' parameter is a hidden argument that is ignored.
 * Returns
 *	Void.
 */
extern void bpf_obj_drop_impl(void *kptr, void *meta) __ksym;

/* Convenience macro to wrap over bpf_obj_drop_impl */
#define bpf_obj_drop(kptr) bpf_obj_drop_impl(kptr, NULL)

/* Description
 *	Add a new entry to the beginning of the BPF linked list.
 * Returns
 *	Void.
 */
extern void bpf_list_push_front(struct bpf_list_head *head, struct bpf_list_node *node) __ksym;

/* Description
 *	Add a new entry to the end of the BPF linked list.
 * Returns
 *	Void.
 */
extern void bpf_list_push_back(struct bpf_list_head *head, struct bpf_list_node *node) __ksym;

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

#endif
