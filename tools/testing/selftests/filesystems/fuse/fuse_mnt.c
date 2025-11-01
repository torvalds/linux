// SPDX-License-Identifier: GPL-2.0
/*
 * fusectl test file-system
 * Creates a simple FUSE filesystem with a single read-write file (/test)
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#define MAX(a, b) ((a) > (b) ? (a) : (b))

static char *content;
static size_t content_size = 0;
static const char test_path[] = "/test";

static int test_getattr(const char *path, struct stat *st)
{
	memset(st, 0, sizeof(*st));

	if (!strcmp(path, "/")) {
		st->st_mode = S_IFDIR | 0755;
		st->st_nlink = 2;
		return 0;
	}

	if (!strcmp(path, test_path)) {
		st->st_mode = S_IFREG | 0664;
		st->st_nlink = 1;
		st->st_size = content_size;
		return 0;
	}

	return -ENOENT;
}

static int test_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
			off_t offset, struct fuse_file_info *fi)
{
	if (strcmp(path, "/"))
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);
	filler(buf, test_path + 1, NULL, 0);

	return 0;
}

static int test_open(const char *path, struct fuse_file_info *fi)
{
	if (strcmp(path, test_path))
		return -ENOENT;

	return 0;
}

static int test_read(const char *path, char *buf, size_t size, off_t offset,
		     struct fuse_file_info *fi)
{
	if (strcmp(path, test_path) != 0)
		return -ENOENT;

	if (!content || content_size == 0)
		return 0;

	if (offset >= content_size)
		return 0;

	if (offset + size > content_size)
		size = content_size - offset;

	memcpy(buf, content + offset, size);

	return size;
}

static int test_write(const char *path, const char *buf, size_t size,
		      off_t offset, struct fuse_file_info *fi)
{
	size_t new_size;

	if (strcmp(path, test_path) != 0)
		return -ENOENT;

	if(offset > content_size)
		return -EINVAL;

	new_size = MAX(offset + size, content_size);

	if (new_size > content_size)
		content = realloc(content, new_size);

	content_size = new_size;

	if (!content)
		return -ENOMEM;

	memcpy(content + offset, buf, size);

	return size;
}

static int test_truncate(const char *path, off_t size)
{
	if (strcmp(path, test_path) != 0)
		return -ENOENT;

	if (size == 0) {
		free(content);
		content = NULL;
		content_size = 0;
		return 0;
	}

	content = realloc(content, size);

	if (!content)
		return -ENOMEM;

	if (size > content_size)
		memset(content + content_size, 0, size - content_size);

	content_size = size;
	return 0;
}

static struct fuse_operations memfd_ops = {
	.getattr = test_getattr,
	.readdir = test_readdir,
	.open = test_open,
	.read = test_read,
	.write = test_write,
	.truncate = test_truncate,
};

int main(int argc, char *argv[])
{
	return fuse_main(argc, argv, &memfd_ops, NULL);
}
