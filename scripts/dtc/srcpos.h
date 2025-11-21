/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2007 Jon Loeliger, Freescale Semiconductor, Inc.
 */

#ifndef SRCPOS_H
#define SRCPOS_H

#include <stdio.h>
#include <stdbool.h>
#include "util.h"

struct srcfile_state {
	FILE *f;
	char *name;
	char *dir;
	int lineno, colno;
	struct srcfile_state *prev;
};

extern FILE *depfile; /* = NULL */
extern struct srcfile_state *current_srcfile; /* = NULL */

/**
 * Open a source file.
 *
 * If the source file is a relative pathname, then it is searched for in the
 * current directory (the directory of the last source file read) and after
 * that in the search path.
 *
 * We work through the search path in order from the first path specified to
 * the last.
 *
 * If the file is not found, then this function does not return, but calls
 * die().
 *
 * @param fname		Filename to search
 * @param fullnamep	If non-NULL, it is set to the allocated filename of the
 *			file that was opened. The caller is then responsible
 *			for freeing the pointer.
 * @return pointer to opened FILE
 */
FILE *srcfile_relative_open(const char *fname, char **fullnamep);

void srcfile_push(const char *fname);
bool srcfile_pop(void);

/**
 * Add a new directory to the search path for input files
 *
 * The new path is added at the end of the list.
 *
 * @param dirname	Directory to add
 */
void srcfile_add_search_path(const char *dirname);

struct srcpos {
    int first_line;
    int first_column;
    int last_line;
    int last_column;
    struct srcfile_state *file;
    struct srcpos *next;
};

#define YYLTYPE struct srcpos

#define YYLLOC_DEFAULT(Current, Rhs, N)						\
	do {									\
		if (N) {							\
			(Current).first_line = YYRHSLOC(Rhs, 1).first_line;	\
			(Current).first_column = YYRHSLOC(Rhs, 1).first_column;	\
			(Current).last_line = YYRHSLOC(Rhs, N).last_line;	\
			(Current).last_column  = YYRHSLOC (Rhs, N).last_column;	\
			(Current).file = YYRHSLOC(Rhs, N).file;			\
		} else {							\
			(Current).first_line = (Current).last_line =		\
				YYRHSLOC(Rhs, 0).last_line;			\
			(Current).first_column = (Current).last_column =	\
				YYRHSLOC(Rhs, 0).last_column;			\
			(Current).file = YYRHSLOC (Rhs, 0).file;		\
		}								\
		(Current).next = NULL;						\
	} while (0)


extern void srcpos_update(struct srcpos *pos, const char *text, int len);
extern struct srcpos *srcpos_copy(struct srcpos *pos);
extern struct srcpos *srcpos_extend(struct srcpos *new_srcpos,
				    struct srcpos *old_srcpos);
extern void srcpos_free(struct srcpos *pos);
extern char *srcpos_string(struct srcpos *pos);
extern char *srcpos_string_first(struct srcpos *pos, int level);
extern char *srcpos_string_last(struct srcpos *pos, int level);


extern void PRINTF(3, 0) srcpos_verror(struct srcpos *pos, const char *prefix,
					const char *fmt, va_list va);
extern void PRINTF(3, 4) srcpos_error(struct srcpos *pos, const char *prefix,
				      const char *fmt, ...);

extern void srcpos_set_line(char *f, int l);

#endif /* SRCPOS_H */
