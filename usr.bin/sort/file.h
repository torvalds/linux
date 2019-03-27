/*	$FreeBSD$	*/

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

#if !defined(__SORT_FILE_H__)
#define	__SORT_FILE_H__

#include "coll.h"
#include "sort.h"

#define	SORT_DEFAULT	0
#define	SORT_QSORT	1
#define	SORT_MERGESORT	2
#define	SORT_HEAPSORT	3
#define	SORT_RADIXSORT  4

#define	DEFAULT_SORT_ALGORITHM SORT_HEAPSORT
#define	DEFAULT_SORT_FUNC heapsort

/*
 * List of data to be sorted.
 */
struct sort_list
{
	struct sort_list_item	**list;
	unsigned long long	 memsize;
	size_t			 count;
	size_t			 size;
	size_t			 sub_list_pos;
};

/*
 * File reader object
 */
struct file_reader;

/*
 * List of files to be sorted
 */
struct file_list
{
	const char *		*fns;
	size_t			 count;
	size_t			 sz;
	bool			 tmp;
};

/* memory */

/**/

extern unsigned long long free_memory;
extern unsigned long long available_free_memory;

/* Are we using mmap ? */
extern bool use_mmap;

/* temporary file dir */

extern const char *tmpdir;

/*
 * Max number of simultaneously open files (including the output file).
 */
extern size_t max_open_files;

/*
 * Compress program
 */
extern const char* compress_program;

/* funcs */

struct file_reader *file_reader_init(const char *fsrc);
struct bwstring *file_reader_readline(struct file_reader *fr);
void file_reader_free(struct file_reader *fr);

void init_tmp_files(void);
void clear_tmp_files(void);
char *new_tmp_file_name(void);
void tmp_file_atexit(const char *tmp_file);

void file_list_init(struct file_list *fl, bool tmp);
void file_list_add(struct file_list *fl, const char *fn, bool allocate);
void file_list_populate(struct file_list *fl, int argc, char **argv, bool allocate);
void file_list_clean(struct file_list *fl);

int check(const char *);
void merge_files(struct file_list *fl, const char *fn_out);
FILE *openfile(const char *, const char *);
void closefile(FILE *, const char *);
int procfile(const char *fn, struct sort_list *list, struct file_list *fl);

void sort_list_init(struct sort_list *l);
void sort_list_add(struct sort_list *l, struct bwstring *str);
void sort_list_clean(struct sort_list *l);
void sort_list_dump(struct sort_list *l, const char *fn);

void sort_list_to_file(struct sort_list *list, const char *outfile);

#endif /* __SORT_FILE_H__ */
