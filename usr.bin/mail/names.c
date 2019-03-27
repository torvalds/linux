/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1980, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
#if 0
static char sccsid[] = "@(#)names.c	8.1 (Berkeley) 6/6/93";
#endif
#endif /* not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Mail -- a mail program
 *
 * Handle name lists.
 */

#include "rcv.h"
#include <fcntl.h>
#include "extern.h"

/*
 * Allocate a single element of a name list,
 * initialize its name field to the passed
 * name and return it.
 */
struct name *
nalloc(char str[], int ntype)
{
	struct name *np;

	np = (struct name *)salloc(sizeof(*np));
	np->n_flink = NULL;
	np->n_blink = NULL;
	np->n_type = ntype;
	np->n_name = savestr(str);
	return (np);
}

/*
 * Find the tail of a list and return it.
 */
struct name *
tailof(struct name *name)
{
	struct name *np;

	np = name;
	if (np == NULL)
		return (NULL);
	while (np->n_flink != NULL)
		np = np->n_flink;
	return (np);
}

/*
 * Extract a list of names from a line,
 * and make a list of names from it.
 * Return the list or NULL if none found.
 */
struct name *
extract(char *line, int ntype)
{
	char *cp, *nbuf;
	struct name *top, *np, *t;

	if (line == NULL || *line == '\0')
		return (NULL);
	if ((nbuf = malloc(strlen(line) + 1)) == NULL)
		err(1, "Out of memory");
	top = NULL;
	np = NULL;
	cp = line;
	while ((cp = yankword(cp, nbuf)) != NULL) {
		t = nalloc(nbuf, ntype);
		if (top == NULL)
			top = t;
		else
			np->n_flink = t;
		t->n_blink = np;
		np = t;
	}
	(void)free(nbuf);
	return (top);
}

/*
 * Turn a list of names into a string of the same names.
 */
char *
detract(struct name *np, int ntype)
{
	int s, comma;
	char *cp, *top;
	struct name *p;

	comma = ntype & GCOMMA;
	if (np == NULL)
		return (NULL);
	ntype &= ~GCOMMA;
	s = 0;
	if (debug && comma)
		fprintf(stderr, "detract asked to insert commas\n");
	for (p = np; p != NULL; p = p->n_flink) {
		if (ntype && (p->n_type & GMASK) != ntype)
			continue;
		s += strlen(p->n_name) + 1;
		if (comma)
			s++;
	}
	if (s == 0)
		return (NULL);
	s += 2;
	top = salloc(s);
	cp = top;
	for (p = np; p != NULL; p = p->n_flink) {
		if (ntype && (p->n_type & GMASK) != ntype)
			continue;
		cp += strlcpy(cp, p->n_name, strlen(p->n_name) + 1);
		if (comma && p->n_flink != NULL)
			*cp++ = ',';
		*cp++ = ' ';
	}
	*--cp = '\0';
	if (comma && *--cp == ',')
		*cp = '\0';
	return (top);
}

/*
 * Grab a single word (liberal word)
 * Throw away things between ()'s, and take anything between <>.
 */
char *
yankword(char *ap, char *wbuf)
{
	char *cp, *cp2;

	cp = ap;
	for (;;) {
		if (*cp == '\0')
			return (NULL);
		if (*cp == '(') {
			int nesting = 0;

			while (*cp != '\0') {
				switch (*cp++) {
				case '(':
					nesting++;
					break;
				case ')':
					--nesting;
					break;
				}
				if (nesting <= 0)
					break;
			}
		} else if (*cp == ' ' || *cp == '\t' || *cp == ',')
			cp++;
		else
			break;
	}
	if (*cp ==  '<')
		for (cp2 = wbuf; *cp && (*cp2++ = *cp++) != '>';)
			;
	else
		for (cp2 = wbuf; *cp != '\0' && strchr(" \t,(", *cp) == NULL;
		    *cp2++ = *cp++)
			;
	*cp2 = '\0';
	return (cp);
}

/*
 * Grab a single login name (liberal word)
 * Throw away things between ()'s, take anything between <>,
 * and look for words before metacharacters %, @, !.
 */
char *
yanklogin(char *ap, char *wbuf)
{
	char *cp, *cp2, *cp_temp;
	int n;

	cp = ap;
	for (;;) {
		if (*cp == '\0')
			return (NULL);
		if (*cp == '(') {
			int nesting = 0;

			while (*cp != '\0') {
				switch (*cp++) {
				case '(':
					nesting++;
					break;
				case ')':
					--nesting;
					break;
				}
				if (nesting <= 0)
					break;
			}
		} else if (*cp == ' ' || *cp == '\t' || *cp == ',')
			cp++;
		else
			break;
	}

	/*
	 * Now, let's go forward till we meet the needed character,
	 * and step one word back.
	 */

	/* First, remember current point. */
	cp_temp = cp;
	n = 0;

	/*
	 * Note that we look ahead in a cycle. This is safe, since
	 * non-end of string is checked first.
	 */
	while(*cp != '\0' && strchr("@%!", *(cp + 1)) == NULL)
		cp++;

	/*
	 * Now, start stepping back to the first non-word character,
	 * while counting the number of symbols in a word.
	 */
	while(cp != cp_temp && strchr(" \t,<>", *(cp - 1)) == NULL) {
		n++;
		cp--;
	}

	/* Finally, grab the word forward. */
	cp2 = wbuf;
	while(n >= 0) {
		*cp2++=*cp++;
		n--;
	}

	*cp2 = '\0';
	return (cp);
}

/*
 * For each recipient in the passed name list with a /
 * in the name, append the message to the end of the named file
 * and remove him from the recipient list.
 *
 * Recipients whose name begins with | are piped through the given
 * program and removed.
 */
struct name *
outof(struct name *names, FILE *fo, struct header *hp)
{
	int c, ispipe;
	struct name *np, *top;
	time_t now;
	char *date, *fname;
	FILE *fout, *fin;

	top = names;
	np = names;
	(void)time(&now);
	date = ctime(&now);
	while (np != NULL) {
		if (!isfileaddr(np->n_name) && np->n_name[0] != '|') {
			np = np->n_flink;
			continue;
		}
		ispipe = np->n_name[0] == '|';
		if (ispipe)
			fname = np->n_name+1;
		else
			fname = expand(np->n_name);

		/*
		 * See if we have copied the complete message out yet.
		 * If not, do so.
		 */

		if (image < 0) {
			int fd;
			char tempname[PATHSIZE];

			(void)snprintf(tempname, sizeof(tempname),
			    "%s/mail.ReXXXXXXXXXX", tmpdir);
			if ((fd = mkstemp(tempname)) == -1 ||
			    (fout = Fdopen(fd, "a")) == NULL) {
				warn("%s", tempname);
				senderr++;
				goto cant;
			}
			image = open(tempname, O_RDWR);
			(void)rm(tempname);
			if (image < 0) {
				warn("%s", tempname);
				senderr++;
				(void)Fclose(fout);
				goto cant;
			}
			(void)fcntl(image, F_SETFD, 1);
			fprintf(fout, "From %s %s", myname, date);
			puthead(hp, fout,
			    GTO|GSUBJECT|GCC|GREPLYTO|GINREPLYTO|GNL);
			while ((c = getc(fo)) != EOF)
				(void)putc(c, fout);
			rewind(fo);
			fprintf(fout, "\n");
			(void)fflush(fout);
			if (ferror(fout)) {
				warn("%s", tempname);
				senderr++;
				(void)Fclose(fout);
				goto cant;
			}
			(void)Fclose(fout);
		}

		/*
		 * Now either copy "image" to the desired file
		 * or give it as the standard input to the desired
		 * program as appropriate.
		 */

		if (ispipe) {
			int pid;
			char *sh;
			sigset_t nset;

			/*
			 * XXX
			 * We can't really reuse the same image file,
			 * because multiple piped recipients will
			 * share the same lseek location and trample
			 * on one another.
			 */
			if ((sh = value("SHELL")) == NULL)
				sh = _PATH_CSHELL;
			(void)sigemptyset(&nset);
			(void)sigaddset(&nset, SIGHUP);
			(void)sigaddset(&nset, SIGINT);
			(void)sigaddset(&nset, SIGQUIT);
			pid = start_command(sh, &nset, image, -1, "-c", fname,
			    NULL);
			if (pid < 0) {
				senderr++;
				goto cant;
			}
			free_child(pid);
		} else {
			int f;
			if ((fout = Fopen(fname, "a")) == NULL) {
				warn("%s", fname);
				senderr++;
				goto cant;
			}
			if ((f = dup(image)) < 0) {
				warn("dup");
				fin = NULL;
			} else
				fin = Fdopen(f, "r");
			if (fin == NULL) {
				fprintf(stderr, "Can't reopen image\n");
				(void)Fclose(fout);
				senderr++;
				goto cant;
			}
			rewind(fin);
			while ((c = getc(fin)) != EOF)
				(void)putc(c, fout);
			if (ferror(fout)) {
				warnx("%s", fname);
				senderr++;
				(void)Fclose(fout);
				(void)Fclose(fin);
				goto cant;
			}
			(void)Fclose(fout);
			(void)Fclose(fin);
		}
cant:
		/*
		 * In days of old we removed the entry from the
		 * the list; now for sake of header expansion
		 * we leave it in and mark it as deleted.
		 */
		np->n_type |= GDEL;
		np = np->n_flink;
	}
	if (image >= 0) {
		(void)close(image);
		image = -1;
	}
	return (top);
}

/*
 * Determine if the passed address is a local "send to file" address.
 * If any of the network metacharacters precedes any slashes, it can't
 * be a filename.  We cheat with .'s to allow path names like ./...
 */
int
isfileaddr(char *name)
{
	char *cp;

	if (*name == '+')
		return (1);
	for (cp = name; *cp != '\0'; cp++) {
		if (*cp == '!' || *cp == '%' || *cp == '@')
			return (0);
		if (*cp == '/')
			return (1);
	}
	return (0);
}

/*
 * Map all of the aliased users in the invoker's mailrc
 * file and insert them into the list.
 * Changed after all these months of service to recursively
 * expand names (2/14/80).
 */

struct name *
usermap(struct name *names)
{
	struct name *new, *np, *cp;
	struct grouphead *gh;
	int metoo;

	new = NULL;
	np = names;
	metoo = (value("metoo") != NULL);
	while (np != NULL) {
		if (np->n_name[0] == '\\') {
			cp = np->n_flink;
			new = put(new, np);
			np = cp;
			continue;
		}
		gh = findgroup(np->n_name);
		cp = np->n_flink;
		if (gh != NULL)
			new = gexpand(new, gh, metoo, np->n_type);
		else
			new = put(new, np);
		np = cp;
	}
	return (new);
}

/*
 * Recursively expand a group name.  We limit the expansion to some
 * fixed level to keep things from going haywire.
 * Direct recursion is not expanded for convenience.
 */

struct name *
gexpand(struct name *nlist, struct grouphead *gh, int metoo, int ntype)
{
	struct group *gp;
	struct grouphead *ngh;
	struct name *np;
	static int depth;
	char *cp;

	if (depth > MAXEXP) {
		printf("Expanding alias to depth larger than %d\n", MAXEXP);
		return (nlist);
	}
	depth++;
	for (gp = gh->g_list; gp != NULL; gp = gp->ge_link) {
		cp = gp->ge_name;
		if (*cp == '\\')
			goto quote;
		if (strcmp(cp, gh->g_name) == 0)
			goto quote;
		if ((ngh = findgroup(cp)) != NULL) {
			nlist = gexpand(nlist, ngh, metoo, ntype);
			continue;
		}
quote:
		np = nalloc(cp, ntype);
		/*
		 * At this point should allow to expand
		 * to self if only person in group
		 */
		if (gp == gh->g_list && gp->ge_link == NULL)
			goto skip;
		if (!metoo && strcmp(cp, myname) == 0)
			np->n_type |= GDEL;
skip:
		nlist = put(nlist, np);
	}
	depth--;
	return (nlist);
}

/*
 * Concatenate the two passed name lists, return the result.
 */
struct name *
cat(struct name *n1, struct name *n2)
{
	struct name *tail;

	if (n1 == NULL)
		return (n2);
	if (n2 == NULL)
		return (n1);
	tail = tailof(n1);
	tail->n_flink = n2;
	n2->n_blink = tail;
	return (n1);
}

/*
 * Unpack the name list onto a vector of strings.
 * Return an error if the name list won't fit.
 */
char **
unpack(struct name *np)
{
	char **ap, **top;
	struct name *n;
	int t, extra, metoo, verbose;

	n = np;
	if ((t = count(n)) == 0)
		errx(1, "No names to unpack");
	/*
	 * Compute the number of extra arguments we will need.
	 * We need at least two extra -- one for "mail" and one for
	 * the terminating 0 pointer.  Additional spots may be needed
	 * to pass along -f to the host mailer.
	 */
	extra = 2;
	extra++;
	metoo = value("metoo") != NULL;
	if (metoo)
		extra++;
	verbose = value("verbose") != NULL;
	if (verbose)
		extra++;
	top = (char **)salloc((t + extra) * sizeof(*top));
	ap = top;
	*ap++ = "sendmail";
	*ap++ = "-i";
	if (metoo)
		*ap++ = "-m";
	if (verbose)
		*ap++ = "-v";
	for (; n != NULL; n = n->n_flink)
		if ((n->n_type & GDEL) == 0)
			*ap++ = n->n_name;
	*ap = NULL;
	return (top);
}

/*
 * Remove all of the duplicates from the passed name list by
 * insertion sorting them, then checking for dups.
 * Return the head of the new list.
 */
struct name *
elide(struct name *names)
{
	struct name *np, *t, *new;
	struct name *x;

	if (names == NULL)
		return (NULL);
	new = names;
	np = names;
	np = np->n_flink;
	if (np != NULL)
		np->n_blink = NULL;
	new->n_flink = NULL;
	while (np != NULL) {
		t = new;
		while (strcasecmp(t->n_name, np->n_name) < 0) {
			if (t->n_flink == NULL)
				break;
			t = t->n_flink;
		}

		/*
		 * If we ran out of t's, put the new entry after
		 * the current value of t.
		 */

		if (strcasecmp(t->n_name, np->n_name) < 0) {
			t->n_flink = np;
			np->n_blink = t;
			t = np;
			np = np->n_flink;
			t->n_flink = NULL;
			continue;
		}

		/*
		 * Otherwise, put the new entry in front of the
		 * current t.  If at the front of the list,
		 * the new guy becomes the new head of the list.
		 */

		if (t == new) {
			t = np;
			np = np->n_flink;
			t->n_flink = new;
			new->n_blink = t;
			t->n_blink = NULL;
			new = t;
			continue;
		}

		/*
		 * The normal case -- we are inserting into the
		 * middle of the list.
		 */

		x = np;
		np = np->n_flink;
		x->n_flink = t;
		x->n_blink = t->n_blink;
		t->n_blink->n_flink = x;
		t->n_blink = x;
	}

	/*
	 * Now the list headed up by new is sorted.
	 * Go through it and remove duplicates.
	 */

	np = new;
	while (np != NULL) {
		t = np;
		while (t->n_flink != NULL &&
		    strcasecmp(np->n_name, t->n_flink->n_name) == 0)
			t = t->n_flink;
		if (t == np || t == NULL) {
			np = np->n_flink;
			continue;
		}

		/*
		 * Now t points to the last entry with the same name
		 * as np.  Make np point beyond t.
		 */

		np->n_flink = t->n_flink;
		if (t->n_flink != NULL)
			t->n_flink->n_blink = np;
		np = np->n_flink;
	}
	return (new);
}

/*
 * Put another node onto a list of names and return
 * the list.
 */
struct name *
put(struct name *list, struct name *node)
{
	node->n_flink = list;
	node->n_blink = NULL;
	if (list != NULL)
		list->n_blink = node;
	return (node);
}

/*
 * Determine the number of undeleted elements in
 * a name list and return it.
 */
int
count(struct name *np)
{
	int c;

	for (c = 0; np != NULL; np = np->n_flink)
		if ((np->n_type & GDEL) == 0)
			c++;
	return (c);
}

/*
 * Delete the given name from a namelist.
 */
struct name *
delname(struct name *np, char name[])
{
	struct name *p;

	for (p = np; p != NULL; p = p->n_flink)
		if (strcasecmp(p->n_name, name) == 0) {
			if (p->n_blink == NULL) {
				if (p->n_flink != NULL)
					p->n_flink->n_blink = NULL;
				np = p->n_flink;
				continue;
			}
			if (p->n_flink == NULL) {
				if (p->n_blink != NULL)
					p->n_blink->n_flink = NULL;
				continue;
			}
			p->n_blink->n_flink = p->n_flink;
			p->n_flink->n_blink = p->n_blink;
		}
	return (np);
}

/*
 * Pretty print a name list
 * Uncomment it if you need it.
 */

/*
void
prettyprint(struct name *name)
{
	struct name *np;

	np = name;
	while (np != NULL) {
		fprintf(stderr, "%s(%d) ", np->n_name, np->n_type);
		np = np->n_flink;
	}
	fprintf(stderr, "\n");
}
*/
