/*	$OpenBSD: bootparam.h,v 1.4 2002/03/14 01:27:07 millert Exp $	*/

int bp_whoami(int sock);
int bp_getfile(int sock, char *key, struct in_addr *addrp, char *path);

