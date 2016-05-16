#include <linux/compiler.h>
#include <linux/kernel.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "data.h"
#include "util.h"
#include "debug.h"

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
	char sbuf[STRERR_BUFSIZE];

	fd = open(file->path, O_RDONLY);
	if (fd < 0) {
		int err = errno;

		pr_err("failed to open %s: %s", file->path,
			strerror_r(err, sbuf, sizeof(sbuf)));
		if (err == ENOENT && !strcmp(file->path, "perf.data"))
			pr_err("  (try 'perf record' first)");
		pr_err("\n");
		return -err;
	}

	if (fstat(fd, &st) < 0)
		goto out_close;

	if (!file->force && st.st_uid && (st.st_uid != geteuid())) {
		pr_err("File %s not owned by current user or root (use -f to override)\n",
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
	int fd;
	char sbuf[STRERR_BUFSIZE];

	if (check_backup(file))
		return -1;

	fd = open(file->path, O_CREAT|O_RDWR|O_TRUNC, S_IRUSR|S_IWUSR);

	if (fd < 0)
		pr_err("failed to open %s : %s\n", file->path,
			strerror_r(errno, sbuf, sizeof(sbuf)));

	return fd;
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

int perf_data_file__switch(struct perf_data_file *file,
			   const char *postfix,
			   size_t pos, bool at_exit)
{
	char *new_filepath;
	int ret;

	if (check_pipe(file))
		return -EINVAL;
	if (perf_data_file__is_read(file))
		return -EINVAL;

	if (asprintf(&new_filepath, "%s.%s", file->path, postfix) < 0)
		return -ENOMEM;

	/*
	 * Only fire a warning, don't return error, continue fill
	 * original file.
	 */
	if (rename(file->path, new_filepath))
		pr_warning("Failed to rename %s to %s\n", file->path, new_filepath);

	if (!at_exit) {
		close(file->fd);
		ret = perf_data_file__open(file);
		if (ret < 0)
			goto out;

		if (lseek(file->fd, pos, SEEK_SET) == (off_t)-1) {
			ret = -errno;
			pr_debug("Failed to lseek to %zu: %s",
				 pos, strerror(errno));
			goto out;
		}
	}
	ret = file->fd;
out:
	free(new_filepath);
	return ret;
}
