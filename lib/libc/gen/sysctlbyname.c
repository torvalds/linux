/*
 * ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42):
 * <phk@FreeBSD.org> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.   Poul-Henning Kamp
 * ----------------------------------------------------------------------------
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/sysctl.h>

int
sysctlbyname(const char *name, void *oldp, size_t *oldlenp,
    const void *newp, size_t newlen)
{
	int real_oid[CTL_MAXNAME+2];
	size_t oidlen;

	oidlen = sizeof(real_oid) / sizeof(int);
	if (sysctlnametomib(name, real_oid, &oidlen) < 0)
		return (-1);
	return (sysctl(real_oid, oidlen, oldp, oldlenp, newp, newlen));
}
