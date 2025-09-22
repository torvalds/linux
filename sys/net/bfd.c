/*	$OpenBSD: bfd.c,v 1.81 2025/07/24 00:49:22 jsg Exp $	*/

/*
 * Copyright (c) 2016-2018 Peter Hessler <phessler@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Support for Bi-directional Forwarding Detection (RFC 5880 / 5881)
 */

#include <sys/param.h>
#include <sys/errno.h>

#include <sys/task.h>
#include <sys/pool.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/stdint.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/ip.h>

#include <net/bfd.h>

/*
 * RFC 5880 Page 7
 * The Mandatory Section of a BFD Control packet has the following
 * format:
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |Vers | Diag    |Sta|P|F|C|A|D|M|  Detect Mult  |    Length     |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      My Discriminator                         |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                     Your Discriminator                        |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                  Desired Min TX Interval                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                 Required Min RX Interval                      |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                Required Min Echo RX Interval                  |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *
 * An optional Authentication Section MAY be present:
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |   Auth Type   |   Auth Len    |     Authentication Data...    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 */

/* BFD on-wire format */
struct bfd_header {
	uint8_t	bfd_ver_diag;
	uint8_t	bfd_sta_flags;

	uint8_t		bfd_detect_multi;	/* detection time multiplier */
	uint8_t		bfd_length;		/* in bytes */
	uint32_t	bfd_my_discriminator;		/* From this system */
	uint32_t	bfd_your_discriminator;		/* Received */
	uint32_t	bfd_desired_min_tx_interval;	/* in microseconds */
	uint32_t	bfd_required_min_rx_interval;	/* in microseconds */
	uint32_t	bfd_required_min_echo_interval;	/* in microseconds */
} __packed;

/* optional authentication on-wire format */
struct bfd_auth_header {
	uint8_t	bfd_auth_type;
	uint8_t	bfd_auth_len;
	uint16_t	bfd_auth_data;
} __packed;

#define BFD_VERSION		1	/* RFC 5880 Page 6 */
#define BFD_VER(x)		(((x) & 0xe0) >> 5)
#define BFD_DIAG(x)		((x) & 0x1f)
#define BFD_STATE(x)		(((x) & 0xc0) >> 6)
#define BFD_FLAGS(x)		((x) & 0x3f)
#define BFD_HDRLEN		24	/* RFC 5880 Page 37 */
#define BFD_AUTH_SIMPLE_LEN	16 + 3	/* RFC 5880 Page 10 */
#define BFD_AUTH_MD5_LEN	24	/* RFC 5880 Page 11 */
#define BFD_AUTH_SHA1_LEN	28	/* RFC 5880 Page 12 */

/* Diagnostic Code (RFC 5880 Page 8) */
#define BFD_DIAG_NONE			0
#define BFD_DIAG_EXPIRED		1
#define BFD_DIAG_ECHO_FAILED		2
#define BFD_DIAG_NEIGHBOR_SIGDOWN	3
#define BFD_DIAG_FIB_RESET		4
#define BFD_DIAG_PATH_DOWN		5
#define BFD_DIAG_CONCAT_PATH_DOWN	6
#define BFD_DIAG_ADMIN_DOWN		7
#define BFD_DIAG_CONCAT_REVERSE_DOWN	8

/* State (RFC 5880 Page 8) */
#define BFD_STATE_ADMINDOWN		0
#define BFD_STATE_DOWN			1
#define BFD_STATE_INIT			2
#define BFD_STATE_UP			3

/* Flags (RFC 5880 Page 8) */
#define BFD_FLAG_P			0x20
#define BFD_FLAG_F			0x10
#define BFD_FLAG_C			0x08
#define BFD_FLAG_A			0x04
#define BFD_FLAG_D			0x02
#define BFD_FLAG_M			0x01


/* Auth Type (RFC 5880 Page 10) */
#define BFD_AUTH_TYPE_RESERVED		0
#define BFD_AUTH_TYPE_SIMPLE		1
#define BFD_AUTH_KEYED_MD5		2
#define BFD_AUTH_METICULOUS_MD5		3
#define BFD_AUTH_KEYED_SHA1		4
#define BFD_AUTH_METICULOUS_SHA1	5

#define BFD_UDP_PORT_CONTROL		3784
#define BFD_UDP_PORT_ECHO		3785

#define BFD_SECOND			1000000 /* 1,000,000 us == 1 second */
/* We currently tick every 10ms, so force a minimum that can be handled */
#define BFD_MINIMUM			50000	/* 50,000 us == 50 ms */


struct pool	 bfd_pool, bfd_pool_neigh, bfd_pool_time;
struct taskq	*bfdtq;


struct bfd_config *bfd_lookup(const struct rtentry *);
void		 bfddestroy(void);

struct socket	*bfd_listener(struct bfd_config *, unsigned int);
struct socket	*bfd_sender(struct bfd_config *, unsigned int);
void		 bfd_input(struct bfd_config *, struct mbuf *);
void		 bfd_set_state(struct bfd_config *, unsigned int);

int	 bfd_send(struct bfd_config *, struct mbuf *);
void	 bfd_send_control(void *);

void	 bfd_start_task(void *);
void	 bfd_send_task(void *);
void	 bfd_upcall_task(void *);
void	 bfd_clear_task(void *);
void	 bfd_error(struct bfd_config *);
void	 bfd_timeout_rx(void *);
void	 bfd_timeout_tx(void *);

void	 bfd_upcall(struct socket *, caddr_t, int);
void	 bfd_senddown(struct bfd_config *);
void	 bfd_reset(struct bfd_config *);
void	 bfd_set_uptime(struct bfd_config *);

void	 bfd_debug(struct bfd_config *);

TAILQ_HEAD(bfd_queue, bfd_config)  bfd_queue;

/*
 * allocate a new bfd session
 */
int
bfdset(struct rtentry *rt)
{
	struct bfd_config	*bfd;

	/* at the moment it is not allowed to run BFD on indirect routes */
	if (ISSET(rt->rt_flags, RTF_GATEWAY) || !ISSET(rt->rt_flags, RTF_HOST))
		return (EINVAL);

	/* Do our necessary memory allocations upfront */
	bfd = pool_get(&bfd_pool, PR_WAITOK | PR_ZERO);

	/* make sure we don't already have this setup */
	if (bfd_lookup(rt) != NULL) {
		pool_put(&bfd_pool, bfd);
		return (EADDRINUSE);
	}

	bfd->bc_neighbor = pool_get(&bfd_pool_neigh, PR_WAITOK | PR_ZERO);
	bfd->bc_time = pool_get(&bfd_pool_time, PR_WAITOK | PR_ZERO);

	bfd->bc_rt = rt;
	rtref(bfd->bc_rt);	/* we depend on this route not going away */

	getmicrotime(bfd->bc_time);
	bfd_reset(bfd);
	bfd->bc_neighbor->bn_ldiscr = arc4random();

	if (!timeout_initialized(&bfd->bc_timo_rx))
		timeout_set(&bfd->bc_timo_rx, bfd_timeout_rx, bfd);
	if (!timeout_initialized(&bfd->bc_timo_tx))
		timeout_set(&bfd->bc_timo_tx, bfd_timeout_tx, bfd);

	task_set(&bfd->bc_bfd_task, bfd_start_task, bfd);
	task_set(&bfd->bc_clear_task, bfd_clear_task, bfd);

	task_add(bfdtq, &bfd->bc_bfd_task);

	TAILQ_INSERT_TAIL(&bfd_queue, bfd, bc_entry);
	bfd_set_state(bfd, BFD_STATE_DOWN);

	return (0);
}

/*
 * remove and free a bfd session
 */
void
bfdclear(struct rtentry *rt)
{
	struct bfd_config *bfd;

	if ((bfd = bfd_lookup(rt)) == NULL)
		return;

	task_add(bfdtq, &bfd->bc_clear_task);
}

void
bfd_clear_task(void *arg)
{
	struct bfd_config	*bfd = (struct bfd_config *)arg;
	struct rtentry		*rt = bfd->bc_rt;

	timeout_del(&bfd->bc_timo_rx);
	timeout_del(&bfd->bc_timo_tx);
	task_del(bfdtq, &bfd->bc_upcall_task);
	task_del(bfdtq, &bfd->bc_bfd_send_task);

	TAILQ_REMOVE(&bfd_queue, bfd, bc_entry);

	/* inform our neighbor */
	bfd_senddown(bfd);

	rt->rt_flags &= ~RTF_BFD;
	if (bfd->bc_so) {
		/* remove upcall before calling soclose or it will be called */
		bfd->bc_so->so_upcall = NULL;
		soclose(bfd->bc_so, MSG_DONTWAIT);
	}
	if (bfd->bc_soecho) {
		bfd->bc_soecho->so_upcall = NULL;
		soclose(bfd->bc_soecho, MSG_DONTWAIT);
	}
	if (bfd->bc_sosend)
		soclose(bfd->bc_sosend, MSG_DONTWAIT);

	rtfree(bfd->bc_rt);
	bfd->bc_rt = NULL;

	pool_put(&bfd_pool_time, bfd->bc_time);
	pool_put(&bfd_pool_neigh, bfd->bc_neighbor);
	pool_put(&bfd_pool, bfd);
}

/*
 * Create and initialize the global bfd framework
 */
void
bfdinit(void)
{
	pool_init(&bfd_pool, sizeof(struct bfd_config), 0,
	    IPL_SOFTNET, 0, "bfd_config", NULL);
	pool_init(&bfd_pool_neigh, sizeof(struct bfd_neighbor), 0,
	    IPL_SOFTNET, 0, "bfd_config_peer", NULL);
	pool_init(&bfd_pool_time, sizeof(struct timeval), 0,
	    IPL_SOFTNET, 0, "bfd_config_time", NULL);

	bfdtq = taskq_create("bfd", 1, IPL_SOFTNET, 0);
	if (bfdtq == NULL)
		panic("unable to create BFD taskq");

	TAILQ_INIT(&bfd_queue);
}

/*
 * Destroy all bfd sessions and remove the tasks
 *
 */
void
bfddestroy(void)
{
	struct bfd_config	*bfd;

	/* inform our neighbor we are rebooting */
	while ((bfd = TAILQ_FIRST(&bfd_queue))) {
		bfd->bc_neighbor->bn_ldiag = BFD_DIAG_FIB_RESET;
		bfdclear(bfd->bc_rt);
	}

	taskq_barrier(bfdtq);
	taskq_destroy(bfdtq);
	pool_destroy(&bfd_pool_time);
	pool_destroy(&bfd_pool_neigh);
	pool_destroy(&bfd_pool);
}

/*
 * Return the matching bfd
 */
struct bfd_config *
bfd_lookup(const struct rtentry *rt)
{
	struct bfd_config *bfd;

	TAILQ_FOREACH(bfd, &bfd_queue, bc_entry) {
		if (bfd->bc_rt == rt)
			return (bfd);
	}
	return (NULL);
}

struct sockaddr *
bfd2sa(const struct rtentry *rt, struct sockaddr_bfd *sa_bfd)
{
	struct bfd_config *bfd;

	bfd = bfd_lookup(rt);

	if (bfd == NULL)
		return (NULL);

	memset(sa_bfd, 0, sizeof(*sa_bfd));
	sa_bfd->bs_len = sizeof(*sa_bfd);
	sa_bfd->bs_family = bfd->bc_rt->rt_dest->sa_family;

	sa_bfd->bs_mode = bfd->bc_mode;
	sa_bfd->bs_mintx = bfd->bc_mintx;
	sa_bfd->bs_minrx = bfd->bc_minrx;
	sa_bfd->bs_minecho = bfd->bc_minecho;
	sa_bfd->bs_multiplier = bfd->bc_multiplier;

	sa_bfd->bs_uptime = bfd->bc_time->tv_sec;
	sa_bfd->bs_lastuptime = bfd->bc_lastuptime;
	sa_bfd->bs_state = bfd->bc_state;
	sa_bfd->bs_remotestate = bfd->bc_neighbor->bn_rstate;
	sa_bfd->bs_laststate = bfd->bc_laststate;
	sa_bfd->bs_error = bfd->bc_error;

	sa_bfd->bs_localdiscr = bfd->bc_neighbor->bn_ldiscr;
	sa_bfd->bs_localdiag = bfd->bc_neighbor->bn_ldiag;
	sa_bfd->bs_remotediscr = bfd->bc_neighbor->bn_rdiscr;
	sa_bfd->bs_remotediag = bfd->bc_neighbor->bn_rdiag;

	return ((struct sockaddr *)sa_bfd);
}

/*
 * End of public interfaces.
 *
 * Everything below this line should not be used outside of this file.
 */

/*
 * Task to listen and kick off the bfd process
 */
void
bfd_start_task(void *arg)
{
	struct bfd_config	*bfd = (struct bfd_config *)arg;

	/* start listeners */
	bfd->bc_so = bfd_listener(bfd, BFD_UDP_PORT_CONTROL);
	if (!bfd->bc_so)
		printf("bfd_listener(%d) failed\n",
		    BFD_UDP_PORT_CONTROL);
	bfd->bc_soecho = bfd_listener(bfd, BFD_UDP_PORT_ECHO);
	if (!bfd->bc_soecho)
		printf("bfd_listener(%d) failed\n",
		    BFD_UDP_PORT_ECHO);

	/* start sending */
	bfd->bc_sosend = bfd_sender(bfd, BFD_UDP_PORT_CONTROL);
	if (bfd->bc_sosend) {
		task_set(&bfd->bc_bfd_send_task, bfd_send_task, bfd);
		task_add(bfdtq, &bfd->bc_bfd_send_task);
	}

	task_set(&bfd->bc_upcall_task, bfd_upcall_task, bfd);

	return;
}

void
bfd_send_task(void *arg)
{
	struct bfd_config	*bfd = (struct bfd_config *)arg;
	struct rtentry		*rt = bfd->bc_rt;

	if (ISSET(rt->rt_flags, RTF_UP)) {
		bfd_send_control(bfd);
	} else {
		if (bfd->bc_neighbor->bn_lstate > BFD_STATE_DOWN) {
			bfd->bc_error++;
			bfd->bc_neighbor->bn_ldiag = BFD_DIAG_PATH_DOWN;
			bfd_reset(bfd);
			bfd_set_state(bfd, BFD_STATE_DOWN);
		}
	}
//rtm_bfd(bfd);

	/* re-add 70%-90% jitter to our transmits, rfc 5880 6.8.7 */
	timeout_add_usec(&bfd->bc_timo_tx,
	    bfd->bc_mintx * (arc4random_uniform(20) + 70) / 100);
}

/*
 * Setup a bfd listener socket
 */
struct socket *
bfd_listener(struct bfd_config *bfd, unsigned int port)
{
	struct proc		*p = curproc;
	struct rtentry		*rt = bfd->bc_rt;
	struct sockaddr		*src = rt->rt_ifa->ifa_addr;
	struct sockaddr		*dst = rt_key(rt);
	struct sockaddr		*sa;
	struct sockaddr_in	*sin;
	struct sockaddr_in6	*sin6;
	struct socket		*so;
	struct mbuf		*m = NULL, *mopt = NULL;
	int			*ip, error;

	/* sa_family and sa_len must be equal */
	if (src->sa_family != dst->sa_family || src->sa_len != dst->sa_len)
		return (NULL);

	error = socreate(dst->sa_family, &so, SOCK_DGRAM, 0);
	if (error) {
		printf("%s: socreate error %d\n",
		    __func__, error);
		return (NULL);
	}

	MGET(mopt, M_WAIT, MT_SOOPTS);
	mopt->m_len = sizeof(int);
	ip = mtod(mopt, int *);
	*ip = MAXTTL;
	error = sosetopt(so, IPPROTO_IP, IP_MINTTL, mopt);
	m_freem(mopt);
	if (error) {
		printf("%s: sosetopt error %d\n",
		    __func__, error);
		goto close;
	}

	MGET(m, M_WAIT, MT_SONAME);
	m->m_len = src->sa_len;
	sa = mtod(m, struct sockaddr *);
	memcpy(sa, src, src->sa_len);
	switch(sa->sa_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)sa;
		sin->sin_port = htons(port);
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)sa;
		sin6->sin6_port = htons(port);
		break;
	default:
		break;
	}

	solock(so);
	error = sobind(so, m, p);
	sounlock(so);
	if (error) {
		printf("%s: sobind error %d\n",
		    __func__, error);
		goto close;
	}
	so->so_upcallarg = (caddr_t)bfd;
	so->so_upcall = bfd_upcall;

	m_free(m);

	return (so);

 close:
	m_free(m);
	soclose(so, MSG_DONTWAIT);

	return (NULL);
}

/*
 * Setup the bfd sending process
 */
struct socket *
bfd_sender(struct bfd_config *bfd, unsigned int port)
{
	struct socket		*so;
	struct rtentry		*rt = bfd->bc_rt;
	struct proc		*p = curproc;
	struct mbuf		*m = NULL, *mopt = NULL;
	struct sockaddr		*src = rt->rt_ifa->ifa_addr;
	struct sockaddr		*dst = rt_key(rt);
	struct sockaddr		*sa;
	struct sockaddr_in6	*sin6;
	struct sockaddr_in	*sin;
	int		 error, *ip;

	/* sa_family and sa_len must be equal */
	if (src->sa_family != dst->sa_family || src->sa_len != dst->sa_len)
		return (NULL);

	error = socreate(dst->sa_family, &so, SOCK_DGRAM, 0);

	if (error)
		return (NULL);

	MGET(mopt, M_WAIT, MT_SOOPTS);
	mopt->m_len = sizeof(int);
	ip = mtod(mopt, int *);
	*ip = IP_PORTRANGE_HIGH;
	error = sosetopt(so, IPPROTO_IP, IP_PORTRANGE, mopt);
	m_freem(mopt);
	if (error) {
		printf("%s: sosetopt error %d\n",
		    __func__, error);
		goto close;
	}

	MGET(mopt, M_WAIT, MT_SOOPTS);
	mopt->m_len = sizeof(int);
	ip = mtod(mopt, int *);
	*ip = MAXTTL;
	error = sosetopt(so, IPPROTO_IP, IP_TTL, mopt);
	m_freem(mopt);
	if (error) {
		printf("%s: sosetopt error %d\n",
		    __func__, error);
		goto close;
	}

	MGET(mopt, M_WAIT, MT_SOOPTS);
	mopt->m_len = sizeof(int);
	ip = mtod(mopt, int *);
	*ip = IPTOS_PREC_INTERNETCONTROL;
	error = sosetopt(so, IPPROTO_IP, IP_TOS, mopt);
	m_freem(mopt);
	if (error) {
		printf("%s: sosetopt error %d\n",
		    __func__, error);
		goto close;
	}

	MGET(m, M_WAIT, MT_SONAME);
	m->m_len = src->sa_len;
	sa = mtod(m, struct sockaddr *);
	memcpy(sa, src, src->sa_len);
	switch(sa->sa_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)sa;
		sin->sin_port = 0;
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)sa;
		sin6->sin6_port = 0;
		break;
	default:
		break;
	}

	solock(so);
	error = sobind(so, m, p);
	sounlock(so);
	if (error) {
		printf("%s: sobind error %d\n",
		    __func__, error);
		goto close;
	}

	memcpy(sa, dst, dst->sa_len);
	switch(sa->sa_family) {
	case AF_INET:
		sin = (struct sockaddr_in *)sa;
		sin->sin_port = ntohs(port);
		break;
	case AF_INET6:
		sin6 = (struct sockaddr_in6 *)sa;
		sin6->sin6_port = ntohs(port);
		break;
	default:
		break;
	}

	solock(so);
	error = soconnect(so, m);
	sounlock(so);
	if (error && error != ECONNREFUSED) {
		printf("%s: soconnect error %d\n",
		    __func__, error);
		goto close;
	}

	m_free(m);

	return (so);

 close:
	m_free(m);
	soclose(so, MSG_DONTWAIT);

	return (NULL);
}

/*
 * Will be called per-received packet
 */
void
bfd_upcall(struct socket *so, caddr_t arg, int waitflag)
{
	struct bfd_config *bfd = (struct bfd_config *)arg;

	bfd->bc_upcallso = so;
	task_add(bfdtq, &bfd->bc_upcall_task);	
}

void
bfd_upcall_task(void *arg)
{
	struct bfd_config	*bfd = (struct bfd_config *)arg;
	struct socket		*so = bfd->bc_upcallso;
	struct mbuf		*m;
	struct uio		 uio;
	int			 flags, error;

	uio.uio_procp = NULL;
	do {
		uio.uio_resid = so->so_rcv.sb_cc;
		flags = MSG_DONTWAIT;
		error = soreceive(so, NULL, &uio, &m, NULL, &flags, 0);
		if (error && error != EAGAIN) {
			bfd_error(bfd);
			return;
		}
		if (m != NULL)
			bfd_input(bfd, m);
	} while (so->so_rcv.sb_cc);

	bfd->bc_upcallso = NULL;

	return;
}

void
bfd_error(struct bfd_config *bfd)
{
	if (bfd->bc_state <= BFD_STATE_DOWN)
		return;

	if (++bfd->bc_error >= bfd->bc_neighbor->bn_mult) {
		bfd->bc_neighbor->bn_ldiag = BFD_DIAG_EXPIRED;
		bfd_reset(bfd);
		if (bfd->bc_state > BFD_STATE_DOWN)
			bfd_set_state(bfd, BFD_STATE_DOWN);
	}
}

void
bfd_timeout_tx(void *v)
{
	struct bfd_config *bfd = v;
	task_add(bfdtq, &bfd->bc_bfd_send_task);
}

/*
 * Triggers when we do not receive a valid packet in time
 */
void
bfd_timeout_rx(void *v)
{
	struct bfd_config *bfd = v;

	if (bfd->bc_state > BFD_STATE_DOWN) {
		bfd_error(bfd);
		rtm_bfd(bfd);
	}

	timeout_add_usec(&bfd->bc_timo_rx, bfd->bc_minrx);
}

/*
 * Tell our neighbor that we are going down
 */
void
bfd_senddown(struct bfd_config *bfd)
{
	/* If we are down, return early */
	if (bfd->bc_state < BFD_STATE_INIT)
		return;

	if (bfd->bc_neighbor->bn_ldiag == 0)
		bfd->bc_neighbor->bn_ldiag = BFD_DIAG_ADMIN_DOWN;

	bfd_set_state(bfd, BFD_STATE_ADMINDOWN);
	bfd_send_control(bfd);

	return;
}

/*
 * Clean a BFD peer to defaults
 */
void
bfd_reset(struct bfd_config *bfd)
{
	/* Clean */
	bfd->bc_neighbor->bn_rdiscr = 0;
	bfd->bc_neighbor->bn_demand = 0;
	bfd->bc_neighbor->bn_rdemand = 0;
	bfd->bc_neighbor->bn_authtype = 0;
	bfd->bc_neighbor->bn_rauthseq = 0;
	bfd->bc_neighbor->bn_lauthseq = 0;
	bfd->bc_neighbor->bn_authseqknown = 0;
	bfd->bc_neighbor->bn_ldiag = 0;

	bfd->bc_mode = BFD_MODE_ASYNC;
	bfd->bc_state = BFD_STATE_DOWN;

	/* rfc5880 6.8.18 */
	bfd->bc_neighbor->bn_lstate = BFD_STATE_DOWN;
	bfd->bc_neighbor->bn_rstate = BFD_STATE_DOWN;
	bfd->bc_neighbor->bn_mintx = BFD_SECOND;
	bfd->bc_neighbor->bn_req_minrx = BFD_SECOND;
	bfd->bc_neighbor->bn_rminrx = 1;
	bfd->bc_neighbor->bn_mult = 3;

	bfd->bc_mintx = bfd->bc_neighbor->bn_mintx;
	bfd->bc_minrx = bfd->bc_neighbor->bn_req_minrx;
	bfd->bc_multiplier = bfd->bc_neighbor->bn_mult;
	bfd->bc_minecho = 0;	//XXX - BFD_SECOND;

	bfd_set_uptime(bfd);

	return;
}

void
bfd_input(struct bfd_config *bfd, struct mbuf *m)
{
	struct bfd_header	*peer;
	struct bfd_auth_header	*auth;
	struct mbuf		*mp, *mp0;
	unsigned int		 ver, diag = BFD_DIAG_NONE, state, flags;
	int			 offp;

	mp = m_pulldown(m, 0, sizeof(*peer), &offp);

	if (mp == NULL)
		return;
	peer = (struct bfd_header *)(mp->m_data + offp);

	/* We only support BFD Version 1 */
	if (( ver = BFD_VER(peer->bfd_ver_diag)) != 1)
		goto discard;

	diag = BFD_DIAG(peer->bfd_ver_diag);
	state = BFD_STATE(peer->bfd_sta_flags);
	flags = BFD_FLAGS(peer->bfd_sta_flags);

	if (peer->bfd_length + offp > mp->m_len) {
		printf("%s: bad len %d != %d\n", __func__,
		    peer->bfd_length + offp, mp->m_len);
		goto discard;
	}

	if (peer->bfd_detect_multi == 0)
		goto discard;
	if (flags & BFD_FLAG_M)
		goto discard;
	if (ntohl(peer->bfd_my_discriminator) == 0)
		goto discard;
	if (ntohl(peer->bfd_your_discriminator) == 0 &&
	    BFD_STATE(peer->bfd_sta_flags) > BFD_STATE_DOWN)
		goto discard;
	if ((ntohl(peer->bfd_your_discriminator) != 0) &&
	    (ntohl(peer->bfd_your_discriminator) !=
	    bfd->bc_neighbor->bn_ldiscr)) {
		bfd_error(bfd);
		goto discard;
	}

	if ((flags & BFD_FLAG_A) && bfd->bc_neighbor->bn_authtype == 0)
		goto discard;
	if (!(flags & BFD_FLAG_A) && bfd->bc_neighbor->bn_authtype != 0)
		goto discard;
	if (flags & BFD_FLAG_A) {
		mp0 = m_pulldown(mp, 0, sizeof(*auth), &offp);
		if (mp0 == NULL)
			goto discard;
		auth = (struct bfd_auth_header *)(mp0->m_data + offp);
#if 0
		if (bfd_process_auth(bfd, auth) != 0) {
			m_free(mp0);
			goto discard;
		}
#endif
	}

	bfd->bc_neighbor->bn_rdiscr = ntohl(peer->bfd_my_discriminator);
	bfd->bc_neighbor->bn_rstate = state;
	bfd->bc_neighbor->bn_rdemand = (flags & BFD_FLAG_D);
	bfd->bc_poll = (flags & BFD_FLAG_F);

	/* Local change to the algorithm, we don't accept below 50ms */
	if (ntohl(peer->bfd_required_min_rx_interval) < BFD_MINIMUM)
		goto discard;
	/*
	 * Local change to the algorithm, we can't use larger than signed
	 * 32bits for a timeout.
	 * That is Too Long(tm) anyways.
	 */
	if (ntohl(peer->bfd_required_min_rx_interval) > INT32_MAX)
		goto discard;
	bfd->bc_neighbor->bn_rminrx =
	    ntohl(peer->bfd_required_min_rx_interval);
	bfd->bc_minrx = bfd->bc_neighbor->bn_req_minrx;

	bfd->bc_neighbor->bn_mintx =
	    htonl(peer->bfd_desired_min_tx_interval);
	if (bfd->bc_neighbor->bn_lstate != BFD_STATE_UP)
		bfd->bc_neighbor->bn_mintx = BFD_SECOND;

	bfd->bc_neighbor->bn_req_minrx =
	    ntohl(peer->bfd_required_min_rx_interval);

	/* rfc5880 6.8.7 */
	bfd->bc_mintx = max(bfd->bc_neighbor->bn_rminrx,
	    bfd->bc_neighbor->bn_mintx);

	/* According the to pseudo-code RFC 5880 page 34 */
	if (bfd->bc_state == BFD_STATE_ADMINDOWN)
		goto discard;
	if (bfd->bc_neighbor->bn_rstate == BFD_STATE_ADMINDOWN) {
		if (bfd->bc_neighbor->bn_lstate != BFD_STATE_DOWN) {
			bfd->bc_neighbor->bn_ldiag = BFD_DIAG_NEIGHBOR_SIGDOWN;
			bfd_set_state(bfd, BFD_STATE_DOWN);
		}
	} else if (bfd->bc_neighbor->bn_lstate == BFD_STATE_DOWN) {
		if (bfd->bc_neighbor->bn_rstate == BFD_STATE_DOWN)
			bfd_set_state(bfd, BFD_STATE_INIT);
		else if (bfd->bc_neighbor->bn_rstate == BFD_STATE_INIT) {
			bfd->bc_neighbor->bn_ldiag = 0;
			bfd_set_state(bfd, BFD_STATE_UP);
		}
	} else if (bfd->bc_neighbor->bn_lstate == BFD_STATE_INIT) {
		if (bfd->bc_neighbor->bn_rstate >= BFD_STATE_INIT) {
			bfd->bc_neighbor->bn_ldiag = 0;
			bfd_set_state(bfd, BFD_STATE_UP);
		} else {
			goto discard;
		}
	} else {
		if (bfd->bc_neighbor->bn_rstate == BFD_STATE_DOWN) {
			bfd->bc_neighbor->bn_ldiag = BFD_DIAG_NEIGHBOR_SIGDOWN;
			bfd_set_state(bfd, BFD_STATE_DOWN);
			goto discard;
		}
	}

	if (bfd->bc_neighbor->bn_lstate == BFD_STATE_UP) {
		bfd->bc_neighbor->bn_ldiag = 0;
		bfd->bc_neighbor->bn_demand = 1;
		bfd->bc_neighbor->bn_rdemand = (flags & BFD_FLAG_D);
	}

	bfd->bc_error = 0;

 discard:
	bfd->bc_neighbor->bn_rdiag = diag;
	m_free(m);

	timeout_add_usec(&bfd->bc_timo_rx, bfd->bc_minrx);

	return;
}

void
bfd_set_state(struct bfd_config *bfd, unsigned int state)
{
	struct ifnet	*ifp;
	struct rtentry	*rt = bfd->bc_rt;

	ifp = if_get(rt->rt_ifidx);
	if (ifp == NULL) {
		printf("%s: cannot find interface index %u\n",
		    __func__, rt->rt_ifidx);
		bfd->bc_error++;
		bfd_reset(bfd);
		return;
	}

	bfd->bc_neighbor->bn_lstate = state;
	if (state > BFD_STATE_ADMINDOWN)
		bfd->bc_neighbor->bn_ldiag = 0;

	if (!rtisvalid(rt))
		bfd->bc_neighbor->bn_lstate = BFD_STATE_DOWN;

	switch (state) {
	case BFD_STATE_ADMINDOWN:
		bfd->bc_laststate = bfd->bc_state;
	/* FALLTHROUGH */
	case BFD_STATE_DOWN:
		if (bfd->bc_state == BFD_STATE_UP) {
			bfd->bc_laststate = bfd->bc_state;
			bfd_set_uptime(bfd);
		}
		break;
	case BFD_STATE_INIT:
		bfd->bc_laststate = bfd->bc_state;
		break;
	case BFD_STATE_UP:
		bfd->bc_laststate =
		    bfd->bc_state == BFD_STATE_INIT ?
		    bfd->bc_laststate : bfd->bc_state;
		bfd_set_uptime(bfd);
		break;
	}

	bfd->bc_state = state;
	rtm_bfd(bfd);
	if_put(ifp);

	return;
}

void
bfd_set_uptime(struct bfd_config *bfd)
{
	struct timeval tv;

	getmicrotime(&tv);
	bfd->bc_lastuptime = tv.tv_sec - bfd->bc_time->tv_sec;
	memcpy(bfd->bc_time, &tv, sizeof(tv));
}

void
bfd_send_control(void *x)
{
	struct bfd_config	*bfd = x;
	struct mbuf		*m;
	struct bfd_header	*h;
	int error, len;

	MGETHDR(m, M_WAIT, MT_DATA);
	MCLGET(m, M_WAIT);

	len = BFD_HDRLEN;
	m->m_len = m->m_pkthdr.len = len;
	h = mtod(m, struct bfd_header *);

	memset(h, 0xff, sizeof(*h));	/* canary */

	h->bfd_ver_diag = ((BFD_VERSION << 5) | (bfd->bc_neighbor->bn_ldiag));
	h->bfd_sta_flags = (bfd->bc_state << 6);
	h->bfd_detect_multi = bfd->bc_neighbor->bn_mult;
	h->bfd_length = BFD_HDRLEN;
	h->bfd_my_discriminator = htonl(bfd->bc_neighbor->bn_ldiscr);
	h->bfd_your_discriminator = htonl(bfd->bc_neighbor->bn_rdiscr);

	h->bfd_desired_min_tx_interval =
	    htonl(bfd->bc_neighbor->bn_mintx);
	h->bfd_required_min_rx_interval =
	    htonl(bfd->bc_neighbor->bn_req_minrx);
	h->bfd_required_min_echo_interval = htonl(bfd->bc_minecho);

	error = bfd_send(bfd, m);

	if (error) {
		bfd_error(bfd);
		if (!(error == EHOSTDOWN || error == ECONNREFUSED)) {
			printf("%s: %u\n", __func__, error);
		}
	}
}

int
bfd_send(struct bfd_config *bfd, struct mbuf *m)
{
	return(sosend(bfd->bc_sosend, NULL, NULL, m, NULL, MSG_DONTWAIT));
}

/*
 * Print debug information about this bfd instance
 */
void
bfd_debug(struct bfd_config *bfd)
{
	struct rtentry	*rt = bfd->bc_rt;
	struct timeval	 tv;
	char buf[64];

	printf("dest: %s ", sockaddr_ntop(rt_key(rt), buf, sizeof(buf)));
	printf("src: %s ", sockaddr_ntop(rt->rt_ifa->ifa_addr, buf,
	    sizeof(buf)));
	printf("\n");
	printf("\t");
	printf("session state: %u ", bfd->bc_state);
	printf("mode: %u ", bfd->bc_mode);
	printf("error: %u ", bfd->bc_error);
	printf("minrx: %u ", bfd->bc_minrx);
	printf("mintx: %u ", bfd->bc_mintx);
	printf("multiplier: %u ", bfd->bc_multiplier);
	printf("\n");
	printf("\t");
	printf("local session state: %u ", bfd->bc_neighbor->bn_lstate);
	printf("local diag: %u ", bfd->bc_neighbor->bn_ldiag);
	printf("\n");
	printf("\t");
	printf("remote discriminator: %u ", bfd->bc_neighbor->bn_rdiscr);
	printf("local discriminator: %u ", bfd->bc_neighbor->bn_ldiscr);
	printf("\n");
	printf("\t");
	printf("remote session state: %u ", bfd->bc_neighbor->bn_rstate);
	printf("remote diag: %u ", bfd->bc_neighbor->bn_rdiag);
	printf("remote min rx: %u ", bfd->bc_neighbor->bn_rminrx);
	printf("\n");
	printf("\t");
	printf("last state: %u ", bfd->bc_laststate);
	getmicrotime(&tv);
	printf("uptime %llds ", tv.tv_sec - bfd->bc_time->tv_sec);
	printf("time started %lld.%06ld ", bfd->bc_time->tv_sec,
	    bfd->bc_time->tv_usec);
	printf("last uptime %llds ", bfd->bc_lastuptime);
	printf("\n");
}
