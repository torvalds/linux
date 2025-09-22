/*	$OpenBSD: dhcpd.h,v 1.25 2025/02/07 23:08:48 bluhm Exp $	*/

/*
 * Copyright (c) 2004 Henning Brauer <henning@openbsd.org>
 * Copyright (c) 1995, 1996, 1997, 1998, 1999
 * The Internet Software Consortium.    All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of The Internet Software Consortium nor the names
 *    of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INTERNET SOFTWARE CONSORTIUM AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE INTERNET SOFTWARE CONSORTIUM OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * This software has been written for the Internet Software Consortium
 * by Ted Lemon <mellon@fugue.com> in cooperation with Vixie
 * Enterprises.  To learn more about the Internet Software Consortium,
 * see ``http://www.vix.com/isc''.  To learn more about Vixie
 * Enterprises, see ``http://www.vix.com''.
 */

#include <sys/queue.h>

#define	SERVER_PORT	67
#define	CLIENT_PORT	68

/* Maximum size of client hardware address. */
#define CHADDR_SIZE	16

struct packet_ctx {
	uint8_t				 pc_htype;
	uint8_t				 pc_hlen;
	uint8_t				 pc_smac[CHADDR_SIZE];
	uint8_t				 pc_dmac[CHADDR_SIZE];

	struct sockaddr_storage		 pc_src;
	struct sockaddr_storage		 pc_dst;

	const char 			*pc_circuit;
	int				 pc_circuitlen;
	const char			*pc_remote;
	int				 pc_remotelen;
};

struct iaddr {
	int len;
	unsigned char iabuf[CHADDR_SIZE];
};

struct hardware {
	u_int8_t htype;
	u_int8_t hlen;
	u_int8_t haddr[CHADDR_SIZE];
};

/* Possible states in which the client can be. */
enum dhcp_state {
	S_REBOOTING,
	S_INIT,
	S_SELECTING,
	S_REQUESTING,
	S_BOUND,
	S_RENEWING,
	S_REBINDING
};

/* DHCP relaying modes. */
enum dhcp_relay_mode {
	DRM_UNKNOWN,
	DRM_LAYER2,
	DRM_LAYER3,
};

struct interface_info {
	struct hardware		 hw_address;
	struct in_addr		 primary_address;
	char			 name[IFNAMSIZ];
	int			 rfdesc;
	int			 wfdesc;
	unsigned char		*rbuf;
	size_t			 rbuf_max;
	size_t			 rbuf_offset;
	size_t			 rbuf_len;
	struct ifreq		 ifr;
	int			 noifmedia;
	int			 errors;
	int			 dead;
	u_int16_t		 index;

	int			 ipv6; /* Has any IPv6 address. */
	int			 gipv6; /* Has global IPv6 address. */
	struct in6_addr		 linklocal; /* IPv6 link-local address. */

	TAILQ_ENTRY(interface_info) entry;
};
TAILQ_HEAD(iflist, interface_info);

struct timeout {
	struct timeout	*next;
	time_t		 when;
	void		 (*func)(void *);
	void		*what;
};

struct protocol {
	struct protocol	*next;
	int fd;
	void (*handler)(struct protocol *);
	void *local;
};

struct server_list {
	struct interface_info *intf;
	struct server_list *next;
	struct sockaddr_storage to;
	int fd;
};

#define	DHCPD_LOG_FACILITY	LOG_DAEMON

/* External definitions... */

/* bpf.c */
int if_register_bpf(struct interface_info *);
void if_register_send(struct interface_info *);
void if_register_receive(struct interface_info *, int);
ssize_t send_packet(struct interface_info *,
    struct dhcp_packet *, size_t, struct packet_ctx *);
ssize_t receive_packet(struct interface_info *, unsigned char *, size_t,
    struct packet_ctx *);

/* dispatch.c */
extern void (*bootp_packet_handler)(struct interface_info *,
    struct dhcp_packet *, int, struct packet_ctx *);
struct interface_info *iflist_getbyname(const char *);
void setup_iflist(void);
struct interface_info *register_interface(const char *,
    void (*)(struct protocol *), int isserver);
void dispatch(void);
void got_one(struct protocol *);
void add_protocol(char *, int, void (*)(struct protocol *), void *);
void remove_protocol(struct protocol *);

/* packet.c */
ssize_t assemble_hw_header(unsigned char *, size_t, size_t,
    struct packet_ctx *, unsigned int);
ssize_t assemble_udp_ip_header(unsigned char *, size_t, size_t,
    struct packet_ctx *pc, unsigned char *, size_t);
ssize_t decode_hw_header(unsigned char *, size_t, size_t, struct packet_ctx *,
    unsigned int);
ssize_t decode_udp_ip_header(unsigned char *, size_t, size_t,
    struct packet_ctx *, u_int16_t);

/* dhcrelay.c */
extern int server_fd;
extern struct iflist intflist;

/* crap */
extern time_t cur_time;

static inline struct sockaddr_in *
ss2sin(struct sockaddr_storage *ss)
{
	return ((struct sockaddr_in *)ss);
}

static inline struct sockaddr_in6 *
ss2sin6(struct sockaddr_storage *ss)
{
	return ((struct sockaddr_in6 *)ss);
}
