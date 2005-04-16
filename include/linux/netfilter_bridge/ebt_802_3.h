#ifndef __LINUX_BRIDGE_EBT_802_3_H
#define __LINUX_BRIDGE_EBT_802_3_H

#define EBT_802_3_SAP 0x01
#define EBT_802_3_TYPE 0x02

#define EBT_802_3_MATCH "802_3"

/*
 * If frame has DSAP/SSAP value 0xaa you must check the SNAP type
 * to discover what kind of packet we're carrying. 
 */
#define CHECK_TYPE 0xaa

/*
 * Control field may be one or two bytes.  If the first byte has
 * the value 0x03 then the entire length is one byte, otherwise it is two.
 * One byte controls are used in Unnumbered Information frames.
 * Two byte controls are used in Numbered Information frames.
 */
#define IS_UI 0x03

#define EBT_802_3_MASK (EBT_802_3_SAP | EBT_802_3_TYPE | EBT_802_3)

/* ui has one byte ctrl, ni has two */
struct hdr_ui {
	uint8_t dsap;
	uint8_t ssap;
	uint8_t ctrl;
	uint8_t orig[3];
	uint16_t type;
};

struct hdr_ni {
	uint8_t dsap;
	uint8_t ssap;
	uint16_t ctrl;
	uint8_t  orig[3];
	uint16_t type;
};

struct ebt_802_3_hdr {
	uint8_t  daddr[6];
	uint8_t  saddr[6];
	uint16_t len;
	union {
		struct hdr_ui ui;
		struct hdr_ni ni;
	} llc;
};

#ifdef __KERNEL__
#include <linux/skbuff.h>

static inline struct ebt_802_3_hdr *ebt_802_3_hdr(const struct sk_buff *skb)
{
	return (struct ebt_802_3_hdr *)skb->mac.raw;
}
#endif

struct ebt_802_3_info 
{
	uint8_t  sap;
	uint16_t type;
	uint8_t  bitmask;
	uint8_t  invflags;
};

#endif
