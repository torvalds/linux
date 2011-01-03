/*
 *  IP Virtual Server
 *  Data structure for network namspace
 *
 */

#ifndef IP_VS_H_
#define IP_VS_H_

#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/list_nulls.h>
#include <linux/ip_vs.h>
#include <asm/atomic.h>
#include <linux/in.h>

struct ip_vs_stats;
struct ip_vs_sync_buff;
struct ctl_table_header;

struct netns_ipvs {
	int			gen;		/* Generation */
};

#endif /* IP_VS_H_ */
