/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994 Christos Zoulas
 * Copyright (c) 1995 Frank van der Linden
 * Copyright (c) 1995 Scott Bartram
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	from: svr4_util.c,v 1.5 1995/01/22 23:44:50 christos Exp
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/fcntl.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/linker_set.h>
#include <sys/mutex.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/sdt.h>
#include <sys/syscallsubr.h>
#include <sys/systm.h>
#include <sys/vnode.h>

#include <machine/stdarg.h>

#include <compat/linux/linux_util.h>

MALLOC_DEFINE(M_LINUX, "linux", "Linux mode structures");
MALLOC_DEFINE(M_EPOLL, "lepoll", "Linux events structures");
MALLOC_DEFINE(M_FUTEX, "futex", "Linux futexes");
MALLOC_DEFINE(M_FUTEX_WP, "futex wp", "Linux futex waiting proc");

const char      linux_emul_path[] = "/compat/linux";

/*
 * Search an alternate path before passing pathname arguments on to
 * system calls. Useful for keeping a separate 'emulation tree'.
 *
 * If cflag is set, we check if an attempt can be made to create the
 * named file, i.e. we check if the directory it should be in exists.
 */
int
linux_emul_convpath(struct thread *td, const char *path, enum uio_seg pathseg,
    char **pbuf, int cflag, int dfd)
{
	int retval;

	retval = kern_alternate_path(td, linux_emul_path, path, pathseg, pbuf,
	    cflag, dfd);

	return (retval);
}

void
linux_msg(const struct thread *td, const char *fmt, ...)
{
	va_list ap;
	struct proc *p;

	p = td->td_proc;
	printf("linux: pid %d (%s): ", (int)p->p_pid, p->p_comm);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}

struct device_element
{
	TAILQ_ENTRY(device_element) list;
	struct linux_device_handler entry;
};

static TAILQ_HEAD(, device_element) devices =
	TAILQ_HEAD_INITIALIZER(devices);

static struct linux_device_handler null_handler =
	{ "mem", "mem", "null", "null", 1, 3, 1};

DATA_SET(linux_device_handler_set, null_handler);

char *
linux_driver_get_name_dev(device_t dev)
{
	struct device_element *de;
	const char *device_name = device_get_name(dev);

	if (device_name == NULL)
		return (NULL);
	TAILQ_FOREACH(de, &devices, list) {
		if (strcmp(device_name, de->entry.bsd_driver_name) == 0)
			return (de->entry.linux_driver_name);
	}

	return (NULL);
}

int
linux_driver_get_major_minor(const char *node, int *major, int *minor)
{
	struct device_element *de;
	unsigned long devno;
	size_t sz;

	if (node == NULL || major == NULL || minor == NULL)
		return (1);

	sz = sizeof("pts/") - 1;
	if (strncmp(node, "pts/", sz) == 0 && node[sz] != '\0') {
		/*
		 * Linux checks major and minors of the slave device
		 * to make sure it's a pty device, so let's make him
		 * believe it is.
		 */
		devno = strtoul(node + sz, NULL, 10);
		*major = 136 + (devno / 256);
		*minor = devno % 256;
		return (0);
	}

	sz = sizeof("dri/card") - 1;
	if (strncmp(node, "dri/card", sz) == 0 && node[sz] != '\0') {
		devno = strtoul(node + sz, NULL, 10);
		*major = 226 + (devno / 256);
		*minor = devno % 256;
		return (0);
	}
	sz = sizeof("dri/controlD") - 1;
	if (strncmp(node, "dri/controlD", sz) == 0 && node[sz] != '\0') {
		devno = strtoul(node + sz, NULL, 10);
		*major = 226 + (devno / 256);
		*minor = devno % 256;
		return (0);
	}
	sz = sizeof("dri/renderD") - 1;
	if (strncmp(node, "dri/renderD", sz) == 0 && node[sz] != '\0') {
		devno = strtoul(node + sz, NULL, 10);
		*major = 226 + (devno / 256);
		*minor = devno % 256;
		return (0);
	}
	sz = sizeof("drm/") - 1;
	if (strncmp(node, "drm/", sz) == 0 && node[sz] != '\0') {
		devno = strtoul(node + sz, NULL, 10);
		*major = 226 + (devno / 256);
		*minor = devno % 256;
		return (0);
	}

	TAILQ_FOREACH(de, &devices, list) {
		if (strcmp(node, de->entry.bsd_device_name) == 0) {
			*major = de->entry.linux_major;
			*minor = de->entry.linux_minor;
			return (0);
		}
	}

	return (1);
}

char *
linux_get_char_devices()
{
	struct device_element *de;
	char *temp, *string, *last;
	char formated[256];
	int current_size = 0, string_size = 1024;

	string = malloc(string_size, M_LINUX, M_WAITOK);
	string[0] = '\000';
	last = "";
	TAILQ_FOREACH(de, &devices, list) {
		if (!de->entry.linux_char_device)
			continue;
		temp = string;
		if (strcmp(last, de->entry.bsd_driver_name) != 0) {
			last = de->entry.bsd_driver_name;

			snprintf(formated, sizeof(formated), "%3d %s\n",
				 de->entry.linux_major,
				 de->entry.linux_device_name);
			if (strlen(formated) + current_size
			    >= string_size) {
				string_size *= 2;
				string = malloc(string_size,
				    M_LINUX, M_WAITOK);
				bcopy(temp, string, current_size);
				free(temp, M_LINUX);
			}
			strcat(string, formated);
			current_size = strlen(string);
		}
	}

	return (string);
}

void
linux_free_get_char_devices(char *string)
{

	free(string, M_LINUX);
}

static int linux_major_starting = 200;

int
linux_device_register_handler(struct linux_device_handler *d)
{
	struct device_element *de;

	if (d == NULL)
		return (EINVAL);

	de = malloc(sizeof(*de), M_LINUX, M_WAITOK);
	if (d->linux_major < 0) {
		d->linux_major = linux_major_starting++;
	}
	bcopy(d, &de->entry, sizeof(*d));

	/* Add the element to the list, sorted on span. */
	TAILQ_INSERT_TAIL(&devices, de, list);

	return (0);
}

int
linux_device_unregister_handler(struct linux_device_handler *d)
{
	struct device_element *de;

	if (d == NULL)
		return (EINVAL);

	TAILQ_FOREACH(de, &devices, list) {
		if (bcmp(d, &de->entry, sizeof(*d)) == 0) {
			TAILQ_REMOVE(&devices, de, list);
			free(de, M_LINUX);

			return (0);
		}
	}

	return (EINVAL);
}
