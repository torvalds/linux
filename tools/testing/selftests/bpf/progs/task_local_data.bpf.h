/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TASK_LOCAL_DATA_BPF_H
#define __TASK_LOCAL_DATA_BPF_H

/*
 * Task local data is a library that facilitates sharing per-task data
 * between user space and bpf programs.
 *
 *
 * USAGE
 *
 * A TLD, an entry of data in task local data, first needs to be created by the
 * user space. This is done by calling user space API, TLD_DEFINE_KEY() or
 * tld_create_key(), with the name of the TLD and the size.
 *
 * TLD_DEFINE_KEY(prio, "priority", sizeof(int));
 *
 * or
 *
 * void func_call(...) {
 *     tld_key_t prio, in_cs;
 *
 *     prio = tld_create_key("priority", sizeof(int));
 *     in_cs = tld_create_key("in_critical_section", sizeof(bool));
 *     ...
 *
 * A key associated with the TLD, which has an opaque type tld_key_t, will be
 * initialized or returned. It can be used to get a pointer to the TLD in the
 * user space by calling tld_get_data().
 *
 * In a bpf program, tld_object_init() first needs to be called to initialized a
 * tld_object on the stack. Then, TLDs can be accessed by calling tld_get_data().
 * The API will try to fetch the key by the name and use it to locate the data.
 * A pointer to the TLD will be returned. It also caches the key in a task local
 * storage map, tld_key_map, whose value type, struct tld_keys, must be defined
 * by the developer.
 *
 * struct tld_keys {
 *     tld_key_t prio;
 *     tld_key_t in_cs;
 * };
 *
 * SEC("struct_ops")
 * void prog(struct task_struct task, ...)
 * {
 *     struct tld_object tld_obj;
 *     int err, *p;
 *
 *     err = tld_object_init(task, &tld_obj);
 *     if (err)
 *         return;
 *
 *     p = tld_get_data(&tld_obj, prio, "priority", sizeof(int));
 *     if (p)
 *         // do something depending on *p
 */
#include <errno.h>
#include <bpf/bpf_helpers.h>

#define TLD_ROUND_MASK(x, y) ((__typeof__(x))((y) - 1))
#define TLD_ROUND_UP(x, y) ((((x) - 1) | TLD_ROUND_MASK(x, y)) + 1)

#define TLD_MAX_DATA_CNT (__PAGE_SIZE / sizeof(struct tld_metadata) - 1)

#ifndef TLD_NAME_LEN
#define TLD_NAME_LEN 62
#endif

#ifndef TLD_KEY_MAP_CREATE_RETRY
#define TLD_KEY_MAP_CREATE_RETRY 10
#endif

typedef struct {
	__s16 off;
} tld_key_t;

struct tld_metadata {
	char name[TLD_NAME_LEN];
	__u16 size;
};

struct tld_meta_u {
	__u8 cnt;
	__u16 size;
	struct tld_metadata metadata[TLD_MAX_DATA_CNT];
};

struct tld_data_u {
	__u64 start; /* offset of tld_data_u->data in a page */
	char data[__PAGE_SIZE - sizeof(__u64)];
};

struct tld_map_value {
	struct tld_data_u __uptr *data;
	struct tld_meta_u __uptr *meta;
};

typedef struct tld_uptr_dummy {
	struct tld_data_u data[0];
	struct tld_meta_u meta[0];
} *tld_uptr_dummy_t;

struct tld_object {
	struct tld_map_value *data_map;
	struct tld_keys *key_map;
	/*
	 * Force the compiler to generate the actual definition of tld_meta_u
	 * and tld_data_u in BTF. Without it, tld_meta_u and u_tld_data will
	 * be BTF_KIND_FWD.
	 */
	tld_uptr_dummy_t dummy[0];
};

/*
 * Map value of tld_key_map for caching keys. Must be defined by the developer.
 * Members should be tld_key_t and passed to the 3rd argument of tld_fetch_key().
 */
struct tld_keys;

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct tld_map_value);
} tld_data_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_TASK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, int);
	__type(value, struct tld_keys);
} tld_key_map SEC(".maps");

/**
 * tld_object_init() - Initialize a tld_object.
 *
 * @task: The task_struct of the target task
 * @tld_obj: A pointer to a tld_object to be initialized
 *
 * Return 0 on success; -ENODATA if the user space did not initialize task local data
 * for the current task through tld_get_data(); -ENOMEM if the creation of tld_key_map
 * fails
 */
__attribute__((unused))
static int tld_object_init(struct task_struct *task, struct tld_object *tld_obj)
{
	int i;

	tld_obj->data_map = bpf_task_storage_get(&tld_data_map, task, 0, 0);
	if (!tld_obj->data_map)
		return -ENODATA;

	bpf_for(i, 0, TLD_KEY_MAP_CREATE_RETRY) {
		tld_obj->key_map = bpf_task_storage_get(&tld_key_map, task, 0,
							BPF_LOCAL_STORAGE_GET_F_CREATE);
		if (likely(tld_obj->key_map))
			break;
	}
	if (!tld_obj->key_map)
		return -ENOMEM;

	return 0;
}

/*
 * Return the offset of TLD if @name is found. Otherwise, return the current TLD count
 * using the nonpositive range so that the next tld_get_data() can skip fetching key if
 * no new TLD is added or start comparing name from the first newly added TLD.
 */
__attribute__((unused))
static int __tld_fetch_key(struct tld_object *tld_obj, const char *name, int i_start)
{
	struct tld_metadata *metadata;
	int i, cnt, start, off = 0;

	if (!tld_obj->data_map || !tld_obj->data_map->data || !tld_obj->data_map->meta)
		return 0;

	start = tld_obj->data_map->data->start;
	cnt = tld_obj->data_map->meta->cnt;
	metadata = tld_obj->data_map->meta->metadata;

	bpf_for(i, 0, cnt) {
		if (i >= TLD_MAX_DATA_CNT)
			break;

		if (i >= i_start && !bpf_strncmp(metadata[i].name, TLD_NAME_LEN, name))
			return start + off;

		off += TLD_ROUND_UP(metadata[i].size, 8);
	}

	return -cnt;
}

/**
 * tld_get_data() - Retrieve a pointer to the TLD associated with the name.
 *
 * @tld_obj: A pointer to a valid tld_object initialized by tld_object_init()
 * @key: The cached key of the TLD in tld_key_map
 * @name: The name of the key associated with a TLD
 * @size: The size of the TLD. Must be a known constant value
 *
 * Return a pointer to the TLD associated with @name; NULL if not found or @size is too
 * big. @key is used to cache the key if the TLD is found to speed up subsequent calls.
 * It should be defined as an member of tld_keys of tld_key_t type by the developer.
 */
#define tld_get_data(tld_obj, key, name, size)						\
	({										\
		void *data = NULL, *_data = (tld_obj)->data_map->data;			\
		long off = (tld_obj)->key_map->key.off;					\
		int cnt;								\
											\
		if (likely(_data)) {							\
			if (likely(off > 0)) {						\
				barrier_var(off);					\
				if (likely(off < __PAGE_SIZE - size))			\
					data = _data + off;				\
			} else {							\
				cnt = -(off);						\
				if (likely((tld_obj)->data_map->meta) &&		\
				    cnt < (tld_obj)->data_map->meta->cnt) {		\
					off = __tld_fetch_key(tld_obj, name, cnt);	\
					(tld_obj)->key_map->key.off = off;		\
											\
					if (likely(off < __PAGE_SIZE - size)) {		\
						barrier_var(off);			\
						if (off > 0)				\
							data = _data + off;		\
					}						\
				}							\
			}								\
		}									\
		data;									\
	})

#endif
