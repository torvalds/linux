#ifndef __PERF_SESSION_H
#define __PERF_SESSION_H

#include "header.h"

struct perf_session {
	struct perf_header	header;
	unsigned long		size;
	int			fd;
	char filename[0];
};

struct perf_session *perf_session__new(const char *filename, int mode, bool force);
void perf_session__delete(struct perf_session *self);

#endif /* __PERF_SESSION_H */
