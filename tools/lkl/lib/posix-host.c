// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <poll.h>
#include <limits.h>
#include <lkl_host.h>
#include "iomem.h"
#include "jmp_buf.h"

/* Let's see if the host has semaphore.h */
#include <unistd.h>

#ifdef _POSIX_SEMAPHORES
#include <semaphore.h>
/* TODO(pscollins): We don't support fork() for now, but maybe one day
 * we will? */
#define SHARE_SEM 0
#endif /* _POSIX_SEMAPHORES */

static void print(const char *str, int len)
{
	int ret __attribute__((unused));

	ret = write(STDOUT_FILENO, str, len);
}

struct lkl_mutex {
	pthread_mutex_t mutex;
};

struct lkl_sem {
#ifdef _POSIX_SEMAPHORES
	sem_t sem;
#else
	pthread_mutex_t lock;
	int count;
	pthread_cond_t cond;
#endif /* _POSIX_SEMAPHORES */
};

struct lkl_tls_key {
	pthread_key_t key;
};

#define WARN_UNLESS(exp) do {						\
		if (exp < 0)						\
			lkl_printf("%s: %s\n", #exp, strerror(errno));	\
	} while (0)

static int _warn_pthread(int ret, char *str_exp)
{
	if (ret > 0)
		lkl_printf("%s: %s\n", str_exp, strerror(ret));

	return ret;
}


/* pthread_* functions use the reverse convention */
#define WARN_PTHREAD(exp) _warn_pthread(exp, #exp)

static struct lkl_sem *sem_alloc(int count)
{
	struct lkl_sem *sem;

	sem = malloc(sizeof(*sem));
	if (!sem)
		return NULL;

#ifdef _POSIX_SEMAPHORES
	if (sem_init(&sem->sem, SHARE_SEM, count) < 0) {
		lkl_printf("sem_init: %s\n", strerror(errno));
		free(sem);
		return NULL;
	}
#else
	pthread_mutex_init(&sem->lock, NULL);
	sem->count = count;
	WARN_PTHREAD(pthread_cond_init(&sem->cond, NULL));
#endif /* _POSIX_SEMAPHORES */

	return sem;
}

static void sem_free(struct lkl_sem *sem)
{
#ifdef _POSIX_SEMAPHORES
	WARN_UNLESS(sem_destroy(&sem->sem));
#else
	WARN_PTHREAD(pthread_cond_destroy(&sem->cond));
	WARN_PTHREAD(pthread_mutex_destroy(&sem->lock));
#endif /* _POSIX_SEMAPHORES */
	free(sem);
}

static void sem_up(struct lkl_sem *sem)
{
#ifdef _POSIX_SEMAPHORES
	WARN_UNLESS(sem_post(&sem->sem));
#else
	WARN_PTHREAD(pthread_mutex_lock(&sem->lock));
	sem->count++;
	if (sem->count > 0)
		WARN_PTHREAD(pthread_cond_signal(&sem->cond));
	WARN_PTHREAD(pthread_mutex_unlock(&sem->lock));
#endif /* _POSIX_SEMAPHORES */

}

static void sem_down(struct lkl_sem *sem)
{
#ifdef _POSIX_SEMAPHORES
	int err;

	do {
		err = sem_wait(&sem->sem);
	} while (err < 0 && errno == EINTR);
	if (err < 0 && errno != EINTR)
		lkl_printf("sem_wait: %s\n", strerror(errno));
#else
	WARN_PTHREAD(pthread_mutex_lock(&sem->lock));
	while (sem->count <= 0)
		WARN_PTHREAD(pthread_cond_wait(&sem->cond, &sem->lock));
	sem->count--;
	WARN_PTHREAD(pthread_mutex_unlock(&sem->lock));
#endif /* _POSIX_SEMAPHORES */
}

static struct lkl_mutex *mutex_alloc(int recursive)
{
	struct lkl_mutex *_mutex = malloc(sizeof(struct lkl_mutex));
	pthread_mutex_t *mutex = NULL;
	pthread_mutexattr_t attr;

	if (!_mutex)
		return NULL;

	mutex = &_mutex->mutex;
	WARN_PTHREAD(pthread_mutexattr_init(&attr));

	/* PTHREAD_MUTEX_ERRORCHECK is *very* useful for debugging,
	 * but has some overhead, so we provide an option to turn it
	 * off. */
#ifdef DEBUG
	if (!recursive)
		WARN_PTHREAD(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
#endif /* DEBUG */

	if (recursive)
		WARN_PTHREAD(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));

	WARN_PTHREAD(pthread_mutex_init(mutex, &attr));

	return _mutex;
}

static void mutex_lock(struct lkl_mutex *mutex)
{
	WARN_PTHREAD(pthread_mutex_lock(&mutex->mutex));
}

static void mutex_unlock(struct lkl_mutex *_mutex)
{
	pthread_mutex_t *mutex = &_mutex->mutex;
	WARN_PTHREAD(pthread_mutex_unlock(mutex));
}

static void mutex_free(struct lkl_mutex *_mutex)
{
	pthread_mutex_t *mutex = &_mutex->mutex;
	WARN_PTHREAD(pthread_mutex_destroy(mutex));
	free(_mutex);
}

static lkl_thread_t thread_create(void (*fn)(void *), void *arg)
{
	pthread_t thread;
	if (WARN_PTHREAD(pthread_create(&thread, NULL, (void* (*)(void *))fn, arg)))
		return 0;
	else
		return (lkl_thread_t) thread;
}

static void thread_detach(void)
{
	WARN_PTHREAD(pthread_detach(pthread_self()));
}

static void thread_exit(void)
{
	pthread_exit(NULL);
}

static int thread_join(lkl_thread_t tid)
{
	if (WARN_PTHREAD(pthread_join((pthread_t)tid, NULL)))
		return -1;
	else
		return 0;
}

static lkl_thread_t thread_self(void)
{
	return (lkl_thread_t)pthread_self();
}

static int thread_equal(lkl_thread_t a, lkl_thread_t b)
{
	return pthread_equal((pthread_t)a, (pthread_t)b);
}

#ifdef	__FreeBSD__
#define pthread_getattr_np pthread_attr_get_np
#endif

void *thread_stack(unsigned long *size)
{
	pthread_attr_t thread_attr;
	size_t stack_size;
	void *thread_stack;

	if (pthread_getattr_np(pthread_self(), &thread_attr))
		return NULL;

	if (pthread_attr_getstack(&thread_attr, &thread_stack, &stack_size))
		thread_stack = NULL;

	pthread_attr_destroy(&thread_attr);

	if (size && thread_stack)
		*size = stack_size;

	return thread_stack;
}

static struct lkl_tls_key *tsd_alloc(void (*destructor)(void *))
{
	struct lkl_tls_key *ret = malloc(sizeof(struct lkl_tls_key));

	if (WARN_PTHREAD(pthread_key_create(&ret->key, destructor))) {
		free(ret);
		return NULL;
	}
	return ret;
}

static void tsd_free(struct lkl_tls_key *key)
{
	WARN_PTHREAD(pthread_key_delete(key->key));
	free(key);
}

static int tsd_set(struct lkl_tls_key *key, void *data)
{
	if (WARN_PTHREAD(pthread_setspecific(key->key, data)))
		return -1;
	return 0;
}

static void *tsd_get(struct lkl_tls_key *key)
{
	return pthread_getspecific(key->key);
}

/**
 * when LKL is loaded via dl*m*open(3), the pthread_getspecific()
 * doesn't work correctly, as a global symbol, __pthread_keys, is
 * duplicated across multiple namespaces and conflicts the same keys
 * in multiple users of TSD in a single process, which makes our case
 * impossible to work (e.g., host_task from each thread).
 *
 * To work around this issue, we use TLS, using __thread which doesn't
 * require any conflict global symbols.  but the default __thread uses
 * __tls_get_addr() of glibc function, calling futex, and making a
 * dead-lock in our thread.  So explicitly initialize with
 * initial-exec is needed.
 *
 * We'll still use the previous *TSD* (thread specific data)
 * implementation based on pthread_key_create, as the most of the
 * cases, don't hit this situation, as dlmopen is not a common
 * practice and a few implementation (i.e., like glibc) has this
 * function.
 *
 */
#define LKL_MAX_TLS_KEYS           (PTHREAD_KEYS_MAX/8) /* 1024/8 = 128 */
struct __lkl_tls_keys {
	int used;
	void *data;
};
static __thread struct __lkl_tls_keys __tls_keys[LKL_MAX_TLS_KEYS];

static struct lkl_tls_key *tls_alloc(void (*destructor)(void *))
{
	int idx;
	struct lkl_tls_key *ret = malloc(sizeof(struct lkl_tls_key));

	for (idx = 0; idx < LKL_MAX_TLS_KEYS; idx++) {
		/* data = NULL means the key unused */
		if (__tls_keys[idx].used == 0) {
			ret->key = (pthread_key_t)idx;
			__tls_keys[idx].used = 1;
			return ret;
		}
	}

	/* if there are no unused keys, return NULL */
	free(ret);
	return NULL;
}

static void tls_free(struct lkl_tls_key *key)
{
	int idx = (int)key->key;

	if (idx < 0 || idx >= LKL_MAX_TLS_KEYS) {
		lkl_printf("%s; key not found\n", __func__);
		return;
	}
	__tls_keys[idx].used = 0;
	free(key);
}

static int tls_set(struct lkl_tls_key *key, void *data)
{
	int idx = (int)key->key;

	if (idx < 0 || idx >= LKL_MAX_TLS_KEYS) {
		lkl_printf("%s; key not found\n", __func__);
		return -1;
	}
	__tls_keys[idx].data = data;
	return 0;
}

static void *tls_get(struct lkl_tls_key *key)
{
	int idx = (int)key->key;

	if (idx < 0 || idx >= LKL_MAX_TLS_KEYS) {
		lkl_printf("%s; key not found\n", __func__);
		return NULL;
	}

	return __tls_keys[idx].data;
}


static unsigned long long time_ns(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	return 1e9*ts.tv_sec + ts.tv_nsec;
}

static void *timer_alloc(void (*fn)(void *), void *arg)
{
	int err;
	timer_t timer;
	struct sigevent se =  {
		.sigev_notify = SIGEV_THREAD,
		.sigev_value = {
			.sival_ptr = arg,
		},
		.sigev_notify_function = (void (*)(union sigval))fn,
	};

	err = timer_create(CLOCK_REALTIME, &se, &timer);
	if (err)
		return NULL;

	return (void *)(long)timer;
}

static int timer_set_oneshot(void *_timer, unsigned long ns)
{
	timer_t timer = (timer_t)(long)_timer;
	struct itimerspec ts = {
		.it_value = {
			.tv_sec = ns / 1000000000,
			.tv_nsec = ns % 1000000000,
		},
	};

	return timer_settime(timer, 0, &ts, NULL);
}

static void timer_free(void *_timer)
{
	timer_t timer = (timer_t)(long)_timer;

	timer_delete(timer);
}

static void panic(void)
{
	assert(0);
}

static long _gettid(void)
{
#ifdef	__FreeBSD__
	return (long)pthread_self();
#else
	return syscall(SYS_gettid);
#endif
}

static void *page_alloc(unsigned long size)
{
	void *addr;

	addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (addr == MAP_FAILED)
		return NULL;

	return addr;
}

static void page_free(void *addr, unsigned long size)
{
	munmap((void *)addr, size);
}

static inline int get_prot(enum lkl_prot lkl_prot)
{
	int prot = PROT_NONE;

	if (lkl_prot & LKL_PROT_READ)
		prot |= PROT_READ;
	if (lkl_prot & LKL_PROT_WRITE)
		prot |= PROT_WRITE;
	if (lkl_prot & LKL_PROT_EXEC)
		prot |= PROT_EXEC;

	return prot;
}


#if defined(__FreeBSD__)
#define MAP_FIXED_NOREPLACE (MAP_FIXED | MAP_EXCL)
#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif
#elif defined(__linux__)
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0
#endif
#endif

static void *lkl_mmap(void *addr, unsigned long size, enum lkl_prot prot)
{
	void *ret;
	int fl = MAP_ANON | MAP_PRIVATE | MAP_FIXED_NOREPLACE | MAP_NORESERVE;

	ret = mmap(addr, size, get_prot(prot), fl, -1, 0);
	if (ret != addr) {
		if (ret != NULL)
			munmap(addr, size);
		return NULL;
	}

	return ret;
}

static int lkl_munmap(void *addr, unsigned long size)
{
	return munmap(addr, size);
}

#ifdef LKL_HOST_CONFIG_VFIO_PCI
extern struct lkl_dev_pci_ops vfio_pci_ops;
#endif

struct lkl_host_operations lkl_host_ops = {
	.panic = panic,
	.thread_create = thread_create,
	.thread_detach = thread_detach,
	.thread_exit = thread_exit,
	.thread_join = thread_join,
	.thread_self = thread_self,
	.thread_equal = thread_equal,
	.thread_stack = thread_stack,
	.sem_alloc = sem_alloc,
	.sem_free = sem_free,
	.sem_up = sem_up,
	.sem_down = sem_down,
	.mutex_alloc = mutex_alloc,
	.mutex_free = mutex_free,
	.mutex_lock = mutex_lock,
	.mutex_unlock = mutex_unlock,
	.tls_alloc = tsd_alloc,
	.tls_free = tsd_free,
	.tls_set = tsd_set,
	.tls_get = tsd_get,
	.time = time_ns,
	.timer_alloc = timer_alloc,
	.timer_set_oneshot = timer_set_oneshot,
	.timer_free = timer_free,
	.print = print,
	.mem_alloc = malloc,
	.mem_free = free,
	.page_alloc = page_alloc,
	.page_free = page_free,
	.ioremap = lkl_ioremap,
	.iomem_access = lkl_iomem_access,
	.virtio_devices = lkl_virtio_devs,
	.gettid = _gettid,
	.jmp_buf_set = jmp_buf_set,
	.jmp_buf_longjmp = jmp_buf_longjmp,
	.memcpy = memcpy,
	.memset = memset,
	.mmap = lkl_mmap,
	.munmap = lkl_munmap,
#ifdef LKL_HOST_CONFIG_VFIO_PCI
	.pci_ops = &vfio_pci_ops,
#endif
};

void lkl_change_tls_mode(void)
{
	lkl_host_ops.tls_alloc = tls_alloc;
	lkl_host_ops.tls_free = tls_free;
	lkl_host_ops.tls_set = tls_set;
	lkl_host_ops.tls_get = tls_get;
}

static int fd_get_capacity(struct lkl_disk disk, unsigned long long *res)
{
	off_t off;

	off = lseek(disk.fd, 0, SEEK_END);
	if (off < 0)
		return -1;

	*res = off;
	return 0;
}

static int do_rw(ssize_t (*fn)(), struct lkl_disk disk, struct lkl_blk_req *req)
{
	off_t off = req->sector * 512;
	void *addr;
	int len;
	int i;
	int ret = 0;

	for (i = 0; i < req->count; i++) {

		addr = req->buf[i].iov_base;
		len = req->buf[i].iov_len;

		do {
			ret = fn(disk.fd, addr, len, off);

			if (ret <= 0) {
				ret = -1;
				goto out;
			}

			addr += ret;
			len -= ret;
			off += ret;

		} while (len);
	}

out:
	return ret;
}

static int blk_request(struct lkl_disk disk, struct lkl_blk_req *req)
{
	int err = 0;

	switch (req->type) {
	case LKL_DEV_BLK_TYPE_READ:
		err = do_rw(pread, disk, req);
		break;
	case LKL_DEV_BLK_TYPE_WRITE:
		err = do_rw(pwrite, disk, req);
		break;
	case LKL_DEV_BLK_TYPE_FLUSH:
	case LKL_DEV_BLK_TYPE_FLUSH_OUT:
#ifdef __linux__
		err = fdatasync(disk.fd);
#else
		err = fsync(disk.fd);
#endif
		break;
	default:
		return LKL_DEV_BLK_STATUS_UNSUP;
	}

	if (err < 0)
		return LKL_DEV_BLK_STATUS_IOERR;

	return LKL_DEV_BLK_STATUS_OK;
}

struct lkl_dev_blk_ops lkl_dev_blk_ops = {
	.get_capacity = fd_get_capacity,
	.request = blk_request,
};

