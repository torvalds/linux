/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018 MediaTek Inc.
 *
 */

#ifndef __MTK_CMDQ_H__
#define __MTK_CMDQ_H__

#include <linux/mailbox_client.h>
#include <linux/mailbox/mtk-cmdq-mailbox.h>
#include <linux/timer.h>

#define CMDQ_ADDR_HIGH(addr)	((u32)(((addr) >> 16) & GENMASK(31, 0)))
#define CMDQ_ADDR_LOW(addr)	((u16)(addr) | BIT(1))

/*
 * Every cmdq thread has its own SPRs (Specific Purpose Registers),
 * so there are 4 * N (threads) SPRs in GCE that shares the same indexes below.
 */
#define CMDQ_THR_SPR_IDX0	(0)
#define CMDQ_THR_SPR_IDX1	(1)
#define CMDQ_THR_SPR_IDX2	(2)
#define CMDQ_THR_SPR_IDX3	(3)

struct cmdq_pkt;

enum cmdq_logic_op {
	CMDQ_LOGIC_ASSIGN = 0,
	CMDQ_LOGIC_ADD = 1,
	CMDQ_LOGIC_SUBTRACT = 2,
	CMDQ_LOGIC_MULTIPLY = 3,
	CMDQ_LOGIC_XOR = 8,
	CMDQ_LOGIC_NOT = 9,
	CMDQ_LOGIC_OR = 10,
	CMDQ_LOGIC_AND = 11,
	CMDQ_LOGIC_LEFT_SHIFT = 12,
	CMDQ_LOGIC_RIGHT_SHIFT = 13,
	CMDQ_LOGIC_MAX,
};

struct cmdq_operand {
	/* register type */
	bool reg;
	union {
		/* index */
		u16 idx;
		/* value */
		u16 value;
	};
};

struct cmdq_client_reg {
	u8 subsys;
	u16 offset;
	u16 size;
};

struct cmdq_client {
	struct mbox_client client;
	struct mbox_chan *chan;
};

#if IS_ENABLED(CONFIG_MTK_CMDQ)

/**
 * cmdq_dev_get_client_reg() - parse cmdq client reg from the device
 *			       node of CMDQ client
 * @dev:	device of CMDQ mailbox client
 * @client_reg: CMDQ client reg pointer
 * @idx:	the index of desired reg
 *
 * Return: 0 for success; else the error code is returned
 *
 * Help CMDQ client parsing the cmdq client reg
 * from the device node of CMDQ client.
 */
int cmdq_dev_get_client_reg(struct device *dev,
			    struct cmdq_client_reg *client_reg, int idx);

/**
 * cmdq_mbox_create() - create CMDQ mailbox client and channel
 * @dev:	device of CMDQ mailbox client
 * @index:	index of CMDQ mailbox channel
 *
 * Return: CMDQ mailbox client pointer
 */
struct cmdq_client *cmdq_mbox_create(struct device *dev, int index);

/**
 * cmdq_mbox_destroy() - destroy CMDQ mailbox client and channel
 * @client:	the CMDQ mailbox client
 */
void cmdq_mbox_destroy(struct cmdq_client *client);

/**
 * cmdq_pkt_create() - create a CMDQ packet
 * @client:	the CMDQ mailbox client
 * @pkt:	the CMDQ packet
 * @size:	required CMDQ buffer size
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_create(struct cmdq_client *client, struct cmdq_pkt *pkt, size_t size);

/**
 * cmdq_pkt_destroy() - destroy the CMDQ packet
 * @client:	the CMDQ mailbox client
 * @pkt:	the CMDQ packet
 */
void cmdq_pkt_destroy(struct cmdq_client *client, struct cmdq_pkt *pkt);

/**
 * cmdq_pkt_write() - append write command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @subsys:	the CMDQ sub system code
 * @offset:	register offset from CMDQ sub system
 * @value:	the specified target register value
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_write(struct cmdq_pkt *pkt, u8 subsys, u16 offset, u32 value);

/**
 * cmdq_pkt_write_mask() - append write command with mask to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @subsys:	the CMDQ sub system code
 * @offset:	register offset from CMDQ sub system
 * @value:	the specified target register value
 * @mask:	the specified target register mask
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_write_mask(struct cmdq_pkt *pkt, u8 subsys,
			u16 offset, u32 value, u32 mask);

/*
 * cmdq_pkt_read_s() - append read_s command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @high_addr_reg_idx:	internal register ID which contains high address of pa
 * @addr_low:	low address of pa
 * @reg_idx:	the CMDQ internal register ID to cache read data
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_read_s(struct cmdq_pkt *pkt, u16 high_addr_reg_idx, u16 addr_low,
		    u16 reg_idx);

/**
 * cmdq_pkt_write_s() - append write_s command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @high_addr_reg_idx:	internal register ID which contains high address of pa
 * @addr_low:	low address of pa
 * @src_reg_idx:	the CMDQ internal register ID which cache source value
 *
 * Return: 0 for success; else the error code is returned
 *
 * Support write value to physical address without subsys. Use CMDQ_ADDR_HIGH()
 * to get high address and call cmdq_pkt_assign() to assign value into internal
 * reg. Also use CMDQ_ADDR_LOW() to get low address for addr_low parameter when
 * call to this function.
 */
int cmdq_pkt_write_s(struct cmdq_pkt *pkt, u16 high_addr_reg_idx,
		     u16 addr_low, u16 src_reg_idx);

/**
 * cmdq_pkt_write_s_mask() - append write_s with mask command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @high_addr_reg_idx:	internal register ID which contains high address of pa
 * @addr_low:	low address of pa
 * @src_reg_idx:	the CMDQ internal register ID which cache source value
 * @mask:	the specified target address mask, use U32_MAX if no need
 *
 * Return: 0 for success; else the error code is returned
 *
 * Support write value to physical address without subsys. Use CMDQ_ADDR_HIGH()
 * to get high address and call cmdq_pkt_assign() to assign value into internal
 * reg. Also use CMDQ_ADDR_LOW() to get low address for addr_low parameter when
 * call to this function.
 */
int cmdq_pkt_write_s_mask(struct cmdq_pkt *pkt, u16 high_addr_reg_idx,
			  u16 addr_low, u16 src_reg_idx, u32 mask);

/**
 * cmdq_pkt_write_s_value() - append write_s command to the CMDQ packet which
 *			      write value to a physical address
 * @pkt:	the CMDQ packet
 * @high_addr_reg_idx:	internal register ID which contains high address of pa
 * @addr_low:	low address of pa
 * @value:	the specified target value
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_write_s_value(struct cmdq_pkt *pkt, u8 high_addr_reg_idx,
			   u16 addr_low, u32 value);

/**
 * cmdq_pkt_write_s_mask_value() - append write_s command with mask to the CMDQ
 *				   packet which write value to a physical
 *				   address
 * @pkt:	the CMDQ packet
 * @high_addr_reg_idx:	internal register ID which contains high address of pa
 * @addr_low:	low address of pa
 * @value:	the specified target value
 * @mask:	the specified target mask
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_write_s_mask_value(struct cmdq_pkt *pkt, u8 high_addr_reg_idx,
				u16 addr_low, u32 value, u32 mask);

/**
 * cmdq_pkt_mem_move() - append memory move command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @src_addr:	source address
 * @dst_addr:	destination address
 *
 * Appends a CMDQ command to copy the value found in `src_addr` to `dst_addr`.
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_mem_move(struct cmdq_pkt *pkt, dma_addr_t src_addr, dma_addr_t dst_addr);

/**
 * cmdq_pkt_wfe() - append wait for event command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @event:	the desired event type to wait
 * @clear:	clear event or not after event arrive
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_wfe(struct cmdq_pkt *pkt, u16 event, bool clear);

/**
 * cmdq_pkt_acquire_event() - append acquire event command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @event:	the desired event to be acquired
 *
 * User can use cmdq_pkt_acquire_event() as `mutex_lock` and cmdq_pkt_clear_event()
 * as `mutex_unlock` to protect some `critical section` instructions between them.
 * cmdq_pkt_acquire_event() would wait for event to be cleared.
 * After event is cleared by cmdq_pkt_clear_event in other GCE threads,
 * cmdq_pkt_acquire_event() would set event and keep executing next instruction.
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_acquire_event(struct cmdq_pkt *pkt, u16 event);

/**
 * cmdq_pkt_clear_event() - append clear event command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @event:	the desired event to be cleared
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_clear_event(struct cmdq_pkt *pkt, u16 event);

/**
 * cmdq_pkt_set_event() - append set event command to the CMDQ packet
 * @pkt:	the CMDQ packet
 * @event:	the desired event to be set
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_set_event(struct cmdq_pkt *pkt, u16 event);

/**
 * cmdq_pkt_poll() - Append polling command to the CMDQ packet, ask GCE to
 *		     execute an instruction that wait for a specified
 *		     hardware register to check for the value w/o mask.
 *		     All GCE hardware threads will be blocked by this
 *		     instruction.
 * @pkt:	the CMDQ packet
 * @subsys:	the CMDQ sub system code
 * @offset:	register offset from CMDQ sub system
 * @value:	the specified target register value
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_poll(struct cmdq_pkt *pkt, u8 subsys,
		  u16 offset, u32 value);

/**
 * cmdq_pkt_poll_mask() - Append polling command to the CMDQ packet, ask GCE to
 *		          execute an instruction that wait for a specified
 *		          hardware register to check for the value w/ mask.
 *		          All GCE hardware threads will be blocked by this
 *		          instruction.
 * @pkt:	the CMDQ packet
 * @subsys:	the CMDQ sub system code
 * @offset:	register offset from CMDQ sub system
 * @value:	the specified target register value
 * @mask:	the specified target register mask
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_poll_mask(struct cmdq_pkt *pkt, u8 subsys,
		       u16 offset, u32 value, u32 mask);

/**
 * cmdq_pkt_logic_command() - Append logic command to the CMDQ packet, ask GCE to
 *		          execute an instruction that store the result of logic operation
 *		          with left and right operand into result_reg_idx.
 * @pkt:		the CMDQ packet
 * @result_reg_idx:	SPR index that store operation result of left_operand and right_operand
 * @left_operand:	left operand
 * @s_op:		the logic operator enum
 * @right_operand:	right operand
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_logic_command(struct cmdq_pkt *pkt, u16 result_reg_idx,
			   struct cmdq_operand *left_operand,
			   enum cmdq_logic_op s_op,
			   struct cmdq_operand *right_operand);

/**
 * cmdq_pkt_assign() - Append logic assign command to the CMDQ packet, ask GCE
 *		       to execute an instruction that set a constant value into
 *		       internal register and use as value, mask or address in
 *		       read/write instruction.
 * @pkt:	the CMDQ packet
 * @reg_idx:	the CMDQ internal register ID
 * @value:	the specified value
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_assign(struct cmdq_pkt *pkt, u16 reg_idx, u32 value);

/**
 * cmdq_pkt_poll_addr() - Append blocking POLL command to CMDQ packet
 * @pkt:	the CMDQ packet
 * @addr:	the hardware register address
 * @value:	the specified target register value
 * @mask:	the specified target register mask
 *
 * Appends a polling (POLL) command to the CMDQ packet and asks the GCE
 * to execute an instruction that checks for the specified `value` (with
 * or without `mask`) to appear in the specified hardware register `addr`.
 * All GCE threads will be blocked by this instruction.
 *
 * Return: 0 for success or negative error code
 */
int cmdq_pkt_poll_addr(struct cmdq_pkt *pkt, dma_addr_t addr, u32 value, u32 mask);

/**
 * cmdq_pkt_jump_abs() - Append jump command to the CMDQ packet, ask GCE
 *			 to execute an instruction that change current thread
 *			 PC to a absolute physical address which should
 *			 contains more instruction.
 * @pkt:        the CMDQ packet
 * @addr:       absolute physical address of target instruction buffer
 * @shift_pa:	shift bits of physical address in CMDQ instruction. This value
 *		is got by cmdq_get_shift_pa().
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_jump_abs(struct cmdq_pkt *pkt, dma_addr_t addr, u8 shift_pa);

/* This wrapper has to be removed after all users migrated to jump_abs */
static inline int cmdq_pkt_jump(struct cmdq_pkt *pkt, dma_addr_t addr, u8 shift_pa)
{
	return cmdq_pkt_jump_abs(pkt, addr, shift_pa);
}

/**
 * cmdq_pkt_jump_rel() - Append jump command to the CMDQ packet, ask GCE
 *			 to execute an instruction that change current thread
 *			 PC to a physical address with relative offset. The
 *			 target address should contains more instruction.
 * @pkt:	the CMDQ packet
 * @offset:	relative offset of target instruction buffer from current PC.
 * @shift_pa:	shift bits of physical address in CMDQ instruction. This value
 *		is got by cmdq_get_shift_pa().
 *
 * Return: 0 for success; else the error code is returned
 */
int cmdq_pkt_jump_rel(struct cmdq_pkt *pkt, s32 offset, u8 shift_pa);

/**
 * cmdq_pkt_eoc() - Append EOC and ask GCE to generate an IRQ at end of execution
 * @pkt:	The CMDQ packet
 *
 * Appends an End Of Code (EOC) command to the CMDQ packet and asks the GCE
 * to generate an interrupt at the end of the execution of all commands in
 * the pipeline.
 * The EOC command is usually appended to the end of the pipeline to notify
 * that all commands are done.
 *
 * Return: 0 for success or negative error number
 */
int cmdq_pkt_eoc(struct cmdq_pkt *pkt);

#else /* IS_ENABLED(CONFIG_MTK_CMDQ) */

static inline int cmdq_dev_get_client_reg(struct device *dev,
					  struct cmdq_client_reg *client_reg, int idx)
{
	return -ENODEV;
}

static inline struct cmdq_client *cmdq_mbox_create(struct device *dev, int index)
{
	return ERR_PTR(-EINVAL);
}

static inline void cmdq_mbox_destroy(struct cmdq_client *client) { }

static inline int cmdq_pkt_create(struct cmdq_client *client, struct cmdq_pkt *pkt, size_t size)
{
	return -EINVAL;
}

static inline void cmdq_pkt_destroy(struct cmdq_client *client, struct cmdq_pkt *pkt) { }

static inline int cmdq_pkt_write(struct cmdq_pkt *pkt, u8 subsys, u16 offset, u32 value)
{
	return -ENOENT;
}

static inline int cmdq_pkt_write_mask(struct cmdq_pkt *pkt, u8 subsys,
				      u16 offset, u32 value, u32 mask)
{
	return -ENOENT;
}

static inline int cmdq_pkt_read_s(struct cmdq_pkt *pkt, u16 high_addr_reg_idx,
				  u16 addr_low, u16 reg_idx)
{
	return -ENOENT;
}

static inline int cmdq_pkt_write_s(struct cmdq_pkt *pkt, u16 high_addr_reg_idx,
				   u16 addr_low, u16 src_reg_idx)
{
	return -ENOENT;
}

static inline int cmdq_pkt_write_s_mask(struct cmdq_pkt *pkt, u16 high_addr_reg_idx,
					u16 addr_low, u16 src_reg_idx, u32 mask)
{
	return -ENOENT;
}

static inline int cmdq_pkt_write_s_value(struct cmdq_pkt *pkt, u8 high_addr_reg_idx,
					 u16 addr_low, u32 value)
{
	return -ENOENT;
}

static inline int cmdq_pkt_write_s_mask_value(struct cmdq_pkt *pkt, u8 high_addr_reg_idx,
					      u16 addr_low, u32 value, u32 mask)
{
	return -ENOENT;
}

static inline int cmdq_pkt_wfe(struct cmdq_pkt *pkt, u16 event, bool clear)
{
	return -EINVAL;
}

static inline int cmdq_pkt_clear_event(struct cmdq_pkt *pkt, u16 event)
{
	return -EINVAL;
}

static inline int cmdq_pkt_set_event(struct cmdq_pkt *pkt, u16 event)
{
	return -EINVAL;
}

static inline int cmdq_pkt_poll(struct cmdq_pkt *pkt, u8 subsys,
				u16 offset, u32 value)
{
	return -EINVAL;
}

static inline int cmdq_pkt_poll_mask(struct cmdq_pkt *pkt, u8 subsys,
				     u16 offset, u32 value, u32 mask)
{
	return -EINVAL;
}

static inline int cmdq_pkt_assign(struct cmdq_pkt *pkt, u16 reg_idx, u32 value)
{
	return -EINVAL;
}

static inline int cmdq_pkt_poll_addr(struct cmdq_pkt *pkt, dma_addr_t addr, u32 value, u32 mask)
{
	return -EINVAL;
}

static inline int cmdq_pkt_jump_abs(struct cmdq_pkt *pkt, dma_addr_t addr, u8 shift_pa)
{
	return -EINVAL;
}

static inline int cmdq_pkt_jump(struct cmdq_pkt *pkt, dma_addr_t addr, u8 shift_pa)
{
	return -EINVAL;
}

static inline int cmdq_pkt_jump_rel(struct cmdq_pkt *pkt, s32 offset, u8 shift_pa)
{
	return -EINVAL;
}

static inline int cmdq_pkt_eoc(struct cmdq_pkt *pkt)
{
	return -EINVAL;
}

#endif /* IS_ENABLED(CONFIG_MTK_CMDQ) */

#endif	/* __MTK_CMDQ_H__ */
