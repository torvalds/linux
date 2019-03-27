/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009 Gabor Kovesdan <gabor@FreeBSD.org>
 * Copyright (C) 2012 Oleg Moskalenko <mom040267@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/queue.h>

#include <err.h>
#include <fcntl.h>
#if defined(SORT_THREADS)
#include <pthread.h>
#endif
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

#include "coll.h"
#include "file.h"
#include "radixsort.h"

unsigned long long free_memory = 1000000;
unsigned long long available_free_memory = 1000000;

bool use_mmap;

const char *tmpdir = "/var/tmp";
const char *compress_program;

size_t max_open_files = 16;

/*
 * How much space we read from file at once
 */
#define READ_CHUNK (4096)

/*
 * File reader structure
 */
struct file_reader
{
	struct reader_buffer	 rb;
	FILE			*file;
	char			*fname;
	unsigned char		*buffer;
	unsigned char		*mmapaddr;
	unsigned char		*mmapptr;
	size_t			 bsz;
	size_t			 cbsz;
	size_t			 mmapsize;
	size_t			 strbeg;
	int			 fd;
	char			 elsymb;
};

/*
 * Structure to be used in file merge process.
 */
struct file_header
{
	struct file_reader		*fr;
	struct sort_list_item		*si; /* current top line */
	size_t				 file_pos;
};

/*
 * List elements of "cleanable" files list.
 */
struct CLEANABLE_FILE
{
	char				*fn;
	LIST_ENTRY(CLEANABLE_FILE)	 files;
};

/*
 * List header of "cleanable" files list.
 */
static LIST_HEAD(CLEANABLE_FILES,CLEANABLE_FILE) tmp_files;

/*
 * Semaphore to protect the tmp file list.
 * We use semaphore here because it is signal-safe, according to POSIX.
 * And semaphore does not require pthread library.
 */
static sem_t tmp_files_sem;

static void mt_sort(struct sort_list *list,
    int (*sort_func)(void *, size_t, size_t,
    int (*)(const void *, const void *)), const char* fn);

/*
 * Init tmp files list
 */
void
init_tmp_files(void)
{

	LIST_INIT(&tmp_files);
	sem_init(&tmp_files_sem, 0, 1);
}

/*
 * Save name of a tmp file for signal cleanup
 */
void
tmp_file_atexit(const char *tmp_file)
{

	if (tmp_file) {
		sem_wait(&tmp_files_sem);
		struct CLEANABLE_FILE *item =
		    sort_malloc(sizeof(struct CLEANABLE_FILE));
		item->fn = sort_strdup(tmp_file);
		LIST_INSERT_HEAD(&tmp_files, item, files);
		sem_post(&tmp_files_sem);
	}
}

/*
 * Clear tmp files
 */
void
clear_tmp_files(void)
{
	struct CLEANABLE_FILE *item;

	sem_wait(&tmp_files_sem);
	LIST_FOREACH(item,&tmp_files,files) {
		if ((item) && (item->fn))
			unlink(item->fn);
	}
	sem_post(&tmp_files_sem);
}

/*
 * Check whether a file is a temporary file
 */
static bool
file_is_tmp(const char* fn)
{
	struct CLEANABLE_FILE *item;
	bool ret = false;

	if (fn) {
		sem_wait(&tmp_files_sem);
		LIST_FOREACH(item,&tmp_files,files) {
			if ((item) && (item->fn))
				if (strcmp(item->fn, fn) == 0) {
					ret = true;
					break;
				}
		}
		sem_post(&tmp_files_sem);
	}

	return (ret);
}

/*
 * Generate new temporary file name
 */
char *
new_tmp_file_name(void)
{
	static size_t tfcounter = 0;
	static const char *fn = ".bsdsort.";
	char *ret;
	size_t sz;

	sz = strlen(tmpdir) + 1 + strlen(fn) + 32 + 1;
	ret = sort_malloc(sz);

	sprintf(ret, "%s/%s%d.%lu", tmpdir, fn, (int) getpid(), (unsigned long)(tfcounter++));
	tmp_file_atexit(ret);
	return (ret);
}

/*
 * Initialize file list
 */
void
file_list_init(struct file_list *fl, bool tmp)
{

	if (fl) {
		fl->count = 0;
		fl->sz = 0;
		fl->fns = NULL;
		fl->tmp = tmp;
	}
}

/*
 * Add a file name to the list
 */
void
file_list_add(struct file_list *fl, const char *fn, bool allocate)
{

	if (fl && fn) {
		if (fl->count >= fl->sz || (fl->fns == NULL)) {
			fl->sz = (fl->sz) * 2 + 1;
			fl->fns = sort_realloc(fl->fns, fl->sz *
			    sizeof(char *));
		}
		fl->fns[fl->count] = allocate ? sort_strdup(fn) : fn;
		fl->count += 1;
	}
}

/*
 * Populate file list from array of file names
 */
void
file_list_populate(struct file_list *fl, int argc, char **argv, bool allocate)
{

	if (fl && argv) {
		int i;

		for (i = 0; i < argc; i++)
			file_list_add(fl, argv[i], allocate);
	}
}

/*
 * Clean file list data and delete the files,
 * if this is a list of temporary files
 */
void
file_list_clean(struct file_list *fl)
{

	if (fl) {
		if (fl->fns) {
			size_t i;

			for (i = 0; i < fl->count; i++) {
				if (fl->fns[i]) {
					if (fl->tmp)
						unlink(fl->fns[i]);
					sort_free(fl->fns[i]);
					fl->fns[i] = 0;
				}
			}
			sort_free(fl->fns);
			fl->fns = NULL;
		}
		fl->sz = 0;
		fl->count = 0;
		fl->tmp = false;
	}
}

/*
 * Init sort list
 */
void
sort_list_init(struct sort_list *l)
{

	if (l) {
		l->count = 0;
		l->size = 0;
		l->memsize = sizeof(struct sort_list);
		l->list = NULL;
	}
}

/*
 * Add string to sort list
 */
void
sort_list_add(struct sort_list *l, struct bwstring *str)
{

	if (l && str) {
		size_t indx = l->count;

		if ((l->list == NULL) || (indx >= l->size)) {
			size_t newsize = (l->size + 1) + 1024;

			l->list = sort_realloc(l->list,
			    sizeof(struct sort_list_item*) * newsize);
			l->memsize += (newsize - l->size) *
			    sizeof(struct sort_list_item*);
			l->size = newsize;
		}
		l->list[indx] = sort_list_item_alloc();
		sort_list_item_set(l->list[indx], str);
		l->memsize += sort_list_item_size(l->list[indx]);
		l->count += 1;
	}
}

/*
 * Clean sort list data
 */
void
sort_list_clean(struct sort_list *l)
{

	if (l) {
		if (l->list) {
			size_t i;

			for (i = 0; i < l->count; i++) {
				struct sort_list_item *item;

				item = l->list[i];

				if (item) {
					sort_list_item_clean(item);
					sort_free(item);
					l->list[i] = NULL;
				}
			}
			sort_free(l->list);
			l->list = NULL;
		}
		l->count = 0;
		l->size = 0;
		l->memsize = sizeof(struct sort_list);
	}
}

/*
 * Write sort list to file
 */
void
sort_list_dump(struct sort_list *l, const char *fn)
{

	if (l && fn) {
		FILE *f;

		f = openfile(fn, "w");
		if (f == NULL)
			err(2, NULL);

		if (l->list) {
			size_t i;
			if (!(sort_opts_vals.uflag)) {
				for (i = 0; i < l->count; ++i)
					bwsfwrite(l->list[i]->str, f,
					    sort_opts_vals.zflag);
			} else {
				struct sort_list_item *last_printed_item = NULL;
				struct sort_list_item *item;
				for (i = 0; i < l->count; ++i) {
					item = l->list[i];
					if ((last_printed_item == NULL) ||
					    list_coll(&last_printed_item, &item)) {
						bwsfwrite(item->str, f, sort_opts_vals.zflag);
						last_printed_item = item;
					}
				}
			}
		}

		closefile(f, fn);
	}
}

/*
 * Checks if the given file is sorted.  Stops at the first disorder,
 * prints the disordered line and returns 1.
 */
int
check(const char *fn)
{
	struct bwstring *s1, *s2, *s1disorder, *s2disorder;
	struct file_reader *fr;
	struct keys_array *ka1, *ka2;
	int res;
	size_t pos, posdisorder;

	s1 = s2 = s1disorder = s2disorder = NULL;
	ka1 = ka2 = NULL;

	fr = file_reader_init(fn);

	res = 0;
	pos = 1;
	posdisorder = 1;

	if (fr == NULL) {
		err(2, NULL);
		goto end;
	}

	s1 = file_reader_readline(fr);
	if (s1 == NULL)
		goto end;

	ka1 = keys_array_alloc();
	preproc(s1, ka1);

	s2 = file_reader_readline(fr);
	if (s2 == NULL)
		goto end;

	ka2 = keys_array_alloc();
	preproc(s2, ka2);

	for (;;) {

		if (debug_sort) {
			bwsprintf(stdout, s2, "s1=<", ">");
			bwsprintf(stdout, s1, "s2=<", ">");
		}
		int cmp = key_coll(ka2, ka1, 0);
		if (debug_sort)
			printf("; cmp1=%d", cmp);

		if (!cmp && sort_opts_vals.complex_sort &&
		    !(sort_opts_vals.uflag) && !(sort_opts_vals.sflag)) {
			cmp = top_level_str_coll(s2, s1);
			if (debug_sort)
				printf("; cmp2=%d", cmp);
		}
		if (debug_sort)
			printf("\n");

		if ((sort_opts_vals.uflag && (cmp <= 0)) || (cmp < 0)) {
			if (!(sort_opts_vals.csilentflag)) {
				s2disorder = bwsdup(s2);
				posdisorder = pos;
				if (debug_sort)
					s1disorder = bwsdup(s1);
			}
			res = 1;
			goto end;
		}

		pos++;

		clean_keys_array(s1, ka1);
		sort_free(ka1);
		ka1 = ka2;
		ka2 = NULL;

		bwsfree(s1);
		s1 = s2;

		s2 = file_reader_readline(fr);
		if (s2 == NULL)
			goto end;

		ka2 = keys_array_alloc();
		preproc(s2, ka2);
	}

end:
	if (ka1) {
		clean_keys_array(s1, ka1);
		sort_free(ka1);
	}

	if (s1)
		bwsfree(s1);

	if (ka2) {
		clean_keys_array(s2, ka2);
		sort_free(ka2);
	}

	if (s2)
		bwsfree(s2);

	if ((fn == NULL) || (*fn == 0) || (strcmp(fn, "-") == 0)) {
		for (;;) {
			s2 = file_reader_readline(fr);
			if (s2 == NULL)
				break;
			bwsfree(s2);
		}
	}

	file_reader_free(fr);

	if (s2disorder) {
		bws_disorder_warnx(s2disorder, fn, posdisorder);
		if (s1disorder) {
			bws_disorder_warnx(s1disorder, fn, posdisorder);
			if (s1disorder != s2disorder)
				bwsfree(s1disorder);
		}
		bwsfree(s2disorder);
		s1disorder = NULL;
		s2disorder = NULL;
	}

	if (res)
		exit(res);

	return (0);
}

/*
 * Opens a file.  If the given filename is "-", stdout will be
 * opened.
 */
FILE *
openfile(const char *fn, const char *mode)
{
	FILE *file;

	if (strcmp(fn, "-") == 0) {
		return ((mode && mode[0] == 'r') ? stdin : stdout);
	} else {
		mode_t orig_file_mask = 0;
		int is_tmp = file_is_tmp(fn);

		if (is_tmp && (mode[0] == 'w'))
			orig_file_mask = umask(S_IWGRP | S_IWOTH |
			    S_IRGRP | S_IROTH);

		if (is_tmp && (compress_program != NULL)) {
			char *cmd;
			size_t cmdsz;

			cmdsz = strlen(fn) + 128;
			cmd = sort_malloc(cmdsz);

			fflush(stdout);

			if (mode[0] == 'r')
				snprintf(cmd, cmdsz - 1, "cat %s | %s -d",
				    fn, compress_program);
			else if (mode[0] == 'w')
				snprintf(cmd, cmdsz - 1, "%s > %s",
				    compress_program, fn);
			else
				err(2, "%s", getstr(7));

			if ((file = popen(cmd, mode)) == NULL)
				err(2, NULL);

			sort_free(cmd);

		} else
			if ((file = fopen(fn, mode)) == NULL)
				err(2, NULL);

		if (is_tmp && (mode[0] == 'w'))
			umask(orig_file_mask);
	}

	return (file);
}

/*
 * Close file
 */
void
closefile(FILE *f, const char *fn)
{
	if (f == NULL) {
		;
	} else if (f == stdin) {
		;
	} else if (f == stdout) {
		fflush(f);
	} else {
		if (file_is_tmp(fn) && compress_program != NULL) {
			if(pclose(f)<0)
				err(2,NULL);
		} else
			fclose(f);
	}
}

/*
 * Reads a file into the internal buffer.
 */
struct file_reader *
file_reader_init(const char *fsrc)
{
	struct file_reader *ret;

	if (fsrc == NULL)
		fsrc = "-";

	ret = sort_malloc(sizeof(struct file_reader));
	memset(ret, 0, sizeof(struct file_reader));

	ret->elsymb = '\n';
	if (sort_opts_vals.zflag)
		ret->elsymb = 0;

	ret->fname = sort_strdup(fsrc);

	if (strcmp(fsrc, "-") && (compress_program == NULL) && use_mmap) {

		do {
			struct stat stat_buf;
			void *addr;
			size_t sz = 0;
			int fd, flags;

			flags = MAP_NOCORE | MAP_NOSYNC;

			fd = open(fsrc, O_RDONLY);
			if (fd < 0)
				err(2, NULL);

			if (fstat(fd, &stat_buf) < 0) {
				close(fd);
				break;
			}

			sz = stat_buf.st_size;

#if defined(MAP_PREFAULT_READ)
			flags |= MAP_PREFAULT_READ;
#endif

			addr = mmap(NULL, sz, PROT_READ, flags, fd, 0);
			if (addr == MAP_FAILED) {
				close(fd);
				break;
			}

			ret->fd = fd;
			ret->mmapaddr = addr;
			ret->mmapsize = sz;
			ret->mmapptr = ret->mmapaddr;

		} while (0);
	}

	if (ret->mmapaddr == NULL) {
		ret->file = openfile(fsrc, "r");
		if (ret->file == NULL)
			err(2, NULL);

		if (strcmp(fsrc, "-")) {
			ret->cbsz = READ_CHUNK;
			ret->buffer = sort_malloc(ret->cbsz);
			ret->bsz = 0;
			ret->strbeg = 0;

			ret->bsz = fread(ret->buffer, 1, ret->cbsz, ret->file);
			if (ret->bsz == 0) {
				if (ferror(ret->file))
					err(2, NULL);
			}
		}
	}

	return (ret);
}

struct bwstring *
file_reader_readline(struct file_reader *fr)
{
	struct bwstring *ret = NULL;

	if (fr->mmapaddr) {
		unsigned char *mmapend;

		mmapend = fr->mmapaddr + fr->mmapsize;
		if (fr->mmapptr >= mmapend)
			return (NULL);
		else {
			unsigned char *strend;
			size_t sz;

			sz = mmapend - fr->mmapptr;
			strend = memchr(fr->mmapptr, fr->elsymb, sz);

			if (strend == NULL) {
				ret = bwscsbdup(fr->mmapptr, sz);
				fr->mmapptr = mmapend;
			} else {
				ret = bwscsbdup(fr->mmapptr, strend -
				    fr->mmapptr);
				fr->mmapptr = strend + 1;
			}
		}

	} else if (fr->file != stdin) {
		unsigned char *strend;
		size_t bsz1, remsz, search_start;

		search_start = 0;
		remsz = 0;
		strend = NULL;

		if (fr->bsz > fr->strbeg)
			remsz = fr->bsz - fr->strbeg;

		/* line read cycle */
		for (;;) {
			if (remsz > search_start)
				strend = memchr(fr->buffer + fr->strbeg +
				    search_start, fr->elsymb, remsz -
				    search_start);
			else
				strend = NULL;

			if (strend)
				break;
			if (feof(fr->file))
				break;

			if (fr->bsz != fr->cbsz)
				/* NOTREACHED */
				err(2, "File read software error 1");

			if (remsz > (READ_CHUNK >> 1)) {
				search_start = fr->cbsz - fr->strbeg;
				fr->cbsz += READ_CHUNK;
				fr->buffer = sort_realloc(fr->buffer,
				    fr->cbsz);
				bsz1 = fread(fr->buffer + fr->bsz, 1,
				    READ_CHUNK, fr->file);
				if (bsz1 == 0) {
					if (ferror(fr->file))
						err(2, NULL);
					break;
				}
				fr->bsz += bsz1;
				remsz += bsz1;
			} else {
				if (remsz > 0 && fr->strbeg>0)
					bcopy(fr->buffer + fr->strbeg,
					    fr->buffer, remsz);

				fr->strbeg = 0;
				search_start = remsz;
				bsz1 = fread(fr->buffer + remsz, 1,
				    fr->cbsz - remsz, fr->file);
				if (bsz1 == 0) {
					if (ferror(fr->file))
						err(2, NULL);
					break;
				}
				fr->bsz = remsz + bsz1;
				remsz = fr->bsz;
			}
		}

		if (strend == NULL)
			strend = fr->buffer + fr->bsz;

		if ((fr->buffer + fr->strbeg <= strend) &&
		    (fr->strbeg < fr->bsz) && (remsz>0))
			ret = bwscsbdup(fr->buffer + fr->strbeg, strend -
			    fr->buffer - fr->strbeg);

		fr->strbeg = (strend - fr->buffer) + 1;

	} else {
		size_t len = 0;

		ret = bwsfgetln(fr->file, &len, sort_opts_vals.zflag,
		    &(fr->rb));
	}

	return (ret);
}

static void
file_reader_clean(struct file_reader *fr)
{

	if (fr) {
		if (fr->mmapaddr)
			munmap(fr->mmapaddr, fr->mmapsize);

		if (fr->fd)
			close(fr->fd);

		if (fr->buffer)
			sort_free(fr->buffer);

		if (fr->file)
			if (fr->file != stdin)
				closefile(fr->file, fr->fname);

		if(fr->fname)
			sort_free(fr->fname);

		memset(fr, 0, sizeof(struct file_reader));
	}
}

void
file_reader_free(struct file_reader *fr)
{

	if (fr) {
		file_reader_clean(fr);
		sort_free(fr);
	}
}

int
procfile(const char *fsrc, struct sort_list *list, struct file_list *fl)
{
	struct file_reader *fr;

	fr = file_reader_init(fsrc);
	if (fr == NULL)
		err(2, NULL);

	/* file browse cycle */
	for (;;) {
		struct bwstring *bws;

		bws = file_reader_readline(fr);

		if (bws == NULL)
			break;

		sort_list_add(list, bws);

		if (list->memsize >= available_free_memory) {
			char *fn;

			fn = new_tmp_file_name();
			sort_list_to_file(list, fn);
			file_list_add(fl, fn, false);
			sort_list_clean(list);
		}
	}

	file_reader_free(fr);

	return (0);
}

/*
 * Compare file headers. Files with EOF always go to the end of the list.
 */
static int
file_header_cmp(struct file_header *f1, struct file_header *f2)
{

	if (f1 == f2)
		return (0);
	else {
		if (f1->fr == NULL) {
			return ((f2->fr == NULL) ? 0 : +1);
		} else if (f2->fr == NULL)
			return (-1);
		else {
			int ret;

			ret = list_coll(&(f1->si), &(f2->si));
			if (!ret)
				return ((f1->file_pos < f2->file_pos) ? -1 : +1);
			return (ret);
		}
	}
}

/*
 * Allocate and init file header structure
 */
static void
file_header_init(struct file_header **fh, const char *fn, size_t file_pos)
{

	if (fh && fn) {
		struct bwstring *line;

		*fh = sort_malloc(sizeof(struct file_header));
		(*fh)->file_pos = file_pos;
		(*fh)->fr = file_reader_init(fn);
		if ((*fh)->fr == NULL) {
			perror(fn);
			err(2, "%s", getstr(8));
		}
		line = file_reader_readline((*fh)->fr);
		if (line == NULL) {
			file_reader_free((*fh)->fr);
			(*fh)->fr = NULL;
			(*fh)->si = NULL;
		} else {
			(*fh)->si = sort_list_item_alloc();
			sort_list_item_set((*fh)->si, line);
		}
	}
}

/*
 * Close file
 */
static void
file_header_close(struct file_header **fh)
{

	if (fh && *fh) {
		if ((*fh)->fr) {
			file_reader_free((*fh)->fr);
			(*fh)->fr = NULL;
		}
		if ((*fh)->si) {
			sort_list_item_clean((*fh)->si);
			sort_free((*fh)->si);
			(*fh)->si = NULL;
		}
		sort_free(*fh);
		*fh = NULL;
	}
}

/*
 * Swap two array elements
 */
static void
file_header_swap(struct file_header **fh, size_t i1, size_t i2)
{
	struct file_header *tmp;

	tmp = fh[i1];
	fh[i1] = fh[i2];
	fh[i2] = tmp;
}

/* heap algorithm ==>> */

/*
 * See heap sort algorithm
 * "Raises" last element to its right place
 */
static void
file_header_heap_swim(struct file_header **fh, size_t indx)
{

	if (indx > 0) {
		size_t parent_index;

		parent_index = (indx - 1) >> 1;

		if (file_header_cmp(fh[indx], fh[parent_index]) < 0) {
			/* swap child and parent and continue */
			file_header_swap(fh, indx, parent_index);
			file_header_heap_swim(fh, parent_index);
		}
	}
}

/*
 * Sink the top element to its correct position
 */
static void
file_header_heap_sink(struct file_header **fh, size_t indx, size_t size)
{
	size_t left_child_index;
	size_t right_child_index;

	left_child_index = indx + indx + 1;
	right_child_index = left_child_index + 1;

	if (left_child_index < size) {
		size_t min_child_index;

		min_child_index = left_child_index;

		if ((right_child_index < size) &&
		    (file_header_cmp(fh[left_child_index],
		    fh[right_child_index]) > 0))
			min_child_index = right_child_index;
		if (file_header_cmp(fh[indx], fh[min_child_index]) > 0) {
			file_header_swap(fh, indx, min_child_index);
			file_header_heap_sink(fh, min_child_index, size);
		}
	}
}

/* <<== heap algorithm */

/*
 * Adds element to the "left" end
 */
static void
file_header_list_rearrange_from_header(struct file_header **fh, size_t size)
{

	file_header_heap_sink(fh, 0, size);
}

/*
 * Adds element to the "right" end
 */
static void
file_header_list_push(struct file_header *f, struct file_header **fh, size_t size)
{

	fh[size++] = f;
	file_header_heap_swim(fh, size - 1);
}

struct last_printed
{
	struct bwstring *str;
};

/*
 * Prints the current line of the file
 */
static void
file_header_print(struct file_header *fh, FILE *f_out, struct last_printed *lp)
{

	if (fh && fh->fr && f_out && fh->si && fh->si->str) {
		if (sort_opts_vals.uflag) {
			if ((lp->str == NULL) || (str_list_coll(lp->str, &(fh->si)))) {
				bwsfwrite(fh->si->str, f_out, sort_opts_vals.zflag);
				if (lp->str)
					bwsfree(lp->str);
				lp->str = bwsdup(fh->si->str);
			}
		} else
			bwsfwrite(fh->si->str, f_out, sort_opts_vals.zflag);
	}
}

/*
 * Read next line
 */
static void
file_header_read_next(struct file_header *fh)
{

	if (fh && fh->fr) {
		struct bwstring *tmp;

		tmp = file_reader_readline(fh->fr);
		if (tmp == NULL) {
			file_reader_free(fh->fr);
			fh->fr = NULL;
			if (fh->si) {
				sort_list_item_clean(fh->si);
				sort_free(fh->si);
				fh->si = NULL;
			}
		} else {
			if (fh->si == NULL)
				fh->si = sort_list_item_alloc();
			sort_list_item_set(fh->si, tmp);
		}
	}
}

/*
 * Merge array of "files headers"
 */
static void
file_headers_merge(size_t fnum, struct file_header **fh, FILE *f_out)
{
	struct last_printed lp;
	size_t i;

	memset(&lp, 0, sizeof(lp));

	/*
	 * construct the initial sort structure
	 */
	for (i = 0; i < fnum; i++)
		file_header_list_push(fh[i], fh, i);

	while (fh[0]->fr) { /* unfinished files are always in front */
		/* output the smallest line: */
		file_header_print(fh[0], f_out, &lp);
		/* read a new line, if possible: */
		file_header_read_next(fh[0]);
		/* re-arrange the list: */
		file_header_list_rearrange_from_header(fh, fnum);
	}

	if (lp.str)
		bwsfree(lp.str);
}

/*
 * Merges the given files into the output file, which can be
 * stdout.
 */
static void
merge_files_array(size_t argc, const char **argv, const char *fn_out)
{

	if (argv && fn_out) {
		struct file_header **fh;
		FILE *f_out;
		size_t i;

		f_out = openfile(fn_out, "w");

		if (f_out == NULL)
			err(2, NULL);

		fh = sort_malloc((argc + 1) * sizeof(struct file_header *));

		for (i = 0; i < argc; i++)
			file_header_init(fh + i, argv[i], (size_t) i);

		file_headers_merge(argc, fh, f_out);

		for (i = 0; i < argc; i++)
			file_header_close(fh + i);

		sort_free(fh);

		closefile(f_out, fn_out);
	}
}

/*
 * Shrinks the file list until its size smaller than max number of opened files
 */
static int
shrink_file_list(struct file_list *fl)
{

	if ((fl == NULL) || (size_t) (fl->count) < max_open_files)
		return (0);
	else {
		struct file_list new_fl;
		size_t indx = 0;

		file_list_init(&new_fl, true);
		while (indx < fl->count) {
			char *fnew;
			size_t num;

			num = fl->count - indx;
			fnew = new_tmp_file_name();

			if ((size_t) num >= max_open_files)
				num = max_open_files - 1;
			merge_files_array(num, fl->fns + indx, fnew);
			if (fl->tmp) {
				size_t i;

				for (i = 0; i < num; i++)
					unlink(fl->fns[indx + i]);
			}
			file_list_add(&new_fl, fnew, false);
			indx += num;
		}
		fl->tmp = false; /* already taken care of */
		file_list_clean(fl);

		fl->count = new_fl.count;
		fl->fns = new_fl.fns;
		fl->sz = new_fl.sz;
		fl->tmp = new_fl.tmp;

		return (1);
	}
}

/*
 * Merge list of files
 */
void
merge_files(struct file_list *fl, const char *fn_out)
{

	if (fl && fn_out) {
		while (shrink_file_list(fl));

		merge_files_array(fl->count, fl->fns, fn_out);
	}
}

static const char *
get_sort_method_name(int sm)
{

	if (sm == SORT_MERGESORT)
		return "mergesort";
	else if (sort_opts_vals.sort_method == SORT_RADIXSORT)
		return "radixsort";
	else if (sort_opts_vals.sort_method == SORT_HEAPSORT)
		return "heapsort";
	else
		return "quicksort";
}

/*
 * Wrapper for qsort
 */
static int sort_qsort(void *list, size_t count, size_t elem_size,
    int (*cmp_func)(const void *, const void *))
{

	qsort(list, count, elem_size, cmp_func);
	return (0);
}

/*
 * Sort list of lines and writes it to the file
 */
void
sort_list_to_file(struct sort_list *list, const char *outfile)
{
	struct sort_mods *sm = &(keys[0].sm);

	if (!(sm->Mflag) && !(sm->Rflag) && !(sm->Vflag) && !(sm->Vflag) &&
	    !(sm->gflag) && !(sm->hflag) && !(sm->nflag)) {
		if ((sort_opts_vals.sort_method == SORT_DEFAULT) && byte_sort)
			sort_opts_vals.sort_method = SORT_RADIXSORT;

	} else if (sort_opts_vals.sort_method == SORT_RADIXSORT)
		err(2, "%s", getstr(9));

	/*
	 * to handle stable sort and the unique cases in the
	 * right order, we need stable basic algorithm
	 */
	if (sort_opts_vals.sflag) {
		switch (sort_opts_vals.sort_method){
		case SORT_MERGESORT:
			break;
		case SORT_RADIXSORT:
			break;
		case SORT_DEFAULT:
			sort_opts_vals.sort_method = SORT_MERGESORT;
			break;
		default:
			errx(2, "%s", getstr(10));
		}
	}

	if (sort_opts_vals.sort_method == SORT_DEFAULT)
		sort_opts_vals.sort_method = DEFAULT_SORT_ALGORITHM;

	if (debug_sort)
		printf("sort_method=%s\n",
		    get_sort_method_name(sort_opts_vals.sort_method));

	switch (sort_opts_vals.sort_method){
	case SORT_RADIXSORT:
		rxsort(list->list, list->count);
		sort_list_dump(list, outfile);
		break;
	case SORT_MERGESORT:
		mt_sort(list, mergesort, outfile);
		break;
	case SORT_HEAPSORT:
		mt_sort(list, heapsort,	outfile);
		break;
	case SORT_QSORT:
		mt_sort(list, sort_qsort, outfile);
		break;
	default:
		mt_sort(list, DEFAULT_SORT_FUNC, outfile);
		break;
	}
}

/******************* MT SORT ************************/

#if defined(SORT_THREADS)
/* semaphore to count threads */
static sem_t mtsem;

/* current system sort function */
static int (*g_sort_func)(void *, size_t, size_t,
    int(*)(const void *, const void *));

/*
 * Sort cycle thread (in multi-threaded mode)
 */
static void*
mt_sort_thread(void* arg)
{
	struct sort_list *list = arg;

	g_sort_func(list->list, list->count, sizeof(struct sort_list_item *),
	    (int(*)(const void *, const void *)) list_coll);

	sem_post(&mtsem);

	return (arg);
}

/*
 * Compare sub-lists. Empty sub-lists always go to the end of the list.
 */
static int
sub_list_cmp(struct sort_list *l1, struct sort_list *l2)
{

	if (l1 == l2)
		return (0);
	else {
		if (l1->count == 0) {
			return ((l2->count == 0) ? 0 : +1);
		} else if (l2->count == 0) {
			return (-1);
		} else {
			int ret;

			ret = list_coll(&(l1->list[0]), &(l2->list[0]));
			if (!ret)
				return ((l1->sub_list_pos < l2->sub_list_pos) ?
				    -1 : +1);
			return (ret);
		}
	}
}

/*
 * Swap two array elements
 */
static void
sub_list_swap(struct sort_list **sl, size_t i1, size_t i2)
{
	struct sort_list *tmp;

	tmp = sl[i1];
	sl[i1] = sl[i2];
	sl[i2] = tmp;
}

/* heap algorithm ==>> */

/*
 * See heap sort algorithm
 * "Raises" last element to its right place
 */
static void
sub_list_swim(struct sort_list **sl, size_t indx)
{

	if (indx > 0) {
		size_t parent_index;

		parent_index = (indx - 1) >> 1;

		if (sub_list_cmp(sl[indx], sl[parent_index]) < 0) {
			/* swap child and parent and continue */
			sub_list_swap(sl, indx, parent_index);
			sub_list_swim(sl, parent_index);
		}
	}
}

/*
 * Sink the top element to its correct position
 */
static void
sub_list_sink(struct sort_list **sl, size_t indx, size_t size)
{
	size_t left_child_index;
	size_t right_child_index;

	left_child_index = indx + indx + 1;
	right_child_index = left_child_index + 1;

	if (left_child_index < size) {
		size_t min_child_index;

		min_child_index = left_child_index;

		if ((right_child_index < size) &&
		    (sub_list_cmp(sl[left_child_index],
		    sl[right_child_index]) > 0))
			min_child_index = right_child_index;
		if (sub_list_cmp(sl[indx], sl[min_child_index]) > 0) {
			sub_list_swap(sl, indx, min_child_index);
			sub_list_sink(sl, min_child_index, size);
		}
	}
}

/* <<== heap algorithm */

/*
 * Adds element to the "right" end
 */
static void
sub_list_push(struct sort_list *s, struct sort_list **sl, size_t size)
{

	sl[size++] = s;
	sub_list_swim(sl, size - 1);
}

struct last_printed_item
{
	struct sort_list_item *item;
};

/*
 * Prints the current line of the file
 */
static void
sub_list_header_print(struct sort_list *sl, FILE *f_out,
    struct last_printed_item *lp)
{

	if (sl && sl->count && f_out && sl->list[0]->str) {
		if (sort_opts_vals.uflag) {
			if ((lp->item == NULL) || (list_coll(&(lp->item),
			    &(sl->list[0])))) {
				bwsfwrite(sl->list[0]->str, f_out,
				    sort_opts_vals.zflag);
				lp->item = sl->list[0];
			}
		} else
			bwsfwrite(sl->list[0]->str, f_out,
			    sort_opts_vals.zflag);
	}
}

/*
 * Read next line
 */
static void
sub_list_next(struct sort_list *sl)
{

	if (sl && sl->count) {
		sl->list += 1;
		sl->count -= 1;
	}
}

/*
 * Merge sub-lists to a file
 */
static void
merge_sub_lists(struct sort_list **sl, size_t n, FILE* f_out)
{
	struct last_printed_item lp;
	size_t i;

	memset(&lp,0,sizeof(lp));

	/* construct the initial list: */
	for (i = 0; i < n; i++)
		sub_list_push(sl[i], sl, i);

	while (sl[0]->count) { /* unfinished lists are always in front */
		/* output the smallest line: */
		sub_list_header_print(sl[0], f_out, &lp);
		/* move to a new line, if possible: */
		sub_list_next(sl[0]);
		/* re-arrange the list: */
		sub_list_sink(sl, 0, n);
	}
}

/*
 * Merge sub-lists to a file
 */
static void
merge_list_parts(struct sort_list **parts, size_t n, const char *fn)
{
	FILE* f_out;

	f_out = openfile(fn,"w");

	merge_sub_lists(parts, n, f_out);

	closefile(f_out, fn);
}

#endif /* defined(SORT_THREADS) */
/*
 * Multi-threaded sort algorithm "driver"
 */
static void
mt_sort(struct sort_list *list,
    int(*sort_func)(void *, size_t, size_t, int(*)(const void *, const void *)),
    const char* fn)
{
#if defined(SORT_THREADS)
	if (nthreads < 2 || list->count < MT_SORT_THRESHOLD) {
		size_t nthreads_save = nthreads;
		nthreads = 1;
#endif
		/* if single thread or small data, do simple sort */
		sort_func(list->list, list->count,
		    sizeof(struct sort_list_item *),
		    (int(*)(const void *, const void *)) list_coll);
		sort_list_dump(list, fn);
#if defined(SORT_THREADS)
		nthreads = nthreads_save;
	} else {
		/* multi-threaded sort */
		struct sort_list **parts;
		size_t avgsize, cstart, i;

		/* array of sub-lists */
		parts = sort_malloc(sizeof(struct sort_list*) * nthreads);
		cstart = 0;
		avgsize = list->count / nthreads;

		/* set global system sort function */
		g_sort_func = sort_func;

		/* set sublists */
		for (i = 0; i < nthreads; ++i) {
			size_t sz = 0;

			parts[i] = sort_malloc(sizeof(struct sort_list));
			parts[i]->list = list->list + cstart;
			parts[i]->memsize = 0;
			parts[i]->sub_list_pos = i;

			sz = (i == nthreads - 1) ? list->count - cstart :
			    avgsize;

			parts[i]->count = sz;

			parts[i]->size = parts[i]->count;

			cstart += sz;
		}

		/* init threads counting semaphore */
		sem_init(&mtsem, 0, 0);

		/* start threads */
		for (i = 0; i < nthreads; ++i) {
			pthread_t pth;
			pthread_attr_t attr;

			pthread_attr_init(&attr);
			pthread_attr_setdetachstate(&attr, PTHREAD_DETACHED);

			for (;;) {
				int res = pthread_create(&pth, &attr,
				    mt_sort_thread, parts[i]);

				if (res >= 0)
					break;
				if (errno == EAGAIN) {
					pthread_yield();
					continue;
				}
				err(2, NULL);
			}

			pthread_attr_destroy(&attr);
		}

		/* wait for threads completion */
		for (i = 0; i < nthreads; ++i) {
			sem_wait(&mtsem);
		}
		/* destroy the semaphore - we do not need it anymore */
		sem_destroy(&mtsem);

		/* merge sorted sub-lists to the file */
		merge_list_parts(parts, nthreads, fn);

		/* free sub-lists data */
		for (i = 0; i < nthreads; ++i) {
			sort_free(parts[i]);
		}
		sort_free(parts);
	}
#endif /* defined(SORT_THREADS) */
}
