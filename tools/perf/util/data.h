/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_DATA_H
#define __PERF_DATA_H

#include <stdbool.h>

enum perf_data_mode {
	PERF_DATA_MODE_WRITE,
	PERF_DATA_MODE_READ,
};

struct perf_data_file {
	const char		*path;
	int			 fd;
	bool			 is_pipe;
	bool			 force;
	unsigned long		 size;
	enum perf_data_mode	 mode;
};

static inline bool perf_data_file__is_read(struct perf_data_file *file)
{
	return file->mode == PERF_DATA_MODE_READ;
}

static inline bool perf_data_file__is_write(struct perf_data_file *file)
{
	return file->mode == PERF_DATA_MODE_WRITE;
}

static inline int perf_data_file__is_pipe(struct perf_data_file *file)
{
	return file->is_pipe;
}

static inline int perf_data_file__fd(struct perf_data_file *file)
{
	return file->fd;
}

static inline unsigned long perf_data_file__size(struct perf_data_file *file)
{
	return file->size;
}

int perf_data_file__open(struct perf_data_file *file);
void perf_data_file__close(struct perf_data_file *file);
ssize_t perf_data_file__write(struct perf_data_file *file,
			      void *buf, size_t size);
/*
 * If at_exit is set, only rename current perf.data to
 * perf.data.<postfix>, continue write on original file.
 * Set at_exit when flushing the last output.
 *
 * Return value is fd of new output.
 */
int perf_data_file__switch(struct perf_data_file *file,
			   const char *postfix,
			   size_t pos, bool at_exit);
#endif /* __PERF_DATA_H */
