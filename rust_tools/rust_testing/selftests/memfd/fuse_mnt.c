// SPDX-License-Identifier: GPL-2.0
/*
 * memfd test file-system
 * This file uses FUSE to create a dummy file-system with only one file /memfd.
 * This file is read-only and takes 1s per read.
 *
 * This file-system is used by the memfd test-cases to force the kernel to pin
 * pages during reads(). Due to the 1s delay of this file-system, this is a
 * nice way to test race-conditions against get_user_pages() in the kernel.
 *
 * We use direct_io==1 to force the kernel to use direct-IO for this
 * file-system.
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static const char memfd_content[] = "memfd-example-content";
static const char memfd_path[] = "/memfd";

static int memfd_getattr(const char *path, struct stat *st)
{
	memset(st, 0, sizeof(*st));

	if (!strcmp(path, "/")) {
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2;
	} else if (!strcmp(path, memfd_path)) {
		st->st_mode = S_IFREG | 0444;
		st->st_nlink = 1;
		st->st_size = strlen(memfd_content);
	} else {
		return -ENOENT;
	}

	return 0;
}

static int memfd_readdir(const char *path,
			 void *buf,
			 fuse_fill_dir_t filler,
			 off_t offset,
			 struct fuse_file_info *fi)
{
	if (strcmp(path, "/"))
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, memfd_path + 1, NULL, 0);

	return 0;
}

static int memfd_open(const char *path, struct fuse_file_info *fi)
{
	if (strcmp(path, memfd_path))
		return -ENOENT;

	if ((fi->flags & 3) != O_RDONLY)
		return -EACCES;

	/* force direct-IO */
	fi->direct_io = 1;

	return 0;
}

static int memfd_read(const char *path,
		      char *buf,
		      size_t size,
		      off_t offset,
		      struct fuse_file_info *fi)
{
	size_t len;

	if (strcmp(path, memfd_path) != 0)
		return -ENOENT;

	sleep(1);

	len = strlen(memfd_content);
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;

		memcpy(buf, memfd_content + offset, size);
	} else {
		size = 0;
	}

	return size;
}

static struct fuse_operations memfd_ops = {
	.getattr	= memfd_getattr,
	.readdir	= memfd_readdir,
	.open		= memfd_open,
	.read		= memfd_read,
};

int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &memfd_ops, NULL);
}
