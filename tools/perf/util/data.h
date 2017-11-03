#ifndef __PERF_DATA_H
#define __PERF_DATA_H

#include <stdbool.h>

enum perf_data_mode {
	PERF_DATA_MODE_WRITE,
	PERF_DATA_MODE_READ,
};

struct perf_data_file {
	const char	*path;
	int		 fd;
};

struct perf_data {
	struct perf_data_file	 file;
	bool			 is_pipe;
	bool			 force;
	unsigned long		 size;
	enum perf_data_mode	 mode;
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

static inline int perf_data__fd(struct perf_data *data)
{
	return data->file.fd;
}

static inline unsigned long perf_data__size(struct perf_data *data)
{
	return data->size;
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
			   size_t pos, bool at_exit);
#endif /* __PERF_DATA_H */
