#ifndef __API_FD_ARRAY__
#define __API_FD_ARRAY__

#include <stdio.h>

struct pollfd;

/**
 * struct fdarray: Array of file descriptors
 *
 * @priv: Per array entry priv area, users should access just its contents,
 *	  not set it to anything, as it is kept in synch with @entries, being
 *	  realloc'ed, * for instance, in fdarray__{grow,filter}.
 *
 *	  I.e. using 'fda->priv[N].idx = * value' where N < fda->nr is ok,
 *	  but doing 'fda->priv = malloc(M)' is not allowed.
 */
struct fdarray {
	int	       nr;
	int	       nr_alloc;
	int	       nr_autogrow;
	struct pollfd *entries;
	union {
		int    idx;
	} *priv;
};

void fdarray__init(struct fdarray *fda, int nr_autogrow);
void fdarray__exit(struct fdarray *fda);

struct fdarray *fdarray__new(int nr_alloc, int nr_autogrow);
void fdarray__delete(struct fdarray *fda);

int fdarray__add(struct fdarray *fda, int fd, short revents);
int fdarray__poll(struct fdarray *fda, int timeout);
int fdarray__filter(struct fdarray *fda, short revents,
		    void (*entry_destructor)(struct fdarray *fda, int fd));
int fdarray__grow(struct fdarray *fda, int extra);
int fdarray__fprintf(struct fdarray *fda, FILE *fp);

static inline int fdarray__available_entries(struct fdarray *fda)
{
	return fda->nr_alloc - fda->nr;
}

#endif /* __API_FD_ARRAY__ */
