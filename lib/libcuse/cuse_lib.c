/* $FreeBSD$ */
/*-
 * Copyright (c) 2010-2012 Hans Petter Selasky. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/queue.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <sys/param.h>

#include <fs/cuse/cuse_ioctl.h>

#include "cuse.h"

int	cuse_debug_level;

#ifdef HAVE_DEBUG
static const char *cuse_cmd_str(int cmd);

#define	DPRINTF(...) do {			\
	if (cuse_debug_level != 0)		\
		printf(__VA_ARGS__);		\
} while (0)
#else
#define	DPRINTF(...) do { } while (0)
#endif

struct cuse_vm_allocation {
	uint8_t *ptr;
	uint32_t size;
};

struct cuse_dev_entered {
	TAILQ_ENTRY(cuse_dev_entered) entry;
	pthread_t thread;
	void   *per_file_handle;
	struct cuse_dev *cdev;
	int	cmd;
	int	is_local;
	int	got_signal;
};

struct cuse_dev {
	TAILQ_ENTRY(cuse_dev) entry;
	const struct cuse_methods *mtod;
	void   *priv0;
	void   *priv1;
};

static int f_cuse = -1;

static pthread_mutex_t m_cuse;
static TAILQ_HEAD(, cuse_dev) h_cuse __guarded_by(m_cuse);
static TAILQ_HEAD(, cuse_dev_entered) h_cuse_entered __guarded_by(m_cuse);
static struct cuse_vm_allocation a_cuse[CUSE_ALLOC_UNIT_MAX]
    __guarded_by(m_cuse);

#define	CUSE_LOCK() \
	pthread_mutex_lock(&m_cuse)

#define	CUSE_UNLOCK() \
	pthread_mutex_unlock(&m_cuse)

int
cuse_init(void)
{
	pthread_mutexattr_t attr;

	f_cuse = open("/dev/cuse", O_RDWR);
	if (f_cuse < 0) {
		if (feature_present("cuse") == 0)
			return (CUSE_ERR_NOT_LOADED);
		else
			return (CUSE_ERR_INVALID);
	}
	pthread_mutexattr_init(&attr);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&m_cuse, &attr);

	TAILQ_INIT(&h_cuse);
	TAILQ_INIT(&h_cuse_entered);

	return (0);
}

int
cuse_uninit(void)
{
	int f;

	if (f_cuse < 0)
		return (CUSE_ERR_INVALID);

	f = f_cuse;
	f_cuse = -1;

	close(f);

	pthread_mutex_destroy(&m_cuse);

	memset(a_cuse, 0, sizeof(a_cuse));

	return (0);
}

unsigned long
cuse_vmoffset(void *_ptr)
{
	uint8_t *ptr_min;
	uint8_t *ptr_max;
	uint8_t *ptr = _ptr;
	unsigned long remainder;
	int n;

	CUSE_LOCK();
	for (n = 0; n != CUSE_ALLOC_UNIT_MAX; n++) {
		if (a_cuse[n].ptr == NULL)
			continue;

		ptr_min = a_cuse[n].ptr;
		ptr_max = a_cuse[n].ptr + a_cuse[n].size - 1;

		if ((ptr >= ptr_min) && (ptr <= ptr_max)) {

			CUSE_UNLOCK();

			remainder = (ptr - ptr_min);

			remainder -= remainder % PAGE_SIZE;

			return ((n * PAGE_SIZE * CUSE_ALLOC_PAGES_MAX) + remainder);
		}
	}
	CUSE_UNLOCK();

	return (0x80000000UL);		/* failure */
}

void   *
cuse_vmalloc(int size)
{
	struct cuse_alloc_info info;
	void *ptr;
	int error;
	int n;

	if (f_cuse < 0)
		return (NULL);

	memset(&info, 0, sizeof(info));

	if (size < 1)
		return (NULL);

	info.page_count = howmany(size, PAGE_SIZE);

	CUSE_LOCK();
	for (n = 0; n != CUSE_ALLOC_UNIT_MAX; n++) {

		if (a_cuse[n].ptr != NULL)
			continue;

		a_cuse[n].ptr = ((uint8_t *)1);	/* reserve */
		a_cuse[n].size = 0;

		CUSE_UNLOCK();

		info.alloc_nr = n;

		error = ioctl(f_cuse, CUSE_IOCTL_ALLOC_MEMORY, &info);

		if (error) {

			CUSE_LOCK();

			a_cuse[n].ptr = NULL;

			if (errno == EBUSY)
				continue;
			else
				break;
		}
		ptr = mmap(NULL, info.page_count * PAGE_SIZE,
		    PROT_READ | PROT_WRITE,
		    MAP_SHARED, f_cuse, CUSE_ALLOC_PAGES_MAX *
		    PAGE_SIZE * n);

		if (ptr == MAP_FAILED) {

			error = ioctl(f_cuse, CUSE_IOCTL_FREE_MEMORY, &info);

			if (error) {
				/* ignore */
			}
			CUSE_LOCK();

			a_cuse[n].ptr = NULL;

			break;
		}
		CUSE_LOCK();
		a_cuse[n].ptr = ptr;
		a_cuse[n].size = size;
		CUSE_UNLOCK();

		return (ptr);		/* success */
	}
	CUSE_UNLOCK();
	return (NULL);			/* failure */
}

int
cuse_is_vmalloc_addr(void *ptr)
{
	int n;

	if (f_cuse < 0 || ptr == NULL)
		return (0);		/* false */

	CUSE_LOCK();
	for (n = 0; n != CUSE_ALLOC_UNIT_MAX; n++) {
		if (a_cuse[n].ptr == ptr)
			break;
	}
	CUSE_UNLOCK();

	return (n != CUSE_ALLOC_UNIT_MAX);
}

void
cuse_vmfree(void *ptr)
{
	struct cuse_vm_allocation temp;
	struct cuse_alloc_info info;
	int error;
	int n;

	if (f_cuse < 0)
		return;

	CUSE_LOCK();
	for (n = 0; n != CUSE_ALLOC_UNIT_MAX; n++) {
		if (a_cuse[n].ptr != ptr)
			continue;

		temp = a_cuse[n];

		CUSE_UNLOCK();

		munmap(temp.ptr, temp.size);

		memset(&info, 0, sizeof(info));

		info.alloc_nr = n;

		error = ioctl(f_cuse, CUSE_IOCTL_FREE_MEMORY, &info);

		if (error != 0) {
			/* ignore any errors */
			DPRINTF("Freeing memory failed: %d\n", errno);
		}
		CUSE_LOCK();

		a_cuse[n].ptr = NULL;
		a_cuse[n].size = 0;

		break;
	}
	CUSE_UNLOCK();
}

int
cuse_alloc_unit_number_by_id(int *pnum, int id)
{
	int error;

	if (f_cuse < 0)
		return (CUSE_ERR_INVALID);

	*pnum = (id & CUSE_ID_MASK);

	error = ioctl(f_cuse, CUSE_IOCTL_ALLOC_UNIT_BY_ID, pnum);
	if (error)
		return (CUSE_ERR_NO_MEMORY);

	return (0);

}

int
cuse_free_unit_number_by_id(int num, int id)
{
	int error;

	if (f_cuse < 0)
		return (CUSE_ERR_INVALID);

	if (num != -1 || id != -1)
		num = (id & CUSE_ID_MASK) | (num & 0xFF);

	error = ioctl(f_cuse, CUSE_IOCTL_FREE_UNIT_BY_ID, &num);
	if (error)
		return (CUSE_ERR_NO_MEMORY);

	return (0);
}

int
cuse_alloc_unit_number(int *pnum)
{
	int error;

	if (f_cuse < 0)
		return (CUSE_ERR_INVALID);

	error = ioctl(f_cuse, CUSE_IOCTL_ALLOC_UNIT, pnum);
	if (error)
		return (CUSE_ERR_NO_MEMORY);

	return (0);
}

int
cuse_free_unit_number(int num)
{
	int error;

	if (f_cuse < 0)
		return (CUSE_ERR_INVALID);

	error = ioctl(f_cuse, CUSE_IOCTL_FREE_UNIT, &num);
	if (error)
		return (CUSE_ERR_NO_MEMORY);

	return (0);
}

struct cuse_dev *
cuse_dev_create(const struct cuse_methods *mtod, void *priv0, void *priv1,
    uid_t _uid, gid_t _gid, int _perms, const char *_fmt,...)
{
	struct cuse_create_dev info;
	struct cuse_dev *cdev;
	va_list args;
	int error;

	if (f_cuse < 0)
		return (NULL);

	cdev = malloc(sizeof(*cdev));
	if (cdev == NULL)
		return (NULL);

	memset(cdev, 0, sizeof(*cdev));

	cdev->mtod = mtod;
	cdev->priv0 = priv0;
	cdev->priv1 = priv1;

	memset(&info, 0, sizeof(info));

	info.dev = cdev;
	info.user_id = _uid;
	info.group_id = _gid;
	info.permissions = _perms;

	va_start(args, _fmt);
	vsnprintf(info.devname, sizeof(info.devname), _fmt, args);
	va_end(args);

	error = ioctl(f_cuse, CUSE_IOCTL_CREATE_DEV, &info);
	if (error) {
		free(cdev);
		return (NULL);
	}
	CUSE_LOCK();
	TAILQ_INSERT_TAIL(&h_cuse, cdev, entry);
	CUSE_UNLOCK();

	return (cdev);
}


void
cuse_dev_destroy(struct cuse_dev *cdev)
{
	int error;

	if (f_cuse < 0)
		return;

	CUSE_LOCK();
	TAILQ_REMOVE(&h_cuse, cdev, entry);
	CUSE_UNLOCK();

	error = ioctl(f_cuse, CUSE_IOCTL_DESTROY_DEV, &cdev);
	if (error)
		return;

	free(cdev);
}

void   *
cuse_dev_get_priv0(struct cuse_dev *cdev)
{
	return (cdev->priv0);
}

void   *
cuse_dev_get_priv1(struct cuse_dev *cdev)
{
	return (cdev->priv1);
}

void
cuse_dev_set_priv0(struct cuse_dev *cdev, void *priv)
{
	cdev->priv0 = priv;
}

void
cuse_dev_set_priv1(struct cuse_dev *cdev, void *priv)
{
	cdev->priv1 = priv;
}

int
cuse_wait_and_process(void)
{
	pthread_t curr = pthread_self();
	struct cuse_dev_entered *pe;
	struct cuse_dev_entered enter;
	struct cuse_command info;
	struct cuse_dev *cdev;
	int error;

	if (f_cuse < 0)
		return (CUSE_ERR_INVALID);

	error = ioctl(f_cuse, CUSE_IOCTL_GET_COMMAND, &info);
	if (error)
		return (CUSE_ERR_OTHER);

	cdev = info.dev;

	CUSE_LOCK();
	enter.thread = curr;
	enter.per_file_handle = (void *)info.per_file_handle;
	enter.cmd = info.command;
	enter.is_local = 0;
	enter.got_signal = 0;
	enter.cdev = cdev;
	TAILQ_INSERT_TAIL(&h_cuse_entered, &enter, entry);
	CUSE_UNLOCK();

	DPRINTF("cuse: Command = %d = %s, flags = %d, arg = 0x%08x, ptr = 0x%08x\n",
	    (int)info.command, cuse_cmd_str(info.command), (int)info.fflags,
	    (int)info.argument, (int)info.data_pointer);

	switch (info.command) {
	case CUSE_CMD_OPEN:
		if (cdev->mtod->cm_open != NULL)
			error = (cdev->mtod->cm_open) (cdev, (int)info.fflags);
		else
			error = 0;
		break;

	case CUSE_CMD_CLOSE:

		/* wait for other threads to stop */

		while (1) {

			error = 0;

			CUSE_LOCK();
			TAILQ_FOREACH(pe, &h_cuse_entered, entry) {
				if (pe->cdev != cdev)
					continue;
				if (pe->thread == curr)
					continue;
				if (pe->per_file_handle !=
				    enter.per_file_handle)
					continue;
				pe->got_signal = 1;
				pthread_kill(pe->thread, SIGHUP);
				error = CUSE_ERR_BUSY;
			}
			CUSE_UNLOCK();

			if (error == 0)
				break;
			else
				usleep(10000);
		}

		if (cdev->mtod->cm_close != NULL)
			error = (cdev->mtod->cm_close) (cdev, (int)info.fflags);
		else
			error = 0;
		break;

	case CUSE_CMD_READ:
		if (cdev->mtod->cm_read != NULL) {
			error = (cdev->mtod->cm_read) (cdev, (int)info.fflags,
			    (void *)info.data_pointer, (int)info.argument);
		} else {
			error = CUSE_ERR_INVALID;
		}
		break;

	case CUSE_CMD_WRITE:
		if (cdev->mtod->cm_write != NULL) {
			error = (cdev->mtod->cm_write) (cdev, (int)info.fflags,
			    (void *)info.data_pointer, (int)info.argument);
		} else {
			error = CUSE_ERR_INVALID;
		}
		break;

	case CUSE_CMD_IOCTL:
		if (cdev->mtod->cm_ioctl != NULL) {
			error = (cdev->mtod->cm_ioctl) (cdev, (int)info.fflags,
			    (unsigned int)info.argument, (void *)info.data_pointer);
		} else {
			error = CUSE_ERR_INVALID;
		}
		break;

	case CUSE_CMD_POLL:
		if (cdev->mtod->cm_poll != NULL) {
			error = (cdev->mtod->cm_poll) (cdev, (int)info.fflags,
			    (int)info.argument);
		} else {
			error = CUSE_POLL_ERROR;
		}
		break;

	case CUSE_CMD_SIGNAL:
		CUSE_LOCK();
		TAILQ_FOREACH(pe, &h_cuse_entered, entry) {
			if (pe->cdev != cdev)
				continue;
			if (pe->thread == curr)
				continue;
			if (pe->per_file_handle !=
			    enter.per_file_handle)
				continue;
			pe->got_signal = 1;
			pthread_kill(pe->thread, SIGHUP);
		}
		CUSE_UNLOCK();
		break;

	default:
		error = CUSE_ERR_INVALID;
		break;
	}

	DPRINTF("cuse: Command error = %d for %s\n",
	    error, cuse_cmd_str(info.command));

	CUSE_LOCK();
	TAILQ_REMOVE(&h_cuse_entered, &enter, entry);
	CUSE_UNLOCK();

	/* we ignore any sync command failures */
	ioctl(f_cuse, CUSE_IOCTL_SYNC_COMMAND, &error);

	return (0);
}

static struct cuse_dev_entered *
cuse_dev_get_entered(void)
{
	struct cuse_dev_entered *pe;
	pthread_t curr = pthread_self();

	CUSE_LOCK();
	TAILQ_FOREACH(pe, &h_cuse_entered, entry) {
		if (pe->thread == curr)
			break;
	}
	CUSE_UNLOCK();
	return (pe);
}

void
cuse_dev_set_per_file_handle(struct cuse_dev *cdev, void *handle)
{
	struct cuse_dev_entered *pe;

	pe = cuse_dev_get_entered();
	if (pe == NULL || pe->cdev != cdev)
		return;

	pe->per_file_handle = handle;
	ioctl(f_cuse, CUSE_IOCTL_SET_PFH, &handle);
}

void   *
cuse_dev_get_per_file_handle(struct cuse_dev *cdev)
{
	struct cuse_dev_entered *pe;

	pe = cuse_dev_get_entered();
	if (pe == NULL || pe->cdev != cdev)
		return (NULL);

	return (pe->per_file_handle);
}

void
cuse_set_local(int val)
{
	struct cuse_dev_entered *pe;

	pe = cuse_dev_get_entered();
	if (pe == NULL)
		return;

	pe->is_local = val;
}

#ifdef HAVE_DEBUG
static const char *
cuse_cmd_str(int cmd)
{
	static const char *str[CUSE_CMD_MAX] = {
		[CUSE_CMD_NONE] = "none",
		[CUSE_CMD_OPEN] = "open",
		[CUSE_CMD_CLOSE] = "close",
		[CUSE_CMD_READ] = "read",
		[CUSE_CMD_WRITE] = "write",
		[CUSE_CMD_IOCTL] = "ioctl",
		[CUSE_CMD_POLL] = "poll",
		[CUSE_CMD_SIGNAL] = "signal",
		[CUSE_CMD_SYNC] = "sync",
	};

	if ((cmd >= 0) && (cmd < CUSE_CMD_MAX) &&
	    (str[cmd] != NULL))
		return (str[cmd]);
	else
		return ("unknown");
}

#endif

int
cuse_get_local(void)
{
	struct cuse_dev_entered *pe;

	pe = cuse_dev_get_entered();
	if (pe == NULL)
		return (0);

	return (pe->is_local);
}

int
cuse_copy_out(const void *src, void *user_dst, int len)
{
	struct cuse_data_chunk info;
	struct cuse_dev_entered *pe;
	int error;

	if ((f_cuse < 0) || (len < 0))
		return (CUSE_ERR_INVALID);

	pe = cuse_dev_get_entered();
	if (pe == NULL)
		return (CUSE_ERR_INVALID);

	DPRINTF("cuse: copy_out(%p,%p,%d), cmd = %d = %s\n",
	    src, user_dst, len, pe->cmd, cuse_cmd_str(pe->cmd));

	if (pe->is_local) {
		memcpy(user_dst, src, len);
	} else {
		info.local_ptr = (uintptr_t)src;
		info.peer_ptr = (uintptr_t)user_dst;
		info.length = len;

		error = ioctl(f_cuse, CUSE_IOCTL_WRITE_DATA, &info);
		if (error) {
			DPRINTF("cuse: copy_out() error = %d\n", errno);
			return (CUSE_ERR_FAULT);
		}
	}
	return (0);
}

int
cuse_copy_in(const void *user_src, void *dst, int len)
{
	struct cuse_data_chunk info;
	struct cuse_dev_entered *pe;
	int error;

	if ((f_cuse < 0) || (len < 0))
		return (CUSE_ERR_INVALID);

	pe = cuse_dev_get_entered();
	if (pe == NULL)
		return (CUSE_ERR_INVALID);

	DPRINTF("cuse: copy_in(%p,%p,%d), cmd = %d = %s\n",
	    user_src, dst, len, pe->cmd, cuse_cmd_str(pe->cmd));

	if (pe->is_local) {
		memcpy(dst, user_src, len);
	} else {
		info.local_ptr = (uintptr_t)dst;
		info.peer_ptr = (uintptr_t)user_src;
		info.length = len;

		error = ioctl(f_cuse, CUSE_IOCTL_READ_DATA, &info);
		if (error) {
			DPRINTF("cuse: copy_in() error = %d\n", errno);
			return (CUSE_ERR_FAULT);
		}
	}
	return (0);
}

struct cuse_dev *
cuse_dev_get_current(int *pcmd)
{
	struct cuse_dev_entered *pe;

	pe = cuse_dev_get_entered();
	if (pe == NULL) {
		if (pcmd != NULL)
			*pcmd = 0;
		return (NULL);
	}
	if (pcmd != NULL)
		*pcmd = pe->cmd;
	return (pe->cdev);
}

int
cuse_got_peer_signal(void)
{
	struct cuse_dev_entered *pe;

	pe = cuse_dev_get_entered();
	if (pe == NULL)
		return (CUSE_ERR_INVALID);

	if (pe->got_signal)
		return (0);

	return (CUSE_ERR_OTHER);
}

void
cuse_poll_wakeup(void)
{
	int error = 0;

	if (f_cuse < 0)
		return;

	ioctl(f_cuse, CUSE_IOCTL_SELWAKEUP, &error);
}
