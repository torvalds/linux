// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014, Red Hat Inc, Arnaldo Carvalho de Melo <acme@redhat.com>
 */
#include "array.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

void fdarray__init(struct fdarray *fda, int nr_autogrow)
{
	fda->entries	 = NULL;
	fda->priv	 = NULL;
	fda->nr		 = fda->nr_alloc = 0;
	fda->nr_autogrow = nr_autogrow;
}

int fdarray__grow(struct fdarray *fda, int nr)
{
	struct priv *priv;
	int nr_alloc = fda->nr_alloc + nr;
	size_t psize = sizeof(fda->priv[0]) * nr_alloc;
	size_t size  = sizeof(struct pollfd) * nr_alloc;
	struct pollfd *entries = realloc(fda->entries, size);

	if (entries == NULL)
		return -ENOMEM;

	priv = realloc(fda->priv, psize);
	if (priv == NULL) {
		free(entries);
		return -ENOMEM;
	}

	memset(&entries[fda->nr_alloc], 0, sizeof(struct pollfd) * nr);
	memset(&priv[fda->nr_alloc], 0, sizeof(fda->priv[0]) * nr);

	fda->nr_alloc = nr_alloc;
	fda->entries  = entries;
	fda->priv     = priv;
	return 0;
}

struct fdarray *fdarray__new(int nr_alloc, int nr_autogrow)
{
	struct fdarray *fda = calloc(1, sizeof(*fda));

	if (fda != NULL) {
		if (fdarray__grow(fda, nr_alloc)) {
			free(fda);
			fda = NULL;
		} else {
			fda->nr_autogrow = nr_autogrow;
		}
	}

	return fda;
}

void fdarray__exit(struct fdarray *fda)
{
	free(fda->entries);
	free(fda->priv);
	fdarray__init(fda, 0);
}

void fdarray__delete(struct fdarray *fda)
{
	fdarray__exit(fda);
	free(fda);
}

int fdarray__add(struct fdarray *fda, int fd, short revents, enum fdarray_flags flags)
{
	int pos = fda->nr;

	if (fda->nr == fda->nr_alloc &&
	    fdarray__grow(fda, fda->nr_autogrow) < 0)
		return -ENOMEM;

	fda->entries[fda->nr].fd     = fd;
	fda->entries[fda->nr].events = revents;
	fda->priv[fda->nr].flags = flags;
	fda->nr++;
	return pos;
}

int fdarray__filter(struct fdarray *fda, short revents,
		    void (*entry_destructor)(struct fdarray *fda, int fd, void *arg),
		    void *arg)
{
	int fd, nr = 0;

	if (fda->nr == 0)
		return 0;

	for (fd = 0; fd < fda->nr; ++fd) {
		if (!fda->entries[fd].events)
			continue;

		if (fda->entries[fd].revents & revents) {
			if (entry_destructor)
				entry_destructor(fda, fd, arg);

			fda->entries[fd].revents = fda->entries[fd].events = 0;
			continue;
		}

		if (!(fda->priv[fd].flags & fdarray_flag__nonfilterable))
			++nr;
	}

	return nr;
}

int fdarray__poll(struct fdarray *fda, int timeout)
{
	return poll(fda->entries, fda->nr, timeout);
}

int fdarray__fprintf(struct fdarray *fda, FILE *fp)
{
	int fd, printed = fprintf(fp, "%d [ ", fda->nr);

	for (fd = 0; fd < fda->nr; ++fd)
		printed += fprintf(fp, "%s%d", fd ? ", " : "", fda->entries[fd].fd);

	return printed + fprintf(fp, " ]");
}
