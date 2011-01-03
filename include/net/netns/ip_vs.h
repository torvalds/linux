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
	/*
	 *	Hash table: for real service lookups
	 */
	#define IP_VS_RTAB_BITS 4
	#define IP_VS_RTAB_SIZE (1 << IP_VS_RTAB_BITS)
	#define IP_VS_RTAB_MASK (IP_VS_RTAB_SIZE - 1)

	struct list_head	rs_table[IP_VS_RTAB_SIZE];

	/* ip_vs_lblcr */
	int			sysctl_lblcr_expiration;
	struct ctl_table_header	*lblcr_ctl_header;
	struct ctl_table	*lblcr_ctl_table;
};

#endif /* IP_VS_H_ */
