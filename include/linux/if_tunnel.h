#ifndef _IF_TUNNEL_H_
#define _IF_TUNNEL_H_

#include <linux/ip.h>
#include <linux/in6.h>
#include <uapi/linux/if_tunnel.h>
#include <linux/u64_stats_sync.h>

/* often modified stats are per cpu, other are shared (netdev->stats) */
struct pcpu_tstats {
	u64	rx_packets;
	u64	rx_bytes;
	u64	tx_packets;
	u64	tx_bytes;
	struct u64_stats_sync	syncp;
};

#endif /* _IF_TUNNEL_H_ */
