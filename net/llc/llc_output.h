#ifndef LLC_OUTPUT_H
#define LLC_OUTPUT_H
/*
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License version 2 for more details.
 */

struct sk_buff;

int llc_mac_hdr_init(struct sk_buff *skb, unsigned char *sa, unsigned char *da);

#endif /* LLC_OUTPUT_H */
