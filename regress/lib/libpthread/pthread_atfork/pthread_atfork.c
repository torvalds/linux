/*	$OpenBSD: pthread_atfork.c,v 1.3 2005/11/05 04:28:46 fgsch Exp $	*/

/*
 * Federico Schwindt <fgsch@openbsd.org>, 2005. Public Domain.
 */

#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>

#include "test.h"

int cnt;

void
prepare1(void)
{
	ASSERT(cnt == 1);
	cnt--;
}

void
prepare2(void)
{
	ASSERT(cnt == 2);
	cnt--;
}

void
parent1(void)
{
	ASSERT(cnt == 0);
	cnt += 2;
}

void
parent2(void)
{
	ASSERT(cnt == 2);
	cnt -= 2;
}

void
child1(void)
{
	ASSERT(cnt == 0);
	cnt += 3;
}

void
child2(void)
{
	ASSERT(cnt == 3);
	cnt++;
}

void *
forker1(void *arg)
{
	CHECKr(pthread_atfork(prepare1, parent1, child1));

	cnt = 1;
	switch (fork()) {
	case -1:
		PANIC("fork");
		break;

	case 0:
		ASSERT(cnt == 3);
		_exit(0);

	default:
		ASSERT(cnt == 2);
		break;
	}

	cnt = 1;

	return (NULL);
}

void *
forker2(void *arg)
{
	CHECKr(pthread_atfork(prepare2, parent2, child2));

	cnt = 2;
	switch (fork()) {
	case -1:
		PANIC("fork");
		break;

	case 0:
		ASSERT(cnt == 4);
		_exit(0);

	default:
		ASSERT(cnt == 0);
		break;
	}

	cnt = 0;

	return (NULL);
}

int
main(int argc, char **argv)
{
	pthread_t tid;

	CHECKr(pthread_create(&tid, NULL, forker1, NULL));
	CHECKr(pthread_join(tid, NULL));
	ASSERT(cnt == 1);
	CHECKr(pthread_create(&tid, NULL, forker2, NULL));
	CHECKr(pthread_join(tid, NULL));
	ASSERT(cnt == 0);
	SUCCEED;
}
