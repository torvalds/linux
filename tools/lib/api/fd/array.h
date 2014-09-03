#ifndef __API_FD_ARRAY__
#define __API_FD_ARRAY__

#include <stdio.h>

struct pollfd;

struct fdarray {
	int	       nr;
	int	       nr_alloc;
	int	       nr_autogrow;
	struct pollfd *entries;
};

void fdarray__init(struct fdarray *fda, int nr_autogrow);
void fdarray__exit(struct fdarray *fda);

struct fdarray *fdarray__new(int nr_alloc, int nr_autogrow);
void fdarray__delete(struct fdarray *fda);

int fdarray__add(struct fdarray *fda, int fd, short revents);
int fdarray__poll(struct fdarray *fda, int timeout);
int fdarray__filter(struct fdarray *fda, short revents);
int fdarray__grow(struct fdarray *fda, int extra);
int fdarray__fprintf(struct fdarray *fda, FILE *fp);

static inline int fdarray__available_entries(struct fdarray *fda)
{
	return fda->nr_alloc - fda->nr;
}

#endif /* __API_FD_ARRAY__ */
