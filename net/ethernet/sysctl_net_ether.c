/* -*- linux-c -*-
 * sysctl_net_ether.c: sysctl interface to net Ethernet subsystem.
 *
 * Begun April 1, 1996, Mike Shaver.
 * Added /proc/sys/net/ether directory entry (empty =) ). [MS]
 */

#include <linux/mm.h>
#include <linux/sysctl.h>
#include <linux/if_ether.h>

ctl_table ether_table[] = {
	{0}
};
