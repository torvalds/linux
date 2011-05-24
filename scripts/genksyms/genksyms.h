/* Generate kernel symbol version hashes.
   Copyright 1996, 1997 Linux International.

   New implementation contributed by Richard Henderson <rth@tamu.edu>
   Based on original work by Bjorn Ekwall <bj0rn@blox.se>

   This file is part of the Linux modutils.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2 of the License, or (at your
   option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

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
extern char *cur_filename;

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

void error_with_pos(const char *, ...);

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
