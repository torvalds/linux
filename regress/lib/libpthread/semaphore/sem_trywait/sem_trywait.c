/*	$OpenBSD: sem_trywait.c,v 1.2 2012/03/03 09:36:26 guenther Exp $	*/
/*
 * Martin Pieuchot <mpi@openbsd.org>, 2011. Public Domain.
 */

#include <errno.h>
#include <unistd.h>
#include <semaphore.h>
#include "test.h"

sem_t sem;

int
main(int argc, char **argv)
{
	int val;

	CHECKn(sem_trywait(&sem));
	ASSERT(errno == EINVAL);

	CHECKr(sem_init(&sem, 0, 0));

	CHECKn(sem_trywait(&sem));
	ASSERT(errno == EAGAIN);

	CHECKr(sem_post(&sem));
	CHECKr(sem_trywait(&sem));

	CHECKe(sem_getvalue(&sem, &val));
	ASSERT(val == 0);

	CHECKe(sem_destroy(&sem));

	SUCCEED;
}
