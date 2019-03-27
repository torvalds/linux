/*-
 * Copyright (c) 2011 Google, Inc.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Read from the host filesystem
 */

#include <sys/param.h>
#include <sys/time.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <stand.h>
#include <bootstrap.h>

#include "libuserboot.h"

/*
 * Open a file.
 */
static int
host_open(const char *upath, struct open_file *f)
{

	if (f->f_dev != &host_dev)
		return (EINVAL);

	return (CALLBACK(open, upath, &f->f_fsdata));
}

static int
host_close(struct open_file *f)
{

        CALLBACK(close, f->f_fsdata);
	f->f_fsdata = (void *)0;

	return (0);
}

/*
 * Copy a portion of a file into memory.
 */
static int
host_read(struct open_file *f, void *start, size_t size, size_t *resid)
{

	return (CALLBACK(read, f->f_fsdata, start, size, resid));
}

static off_t
host_seek(struct open_file *f, off_t offset, int where)
{

	return (CALLBACK(seek, f->f_fsdata, offset, where));
}

static int
host_stat(struct open_file *f, struct stat *sb)
{
	int mode;
	int uid;
	int gid;
	uint64_t size;

	CALLBACK(stat, f->f_fsdata, &mode, &uid, &gid, &size);
	sb->st_mode = mode;
	sb->st_uid = uid;
	sb->st_gid = gid;
	sb->st_size = size;
	return (0);
}

static int
host_readdir(struct open_file *f, struct dirent *d)
{
	uint32_t fileno;
	uint8_t type;
	size_t namelen;
	int rc;

	rc = CALLBACK(readdir, f->f_fsdata, &fileno, &type, &namelen,
            d->d_name);
	if (rc)
		return (rc);

	d->d_fileno = fileno;
	d->d_type = type;
	d->d_namlen = namelen;

	return (0);
}

static int
host_dev_init(void)
{

	return (0);
}

static int
host_dev_print(int verbose)
{
	char line[80];

	printf("%s devices:", host_dev.dv_name);
	if (pager_output("\n") != 0)
		return (1);

	snprintf(line, sizeof(line), "    host%d:   Host filesystem\n", 0);
	return (pager_output(line));
}

/*
 * 'Open' the host device.
 */
static int
host_dev_open(struct open_file *f, ...)
{
	va_list		args;
	struct devdesc	*dev;

	va_start(args, f);
	dev = va_arg(args, struct devdesc*);
	va_end(args);

	return (0);
}

static int
host_dev_close(struct open_file *f)
{

	return (0);
}

static int
host_dev_strategy(void *devdata, int rw, daddr_t dblk, size_t size,
    char *buf, size_t *rsize)
{

	return (ENOSYS);
}

struct fs_ops host_fsops = {
	"host",
	host_open,
	host_close,
	host_read,
	null_write,
	host_seek,
	host_stat,
	host_readdir
};

struct devsw host_dev = {
	.dv_name = "host",
	.dv_type = DEVT_NET,
	.dv_init = host_dev_init,
	.dv_strategy = host_dev_strategy,
	.dv_open = host_dev_open,
	.dv_close = host_dev_close,
	.dv_ioctl = noioctl,
	.dv_print = host_dev_print,
	.dv_cleanup = NULL
};
