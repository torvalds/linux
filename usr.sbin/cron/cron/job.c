/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * All rights reserved
 *
 * Distribute freely, except: don't remove my name from the source or
 * documentation (don't take credit for my work), mark your changes (don't
 * get me blamed for your possible bugs), don't alter or remove this
 * notice.  May be sold if buildable source is provided to buyer.  No
 * warrantee of any kind, express or implied, is included with this
 * software; use at your own risk, responsibility for damages (if any) to
 * anyone resulting from the use of this software rests entirely with the
 * user.
 *
 * Send bug reports, bug fixes, enhancements, requests, flames, etc., and
 * I'll try to keep a version up to date.  I can be reached as follows:
 * Paul Vixie          <paul@vix.com>          uunet!decwrl!vixie!paul
 */

#if !defined(lint) && !defined(LINT)
static const char rcsid[] =
  "$FreeBSD$";
#endif


#include "cron.h"


typedef	struct _job {
	struct _job	*next;
	entry		*e;
	user		*u;
} job;


static job	*jhead = NULL, *jtail = NULL;


void
job_add(e, u)
	register entry *e;
	register user *u;
{
	register job *j;

	/* if already on queue, keep going */
	for (j=jhead; j; j=j->next)
		if (j->e == e && j->u == u) { return; }

	/* build a job queue element */
	if ((j = (job*)malloc(sizeof(job))) == NULL)
		return;
	j->next = (job*) NULL;
	j->e = e;
	j->u = u;

	/* add it to the tail */
	if (!jhead) { jhead=j; }
	else { jtail->next=j; }
	jtail = j;
}


int
job_runqueue()
{
	register job	*j, *jn;
	register int	run = 0;

	for (j=jhead; j; j=jn) {
		do_command(j->e, j->u);
		jn = j->next;
		free(j);
		run++;
	}
	jhead = jtail = NULL;
	return run;
}
