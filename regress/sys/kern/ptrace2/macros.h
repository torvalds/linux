/*	$OpenBSD: macros.h,v 1.2 2021/12/13 16:56:50 deraadt Exp $	*/
/* Public domain - Moritz Buhl */

#include <sys/socket.h>
#include <sys/stdint.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define __arraycount(_a)	nitems(_a)
#define __unreachable()		atf_tc_fail("unreachable")
#define __UNCONST(a)		(a)

#define __dead2 __dead

/* t_chroot.c */
#define fchroot(fd) 0

/* t_clock_gettime.c */
int sysctlbyname(char *, void *, size_t *, void *, size_t);

int
sysctlbyname(char* s, void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
        int mib[3], miblen;

	mib[0] = CTL_KERN;
	if (strcmp(s, "kern.timecounter.hardware") == 0) {
		mib[1] = KERN_TIMECOUNTER;
		mib[2] = KERN_TIMECOUNTER_HARDWARE;
		miblen = 3;
	} else if (strcmp(s, "kern.timecounter.choice") == 0) {
		mib[1] = KERN_TIMECOUNTER;
		mib[2] = KERN_TIMECOUNTER_CHOICE;
		miblen = 3;
	} else if (strcmp(s, "kern.securelevel") == 0) {
		mib[1] = KERN_SECURELVL;
		miblen = 2;
	} else {
		fprintf(stderr, "%s(): mib '%s' not supported\n", __func__, s);
		return -42;
	}

        return sysctl(mib, miblen, oldp, oldlenp, newp, newlen);
}

/* t_mlock.c */
#define MAP_WIRED	__MAP_NOREPLACE

/* t_pipe2.c */
#define O_NOSIGPIPE	0

/* t_poll.c */
#define pollts(a, b, c, e)	0

/* t_sendrecv.c */
#define SO_RERROR	SO_DEBUG

/* t_write.c */
#define _PATH_DEVZERO	"/dev/zero"
