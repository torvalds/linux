// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_core_read.h>
#include "bpf_experimental.h"
#include "bpf_misc.h"

#include "linked_list.h"

struct head_nested_inner {
	struct bpf_spin_lock lock;
	struct bpf_list_head head __contains(foo, node2);
};

struct head_nested {
	int dummy;
	struct head_nested_inner inner;
};

private(C) struct bpf_spin_lock glock_c;
private(C) struct bpf_list_head ghead_array[2] __contains(foo, node2);
private(C) struct bpf_list_head ghead_array_one[1] __contains(foo, node2);

private(D) struct head_nested ghead_nested;

static __always_inline
int list_push_pop(struct bpf_spin_lock *lock, struct bpf_list_head *head, bool leave_in_map)
{
	struct bpf_list_node *n;
	struct foo *f;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 2;

	bpf_spin_lock(lock);
	n = bpf_list_pop_front(head);
	bpf_spin_unlock(lock);
	if (n) {
		bpf_obj_drop(container_of(n, struct foo, node2));
		bpf_obj_drop(f);
		return 3;
	}

	bpf_spin_lock(lock);
	n = bpf_list_pop_back(head);
	bpf_spin_unlock(lock);
	if (n) {
		bpf_obj_drop(container_of(n, struct foo, node2));
		bpf_obj_drop(f);
		return 4;
	}


	bpf_spin_lock(lock);
	f->data = 42;
	bpf_list_push_front(head, &f->node2);
	bpf_spin_unlock(lock);
	if (leave_in_map)
		return 0;
	bpf_spin_lock(lock);
	n = bpf_list_pop_back(head);
	bpf_spin_unlock(lock);
	if (!n)
		return 5;
	f = container_of(n, struct foo, node2);
	if (f->data != 42) {
		bpf_obj_drop(f);
		return 6;
	}

	bpf_spin_lock(lock);
	f->data = 13;
	bpf_list_push_front(head, &f->node2);
	bpf_spin_unlock(lock);
	bpf_spin_lock(lock);
	n = bpf_list_pop_front(head);
	bpf_spin_unlock(lock);
	if (!n)
		return 7;
	f = container_of(n, struct foo, node2);
	if (f->data != 13) {
		bpf_obj_drop(f);
		return 8;
	}
	bpf_obj_drop(f);

	bpf_spin_lock(lock);
	n = bpf_list_pop_front(head);
	bpf_spin_unlock(lock);
	if (n) {
		bpf_obj_drop(container_of(n, struct foo, node2));
		return 9;
	}

	bpf_spin_lock(lock);
	n = bpf_list_pop_back(head);
	bpf_spin_unlock(lock);
	if (n) {
		bpf_obj_drop(container_of(n, struct foo, node2));
		return 10;
	}
	return 0;
}


static __always_inline
int list_push_pop_multiple(struct bpf_spin_lock *lock, struct bpf_list_head *head, bool leave_in_map)
{
	struct bpf_list_node *n;
	struct foo *f[200], *pf;
	int i;

	/* Loop following this check adds nodes 2-at-a-time in order to
	 * validate multiple release_on_unlock release logic
	 */
	if (ARRAY_SIZE(f) % 2)
		return 10;

	for (i = 0; i < ARRAY_SIZE(f); i += 2) {
		f[i] = bpf_obj_new(typeof(**f));
		if (!f[i])
			return 2;
		f[i]->data = i;

		f[i + 1] = bpf_obj_new(typeof(**f));
		if (!f[i + 1]) {
			bpf_obj_drop(f[i]);
			return 9;
		}
		f[i + 1]->data = i + 1;

		bpf_spin_lock(lock);
		bpf_list_push_front(head, &f[i]->node2);
		bpf_list_push_front(head, &f[i + 1]->node2);
		bpf_spin_unlock(lock);
	}

	for (i = 0; i < ARRAY_SIZE(f); i++) {
		bpf_spin_lock(lock);
		n = bpf_list_pop_front(head);
		bpf_spin_unlock(lock);
		if (!n)
			return 3;
		pf = container_of(n, struct foo, node2);
		if (pf->data != (ARRAY_SIZE(f) - i - 1)) {
			bpf_obj_drop(pf);
			return 4;
		}
		bpf_spin_lock(lock);
		bpf_list_push_back(head, &pf->node2);
		bpf_spin_unlock(lock);
	}

	if (leave_in_map)
		return 0;

	for (i = 0; i < ARRAY_SIZE(f); i++) {
		bpf_spin_lock(lock);
		n = bpf_list_pop_back(head);
		bpf_spin_unlock(lock);
		if (!n)
			return 5;
		pf = container_of(n, struct foo, node2);
		if (pf->data != i) {
			bpf_obj_drop(pf);
			return 6;
		}
		bpf_obj_drop(pf);
	}
	bpf_spin_lock(lock);
	n = bpf_list_pop_back(head);
	bpf_spin_unlock(lock);
	if (n) {
		bpf_obj_drop(container_of(n, struct foo, node2));
		return 7;
	}

	bpf_spin_lock(lock);
	n = bpf_list_pop_front(head);
	bpf_spin_unlock(lock);
	if (n) {
		bpf_obj_drop(container_of(n, struct foo, node2));
		return 8;
	}
	return 0;
}

static __always_inline
int list_in_list(struct bpf_spin_lock *lock, struct bpf_list_head *head, bool leave_in_map)
{
	struct bpf_list_node *n;
	struct bar *ba[8], *b;
	struct foo *f;
	int i;

	f = bpf_obj_new(typeof(*f));
	if (!f)
		return 2;
	for (i = 0; i < ARRAY_SIZE(ba); i++) {
		b = bpf_obj_new(typeof(*b));
		if (!b) {
			bpf_obj_drop(f);
			return 3;
		}
		b->data = i;
		bpf_spin_lock(&f->lock);
		bpf_list_push_back(&f->head, &b->node);
		bpf_spin_unlock(&f->lock);
	}

	bpf_spin_lock(lock);
	f->data = 42;
	bpf_list_push_front(head, &f->node2);
	bpf_spin_unlock(lock);

	if (leave_in_map)
		return 0;

	bpf_spin_lock(lock);
	n = bpf_list_pop_front(head);
	bpf_spin_unlock(lock);
	if (!n)
		return 4;
	f = container_of(n, struct foo, node2);
	if (f->data != 42) {
		bpf_obj_drop(f);
		return 5;
	}

	for (i = 0; i < ARRAY_SIZE(ba); i++) {
		bpf_spin_lock(&f->lock);
		n = bpf_list_pop_front(&f->head);
		bpf_spin_unlock(&f->lock);
		if (!n) {
			bpf_obj_drop(f);
			return 6;
		}
		b = container_of(n, struct bar, node);
		if (b->data != i) {
			bpf_obj_drop(f);
			bpf_obj_drop(b);
			return 7;
		}
		bpf_obj_drop(b);
	}
	bpf_spin_lock(&f->lock);
	n = bpf_list_pop_front(&f->head);
	bpf_spin_unlock(&f->lock);
	if (n) {
		bpf_obj_drop(f);
		bpf_obj_drop(container_of(n, struct bar, node));
		return 8;
	}
	bpf_obj_drop(f);
	return 0;
}

static __always_inline
int test_list_push_pop(struct bpf_spin_lock *lock, struct bpf_list_head *head)
{
	int ret;

	ret = list_push_pop(lock, head, false);
	if (ret)
		return ret;
	return list_push_pop(lock, head, true);
}

static __always_inline
int test_list_push_pop_multiple(struct bpf_spin_lock *lock, struct bpf_list_head *head)
{
	int ret;

	ret = list_push_pop_multiple(lock, head, false);
	if (ret)
		return ret;
	return list_push_pop_multiple(lock, head, true);
}

static __always_inline
int test_list_in_list(struct bpf_spin_lock *lock, struct bpf_list_head *head)
{
	int ret;

	ret = list_in_list(lock, head, false);
	if (ret)
		return ret;
	return list_in_list(lock, head, true);
}

SEC("tc")
int map_list_push_pop(void *ctx)
{
	struct map_value *v;

	v = bpf_map_lookup_elem(&array_map, &(int){0});
	if (!v)
		return 1;
	return test_list_push_pop(&v->lock, &v->head);
}

SEC("tc")
int inner_map_list_push_pop(void *ctx)
{
	struct map_value *v;
	void *map;

	map = bpf_map_lookup_elem(&map_of_maps, &(int){0});
	if (!map)
		return 1;
	v = bpf_map_lookup_elem(map, &(int){0});
	if (!v)
		return 1;
	return test_list_push_pop(&v->lock, &v->head);
}

SEC("tc")
int global_list_push_pop(void *ctx)
{
	return test_list_push_pop(&glock, &ghead);
}

SEC("tc")
int global_list_push_pop_nested(void *ctx)
{
	return test_list_push_pop(&ghead_nested.inner.lock, &ghead_nested.inner.head);
}

SEC("tc")
int global_list_array_push_pop(void *ctx)
{
	int r;

	r = test_list_push_pop(&glock_c, &ghead_array[0]);
	if (r)
		return r;

	r = test_list_push_pop(&glock_c, &ghead_array[1]);
	if (r)
		return r;

	/* Arrays with only one element is a special case, being treated
	 * just like a bpf_list_head variable by the verifier, not an
	 * array.
	 */
	return test_list_push_pop(&glock_c, &ghead_array_one[0]);
}

SEC("tc")
int map_list_push_pop_multiple(void *ctx)
{
	struct map_value *v;

	v = bpf_map_lookup_elem(&array_map, &(int){0});
	if (!v)
		return 1;
	return test_list_push_pop_multiple(&v->lock, &v->head);
}

SEC("tc")
int inner_map_list_push_pop_multiple(void *ctx)
{
	struct map_value *v;
	void *map;

	map = bpf_map_lookup_elem(&map_of_maps, &(int){0});
	if (!map)
		return 1;
	v = bpf_map_lookup_elem(map, &(int){0});
	if (!v)
		return 1;
	return test_list_push_pop_multiple(&v->lock, &v->head);
}

SEC("tc")
int global_list_push_pop_multiple(void *ctx)
{
	int ret;

	ret = list_push_pop_multiple(&glock, &ghead, false);
	if (ret)
		return ret;
	return list_push_pop_multiple(&glock, &ghead, true);
}

SEC("tc")
int map_list_in_list(void *ctx)
{
	struct map_value *v;

	v = bpf_map_lookup_elem(&array_map, &(int){0});
	if (!v)
		return 1;
	return test_list_in_list(&v->lock, &v->head);
}

SEC("tc")
int inner_map_list_in_list(void *ctx)
{
	struct map_value *v;
	void *map;

	map = bpf_map_lookup_elem(&map_of_maps, &(int){0});
	if (!map)
		return 1;
	v = bpf_map_lookup_elem(map, &(int){0});
	if (!v)
		return 1;
	return test_list_in_list(&v->lock, &v->head);
}

SEC("tc")
int global_list_in_list(void *ctx)
{
	return test_list_in_list(&glock, &ghead);
}

char _license[] SEC("license") = "GPL";
