/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Generate kernel symbol version hashes.
   Copyright 1996, 1997 Linux International.

   New implementation contributed by Richard Henderson <rth@tamu.edu>
   Based on original work by Bjorn Ekwall <bj0rn@blox.se>

   This file is part of the Linux modutils.

 */

#ifndef MODUTILS_GENKSYMS_H
#define MODUTILS_GENKSYMS_H 1

#include <stdio.h>

enum symbol_type {
	SYM_NORMAL, SYM_TYPEDEF, SYM_ENUM, SYM_STRUCT, SYM_UNION,
	SYM_ENUM_CONST
};

enum symbol_status {
	STATUS_UNCHANGED, STATUS_DEFINED, STATUS_MODIFIED
};

struct string_list {
	struct string_list *next;
	enum symbol_type tag;
	int in_source_file;
	char *string;
};

struct symbol {
	struct symbol *hash_next;
	const char *name;
	enum symbol_type type;
	struct string_list *defn;
	struct symbol *expansion_trail;
	struct symbol *visited;
	int is_extern;
	int is_declared;
	enum symbol_status status;
	int is_override;
};

typedef struct string_list **yystype;
#define YYSTYPE yystype

extern int cur_line;
extern char *cur_filename, *source_file;
extern int in_source_file;

struct symbol *find_symbol(const char *name, enum symbol_type ns, int exact);
struct symbol *add_symbol(const char *name, enum symbol_type type,
			  struct string_list *defn, int is_extern);
void export_symbol(const char *);

void free_node(struct string_list *list);
void free_list(struct string_list *s, struct string_list *e);
struct string_list *copy_node(struct string_list *);
struct string_list *copy_list_range(struct string_list *start,
				    struct string_list *end);

int yylex(void);
int yyparse(void);

void error_with_pos(const char *, ...) __attribute__ ((format(printf, 1, 2)));

/*----------------------------------------------------------------------*/
#define xmalloc(size) ({ void *__ptr = malloc(size);		\
	if(!__ptr && size != 0) {				\
		fprintf(stderr, "out of memory\n");		\
		exit(1);					\
	}							\
	__ptr; })
#define xstrdup(str)  ({ char *__str = strdup(str);		\
	if (!__str) {						\
		fprintf(stderr, "out of memory\n");		\
		exit(1);					\
	}							\
	__str; })

#endif				/* genksyms.h */
