/* $FreeBSD$ */
#if __FreeBSD__ == 0		/* 1.0 did not define __FreeBSD__ */
#define __FreeBSD_version 199401
#elif __FreeBSD__ == 1		/* 1.1 defined it to be 1 */
#define __FreeBSD_version 199405
#else				/* 2.0 and higher define it to be 2 */
#include <osreldate.h>		/* and this works */
#endif
#include <stdio.h>
#include <unistd.h>

int
main(void) {
	printf("Compilation release date: %d\n", __FreeBSD_version);
#if __FreeBSD_version >= 199408
	printf("Execution environment release date: %d\n", getosreldate());
#else
	printf("Execution environment release date: can't tell\n");
#endif
	return (0);
}
