/* $OpenBSD: stack.c,v 1.4 2014/08/10 05:08:31 guenther Exp $ */
/* PUBLIC DOMAIN Feb 2012 <guenther@openbsd.org> */

/* Test the handling of the pthread_attr_t stack attributes */

#include <sys/types.h>
#include <sys/mman.h>
#include <stdint.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test.h"

#define LARGE_SIZE	(1024 * 1024)

/* thread main for plain location tests */
void *
tmain0(void *arg)
{
	int s;

	return (&s);
}

/* thread main for testing a large buffer on the stack */
void *
tmain1(void *arg)
{
	char buf[LARGE_SIZE];

	memset(buf, 0xd0, sizeof(buf));
	return (buf + LARGE_SIZE/2);
}

/*
 * struct and thread main for testing that a thread's stack is where
 * we put it
 */
struct st
{
	char *addr;
	size_t size;
};
void *
tmain2(void *arg)
{
	struct st *s = arg;

	ASSERT((char *)&s >= s->addr && (char *)&s - s->addr < s->size);
	return (NULL);
}

int
main(void)
{
	pthread_attr_t attr;
	pthread_t t;
	struct st thread_stack;
	void *addr, *addr2;
	size_t size, size2, pagesize;
	int err;

	pagesize = (size_t)sysconf(_SC_PAGESIZE);

	CHECKr(pthread_attr_init(&attr));

	/* verify that the initial values are what we expect */
	size = 1;
	CHECKr(pthread_attr_getguardsize(&attr, &size));
	ASSERT(size != 1);		/* must have changed */
	ASSERT(size != 0);		/* we default to having a guardpage */

	size = 1;
	CHECKr(pthread_attr_getstacksize(&attr, &size));
	ASSERT(size >= PTHREAD_STACK_MIN);

	addr = &addr;
	CHECKr(pthread_attr_getstackaddr(&attr, &addr));
	ASSERT(addr == NULL);		/* default must be NULL */

	addr2 = &addr;
	size2 = 1;
	CHECKr(pthread_attr_getstack(&attr, &addr2, &size2));
	ASSERT(addr2 == addr);		/* must match the other calls */
	ASSERT(size2 == size);

	/* verify that too small a size is rejected */
	err = pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN - 1);
	ASSERT(err == EINVAL);
	CHECKr(pthread_attr_getstacksize(&attr, &size2));
	ASSERT(size2 == size);


	/* create a thread with the default stack attr so we can test reuse */
	CHECKr(pthread_create(&t, NULL, &tmain0, NULL));
	sleep(1);
	CHECKr(pthread_join(t, &addr));

	/*
	 * verify that the stack has *not* been freed: we expect it to be
	 * cached for reuse.  This is unportable for the same reasons as
	 * the mquery() test below.  :-/
	 */
	*(int *)addr = 100;


	/* do the above again and make sure the stack got reused */
	CHECKr(pthread_create(&t, NULL, &tmain0, NULL));
	sleep(1);
	CHECKr(pthread_join(t, &addr2));
	ASSERT(addr == addr2);


	/*
	 * increase the stacksize, then verify that the change sticks,
	 * and that a large buffer fits on the resulting thread's stack
	 */
	size2 += LARGE_SIZE;
	CHECKr(pthread_attr_setstacksize(&attr, size2));
	CHECKr(pthread_attr_getstacksize(&attr, &size));
	ASSERT(size == size2);

	CHECKr(pthread_create(&t, &attr, &tmain1, NULL));
	sleep(1);
	CHECKr(pthread_join(t, &addr));

	/* test whether the stack has been freed */
	/* XXX yow, this is grossly unportable, as it depends on the stack
	 * not being cached, the thread being marked freeable before
	 * pthread_join() calls the gc routine (thus the sleep), and this
	 * being testable by mquery */
	addr = (void *)((uintptr_t)addr & ~(pagesize - 1));
	ASSERT(mquery(addr, pagesize, PROT_READ, MAP_FIXED|MAP_ANON, -1, 0)
	    == addr);

	/* the attr wasn't modified by pthread_create, right? */
	size = 1;
	CHECKr(pthread_attr_getstacksize(&attr, &size));
	ASSERT(size == size2);


	/* allocate our own stack and verify the thread uses it */
	size = pagesize * 4;
	addr = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE,
	    -1, 0);
	ASSERT(addr != MAP_FAILED);
	memset(addr, 0xd0, size);
	CHECKr(pthread_attr_setstack(&attr, addr, size));

	CHECKr(pthread_attr_getstacksize(&attr, &size2));
	ASSERT(size2 == size);
	CHECKr(pthread_attr_getstackaddr(&attr, &addr2));
	ASSERT(addr2 == addr);
	CHECKr(pthread_attr_getstack(&attr, &addr2, &size2));
	ASSERT(addr2 == addr);
	ASSERT(size2 == size);

	thread_stack.addr = addr;
	thread_stack.size = size;
	CHECKr(pthread_create(&t, &attr, &tmain2, &thread_stack));
	sleep(1);
	CHECKr(pthread_join(t, NULL));

	/* verify that the stack we allocated was *not* freed */
	memset(addr, 0xd0, size);

	return (0);
}
