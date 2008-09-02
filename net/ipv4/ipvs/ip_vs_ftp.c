/*
 * ip_vs_ftp.c: IPVS ftp application module
 *
 * Authors:	Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 * Changes:
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 * Most code here is taken from ip_masq_ftp.c in kernel 2.2. The difference
 * is that ip_vs_ftp module handles the reverse direction to ip_masq_ftp.
 *
 *		IP_MASQ_FTP ftp masquerading module
 *
 * Version:	@(#)ip_masq_ftp.c 0.04   02/05/96
 *
 * Author:	Wouter Gadeyne
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <net/protocol.h>
#include <net/tcp.h>
#include <asm/unaligned.h>

#include <net/ip_vs.h>


#define SERVER_STRING "227 Entering Passive Mode ("
#define CLIENT_STRING "PORT "


/*
 * List of ports (up to IP_VS_APP_MAX_PORTS) to be handled by helper
 * First port is set to the default port.
 */
static unsigned short ports[IP_VS_APP_MAX_PORTS] = {21, 0};
module_param_array(ports, ushort, NULL, 0);
MODULE_PARM_DESC(ports, "Ports to monitor for FTP control commands");


/*	Dummy variable */
static int ip_vs_ftp_pasv;


static int
ip_vs_ftp_init_conn(struct ip_vs_app *app, struct ip_vs_conn *cp)
{
	return 0;
}


static int
ip_vs_ftp_done_conn(struct ip_vs_app *app, struct ip_vs_conn *cp)
{
	return 0;
}


/*
 * Get <addr,port> from the string "xxx.xxx.xxx.xxx,ppp,ppp", started
 * with the "pattern" and terminated with the "term" character.
 * <addr,port> is in network order.
 */
static int ip_vs_ftp_get_addrport(char *data, char *data_limit,
				  const char *pattern, size_t plen, char term,
				  __be32 *addr, __be16 *port,
				  char **start, char **end)
{
	unsigned char p[6];
	int i = 0;

	if (data_limit - data < plen) {
		/* check if there is partial match */
		if (strnicmp(data, pattern, data_limit - data) == 0)
			return -1;
		else
			return 0;
	}

	if (strnicmp(data, pattern, plen) != 0) {
		return 0;
	}
	*start = data + plen;

	for (data = *start; *data != term; data++) {
		if (data == data_limit)
			return -1;
	}
	*end = data;

	memset(p, 0, sizeof(p));
	for (data = *start; data != *end; data++) {
		if (*data >= '0' && *data <= '9') {
			p[i] = p[i]*10 + *data - '0';
		} else if (*data == ',' && i < 5) {
			i++;
		} else {
			/* unexpected character */
			return -1;
		}
	}

	if (i != 5)
		return -1;

	*addr = get_unaligned((__be32 *)p);
	*port = get_unaligned((__be16 *)(p + 4));
	return 1;
}


/*
 * Look at outgoing ftp packets to catch the response to a PASV command
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
 */
static int ip_vs_ftp_out(struct ip_vs_app *app, struct ip_vs_conn *cp,
			 struct sk_buff *skb, int *diff)
{
	struct iphdr *iph;
	struct tcphdr *th;
	char *data, *data_limit;
	char *start, *end;
	union nf_inet_addr from;
	__be16 port;
	struct ip_vs_conn *n_cp;
	char buf[24];		/* xxx.xxx.xxx.xxx,ppp,ppp\000 */
	unsigned buf_len;
	int ret;

	*diff = 0;

	/* Only useful for established sessions */
	if (cp->state != IP_VS_TCP_S_ESTABLISHED)
		return 1;

	/* Linear packets are much easier to deal with. */
	if (!skb_make_writable(skb, skb->len))
		return 0;

	if (cp->app_data == &ip_vs_ftp_pasv) {
		iph = ip_hdr(skb);
		th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);
		data = (char *)th + (th->doff << 2);
		data_limit = skb_tail_pointer(skb);

		if (ip_vs_ftp_get_addrport(data, data_limit,
					   SERVER_STRING,
					   sizeof(SERVER_STRING)-1, ')',
					   &from.ip, &port,
					   &start, &end) != 1)
			return 1;

		IP_VS_DBG(7, "PASV response (%u.%u.%u.%u:%d) -> "
			  "%u.%u.%u.%u:%d detected\n",
			  NIPQUAD(from.ip), ntohs(port),
			  NIPQUAD(cp->caddr.ip), 0);

		/*
		 * Now update or create an connection entry for it
		 */
		n_cp = ip_vs_conn_out_get(AF_INET, iph->protocol, &from, port,
					  &cp->caddr, 0);
		if (!n_cp) {
			n_cp = ip_vs_conn_new(AF_INET, IPPROTO_TCP,
					      &cp->caddr, 0,
					      &cp->vaddr, port,
					      &from, port,
					      IP_VS_CONN_F_NO_CPORT,
					      cp->dest);
			if (!n_cp)
				return 0;

			/* add its controller */
			ip_vs_control_add(n_cp, cp);
		}

		/*
		 * Replace the old passive address with the new one
		 */
		from.ip = n_cp->vaddr.ip;
		port = n_cp->vport;
		sprintf(buf, "%d,%d,%d,%d,%d,%d", NIPQUAD(from.ip),
			(ntohs(port)>>8)&255, ntohs(port)&255);
		buf_len = strlen(buf);

		/*
		 * Calculate required delta-offset to keep TCP happy
		 */
		*diff = buf_len - (end-start);

		if (*diff == 0) {
			/* simply replace it with new passive address */
			memcpy(start, buf, buf_len);
			ret = 1;
		} else {
			ret = !ip_vs_skb_replace(skb, GFP_ATOMIC, start,
					  end-start, buf, buf_len);
		}

		cp->app_data = NULL;
		ip_vs_tcp_conn_listen(n_cp);
		ip_vs_conn_put(n_cp);
		return ret;
	}
	return 1;
}


/*
 * Look at incoming ftp packets to catch the PASV/PORT command
 * (outside-to-inside).
 *
 * The incoming packet having the PORT command should be something like
 *      "PORT xxx,xxx,xxx,xxx,ppp,ppp\n".
 * xxx,xxx,xxx,xxx is the client address, ppp,ppp is the client port number.
 * In this case, we create a connection entry using the client address and
 * port, so that the active ftp data connection from the server can reach
 * the client.
 */
static int ip_vs_ftp_in(struct ip_vs_app *app, struct ip_vs_conn *cp,
			struct sk_buff *skb, int *diff)
{
	struct iphdr *iph;
	struct tcphdr *th;
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

	/*
	 * Detecting whether it is passive
	 */
	iph = ip_hdr(skb);
	th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);

	/* Since there may be OPTIONS in the TCP packet and the HLEN is
	   the length of the header in 32-bit multiples, it is accurate
	   to calculate data address by th+HLEN*4 */
	data = data_start = (char *)th + (th->doff << 2);
	data_limit = skb_tail_pointer(skb);

	while (data <= data_limit - 6) {
		if (strnicmp(data, "PASV\r\n", 6) == 0) {
			/* Passive mode on */
			IP_VS_DBG(7, "got PASV at %td of %td\n",
				  data - data_start,
				  data_limit - data_start);
			cp->app_data = &ip_vs_ftp_pasv;
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
	if (ip_vs_ftp_get_addrport(data_start, data_limit,
				   CLIENT_STRING, sizeof(CLIENT_STRING)-1,
				   '\r', &to.ip, &port,
				   &start, &end) != 1)
		return 1;

	IP_VS_DBG(7, "PORT %u.%u.%u.%u:%d detected\n",
		  NIPQUAD(to.ip), ntohs(port));

	/* Passive mode off */
	cp->app_data = NULL;

	/*
	 * Now update or create a connection entry for it
	 */
	IP_VS_DBG(7, "protocol %s %u.%u.%u.%u:%d %u.%u.%u.%u:%d\n",
		  ip_vs_proto_name(iph->protocol),
		  NIPQUAD(to.ip), ntohs(port), NIPQUAD(cp->vaddr.ip), 0);

	n_cp = ip_vs_conn_in_get(AF_INET, iph->protocol,
				 &to, port,
				 &cp->vaddr, htons(ntohs(cp->vport)-1));
	if (!n_cp) {
		n_cp = ip_vs_conn_new(AF_INET, IPPROTO_TCP,
				      &to, port,
				      &cp->vaddr, htons(ntohs(cp->vport)-1),
				      &cp->daddr, htons(ntohs(cp->dport)-1),
				      0,
				      cp->dest);
		if (!n_cp)
			return 0;

		/* add its controller */
		ip_vs_control_add(n_cp, cp);
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
 *	ip_vs_ftp initialization
 */
static int __init ip_vs_ftp_init(void)
{
	int i, ret;
	struct ip_vs_app *app = &ip_vs_ftp;

	ret = register_ip_vs_app(app);
	if (ret)
		return ret;

	for (i=0; i<IP_VS_APP_MAX_PORTS; i++) {
		if (!ports[i])
			continue;
		ret = register_ip_vs_app_inc(app, app->protocol, ports[i]);
		if (ret)
			break;
		IP_VS_INFO("%s: loaded support on port[%d] = %d\n",
			   app->name, i, ports[i]);
	}

	if (ret)
		unregister_ip_vs_app(app);

	return ret;
}


/*
 *	ip_vs_ftp finish.
 */
static void __exit ip_vs_ftp_exit(void)
{
	unregister_ip_vs_app(&ip_vs_ftp);
}


module_init(ip_vs_ftp_init);
module_exit(ip_vs_ftp_exit);
MODULE_LICENSE("GPL");
