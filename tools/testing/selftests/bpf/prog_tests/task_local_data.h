/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TASK_LOCAL_DATA_H
#define __TASK_LOCAL_DATA_H

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>

#ifdef TLD_FREE_DATA_ON_THREAD_EXIT
#include <pthread.h>
#endif

#include <bpf/bpf.h>

/*
 * OPTIONS
 *
 *   Define the option before including the header
 *
 *   TLD_FREE_DATA_ON_THREAD_EXIT - Frees memory on thread exit automatically
 *
 *   Thread-specific memory for storing TLD is allocated lazily on the first call to
 *   tld_get_data(). The thread that calls it must also call tld_free() on thread exit
 *   to prevent memory leak. Pthread will be included if the option is defined. A pthread
 *   key will be registered with a destructor that calls tld_free().
 *
 *
 *   TLD_DYN_DATA_SIZE - The maximum size of memory allocated for TLDs created dynamically
 *   (default: 64 bytes)
 *
 *   A TLD can be defined statically using TLD_DEFINE_KEY() or created on the fly using
 *   tld_create_key(). As the total size of TLDs created with tld_create_key() cannot be
 *   possibly known statically, a memory area of size TLD_DYN_DATA_SIZE will be allocated
 *   for these TLDs. This additional memory is allocated for every thread that calls
 *   tld_get_data() even if no tld_create_key are actually called, so be mindful of
 *   potential memory wastage. Use TLD_DEFINE_KEY() whenever possible as just enough memory
 *   will be allocated for TLDs created with it.
 *
 *
 *   TLD_NAME_LEN - The maximum length of the name of a TLD (default: 62)
 *
 *   Setting TLD_NAME_LEN will affect the maximum number of TLDs a process can store,
 *   TLD_MAX_DATA_CNT.
 *
 *
 *   TLD_DATA_USE_ALIGNED_ALLOC - Always use aligned_alloc() instead of malloc()
 *
 *   When allocating the memory for storing TLDs, we need to make sure there is a memory
 *   region of the X bytes within a page. This is due to the limit posed by UPTR: memory
 *   pinned to the kernel cannot exceed a page nor can it cross the page boundary. The
 *   library normally calls malloc(2*X) given X bytes of total TLDs, and only uses
 *   aligned_alloc(PAGE_SIZE, X) when X >= PAGE_SIZE / 2. This is to reduce memory wastage
 *   as not all memory allocator can use the exact amount of memory requested to fulfill
 *   aligned_alloc(). For example, some may round the size up to the alignment. Enable the
 *   option to always use aligned_alloc() if the implementation has low memory overhead.
 */

#define TLD_PAGE_SIZE getpagesize()
#define TLD_PAGE_MASK (~(TLD_PAGE_SIZE - 1))

#define TLD_ROUND_MASK(x, y) ((__typeof__(x))((y) - 1))
#define TLD_ROUND_UP(x, y) ((((x) - 1) | TLD_ROUND_MASK(x, y)) + 1)

#define TLD_READ_ONCE(x) (*(volatile typeof(x) *)&(x))

#ifndef TLD_DYN_DATA_SIZE
#define TLD_DYN_DATA_SIZE 64
#endif

#define TLD_MAX_DATA_CNT (TLD_PAGE_SIZE / sizeof(struct tld_metadata) - 1)

#ifndef TLD_NAME_LEN
#define TLD_NAME_LEN 62
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	__s16 off;
} tld_key_t;

struct tld_metadata {
	char name[TLD_NAME_LEN];
	_Atomic __u16 size;
};

struct tld_meta_u {
	_Atomic __u8 cnt;
	__u16 size;
	struct tld_metadata metadata[];
};

struct tld_data_u {
	__u64 start; /* offset of tld_data_u->data in a page */
	char data[];
};

struct tld_map_value {
	void *data;
	struct tld_meta_u *meta;
};

struct tld_meta_u * _Atomic tld_meta_p __attribute__((weak));
__thread struct tld_data_u *tld_data_p __attribute__((weak));
__thread void *tld_data_alloc_p __attribute__((weak));

#ifdef TLD_FREE_DATA_ON_THREAD_EXIT
pthread_key_t tld_pthread_key __attribute__((weak));

static void tld_free(void);

static void __tld_thread_exit_handler(void *unused)
{
	tld_free();
}
#endif

static int __tld_init_meta_p(void)
{
	struct tld_meta_u *meta, *uninit = NULL;
	int err = 0;

	meta = (struct tld_meta_u *)aligned_alloc(TLD_PAGE_SIZE, TLD_PAGE_SIZE);
	if (!meta) {
		err = -ENOMEM;
		goto out;
	}

	memset(meta, 0, TLD_PAGE_SIZE);
	meta->size = TLD_DYN_DATA_SIZE;

	if (!atomic_compare_exchange_strong(&tld_meta_p, &uninit, meta)) {
		free(meta);
		goto out;
	}

#ifdef TLD_FREE_DATA_ON_THREAD_EXIT
	pthread_key_create(&tld_pthread_key, __tld_thread_exit_handler);
#endif
out:
	return err;
}

static int __tld_init_data_p(int map_fd)
{
	bool use_aligned_alloc = false;
	struct tld_map_value map_val;
	struct tld_data_u *data;
	void *data_alloc = NULL;
	int err, tid_fd = -1;

	tid_fd = syscall(SYS_pidfd_open, sys_gettid(), O_EXCL);
	if (tid_fd < 0) {
		err = -errno;
		goto out;
	}

#ifdef TLD_DATA_USE_ALIGNED_ALLOC
	use_aligned_alloc = true;
#endif

	/*
	 * tld_meta_p->size = TLD_DYN_DATA_SIZE +
	 *          total size of TLDs defined via TLD_DEFINE_KEY()
	 */
	data_alloc = (use_aligned_alloc || tld_meta_p->size * 2 >= TLD_PAGE_SIZE) ?
		aligned_alloc(TLD_PAGE_SIZE, tld_meta_p->size) :
		malloc(tld_meta_p->size * 2);
	if (!data_alloc) {
		err = -ENOMEM;
		goto out;
	}

	/*
	 * Always pass a page-aligned address to UPTR since the size of tld_map_value::data
	 * is a page in BTF. If data_alloc spans across two pages, use the page that contains large
	 * enough memory.
	 */
	if (TLD_PAGE_SIZE - (~TLD_PAGE_MASK & (intptr_t)data_alloc) >= tld_meta_p->size) {
		map_val.data = (void *)(TLD_PAGE_MASK & (intptr_t)data_alloc);
		data = data_alloc;
		data->start = (~TLD_PAGE_MASK & (intptr_t)data_alloc) +
			      offsetof(struct tld_data_u, data);
	} else {
		map_val.data = (void *)(TLD_ROUND_UP((intptr_t)data_alloc, TLD_PAGE_SIZE));
		data = (void *)(TLD_ROUND_UP((intptr_t)data_alloc, TLD_PAGE_SIZE));
		data->start = offsetof(struct tld_data_u, data);
	}
	map_val.meta = TLD_READ_ONCE(tld_meta_p);

	err = bpf_map_update_elem(map_fd, &tid_fd, &map_val, 0);
	if (err) {
		free(data_alloc);
		goto out;
	}

	tld_data_p = data;
	tld_data_alloc_p = data_alloc;
#ifdef TLD_FREE_DATA_ON_THREAD_EXIT
	pthread_setspecific(tld_pthread_key, (void *)1);
#endif
out:
	if (tid_fd >= 0)
		close(tid_fd);
	return err;
}

static tld_key_t __tld_create_key(const char *name, size_t size, bool dyn_data)
{
	int err, i, sz, off = 0;
	__u8 cnt;

	if (!TLD_READ_ONCE(tld_meta_p)) {
		err = __tld_init_meta_p();
		if (err)
			return (tld_key_t){err};
	}

	for (i = 0; i < TLD_MAX_DATA_CNT; i++) {
retry:
		cnt = atomic_load(&tld_meta_p->cnt);
		if (i < cnt) {
			/* A metadata is not ready until size is updated with a non-zero value */
			while (!(sz = atomic_load(&tld_meta_p->metadata[i].size)))
				sched_yield();

			if (!strncmp(tld_meta_p->metadata[i].name, name, TLD_NAME_LEN))
				return (tld_key_t){-EEXIST};

			off += TLD_ROUND_UP(sz, 8);
			continue;
		}

		/*
		 * TLD_DEFINE_KEY() is given memory upto a page while at most
		 * TLD_DYN_DATA_SIZE is allocated for tld_create_key()
		 */
		if (dyn_data) {
			if (off + TLD_ROUND_UP(size, 8) > tld_meta_p->size)
				return (tld_key_t){-E2BIG};
		} else {
			if (off + TLD_ROUND_UP(size, 8) > TLD_PAGE_SIZE - sizeof(struct tld_data_u))
				return (tld_key_t){-E2BIG};
			tld_meta_p->size += TLD_ROUND_UP(size, 8);
		}

		/*
		 * Only one tld_create_key() can increase the current cnt by one and
		 * takes the latest available slot. Other threads will check again if a new
		 * TLD can still be added, and then compete for the new slot after the
		 * succeeding thread update the size.
		 */
		if (!atomic_compare_exchange_strong(&tld_meta_p->cnt, &cnt, cnt + 1))
			goto retry;

		strncpy(tld_meta_p->metadata[i].name, name, TLD_NAME_LEN);
		atomic_store(&tld_meta_p->metadata[i].size, size);
		return (tld_key_t){(__s16)off};
	}

	return (tld_key_t){-ENOSPC};
}

/**
 * TLD_DEFINE_KEY() - Define a TLD and a global variable key associated with the TLD.
 *
 * @name: The name of the TLD
 * @size: The size of the TLD
 * @key: The variable name of the key. Cannot exceed TLD_NAME_LEN
 *
 * The macro can only be used in file scope.
 *
 * A global variable key of opaque type, tld_key_t, will be declared and initialized before
 * main() starts. Use tld_key_is_err() or tld_key_err_or_zero() later to check if the key
 * creation succeeded. Pass the key to tld_get_data() to get a pointer to the TLD.
 * bpf programs can also fetch the same key by name.
 *
 * The total size of TLDs created using TLD_DEFINE_KEY() cannot exceed a page. Just
 * enough memory will be allocated for each thread on the first call to tld_get_data().
 */
#define TLD_DEFINE_KEY(key, name, size)			\
tld_key_t key;						\
							\
__attribute__((constructor))				\
void __tld_define_key_##key(void)			\
{							\
	key = __tld_create_key(name, size, false);	\
}

/**
 * tld_create_key() - Create a TLD and return a key associated with the TLD.
 *
 * @name: The name the TLD
 * @size: The size of the TLD
 *
 * Return an opaque object key. Use tld_key_is_err() or tld_key_err_or_zero() to check
 * if the key creation succeeded. Pass the key to tld_get_data() to get a pointer to
 * locate the TLD. bpf programs can also fetch the same key by name.
 *
 * Use tld_create_key() only when a TLD needs to be created dynamically (e.g., @name is
 * not known statically or a TLD needs to be created conditionally)
 *
 * An additional TLD_DYN_DATA_SIZE bytes are allocated per-thread to accommodate TLDs
 * created dynamically with tld_create_key(). Since only a user page is pinned to the
 * kernel, when TLDs created with TLD_DEFINE_KEY() uses more than TLD_PAGE_SIZE -
 * TLD_DYN_DATA_SIZE, the buffer size will be limited to the rest of the page.
 */
__attribute__((unused))
static tld_key_t tld_create_key(const char *name, size_t size)
{
	return __tld_create_key(name, size, true);
}

__attribute__((unused))
static inline bool tld_key_is_err(tld_key_t key)
{
	return key.off < 0;
}

__attribute__((unused))
static inline int tld_key_err_or_zero(tld_key_t key)
{
	return tld_key_is_err(key) ? key.off : 0;
}

/**
 * tld_get_data() - Get a pointer to the TLD associated with the given key of the
 * calling thread.
 *
 * @map_fd: A file descriptor of tld_data_map, the underlying BPF task local storage map
 * of task local data.
 * @key: A key object created by TLD_DEFINE_KEY() or tld_create_key().
 *
 * Return a pointer to the TLD if the key is valid; NULL if not enough memory for TLD
 * for this thread, or the key is invalid. The returned pointer is guaranteed to be 8-byte
 * aligned.
 *
 * Threads that call tld_get_data() must call tld_free() on exit to prevent
 * memory leak if TLD_FREE_DATA_ON_THREAD_EXIT is not defined.
 */
__attribute__((unused))
static void *tld_get_data(int map_fd, tld_key_t key)
{
	if (!TLD_READ_ONCE(tld_meta_p))
		return NULL;

	/* tld_data_p is allocated on the first invocation of tld_get_data() */
	if (!tld_data_p && __tld_init_data_p(map_fd))
		return NULL;

	return tld_data_p->data + key.off;
}

/**
 * tld_free() - Free task local data memory of the calling thread
 *
 * For the calling thread, all pointers to TLDs acquired before will become invalid.
 *
 * Users must call tld_free() on thread exit to prevent memory leak. Alternatively,
 * define TLD_FREE_DATA_ON_THREAD_EXIT and a thread exit handler will be registered
 * to free the memory automatically.
 */
__attribute__((unused))
static void tld_free(void)
{
	if (tld_data_alloc_p) {
		free(tld_data_alloc_p);
		tld_data_alloc_p = NULL;
		tld_data_p = NULL;
	}
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __TASK_LOCAL_DATA_H */
