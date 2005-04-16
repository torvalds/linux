#ifndef LLC_H
#define LLC_H
/*
 * Copyright (c) 1997 by Procom Technology, Inc.
 * 		 2001-2003 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */

#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/list.h>
#include <linux/spinlock.h>

struct net_device;
struct packet_type;
struct sk_buff;

struct llc_addr {
	unsigned char lsap;
	unsigned char mac[IFHWADDRLEN];
};

#define LLC_SAP_STATE_INACTIVE	1
#define LLC_SAP_STATE_ACTIVE	2

/**
 * struct llc_sap - Defines the SAP component
 *
 * @station - station this sap belongs to
 * @state - sap state
 * @p_bit - only lowest-order bit used
 * @f_bit - only lowest-order bit used
 * @laddr - SAP value in this 'lsap'
 * @node - entry in station sap_list
 * @sk_list - LLC sockets this one manages
 */
struct llc_sap {
	unsigned char	 state;
	unsigned char	 p_bit;
	unsigned char	 f_bit;
	int		 (*rcv_func)(struct sk_buff *skb,
				     struct net_device *dev,
				     struct packet_type *pt);
	struct llc_addr	 laddr;
	struct list_head node;
	struct {
		rwlock_t	  lock;
		struct hlist_head list;
	} sk_list;
};

#define LLC_DEST_INVALID         0      /* Invalid LLC PDU type */
#define LLC_DEST_SAP             1      /* Type 1 goes here */
#define LLC_DEST_CONN            2      /* Type 2 goes here */

extern struct list_head llc_sap_list;
extern rwlock_t llc_sap_list_lock;
extern unsigned char llc_station_mac_sa[ETH_ALEN];

extern int llc_rcv(struct sk_buff *skb, struct net_device *dev,
		   struct packet_type *pt);

extern int llc_mac_hdr_init(struct sk_buff *skb,
			    unsigned char *sa, unsigned char *da);

extern void llc_add_pack(int type, void (*handler)(struct llc_sap *sap,
						   struct sk_buff *skb));
extern void llc_remove_pack(int type);

extern void llc_set_station_handler(void (*handler)(struct sk_buff *skb));

extern struct llc_sap *llc_sap_open(unsigned char lsap,
				    int (*rcv)(struct sk_buff *skb,
					       struct net_device *dev,
					       struct packet_type *pt));
extern void llc_sap_close(struct llc_sap *sap);

extern struct llc_sap *llc_sap_find(unsigned char sap_value);

extern int llc_build_and_send_ui_pkt(struct llc_sap *sap, struct sk_buff *skb,
				     unsigned char *dmac, unsigned char dsap);

extern int llc_station_init(void);
extern void llc_station_exit(void);

#ifdef CONFIG_PROC_FS
extern int llc_proc_init(void);
extern void llc_proc_exit(void);
#else
#define llc_proc_init()	(0)
#define llc_proc_exit()	do { } while(0)
#endif /* CONFIG_PROC_FS */
#endif /* LLC_H */
