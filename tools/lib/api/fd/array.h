/* SPDX-License-Identifier: GPL-2.0 */
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
	struct priv {
		union {
			int    idx;
			void   *ptr;
		};
		unsigned int flags;
	} *priv;
};

enum fdarray_flags {
	fdarray_flag__default		= 0x00000000,
	fdarray_flag__nonfilterable	= 0x00000001,
	fdarray_flag__non_perf_event	= 0x00000002,
};

void fdarray__init(struct fdarray *fda, int nr_autogrow);
void fdarray__exit(struct fdarray *fda);

struct fdarray *fdarray__new(int nr_alloc, int nr_autogrow);
void fdarray__delete(struct fdarray *fda);

int fdarray__add(struct fdarray *fda, int fd, short revents, enum fdarray_flags flags);
int fdarray__dup_entry_from(struct fdarray *fda, int pos, struct fdarray *from);
int fdarray__poll(struct fdarray *fda, int timeout);
int fdarray__filter(struct fdarray *fda, short revents,
		    void (*entry_destructor)(struct fdarray *fda, int fd, void *arg),
		    void *arg);
int fdarray__grow(struct fdarray *fda, int extra);
int fdarray__fprintf(struct fdarray *fda, FILE *fp);

static inline int fdarray__available_entries(struct fdarray *fda)
{
	return fda->nr_alloc - fda->nr;
}

#endif /* __API_FD_ARRAY__ */
