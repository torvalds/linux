/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_DATA_H
#define __PERF_DATA_H

#include <stdbool.h>

enum perf_data_mode {
	PERF_DATA_MODE_WRITE,
	PERF_DATA_MODE_READ,
};

struct perf_data_file {
	char		*path;
	int		 fd;
	unsigned long	 size;
};

struct perf_data {
	const char		*path;
	struct perf_data_file	 file;
	bool			 is_pipe;
	bool			 is_dir;
	bool			 force;
	enum perf_data_mode	 mode;

	struct {
		u64			 version;
		struct perf_data_file	*files;
		int			 nr;
	} dir;
};

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

static inline int perf_data__fd(struct perf_data *data)
{
	return data->file.fd;
}

int perf_data__open(struct perf_data *data);
void perf_data__close(struct perf_data *data);
ssize_t perf_data__write(struct perf_data *data,
			      void *buf, size_t size);
ssize_t perf_data_file__write(struct perf_data_file *file,
			      void *buf, size_t size);
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
int perf_data__update_dir(struct perf_data *data);
unsigned long perf_data__size(struct perf_data *data);
#endif /* __PERF_DATA_H */
