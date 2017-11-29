// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include "data.h"
#include "util.h"
#include "debug.h"

#ifndef O_CLOEXEC
#ifdef __sparc__
#define O_CLOEXEC	0x400000
#elif defined(__alpha__) || defined(__hppa__)
#define O_CLOEXEC	010000000
#else
#define O_CLOEXEC	02000000
#endif
#endif

static bool check_pipe(struct perf_data *data)
{
	struct stat st;
	bool is_pipe = false;
	int fd = perf_data__is_read(data) ?
		 STDIN_FILENO : STDOUT_FILENO;

	if (!data->file.path) {
		if (!fstat(fd, &st) && S_ISFIFO(st.st_mode))
			is_pipe = true;
	} else {
		if (!strcmp(data->file.path, "-"))
			is_pipe = true;
	}

	if (is_pipe)
		data->file.fd = fd;

	return data->is_pipe = is_pipe;
}

static int check_backup(struct perf_data *data)
{
	struct stat st;

	if (!stat(data->file.path, &st) && st.st_size) {
		/* TODO check errors properly */
		char oldname[PATH_MAX];
		snprintf(oldname, sizeof(oldname), "%s.old",
			 data->file.path);
		unlink(oldname);
		rename(data->file.path, oldname);
	}

	return 0;
}

static int open_file_read(struct perf_data *data)
{
	struct stat st;
	int fd;
	char sbuf[STRERR_BUFSIZE];

	fd = open(data->file.path, O_RDONLY);
	if (fd < 0) {
		int err = errno;

		pr_err("failed to open %s: %s", data->file.path,
			str_error_r(err, sbuf, sizeof(sbuf)));
		if (err == ENOENT && !strcmp(data->file.path, "perf.data"))
			pr_err("  (try 'perf record' first)");
		pr_err("\n");
		return -err;
	}

	if (fstat(fd, &st) < 0)
		goto out_close;

	if (!data->force && st.st_uid && (st.st_uid != geteuid())) {
		pr_err("File %s not owned by current user or root (use -f to override)\n",
		       data->file.path);
		goto out_close;
	}

	if (!st.st_size) {
		pr_info("zero-sized data (%s), nothing to do!\n",
			data->file.path);
		goto out_close;
	}

	data->size = st.st_size;
	return fd;

 out_close:
	close(fd);
	return -1;
}

static int open_file_write(struct perf_data *data)
{
	int fd;
	char sbuf[STRERR_BUFSIZE];

	if (check_backup(data))
		return -1;

	fd = open(data->file.path, O_CREAT|O_RDWR|O_TRUNC|O_CLOEXEC,
		  S_IRUSR|S_IWUSR);

	if (fd < 0)
		pr_err("failed to open %s : %s\n", data->file.path,
			str_error_r(errno, sbuf, sizeof(sbuf)));

	return fd;
}

static int open_file(struct perf_data *data)
{
	int fd;

	fd = perf_data__is_read(data) ?
	     open_file_read(data) : open_file_write(data);

	data->file.fd = fd;
	return fd < 0 ? -1 : 0;
}

int perf_data__open(struct perf_data *data)
{
	if (check_pipe(data))
		return 0;

	if (!data->file.path)
		data->file.path = "perf.data";

	return open_file(data);
}

void perf_data__close(struct perf_data *data)
{
	close(data->file.fd);
}

ssize_t perf_data_file__write(struct perf_data_file *file,
			      void *buf, size_t size)
{
	return writen(file->fd, buf, size);
}

ssize_t perf_data__write(struct perf_data *data,
			      void *buf, size_t size)
{
	return perf_data_file__write(&data->file, buf, size);
}

int perf_data__switch(struct perf_data *data,
			   const char *postfix,
			   size_t pos, bool at_exit)
{
	char *new_filepath;
	int ret;

	if (check_pipe(data))
		return -EINVAL;
	if (perf_data__is_read(data))
		return -EINVAL;

	if (asprintf(&new_filepath, "%s.%s", data->file.path, postfix) < 0)
		return -ENOMEM;

	/*
	 * Only fire a warning, don't return error, continue fill
	 * original file.
	 */
	if (rename(data->file.path, new_filepath))
		pr_warning("Failed to rename %s to %s\n", data->file.path, new_filepath);

	if (!at_exit) {
		close(data->file.fd);
		ret = perf_data__open(data);
		if (ret < 0)
			goto out;

		if (lseek(data->file.fd, pos, SEEK_SET) == (off_t)-1) {
			ret = -errno;
			pr_debug("Failed to lseek to %zu: %s",
				 pos, strerror(errno));
			goto out;
		}
	}
	ret = data->file.fd;
out:
	free(new_filepath);
	return ret;
}
