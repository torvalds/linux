/*	$OpenBSD: sem_timedwait.c,v 1.5 2022/05/13 15:32:49 anton Exp $	*/
/*
 * Martin Pieuchot <mpi@openbsd.org>, 2011. Public Domain.
 */

#include <sys/types.h>
#include <sys/sysctl.h>

#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <pthread.h>
#include "test.h"


void *waiter(void *arg);

void
handler(int sig)
{
	static char message[] = "got sig\n";

	write(STDERR_FILENO, message, sizeof(message) - 1);
}

sem_t sem;
volatile int posted = 0, eintr_ok = 0;

int
main(int argc, char **argv)
{
	pthread_t th;
	struct clockinfo info;
	struct sigaction sa;
	struct timespec delay, ts, ts2;
	size_t infosize = sizeof(info);
	int mib[] = { CTL_KERN, KERN_CLOCKRATE };

	CHECKr(clock_gettime(CLOCK_REALTIME, &ts));
	ts.tv_sec += 3;
	CHECKn(sem_timedwait(&sem, &ts));
	ASSERT(errno == EINVAL);

	CHECKr(sem_init(&sem, 0, 0));

	CHECKr(pthread_create(&th, NULL, waiter, &sem));

	sleep(1);

	printf("expect: sem_destroy on semaphore with waiters!\n");
	CHECKn(sem_destroy(&sem));
	ASSERT(errno == EBUSY);

	posted = 1;
	CHECKr(sem_post(&sem));
	CHECKr(pthread_join(th, NULL));

	/* test that sem_timedwait() resumes after handling a signal */
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = &handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGUSR1, &sa, NULL))
		err(1, "sigaction");
	posted = 0;
	CHECKr(pthread_create(&th, NULL, waiter, &sem));
	sleep(1);
	fprintf(stderr, "sending sig\n");
	eintr_ok = 1;
	pthread_kill(th, SIGUSR1);
	sleep(1);
	fprintf(stderr, "posting\n");
	posted = 1;
	eintr_ok = 0;
	CHECKr(sem_post(&sem));
	CHECKr(pthread_join(th, NULL));

	CHECKr(clock_gettime(CLOCK_REALTIME, &ts));
	ts.tv_sec += 2;
	CHECKn(sem_timedwait(&sem, &ts));
	ASSERT(errno == ETIMEDOUT);
	CHECKr(clock_gettime(CLOCK_REALTIME, &ts2));

	fprintf(stderr, "timeout: expected %lld.%09ld actual %lld.%09ld\n",
	    ts.tv_sec, ts.tv_nsec, ts2.tv_sec, ts2.tv_nsec);

	/* Check that we don't return early. */
	ASSERT(timespeccmp(&ts, &ts2, <=));

	/*
	 * Check that we don't return unusually late.  Something might be
	 * off if the wait returns more than two ticks after our timeout.
	 */
	CHECKr(sysctl(mib, 2, &info, &infosize, NULL, 0));
	delay.tv_sec = 0;
	delay.tv_nsec = info.tick * 1000;	/* usecs -> nsecs */
	timespecadd(&delay, &delay, &delay);	/* up to two ticks of delay */
	timespecadd(&ts, &delay, &ts);
	fprintf(stderr, "timeout: expected %lld.%09ld actual %lld.%09ld\n",
	    ts2.tv_sec, ts2.tv_nsec, ts.tv_sec, ts.tv_nsec);
	ASSERT(timespeccmp(&ts2, &ts, <=));

	CHECKe(sem_destroy(&sem));

	SUCCEED;
}

void *
waiter(void *arg)
{
	sem_t *semp = arg;
	struct timespec ts;
	int value;
	int r;

	CHECKr(clock_gettime(CLOCK_REALTIME, &ts));
	ts.tv_sec += 3;
	r = sem_timedwait(semp, &ts);
	CHECKr(sem_getvalue(semp, &value));
	if (r == 0) {
		ASSERT(value == 0);
		ASSERT(posted != 0);
	} else {
		ASSERT(r == -1);
		ASSERT(errno == EINTR);
		ASSERT(eintr_ok);
		if (posted)
			ASSERT(value == 1);
		else
			ASSERT(value == 0);
	}

	return (NULL);
}
