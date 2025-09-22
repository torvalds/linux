/*	$OpenBSD: sem_getvalue.c,v 1.2 2012/03/03 09:36:26 guenther Exp $	*/
/*
 * Martin Pieuchot <mpi@openbsd.org>, 2011. Public Domain.
 */

#include <unistd.h>
#include <semaphore.h>
#include "test.h"

sem_t sem;

int
main(int argc, char **argv)
{
	int val;

	CHECKr(sem_init(&sem, 0, 0));
	CHECKe(sem_getvalue(&sem, &val));
	ASSERT(val == 0);

	CHECKr(sem_post(&sem));
	CHECKe(sem_getvalue(&sem, &val));
	ASSERT(val == 1);

	CHECKe(sem_destroy(&sem));

	SUCCEED;
}
