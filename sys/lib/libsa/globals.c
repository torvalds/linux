/*	$OpenBSD: globals.c,v 1.4 2014/07/13 15:31:20 mpi Exp $	*/
/*	$NetBSD: globals.c,v 1.3 1995/09/18 21:19:27 pk Exp $	*/

/*
 *	globals.c:
 *
 *	global variables should be separate, so nothing else
 *	must be included extraneously.
 */

#include <sys/param.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/in.h>

#include "stand.h"
#include "net.h"

u_char	bcea[6] = BA;			/* broadcast ethernet address */

char	rootpath[FNAME_SIZE] = "/";	/* root mount path */
char	bootfile[FNAME_SIZE];		/* bootp says to boot this */
char	hostname[FNAME_SIZE];		/* our hostname */
int	hostnamelen;
char	domainname[FNAME_SIZE];		/* our DNS domain */
int	domainnamelen;
char	ifname[IFNAME_SIZE];		/* name of interface (e.g. "le0") */
struct	in_addr myip;			/* my ip address */
struct	in_addr nameip;			/* DNS server ip address */
struct	in_addr rootip;			/* root ip address */
struct	in_addr swapip;			/* swap ip address */
struct	in_addr gateip;			/* swap ip address */
u_int32_t netmask = 0xffffff00;		/* subnet or net mask */
