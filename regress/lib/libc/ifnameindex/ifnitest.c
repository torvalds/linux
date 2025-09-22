/*	$OpenBSD: ifnitest.c,v 1.3 2017/02/25 07:28:32 jsg Exp $ */

/* Public domain. 2015, Claudio Jeker */

#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#include <err.h>
#include <stdio.h>
#include <string.h>

int
main(int argc, char *argv[])
{
	char name[IF_NAMESIZE], *ifname;
	unsigned int lo0index;

	lo0index = if_nametoindex("lo0");
	if (lo0index == 0)
		err(1, "if_nametoindex(lo0)");
	ifname = if_indextoname(lo0index, name);
	if (ifname == NULL || strcmp("lo0", ifname) != 0)
		err(1, "if_indextoname(%u)", lo0index);

	/* test failures */
	if (if_nametoindex("4kingbula") != 0)
		err(1, "if_nametoindex(4kingbula)");
	if (if_indextoname(65536, name) != NULL)
		err(1, "if_indextoname(%u)", 65536);

	return 0;
}
