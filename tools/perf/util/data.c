// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/zalloc.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <asm/bug.h>
#include <dirent.h>

#include "data.h"
#include "util.h" // rm_rf_perf_data()
#include "debug.h"
#include "header.h"
#include <internal/lib.h>

static void close_dir(struct perf_data_file *files, int nr)
{
	while (--nr >= 0) {
		close(files[nr].fd);
		zfree(&files[nr].path);
	}
	free(files);
}

void perf_data__close_dir(struct perf_data *data)
{
	close_dir(data->dir.files, data->dir.nr);
}

int perf_data__create_dir(struct perf_data *data, int nr)
{
	struct perf_data_file *files = NULL;
	int i, ret;

	if (WARN_ON(!data->is_dir))
		return -EINVAL;

	files = zalloc(nr * sizeof(*files));
	if (!files)
		return -ENOMEM;

	data->dir.version = PERF_DIR_VERSION;
	data->dir.files   = files;
	data->dir.nr      = nr;

	for (i = 0; i < nr; i++) {
		struct perf_data_file *file = &files[i];

		ret = asprintf(&file->path, "%s/data.%d", data->path, i);
		if (ret < 0)
			goto out_err;

		ret = open(file->path, O_RDWR|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR);
		if (ret < 0)
			goto out_err;

		file->fd = ret;
	}

	return 0;

out_err:
	close_dir(files, i);
	return ret;
}

int perf_data__open_dir(struct perf_data *data)
{
	struct perf_data_file *files = NULL;
	struct dirent *dent;
	int ret = -1;
	DIR *dir;
	int nr = 0;

	/*
	 * Directory containing a single regular perf data file which is already
	 * open, means there is nothing more to do here.
	 */
	if (perf_data__is_single_file(data))
		return 0;

	if (WARN_ON(!data->is_dir))
		return -EINVAL;

	/* The version is provided by DIR_FORMAT feature. */
	if (WARN_ON(data->dir.version != PERF_DIR_VERSION))
		return -1;

	dir = opendir(data->path);
	if (!dir)
		return -EINVAL;

	while ((dent = readdir(dir)) != NULL) {
		struct perf_data_file *file;
		char path[PATH_MAX];
		struct stat st;

		snprintf(path, sizeof(path), "%s/%s", data->path, dent->d_name);
		if (stat(path, &st))
			continue;

		if (!S_ISREG(st.st_mode) || strncmp(dent->d_name, "data.", 5))
			continue;

		ret = -ENOMEM;

		file = realloc(files, (nr + 1) * sizeof(*files));
		if (!file)
			goto out_err;

		files = file;
		file = &files[nr++];

		file->path = strdup(path);
		if (!file->path)
			goto out_err;

		ret = open(file->path, O_RDONLY);
		if (ret < 0)
			goto out_err;

		file->fd = ret;
		file->size = st.st_size;
	}

	if (!files)
		return -EINVAL;

	data->dir.files = files;
	data->dir.nr    = nr;
	return 0;

out_err:
	close_dir(files, nr);
	return ret;
}

int perf_data__update_dir(struct perf_data *data)
{
	int i;

	if (WARN_ON(!data->is_dir))
		return -EINVAL;

	for (i = 0; i < data->dir.nr; i++) {
		struct perf_data_file *file = &data->dir.files[i];
		struct stat st;

		if (fstat(file->fd, &st))
			return -1;

		file->size = st.st_size;
	}

	return 0;
}

static bool check_pipe(struct perf_data *data)
{
	struct stat st;
	bool is_pipe = false;
	int fd = perf_data__is_read(data) ?
		 STDIN_FILENO : STDOUT_FILENO;

	if (!data->path) {
		if (!fstat(fd, &st) && S_ISFIFO(st.st_mode))
			is_pipe = true;
	} else {
		if (!strcmp(data->path, "-"))
			is_pipe = true;
	}

	if (is_pipe)
		data->file.fd = fd;

	return data->is_pipe = is_pipe;
}

static int check_backup(struct perf_data *data)
{
	struct stat st;

	if (perf_data__is_read(data))
		return 0;

	if (!stat(data->path, &st) && st.st_size) {
		char oldname[PATH_MAX];
		int ret;

		snprintf(oldname, sizeof(oldname), "%s.old",
			 data->path);

		ret = rm_rf_perf_data(oldname);
		if (ret) {
			pr_err("Can't remove old data: %s (%s)\n",
			       ret == -2 ?
			       "Unknown file found" : strerror(errno),
			       oldname);
			return -1;
		}

		if (rename(data->path, oldname)) {
			pr_err("Can't move data: %s (%s to %s)\n",
			       strerror(errno),
			       data->path, oldname);
			return -1;
		}
	}

	return 0;
}

static bool is_dir(struct perf_data *data)
{
	struct stat st;

	if (stat(data->path, &st))
		return false;

	return (st.st_mode & S_IFMT) == S_IFDIR;
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

	data->file.size = st.st_size;
	return fd;

 out_close:
	close(fd);
	return -1;
}

static int open_file_write(struct perf_data *data)
{
	int fd;
	char sbuf[STRERR_BUFSIZE];

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

	if (fd < 0) {
		zfree(&data->file.path);
		return -1;
	}

	data->file.fd = fd;
	return 0;
}

static int open_file_dup(struct perf_data *data)
{
	data->file.path = strdup(data->path);
	if (!data->file.path)
		return -ENOMEM;

	return open_file(data);
}

static int open_dir(struct perf_data *data)
{
	int ret;

	/*
	 * So far we open only the header, so we can read the data version and
	 * layout.
	 */
	if (asprintf(&data->file.path, "%s/data", data->path) < 0)
		return -1;

	if (perf_data__is_write(data) &&
	    mkdir(data->path, S_IRWXU) < 0)
		return -1;

	ret = open_file(data);

	/* Cleanup whatever we managed to create so far. */
	if (ret && perf_data__is_write(data))
		rm_rf_perf_data(data->path);

	return ret;
}

int perf_data__open(struct perf_data *data)
{
	if (check_pipe(data))
		return 0;

	if (!data->path)
		data->path = "perf.data";

	if (check_backup(data))
		return -1;

	if (perf_data__is_read(data))
		data->is_dir = is_dir(data);

	return perf_data__is_dir(data) ?
	       open_dir(data) : open_file_dup(data);
}

void perf_data__close(struct perf_data *data)
{
	if (perf_data__is_dir(data))
		perf_data__close_dir(data);

	zfree(&data->file.path);
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
			   size_t pos, bool at_exit,
			   char **new_filepath)
{
	int ret;

	if (check_pipe(data))
		return -EINVAL;
	if (perf_data__is_read(data))
		return -EINVAL;

	if (asprintf(new_filepath, "%s.%s", data->path, postfix) < 0)
		return -ENOMEM;

	/*
	 * Only fire a warning, don't return error, continue fill
	 * original file.
	 */
	if (rename(data->path, *new_filepath))
		pr_warning("Failed to rename %s to %s\n", data->path, *new_filepath);

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
	return ret;
}

unsigned long perf_data__size(struct perf_data *data)
{
	u64 size = data->file.size;
	int i;

	if (perf_data__is_single_file(data))
		return size;

	for (i = 0; i < data->dir.nr; i++) {
		struct perf_data_file *file = &data->dir.files[i];

		size += file->size;
	}

	return size;
}

int perf_data__make_kcore_dir(struct perf_data *data, char *buf, size_t buf_sz)
{
	int ret;

	if (!data->is_dir)
		return -1;

	ret = snprintf(buf, buf_sz, "%s/kcore_dir", data->path);
	if (ret < 0 || (size_t)ret >= buf_sz)
		return -1;

	return mkdir(buf, S_IRWXU);
}

char *perf_data__kallsyms_name(struct perf_data *data)
{
	char *kallsyms_name;
	struct stat st;

	if (!data->is_dir)
		return NULL;

	if (asprintf(&kallsyms_name, "%s/kcore_dir/kallsyms", data->path) < 0)
		return NULL;

	if (stat(kallsyms_name, &st)) {
		free(kallsyms_name);
		return NULL;
	}

	return kallsyms_name;
}
