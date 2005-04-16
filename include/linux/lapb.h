/*
 * These are the public elements of the Linux LAPB module.
 */

#ifndef	LAPB_KERNEL_H
#define	LAPB_KERNEL_H

#define	LAPB_OK			0
#define	LAPB_BADTOKEN		1
#define	LAPB_INVALUE		2
#define	LAPB_CONNECTED		3
#define	LAPB_NOTCONNECTED	4
#define	LAPB_REFUSED		5
#define	LAPB_TIMEDOUT		6
#define	LAPB_NOMEM		7

#define	LAPB_STANDARD		0x00
#define	LAPB_EXTENDED		0x01

#define	LAPB_SLP		0x00
#define	LAPB_MLP		0x02

#define	LAPB_DTE		0x00
#define	LAPB_DCE		0x04

struct lapb_register_struct {
	void (*connect_confirmation)(struct net_device *dev, int reason);
	void (*connect_indication)(struct net_device *dev, int reason);
	void (*disconnect_confirmation)(struct net_device *dev, int reason);
	void (*disconnect_indication)(struct net_device *dev, int reason);
	int  (*data_indication)(struct net_device *dev, struct sk_buff *skb);
	void (*data_transmit)(struct net_device *dev, struct sk_buff *skb);
};

struct lapb_parms_struct {
	unsigned int t1;
	unsigned int t1timer;
	unsigned int t2;
	unsigned int t2timer;
	unsigned int n2;
	unsigned int n2count;
	unsigned int window;
	unsigned int state;
	unsigned int mode;
};

extern int lapb_register(struct net_device *dev, struct lapb_register_struct *callbacks);
extern int lapb_unregister(struct net_device *dev);
extern int lapb_getparms(struct net_device *dev, struct lapb_parms_struct *parms);
extern int lapb_setparms(struct net_device *dev, struct lapb_parms_struct *parms);
extern int lapb_connect_request(struct net_device *dev);
extern int lapb_disconnect_request(struct net_device *dev);
extern int lapb_data_request(struct net_device *dev, struct sk_buff *skb);
extern int lapb_data_received(struct net_device *dev, struct sk_buff *skb);

#endif
