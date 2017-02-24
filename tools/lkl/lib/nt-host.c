#include <windows.h>
#include <assert.h>
#include <unistd.h>
#undef s_addr
#include <lkl_host.h>
#include "iomem.h"
#include "jmp_buf.h"

#define DIFF_1601_TO_1970_IN_100NS (11644473600L * 10000000L)

struct lkl_mutex {
	int recursive;
	HANDLE handle;
};

struct lkl_sem {
	HANDLE sem;
};

struct lkl_tls_key {
	DWORD key;
};

static struct lkl_sem *sem_alloc(int count)
{
	struct lkl_sem *sem = malloc(sizeof(struct lkl_sem));

	sem->sem = CreateSemaphore(NULL, count, 100, NULL);
	return sem;
}

static void sem_up(struct lkl_sem *sem)
{
	ReleaseSemaphore(sem->sem, 1, NULL);
}

static void sem_down(struct lkl_sem *sem)
{
	WaitForSingleObject(sem->sem, INFINITE);
}

static void sem_free(struct lkl_sem *sem)
{
	CloseHandle(sem->sem);
	free(sem);
}

static struct lkl_mutex *mutex_alloc(int recursive)
{
	struct lkl_mutex *_mutex = malloc(sizeof(struct lkl_mutex));
	if (!_mutex)
		return NULL;

	if (recursive)
		_mutex->handle = CreateMutex(0, FALSE, 0);
	else
		_mutex->handle = CreateSemaphore(NULL, 1, 100, NULL);
	_mutex->recursive = recursive;
	return _mutex;
}

static void mutex_lock(struct lkl_mutex *mutex)
{
	WaitForSingleObject(mutex->handle, INFINITE);
}

static void mutex_unlock(struct lkl_mutex *_mutex)
{
	if (_mutex->recursive)
		ReleaseMutex(_mutex->handle);
	else
		ReleaseSemaphore(_mutex->handle, 1, NULL);
}

static void mutex_free(struct lkl_mutex *_mutex)
{
	CloseHandle(_mutex->handle);
	free(_mutex);
}

static lkl_thread_t thread_create(void (*fn)(void *), void *arg)
{
	DWORD WINAPI (*win_fn)(LPVOID arg) = (DWORD WINAPI (*)(LPVOID))fn;
	HANDLE h = CreateThread(NULL, 0, win_fn, arg, 0, NULL);

	if (!h)
		return 0;

	return GetThreadId(h);
}

static void thread_detach(void)
{
}

static void thread_exit(void)
{
	ExitThread(0);
}

static int thread_join(lkl_thread_t tid)
{
	int ret;
	HANDLE *h;

	h = OpenThread(SYNCHRONIZE, FALSE, tid);
	if (!h)
		lkl_printf("%s: can't get thread handle\n", __func__);

	ret = WaitForSingleObject(h, INFINITE);
	if (ret)
		lkl_printf("%s: %d\n", __func__, ret);

	CloseHandle(h);

	return ret ? -1 : 0;
}

static lkl_thread_t thread_self(void)
{
	return GetThreadId(GetCurrentThread());
}

static int thread_equal(lkl_thread_t a, lkl_thread_t b)
{
	return a == b;
}

static struct lkl_tls_key *tls_alloc(void (*destructor)(void *))
{
	struct lkl_tls_key *ret = malloc(sizeof(struct lkl_tls_key));

	ret->key = FlsAlloc((PFLS_CALLBACK_FUNCTION)destructor);
	if (ret->key == TLS_OUT_OF_INDEXES) {
		free(ret);
		return NULL;
	}
	return ret;
}

static void tls_free(struct lkl_tls_key *key)
{
	/* setting to NULL first to prevent the callback from being called */
	FlsSetValue(key->key, NULL);
	FlsFree(key->key);
	free(key);
}

static int tls_set(struct lkl_tls_key *key, void *data)
{
	return FlsSetValue(key->key, data) ? 0 : -1;
}

static void *tls_get(struct lkl_tls_key *key)
{
	return FlsGetValue(key->key);
}


/*
 * With 64 bits, we can cover about 583 years at a nanosecond resolution.
 * Windows counts time from 1601 so we do have about 100 years before we
 * overflow.
 */
static unsigned long long time_ns(void)
{
	SYSTEMTIME st;
	FILETIME ft;
	ULARGE_INTEGER uli;

	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);
	uli.LowPart = ft.dwLowDateTime;
	uli.HighPart = ft.dwHighDateTime;

	return (uli.QuadPart - DIFF_1601_TO_1970_IN_100NS) * 100;
}

struct timer {
	HANDLE queue;
	void (*callback)(void *);
	void *arg;
};

static void *timer_alloc(void (*fn)(void *), void *arg)
{
	struct timer *t;

	t = malloc(sizeof(*t));
	if (!t)
		return NULL;

	t->queue = CreateTimerQueue();
	if (!t->queue) {
		free(t);
		return NULL;
	}

	t->callback = fn;
	t->arg = arg;

	return t;
}

static void CALLBACK timer_callback(void *arg, BOOLEAN TimerOrWaitFired)
{
	struct timer *t = (struct timer *)arg;

	if (TimerOrWaitFired)
		t->callback(t->arg);
}

static int timer_set_oneshot(void *timer, unsigned long ns)
{
	struct timer *t = (struct timer *)timer;
	HANDLE tmp;

	return !CreateTimerQueueTimer(&tmp, t->queue, timer_callback, t,
				      ns / 1000000, 0, 0);
}

static void timer_free(void *timer)
{
	struct timer *t = (struct timer *)timer;
	HANDLE completion;

	completion = CreateEvent(NULL, FALSE, FALSE, NULL);
	DeleteTimerQueueEx(t->queue, completion);
	WaitForSingleObject(completion, INFINITE);
	free(t);
}

static void panic(void)
{
	int *x = NULL;

	*x = 1;
	assert(0);
}

static void print(const char *str, int len)
{
	write(1, str, len);
}

static long gettid(void)
{
	return GetCurrentThreadId();
}

static void *mem_alloc(unsigned long size)
{
	return malloc(size);
}

struct lkl_host_operations lkl_host_ops = {
	.panic = panic,
	.thread_create = thread_create,
	.thread_detach = thread_detach,
	.thread_exit = thread_exit,
	.thread_join = thread_join,
	.thread_self = thread_self,
	.thread_equal = thread_equal,
	.sem_alloc = sem_alloc,
	.sem_free = sem_free,
	.sem_up = sem_up,
	.sem_down = sem_down,
	.mutex_alloc = mutex_alloc,
	.mutex_free = mutex_free,
	.mutex_lock = mutex_lock,
	.mutex_unlock = mutex_unlock,
	.tls_alloc = tls_alloc,
	.tls_free = tls_free,
	.tls_set = tls_set,
	.tls_get = tls_get,
	.time = time_ns,
	.timer_alloc = timer_alloc,
	.timer_set_oneshot = timer_set_oneshot,
	.timer_free = timer_free,
	.print = print,
	.mem_alloc = mem_alloc,
	.mem_free = free,
	.ioremap = lkl_ioremap,
	.iomem_access = lkl_iomem_access,
	.virtio_devices = lkl_virtio_devs,
	.gettid = gettid,
	.jmp_buf_set = jmp_buf_set,
	.jmp_buf_longjmp = jmp_buf_longjmp,
};

int handle_get_capacity(struct lkl_disk disk, unsigned long long *res)
{
	LARGE_INTEGER tmp;

	if (!GetFileSizeEx(disk.handle, &tmp))
		return -1;

	*res = tmp.QuadPart;
	return 0;
}

static int blk_request(struct lkl_disk disk, struct lkl_blk_req *req)
{
	unsigned long long offset = req->sector * 512;
	OVERLAPPED ov = { 0, };
	int err = 0, ret;

	switch (req->type) {
	case LKL_DEV_BLK_TYPE_READ:
	case LKL_DEV_BLK_TYPE_WRITE:
	{
		int i;

		for (i = 0; i < req->count; i++) {
			DWORD res;
			struct iovec *buf = &req->buf[i];

			ov.Offset = offset & 0xffffffff;
			ov.OffsetHigh = offset >> 32;

			if (req->type == LKL_DEV_BLK_TYPE_READ)
				ret = ReadFile(disk.handle, buf->iov_base,
					       buf->iov_len, &res, &ov);
			else
				ret = WriteFile(disk.handle, buf->iov_base,
						buf->iov_len, &res, &ov);
			if (!ret) {
				lkl_printf("%s: I/O error: %d\n", __func__,
					   GetLastError());
				err = -1;
				goto out;
			}

			if (res != buf->iov_len) {
				lkl_printf("%s: I/O error: short: %d %d\n",
					   res, buf->iov_len);
				err = -1;
				goto out;
			}

			offset += buf->iov_len;
		}
		break;
	}
	case LKL_DEV_BLK_TYPE_FLUSH:
	case LKL_DEV_BLK_TYPE_FLUSH_OUT:
		ret = FlushFileBuffers(disk.handle);
		if (!ret)
			err = 1;
		break;
	default:
		return LKL_DEV_BLK_STATUS_UNSUP;
	}

out:
	if (err < 0)
		return LKL_DEV_BLK_STATUS_IOERR;

	return LKL_DEV_BLK_STATUS_OK;
}

struct lkl_dev_blk_ops lkl_dev_blk_ops = {
	.get_capacity = handle_get_capacity,
	.request = blk_request,
};

/* Needed to resolve linker error on Win32. We don't really support
 * any network IO on Windows, anyway, so there's no loss here. */
int lkl_netdevs_remove(void)
{
	return 0;
}
