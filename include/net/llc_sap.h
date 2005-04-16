#ifndef LLC_SAP_H
#define LLC_SAP_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
struct llc_sap;
struct sk_buff;

extern void llc_sap_rtn_pdu(struct llc_sap *sap, struct sk_buff *skb);
extern void llc_save_primitive(struct sk_buff* skb, unsigned char prim);
extern struct sk_buff *llc_alloc_frame(void);

extern void llc_build_and_send_test_pkt(struct llc_sap *sap,
				        struct sk_buff *skb,
					unsigned char *dmac,
					unsigned char dsap);
extern void llc_build_and_send_xid_pkt(struct llc_sap *sap,
				       struct sk_buff *skb,
				       unsigned char *dmac,
				       unsigned char dsap);
#endif /* LLC_SAP_H */
