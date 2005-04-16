/* IRC extension for IP connection tracking.
 * (C) 2000 by Harald Welte <laforge@gnumonks.org>
 * based on RR's ip_conntrack_ftp.h
 *
 * ip_conntrack_irc.h,v 1.6 2000/11/07 18:26:42 laforge Exp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *
 */
#ifndef _IP_CONNTRACK_IRC_H
#define _IP_CONNTRACK_IRC_H

/* This structure exists only once per master */
struct ip_ct_irc_master {
};

#ifdef __KERNEL__
extern unsigned int (*ip_nat_irc_hook)(struct sk_buff **pskb,
				       enum ip_conntrack_info ctinfo,
				       unsigned int matchoff,
				       unsigned int matchlen,
				       struct ip_conntrack_expect *exp);

#define IRC_PORT	6667

#endif /* __KERNEL__ */

#endif /* _IP_CONNTRACK_IRC_H */
