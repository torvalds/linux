# if 0
/*	$NetBSD: rcorder.c,v 1.7 2000/08/04 07:33:55 enami Exp $	*/
#endif

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998, 1999 Matthew R. Green
 * All rights reserved.
 * Copyright (c) 1998
 * 	Perry E. Metzger.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed for the NetBSD Project
 *	by Perry E. Metzger.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
__FBSDID("$FreeBSD$");

#include <sys/stat.h>

#include <err.h>
#include <stdio.h>
#include <libutil.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ealloc.h"
#include "sprite.h"
#include "hash.h"

#ifdef DEBUG
static int debug = 0;
# define	DPRINTF(args) if (debug) { fflush(stdout); fprintf args; }
#else
# define	DPRINTF(args)
#endif

#define REQUIRE_STR	"# REQUIRE:"
#define REQUIRE_LEN	(sizeof(REQUIRE_STR) - 1)
#define REQUIRES_STR	"# REQUIRES:"
#define REQUIRES_LEN	(sizeof(REQUIRES_STR) - 1)
#define PROVIDE_STR	"# PROVIDE:"
#define PROVIDE_LEN	(sizeof(PROVIDE_STR) - 1)
#define PROVIDES_STR	"# PROVIDES:"
#define PROVIDES_LEN	(sizeof(PROVIDES_STR) - 1)
#define BEFORE_STR	"# BEFORE:"
#define BEFORE_LEN	(sizeof(BEFORE_STR) - 1)
#define KEYWORD_STR	"# KEYWORD:"
#define KEYWORD_LEN	(sizeof(KEYWORD_STR) - 1)
#define KEYWORDS_STR	"# KEYWORDS:"
#define KEYWORDS_LEN	(sizeof(KEYWORDS_STR) - 1)

static int exit_code;
static int file_count;
static char **file_list;

typedef int bool;
#define TRUE 1
#define FALSE 0
typedef bool flag;
#define SET TRUE
#define RESET FALSE

static Hash_Table provide_hash_s, *provide_hash;

typedef struct provnode provnode;
typedef struct filenode filenode;
typedef struct f_provnode f_provnode;
typedef struct f_reqnode f_reqnode;
typedef struct strnodelist strnodelist;

struct provnode {
	flag		head;
	flag		in_progress;
	filenode	*fnode;
	provnode	*next, *last;
};

struct f_provnode {
	provnode	*pnode;
	f_provnode	*next;
};

struct f_reqnode {
	Hash_Entry	*entry;
	f_reqnode	*next;
};

struct strnodelist {
	filenode	*node;
	strnodelist	*next;
	char		s[1];
};

struct filenode {
	char		*filename;
	flag		in_progress;
	filenode	*next, *last;
	f_reqnode	*req_list;
	f_provnode	*prov_list;
	strnodelist	*keyword_list;
};

static filenode fn_head_s, *fn_head;

static strnodelist *bl_list;
static strnodelist *keep_list;
static strnodelist *skip_list;

static void do_file(filenode *fnode);
static void strnode_add(strnodelist **, char *, filenode *);
static int skip_ok(filenode *fnode);
static int keep_ok(filenode *fnode);
static void satisfy_req(f_reqnode *rnode, char *filename);
static void crunch_file(char *);
static void parse_require(filenode *, char *);
static void parse_provide(filenode *, char *);
static void parse_before(filenode *, char *);
static void parse_keywords(filenode *, char *);
static filenode *filenode_new(char *);
static void add_require(filenode *, char *);
static void add_provide(filenode *, char *);
static void add_before(filenode *, char *);
static void add_keyword(filenode *, char *);
static void insert_before(void);
static Hash_Entry *make_fake_provision(filenode *);
static void crunch_all_files(void);
static void initialize(void);
static void generate_ordering(void);

int
main(int argc, char *argv[])
{
	int ch;

	while ((ch = getopt(argc, argv, "dk:s:")) != -1)
		switch (ch) {
		case 'd':
#ifdef DEBUG
			debug = 1;
#else
			warnx("debugging not compiled in, -d ignored");
#endif
			break;
		case 'k':
			strnode_add(&keep_list, optarg, 0);
			break;
		case 's':
			strnode_add(&skip_list, optarg, 0);
			break;
		default:
			/* XXX should crunch it? */
			break;
		}
	argc -= optind;
	argv += optind;

	file_count = argc;
	file_list = argv;

	DPRINTF((stderr, "parse_args\n"));
	initialize();
	DPRINTF((stderr, "initialize\n"));
	crunch_all_files();
	DPRINTF((stderr, "crunch_all_files\n"));
	generate_ordering();
	DPRINTF((stderr, "generate_ordering\n"));

	exit(exit_code);
}

/*
 * initialise various variables.
 */
static void
initialize(void)
{

	fn_head = &fn_head_s;

	provide_hash = &provide_hash_s;
	Hash_InitTable(provide_hash, file_count);
}

/* generic function to insert a new strnodelist element */
static void
strnode_add(strnodelist **listp, char *s, filenode *fnode)
{
	strnodelist *ent;

	ent = emalloc(sizeof *ent + strlen(s));
	ent->node = fnode;
	strcpy(ent->s, s);
	ent->next = *listp;
	*listp = ent;
}

/*
 * below are the functions that deal with creating the lists
 * from the filename's given dependencies and provisions
 * in each of these files.  no ordering or checking is done here.
 */

/*
 * we have a new filename, create a new filenode structure.
 * fill in the bits, and put it in the filenode linked list
 */
static filenode *
filenode_new(char *filename)
{
	filenode *temp;

	temp = emalloc(sizeof(*temp));
	memset(temp, 0, sizeof(*temp));
	temp->filename = estrdup(filename);
	temp->req_list = NULL;
	temp->prov_list = NULL;
	temp->keyword_list = NULL;
	temp->in_progress = RESET;
	/*
	 * link the filenode into the list of filenodes.
	 * note that the double linking means we can delete a
	 * filenode without searching for where it belongs.
	 */
	temp->next = fn_head->next;
	if (temp->next != NULL)
		temp->next->last = temp;
	temp->last = fn_head;
	fn_head->next = temp;
	return (temp);
}

/*
 * add a requirement to a filenode.
 */
static void
add_require(filenode *fnode, char *s)
{
	Hash_Entry *entry;
	f_reqnode *rnode;
	int new;

	entry = Hash_CreateEntry(provide_hash, s, &new);
	if (new)
		Hash_SetValue(entry, NULL);
	rnode = emalloc(sizeof(*rnode));
	rnode->entry = entry;
	rnode->next = fnode->req_list;
	fnode->req_list = rnode;
}

/*
 * add a provision to a filenode.  if this provision doesn't
 * have a head node, create one here.
 */
static void
add_provide(filenode *fnode, char *s)
{
	Hash_Entry *entry;
	f_provnode *f_pnode;
	provnode *pnode, *head;
	int new;

	entry = Hash_CreateEntry(provide_hash, s, &new);
	head = Hash_GetValue(entry);

	/* create a head node if necessary. */
	if (head == NULL) {
		head = emalloc(sizeof(*head));
		head->head = SET;
		head->in_progress = RESET;
		head->fnode = NULL;
		head->last = head->next = NULL;
		Hash_SetValue(entry, head);
	}
#if 0
	/*
	 * Don't warn about this.  We want to be able to support
	 * scripts that do two complex things:
	 *
	 *	- Two independent scripts which both provide the
	 *	  same thing.  Both scripts must be executed in
	 *	  any order to meet the barrier.  An example:
	 *
	 *		Script 1:
	 *
	 *			PROVIDE: mail
	 *			REQUIRE: LOGIN
	 *
	 *		Script 2:
	 *
	 *			PROVIDE: mail
	 *			REQUIRE: LOGIN
	 *
	 * 	- Two interdependent scripts which both provide the
	 *	  same thing.  Both scripts must be executed in
	 *	  graph order to meet the barrier.  An example:
	 *
	 *		Script 1:
	 *
	 *			PROVIDE: nameservice dnscache
	 *			REQUIRE: SERVERS
	 *
	 *		Script 2:
	 *
	 *			PROVIDE: nameservice nscd
	 *			REQUIRE: dnscache
	 */
	else if (new == 0) {
		warnx("file `%s' provides `%s'.", fnode->filename, s);
		warnx("\tpreviously seen in `%s'.",
		    head->next->fnode->filename);
	}
#endif

	pnode = emalloc(sizeof(*pnode));
	pnode->head = RESET;
	pnode->in_progress = RESET;
	pnode->fnode = fnode;
	pnode->next = head->next;
	pnode->last = head;
	head->next = pnode;
	if (pnode->next != NULL)
		pnode->next->last = pnode;

	f_pnode = emalloc(sizeof(*f_pnode));
	f_pnode->pnode = pnode;
	f_pnode->next = fnode->prov_list;
	fnode->prov_list = f_pnode;
}

/*
 * put the BEFORE: lines to a list and handle them later.
 */
static void
add_before(filenode *fnode, char *s)
{
	strnodelist *bf_ent;

	bf_ent = emalloc(sizeof *bf_ent + strlen(s));
	bf_ent->node = fnode;
	strcpy(bf_ent->s, s);
	bf_ent->next = bl_list;
	bl_list = bf_ent;
}

/*
 * add a key to a filenode.
 */
static void
add_keyword(filenode *fnode, char *s)
{

	strnode_add(&fnode->keyword_list, s, fnode);
}

/*
 * loop over the rest of a REQUIRE line, giving each word to
 * add_require() to do the real work.
 */
static void
parse_require(filenode *node, char *buffer)
{
	char *s;
	
	while ((s = strsep(&buffer, " \t\n")) != NULL)
		if (*s != '\0')
			add_require(node, s);
}

/*
 * loop over the rest of a PROVIDE line, giving each word to
 * add_provide() to do the real work.
 */
static void
parse_provide(filenode *node, char *buffer)
{
	char *s;
	
	while ((s = strsep(&buffer, " \t\n")) != NULL)
		if (*s != '\0')
			add_provide(node, s);
}

/*
 * loop over the rest of a BEFORE line, giving each word to
 * add_before() to do the real work.
 */
static void
parse_before(filenode *node, char *buffer)
{
	char *s;
	
	while ((s = strsep(&buffer, " \t\n")) != NULL)
		if (*s != '\0')
			add_before(node, s);
}

/*
 * loop over the rest of a KEYWORD line, giving each word to
 * add_keyword() to do the real work.
 */
static void
parse_keywords(filenode *node, char *buffer)
{
	char *s;
	
	while ((s = strsep(&buffer, " \t\n")) != NULL)
		if (*s != '\0')
			add_keyword(node, s);
}

/*
 * given a file name, create a filenode for it, read in lines looking
 * for provision and requirement lines, building the graphs as needed.
 */
static void
crunch_file(char *filename)
{
	FILE *fp;
	char *buf;
	int require_flag, provide_flag, before_flag, keywords_flag;
	enum { BEFORE_PARSING, PARSING, PARSING_DONE } state;
	filenode *node;
	char delims[3] = { '\\', '\\', '\0' };
	struct stat st;

	if ((fp = fopen(filename, "r")) == NULL) {
		warn("could not open %s", filename);
		return;
	}

	if (fstat(fileno(fp), &st) == -1) {
		warn("could not stat %s", filename);
		fclose(fp);
		return;
	}

	if (!S_ISREG(st.st_mode)) {
#if 0
		warnx("%s is not a file", filename);
#endif
		fclose(fp);
		return;
	}

	node = filenode_new(filename);

	/*
	 * we don't care about length, line number, don't want # for comments,
	 * and have no flags.
	 */
	for (state = BEFORE_PARSING; state != PARSING_DONE &&
	    (buf = fparseln(fp, NULL, NULL, delims, 0)) != NULL; free(buf)) {
		require_flag = provide_flag = before_flag = keywords_flag = 0;
		if (strncmp(REQUIRE_STR, buf, REQUIRE_LEN) == 0)
			require_flag = REQUIRE_LEN;
		else if (strncmp(REQUIRES_STR, buf, REQUIRES_LEN) == 0)
			require_flag = REQUIRES_LEN;
		else if (strncmp(PROVIDE_STR, buf, PROVIDE_LEN) == 0)
			provide_flag = PROVIDE_LEN;
		else if (strncmp(PROVIDES_STR, buf, PROVIDES_LEN) == 0)
			provide_flag = PROVIDES_LEN;
		else if (strncmp(BEFORE_STR, buf, BEFORE_LEN) == 0)
			before_flag = BEFORE_LEN;
		else if (strncmp(KEYWORD_STR, buf, KEYWORD_LEN) == 0)
			keywords_flag = KEYWORD_LEN;
		else if (strncmp(KEYWORDS_STR, buf, KEYWORDS_LEN) == 0)
			keywords_flag = KEYWORDS_LEN;
		else {
			if (state == PARSING)
				state = PARSING_DONE;
			continue;
		}

		state = PARSING;
		if (require_flag)
			parse_require(node, buf + require_flag);
		else if (provide_flag)
			parse_provide(node, buf + provide_flag);
		else if (before_flag)
			parse_before(node, buf + before_flag);
		else if (keywords_flag)
			parse_keywords(node, buf + keywords_flag);
	}
	fclose(fp);
}

static Hash_Entry *
make_fake_provision(filenode *node)
{
	Hash_Entry *entry;
	f_provnode *f_pnode;
	provnode *head, *pnode;
	static	int i = 0;
	int	new;
	char buffer[30];

	do {
		snprintf(buffer, sizeof buffer, "fake_prov_%08d", i++);
		entry = Hash_CreateEntry(provide_hash, buffer, &new);
	} while (new == 0);
	head = emalloc(sizeof(*head));
	head->head = SET;
	head->in_progress = RESET;
	head->fnode = NULL;
	head->last = head->next = NULL;
	Hash_SetValue(entry, head);

	pnode = emalloc(sizeof(*pnode));
	pnode->head = RESET;
	pnode->in_progress = RESET;
	pnode->fnode = node;
	pnode->next = head->next;
	pnode->last = head;
	head->next = pnode;
	if (pnode->next != NULL)
		pnode->next->last = pnode;

	f_pnode = emalloc(sizeof(*f_pnode));
	f_pnode->pnode = pnode;
	f_pnode->next = node->prov_list;
	node->prov_list = f_pnode;

	return (entry);
}

/*
 * go through the BEFORE list, inserting requirements into the graph(s)
 * as required.  in the before list, for each entry B, we have a file F
 * and a string S.  we create a "fake" provision (P) that F provides.
 * for each entry in the provision list for S, add a requirement to
 * that provisions filenode for P.
 */
static void
insert_before(void)
{
	Hash_Entry *entry, *fake_prov_entry;
	provnode *pnode;
	f_reqnode *rnode;
	strnodelist *bl;
	int new;
	
	while (bl_list != NULL) {
		bl = bl_list->next;

		fake_prov_entry = make_fake_provision(bl_list->node);

		entry = Hash_CreateEntry(provide_hash, bl_list->s, &new);
		if (new == 1)
			warnx("file `%s' is before unknown provision `%s'", bl_list->node->filename, bl_list->s);

		for (pnode = Hash_GetValue(entry); pnode; pnode = pnode->next) {
			if (pnode->head)
				continue;

			rnode = emalloc(sizeof(*rnode));
			rnode->entry = fake_prov_entry;
			rnode->next = pnode->fnode->req_list;
			pnode->fnode->req_list = rnode;
		}

		free(bl_list);
		bl_list = bl;
	}
}

/*
 * loop over all the files calling crunch_file() on them to do the
 * real work.  after we have built all the nodes, insert the BEFORE:
 * lines into graph(s).
 */
static void
crunch_all_files(void)
{
	int i;
	
	for (i = 0; i < file_count; i++)
		crunch_file(file_list[i]);
	insert_before();
}

/*
 * below are the functions that traverse the graphs we have built
 * finding out the desired ordering, printing each file in turn.
 * if missing requirements, or cyclic graphs are detected, a
 * warning will be issued, and we will continue on..
 */

/*
 * given a requirement node (in a filename) we attempt to satisfy it.
 * we do some sanity checking first, to ensure that we have providers,
 * aren't already satisfied and aren't already being satisfied (ie,
 * cyclic).  if we pass all this, we loop over the provision list
 * calling do_file() (enter recursion) for each filenode in this
 * provision.
 */
static void
satisfy_req(f_reqnode *rnode, char *filename)
{
	Hash_Entry *entry;
	provnode *head;

	entry = rnode->entry;
	head = Hash_GetValue(entry);

	if (head == NULL) {
		warnx("requirement `%s' in file `%s' has no providers.",
		    Hash_GetKey(entry), filename);
		exit_code = 1;
		return;
	}

	/* return if the requirement is already satisfied. */
	if (head->next == NULL)
		return;

	/* 
	 * if list is marked as in progress,
	 *	print that there is a circular dependency on it and abort
	 */
	if (head->in_progress == SET) {
		warnx("Circular dependency on provision `%s' in file `%s'.",
		    Hash_GetKey(entry), filename);
		exit_code = 1;
		return;
	}

	head->in_progress = SET;
	
	/*
	 * while provision_list is not empty
	 *	do_file(first_member_of(provision_list));
	 */
	while (head->next != NULL)
		do_file(head->next->fnode);
}

static int
skip_ok(filenode *fnode)
{
	strnodelist *s;
	strnodelist *k;

	for (s = skip_list; s; s = s->next)
		for (k = fnode->keyword_list; k; k = k->next)
			if (strcmp(k->s, s->s) == 0)
				return (0);

	return (1);
}

static int
keep_ok(filenode *fnode)
{
	strnodelist *s;
	strnodelist *k;

	for (s = keep_list; s; s = s->next)
		for (k = fnode->keyword_list; k; k = k->next)
			if (strcmp(k->s, s->s) == 0)
				return (1);

	/* an empty keep_list means every one */
	return (!keep_list);
}

/*
 * given a filenode, we ensure we are not a cyclic graph.  if this
 * is ok, we loop over the filenodes requirements, calling satisfy_req()
 * for each of them.. once we have done this, remove this filenode
 * from each provision table, as we are now done.
 *
 * NOTE: do_file() is called recursively from several places and cannot
 * safely free() anything related to items that may be recursed on.
 * Circular dependencies will cause problems if we do.
 */
static void
do_file(filenode *fnode)
{
	f_reqnode *r;
	f_provnode *p, *p_tmp;
	provnode *pnode;
	int was_set;	

	DPRINTF((stderr, "do_file on %s.\n", fnode->filename));

	/*
	 * if fnode is marked as in progress,
	 *	 print that fnode; is circularly depended upon and abort.
	 */
	if (fnode->in_progress == SET) {
		warnx("Circular dependency on file `%s'.",
			fnode->filename);
		was_set = exit_code = 1;
	} else
		was_set = 0;

	/* mark fnode */
	fnode->in_progress = SET;

	/*
	 * for each requirement of fnode -> r
	 *	satisfy_req(r, filename)
	 */
	r = fnode->req_list;
	while (r != NULL) {
		satisfy_req(r, fnode->filename);
		r = r->next;
	}
	fnode->req_list = NULL;

	/*
	 * for each provision of fnode -> p
	 *	remove fnode from provision list for p in hash table
	 */
	p = fnode->prov_list;
	while (p != NULL) {
		p_tmp = p;
		pnode = p->pnode;
		if (pnode->next != NULL) {
			pnode->next->last = pnode->last;
		}
		if (pnode->last != NULL) {
			pnode->last->next = pnode->next;
		}
		free(pnode);
		p = p->next;
		free(p_tmp);
	}
	fnode->prov_list = NULL;

	/* do_it(fnode) */
	DPRINTF((stderr, "next do: "));

	/* if we were already in progress, don't print again */
	if (was_set == 0 && skip_ok(fnode) && keep_ok(fnode))
		printf("%s\n", fnode->filename);
	
	if (fnode->next != NULL) {
		fnode->next->last = fnode->last;
	}
	if (fnode->last != NULL) {
		fnode->last->next = fnode->next;
	}

	DPRINTF((stderr, "nuking %s\n", fnode->filename));
#if 0
	if (was_set == 0) {
   		free(fnode->filename);
   		free(fnode);
	}
#endif
}

static void
generate_ordering(void)
{

	/*
	 * while there remain undone files{f},
	 *	pick an arbitrary f, and do_file(f)
	 * Note that the first file in the file list is perfectly
	 * arbitrary, and easy to find, so we use that.
	 */

	/*
	 * N.B.: the file nodes "self delete" after they execute, so
	 * after each iteration of the loop, the head will be pointing
	 * to something totally different. The loop ends up being
	 * executed only once for every strongly connected set of
	 * nodes.
	 */
	while (fn_head->next != NULL) {
		DPRINTF((stderr, "generate on %s\n", fn_head->next->filename));
		do_file(fn_head->next);
	}
}
