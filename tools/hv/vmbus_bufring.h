/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef _VMBUS_BUF_H_
#define _VMBUS_BUF_H_

#include <stdbool.h>
#include <stdint.h>

#define __packed   __attribute__((__packed__))
#define unlikely(x)	__builtin_expect(!!(x), 0)

#define ICMSGHDRFLAG_TRANSACTION	1
#define ICMSGHDRFLAG_REQUEST		2
#define ICMSGHDRFLAG_RESPONSE		4

#define IC_VERSION_NEGOTIATION_MAX_VER_COUNT 100
#define ICMSG_HDR (sizeof(struct vmbuspipe_hdr) + sizeof(struct icmsg_hdr))
#define ICMSG_NEGOTIATE_PKT_SIZE(icframe_vercnt, icmsg_vercnt) \
	(ICMSG_HDR + sizeof(struct icmsg_negotiate) + \
	 (((icframe_vercnt) + (icmsg_vercnt)) * sizeof(struct ic_version)))

/*
 * Channel packets
 */

/* Channel packet flags */
#define VMBUS_CHANPKT_TYPE_INBAND	0x0006
#define VMBUS_CHANPKT_TYPE_RXBUF	0x0007
#define VMBUS_CHANPKT_TYPE_GPA		0x0009
#define VMBUS_CHANPKT_TYPE_COMP		0x000b

#define VMBUS_CHANPKT_FLAG_NONE		0
#define VMBUS_CHANPKT_FLAG_RC		0x0001  /* report completion */

#define VMBUS_CHANPKT_SIZE_SHIFT	3
#define VMBUS_CHANPKT_SIZE_ALIGN	BIT(VMBUS_CHANPKT_SIZE_SHIFT)
#define VMBUS_CHANPKT_HLEN_MIN		\
	(sizeof(struct vmbus_chanpkt_hdr) >> VMBUS_CHANPKT_SIZE_SHIFT)

/*
 * Buffer ring
 */
struct vmbus_bufring {
	volatile uint32_t windex;
	volatile uint32_t rindex;

	/*
	 * Interrupt mask {0,1}
	 *
	 * For TX bufring, host set this to 1, when it is processing
	 * the TX bufring, so that we can safely skip the TX event
	 * notification to host.
	 *
	 * For RX bufring, once this is set to 1 by us, host will not
	 * further dispatch interrupts to us, even if there are data
	 * pending on the RX bufring.  This effectively disables the
	 * interrupt of the channel to which this RX bufring is attached.
	 */
	volatile uint32_t imask;

	/*
	 * Win8 uses some of the reserved bits to implement
	 * interrupt driven flow management. On the send side
	 * we can request that the receiver interrupt the sender
	 * when the ring transitions from being full to being able
	 * to handle a message of size "pending_send_sz".
	 *
	 * Add necessary state for this enhancement.
	 */
	volatile uint32_t pending_send;
	uint32_t reserved1[12];

	union {
		struct {
			uint32_t feat_pending_send_sz:1;
		};
		uint32_t value;
	} feature_bits;

	/* Pad it to rte_mem_page_size() so that data starts on page boundary */
	uint8_t	reserved2[4028];

	/*
	 * Ring data starts here + RingDataStartOffset
	 * !!! DO NOT place any fields below this !!!
	 */
	uint8_t data[];
} __packed;

struct vmbus_br {
	struct vmbus_bufring *vbr;
	uint32_t	dsize;
	uint32_t	windex; /* next available location */
};

struct vmbus_chanpkt_hdr {
	uint16_t	type;	/* VMBUS_CHANPKT_TYPE_ */
	uint16_t	hlen;	/* header len, in 8 bytes */
	uint16_t	tlen;	/* total len, in 8 bytes */
	uint16_t	flags;	/* VMBUS_CHANPKT_FLAG_ */
	uint64_t	xactid;
} __packed;

struct vmbus_chanpkt {
	struct vmbus_chanpkt_hdr hdr;
} __packed;

struct vmbuspipe_hdr {
	unsigned int flags;
	unsigned int msgsize;
} __packed;

struct ic_version {
	unsigned short major;
	unsigned short minor;
} __packed;

struct icmsg_negotiate {
	unsigned short icframe_vercnt;
	unsigned short icmsg_vercnt;
	unsigned int reserved;
	struct ic_version icversion_data[]; /* any size array */
} __packed;

struct icmsg_hdr {
	struct ic_version icverframe;
	unsigned short icmsgtype;
	struct ic_version icvermsg;
	unsigned short icmsgsize;
	unsigned int status;
	unsigned char ictransaction_id;
	unsigned char icflags;
	unsigned char reserved[2];
} __packed;

int rte_vmbus_chan_recv_raw(struct vmbus_br *rxbr, void *data, uint32_t *len);
int rte_vmbus_chan_send(struct vmbus_br *txbr, uint16_t type, void *data,
			uint32_t dlen, uint32_t flags);
void vmbus_br_setup(struct vmbus_br *br, void *buf, unsigned int blen);
void *vmbus_uio_map(int *fd, int size);

/* Amount of space available for write */
static inline uint32_t vmbus_br_availwrite(const struct vmbus_br *br, uint32_t windex)
{
	uint32_t rindex = br->vbr->rindex;

	if (windex >= rindex)
		return br->dsize - (windex - rindex);
	else
		return rindex - windex;
}

static inline uint32_t vmbus_br_availread(const struct vmbus_br *br)
{
	return br->dsize - vmbus_br_availwrite(br, br->vbr->windex);
}

#endif	/* !_VMBUS_BUF_H_ */
