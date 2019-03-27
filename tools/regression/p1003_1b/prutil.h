#ifndef _PRUTIL_H_
#define _PRUTIL_H_

/*
 * $FreeBSD$
 */

struct sched_param;

void quit(const char *);
char *sched_text(int);
int sched_is(int line, struct sched_param *, int);

#endif /* _PRUTIL_H_ */
