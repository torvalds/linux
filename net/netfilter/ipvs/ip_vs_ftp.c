// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ip_vs_ftp.c: IPVS ftp application module
 *
 * Authors:	Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 * Changes:
 *
 * Most code here is taken from ip_masq_ftp.c in kernel 2.2. The difference
 * is that ip_vs_ftp module handles the reverse direction to ip_masq_ftp.
 *
 *		IP_MASQ_FTP ftp masquerading module
 *
 * Version:	@(#)ip_masq_ftp.c 0.04   02/05/96
 *
 * Author:	Wouter Gadeyne
 */

#define KMSG_COMPONENT "IPVS"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/ctype.h>
#include <linux/inet.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_expect.h>
#include <net/netfilter/nf_nat.h>
#include <net/netfilter/nf_nat_helper.h>
#include <linux/gfp.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <asm/unaligned.h>

#include <net/ip_vs.h>


#define SERVER_STRING_PASV "227 "
#define CLIENT_STRING_PORT "PORT"
#define SERVER_STRING_EPSV "229 "
#define CLIENT_STRING_EPRT "EPRT"

enum {
	IP_VS_FTP_ACTIVE = 0,
	IP_VS_FTP_PORT = 0,
	IP_VS_FTP_PASV,
	IP_VS_FTP_EPRT,
	IP_VS_FTP_EPSV,
};

/*
 * List of ports (up to IP_VS_APP_MAX_PORTS) to be handled by helper
 * First port is set to the default port.
 */
static unsigned int ports_count = 1;
static unsigned short ports[IP_VS_APP_MAX_PORTS] = {21, 0};
module_param_array(ports, ushort, &ports_count, 0444);
MODULE_PARM_DESC(ports, "Ports to monitor for FTP control commands");


static char *ip_vs_ftp_data_ptr(struct sk_buff *skb, struct ip_vs_iphdr *ipvsh)
{
	struct tcphdr *th = (struct tcphdr *)((char *)skb->data + ipvsh->len);

	if ((th->doff << 2) < sizeof(struct tcphdr))
		return NULL;

	return (char *)th + (th->doff << 2);
}

static int
ip_vs_ftp_init_conn(struct ip_vs_app *app, struct ip_vs_conn *cp)
{
	/* We use connection tracking for the command connection */
	cp->flags |= IP_VS_CONN_F_NFCT;
	return 0;
}


static int
ip_vs_ftp_done_conn(struct ip_vs_app *app, struct ip_vs_conn *cp)
{
	return 0;
}


/* Get <addr,port> from the string "xxx.xxx.xxx.xxx,ppp,ppp", started
 * with the "pattern". <addr,port> is in network order.
 * Parse extended format depending on ext. In this case addr can be pre-set.
 */
static int ip_vs_ftp_get_addrport(char *data, char *data_limit,
				  const char *pattern, size_t plen,
				  char skip, bool ext, int mode,
				  union nf_inet_addr *addr, __be16 *port,
				  __u16 af, char **start, char **end)
{
	char *s, c;
	unsigned char p[6];
	char edelim;
	__u16 hport;
	int i = 0;

	if (data_limit - data < plen) {
		/* check if there is partial match */
		if (strncasecmp(data, pattern, data_limit - data) == 0)
			return -1;
		else
			return 0;
	}

	if (strncasecmp(data, pattern, plen) != 0) {
		return 0;
	}
	s = data + plen;
	if (skip) {
		bool found = false;

		for (;; s++) {
			if (s == data_limit)
				return -1;
			if (!found) {
				/* "(" is optional for non-extended format,
				 * so catch the start of IPv4 address
				 */
				if (!ext && isdigit(*s))
					break;
				if (*s == skip)
					found = true;
			} else if (*s != skip) {
				break;
			}
		}
	}
	/* Old IPv4-only format? */
	if (!ext) {
		p[0] = 0;
		for (data = s; ; data++) {
			if (data == data_limit)
				return -1;
			c = *data;
			if (isdigit(c)) {
				p[i] = p[i]*10 + c - '0';
			} else if (c == ',' && i < 5) {
				i++;
				p[i] = 0;
			} else {
				/* unexpected character or terminator */
				break;
			}
		}

		if (i != 5)
			return -1;

		*start = s;
		*end = data;
		addr->ip = get_unaligned((__be32 *) p);
		*port = get_unaligned((__be16 *) (p + 4));
		return 1;
	}
	if (s == data_limit)
		return -1;
	*start = s;
	edelim = *s++;
	if (edelim < 33 || edelim > 126)
		return -1;
	if (s == data_limit)
		return -1;
	if (*s == edelim) {
		/* Address family is usually missing for EPSV response */
		if (mode != IP_VS_FTP_EPSV)
			return -1;
		s++;
		if (s == data_limit)
			return -1;
		/* Then address should be missing too */
		if (*s != edelim)
			return -1;
		/* Caller can pre-set addr, if needed */
		s++;
	} else {
		const char *ep;

		/* We allow address only from same family */
		if (af == AF_INET6 && *s != '2')
			return -1;
		if (af == AF_INET && *s != '1')
			return -1;
		s++;
		if (s == data_limit)
			return -1;
		if (*s != edelim)
			return -1;
		s++;
		if (s == data_limit)
			return -1;
		if (af == AF_INET6) {
			if (in6_pton(s, data_limit - s, (u8 *)addr, edelim,
				     &ep) <= 0)
				return -1;
		} else {
			if (in4_pton(s, data_limit - s, (u8 *)addr, edelim,
				     &ep) <= 0)
				return -1;
		}
		s = (char *) ep;
		if (s == data_limit)
			return -1;
		if (*s != edelim)
			return -1;
		s++;
	}
	for (hport = 0; ; s++)
	{
		if (s == data_limit)
			return -1;
		if (!isdigit(*s))
			break;
		hport = hport * 10 + *s - '0';
	}
	if (s == data_limit || !hport || *s != edelim)
		return -1;
	s++;
	*end = s;
	*port = htons(hport);
	return 1;
}

/* Look at outgoing ftp packets to catch the response to a PASV/EPSV command
 * from the server (inside-to-outside).
 * When we see one, we build a connection entry with the client address,
 * client port 0 (unknown at the moment), the server address and the
 * server port.  Mark the current connection entry as a control channel
 * of the new entry. All this work is just to make the data connection
 * can be scheduled to the right server later.
 *
 * The outgoing packet should be something like
 *   "227 Entering Passive Mode (xxx,xxx,xxx,xxx,ppp,ppp)".
 * xxx,xxx,xxx,xxx is the server address, ppp,ppp is the server port number.
 * The extended format for EPSV response provides usually only port:
 *   "229 Entering Extended Passive Mode (|||ppp|)"
 */
static int ip_vs_ftp_out(struct ip_vs_app *app, struct ip_vs_conn *cp,
			 struct sk_buff *skb, int *diff,
			 struct ip_vs_iphdr *ipvsh)
{
	char *data, *data_limit;
	char *start, *end;
	union nf_inet_addr from;
	__be16 port;
	struct ip_vs_conn *n_cp;
	char buf[24];		/* xxx.xxx.xxx.xxx,ppp,ppp\000 */
	unsigned int buf_len;
	int ret = 0;
	enum ip_conntrack_info ctinfo;
	struct nf_conn *ct;

	*diff = 0;

	/* Only useful for established sessions */
	if (cp->state != IP_VS_TCP_S_ESTABLISHED)
		return 1;

	/* Linear packets are much easier to deal with. */
	if (!skb_make_writable(skb, skb->len))
		return 0;

	if (cp->app_data == (void *) IP_VS_FTP_PASV) {
		data = ip_vs_ftp_data_ptr(skb, ipvsh);
		data_limit = skb_tail_pointer(skb);

		if (!data || data >= data_limit)
			return 1;

		if (ip_vs_ftp_get_addrport(data, data_limit,
					   SERVER_STRING_PASV,
					   sizeof(SERVER_STRING_PASV)-1,
					   '(', false, IP_VS_FTP_PASV,
					   &from, &port, cp->af,
					   &start, &end) != 1)
			return 1;

		IP_VS_DBG(7, "PASV response (%pI4:%u) -> %pI4:%u detected\n",
			  &from.ip, ntohs(port), &cp->caddr.ip, 0);
	} else if (cp->app_data == (void *) IP_VS_FTP_EPSV) {
		data = ip_vs_ftp_data_ptr(skb, ipvsh);
		data_limit = skb_tail_pointer(skb);

		if (!data || data >= data_limit)
			return 1;

		/* Usually, data address is not specified but
		 * we support different address, so pre-set it.
		 */
		from = cp->daddr;
		if (ip_vs_ftp_get_addrport(data, data_limit,
					   SERVER_STRING_EPSV,
					   sizeof(SERVER_STRING_EPSV)-1,
					   '(', true, IP_VS_FTP_EPSV,
					   &from, &port, cp->af,
					   &start, &end) != 1)
			return 1;

		IP_VS_DBG_BUF(7, "EPSV response (%s:%u) -> %s:%u detected\n",
			      IP_VS_DBG_ADDR(cp->af, &from), ntohs(port),
			      IP_VS_DBG_ADDR(cp->af, &cp->caddr), 0);
	} else {
		return 1;
	}

	/* Now update or create a connection entry for it */
	{
		struct ip_vs_conn_param p;

		ip_vs_conn_fill_param(cp->ipvs, cp->af,
				      ipvsh->protocol, &from, port,
				      &cp->caddr, 0, &p);
		n_cp = ip_vs_conn_out_get(&p);
	}
	if (!n_cp) {
		struct ip_vs_conn_param p;

		ip_vs_conn_fill_param(cp->ipvs,
				      cp->af, ipvsh->protocol, &cp->caddr,
				      0, &cp->vaddr, port, &p);
		n_cp = ip_vs_conn_new(&p, cp->af, &from, port,
				      IP_VS_CONN_F_NO_CPORT |
				      IP_VS_CONN_F_NFCT,
				      cp->dest, skb->mark);
		if (!n_cp)
			return 0;

		/* add its controller */
		ip_vs_control_add(n_cp, cp);
	}

	/* Replace the old passive address with the new one */
	if (cp->app_data == (void *) IP_VS_FTP_PASV) {
		from.ip = n_cp->vaddr.ip;
		port = n_cp->vport;
		snprintf(buf, sizeof(buf), "%u,%u,%u,%u,%u,%u",
			 ((unsigned char *)&from.ip)[0],
			 ((unsigned char *)&from.ip)[1],
			 ((unsigned char *)&from.ip)[2],
			 ((unsigned char *)&from.ip)[3],
			 ntohs(port) >> 8,
			 ntohs(port) & 0xFF);
	} else if (cp->app_data == (void *) IP_VS_FTP_EPSV) {
		from = n_cp->vaddr;
		port = n_cp->vport;
		/* Only port, client will use VIP for the data connection */
		snprintf(buf, sizeof(buf), "|||%u|",
			 ntohs(port));
	} else {
		*buf = 0;
	}
	buf_len = strlen(buf);

	ct = nf_ct_get(skb, &ctinfo);
	if (ct) {
		bool mangled;

		/* If mangling fails this function will return 0
		 * which will cause the packet to be dropped.
		 * Mangling can only fail under memory pressure,
		 * hopefully it will succeed on the retransmitted
		 * packet.
		 */
		mangled = nf_nat_mangle_tcp_packet(skb, ct, ctinfo,
						   ipvsh->len,
						   start - data,
						   end - start,
						   buf, buf_len);
		if (mangled) {
			ip_vs_nfct_expect_related(skb, ct, n_cp,
						  ipvsh->protocol, 0, 0);
			if (skb->ip_summed == CHECKSUM_COMPLETE)
				skb->ip_summed = CHECKSUM_UNNECESSARY;
			/* csum is updated */
			ret = 1;
		}
	}

	/* Not setting 'diff' is intentional, otherwise the sequence
	 * would be adjusted twice.
	 */

	cp->app_data = (void *) IP_VS_FTP_ACTIVE;
	ip_vs_tcp_conn_listen(n_cp);
	ip_vs_conn_put(n_cp);
	return ret;
}


/* Look at incoming ftp packets to catch the PASV/PORT/EPRT/EPSV command
 * (outside-to-inside).
 *
 * The incoming packet having the PORT command should be something like
 *      "PORT xxx,xxx,xxx,xxx,ppp,ppp\n".
 * xxx,xxx,xxx,xxx is the client address, ppp,ppp is the client port number.
 * In this case, we create a connection entry using the client address and
 * port, so that the active ftp data connection from the server can reach
 * the client.
 * Extended format:
 *	"EPSV\r\n" when client requests server address from same family
 *	"EPSV 1\r\n" when client requests IPv4 server address
 *	"EPSV 2\r\n" when client requests IPv6 server address
 *	"EPSV ALL\r\n" - not supported
 *	EPRT with specified delimiter (ASCII 33..126), "|" by default:
 *	"EPRT |1|IPv4ADDR|PORT|\r\n" when client provides IPv4 addrport
 *	"EPRT |2|IPv6ADDR|PORT|\r\n" when client provides IPv6 addrport
 */
static int ip_vs_ftp_in(struct ip_vs_app *app, struct ip_vs_conn *cp,
			struct sk_buff *skb, int *diff,
			struct ip_vs_iphdr *ipvsh)
{
	char *data, *data_start, *data_limit;
	char *start, *end;
	union nf_inet_addr to;
	__be16 port;
	struct ip_vs_conn *n_cp;

	/* no diff required for incoming packets */
	*diff = 0;

	/* Only useful for established sessions */
	if (cp->state != IP_VS_TCP_S_ESTABLISHED)
		return 1;

	/* Linear packets are much easier to deal with. */
	if (!skb_make_writable(skb, skb->len))
		return 0;

	data = data_start = ip_vs_ftp_data_ptr(skb, ipvsh);
	data_limit = skb_tail_pointer(skb);
	if (!data || data >= data_limit)
		return 1;

	while (data <= data_limit - 6) {
		if (cp->af == AF_INET &&
		    strncasecmp(data, "PASV\r\n", 6) == 0) {
			/* Passive mode on */
			IP_VS_DBG(7, "got PASV at %td of %td\n",
				  data - data_start,
				  data_limit - data_start);
			cp->app_data = (void *) IP_VS_FTP_PASV;
			return 1;
		}

		/* EPSV or EPSV<space><net-prt> */
		if (strncasecmp(data, "EPSV", 4) == 0 &&
		    (data[4] == ' ' || data[4] == '\r')) {
			if (data[4] == ' ') {
				char proto = data[5];

				if (data > data_limit - 7 || data[6] != '\r')
					return 1;

#ifdef CONFIG_IP_VS_IPV6
				if (cp->af == AF_INET6 && proto == '2') {
				} else
#endif
				if (cp->af == AF_INET && proto == '1') {
				} else {
					return 1;
				}
			}
			/* Extended Passive mode on */
			IP_VS_DBG(7, "got EPSV at %td of %td\n",
				  data - data_start,
				  data_limit - data_start);
			cp->app_data = (void *) IP_VS_FTP_EPSV;
			return 1;
		}

		data++;
	}

	/*
	 * To support virtual FTP server, the scenerio is as follows:
	 *       FTP client ----> Load Balancer ----> FTP server
	 * First detect the port number in the application data,
	 * then create a new connection entry for the coming data
	 * connection.
	 */
	if (cp->af == AF_INET &&
	    ip_vs_ftp_get_addrport(data_start, data_limit,
				   CLIENT_STRING_PORT,
				   sizeof(CLIENT_STRING_PORT)-1,
				   ' ', false, IP_VS_FTP_PORT,
				   &to, &port, cp->af,
				   &start, &end) == 1) {

		IP_VS_DBG(7, "PORT %pI4:%u detected\n", &to.ip, ntohs(port));

		/* Now update or create a connection entry for it */
		IP_VS_DBG(7, "protocol %s %pI4:%u %pI4:%u\n",
			  ip_vs_proto_name(ipvsh->protocol),
			  &to.ip, ntohs(port), &cp->vaddr.ip,
			  ntohs(cp->vport)-1);
	} else if (ip_vs_ftp_get_addrport(data_start, data_limit,
					  CLIENT_STRING_EPRT,
					  sizeof(CLIENT_STRING_EPRT)-1,
					  ' ', true, IP_VS_FTP_EPRT,
					  &to, &port, cp->af,
					  &start, &end) == 1) {

		IP_VS_DBG_BUF(7, "EPRT %s:%u detected\n",
			      IP_VS_DBG_ADDR(cp->af, &to), ntohs(port));

		/* Now update or create a connection entry for it */
		IP_VS_DBG_BUF(7, "protocol %s %s:%u %s:%u\n",
			      ip_vs_proto_name(ipvsh->protocol),
			      IP_VS_DBG_ADDR(cp->af, &to), ntohs(port),
			      IP_VS_DBG_ADDR(cp->af, &cp->vaddr),
			      ntohs(cp->vport)-1);
	} else {
		return 1;
	}

	/* Passive mode off */
	cp->app_data = (void *) IP_VS_FTP_ACTIVE;

	{
		struct ip_vs_conn_param p;
		ip_vs_conn_fill_param(cp->ipvs, cp->af,
				      ipvsh->protocol, &to, port, &cp->vaddr,
				      htons(ntohs(cp->vport)-1), &p);
		n_cp = ip_vs_conn_in_get(&p);
		if (!n_cp) {
			n_cp = ip_vs_conn_new(&p, cp->af, &cp->daddr,
					      htons(ntohs(cp->dport)-1),
					      IP_VS_CONN_F_NFCT, cp->dest,
					      skb->mark);
			if (!n_cp)
				return 0;

			/* add its controller */
			ip_vs_control_add(n_cp, cp);
		}
	}

	/*
	 *	Move tunnel to listen state
	 */
	ip_vs_tcp_conn_listen(n_cp);
	ip_vs_conn_put(n_cp);

	return 1;
}


static struct ip_vs_app ip_vs_ftp = {
	.name =		"ftp",
	.type =		IP_VS_APP_TYPE_FTP,
	.protocol =	IPPROTO_TCP,
	.module =	THIS_MODULE,
	.incs_list =	LIST_HEAD_INIT(ip_vs_ftp.incs_list),
	.init_conn =	ip_vs_ftp_init_conn,
	.done_conn =	ip_vs_ftp_done_conn,
	.bind_conn =	NULL,
	.unbind_conn =	NULL,
	.pkt_out =	ip_vs_ftp_out,
	.pkt_in =	ip_vs_ftp_in,
};

/*
 *	per netns ip_vs_ftp initialization
 */
static int __net_init __ip_vs_ftp_init(struct net *net)
{
	int i, ret;
	struct ip_vs_app *app;
	struct netns_ipvs *ipvs = net_ipvs(net);

	if (!ipvs)
		return -ENOENT;

	app = register_ip_vs_app(ipvs, &ip_vs_ftp);
	if (IS_ERR(app))
		return PTR_ERR(app);

	for (i = 0; i < ports_count; i++) {
		if (!ports[i])
			continue;
		ret = register_ip_vs_app_inc(ipvs, app, app->protocol, ports[i]);
		if (ret)
			goto err_unreg;
		pr_info("%s: loaded support on port[%d] = %u\n",
			app->name, i, ports[i]);
	}
	return 0;

err_unreg:
	unregister_ip_vs_app(ipvs, &ip_vs_ftp);
	return ret;
}
/*
 *	netns exit
 */
static void __ip_vs_ftp_exit(struct net *net)
{
	struct netns_ipvs *ipvs = net_ipvs(net);

	if (!ipvs)
		return;

	unregister_ip_vs_app(ipvs, &ip_vs_ftp);
}

static struct pernet_operations ip_vs_ftp_ops = {
	.init = __ip_vs_ftp_init,
	.exit = __ip_vs_ftp_exit,
};

static int __init ip_vs_ftp_init(void)
{
	/* rcu_barrier() is called by netns on error */
	return register_pernet_subsys(&ip_vs_ftp_ops);
}

/*
 *	ip_vs_ftp finish.
 */
static void __exit ip_vs_ftp_exit(void)
{
	unregister_pernet_subsys(&ip_vs_ftp_ops);
	/* rcu_barrier() is called by netns */
}


module_init(ip_vs_ftp_init);
module_exit(ip_vs_ftp_exit);
MODULE_LICENSE("GPL");
