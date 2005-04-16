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


enum symbol_type
{
  SYM_NORMAL, SYM_TYPEDEF, SYM_ENUM, SYM_STRUCT, SYM_UNION
};

struct string_list
{
  struct string_list *next;
  enum symbol_type tag;
  char *string;
};

struct symbol
{
  struct symbol *hash_next;
  const char *name;
  enum symbol_type type;
  struct string_list *defn;
  struct symbol *expansion_trail;
  int is_extern;
};

typedef struct string_list **yystype;
#define YYSTYPE yystype

extern FILE *outfile, *debugfile;

extern int cur_line;
extern char *cur_filename, *output_directory;

extern int flag_debug, flag_dump_defs, flag_warnings;
extern int checksum_version, kernel_version;

extern int want_brace_phrase, want_exp_phrase, discard_phrase_contents;
extern struct string_list *current_list, *next_list;


struct symbol *find_symbol(const char *name, enum symbol_type ns);
struct symbol *add_symbol(const char *name, enum symbol_type type,
			   struct string_list *defn, int is_extern);
void export_symbol(const char *);

struct string_list *reset_list(void);
void free_list(struct string_list *s, struct string_list *e);
void free_node(struct string_list *list);
struct string_list *copy_node(struct string_list *);
struct string_list *copy_list(struct string_list *s, struct string_list *e);
int equal_list(struct string_list *a, struct string_list *b);
void print_list(FILE *, struct string_list *list);

int yylex(void);
int yyparse(void);

void error_with_pos(const char *, ...);

#define version(a,b,c)  ((a << 16) | (b << 8) | (c))

/*----------------------------------------------------------------------*/

#define MODUTILS_VERSION "<in-kernel>"

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

#endif /* genksyms.h */
