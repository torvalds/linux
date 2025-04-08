// SPDX-License-Identifier: GPL-2.0
/*
 * Guest ITS library, generously donated by drivers/irqchip/irq-gic-v3-its.c
 * over in the kernel tree.
 */

#include <linux/kvm.h>
#include <linux/sizes.h>
#include <asm/kvm_para.h>
#include <asm/kvm.h>

#include "kvm_util.h"
#include "vgic.h"
#include "gic.h"
#include "gic_v3.h"
#include "processor.h"

static u64 its_read_u64(unsigned long offset)
{
	return readq_relaxed(GITS_BASE_GVA + offset);
}

static void its_write_u64(unsigned long offset, u64 val)
{
	writeq_relaxed(val, GITS_BASE_GVA + offset);
}

static u32 its_read_u32(unsigned long offset)
{
	return readl_relaxed(GITS_BASE_GVA + offset);
}

static void its_write_u32(unsigned long offset, u32 val)
{
	writel_relaxed(val, GITS_BASE_GVA + offset);
}

static unsigned long its_find_baser(unsigned int type)
{
	int i;

	for (i = 0; i < GITS_BASER_NR_REGS; i++) {
		u64 baser;
		unsigned long offset = GITS_BASER + (i * sizeof(baser));

		baser = its_read_u64(offset);
		if (GITS_BASER_TYPE(baser) == type)
			return offset;
	}

	GUEST_FAIL("Couldn't find an ITS BASER of type %u", type);
	return -1;
}

static void its_install_table(unsigned int type, vm_paddr_t base, size_t size)
{
	unsigned long offset = its_find_baser(type);
	u64 baser;

	baser = ((size / SZ_64K) - 1) |
		GITS_BASER_PAGE_SIZE_64K |
		GITS_BASER_InnerShareable |
		base |
		GITS_BASER_RaWaWb |
		GITS_BASER_VALID;

	its_write_u64(offset, baser);
}

static void its_install_cmdq(vm_paddr_t base, size_t size)
{
	u64 cbaser;

	cbaser = ((size / SZ_4K) - 1) |
		 GITS_CBASER_InnerShareable |
		 base |
		 GITS_CBASER_RaWaWb |
		 GITS_CBASER_VALID;

	its_write_u64(GITS_CBASER, cbaser);
}

void its_init(vm_paddr_t coll_tbl, size_t coll_tbl_sz,
	      vm_paddr_t device_tbl, size_t device_tbl_sz,
	      vm_paddr_t cmdq, size_t cmdq_size)
{
	u32 ctlr;

	its_install_table(GITS_BASER_TYPE_COLLECTION, coll_tbl, coll_tbl_sz);
	its_install_table(GITS_BASER_TYPE_DEVICE, device_tbl, device_tbl_sz);
	its_install_cmdq(cmdq, cmdq_size);

	ctlr = its_read_u32(GITS_CTLR);
	ctlr |= GITS_CTLR_ENABLE;
	its_write_u32(GITS_CTLR, ctlr);
}

struct its_cmd_block {
	union {
		u64	raw_cmd[4];
		__le64	raw_cmd_le[4];
	};
};

static inline void its_fixup_cmd(struct its_cmd_block *cmd)
{
	/* Let's fixup BE commands */
	cmd->raw_cmd_le[0] = cpu_to_le64(cmd->raw_cmd[0]);
	cmd->raw_cmd_le[1] = cpu_to_le64(cmd->raw_cmd[1]);
	cmd->raw_cmd_le[2] = cpu_to_le64(cmd->raw_cmd[2]);
	cmd->raw_cmd_le[3] = cpu_to_le64(cmd->raw_cmd[3]);
}

static void its_mask_encode(u64 *raw_cmd, u64 val, int h, int l)
{
	u64 mask = GENMASK_ULL(h, l);
	*raw_cmd &= ~mask;
	*raw_cmd |= (val << l) & mask;
}

static void its_encode_cmd(struct its_cmd_block *cmd, u8 cmd_nr)
{
	its_mask_encode(&cmd->raw_cmd[0], cmd_nr, 7, 0);
}

static void its_encode_devid(struct its_cmd_block *cmd, u32 devid)
{
	its_mask_encode(&cmd->raw_cmd[0], devid, 63, 32);
}

static void its_encode_event_id(struct its_cmd_block *cmd, u32 id)
{
	its_mask_encode(&cmd->raw_cmd[1], id, 31, 0);
}

static void its_encode_phys_id(struct its_cmd_block *cmd, u32 phys_id)
{
	its_mask_encode(&cmd->raw_cmd[1], phys_id, 63, 32);
}

static void its_encode_size(struct its_cmd_block *cmd, u8 size)
{
	its_mask_encode(&cmd->raw_cmd[1], size, 4, 0);
}

static void its_encode_itt(struct its_cmd_block *cmd, u64 itt_addr)
{
	its_mask_encode(&cmd->raw_cmd[2], itt_addr >> 8, 51, 8);
}

static void its_encode_valid(struct its_cmd_block *cmd, int valid)
{
	its_mask_encode(&cmd->raw_cmd[2], !!valid, 63, 63);
}

static void its_encode_target(struct its_cmd_block *cmd, u64 target_addr)
{
	its_mask_encode(&cmd->raw_cmd[2], target_addr >> 16, 51, 16);
}

static void its_encode_collection(struct its_cmd_block *cmd, u16 col)
{
	its_mask_encode(&cmd->raw_cmd[2], col, 15, 0);
}

#define GITS_CMDQ_POLL_ITERATIONS	0

static void its_send_cmd(void *cmdq_base, struct its_cmd_block *cmd)
{
	u64 cwriter = its_read_u64(GITS_CWRITER);
	struct its_cmd_block *dst = cmdq_base + cwriter;
	u64 cbaser = its_read_u64(GITS_CBASER);
	size_t cmdq_size;
	u64 next;
	int i;

	cmdq_size = ((cbaser & 0xFF) + 1) * SZ_4K;

	its_fixup_cmd(cmd);

	WRITE_ONCE(*dst, *cmd);
	dsb(ishst);
	next = (cwriter + sizeof(*cmd)) % cmdq_size;
	its_write_u64(GITS_CWRITER, next);

	/*
	 * Polling isn't necessary considering KVM's ITS emulation at the time
	 * of writing this, as the CMDQ is processed synchronously after a write
	 * to CWRITER.
	 */
	for (i = 0; its_read_u64(GITS_CREADR) != next; i++) {
		__GUEST_ASSERT(i < GITS_CMDQ_POLL_ITERATIONS,
			       "ITS didn't process command at offset %lu after %d iterations\n",
			       cwriter, i);

		cpu_relax();
	}
}

void its_send_mapd_cmd(void *cmdq_base, u32 device_id, vm_paddr_t itt_base,
		       size_t itt_size, bool valid)
{
	struct its_cmd_block cmd = {};

	its_encode_cmd(&cmd, GITS_CMD_MAPD);
	its_encode_devid(&cmd, device_id);
	its_encode_size(&cmd, ilog2(itt_size) - 1);
	its_encode_itt(&cmd, itt_base);
	its_encode_valid(&cmd, valid);

	its_send_cmd(cmdq_base, &cmd);
}

void its_send_mapc_cmd(void *cmdq_base, u32 vcpu_id, u32 collection_id, bool valid)
{
	struct its_cmd_block cmd = {};

	its_encode_cmd(&cmd, GITS_CMD_MAPC);
	its_encode_collection(&cmd, collection_id);
	its_encode_target(&cmd, vcpu_id);
	its_encode_valid(&cmd, valid);

	its_send_cmd(cmdq_base, &cmd);
}

void its_send_mapti_cmd(void *cmdq_base, u32 device_id, u32 event_id,
			u32 collection_id, u32 intid)
{
	struct its_cmd_block cmd = {};

	its_encode_cmd(&cmd, GITS_CMD_MAPTI);
	its_encode_devid(&cmd, device_id);
	its_encode_event_id(&cmd, event_id);
	its_encode_phys_id(&cmd, intid);
	its_encode_collection(&cmd, collection_id);

	its_send_cmd(cmdq_base, &cmd);
}

void its_send_invall_cmd(void *cmdq_base, u32 collection_id)
{
	struct its_cmd_block cmd = {};

	its_encode_cmd(&cmd, GITS_CMD_INVALL);
	its_encode_collection(&cmd, collection_id);

	its_send_cmd(cmdq_base, &cmd);
}
