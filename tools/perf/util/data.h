/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_DATA_H
#define __PERF_DATA_H

#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>
#include <linux/types.h>

enum perf_data_mode {
	PERF_DATA_MODE_WRITE,
	PERF_DATA_MODE_READ,
};

enum perf_dir_version {
	PERF_DIR_SINGLE_FILE	= 0,
	PERF_DIR_VERSION	= 1,
};

/**
 * struct perf_data_file: A wrapper around a file used for perf.data reading or writing. Generally
 * part of struct perf_data.
 */
struct perf_data_file {
	/**
	 * @path: Path of file. Generally a copy of perf_data.path but for a
	 * directory it is the file within the directory.
	 */
	char		*path;
	union {
		/** @fd: File descriptor for read/writes. Valid if use_stdio is false. */
		int	 fd;
		/**
		 * @fptr: Stdio FILE. Valid if use_stdio is true, currently just
		 * pipes in perf inject.
		 */
		FILE	*fptr;
	};
	/** @size: Size of file when opened. */
	unsigned long	 size;
	/** @use_stdio: Use buffered stdio operations. */
	bool		 use_stdio;
};

/**
 * struct perf_data: A wrapper around a file used for perf.data reading or writing.
 */
struct perf_data {
	/** @path: Path to open and of the file. NULL implies 'perf.data' will be used. */
	const char		*path;
	/** @file: Underlying file to be used. */
	struct perf_data_file	 file;
	/** @is_pipe: Underlying file is a pipe. */
	bool			 is_pipe;
	/** @is_dir: Underlying file is a directory. */
	bool			 is_dir;
	/** @force: Ignore opening a file creating created by a different user. */
	bool			 force;
	/** @in_place_update: A file opened for reading but will be written to. */
	bool			 in_place_update;
	/** @mode: Read or write mode. */
	enum perf_data_mode	 mode;

	struct {
		/** @version: perf_dir_version. */
		u64			 version;
		/** @files: perf data files for the directory. */
		struct perf_data_file	*files;
		/** @nr: Number of perf data files for the directory. */
		int			 nr;
	} dir;
};

static inline int perf_data_file__fd(struct perf_data_file *file)
{
	return file->use_stdio ? fileno(file->fptr) : file->fd;
}

ssize_t perf_data_file__write(struct perf_data_file *file,
			      void *buf, size_t size);
off_t perf_data_file__seek(struct perf_data_file *file, off_t offset, int whence);


static inline bool perf_data__is_read(struct perf_data *data)
{
	return data->mode == PERF_DATA_MODE_READ;
}

static inline bool perf_data__is_write(struct perf_data *data)
{
	return data->mode == PERF_DATA_MODE_WRITE;
}

static inline int perf_data__is_pipe(struct perf_data *data)
{
	return data->is_pipe;
}

static inline bool perf_data__is_dir(struct perf_data *data)
{
	return data->is_dir;
}

static inline bool perf_data__is_single_file(struct perf_data *data)
{
	return data->dir.version == PERF_DIR_SINGLE_FILE;
}

static inline int perf_data__fd(struct perf_data *data)
{
	return perf_data_file__fd(&data->file);
}

int perf_data__open(struct perf_data *data);
void perf_data__close(struct perf_data *data);
ssize_t perf_data__read(struct perf_data *data, void *buf, size_t size);
ssize_t perf_data__write(struct perf_data *data,
			 void *buf, size_t size);
off_t perf_data__seek(struct perf_data *data, off_t offset, int whence);
/*
 * If at_exit is set, only rename current perf.data to
 * perf.data.<postfix>, continue write on original data.
 * Set at_exit when flushing the last output.
 *
 * Return value is fd of new output.
 */
int perf_data__switch(struct perf_data *data,
		      const char *postfix,
		      size_t pos, bool at_exit, char **new_filepath);

int perf_data__create_dir(struct perf_data *data, int nr);
int perf_data__open_dir(struct perf_data *data);
void perf_data__close_dir(struct perf_data *data);
unsigned long perf_data__size(struct perf_data *data);
int perf_data__make_kcore_dir(struct perf_data *data, char *buf, size_t buf_sz);
char *perf_data__kallsyms_name(struct perf_data *data);
char *perf_data__guest_kallsyms_name(struct perf_data *data, pid_t machine_pid);

bool has_kcore_dir(const char *path);
bool is_perf_data(const char *path);

#endif /* __PERF_DATA_H */
