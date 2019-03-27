/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2011 James Gritton
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

#include <sys/uio.h>

#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "jailp.h"

struct cfjails ready = TAILQ_HEAD_INITIALIZER(ready);
struct cfjails depend = TAILQ_HEAD_INITIALIZER(depend);

static void dep_add(struct cfjail *from, struct cfjail *to, unsigned flags);
static int cmp_jailptr(const void *a, const void *b);
static int cmp_jailptr_name(const void *a, const void *b);
static struct cfjail *find_jail(const char *name);
static int running_jid(const char *name, int flags);

static struct cfjail **jails_byname;
static size_t njails;

/*
 * Set up jail dependency lists.
 */
void
dep_setup(int docf)
{
	struct cfjail *j, *dj;
	struct cfparam *p;
	struct cfstring *s;
	struct cfdepend *d;
	const char *cs;
	char *pname;
	size_t plen;
	int deps, ldeps;

	if (!docf) {
		/*
		 * With no config file, let "depend" for a single jail
		 * look at currently running jails.
		 */
		if ((j = TAILQ_FIRST(&cfjails)) &&
		    (p = j->intparams[IP_DEPEND])) {
			TAILQ_FOREACH(s, &p->val, tq) {
				if (running_jid(s->s, 0) < 0) {
					warnx("depends on nonexistent jail "
					    "\"%s\"", s->s);
					j->flags |= JF_FAILED;
				}
			}
		}
		return;
	}

	njails = 0;
	TAILQ_FOREACH(j, &cfjails, tq)
		njails++;
	jails_byname = emalloc(njails * sizeof(struct cfjail *));
	njails = 0;
	TAILQ_FOREACH(j, &cfjails, tq)
		jails_byname[njails++] = j;
	qsort(jails_byname, njails, sizeof(struct cfjail *), cmp_jailptr);
	deps = 0;
	ldeps = 0;
	plen = 0;
	pname = NULL;
	TAILQ_FOREACH(j, &cfjails, tq) {
		if (j->flags & JF_FAILED)
			continue;
		if ((p = j->intparams[IP_DEPEND])) {
			TAILQ_FOREACH(s, &p->val, tq) {
				dj = find_jail(s->s);
				if (dj != NULL) {
					deps++;
					dep_add(j, dj, 0);
				} else {
					jail_warnx(j,
					    "depends on undefined jail \"%s\"",
					    s->s);
					j->flags |= JF_FAILED;
				}
			}
		}
		/* A jail has an implied dependency on its parent. */
		if ((cs = strrchr(j->name, '.')))
		{
			if (plen < (size_t)(cs - j->name + 1)) {
				plen = (cs - j->name) + 1;
				pname = erealloc(pname, plen);
			}
			strlcpy(pname, j->name, plen);
			dj = find_jail(pname);
			if (dj != NULL) {
				ldeps++;
				dep_add(j, dj, DF_LIGHT);
			}
		}
	}

	/* Look for dependency loops. */
	if (deps && (deps > 1 || ldeps)) {
		(void)start_state(NULL, 0, 0, 0);
		while ((j = TAILQ_FIRST(&ready))) {
			requeue(j, &cfjails);
			dep_done(j, DF_NOFAIL);
		}
		while ((j = TAILQ_FIRST(&depend)) != NULL) {
			jail_warnx(j, "dependency loop");
			j->flags |= JF_FAILED;
			do {
				requeue(j, &cfjails);
				dep_done(j, DF_NOFAIL);
			} while ((j = TAILQ_FIRST(&ready)));
		}
		TAILQ_FOREACH(j, &cfjails, tq)
			STAILQ_FOREACH(d, &j->dep[DEP_FROM], tq[DEP_FROM])
				d->flags &= ~DF_SEEN;
	}
	if (pname != NULL)
		free(pname);
}

/*
 * Return if a jail has dependencies.
 */
int
dep_check(struct cfjail *j)
{
	int reset, depfrom, depto, ndeps, rev;
	struct cfjail *dj;
	struct cfdepend *d;

	static int bits[] = { 0, 1, 1, 2, 1, 2, 2, 3 };

	if (j->ndeps == 0)
		return 0;
	ndeps = 0;
	if ((rev = JF_DO_STOP(j->flags))) {
		depfrom = DEP_TO;
		depto = DEP_FROM;
	} else {
		depfrom = DEP_FROM;
		depto = DEP_TO;
	}
	STAILQ_FOREACH(d, &j->dep[depfrom], tq[depfrom]) {
		if (d->flags & DF_SEEN)
			continue;
		dj = d->j[depto];
		if (dj->flags & JF_FAILED) {
			if (!(j->flags & (JF_DEPEND | JF_FAILED)) &&
			    verbose >= 0)
				jail_warnx(j, "skipped");
			j->flags |= JF_FAILED;
			continue;
		}
		/*
		 * The dependee's state may be set (or changed) as a result of
		 * being in a dependency it wasn't in earlier.
		 */
		reset = 0;
		if (bits[dj->flags & JF_OP_MASK] <= 1) {
			if (!(dj->flags & JF_OP_MASK)) {
				reset = 1;
				dj->flags |= JF_DEPEND;
				requeue(dj, &ready);
			}
			/* Set or change the dependee's state. */
			switch (j->flags & JF_OP_MASK) {
			case JF_START:
				dj->flags |= JF_START;
				break;
			case JF_SET:
				if (!(dj->flags & JF_OP_MASK))
					dj->flags |= JF_SET;
				else if (dj->flags & JF_STOP)
					dj->flags |= JF_START;
				break;
			case JF_STOP:
			case JF_RESTART:
				if (!(dj->flags & JF_STOP))
					reset = 1;
				dj->flags |= JF_STOP;
				if (dj->flags & JF_SET)
					dj->flags ^= (JF_START | JF_SET);
				break;
			}
		}
		if (reset)
			dep_reset(dj);
		if (!((d->flags & DF_LIGHT) &&
		    (rev ? dj->jid < 0 : dj->jid > 0)))
			ndeps++;
	}
	if (ndeps == 0)
		return 0;
	requeue(j, &depend);
	return 1;
}

/*
 * Resolve any dependencies from a finished jail.
 */
void
dep_done(struct cfjail *j, unsigned flags)
{
	struct cfjail *dj;
	struct cfdepend *d;
	int depfrom, depto;

	if (JF_DO_STOP(j->flags)) {
		depfrom = DEP_TO;
		depto = DEP_FROM;
	} else {
		depfrom = DEP_FROM;
		depto = DEP_TO;
	}
	STAILQ_FOREACH(d, &j->dep[depto], tq[depto]) {
		if ((d->flags & DF_SEEN) | (flags & ~d->flags & DF_LIGHT))
			continue;
		d->flags |= DF_SEEN;
		dj = d->j[depfrom];
		if (!(flags & DF_NOFAIL) && (j->flags & JF_FAILED) &&
		    (j->flags & (JF_OP_MASK | JF_DEPEND)) !=
		    (JF_SET | JF_DEPEND)) {
			if (!(dj->flags & (JF_DEPEND | JF_FAILED)) &&
			    verbose >= 0)
				jail_warnx(dj, "skipped");
			dj->flags |= JF_FAILED;
		}
		if (!--dj->ndeps && dj->queue == &depend)
			requeue(dj, &ready);
	}
}

/*
 * Count a jail's dependencies and mark them as unseen.
 */
void
dep_reset(struct cfjail *j)
{
	int depfrom;
	struct cfdepend *d;

	depfrom = JF_DO_STOP(j->flags) ? DEP_TO : DEP_FROM;
	j->ndeps = 0;
	STAILQ_FOREACH(d, &j->dep[depfrom], tq[depfrom])
		j->ndeps++;
}

/*
 * Find the next jail ready to do something.
 */
struct cfjail *
next_jail(void)
{
	struct cfjail *j;

	if (!(j = next_proc(!TAILQ_EMPTY(&ready))) &&
	    (j = TAILQ_FIRST(&ready)) && JF_DO_STOP(j->flags) &&
	    (j = TAILQ_LAST(&ready, cfjails)) && !JF_DO_STOP(j->flags)) {
		TAILQ_FOREACH_REVERSE(j, &ready, cfjails, tq)
			if (JF_DO_STOP(j->flags))
				break;
	}
	if (j != NULL)
		requeue(j, &cfjails);
	return j;
}

/*
 * Set jails to the proper start state.
 */
int
start_state(const char *target, int docf, unsigned state, int running)
{
	struct iovec jiov[6];
	struct cfjail *j, *tj;
	int jid;
	char namebuf[MAXHOSTNAMELEN];

	if (!target || (!docf && state != JF_STOP) ||
	    (!running && !strcmp(target, "*"))) {
		/*
		 * For a global wildcard (including no target specified),
		 * set the state on all jails and start with those that
		 * have no dependencies.
		 */
		TAILQ_FOREACH_SAFE(j, &cfjails, tq, tj) {
			j->flags = (j->flags & JF_FAILED) | state |
			    (docf ? JF_WILD : 0);
			dep_reset(j);
			requeue(j, j->ndeps ? &depend : &ready);
		}
	} else if (wild_jail_name(target)) {
		/*
		 * For targets specified singly, or with a non-global wildcard,
		 * set their state and call them ready (even if there are
		 * dependencies).  Leave everything else unqueued for now.
		 */
		if (running) {
			/*
			 * -R matches its wildcards against currently running
			 * jails, not against the config file.
			 */
			jiov[0].iov_base = __DECONST(char *, "lastjid");
			jiov[0].iov_len = sizeof("lastjid");
			jiov[1].iov_base = &jid;
			jiov[1].iov_len = sizeof(jid);
			jiov[2].iov_base = __DECONST(char *, "jid");
			jiov[2].iov_len = sizeof("jid");
			jiov[3].iov_base = &jid;
			jiov[3].iov_len = sizeof(jid);
			jiov[4].iov_base = __DECONST(char *, "name");
			jiov[4].iov_len = sizeof("name");
			jiov[5].iov_base = &namebuf;
			jiov[5].iov_len = sizeof(namebuf);
			for (jid = 0; jail_get(jiov, 6, 0) > 0; ) {
				if (wild_jail_match(namebuf, target)) {
					j = add_jail();
					j->name = estrdup(namebuf);
					j->jid = jid;
					j->flags = (j->flags & JF_FAILED) |
					    state | JF_WILD;
					dep_reset(j);
					requeue(j, &ready);
				}
			}
		} else {
			TAILQ_FOREACH_SAFE(j, &cfjails, tq, tj) {
				if (wild_jail_match(j->name, target)) {
					j->flags = (j->flags & JF_FAILED) |
					    state | JF_WILD;
					dep_reset(j);
					requeue(j, &ready);
				}
			}
		}
	} else {
		j = find_jail(target);
		if (j == NULL && state == JF_STOP) {
			/* Allow -[rR] to specify a currently running jail. */
			if ((jid = running_jid(target, JAIL_DYING)) > 0) {
				j = add_jail();
				j->name = estrdup(target);
				j->jid = jid;
			}
		}
		if (j == NULL) {
			warnx("\"%s\" not found", target);
			return -1;
		}
		j->flags = (j->flags & JF_FAILED) | state;
		dep_reset(j);
		requeue(j, &ready);
	}
	return 0;
}

/*
 * Move a jail to a new list.
 */
void
requeue(struct cfjail *j, struct cfjails *queue)
{
	if (j->queue != queue) {
		TAILQ_REMOVE(j->queue, j, tq);
		TAILQ_INSERT_TAIL(queue, j, tq);
		j->queue = queue;
	}
}

void
requeue_head(struct cfjail *j, struct cfjails *queue)
{
    TAILQ_REMOVE(j->queue, j, tq);
    TAILQ_INSERT_HEAD(queue, j, tq);
    j->queue = queue;
}

/*
 * Add a dependency edge between two jails.
 */
static void
dep_add(struct cfjail *from, struct cfjail *to, unsigned flags)
{
	struct cfdepend *d;

	d = emalloc(sizeof(struct cfdepend));
	d->flags = flags;
	d->j[DEP_FROM] = from;
	d->j[DEP_TO] = to;
	STAILQ_INSERT_TAIL(&from->dep[DEP_FROM], d, tq[DEP_FROM]);
	STAILQ_INSERT_TAIL(&to->dep[DEP_TO], d, tq[DEP_TO]);
}

/*
 * Compare jail pointers for qsort/bsearch.
 */
static int
cmp_jailptr(const void *a, const void *b)
{
	return strcmp((*((struct cfjail * const *)a))->name,
	    ((*(struct cfjail * const *)b))->name);
}

static int
cmp_jailptr_name(const void *a, const void *b)
{
	return strcmp((const char *)a, ((*(struct cfjail * const *)b))->name);
}

/*
 * Find a jail object by name.
 */
static struct cfjail *
find_jail(const char *name)
{
	struct cfjail **jp;

	jp = bsearch(name, jails_byname, njails, sizeof(struct cfjail *),
	    cmp_jailptr_name);
	return jp ? *jp : NULL;
}

/*
 * Return the named jail's jid if it is running, and -1 if it isn't.
 */
static int
running_jid(const char *name, int flags)
{
	struct iovec jiov[2];
	char *ep;
	int jid;

	if ((jid = strtol(name, &ep, 10)) && !*ep) {
		jiov[0].iov_base = __DECONST(char *, "jid");
		jiov[0].iov_len = sizeof("jid");
		jiov[1].iov_base = &jid;
		jiov[1].iov_len = sizeof(jid);
	} else {
		jiov[0].iov_base = __DECONST(char *, "name");
		jiov[0].iov_len = sizeof("name");
		jiov[1].iov_len = strlen(name) + 1;
		jiov[1].iov_base = alloca(jiov[1].iov_len);
		strcpy(jiov[1].iov_base, name);
	}
	return jail_get(jiov, 2, flags);
}
