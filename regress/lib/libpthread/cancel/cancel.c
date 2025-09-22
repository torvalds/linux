/*	$OpenBSD: cancel.c,v 1.9 2016/09/20 17:25:06 otto Exp $	*/
/* David Leonard <d@openbsd.org>, 1999. Public Domain. */

#include <pthread.h>
#include <pthread_np.h>
#include <unistd.h>
#include <util.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include "test.h"

static pthread_cond_t cond;
static pthread_mutex_t mutex;
static struct timespec expiretime;

static volatile int pv_state = 0;

static void
p(void)
{
	CHECKr(pthread_mutex_lock(&mutex));
	if (pv_state <= 0) {
		CHECKr(pthread_cond_timedwait(&cond, &mutex, &expiretime));
	}
	pv_state--;
	CHECKr(pthread_mutex_unlock(&mutex));
}

static void
v(void)
{
	int needsignal;

	CHECKr(pthread_mutex_lock(&mutex));
	pv_state++;
	needsignal = (pv_state == 1);
	if (needsignal)
		CHECKr(pthread_cond_signal(&cond));
	CHECKr(pthread_mutex_unlock(&mutex));
}

static void
c1handler(void *arg)
{
	CHECKe(close(*(int *)arg));
	v();
}

static void *
child1fn(void *arg)
{
	int fd, dummy;
	char buf[1024];
	int len;

	SET_NAME("c1");
	CHECKr(pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL));
	/* something that will block */
	CHECKe(openpty(&dummy, &fd, NULL, NULL, NULL));
	pthread_cleanup_push(c1handler, (void *)&fd);
	v();
	while (1) {
		CHECKe(len = read(fd, &buf, sizeof buf));
		printf("child 1 read %d bytes\n", len);
	}
	pthread_cleanup_pop(0);
	PANIC("child 1");
}

static int c2_in_test = 0;

static void
c2handler(void *arg)
{
	ASSERT(c2_in_test);
	v();
}

static int message_seen = 0;
static void *
child2fn(void *arg)
{
	SET_NAME("c2");

	CHECKr(pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL));
	pthread_cleanup_push(c2handler, NULL);
	v();

	while (1) {
		struct timespec now;
		struct timespec end;

		/*
		 * XXX Be careful not to call any cancellation points
		 * until pthread_testcancel()
		 */

		CHECKe(clock_gettime(CLOCK_REALTIME, &end));
		end.tv_sec ++;

		while (1) {
			CHECKe(clock_gettime(CLOCK_REALTIME, &now));
			if (timespeccmp(&now, &end, >=))
				break;
			pthread_yield();
		}

		/* XXX write() contains a cancellation point */
		/* printf("child 2 testing for cancel\n"); */

		c2_in_test = 1;
		pthread_testcancel();
		printf("you should see this message exactly once\n");
		message_seen++;
		c2_in_test = 0;
		ASSERT(message_seen == 1);
		v();
	}
	pthread_cleanup_pop(0);
	PANIC("child 2");
}

static int c3_cancel_survived;

static void
c3handler(void *arg)
{
	printf("(fyi, cancellation of self %s instantaneous)\n",
		(c3_cancel_survived ? "was not" : "was"));
	v();
}

static void *
child3fn(void *arg)
{
	SET_NAME("c3");
	pthread_cleanup_push(c3handler, NULL);

	/* Cancel myself */
	CHECKr(pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL));
	c3_cancel_survived = 0;
	pthread_cancel(pthread_self());
	c3_cancel_survived = 1;
	pthread_testcancel();
	pthread_cleanup_pop(0);

	PANIC("child 3");
}

static int c4_cancel_early;

static void
c4handler(void *arg)
{
	printf("early = %d\n", c4_cancel_early);
	ASSERT(c4_cancel_early == 0);
	v();
}

static void *
child4fn(void *arg)
{
	SET_NAME("c4");
	pthread_cleanup_push(c4handler, NULL);

	/* Cancel myself */
	CHECKr(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL));

	c4_cancel_early = 3;
	pthread_cancel(pthread_self());

	c4_cancel_early = 2;
	pthread_testcancel();

	c4_cancel_early = 1;
	CHECKr(pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL));

	c4_cancel_early = 0;
	pthread_testcancel();

	pthread_cleanup_pop(0);

	PANIC("child 4");
}

int
main(int argc, char *argv[])
{
	pthread_t child1, child2, child3, child4;

	/* Set up our control flow */
	CHECKr(pthread_mutex_init(&mutex, NULL));
	CHECKr(pthread_cond_init(&cond, NULL));
	CHECKe(clock_gettime(CLOCK_REALTIME, &expiretime));
	expiretime.tv_sec += 5; /* this test shouldn't run over 5 seconds */

	CHECKr(pthread_create(&child1, NULL, child1fn, NULL));
	CHECKr(pthread_create(&child2, NULL, child2fn, NULL));
	p();
	p();

	CHECKr(pthread_cancel(child1));
	p();

	/* Give thread 2 a chance to go through its deferred loop once */
	p();
	CHECKr(pthread_cancel(child2));
	p();

	/* Child 3 cancels itself */
	CHECKr(pthread_create(&child3, NULL, child3fn, NULL));
	p();

	/* Child 4 also cancels itself */
	CHECKr(pthread_create(&child4, NULL, child4fn, NULL));
	p();

	/* Make sure they're all gone */
	CHECKr(pthread_join(child4, NULL));
	CHECKr(pthread_join(child3, NULL));
	CHECKr(pthread_join(child2, NULL));
	CHECKr(pthread_join(child1, NULL));

	exit(0);
}
