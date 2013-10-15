#ifndef __PERF_DATA_H
#define __PERF_DATA_H

#include <stdbool.h>

enum perf_data_mode {
	PERF_DATA_MODE_WRITE,
	PERF_DATA_MODE_READ,
};

struct perf_data_file {
	const char *path;
	int fd;
	bool is_pipe;
	bool force;
	enum perf_data_mode mode;
};

static inline bool perf_data_file__is_read(struct perf_data_file *file)
{
	return file->mode == PERF_DATA_MODE_READ;
}

static inline bool perf_data_file__is_write(struct perf_data_file *file)
{
	return file->mode == PERF_DATA_MODE_WRITE;
}

#endif /* __PERF_DATA_H */
