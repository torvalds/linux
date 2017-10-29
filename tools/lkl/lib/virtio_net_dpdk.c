/*
 * Intel DPDK based virtual network interface feature for LKL
 * Copyright (c) 2015,2016 Ryo Nakamura, Hajime Tazaki
 *
 * Author: Ryo Nakamura <upa@wide.ad.jp>
 *         Hajime Tazaki <thehajime@gmail.com>
 */

//#define DEBUG

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/queue.h>

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_net.h>

#include <lkl_host.h>

static char *ealargs[4] = {
	"lkl_vif_dpdk",
	"-c 1",
	"-n 1",
	"--log-level=0",
};

#define MAX_PKT_BURST           16
/* XXX: disable cache due to no thread-safe on mempool cache. */
#define MEMPOOL_CACHE_SZ        0
/* for TSO pkt */
#define MAX_PACKET_SZ           (65535 \
	- (sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM))
#define MBUF_NUM                (512*2) /* vmxnet3 requires 1024 */
#define MBUF_SIZ        \
	(MAX_PACKET_SZ + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)
#define NUMDESC         512	/* nb_min on vmxnet3 is 512 */
#define NUMQUEUE        1

#define BIT(x) (1ULL << x)

static int portid;

struct lkl_netdev_dpdk {
	struct lkl_netdev dev;
	int portid;
	struct rte_mempool *rxpool, *txpool; /* ring buffer pool */
	/* burst receive context by rump dpdk code */
	struct rte_mbuf *rcv_mbuf[MAX_PKT_BURST];
	int npkts;
	int bufidx;
	int offload;
	int close: 1;
	int busy_poll: 1;
};

static int dpdk_net_tx_prep(struct rte_mbuf *rm,
		struct lkl_virtio_net_hdr_v1 *header)
{
	struct rte_net_hdr_lens hdr_lens;
	uint32_t ptype;

#ifdef DEBUG
	lkl_printf("dpdk-tx: gso_type=%d, gso=%d, hdrlen=%d validation=%d\n",
		header->gso_type, header->gso_size, header->hdr_len,
		rte_validate_tx_offload(rm));
#endif

	ptype = rte_net_get_ptype(rm, &hdr_lens, RTE_PTYPE_ALL_MASK);
	rm->l2_len = hdr_lens.l2_len;
	rm->l3_len = hdr_lens.l3_len;
	rm->l4_len = hdr_lens.l4_len; // including tcp opts

	if ((ptype & RTE_PTYPE_L4_MASK) == RTE_PTYPE_L4_TCP) {
		if ((ptype & RTE_PTYPE_L3_MASK) == RTE_PTYPE_L3_IPV4)
			rm->ol_flags = PKT_TX_IPV4;
		else if ((ptype & RTE_PTYPE_L3_MASK) == RTE_PTYPE_L3_IPV6)
			rm->ol_flags = PKT_TX_IPV6;

		rm->ol_flags |= PKT_TX_TCP_CKSUM;
		rm->tso_segsz = header->gso_size;
		/* TSO case */
		if (header->gso_type == LKL_VIRTIO_NET_HDR_GSO_TCPV4)
			rm->ol_flags |= (PKT_TX_TCP_SEG | PKT_TX_IP_CKSUM);
		else if (header->gso_type == LKL_VIRTIO_NET_HDR_GSO_TCPV6)
			rm->ol_flags |= PKT_TX_TCP_SEG;
	}

	return sizeof(struct lkl_virtio_net_hdr_v1);

}

static int dpdk_net_tx(struct lkl_netdev *nd, struct iovec *iov, int cnt)
{
	void *pkt;
	struct rte_mbuf *rm;
	struct lkl_netdev_dpdk *nd_dpdk;
	struct lkl_virtio_net_hdr_v1 *header = NULL;
	int i, len, sent = 0;
	void *data = NULL;

	nd_dpdk = (struct lkl_netdev_dpdk *) nd;

	/*
	 * XXX: someone reported that DPDK's mempool with cache is not thread
	 * safe (e.g., http://www.dpdk.io/ml/archives/dev/2014-February/001401.html),
	 * potentially rte_pktmbuf_alloc() is not thread safe here.  so I
	 * tentatively disabled the cache on mempool by assigning
	 * MEMPOOL_CACHE_SZ to 0.
	 */
	rm = rte_pktmbuf_alloc(nd_dpdk->txpool);

	for (i = 0; i < cnt; i++) {
		data = iov[i].iov_base;
		len = (int)iov[i].iov_len;

		if (i == 0) {
			header = data;
			data += sizeof(*header);
			len -= sizeof(*header);
		}

		if (len == 0)
			continue;

		pkt = rte_pktmbuf_append(rm, len);
		if (pkt) {
			/* XXX: I wanna have M_EXT flag !!! */
			memcpy(pkt, data, len);
			sent += len;
		} else {
			lkl_printf("dpdk-tx: failed to append: idx=%d len=%d\n",
				   i, len);
			rte_pktmbuf_free(rm);
			return -1;
		}
#ifdef DEBUG
		lkl_printf("dpdk-tx: pkt[%d]len=%d\n", i, len);
#endif
	}

	/* preparation for TX offloads */
	sent += dpdk_net_tx_prep(rm, header);

	/* XXX: should be bulk-trasmitted !! */
	if (rte_eth_tx_prepare(nd_dpdk->portid, 0, &rm, 1) != 1)
		lkl_printf("tx_prep failed\n");

	rte_eth_tx_burst(nd_dpdk->portid, 0, &rm, 1);

	rte_pktmbuf_free(rm);
	return sent;
}

static int __dpdk_net_rx(struct lkl_netdev *nd, struct iovec *iov, int cnt)
{
	struct lkl_netdev_dpdk *nd_dpdk;
	int i = 0;
	struct rte_mbuf *rm, *first;
	void *r_data;
	size_t read = 0, r_size, copylen = 0, offset = 0;
	struct lkl_virtio_net_hdr_v1 *header = iov[0].iov_base;
	uint16_t mtu;

	nd_dpdk = (struct lkl_netdev_dpdk *) nd;
	memset(header, 0, sizeof(struct lkl_virtio_net_hdr_v1));

	first = nd_dpdk->rcv_mbuf[nd_dpdk->bufidx];

	for (rm = nd_dpdk->rcv_mbuf[nd_dpdk->bufidx]; rm; rm = rm->next) {
		r_data = rte_pktmbuf_mtod(rm, void *);
		r_size = rte_pktmbuf_data_len(rm);

#ifdef DEBUG
		lkl_printf("dpdk-rx: mbuf pktlen=%d orig_len=%lu\n",
			   r_size, iov[i].iov_len);
#endif
		/* mergeable buffer starts data after vnet header at [0] */
		if (nd_dpdk->offload & BIT(LKL_VIRTIO_NET_F_MRG_RXBUF) &&
		    i == 0)
			offset = sizeof(struct lkl_virtio_net_hdr_v1);
		else if (nd_dpdk->offload & BIT(LKL_VIRTIO_NET_F_GUEST_TSO4) &&
			 i == 0)
			i++;
		else
			offset = sizeof(struct lkl_virtio_net_hdr_v1);

		read += r_size;
		while (r_size > 0) {
			if (i >= cnt) {
				fprintf(stderr,
					"dpdk-rx: buffer full. skip it. ");
				fprintf(stderr,
					"(cnt=%d, buf[%d]=%lu, size=%lu)\n",
					i, cnt, iov[i].iov_len, r_size);
				goto end;
			}

			copylen = r_size < (iov[i].iov_len - offset) ? r_size
				: iov[i].iov_len - offset;
			memcpy(iov[i].iov_base + offset, r_data, copylen);

			r_size -= copylen;
			offset = 0;
			i++;
		}
	}

end:
	/* TSO (big_packet mode) */
	header->flags = LKL_VIRTIO_NET_HDR_F_DATA_VALID;
	rte_eth_dev_get_mtu(nd_dpdk->portid, &mtu);

	if (read > (mtu + sizeof(struct ether_hdr)
		    + sizeof(struct lkl_virtio_net_hdr_v1))) {
		struct rte_net_hdr_lens hdr_lens;
		uint32_t ptype;

		ptype = rte_net_get_ptype(first, &hdr_lens, RTE_PTYPE_ALL_MASK);

		if ((ptype & RTE_PTYPE_L4_MASK) == RTE_PTYPE_L4_TCP) {
			if ((ptype & RTE_PTYPE_L3_MASK) == RTE_PTYPE_L3_IPV4 &&
			    nd_dpdk->offload & BIT(LKL_VIRTIO_NET_F_GUEST_TSO4))
				header->gso_type = LKL_VIRTIO_NET_HDR_GSO_TCPV4;
			/* XXX: Intel X540 doesn't support LRO
			 * with tcpv6 packets
			 */
			if ((ptype & RTE_PTYPE_L3_MASK) == RTE_PTYPE_L3_IPV6 &&
			    nd_dpdk->offload & BIT(LKL_VIRTIO_NET_F_GUEST_TSO6))
				header->gso_type = LKL_VIRTIO_NET_HDR_GSO_TCPV6;
		}

		header->gso_size = mtu - hdr_lens.l3_len - hdr_lens.l4_len;
		header->hdr_len = hdr_lens.l2_len + hdr_lens.l3_len
			+ hdr_lens.l4_len;
	}

	read += sizeof(struct lkl_virtio_net_hdr_v1);

#ifdef DEBUG
	lkl_printf("dpdk-rx: len=%d mtu=%d type=%d, size=%d, hdrlen=%d\n",
		   read, mtu, header->gso_type,
		   header->gso_size, header->hdr_len);
#endif

	return read;
}


/*
 * this function is not thread-safe.
 *
 * nd_dpdk->rcv_mbuf is specifically not safe in parallel access.  if future
 * refactor allows us to read in parallel, the buffer (nd_dpdk->rcv_mbuf) shall
 * be guarded.
 */
static int dpdk_net_rx(struct lkl_netdev *nd, struct iovec *iov, int cnt)
{
	struct lkl_netdev_dpdk *nd_dpdk;
	int read = 0;

	nd_dpdk = (struct lkl_netdev_dpdk *) nd;

	if (nd_dpdk->npkts == 0) {
		nd_dpdk->npkts = rte_eth_rx_burst(nd_dpdk->portid, 0,
						  nd_dpdk->rcv_mbuf,
						  MAX_PKT_BURST);
		if (nd_dpdk->npkts <= 0) {
			/* XXX: need to implement proper poll()
			 * or interrupt mode PMD of dpdk, which is only
			 * availbale on ixgbe/igb/e1000 (as of Jan. 2016)
			 */
			if (!nd_dpdk->busy_poll)
				usleep(1);
			return -1;
		}
		nd_dpdk->bufidx = 0;
	}

	/* mergeable buffer */
	read = __dpdk_net_rx(nd, iov, cnt);

	rte_pktmbuf_free(nd_dpdk->rcv_mbuf[nd_dpdk->bufidx]);

	nd_dpdk->bufidx++;
	nd_dpdk->npkts--;

	return read;
}

static int dpdk_net_poll(struct lkl_netdev *nd)
{
	struct lkl_netdev_dpdk *nd_dpdk =
		container_of(nd, struct lkl_netdev_dpdk, dev);

	if (nd_dpdk->close)
		return LKL_DEV_NET_POLL_HUP;
	/*
	 * dpdk's interrupt mode has equivalent of epoll_wait(2),
	 * which we can apply here. but AFAIK the mode is only available
	 * on limited NIC drivers like ixgbe/igb/e1000 (with dpdk v2.2.0),
	 * while vmxnet3 is not supported e.g..
	 */
	return LKL_DEV_NET_POLL_RX | LKL_DEV_NET_POLL_TX;
}

static void dpdk_net_poll_hup(struct lkl_netdev *nd)
{
	struct lkl_netdev_dpdk *nd_dpdk =
		container_of(nd, struct lkl_netdev_dpdk, dev);

	nd_dpdk->close = 1;
}

static void dpdk_net_free(struct lkl_netdev *nd)
{
	struct lkl_netdev_dpdk *nd_dpdk =
		container_of(nd, struct lkl_netdev_dpdk, dev);

	free(nd_dpdk);
}

struct lkl_dev_net_ops dpdk_net_ops = {
	.tx = dpdk_net_tx,
	.rx = dpdk_net_rx,
	.poll = dpdk_net_poll,
	.poll_hup = dpdk_net_poll_hup,
	.free = dpdk_net_free,
};


static int dpdk_init;
struct lkl_netdev *lkl_netdev_dpdk_create(const char *ifparams, int offload,
					 unsigned char *mac)
{
	int ret = 0;
	struct rte_eth_conf portconf;
	struct rte_eth_link link;
	struct lkl_netdev_dpdk *nd;
	struct rte_eth_dev_info dev_info;
	char poolname[RTE_MEMZONE_NAMESIZE];
	char *debug = getenv("LKL_HIJACK_DEBUG");
	int lkl_debug = 0;

	if (!dpdk_init) {
		if (debug)
			lkl_debug = strtol(debug, NULL, 0);
		if (lkl_debug & 0x400)
			ealargs[3] = "--log-level=100";

		ret = rte_eal_init(sizeof(ealargs) / sizeof(ealargs[0]),
				   ealargs);
		if (ret < 0)
			lkl_printf("dpdk: failed to initialize eal\n");

		dpdk_init = 1;
	}

	nd = malloc(sizeof(struct lkl_netdev_dpdk));
	memset(nd, 0, sizeof(struct lkl_netdev_dpdk));
	nd->dev.ops = &dpdk_net_ops;
	nd->portid = portid++;
	/* busy-poll mode is described 'ifparams' with "*-busy" */
	nd->busy_poll = strstr(ifparams, "busy") ? 1 : 0;
	/* we always enable big_packet mode with dpdk. */
	nd->offload = offload;

	snprintf(poolname, RTE_MEMZONE_NAMESIZE, "%s%s", "tx-", ifparams);
	nd->txpool =
		rte_mempool_create(poolname,
				   MBUF_NUM, MBUF_SIZ, MEMPOOL_CACHE_SZ,
				   sizeof(struct rte_pktmbuf_pool_private),
				   rte_pktmbuf_pool_init, NULL,
				   rte_pktmbuf_init, NULL, 0, 0);

	if (!nd->txpool) {
		lkl_printf("dpdk: failed to allocate tx pool\n");
		free(nd);
		return NULL;
	}


	snprintf(poolname, RTE_MEMZONE_NAMESIZE, "%s%s", "rx-", ifparams);
	nd->rxpool =
		rte_mempool_create(poolname, MBUF_NUM, MBUF_SIZ, 0,
				   sizeof(struct rte_pktmbuf_pool_private),
				   rte_pktmbuf_pool_init, NULL,
				   rte_pktmbuf_init, NULL, 0, 0);
	if (!nd->rxpool) {
		lkl_printf("dpdk: failed to allocate rx pool\n");
		free(nd);
		return NULL;
	}

	memset(&portconf, 0, sizeof(portconf));

	/* offload bits */
	/* but, we only configure NIC to use TSO *only if* user specifies. */
	if (offload & (BIT(LKL_VIRTIO_NET_F_GUEST_TSO4) |
			BIT(LKL_VIRTIO_NET_F_GUEST_TSO6) |
			BIT(LKL_VIRTIO_NET_F_MRG_RXBUF))) {
		portconf.rxmode.enable_lro = 1;
		portconf.rxmode.hw_strip_crc = 1;
	}

	ret = rte_eth_dev_configure(nd->portid, NUMQUEUE, NUMQUEUE,
				    &portconf);
	if (ret < 0) {
		lkl_printf("dpdk: failed to configure port\n");
		free(nd);
		return NULL;
	}

	rte_eth_dev_info_get(nd->portid, &dev_info);

	ret = rte_eth_rx_queue_setup(nd->portid, 0, NUMDESC, 0,
				     &dev_info.default_rxconf, nd->rxpool);
	if (ret < 0) {
		lkl_printf("dpdk: failed to setup rx queue\n");
		free(nd);
		return NULL;
	}

	dev_info.default_txconf.txq_flags = 0;

	dev_info.default_txconf.txq_flags |= ETH_TXQ_FLAGS_NOXSUMSCTP;
	dev_info.default_txconf.txq_flags |= ETH_TXQ_FLAGS_NOVLANOFFL;


	ret = rte_eth_tx_queue_setup(nd->portid, 0, NUMDESC, 0,
				     &dev_info.default_txconf);
	if (ret < 0) {
		lkl_printf("dpdk: failed to setup tx queue\n");
		free(nd);
		return NULL;
	}

	ret = rte_eth_dev_start(nd->portid);
	/* XXX: this function returns positive val (e.g., 12)
	 * if there's an error
	 */
	if (ret != 0) {
		lkl_printf("dpdk: failed to start device\n");
		free(nd);
		return NULL;
	}

	if (mac) {
		rte_eth_macaddr_get(nd->portid, (struct ether_addr *)mac);
		lkl_printf("Port %d: %02x:%02x:%02x:%02x:%02x:%02x\n",
			   nd->portid,
			   mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	}

	rte_eth_dev_set_link_up(nd->portid);

	rte_eth_link_get(nd->portid, &link);
	if (!link.link_status) {
		fprintf(stderr, "dpdk: interface state is down\n");
		rte_eth_link_get(nd->portid, &link);
		if (!link.link_status) {
			fprintf(stderr,
				"dpdk: interface state is down.. Giving up.\n");
			return NULL;
		}
		lkl_printf("dpdk: interface state should be up now.\n");
	}

	/* should be promisc ? */
	rte_eth_promiscuous_enable(nd->portid);

	/* as we always assume to have vnet_hdr for dpdk device. */
	nd->dev.has_vnet_hdr = 1;

	return (struct lkl_netdev *) nd;
}
