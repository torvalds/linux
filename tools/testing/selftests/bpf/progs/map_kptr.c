// SPDX-License-Identifier: GPL-2.0
#include <vmlinux.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_helpers.h>
#include "../bpf_testmod/bpf_testmod_kfunc.h"

struct map_value {
	struct prog_test_ref_kfunc __kptr_untrusted *unref_ptr;
	struct prog_test_ref_kfunc __kptr *ref_ptr;
};

struct array_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} array_map SEC(".maps");

struct pcpu_array_map {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} pcpu_array_map SEC(".maps");

struct hash_map {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} hash_map SEC(".maps");

struct pcpu_hash_map {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} pcpu_hash_map SEC(".maps");

struct hash_malloc_map {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
	__uint(map_flags, BPF_F_NO_PREALLOC);
} hash_malloc_map SEC(".maps");

struct pcpu_hash_malloc_map {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
	__uint(map_flags, BPF_F_NO_PREALLOC);
} pcpu_hash_malloc_map SEC(".maps");

struct lru_hash_map {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} lru_hash_map SEC(".maps");

struct lru_pcpu_hash_map {
	__uint(type, BPF_MAP_TYPE_LRU_PERCPU_HASH);
	__type(key, int);
	__type(value, struct map_value);
	__uint(max_entries, 1);
} lru_pcpu_hash_map SEC(".maps");

struct cgrp_ls_map {
	__uint(type, BPF_MAP_TYPE_CGRP_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct map_value);
} cgrp_ls_map SEC(".maps");

struct task_ls_map {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct map_value);
} task_ls_map SEC(".maps");

struct inode_ls_map {
	__uint(type, BPF_MAP_TYPE_INODE_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct map_value);
} inode_ls_map SEC(".maps");

struct sk_ls_map {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct map_value);
} sk_ls_map SEC(".maps");

#define DEFINE_MAP_OF_MAP(map_type, inner_map_type, name)       \
	struct {                                                \
		__uint(type, map_type);                         \
		__uint(max_entries, 1);                         \
		__uint(key_size, sizeof(int));                  \
		__uint(value_size, sizeof(int));                \
		__array(values, struct inner_map_type);         \
	} name SEC(".maps") = {                                 \
		.values = { [0] = &inner_map_type },            \
	}

DEFINE_MAP_OF_MAP(BPF_MAP_TYPE_ARRAY_OF_MAPS, array_map, array_of_array_maps);
DEFINE_MAP_OF_MAP(BPF_MAP_TYPE_ARRAY_OF_MAPS, hash_map, array_of_hash_maps);
DEFINE_MAP_OF_MAP(BPF_MAP_TYPE_ARRAY_OF_MAPS, hash_malloc_map, array_of_hash_malloc_maps);
DEFINE_MAP_OF_MAP(BPF_MAP_TYPE_ARRAY_OF_MAPS, lru_hash_map, array_of_lru_hash_maps);
DEFINE_MAP_OF_MAP(BPF_MAP_TYPE_HASH_OF_MAPS, array_map, hash_of_array_maps);
DEFINE_MAP_OF_MAP(BPF_MAP_TYPE_HASH_OF_MAPS, hash_map, hash_of_hash_maps);
DEFINE_MAP_OF_MAP(BPF_MAP_TYPE_HASH_OF_MAPS, hash_malloc_map, hash_of_hash_malloc_maps);
DEFINE_MAP_OF_MAP(BPF_MAP_TYPE_HASH_OF_MAPS, lru_hash_map, hash_of_lru_hash_maps);

#define WRITE_ONCE(x, val) ((*(volatile typeof(x) *) &(x)) = (val))

static void test_kptr_unref(struct map_value *v)
{
	struct prog_test_ref_kfunc *p;

	p = v->unref_ptr;
	/* store untrusted_ptr_or_null_ */
	WRITE_ONCE(v->unref_ptr, p);
	if (!p)
		return;
	if (p->a + p->b > 100)
		return;
	/* store untrusted_ptr_ */
	WRITE_ONCE(v->unref_ptr, p);
	/* store NULL */
	WRITE_ONCE(v->unref_ptr, NULL);
}

static void test_kptr_ref(struct map_value *v)
{
	struct prog_test_ref_kfunc *p;

	p = v->ref_ptr;
	/* store ptr_or_null_ */
	WRITE_ONCE(v->unref_ptr, p);
	if (!p)
		return;
	/*
	 * p is rcu_ptr_prog_test_ref_kfunc,
	 * because bpf prog is non-sleepable and runs in RCU CS.
	 * p can be passed to kfunc that requires KF_RCU.
	 */
	bpf_kfunc_call_test_ref(p);
	if (p->a + p->b > 100)
		return;
	/* store NULL */
	p = bpf_kptr_xchg(&v->ref_ptr, NULL);
	if (!p)
		return;
	/*
	 * p is trusted_ptr_prog_test_ref_kfunc.
	 * p can be passed to kfunc that requires KF_RCU.
	 */
	bpf_kfunc_call_test_ref(p);
	if (p->a + p->b > 100) {
		bpf_kfunc_call_test_release(p);
		return;
	}
	/* store ptr_ */
	WRITE_ONCE(v->unref_ptr, p);
	bpf_kfunc_call_test_release(p);

	p = bpf_kfunc_call_test_acquire(&(unsigned long){0});
	if (!p)
		return;
	/* store ptr_ */
	p = bpf_kptr_xchg(&v->ref_ptr, p);
	if (!p)
		return;
	if (p->a + p->b > 100) {
		bpf_kfunc_call_test_release(p);
		return;
	}
	bpf_kfunc_call_test_release(p);
}

static void test_kptr(struct map_value *v)
{
	test_kptr_unref(v);
	test_kptr_ref(v);
}

SEC("tc")
int test_map_kptr(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;

#define TEST(map)					\
	v = bpf_map_lookup_elem(&map, &key);		\
	if (!v)						\
		return 0;				\
	test_kptr(v)

	TEST(array_map);
	TEST(hash_map);
	TEST(hash_malloc_map);
	TEST(lru_hash_map);

#undef TEST
	return 0;
}

SEC("tp_btf/cgroup_mkdir")
int BPF_PROG(test_cgrp_map_kptr, struct cgroup *cgrp, const char *path)
{
	struct map_value *v;

	v = bpf_cgrp_storage_get(&cgrp_ls_map, cgrp, NULL, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (v)
		test_kptr(v);
	return 0;
}

SEC("lsm/inode_unlink")
int BPF_PROG(test_task_map_kptr, struct inode *inode, struct dentry *victim)
{
	struct task_struct *task;
	struct map_value *v;

	task = bpf_get_current_task_btf();
	if (!task)
		return 0;
	v = bpf_task_storage_get(&task_ls_map, task, NULL, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (v)
		test_kptr(v);
	return 0;
}

SEC("lsm/inode_unlink")
int BPF_PROG(test_inode_map_kptr, struct inode *inode, struct dentry *victim)
{
	struct map_value *v;

	v = bpf_inode_storage_get(&inode_ls_map, inode, NULL, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (v)
		test_kptr(v);
	return 0;
}

SEC("tc")
int test_sk_map_kptr(struct __sk_buff *ctx)
{
	struct map_value *v;
	struct bpf_sock *sk;

	sk = ctx->sk;
	if (!sk)
		return 0;
	v = bpf_sk_storage_get(&sk_ls_map, sk, NULL, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (v)
		test_kptr(v);
	return 0;
}

SEC("tc")
int test_map_in_map_kptr(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0;
	void *map;

#define TEST(map_in_map)                                \
	map = bpf_map_lookup_elem(&map_in_map, &key);   \
	if (!map)                                       \
		return 0;                               \
	v = bpf_map_lookup_elem(map, &key);		\
	if (!v)						\
		return 0;				\
	test_kptr(v)

	TEST(array_of_array_maps);
	TEST(array_of_hash_maps);
	TEST(array_of_hash_malloc_maps);
	TEST(array_of_lru_hash_maps);
	TEST(hash_of_array_maps);
	TEST(hash_of_hash_maps);
	TEST(hash_of_hash_malloc_maps);
	TEST(hash_of_lru_hash_maps);

#undef TEST
	return 0;
}

int ref = 1;

static __always_inline
int test_map_kptr_ref_pre(struct map_value *v)
{
	struct prog_test_ref_kfunc *p, *p_st;
	unsigned long arg = 0;
	int ret;

	p = bpf_kfunc_call_test_acquire(&arg);
	if (!p)
		return 1;
	ref++;

	p_st = p->next;
	if (p_st->cnt.refs.counter != ref) {
		ret = 2;
		goto end;
	}

	p = bpf_kptr_xchg(&v->ref_ptr, p);
	if (p) {
		ret = 3;
		goto end;
	}
	if (p_st->cnt.refs.counter != ref)
		return 4;

	p = bpf_kptr_xchg(&v->ref_ptr, NULL);
	if (!p)
		return 5;
	bpf_kfunc_call_test_release(p);
	ref--;
	if (p_st->cnt.refs.counter != ref)
		return 6;

	p = bpf_kfunc_call_test_acquire(&arg);
	if (!p)
		return 7;
	ref++;
	p = bpf_kptr_xchg(&v->ref_ptr, p);
	if (p) {
		ret = 8;
		goto end;
	}
	if (p_st->cnt.refs.counter != ref)
		return 9;
	/* Leave in map */

	return 0;
end:
	ref--;
	bpf_kfunc_call_test_release(p);
	return ret;
}

static __always_inline
int test_map_kptr_ref_post(struct map_value *v)
{
	struct prog_test_ref_kfunc *p, *p_st;

	p_st = v->ref_ptr;
	if (!p_st || p_st->cnt.refs.counter != ref)
		return 1;

	p = bpf_kptr_xchg(&v->ref_ptr, NULL);
	if (!p)
		return 2;
	if (p_st->cnt.refs.counter != ref) {
		bpf_kfunc_call_test_release(p);
		return 3;
	}

	p = bpf_kptr_xchg(&v->ref_ptr, p);
	if (p) {
		bpf_kfunc_call_test_release(p);
		return 4;
	}
	if (p_st->cnt.refs.counter != ref)
		return 5;

	return 0;
}

#define TEST(map)                            \
	v = bpf_map_lookup_elem(&map, &key); \
	if (!v)                              \
		return -1;                   \
	ret = test_map_kptr_ref_pre(v);      \
	if (ret)                             \
		return ret;

#define TEST_PCPU(map)                                 \
	v = bpf_map_lookup_percpu_elem(&map, &key, 0); \
	if (!v)                                        \
		return -1;                             \
	ret = test_map_kptr_ref_pre(v);                \
	if (ret)                                       \
		return ret;

SEC("tc")
int test_map_kptr_ref1(struct __sk_buff *ctx)
{
	struct map_value *v, val = {};
	int key = 0, ret;

	bpf_map_update_elem(&hash_map, &key, &val, 0);
	bpf_map_update_elem(&hash_malloc_map, &key, &val, 0);
	bpf_map_update_elem(&lru_hash_map, &key, &val, 0);

	bpf_map_update_elem(&pcpu_hash_map, &key, &val, 0);
	bpf_map_update_elem(&pcpu_hash_malloc_map, &key, &val, 0);
	bpf_map_update_elem(&lru_pcpu_hash_map, &key, &val, 0);

	TEST(array_map);
	TEST(hash_map);
	TEST(hash_malloc_map);
	TEST(lru_hash_map);

	TEST_PCPU(pcpu_array_map);
	TEST_PCPU(pcpu_hash_map);
	TEST_PCPU(pcpu_hash_malloc_map);
	TEST_PCPU(lru_pcpu_hash_map);

	return 0;
}

#undef TEST
#undef TEST_PCPU

#define TEST(map)                            \
	v = bpf_map_lookup_elem(&map, &key); \
	if (!v)                              \
		return -1;                   \
	ret = test_map_kptr_ref_post(v);     \
	if (ret)                             \
		return ret;

#define TEST_PCPU(map)                                 \
	v = bpf_map_lookup_percpu_elem(&map, &key, 0); \
	if (!v)                                        \
		return -1;                             \
	ret = test_map_kptr_ref_post(v);               \
	if (ret)                                       \
		return ret;

SEC("tc")
int test_map_kptr_ref2(struct __sk_buff *ctx)
{
	struct map_value *v;
	int key = 0, ret;

	TEST(array_map);
	TEST(hash_map);
	TEST(hash_malloc_map);
	TEST(lru_hash_map);

	TEST_PCPU(pcpu_array_map);
	TEST_PCPU(pcpu_hash_map);
	TEST_PCPU(pcpu_hash_malloc_map);
	TEST_PCPU(lru_pcpu_hash_map);

	return 0;
}

#undef TEST
#undef TEST_PCPU

SEC("tc")
int test_map_kptr_ref3(struct __sk_buff *ctx)
{
	struct prog_test_ref_kfunc *p;
	unsigned long sp = 0;

	p = bpf_kfunc_call_test_acquire(&sp);
	if (!p)
		return 1;
	ref++;
	if (p->cnt.refs.counter != ref) {
		bpf_kfunc_call_test_release(p);
		return 2;
	}
	bpf_kfunc_call_test_release(p);
	ref--;
	return 0;
}

SEC("syscall")
int test_ls_map_kptr_ref1(void *ctx)
{
	struct task_struct *current;
	struct map_value *v;

	current = bpf_get_current_task_btf();
	if (!current)
		return 100;
	v = bpf_task_storage_get(&task_ls_map, current, NULL, 0);
	if (v)
		return 150;
	v = bpf_task_storage_get(&task_ls_map, current, NULL, BPF_LOCAL_STORAGE_GET_F_CREATE);
	if (!v)
		return 200;
	return test_map_kptr_ref_pre(v);
}

SEC("syscall")
int test_ls_map_kptr_ref2(void *ctx)
{
	struct task_struct *current;
	struct map_value *v;

	current = bpf_get_current_task_btf();
	if (!current)
		return 100;
	v = bpf_task_storage_get(&task_ls_map, current, NULL, 0);
	if (!v)
		return 200;
	return test_map_kptr_ref_post(v);
}

SEC("syscall")
int test_ls_map_kptr_ref_del(void *ctx)
{
	struct task_struct *current;
	struct map_value *v;

	current = bpf_get_current_task_btf();
	if (!current)
		return 100;
	v = bpf_task_storage_get(&task_ls_map, current, NULL, 0);
	if (!v)
		return 200;
	if (!v->ref_ptr)
		return 300;
	return bpf_task_storage_delete(&task_ls_map, current);
}

char _license[] SEC("license") = "GPL";
