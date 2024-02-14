// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"

#include "linked_list.h"

#define INIT                                                  \
	struct map_value *v, *v2, *iv, *iv2;                  \
	struct foo *f, *f1, *f2;                              \
	struct bar *b;                                        \
	void *map;                                            \
                                                              \
	map = bpf_map_lookup_elem(&map_of_maps, &(int){ 0 }); \
	if (!map)                                             \
		return 0;                                     \
	v = bpf_map_lookup_elem(&array_map, &(int){ 0 });     \
	if (!v)                                               \
		return 0;                                     \
	v2 = bpf_map_lookup_elem(&array_map, &(int){ 0 });    \
	if (!v2)                                              \
		return 0;                                     \
	iv = bpf_map_lookup_elem(map, &(int){ 0 });           \
	if (!iv)                                              \
		return 0;                                     \
	iv2 = bpf_map_lookup_elem(map, &(int){ 0 });          \
	if (!iv2)                                             \
		return 0;                                     \
	f = bpf_obj_new(typeof(*f));                          \
	if (!f)                                               \
		return 0;                                     \
	f1 = f;                                               \
	f2 = bpf_obj_new(typeof(*f2));                        \
	if (!f2) {                                            \
		bpf_obj_drop(f1);                             \
		return 0;                                     \
	}                                                     \
	b = bpf_obj_new(typeof(*b));                          \
	if (!b) {                                             \
		bpf_obj_drop(f2);                             \
		bpf_obj_drop(f1);                             \
		return 0;                                     \
	}

#define CHECK(test, op, hexpr)                              \
	SEC("?tc")                                          \
	int test##_missing_lock_##op(void *ctx)             \
	{                                                   \
		INIT;                                       \
		void (*p)(void *) = (void *)&bpf_list_##op; \
		p(hexpr);                                   \
		return 0;                                   \
	}

CHECK(kptr, pop_front, &f->head);
CHECK(kptr, pop_back, &f->head);

CHECK(global, pop_front, &ghead);
CHECK(global, pop_back, &ghead);

CHECK(map, pop_front, &v->head);
CHECK(map, pop_back, &v->head);

CHECK(inner_map, pop_front, &iv->head);
CHECK(inner_map, pop_back, &iv->head);

#undef CHECK

#define CHECK(test, op, hexpr, nexpr)					\
	SEC("?tc")							\
	int test##_missing_lock_##op(void *ctx)				\
	{								\
		INIT;							\
		bpf_list_##op(hexpr, nexpr);				\
		return 0;						\
	}

CHECK(kptr, push_front, &f->head, &b->node);
CHECK(kptr, push_back, &f->head, &b->node);

CHECK(global, push_front, &ghead, &f->node2);
CHECK(global, push_back, &ghead, &f->node2);

CHECK(map, push_front, &v->head, &f->node2);
CHECK(map, push_back, &v->head, &f->node2);

CHECK(inner_map, push_front, &iv->head, &f->node2);
CHECK(inner_map, push_back, &iv->head, &f->node2);

#undef CHECK

#define CHECK(test, op, lexpr, hexpr)                       \
	SEC("?tc")                                          \
	int test##_incorrect_lock_##op(void *ctx)           \
	{                                                   \
		INIT;                                       \
		void (*p)(void *) = (void *)&bpf_list_##op; \
		bpf_spin_lock(lexpr);                       \
		p(hexpr);                                   \
		return 0;                                   \
	}

#define CHECK_OP(op)                                           \
	CHECK(kptr_kptr, op, &f1->lock, &f2->head);            \
	CHECK(kptr_global, op, &f1->lock, &ghead);             \
	CHECK(kptr_map, op, &f1->lock, &v->head);              \
	CHECK(kptr_inner_map, op, &f1->lock, &iv->head);       \
                                                               \
	CHECK(global_global, op, &glock2, &ghead);             \
	CHECK(global_kptr, op, &glock, &f1->head);             \
	CHECK(global_map, op, &glock, &v->head);               \
	CHECK(global_inner_map, op, &glock, &iv->head);        \
                                                               \
	CHECK(map_map, op, &v->lock, &v2->head);               \
	CHECK(map_kptr, op, &v->lock, &f2->head);              \
	CHECK(map_global, op, &v->lock, &ghead);               \
	CHECK(map_inner_map, op, &v->lock, &iv->head);         \
                                                               \
	CHECK(inner_map_inner_map, op, &iv->lock, &iv2->head); \
	CHECK(inner_map_kptr, op, &iv->lock, &f2->head);       \
	CHECK(inner_map_global, op, &iv->lock, &ghead);        \
	CHECK(inner_map_map, op, &iv->lock, &v->head);

CHECK_OP(pop_front);
CHECK_OP(pop_back);

#undef CHECK
#undef CHECK_OP

#define CHECK(test, op, lexpr, hexpr, nexpr)				\
	SEC("?tc")							\
	int test##_incorrect_lock_##op(void *ctx)			\
	{								\
		INIT;							\
		bpf_spin_lock(lexpr);					\
		bpf_list_##op(hexpr, nexpr);				\
		return 0;						\
	}

#define CHECK_OP(op)							\
	CHECK(kptr_kptr, op, &f1->lock, &f2->head, &b->node);		\
	CHECK(kptr_global, op, &f1->lock, &ghead, &f->node2);		\
	CHECK(kptr_map, op, &f1->lock, &v->head, &f->node2);		\
	CHECK(kptr_inner_map, op, &f1->lock, &iv->head, &f->node2);	\
									\
	CHECK(global_global, op, &glock2, &ghead, &f->node2);		\
	CHECK(global_kptr, op, &glock, &f1->head, &b->node);		\
	CHECK(global_map, op, &glock, &v->head, &f->node2);		\
	CHECK(global_inner_map, op, &glock, &iv->head, &f->node2);	\
									\
	CHECK(map_map, op, &v->lock, &v2->head, &f->node2);		\
	CHECK(map_kptr, op, &v->lock, &f2->head, &b->node);		\
	CHECK(map_global, op, &v->lock, &ghead, &f->node2);		\
	CHECK(map_inner_map, op, &v->lock, &iv->head, &f->node2);	\
									\
	CHECK(inner_map_inner_map, op, &iv->lock, &iv2->head, &f->node2);\
	CHECK(inner_map_kptr, op, &iv->lock, &f2->head, &b->node);	\
	CHECK(inner_map_global, op, &iv->lock, &ghead, &f->node2);	\
	CHECK(inner_map_map, op, &iv->lock, &v->head, &f->node2);

CHECK_OP(push_front);
CHECK_OP(push_back);

#undef CHECK
#undef CHECK_OP
#undef INIT

SEC("?kprobe/xyz")
int map_compat_kprobe(void *ctx)
{
	bpf_list_push_front(&ghead, NULL);
	return 0;
}

SEC("?kretprobe/xyz")
int map_compat_kretprobe(void *ctx)
{
	bpf_list_push_front(&ghead, NULL);
	return 0;
}

SEC("?tracepoint/xyz")
int map_compat_tp(void *ctx)
{
	bpf_list_push_front(&ghead, NULL);
	return 0;
}

SEC("?perf_event")
int map_compat_perf(void *ctx)
{
	bpf_list_push_front(&ghead, NULL);
	return 0;
}

SEC("?raw_tp/xyz")
int map_compat_raw_tp(void *ctx)
{
	bpf_list_push_front(&ghead, NULL);
	return 0;
}

SEC("?raw_tp.w/xyz")
int map_compat_raw_tp_w(void *ctx)
{
	bpf_list_push_front(&ghead, NULL);
	return 0;
}

SEC("?tc")
int obj_type_id_oor(void *ctx)
{
	bpf_obj_new_impl(~0UL, NULL);
	return 0;
}

SEC("?tc")
int obj_new_no_composite(void *ctx)
{
	bpf_obj_new_impl(bpf_core_type_id_local(int), (void *)42);
	return 0;
}

SEC("?tc")
int obj_new_no_struct(void *ctx)
{

	bpf_obj_new(union { int data; unsigned udata; });
	return 0;
}

SEC("?tc")
int obj_drop_non_zero_off(void *ctx)
{
	void *f;

	f = bpf_obj_new(struct foo);
	if (!f)
		return 0;
	bpf_obj_drop(f+1);
	return 0;
}

SEC("?tc")
int new_null_ret(void *ctx)
{
	return bpf_obj_new(struct foo)->data;
}

SEC("?tc")
int obj_new_acq(void *ctx)
{
	bpf_obj_new(struct foo);
	return 0;
}

SEC("?tc")
int use_after_drop(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_obj_drop(f);
	return f->data;
}

SEC("?tc")
int ptr_walk_scalar(void *ctx)
{
	struct test1 {
		struct test2 {
			struct test2 *next;
		} *ptr;
	} *p;

	p = bpf_obj_new(typeof(*p));
	if (!p)
		return 0;
	bpf_this_cpu_ptr(p->ptr);
	return 0;
}

SEC("?tc")
int direct_read_lock(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	return *(int *)&f->lock;
}

SEC("?tc")
int direct_write_lock(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	*(int *)&f->lock = 0;
	return 0;
}

SEC("?tc")
int direct_read_head(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	return *(int *)&f->head;
}

SEC("?tc")
int direct_write_head(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	*(int *)&f->head = 0;
	return 0;
}

SEC("?tc")
int direct_read_node(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	return *(int *)&f->node2;
}

SEC("?tc")
int direct_write_node(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	*(int *)&f->node2 = 0;
	return 0;
}

static __always_inline
int use_after_unlock(bool push_front)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_spin_lock(&glock);
	f->data = 42;
	if (push_front)
		bpf_list_push_front(&ghead, &f->node2);
	else
		bpf_list_push_back(&ghead, &f->node2);
	bpf_spin_unlock(&glock);

	return f->data;
}

SEC("?tc")
int use_after_unlock_push_front(void *ctx)
{
	return use_after_unlock(true);
}

SEC("?tc")
int use_after_unlock_push_back(void *ctx)
{
	return use_after_unlock(false);
}

static __always_inline
int list_double_add(bool push_front)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_spin_lock(&glock);
	if (push_front) {
		bpf_list_push_front(&ghead, &f->node2);
		bpf_list_push_front(&ghead, &f->node2);
	} else {
		bpf_list_push_back(&ghead, &f->node2);
		bpf_list_push_back(&ghead, &f->node2);
	}
	bpf_spin_unlock(&glock);

	return 0;
}

SEC("?tc")
int double_push_front(void *ctx)
{
	return list_double_add(true);
}

SEC("?tc")
int double_push_back(void *ctx)
{
	return list_double_add(false);
}

SEC("?tc")
int no_node_value_type(void *ctx)
{
	void *p;

	p = bpf_obj_new(struct { int data; });
	if (!p)
		return 0;
	bpf_spin_lock(&glock);
	bpf_list_push_front(&ghead, p);
	bpf_spin_unlock(&glock);

	return 0;
}

SEC("?tc")
int incorrect_value_type(void *ctx)
{
	struct bar *b;

	b = bpf_obj_new(typeof(*b));
	if (!b)
		return 0;
	bpf_spin_lock(&glock);
	bpf_list_push_front(&ghead, &b->node);
	bpf_spin_unlock(&glock);

	return 0;
}

SEC("?tc")
int incorrect_node_var_off(struct __sk_buff *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_spin_lock(&glock);
	bpf_list_push_front(&ghead, (void *)&f->node2 + ctx->protocol);
	bpf_spin_unlock(&glock);

	return 0;
}

SEC("?tc")
int incorrect_node_off1(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_spin_lock(&glock);
	bpf_list_push_front(&ghead, (void *)&f->node2 + 1);
	bpf_spin_unlock(&glock);

	return 0;
}

SEC("?tc")
int incorrect_node_off2(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_spin_lock(&glock);
	bpf_list_push_front(&ghead, &f->node);
	bpf_spin_unlock(&glock);

	return 0;
}

SEC("?tc")
int no_head_type(void *ctx)
{
	void *p;

	p = bpf_obj_new(typeof(struct { int data; }));
	if (!p)
		return 0;
	bpf_spin_lock(&glock);
	bpf_list_push_front(p, NULL);
	bpf_spin_lock(&glock);

	return 0;
}

SEC("?tc")
int incorrect_head_var_off1(struct __sk_buff *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_spin_lock(&glock);
	bpf_list_push_front((void *)&ghead + ctx->protocol, &f->node2);
	bpf_spin_unlock(&glock);

	return 0;
}

SEC("?tc")
int incorrect_head_var_off2(struct __sk_buff *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	bpf_spin_lock(&glock);
	bpf_list_push_front((void *)&f->head + ctx->protocol, &f->node2);
	bpf_spin_unlock(&glock);

	return 0;
}

SEC("?tc")
int incorrect_head_off1(void *ctx)
{
	struct foo *f;
	struct bar *b;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;
	b = bpf_obj_new(typeof(*b));
	if (!b) {
		bpf_obj_drop(f);
		return 0;
	}

	bpf_spin_lock(&f->lock);
	bpf_list_push_front((void *)&f->head + 1, &b->node);
	bpf_spin_unlock(&f->lock);

	return 0;
}

SEC("?tc")
int incorrect_head_off2(void *ctx)
{
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 0;

	bpf_spin_lock(&glock);
	bpf_list_push_front((void *)&ghead + 1, &f->node2);
	bpf_spin_unlock(&glock);

	return 0;
}

static __always_inline
int pop_ptr_off(void *(*op)(void *head))
{
	struct {
		struct bpf_list_head head __contains(foo, node2);
		struct bpf_spin_lock lock;
	} *p;
	struct bpf_list_node *n;

	p = bpf_obj_new(typeof(*p));
	if (!p)
		return 0;
	bpf_spin_lock(&p->lock);
	n = op(&p->head);
	bpf_spin_unlock(&p->lock);

	if (!n)
		return 0;
	bpf_spin_lock((void *)n);
	return 0;
}

SEC("?tc")
int pop_front_off(void *ctx)
{
	return pop_ptr_off((void *)bpf_list_pop_front);
}

SEC("?tc")
int pop_back_off(void *ctx)
{
	return pop_ptr_off((void *)bpf_list_pop_back);
}

char _license[] SEC("license") = "GPL";
