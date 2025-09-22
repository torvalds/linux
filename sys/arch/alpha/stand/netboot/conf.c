/*	$OpenBSD: conf.c,v 1.2 1997/05/05 06:01:59 millert Exp $	*/
/*	$NetBSD: conf.c,v 1.4 1997/04/06 08:41:23 cgd Exp $	*/

#include <sys/types.h>
#include <netinet/in.h>

#include <lib/libsa/stand.h>
#include <lib/libsa/nfs.h>
#include "dev_net.h"

struct fs_ops file_system[] = {
	{ nfs_open, nfs_close, nfs_read, nfs_write, nfs_seek, nfs_stat },
};
int nfsys = 1;

struct devsw devsw[] = {
	{ "net",  net_strategy,  net_open,  net_close,  net_ioctl },
};
int	ndevs = 1;

extern struct netif_driver prom_netif_driver;

struct netif_driver *netif_drivers[] = {
	&prom_netif_driver,
};
int	n_netif_drivers = (sizeof(netif_drivers) / sizeof(netif_drivers[0]));
