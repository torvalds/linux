#include <windows.h>
#include <assert.h>
#include <unistd.h>
#undef s_addr
#include <lkl_host.h>
#include "iomem.h"

static void *sem_alloc(int count)
{
	return CreateSemaphore(NULL, count, 100, NULL);
}

static void sem_up(void *sem)
{
	ReleaseSemaphore(sem, 1, NULL);
}

static void sem_down(void *sem)
{
	WaitForSingleObject(sem, INFINITE);
}

static void sem_free(void *sem)
{
	CloseHandle(sem);
}

static int thread_create(void (*fn)(void *), void *arg)
{
	DWORD WINAPI (*win_fn)(LPVOID arg) = (DWORD WINAPI (*)(LPVOID))fn;

	return CreateThread(NULL, 0, win_fn, arg, 0, NULL) ? 0 : -1;
}

static void thread_exit(void)
{
	ExitThread(0);
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
	LARGE_INTEGER li;

	GetSystemTime(&st);
	SystemTimeToFileTime(&st, &ft);
	li.LowPart = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;

	return li.QuadPart*100;
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

static void *mem_alloc(unsigned long size)
{
	return malloc(size);
}

struct lkl_host_operations lkl_host_ops = {
	.panic = panic,
	.thread_create = thread_create,
	.thread_exit = thread_exit,
	.sem_alloc = sem_alloc,
	.sem_free = sem_free,
	.sem_up = sem_up,
	.sem_down = sem_down,
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
};

int handle_get_capacity(union lkl_disk disk, unsigned long long *res)
{
	LARGE_INTEGER tmp;

	if (!GetFileSizeEx(disk.handle, &tmp))
		return -1;

	*res = tmp.QuadPart;
	return 0;
}

static int blk_request(union lkl_disk disk, struct lkl_blk_req *req)
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

			ov.Offset = offset & 0xffffffff;
			ov.OffsetHigh = offset >> 32;

			if (req->type == LKL_DEV_BLK_TYPE_READ)
				ret = ReadFile(disk.handle, req->buf[i].addr,
					       req->buf[i].len, &res, &ov);
			else
				ret = WriteFile(disk.handle, req->buf[i].addr,
						req->buf[i].len, &res, &ov);
			if (!ret) {
				lkl_printf("%s: I/O error: %d\n", __func__,
					   GetLastError());
				err = -1;
				goto out;
			}

			if (res != req->buf[i].len) {
				lkl_printf("%s: I/O error: short: %d %d\n",
					   res, req->buf[i].len);
				err = -1;
				goto out;
			}

			offset += req->buf[i].len;
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
