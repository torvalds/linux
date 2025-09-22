/****************************************************************************
 * Copyright 2018-2019,2020 Thomas E. Dickey                                *
 * Copyright 2009-2013,2017 Free Software Foundation, Inc.                  *
 *                                                                          *
 * Permission is hereby granted, free of charge, to any person obtaining a  *
 * copy of this software and associated documentation files (the            *
 * "Software"), to deal in the Software without restriction, including      *
 * without limitation the rights to use, copy, modify, merge, publish,      *
 * distribute, distribute with modifications, sublicense, and/or sell       *
 * copies of the Software, and to permit persons to whom the Software is    *
 * furnished to do so, subject to the following conditions:                 *
 *                                                                          *
 * The above copyright notice and this permission notice shall be included  *
 * in all copies or substantial portions of the Software.                   *
 *                                                                          *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS  *
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF               *
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.   *
 * IN NO EVENT SHALL THE ABOVE COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,   *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR    *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR    *
 * THE USE OR OTHER DEALINGS IN THE SOFTWARE.                               *
 *                                                                          *
 * Except as contained in this notice, the name(s) of the above copyright   *
 * holders shall not be used in advertising or otherwise to promote the     *
 * sale, use or other dealings in this Software without prior written       *
 * authorization.                                                           *
 ****************************************************************************/

/****************************************************************************
 *  Author: Zeyd M. Ben-Halim <zmbenhal@netcom.com> 1992,1995               *
 *     and: Eric S. Raymond <esr@snark.thyrsus.com>                         *
 *     and: Thomas E. Dickey                        1996-on                 *
 ****************************************************************************/

/*
 *	make_hash.c --- build-time program for constructing comp_captab.c
 */

#include <build.priv.h>

#include <tic.h>
#include <hashsize.h>

#include <ctype.h>

MODULE_ID("$Id: make_hash.c,v 1.1 2023/10/17 09:52:09 nicm Exp $")

/*
 *	_nc_make_hash_table()
 *
 *	Takes the entries in table[] and hashes them into hash_table[]
 *	by name.  There are CAPTABSIZE entries in the predefined table[]
 *	and HASHTABSIZE slots in hash_table[].
 *
 */

#undef MODULE_ID
#define MODULE_ID(id)		/*nothing */
#include <tinfo/doalloc.c>

#define L_PAREN "("
#define R_PAREN ")"
#define L_BRACE "{"
#define R_BRACE "}"

static const char *typenames[] =
{"BOOLEAN", "NUMBER", "STRING"};

static void
failed(const char *s)
{
    perror(s);
    exit(EXIT_FAILURE);
}

static char *
strmalloc(char *s)
{
    size_t need = strlen(s) + 1;
    char *result = malloc(need);
    if (result == 0)
	failed("strmalloc");
    _nc_STRCPY(result, s, need);
    return result;
}

/*
 *	int hash_function(string)
 *
 *	Computes the hashing function on the given string.
 *
 *	The current hash function is the sum of each consecutive pair
 *	of characters, taken as two-byte integers, mod HASHTABSIZE.
 *
 */

static int
hash_function(const char *string)
{
    long sum = 0;

    while (*string) {
	sum += (long) (UChar(*string) + (UChar(*(string + 1)) << 8));
	string++;
    }

    return (int) (sum % HASHTABSIZE);
}

#define UNUSED -1

static void
_nc_make_hash_table(struct user_table_entry *table,
		    HashValue * hash_table,
		    unsigned tablesize)
{
    unsigned i;
    int hashvalue;
    int collisions = 0;

    for (i = 0; i < HASHTABSIZE; i++) {
	hash_table[i] = UNUSED;
    }
    for (i = 0; i < tablesize; i++) {
	hashvalue = hash_function(table[i].ute_name);

	if (hash_table[hashvalue] >= 0)
	    collisions++;

	if (hash_table[hashvalue] != UNUSED) {
	    table[i].ute_link = hash_table[hashvalue];
	}
	hash_table[hashvalue] = (HashValue) i;
    }

    printf("/* %d collisions out of %d entries */\n", collisions, tablesize);
}

/*
 * This filter reads from standard input a list of tab-delimited columns,
 * (e.g., from Caps.filtered) computes the hash-value of a specified column and
 * writes the hashed tables to standard output.
 *
 * By compiling the hash table at build time, we're able to make the entire
 * set of terminfo and termcap tables readonly (and also provide some runtime
 * performance enhancement).
 */

#define MAX_COLUMNS BUFSIZ	/* this _has_ to be worst-case */

static int
count_columns(char **list)
{
    int result = 0;
    if (list != 0) {
	while (*list++) {
	    ++result;
	}
    }
    return result;
}

static char **
parse_columns(char *buffer)
{
    static char **list;

    int col = 0;

    if (buffer == 0) {
	free(list);
	list = 0;
	return 0;
    }

    if (*buffer != '#') {
	if (list == 0) {
	    list = typeCalloc(char *, (MAX_COLUMNS + 1));
	    if (list == 0)
		return (0);
	}
	while (*buffer != '\0') {
	    char *s;
	    for (s = buffer; (*s != '\0') && !isspace(UChar(*s)); s++)
		/*EMPTY */ ;
	    if (s != buffer) {
		char mark = *s;
		*s = '\0';
		if ((s - buffer) > 1
		    && (*buffer == '"')
		    && (s[-1] == '"')) {	/* strip the quotes */
		    assert(s > buffer + 1);
		    s[-1] = '\0';
		    buffer++;
		}
		list[col] = buffer;
		col++;
		if (mark == '\0')
		    break;
		while (*++s && isspace(UChar(*s)))
		    /*EMPTY */ ;
		buffer = s;
	    } else
		break;
	}
    }
    return col ? list : 0;
}

#define SetType(n,t) \
	if (is_user) \
	    name_table[n].ute_type |= (int)(1 << (t)); \
	else \
	    name_table[n].ute_type = (t)

#define GetType(n) \
	(is_user \
	 ? get_type(name_table[n].ute_type) \
	 : typenames[name_table[n].ute_type])

static char *
get_type(int type_mask)
{
    static char result[80];
    unsigned n;
    _nc_STRCPY(result, L_PAREN, sizeof(result));
    for (n = 0; n < 3; ++n) {
	if ((1 << n) & type_mask) {
	    size_t want = 5 + strlen(typenames[n]);
	    if (want > sizeof(result)) {
		fprintf(stderr, "Buffer is not large enough for %s + %s\n",
			result, typenames[n]);
		exit(EXIT_FAILURE);
	    }
	    if (result[1])
		_nc_STRCAT(result, "|", sizeof(result));
	    _nc_STRCAT(result, "1<<", sizeof(result));
	    _nc_STRCAT(result, typenames[n], sizeof(result));
	}
    }
    _nc_STRCAT(result, R_PAREN, sizeof(result));
    return result;
}

int
main(int argc, char **argv)
{
    unsigned tablesize = CAPTABSIZE;
    struct user_table_entry *name_table = typeCalloc(struct
						     user_table_entry, tablesize);
    HashValue *hash_table = typeCalloc(HashValue, HASHTABSIZE);
    const char *root_name = "";
    int column = 0;
    int bigstring = 0;
    unsigned n;
    unsigned nn;
    unsigned tableused = 0;
    bool is_user;
    const char *table_name;
    char buffer[BUFSIZ];

    short BoolCount = 0;
    short NumCount = 0;
    short StrCount = 0;

    /* The first argument is the column-number (starting with 0).
     * The second is the root name of the tables to generate.
     */
    if (argc <= 3
	|| (column = atoi(argv[1])) <= 0
	|| (column >= MAX_COLUMNS)
	|| *(root_name = argv[2]) == 0
	|| (bigstring = atoi(argv[3])) < 0
	|| name_table == 0
	|| hash_table == 0) {
	fprintf(stderr, "usage: make_hash column root_name bigstring\n");
	exit(EXIT_FAILURE);
    }
    is_user = (*root_name == 'u');
    table_name = (is_user ? "user" : "name");

    /*
     * Read the table into our arrays.
     */
    for (n = 0; (n < tablesize) && fgets(buffer, BUFSIZ, stdin);) {
	char **list;
	char *nlp = strchr(buffer, '\n');
	if (nlp)
	    *nlp = '\0';
	else
	    buffer[sizeof(buffer) - 2] = '\0';
	list = parse_columns(buffer);
	if (list == 0)		/* blank or comment */
	    continue;
	if (is_user) {
	    if (strcmp(list[0], "userdef"))
		continue;
	} else if (!strcmp(list[0], "userdef")) {
	    continue;
	}
	if (column < 0 || column > count_columns(list)) {
	    fprintf(stderr, "expected %d columns, have %d:\n%s\n",
		    column,
		    count_columns(list),
		    buffer);
	    exit(EXIT_FAILURE);
	}
	nn = tableused;
	if (is_user) {
	    unsigned j;
	    for (j = 0; j < tableused; ++j) {
		if (!strcmp(list[column], name_table[j].ute_name)) {
		    nn = j;
		    break;
		}
	    }
	}
	if (nn == tableused) {
	    name_table[nn].ute_link = -1;	/* end-of-hash */
	    name_table[nn].ute_name = strmalloc(list[column]);
	    ++tableused;
	}

	if (!strcmp(list[2], "bool")) {
	    SetType(nn, BOOLEAN);
	    name_table[nn].ute_index = BoolCount++;
	} else if (!strcmp(list[2], "num")) {
	    SetType(nn, NUMBER);
	    name_table[nn].ute_index = NumCount++;
	} else if (!strcmp(list[2], "str")) {
	    SetType(nn, STRING);
	    name_table[nn].ute_index = StrCount++;
	    if (is_user) {
		if (*list[3] != '-') {
		    unsigned j;
		    name_table[nn].ute_argc = (unsigned) strlen(list[3]);
		    for (j = 0; j < name_table[nn].ute_argc; ++j) {
			if (list[3][j] == 's') {
			    name_table[nn].ute_args |= (1U << j);
			}
		    }
		}
	    }
	} else {
	    fprintf(stderr, "Unknown type: %s\n", list[2]);
	    exit(EXIT_FAILURE);
	}
	n++;
    }
    if (tablesize > tableused)
	tablesize = tableused;
    _nc_make_hash_table(name_table, hash_table, tablesize);

    /*
     * Write the compiled tables to standard output
     */
    if (bigstring) {
	int len = 0;
	int nxt;

	printf("static const char %s_names_text[] = \\\n", root_name);
	for (n = 0; n < tablesize; n++) {
	    nxt = (int) strlen(name_table[n].ute_name) + 5;
	    if (nxt + len > 72) {
		printf("\\\n");
		len = 0;
	    }
	    printf("\"%s\\0\" ", name_table[n].ute_name);
	    len += nxt;
	}
	printf(";\n\n");

	len = 0;
	printf("static %s_table_data const %s_names_data[] =\n",
	       table_name,
	       root_name);
	printf("%s\n", L_BRACE);
	for (n = 0; n < tablesize; n++) {
	    printf("\t%s %15d,\t%10s,", L_BRACE, len, GetType(n));
	    if (is_user)
		printf("\t%d,%d,",
		       name_table[n].ute_argc,
		       name_table[n].ute_args);
	    printf("\t%3d, %3d %s%c\n",
		   name_table[n].ute_index,
		   name_table[n].ute_link,
		   R_BRACE,
		   n < tablesize - 1 ? ',' : ' ');
	    len += (int) strlen(name_table[n].ute_name) + 1;
	}
	printf("%s;\n\n", R_BRACE);
	printf("static struct %s_table_entry *_nc_%s_table = 0;\n\n",
	       table_name,
	       root_name);
    } else {

	printf("static struct %s_table_entry const _nc_%s_table[] =\n",
	       table_name,
	       root_name);
	printf("%s\n", L_BRACE);
	for (n = 0; n < tablesize; n++) {
	    _nc_SPRINTF(buffer, _nc_SLIMIT(sizeof(buffer)) "\"%s\"",
			name_table[n].ute_name);
	    printf("\t%s %15s,\t%10s,", L_BRACE, buffer, GetType(n));
	    if (is_user)
		printf("\t%d,%d,",
		       name_table[n].ute_argc,
		       name_table[n].ute_args);
	    printf("\t%3d, %3d %s%c\n",
		   name_table[n].ute_index,
		   name_table[n].ute_link,
		   R_BRACE,
		   n < tablesize - 1 ? ',' : ' ');
	}
	printf("%s;\n\n", R_BRACE);
    }

    printf("static const HashValue _nc_%s_hash_table[%d] =\n",
	   root_name,
	   HASHTABSIZE + 1);
    printf("%s\n", L_BRACE);
    for (n = 0; n < HASHTABSIZE; n++) {
	printf("\t%3d,\n", hash_table[n]);
    }
    printf("\t0\t/* base-of-table */\n");
    printf("%s;\n\n", R_BRACE);

    if (!is_user) {
	printf("#if (BOOLCOUNT!=%d)||(NUMCOUNT!=%d)||(STRCOUNT!=%d)\n",
	       BoolCount, NumCount, StrCount);
	printf("#error\t--> term.h and comp_captab.c disagree about the <--\n");
	printf("#error\t--> numbers of booleans, numbers and/or strings <--\n");
	printf("#endif\n\n");
    }

    free(hash_table);
    for (n = 0; (n < tablesize); ++n) {
	free((void *) name_table[n].ute_name);
    }
    free(name_table);
    parse_columns(0);

    return EXIT_SUCCESS;
}
