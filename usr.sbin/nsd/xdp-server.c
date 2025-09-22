/*
 * xdp-server.c -- integration of AF_XDP into nsd
 *
 * Copyright (c) 2024, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
 *
 */

/*
 * Parts inspired by https://github.com/xdp-project/xdp-tutorial
 */

#include "config.h"

#ifdef USE_XDP

#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <linux/limits.h>
#include <sys/mman.h>

#include <sys/poll.h>
#include <sys/resource.h>

/* #include <bpf/bpf.h> */
#include <xdp/xsk.h>
#include <xdp/libxdp.h>
#include <bpf/libbpf.h>

#include <arpa/inet.h>
#include <linux/icmpv6.h>
#include <linux/if_ether.h>
#include <linux/ipv6.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <net/if.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <linux/if_link.h>

#include "query.h"
#include "dns.h"
#include "util.h"
#include "xdp-server.h"
#include "xdp-util.h"
#include "nsd.h"

// TODO: make configurable
#define DNS_PORT 53

struct xdp_config {
	__u32 xdp_flags;
	__u32 libxdp_flags;
	__u16 xsk_bind_flags;
};

struct umem_ptr {
	uint64_t addr;
	uint32_t len;
};

static struct umem_ptr umem_ptrs[XDP_RX_BATCH_SIZE];

/*
 * Allocate memory for UMEM and setup rings
 */
static int
xsk_configure_umem(struct xsk_umem_info *umem_info, uint64_t size);

/*
 * Retrieve a UMEM frame address for allocation
 *
 * Returns XDP_INVALID_UMEM_FRAME when there are no free frames available.
 */
static uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk);

/*
 * Bind AF_XDP socket and setup rings
 */
static int xsk_configure_socket(struct xdp_server *xdp,
                                struct xsk_socket_info *xsk_info,
                                struct xsk_umem_info *umem,
                                uint32_t queue_index);

/*
 * Get number of free frames in UMEM
 */
static uint64_t xsk_umem_free_frames(struct xsk_socket_info *xsk);

/*
 * Free a frame in UMEM
 */
static void xsk_free_umem_frame(struct xsk_socket_info *xsk, uint64_t frame);

/*
 * Fill fill ring with as many frames as possible
 */
static void fill_fq(struct xsk_socket_info *xsk);

/*
 * Load eBPF program to forward traffic to our socket
 */
static int load_xdp_program_and_map(struct xdp_server *xdp);

/*
 * Unload eBPF/XDP program
 */
static void unload_xdp_program(struct xdp_server *xdp);

/*
 * Figure out IP addresses to listen to.
 */
static int figure_ip_addresses(struct xdp_server *xdp);

/*
 * Add IP address to allowed destination addresses for incoming packets
 */
static void add_ip_address(struct xdp_server *xdp,
                           struct sockaddr_storage *addr);

/*
 * Check whether destination IPv4 is in allowed IPs list
 */
static int dest_ip_allowed4(struct xdp_server *xdp, struct iphdr *ipv4);

/*
 * Check whether destination IPv6 is in allowed IPs list
 */
static int dest_ip_allowed6(struct xdp_server *xdp, struct ipv6hdr *ipv6);

/*
 * Setup XDP sockets
 */
static int xdp_sockets_init(struct xdp_server *xdp);

/*
 * Cleanup XDP sockets and memory
 */
static void xdp_sockets_cleanup(struct xdp_server *xdp);

/*
 * Allocate a block of shared memory
 */
static void *alloc_shared_mem(size_t len);

/*
 * Collect free frames from completion queue
 */
static void drain_cq(struct xsk_socket_info *xsk);

/*
 * Send outstanding packets and recollect completed frame addresses
 */
static void handle_tx(struct xsk_socket_info *xsk);

/*
 * Process packet and indicate if it should be dropped
 * return 0 or less => drop
 * return greater than 0 => use for tx
 */
static int
process_packet(struct xdp_server *xdp,
               uint8_t *pkt,
               uint32_t *len,
               struct query *query);

static inline void swap_eth(struct ethhdr *eth);
static inline void swap_udp(struct udphdr *udp);
static inline void swap_ipv6(struct ipv6hdr *ipv6);
static inline void swap_ipv4(struct iphdr *ipv4);
static inline void *parse_udp(struct udphdr *udp);
static inline void *parse_ipv6(struct ipv6hdr *ipv6);
static inline void *parse_ipv4(struct iphdr *ipv4);

/*
 * Parse dns message and return new length of dns message
 */
static uint32_t parse_dns(struct nsd* nsd,
                          uint32_t dnslen,
                          struct query *q,
                          sa_family_t ai_family);

/* *************** */
/* Implementations */
/* *************** */

static uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk) {
	uint64_t frame;
	if (xsk->umem->umem_frame_free == 0) {
		return XDP_INVALID_UMEM_FRAME;
	}

	frame = xsk->umem->umem_frame_addr[--xsk->umem->umem_frame_free];
	xsk->umem->umem_frame_addr[xsk->umem->umem_frame_free] =
		XDP_INVALID_UMEM_FRAME;
	return frame;
}

static uint64_t xsk_umem_free_frames(struct xsk_socket_info *xsk) {
	return xsk->umem->umem_frame_free;
}

static void xsk_free_umem_frame(struct xsk_socket_info *xsk, uint64_t frame) {
	assert(xsk->umem_frame_free < XDP_NUM_FRAMES);
	xsk->umem->umem_frame_addr[xsk->umem->umem_frame_free++] = frame;
}

static void fill_fq(struct xsk_socket_info *xsk) {
	uint32_t stock_frames;
	uint32_t idx_fq = 0;
	/* fill the fill ring with as many frames as are available */
	/* get number of spots available in fq */
	stock_frames = xsk_prod_nb_free(&xsk->umem->fq,
	                                (uint32_t) xsk_umem_free_frames(xsk));
	if (stock_frames > 0) {
		/* ignoring prod__reserve return value, because we got stock_frames
		 * from xsk_prod_nb_free(), which are therefore available */
		xsk_ring_prod__reserve(&xsk->umem->fq, stock_frames, &idx_fq);

		for (uint32_t i = 0; i < stock_frames; ++i) {
			/* TODO: handle lack of available frames?
			 * Is not necessary when the total amount of frames exceeds the
			 * total slots available across all queues combined */
			/* uint64_t frame = xsk_alloc_umem_frame(xsk); */
			/* if (frame == XDP_INVALID_UMEM_FRAME) */
			/*     printf("xdp: trying to fill_addr INVALID UMEM FRAME"); */
			*xsk_ring_prod__fill_addr(&xsk->umem->fq, idx_fq++) =
				xsk_alloc_umem_frame(xsk);
		}

		xsk_ring_prod__submit(&xsk->umem->fq, stock_frames);
	}
}

static int load_xdp_program_and_map(struct xdp_server *xdp) {
	struct bpf_map *map;
	char errmsg[512];
	int err, ret;
	/* UNSPEC => let libxdp decide */
	// TODO: put this into a config option as well?
	enum xdp_attach_mode attach_mode = XDP_MODE_UNSPEC;

	DECLARE_LIBXDP_OPTS(bpf_object_open_opts, opts);
	if (xdp->bpf_bpffs_path)
		opts.pin_root_path = xdp->bpf_bpffs_path;

	/* for now our xdp program should contain just one program section */
	// TODO: look at xdp_program__create because it can take a pinned prog
	xdp->bpf_prog = xdp_program__open_file(xdp->bpf_prog_filename, NULL, &opts);

	// conversion should be fine, libxdp errors shouldn't exceed (int),
	// also libxdp_strerr takes int anyway...
	err = (int) libxdp_get_error(xdp->bpf_prog);
	if (err) {
		libxdp_strerror(err, errmsg, sizeof(errmsg));
		log_msg(LOG_ERR, "xdp: could not open xdp program: %s\n", errmsg);
		return err;
	}

	if (xdp->bpf_prog_should_load) {
		/* TODO: I find setting environment variables from within a program
		 * not a good thing to do, but for the meantime this helps... */
		/* This is done to allow unloading the XDP program we load without
		 * needing the SYS_ADMIN capability, and libxdp doesn't allow skipping
		 * the dispatcher through different means. */
		putenv("LIBXDP_SKIP_DISPATCHER=1");
		err = xdp_program__attach(xdp->bpf_prog, (int) xdp->interface_index, attach_mode, 0);
		/* err = xdp_program__attach_single(xdp->bpf_prog, xdp->interface_index, attach_mode); */
		if (err) {
			libxdp_strerror(err, errmsg, sizeof(errmsg));
			log_msg(LOG_ERR, "xdp: could not attach xdp program to interface '%s' : %s\n", 
					xdp->interface_name, errmsg);
			return err;
		}

		xdp->bpf_prog_fd = xdp_program__fd(xdp->bpf_prog);
		xdp->bpf_prog_id = xdp_program__id(xdp->bpf_prog);

		/* We also need to get the file descriptor to the xsks_map */
		map = bpf_object__find_map_by_name(xdp_program__bpf_obj(xdp->bpf_prog), "xsks_map");
		ret = bpf_map__fd(map);
		if (ret < 0) {
			log_msg(LOG_ERR, "xdp: no xsks map found in xdp program: %s\n", strerror(ret));
			return ret;
		}
		xdp->xsk_map_fd = ret;
		xdp->xsk_map = map;
	} else {
		char map_path[PATH_MAX];
		int fd;

		snprintf(map_path, PATH_MAX, "%s/%s", xdp->bpf_bpffs_path, "xsks_map");

		fd = bpf_obj_get(map_path);
		if (fd < 0) {
			log_msg(LOG_ERR, "xdp: could not retrieve xsks_map pin from %s: %s", map_path, strerror(errno));
			return fd;
		}

		map = bpf_object__find_map_by_name(xdp_program__bpf_obj(xdp->bpf_prog), "xsks_map");
		if ((ret = bpf_map__reuse_fd(map, fd))) {
			log_msg(LOG_ERR, "xdp: could not re-use xsks_map: %s\n", strerror(errno));
			return ret;
		}

		xdp->xsk_map_fd = fd;
		xdp->xsk_map = map;
	}

	return 0;
}

static int
xsk_configure_umem(struct xsk_umem_info *umem_info, uint64_t size) {
	int ret;
	struct xsk_umem_config umem_config = {
		.fill_size = XSK_RING_PROD__NUM_DESCS,
		.comp_size = XSK_RING_CONS__NUM_DESCS,
		.frame_size = XDP_FRAME_SIZE,
		.frame_headroom = XSK_UMEM_FRAME_HEADROOM,
		.flags = XSK_UMEM_FLAGS,
	};

	ret = xsk_umem__create(&umem_info->umem, umem_info->buffer, size, &umem_info->fq, &umem_info->cq, &umem_config);
	if (ret) {
		errno = -ret;
		return ret;
	}

	return 0;
}

static int
xsk_configure_socket(struct xdp_server *xdp, struct xsk_socket_info *xsk_info,
                     struct xsk_umem_info *umem, uint32_t queue_index) {
	uint16_t xsk_bind_flags = XDP_USE_NEED_WAKEUP;
	if (xdp->force_copy) {
		xsk_bind_flags |= XDP_COPY;
	}
	struct xdp_config cfg = {
		.xdp_flags = 0,
		.xsk_bind_flags = xsk_bind_flags,
		.libxdp_flags = XSK_LIBXDP_FLAGS__INHIBIT_PROG_LOAD,
	};

	struct xsk_socket_config xsk_cfg;
	uint32_t idx, reserved;
	int ret;

	xsk_info->umem = umem;
	xsk_cfg.rx_size = XSK_RING_CONS__NUM_DESCS;
	xsk_cfg.tx_size = XSK_RING_PROD__NUM_DESCS;
	xsk_cfg.xdp_flags = cfg.xdp_flags;
	xsk_cfg.bind_flags = cfg.xsk_bind_flags;
	xsk_cfg.libxdp_flags = cfg.libxdp_flags;

	ret = xsk_socket__create(&xsk_info->xsk,
	                         xdp->interface_name,
	                         queue_index,
	                         umem->umem,
	                         &xsk_info->rx,
	                         &xsk_info->tx,
	                         &xsk_cfg);
	if (ret) {
		log_msg(LOG_ERR, "xdp: failed to create xsk_socket");
		goto error_exit;
	}

	ret = xsk_socket__update_xskmap(xsk_info->xsk, xdp->xsk_map_fd);
	if (ret) {
		log_msg(LOG_ERR, "xdp: failed to update xskmap");
		goto error_exit;
	}

	/* Initialize umem frame allocation */
	for (uint32_t i = 0; i < XDP_NUM_FRAMES; ++i) {
		xsk_info->umem->umem_frame_addr[i] = i * XDP_FRAME_SIZE;
	}

	xsk_info->umem->umem_frame_free = XDP_NUM_FRAMES;

	reserved = xsk_ring_prod__reserve(&xsk_info->umem->fq,
	                             XSK_RING_PROD__NUM_DESCS,
	                             &idx);

	if (reserved != XSK_RING_PROD__NUM_DESCS) {
		log_msg(LOG_ERR,
		        "xdp: amount of reserved addr not as expected (is %d)", reserved);
		// "ENOMEM 12 Cannot allocate memory" is the closest to the
		// error that not as much memory was reserved as expected
		ret = -12;
		goto error_exit;
	}

	for (uint32_t i = 0; i < XSK_RING_PROD__NUM_DESCS; ++i) {
		*xsk_ring_prod__fill_addr(&xsk_info->umem->fq, idx++) =
			xsk_alloc_umem_frame(xsk_info);
	}

	xsk_ring_prod__submit(&xsk_info->umem->fq, XSK_RING_PROD__NUM_DESCS);

	return 0;

error_exit:
	errno = -ret;
	return ret;
}

static void *alloc_shared_mem(size_t len) {
	/* MAP_ANONYMOUS memory is initialized with zero */
	return mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
}

static int xdp_sockets_init(struct xdp_server *xdp) {
	size_t umems_len = sizeof(struct xsk_umem_info) * xdp->queue_count;
	size_t xsks_len = sizeof(struct xsk_socket_info) * xdp->queue_count;

	xdp->umems = (struct xsk_umem_info *) alloc_shared_mem(umems_len);
	if (xdp->umems == MAP_FAILED) {
		log_msg(LOG_ERR,
		        "xdp: failed to allocate shared memory for umem info: %s",
		        strerror(errno));
		return -1;
	}

	xdp->xsks = (struct xsk_socket_info *) alloc_shared_mem(xsks_len);
	if (xdp->xsks == MAP_FAILED) {
		log_msg(LOG_ERR,
		        "xdp: failed to allocate shared memory for xsk info: %s",
		        strerror(errno));
		return -1;
	}

	for (uint32_t q_idx = 0; q_idx < xdp->queue_count; ++q_idx) {
		/* mmap is supposedly page-aligned, so should be fine */
		xdp->umems[q_idx].buffer = alloc_shared_mem(XDP_BUFFER_SIZE);

		if (xsk_configure_umem(&xdp->umems[q_idx], XDP_BUFFER_SIZE)) {
			log_msg(LOG_ERR, "xdp: cannot create umem: %s", strerror(errno));
			goto out_err_umem;
		}

		if (xsk_configure_socket(xdp, &xdp->xsks[q_idx], &xdp->umems[q_idx],
		                         q_idx)) {
			log_msg(LOG_ERR,
			        "xdp: cannot create AF_XDP socket: %s",
			        strerror(errno));
			goto out_err_xsk;
		}
	}

	return 0;

out_err_xsk:
	for (uint32_t i = 0; i < xdp->queue_count; ++i)
		xsk_umem__delete(xdp->umems[i].umem);

out_err_umem:
	return -1;
}

static void xdp_sockets_cleanup(struct xdp_server *xdp) {
	for (uint32_t i = 0; i < xdp->queue_count; ++i) {
		xsk_socket__delete(xdp->xsks[i].xsk);
		xsk_umem__delete(xdp->umems[i].umem);
	}
}

int xdp_server_init(struct xdp_server *xdp) {
	struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};

	/* check if interface name exists */
	xdp->interface_index = if_nametoindex(xdp->interface_name);
	if (xdp->interface_index == 0) {
		log_msg(LOG_ERR, "xdp: configured xdp-interface (%s) is unknown: %s",
		        xdp->interface_name, strerror(errno));
		return -1;
	}

	/* (optionally) load xdp program and (definitely) set xsks_map_fd */
	if (load_xdp_program_and_map(xdp)) {
		log_msg(LOG_ERR, "xdp: failed to load/pin xdp program/map");
		return -1;
	}

	/* if we don't do set rlimit, libbpf does it */
	/* this either has to be done before privilege drop or
	 * requires CAP_SYS_RESOURCE */
	if (setrlimit(RLIMIT_MEMLOCK, &rlim)) {
		log_msg(LOG_ERR, "xdp: cannot adjust rlimit (RLIMIT_MEMLOCK): \"%s\"\n",
			strerror(errno));
		return -1;
	}

	if (xdp_sockets_init(xdp))
		return -1;

	for (int i = 0; i < XDP_RX_BATCH_SIZE; ++i) {
		umem_ptrs[i].addr = XDP_INVALID_UMEM_FRAME;
		umem_ptrs[i].len = 0;
	}

	if (!xdp->ip_addresses)
		figure_ip_addresses(xdp);

	return 0;
}

void xdp_server_cleanup(struct xdp_server *xdp) {
	xdp_sockets_cleanup(xdp);

	/* only unpin if we loaded the program */
	if (xdp->bpf_prog_should_load) {
		if (xdp->xsk_map && bpf_map__is_pinned(xdp->xsk_map)) {
			if (bpf_map__unpin(xdp->xsk_map, NULL)) {
				/* We currently ship an XDP program that doesn't pin the map. So
				 * if this error happens, it is because the user specified their
				 * custom XDP program to load by NSD. Therefore they should know
				 * about the pinned map and be able to unlink it themselves.
				 */
				log_msg(LOG_ERR, "xdp: failed to unpin bpf map during cleanup: \"%s\". "
				        "This is usually ok, but you need to unpin the map yourself. "
				        "This can usually be fixed by executing chmod o+wx %s\n",
				        strerror(errno), xdp->bpf_bpffs_path);
			}
		}

		unload_xdp_program(xdp);
	}
}

static void unload_xdp_program(struct xdp_server *xdp) {
	DECLARE_LIBBPF_OPTS(bpf_xdp_attach_opts, bpf_opts,
	                    .old_prog_fd = xdp->bpf_prog_fd);

	log_msg(LOG_INFO, "xdp: detaching xdp program %u from %s\n",
			xdp->bpf_prog_id, xdp->interface_name);

	if (bpf_xdp_detach((int) xdp->interface_index, 0, &bpf_opts))
		log_msg(LOG_ERR, "xdp: failed to detach xdp program: %s\n",
		        strerror(errno));
}

static int dest_ip_allowed6(struct xdp_server *xdp, struct ipv6hdr *ipv6) {
	struct xdp_ip_address *ip = xdp->ip_addresses;
	if (!ip)
		// no IPs available, allowing all
		return 1;

	while (ip) {
		if (ip->addr.ss_family == AF_INET6 &&
		    !memcmp(&(((struct sockaddr_in6 *) &ip->addr)->sin6_addr),
		            &ipv6->daddr,
		            sizeof(struct in6_addr)))
			return 1;
		ip = ip->next;
	}

	return 0;
}

static int dest_ip_allowed4(struct xdp_server *xdp, struct iphdr *ipv4) {
	struct xdp_ip_address *ip = xdp->ip_addresses;
	if (!ip)
		// no IPs available, allowing all
		return 1;

	while (ip) {
		if (ip->addr.ss_family == AF_INET &&
		    ipv4->daddr == ((struct sockaddr_in *) &ip->addr)->sin_addr.s_addr)
			return 1;
		ip = ip->next;
	}

	return 0;
}

static void
add_ip_address(struct xdp_server *xdp, struct sockaddr_storage *addr) {
	struct xdp_ip_address *ip = xdp->ip_addresses;
	if (!ip) {
		xdp->ip_addresses = region_alloc_zero(xdp->region,
		                                      sizeof(struct xdp_ip_address));
		ip = xdp->ip_addresses;
	} else {
		while (ip->next)
			ip = ip->next;

		ip->next = region_alloc_zero(xdp->region,
		                             sizeof(struct xdp_ip_address));
		ip = ip->next;
	}

	memcpy(&ip->addr, addr, sizeof(struct sockaddr_storage));
}

static int figure_ip_addresses(struct xdp_server *xdp) {
	// TODO: if using VLANs, also find appropriate IP addresses?
	struct ifaddrs *ifaddr;
	int family, ret = 0;

	if (getifaddrs(&ifaddr) == -1) {
		log_msg(LOG_ERR, "xdp: couldn't determine local IP addresses. "
		                 "Serving all IP addresses now");
		return -1;
	}

	for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL)
			continue;

		if (strcmp(ifa->ifa_name, xdp->interface_name))
			continue;

		family = ifa->ifa_addr->sa_family;

		switch (family) {
			default:
				continue;
			case AF_INET:
			case AF_INET6:
				add_ip_address(xdp, (struct sockaddr_storage *) ifa->ifa_addr);
		}
	}

	freeifaddrs(ifaddr);
	return ret;
}

static inline void swap_eth(struct ethhdr *eth) {
	uint8_t tmp_mac[ETH_ALEN];
	memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
	memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
	memcpy(eth->h_source, tmp_mac, ETH_ALEN);
}

static inline void swap_udp(struct udphdr *udp) {
	uint16_t tmp_port; /* not touching endianness */
	tmp_port = udp->source;
	udp->source = udp->dest;
	udp->dest = tmp_port;
}

static inline void swap_ipv6(struct ipv6hdr *ipv6) {
	struct in6_addr tmp_ip;
	memcpy(&tmp_ip, &ipv6->saddr, sizeof(tmp_ip));
	memcpy(&ipv6->saddr, &ipv6->daddr, sizeof(tmp_ip));
	memcpy(&ipv6->daddr, &tmp_ip, sizeof(tmp_ip));
}

static inline void swap_ipv4(struct iphdr *ipv4) {
	struct in_addr tmp_ip;
	memcpy(&tmp_ip, &ipv4->saddr, sizeof(tmp_ip));
	memcpy(&ipv4->saddr, &ipv4->daddr, sizeof(tmp_ip));
	memcpy(&ipv4->daddr, &tmp_ip, sizeof(tmp_ip));
}

static inline void *parse_udp(struct udphdr *udp) {
	if (ntohs(udp->dest) != DNS_PORT)
		return NULL;

	return (void *)(udp + 1);
}

static inline void *parse_ipv6(struct ipv6hdr *ipv6) {
	if (ipv6->nexthdr != IPPROTO_UDP)
		return NULL;

	return (void *)(ipv6 + 1);
}

static inline void *parse_ipv4(struct iphdr *ipv4) {
	if (ipv4->protocol != IPPROTO_UDP)
		return NULL;

	return (void *)(ipv4 + 1);
}

static uint32_t parse_dns(struct nsd* nsd, uint32_t dnslen,
                          struct query *q, sa_family_t ai_family) {
	/* TODO: implement DNSTAP, PROXYv2, ...? */
	uint32_t now = 0;

	/* set the size of the dns message and move position to start */
	buffer_skip(q->packet, dnslen);
	buffer_flip(q->packet);

	if (query_process(q, nsd, &now) != QUERY_DISCARDED) {
		if (RCODE(q->packet) == RCODE_OK && !AA(q->packet)) {
			STATUP(nsd, nona);
			ZTATUP(nsd, q->zone, nona);
		}

#ifdef USE_ZONE_STATS
		if (ai_family == AF_INET) {
			ZTATUP(nsd, q->zone, qudp);
		} else if (ai_family == AF_INET6) {
			ZTATUP(nsd, q->zone, qudp6);
		}
#endif /* USE_ZONE_STATS */

		query_add_optional(q, nsd, &now);

		buffer_flip(q->packet);

#ifdef BIND8_STATS
			/* Account the rcode & TC... */
			STATUP2(nsd, rcode, RCODE(q->packet));
			ZTATUP2(nsd, q->zone, rcode, RCODE(q->packet));
			if (TC(q->packet)) {
				STATUP(nsd, truncated);
				ZTATUP(nsd, q->zone, truncated);
			}
#endif /* BIND8_STATS */

		/* return new dns message length */
		return (uint32_t) buffer_remaining(q->packet);
	} else {
		query_reset(q, UDP_MAX_MESSAGE_LEN, 0);
		STATUP(nsd, dropped);
		ZTATUP(nsd, q->zone, dropped);
		return 0;
	}
}

static int
process_packet(struct xdp_server *xdp, uint8_t *pkt,
               uint32_t *len, struct query *query) {
	/* log_msg(LOG_INFO, "xdp: received packet with len %d", *len); */

	uint32_t dnslen = *len;
	uint32_t data_before_dnshdr_len = 0;

	struct ethhdr *eth = (struct ethhdr *)pkt;
	struct ipv6hdr *ipv6 = NULL;
	struct iphdr *ipv4 = NULL;
	struct udphdr *udp = NULL;
	void *dnshdr = NULL;

	/* doing the check here, so that the packet/frame is large enough to contain
	 * at least an ethernet header, an ipv4 header (ipv6 header is larger), and
	 * a udp header.
	 */
	if (*len < (sizeof(*eth) + sizeof(struct iphdr) + sizeof(*udp)))
		return -1;

	data_before_dnshdr_len = sizeof(*eth) + sizeof(*udp);

	switch (ntohs(eth->h_proto)) {
	case ETH_P_IPV6: {
		ipv6 = (struct ipv6hdr *)(eth + 1);

		if (*len < (sizeof(*eth) + sizeof(*ipv6) + sizeof(*udp)))
			return -2;
		if (!(udp = parse_ipv6(ipv6)))
			return -3;

		dnslen -= (uint32_t) (sizeof(*eth) + sizeof(*ipv6) + sizeof(*udp));
		data_before_dnshdr_len += sizeof(*ipv6);

		if (!dest_ip_allowed6(xdp, ipv6))
			return -4;

		break;
	} case ETH_P_IP: {
		ipv4 = (struct iphdr *)(eth + 1);

		if (!(udp = parse_ipv4(ipv4)))
			return -5;

		dnslen -= (uint32_t) (sizeof(*eth) + sizeof(*ipv4) + sizeof(*udp));
		data_before_dnshdr_len += sizeof(*ipv4);

		if (!dest_ip_allowed4(xdp, ipv4))
			return -6;

		break;
	}

	/* TODO: vlan? */
	/* case ETH_P_8021AD: case ETH_P_8021Q: */
	/*     if (*len < (sizeof(*eth) + sizeof(*vlan))) */
	/*         break; */
	default:
		return -7;
	}

	if (!(dnshdr = parse_udp(udp)))
		return -8;

	query_set_buffer_data(query, dnshdr, XDP_FRAME_SIZE - data_before_dnshdr_len);

	if(ipv6) {
#ifdef INET6
		struct sockaddr_in6* sock6 = (struct sockaddr_in6*)&query->remote_addr;
		sock6->sin6_family = AF_INET6;
		sock6->sin6_port = udp->dest;
		sock6->sin6_flowinfo = 0;
		sock6->sin6_scope_id = 0;
		memcpy(&sock6->sin6_addr, &ipv6->saddr, sizeof(ipv6->saddr));
		query->remote_addrlen = (socklen_t)sizeof(struct sockaddr_in6);
#else
		return 0; /* no inet6 no network */
#endif /* INET6 */
#ifdef BIND8_STATS
		STATUP(xdp->nsd, qudp6);
#endif /* BIND8_STATS */
	} else {
		struct sockaddr_in* sock4 = (struct sockaddr_in*)&query->remote_addr;
		sock4->sin_family = AF_INET;
		sock4->sin_port = udp->dest;
		sock4->sin_addr.s_addr = ipv4->saddr;
		query->remote_addrlen = (socklen_t)sizeof(struct sockaddr_in);
#ifdef BIND8_STATS
		STATUP(xdp->nsd, qudp);
#endif /* BIND8_STATS */
	}

	query->client_addr    = query->remote_addr;
	query->client_addrlen = query->remote_addrlen;
	query->is_proxied = 0;

	dnslen = parse_dns(xdp->nsd, dnslen, query, query->remote_addr.ss_family);
	if (!dnslen) {
		return -9;
	}

	// Not verifying the packet length (that it fits in an IP packet), as
	// parse_dns truncates too long response messages.
	udp->len = htons((uint16_t) (sizeof(*udp) + dnslen));

	swap_eth(eth);
	swap_udp(udp);

	if (ipv4) {
		swap_ipv4(ipv4);
		__be16 ipv4_old_len = ipv4->tot_len;
		ipv4->tot_len = htons(sizeof(*ipv4)) + udp->len;
		csum16_replace(&ipv4->check, ipv4_old_len, ipv4->tot_len);
		udp->check = calc_csum_udp4(udp, ipv4);
	} else if (ipv6) {
		swap_ipv6(ipv6);
		ipv6->payload_len = udp->len;
		udp->check = calc_csum_udp6(udp, ipv6);
	} else {
		log_msg(LOG_ERR, "xdp: we forgot to implement something... oops");
		return 0;
	}

	/* log_msg(LOG_INFO, "xdp: done with processing the packet"); */

	*len = data_before_dnshdr_len + dnslen;
	return 1;
}

void xdp_handle_recv_and_send(struct xdp_server *xdp) {
	struct xsk_socket_info *xsk = &xdp->xsks[xdp->queue_index];
	unsigned int recvd, i, reserved, to_send = 0;
	uint32_t idx_rx = 0;
	int ret;

	recvd = xsk_ring_cons__peek(&xsk->rx, XDP_RX_BATCH_SIZE, &idx_rx);
	if (!recvd) {
		/* no data available */
		return;
	}

	fill_fq(xsk);

	/* Process received packets */
	for (i = 0; i < recvd; ++i) {
		uint64_t addr = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx)->addr;
		uint32_t len = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx++)->len;

		uint8_t *pkt = xsk_umem__get_data(xsk->umem->buffer, addr);
		if ((ret = process_packet(xdp, pkt, &len, xdp->queries[i])) <= 0) {
			/* drop packet */
			xsk_free_umem_frame(xsk, addr);
		} else {
			umem_ptrs[to_send].addr = addr;
			umem_ptrs[to_send].len = len;
			++to_send;
		}
		/* we can reset the query directly after each packet processing,
		 * because the reset does not delete the underlying buffer/data.
		 * However, if we, in future, need to access data from the query
		 * struct when sending the answer, this needs to change.
		 * This also means, that currently a single query instance (and
		 * not an array) would suffice for this implementation. */
		query_reset(xdp->queries[i], UDP_MAX_MESSAGE_LEN, 0);

		/* xsk->stats.rx_bytes += len; */
	}

	xsk_ring_cons__release(&xsk->rx, recvd);
	/* xsk->stats.rx_packets += rcvd; */

	/* Process sending packets */

	uint32_t tx_idx = 0;

	/* TODO: at least send as many packets as slots are available */
	reserved = xsk_ring_prod__reserve(&xsk->tx, to_send, &tx_idx);
	// if we can't reserve to_send frames, we'll get 0 frames, so
	// no need to "un-reserve"
	if (reserved != to_send) {
		// not enough tx slots available, drop packets
		log_msg(LOG_ERR, "xdp: not enough TX frames available, dropping "
		        "whole batch");
		for (i = 0; i < to_send; ++i) {
			xsk_free_umem_frame(xsk, umem_ptrs[i].addr);
			umem_ptrs[i].addr = XDP_INVALID_UMEM_FRAME;
			umem_ptrs[i].len = 0;
		}
#ifdef BIND8_STATS
		xdp->nsd->st->txerr += to_send;
#endif /* BIND8_STATS */
		to_send = 0;
	}

	for (i = 0; i < to_send; ++i) {
		uint64_t addr = umem_ptrs[i].addr;
		uint32_t len = umem_ptrs[i].len;
		xsk_ring_prod__tx_desc(&xsk->tx, tx_idx)->addr = addr;
		xsk_ring_prod__tx_desc(&xsk->tx, tx_idx)->len = len;
		tx_idx++;
		xsk->outstanding_tx++;
		umem_ptrs[i].addr = XDP_INVALID_UMEM_FRAME;
		umem_ptrs[i].len = 0;
	}

	xsk_ring_prod__submit(&xsk->tx, to_send);

	/* wake up kernel for tx if needed and collect completed tx buffers */
	handle_tx(xsk);
	/* TODO: maybe call fill_fq(xsk) here as well? */
}

static void drain_cq(struct xsk_socket_info *xsk) {
	uint32_t completed, idx_cq;

	/* free completed TX buffers */
	completed = xsk_ring_cons__peek(&xsk->umem->cq,
	                                XSK_RING_CONS__NUM_DESCS,
	                                &idx_cq);

	if (completed > 0) {
		for (uint32_t i = 0; i < completed; i++) {
			xsk_free_umem_frame(xsk, *xsk_ring_cons__comp_addr(&xsk->umem->cq,
			                                                   idx_cq++));
		}

		xsk_ring_cons__release(&xsk->umem->cq, completed);
		xsk->outstanding_tx -= completed < xsk->outstanding_tx ?
		                       completed : xsk->outstanding_tx;
	}
}

static void handle_tx(struct xsk_socket_info *xsk) {
	if (!xsk->outstanding_tx)
		return;

	if (xsk_ring_prod__needs_wakeup(&xsk->tx))
		sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);

	drain_cq(xsk);

	// Update TX-queue pointers
	// This is not needed, because prod__reserve calls this function too,
	// and therefore, if not enough frames are free on the cached pointers,
	// it will update the real pointers.
	/* xsk_prod_nb_free(&xsk->tx, XSK_RING_PROD__NUM_DESCS/4); */
}

#endif /* USE_XDP */
