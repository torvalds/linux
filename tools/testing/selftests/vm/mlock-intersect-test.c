/*
 * It tests the duplicate mlock result:
 * - the ulimit of lock page is 64k
 * - allocate address area 64k starting from p
 * - mlock   [p -- p + 30k]
 * - Then mlock address  [ p -- p + 40k ]
 *
 * It should succeed since totally we locked
 * 40k < 64k limitation.
 *
 * It should not be run with CAP_IPC_LOCK.
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/capability.h>
#include <sys/mman.h>
#include "mlock2.h"

int main(int argc, char **argv)
{
	struct rlimit new;
	char *p = NULL;
	cap_t cap = cap_init();
	int i;

	/* drop capabilities including CAP_IPC_LOCK */
	if (cap_set_proc(cap))
		return -1;

	/* set mlock limits to 64k */
	new.rlim_cur = 65536;
	new.rlim_max = 65536;
	setrlimit(RLIMIT_MEMLOCK, &new);

	/* test VM_LOCK */
	p = malloc(1024 * 64);
	if (mlock(p, 1024 * 30)) {
		printf("mlock() 30k return failure.\n");
		return -1;
	}
	for (i = 0; i < 10; i++) {
		if (mlock(p, 1024 * 40)) {
			printf("mlock() #%d 40k returns failure.\n", i);
			return -1;
		}
	}
	for (i = 0; i < 10; i++) {
		if (mlock2_(p, 1024 * 40, MLOCK_ONFAULT)) {
			printf("mlock2_() #%d 40k returns failure.\n", i);
			return -1;
		}
	}
	free(p);

	/* Test VM_LOCKONFAULT */
	p = malloc(1024 * 64);
	if (mlock2_(p, 1024 * 30, MLOCK_ONFAULT)) {
		printf("mlock2_() 30k return failure.\n");
		return -1;
	}
	for (i = 0; i < 10; i++) {
		if (mlock2_(p, 1024 * 40, MLOCK_ONFAULT)) {
			printf("mlock2_() #%d 40k returns failure.\n", i);
			return -1;
		}
	}
	for (i = 0; i < 10; i++) {
		if (mlock(p, 1024 * 40)) {
			printf("mlock() #%d 40k returns failure.\n", i);
			return -1;
		}
	}
	return 0;
}
