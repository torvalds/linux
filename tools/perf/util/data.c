#include <linux/compiler.h>
#include <linux/kernel.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "data.h"
#include "util.h"

static bool check_pipe(struct perf_data_file *file)
{
	struct stat st;
	bool is_pipe = false;
	int fd = perf_data_file__is_read(file) ?
		 STDIN_FILENO : STDOUT_FILENO;

	if (!file->path) {
		if (!fstat(fd, &st) && S_ISFIFO(st.st_mode))
			is_pipe = true;
	} else {
		if (!strcmp(file->path, "-"))
			is_pipe = true;
	}

	if (is_pipe)
		file->fd = fd;

	return file->is_pipe = is_pipe;
}

static int check_backup(struct perf_data_file *file)
{
	struct stat st;

	if (!stat(file->path, &st) && st.st_size) {
		/* TODO check errors properly */
		char oldname[PATH_MAX];
		snprintf(oldname, sizeof(oldname), "%s.old",
			 file->path);
		unlink(oldname);
		rename(file->path, oldname);
	}

	return 0;
}

static int open_file_read(struct perf_data_file *file)
{
	struct stat st;
	int fd;

	fd = open(file->path, O_RDONLY);
	if (fd < 0) {
		int err = errno;

		pr_err("failed to open %s: %s", file->path, strerror(err));
		if (err == ENOENT && !strcmp(file->path, "perf.data"))
			pr_err("  (try 'perf record' first)");
		pr_err("\n");
		return -err;
	}

	if (fstat(fd, &st) < 0)
		goto out_close;

	if (!file->force && st.st_uid && (st.st_uid != geteuid())) {
		pr_err("file %s not owned by current user or root\n",
		       file->path);
		goto out_close;
	}

	if (!st.st_size) {
		pr_info("zero-sized file (%s), nothing to do!\n",
			file->path);
		goto out_close;
	}

	file->size = st.st_size;
	return fd;

 out_close:
	close(fd);
	return -1;
}

static int open_file_write(struct perf_data_file *file)
{
	if (check_backup(file))
		return -1;

	return open(file->path, O_CREAT|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR);
}

static int open_file(struct perf_data_file *file)
{
	int fd;

	fd = perf_data_file__is_read(file) ?
	     open_file_read(file) : open_file_write(file);

	file->fd = fd;
	return fd < 0 ? -1 : 0;
}

int perf_data_file__open(struct perf_data_file *file)
{
	if (check_pipe(file))
		return 0;

	if (!file->path)
		file->path = "perf.data";

	return open_file(file);
}

void perf_data_file__close(struct perf_data_file *file)
{
	close(file->fd);
}

ssize_t perf_data_file__write(struct perf_data_file *file,
			      void *buf, size_t size)
{
	return writen(file->fd, buf, size);
}
