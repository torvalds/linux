/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef __LIBIE_CONTROLQ_H
#define __LIBIE_CONTROLQ_H

#include <net/libeth/rx.h>

#include <linux/net/intel/libie/pci.h>
#include <linux/net/intel/virtchnl2.h>

/* Default mailbox control queue */
#define LIBIE_CTLQ_MBX_ID			-1
#define LIBIE_CTLQ_MAX_BUF_LEN			SZ_4K

/**
 * enum libie_ctlq_type - control queue type
 * @LIBIE_CTLQ_TYPE_TX: basic Tx control queue
 * @LIBIE_CTLQ_TYPE_RX: basic Rx control queue
 */
enum libie_ctlq_type {
	LIBIE_CTLQ_TYPE_TX = 0,
	LIBIE_CTLQ_TYPE_RX = 1,
};

/* Opcode used to send controlq message to the control plane */
#define LIBIE_CTLQ_SEND_MSG_TO_CP		0x801
#define LIBIE_CTLQ_SEND_MSG_TO_PEER		0x804

/**
 * struct libie_ctlq_ctx - contains controlq info and MMIO region info
 * @mmio_info: MMIO region info structure
 * @ctlqs: list that stores all the control queues
 * @ctlqs_lock: lock for control queue list
 */
struct libie_ctlq_ctx {
	struct libie_mmio_info	mmio_info;
	struct list_head	ctlqs;
	spinlock_t		ctlqs_lock;	/* protects the ctlqs list */
};

/**
 * struct libie_ctlq_reg - structure representing virtual addresses of the
 *			    controlq registers and masks
 * @head: controlq head register address
 * @tail: controlq tail register address
 * @len: register address to write controlq length and enable bit
 * @addr_high: register address to write the upper 32b of ring physical address
 * @addr_low: register address to write the lower 32b of ring physical address
 * @len_mask: mask to read the controlq length
 * @len_ena_mask: mask to write the controlq enable bit
 * @head_mask: mask to read the head value
 */
struct libie_ctlq_reg {
	void __iomem	*head;
	void __iomem	*tail;
	void __iomem	*len;
	void __iomem	*addr_high;
	void __iomem	*addr_low;
	u32		len_mask;
	u32		len_ena_mask;
	u32		head_mask;
};

/**
 * struct libie_cp_dma_mem - structure for DMA memory
 * @va: virtual address
 * @pa: physical address
 * @size: memory size
 * @direction: memory to device or device to memory
 */
struct libie_cp_dma_mem {
	void		*va;
	dma_addr_t	pa;
	size_t		size;
	int		direction;
};

/**
 * struct libie_ctlq_msg - control queue message data
 * @flags: refer to 'Flags sub-structure' definitions
 * @opcode: infrastructure message opcode
 * @data_len: size of the payload
 * @func_id: queue id for mailbox selection, 0 for default mailbox (Tx)
 * @hw_retval: execution status from the HW (Rx)
 * @chnl_opcode: virtchnl message opcode
 * @chnl_retval: virtchnl return value
 * @param0: indirect message raw parameter0
 * @sw_cookie: used to verify the response of the sent virtchnl message
 * @virt_flags: virtchnl capability flags
 * @addr_param: additional parameters in place of the address, given no buffer
 * @recv_mem: virtual address and size of the buffer that contains
 *	      the indirect response
 * @send_mem: physical and virtual address of the DMA buffer,
 *	      used for sending
 */
struct libie_ctlq_msg {
	u16			flags;
	u16			opcode;
	u16			data_len;
	union {
		u16		func_id;
		u16		hw_retval;
	};
	u32			chnl_opcode;
	u32			chnl_retval;
	u32			param0;
	u16			sw_cookie;
	u16			virt_flags;
	u64			addr_param;
	union {
		struct kvec		recv_mem;
		struct libie_cp_dma_mem	send_mem;
	};
};

/**
 * struct libie_ctlq_create_info - control queue create information
 * @type: control queue type (Rx or Tx)
 * @id: queue offset passed as input, -1 for default mailbox
 * @reg: registers accessed by control queue
 * @len: controlq length
 */
struct libie_ctlq_create_info {
	enum libie_ctlq_type		type;
	int				id;
	struct libie_ctlq_reg		reg;
	u16				len;
};

/**
 * struct libie_ctlq_info - control queue information
 * @list: used to add a controlq to the list of queues in libie_ctlq_ctx
 * @type: control queue type
 * @qid: queue identifier
 * @lock: control queue lock
 * @ring_mem: descriptor ring DMA memory
 * @descs: array of descriptors
 * @rx_fqes: array of controlq Rx buffers
 * @tx_msg: Tx messages sent to hardware
 * @reg: registers used by control queue
 * @dev: device that owns this control queue
 * @pp: page pool for controlq Rx buffers
 * @truesize: size to allocate per buffer
 * @next_to_clean: next descriptor to be cleaned
 * @next_to_use: next available slot to send buffer (Tx queue)
 * @next_to_post: next available slot to post buffers to (Rx queue)
 * @ring_len: length of the descriptor ring
 */
struct libie_ctlq_info {
	struct list_head		list;
	enum libie_ctlq_type		type;
	int				qid;
	spinlock_t			lock;	/* for concurrent processing */
	struct libie_cp_dma_mem	ring_mem;
	struct libie_ctlq_desc		*descs;
	union {
		struct libeth_fqe	*rx_fqes;
		struct libie_ctlq_msg	**tx_msg;
	};
	struct libie_ctlq_reg		reg;
	struct device			*dev;
	struct page_pool		*pp;
	u32				truesize;
	u32				next_to_clean;
	union {
		u32			next_to_use;
		u32			next_to_post;
	};
	u32				ring_len;
};

#define LIBIE_CTLQ_MBX_ATQ_LEN			GENMASK(9, 0)

/* libie controlq descriptor qword0 details */

/* Flags sub-structure
 * |0  |1  |2  |3  |4  |5  |6  |7  |8  |9  |10 |11 |12 |13 |14 |15 |
 * |DD |CMP|ERR|  * RSV *  |FTYPE  | *RSV* |RD |VFC|BUF|  HOST_ID  |
 */
#define LIBIE_CTLQ_DESC_FLAG_DD		BIT(0)
#define LIBIE_CTLQ_DESC_FLAG_CMP		BIT(1)
#define LIBIE_CTLQ_DESC_FLAG_ERR		BIT(2)
#define LIBIE_CTLQ_DESC_FLAG_FTYPE_VM		BIT(6)
#define LIBIE_CTLQ_DESC_FLAG_FTYPE_PF		BIT(7)
#define LIBIE_CTLQ_DESC_FLAG_FTYPE		GENMASK(7, 6)
#define LIBIE_CTLQ_DESC_FLAG_RD		BIT(10)
#define LIBIE_CTLQ_DESC_FLAG_VFC		BIT(11)
#define LIBIE_CTLQ_DESC_FLAG_BUF		BIT(12)
#define LIBIE_CTLQ_DESC_FLAG_HOST_ID		GENMASK(15, 13)

#define LIBIE_CTLQ_DESC_FLAGS			GENMASK(15, 0)
#define LIBIE_CTLQ_DESC_INFRA_OPCODE		GENMASK_ULL(31, 16)
#define LIBIE_CTLQ_DESC_DATA_LEN		GENMASK_ULL(47, 32)
#define LIBIE_CTLQ_DESC_HW_RETVAL		GENMASK_ULL(63, 48)

#define LIBIE_CTLQ_DESC_PFID_VFID		GENMASK_ULL(63, 48)

/* libie controlq descriptor qword1 details */
#define LIBIE_CTLQ_DESC_VIRTCHNL_OPCODE	GENMASK(27, 0)
#define LIBIE_CTLQ_DESC_VIRTCHNL_DESC_TYPE	GENMASK_ULL(31, 28)
#define LIBIE_CTLQ_DESC_VIRTCHNL_MSG_RET_VAL	GENMASK_ULL(63, 32)

/* libie controlq descriptor qword2 details */
#define LIBIE_CTLQ_DESC_MSG_PARAM0		GENMASK_ULL(31, 0)
#define LIBIE_CTLQ_DESC_SW_COOKIE		GENMASK_ULL(47, 32)
#define LIBIE_CTLQ_DESC_VIRTCHNL_FLAGS		GENMASK_ULL(63, 48)

/* libie controlq descriptor qword3 details */
#define LIBIE_CTLQ_DESC_DATA_ADDR_HIGH		GENMASK_ULL(31, 0)
#define LIBIE_CTLQ_DESC_DATA_ADDR_LOW		GENMASK_ULL(63, 32)

/**
 * struct libie_ctlq_desc - control queue descriptor format
 * @qword0: flags, message opcode, data length etc
 * @qword1: virtchnl opcode, descriptor type and return value
 * @qword2: indirect message parameters
 * @qword3: indirect message buffer address
 */
struct libie_ctlq_desc {
	__le64			qword0;
	__le64			qword1;
	__le64			qword2;
	__le64			qword3;
};

/**
 * struct libie_ctlq_clean_params - cleaning parameters for Tx messages
 * @rel_dma_mem: non-sleeping callback to put the DMA buffer after send
 * @rel_ctx: additional context for release callback
 * @ctlq: control queue information
 * @num_msgs: number of messages to be cleaned
 * @force: clean even if DD is not yet set, use only for final cleanup
 */
struct libie_ctlq_clean_params {
	void (*rel_dma_mem)(const void *ctx, struct libie_cp_dma_mem *dma_mem);
	const void				*rel_ctx;
	struct libie_ctlq_info			*ctlq;
	u16					num_msgs;
	bool					force;
};

/**
 * libie_ctlq_release_rx_buf - Release Rx buffer for a specific control queue
 * @rx_buf: Rx buffer to be freed
 *
 * Driver uses this function to post back the Rx buffer after the usage.
 */
static inline void libie_ctlq_release_rx_buf(struct kvec *rx_buf)
{
	netmem_ref netmem;

	if (!rx_buf->iov_base)
		return;

	netmem = virt_to_netmem(rx_buf->iov_base);
	page_pool_put_full_netmem(netmem_get_pp(netmem), netmem, false);
}

int libie_ctlq_init(struct libie_ctlq_ctx *ctx,
		    const struct libie_ctlq_create_info *qinfo, u32 numq);
void libie_ctlq_deinit(struct libie_ctlq_ctx *ctx);

struct libie_ctlq_info *libie_find_ctlq(struct libie_ctlq_ctx *ctx,
					enum libie_ctlq_type type,
					int id);

u32 libie_ctlq_send_desc_avail(const struct libie_ctlq_info *ctlq);
void libie_ctlq_send(struct libie_ctlq_info *ctlq, u32 num_q_msg);
u32 libie_ctlq_send_clean(const struct libie_ctlq_clean_params *params);
u32 libie_ctlq_recv(struct libie_ctlq_info *ctlq, struct libie_ctlq_msg *msg,
		    u32 num_q_msg);

int libie_ctlq_post_rx_buffs(struct libie_ctlq_info *ctlq);

#endif /* __LIBIE_CONTROLQ_H */
