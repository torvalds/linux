// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * mlx5 VFIO selftest driver
 *
 * Programs mlx5 ConnectX VFs and PFs through the bare-metal command interface
 * and RDMA WRITE self-loopback to perform DMA.  Implements vfio_pci_driver_ops
 * (probe/init/remove) and plugs into the VFIO selftest framework.
 */
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <linux/pci_regs.h>
#include <linux/pci_ids.h>

#include <libvfio.h>

#include "mlx5_hw.h"

/*
 * Driver state — overlaid on device->driver.region.vaddr.
 *
 * Contains both software-only state and HW-visible DMA buffers. HW buffers need
 * strict IOVA alignment.
 */
struct mlx5st_device {
	/* Back pointer */
	struct vfio_pci_device *device;

	/* BAR0 */
	struct mlx5st_initial_seg __iomem *init_seg;
	void __iomem *bar0;

	/* Command interface */
	struct mlx5st_cmd_queue_entry *cmd_lay;
	struct mlx5st_cmd_queue_entry *pages_cmd_lay;
	u8 cmd_log_stride;
	unsigned int pages_slot;
	u8 cmd_token;
	bool cmd_sig_enabled;

	/* PD */
	u32 pdn;

	/* Global PA-mode MKEY */
	u32 global_lkey;
	u32 global_rkey;
	u32 mkey_index;

	/* CQ */
	u32 cqn;
	u32 cq_ci;
	u32 cq_arm_sn;

	/* UAR */
	u32 uar_page;
	void __iomem *uar_base;
	unsigned int uar_bf_offset;

	/* EQ (cmd/pages events — polled, not interrupt-driven) */
	u32 eqn;
	u32 eq_cons_index;
	bool have_eq;

	/* MSI EQ (CQ completion events — fires MSI-X) */
	u32 msi_eqn;
	u32 msi_eq_cons_index;
	bool have_msi_eq;

	/* Async pages slot state */
	bool pages_slot_in_use;
	bool pages_slot_is_reclaim;
	unsigned int pages_reclaim_npages;
	unsigned int pages_pending_give;
	unsigned int pages_pending_reclaim;
	u16 pages_pending_func_id;
	bool pages_func_id_seen;

	/* QP */
	u32 qpn;
	u32 sq_pi;
	u32 sq_ci;

	/* FW pages bitmap */
	u64 fw_pages_bitmap[MAX_FW_PAGES / 64];
	u32 fw_pages_given;
	u16 fw_func_id;

	/* Capabilities */
	bool fl_supported;
	u8 log_max_msg;

	/* Buffers used by send_msi() to trigger an interrupt */
	u64 send_msi_src;
	u64 send_msi_dst;

	/*
	 * HW-visible DMA buffers below — device reads/writes via DMA.
	 */
	struct mlx5st_cmd_queue_entry cmd_queue
		[MLX5_HW_PAGE_SIZE / sizeof(struct mlx5st_cmd_queue_entry)]
		__aligned(MLX5_HW_PAGE_SIZE);
	struct mlx5st_send_wqe sq_buf[SQ_WQE_CNT];
	struct mlx5st_dbrec cq_dbrec;
	struct mlx5st_dbrec qp_dbrec;
	struct mlx5st_cqe64 cq_buf[CQ_CQE_CNT];

	/* Slot 0 mailboxes (regular commands) */
	struct mlx5st_mbox_entry cmd_in_mbox[CMD_MBOX_NENT];
	struct mlx5st_mbox_entry cmd_out_mbox[CMD_MBOX_NENT];

	/* Pages slot mailboxes (async MANAGE_PAGES) */
	struct mlx5st_mbox_entry pages_in_mbox[CMD_MBOX_NENT];
	struct mlx5st_mbox_entry pages_out_mbox[CMD_MBOX_NENT];

	/* EQ does not support page_offset */
	struct mlx5st_eqe eq_buf[EQ_NENT] __aligned(MLX5_HW_PAGE_SIZE);

	/* MSI EQ buffer — CQ completions generate EQEs here -> MSI-X */
	struct mlx5st_eqe msi_eq_buf[MSI_EQ_NENT] __aligned(MLX5_HW_PAGE_SIZE);

	u8 fw_pages[MAX_FW_PAGES][MLX5_HW_PAGE_SIZE]
		__aligned(MLX5_HW_PAGE_SIZE);
};

/* Check against HW limits on IOVA alignment */
static_assert(offsetof(struct mlx5st_device, cmd_in_mbox) %
			      CMD_MBOX_STRIDE == 0,
	      "cmd_in_mbox must be stride-aligned");
static_assert(offsetof(struct mlx5st_device, pages_in_mbox) %
			      CMD_MBOX_STRIDE == 0,
	      "pages_in_mbox must be stride-aligned");
static_assert(offsetof(struct mlx5st_device, cq_buf) % 64 == 0,
	      "cq_buf must be 64-byte aligned");
static_assert(offsetof(struct mlx5st_device, sq_buf) % 64 == 0,
	      "sq_buf must be 64-byte aligned");
static_assert(offsetof(struct mlx5st_device, cq_dbrec) % 64 == 0,
	      "cq_dbrec must be 64-byte aligned");
static_assert(offsetof(struct mlx5st_device, qp_dbrec) % 64 == 0,
	      "qp_dbrec must be 64-byte aligned");
static_assert(offsetof(struct mlx5st_device, eq_buf) %
			      MLX5_HW_PAGE_SIZE == 0,
	      "eq_buf must be page-aligned");
static_assert(offsetof(struct mlx5st_device, msi_eq_buf) %
			      MLX5_HW_PAGE_SIZE == 0,
	      "msi_eq_buf must be page-aligned");
static_assert(offsetof(struct mlx5st_device, fw_pages) %
			      MLX5_HW_PAGE_SIZE == 0,
	      "fw_pages must be page-aligned");

static struct mlx5st_device *to_mlx5st(struct vfio_pci_device *device)
{
	return device->driver.region.vaddr;
}

/*
 * Fill a PAS (Physical Address Segment) for a buffer in the driver region.
 * Sets pas[0] to the page-aligned IOVA and returns the page_offset (the
 * buffer's byte offset within that page, in units of 64 bytes).
 */
static unsigned int mlx5st_fill_pas(struct vfio_pci_device *device, void *buf,
				    __be64 *pas)
{
	u64 iova = to_iova(device, buf);

	pas[0] = cpu_to_be64(iova & ~(u64)(MLX5_HW_PAGE_SIZE - 1));
	return (iova & (MLX5_HW_PAGE_SIZE - 1)) / 64;
}

/*
 * Probe — match mlx5 devices by PCI vendor/device ID.
 */

static int mlx5st_probe(struct vfio_pci_device *device)
{
	static const u16 mlx5st_pci_ids[] = {
		0x1011, /* Connect-IB */
		0x1012, /* Connect-IB VF */
		0x1013, /* ConnectX-4 */
		0x1014, /* ConnectX-4 VF */
		0x1015, /* ConnectX-4LX */
		0x1016, /* ConnectX-4LX VF */
		0x1017, /* ConnectX-5 */
		0x1018, /* ConnectX-5 VF */
		0x1019, /* ConnectX-5 Ex */
		0x101a, /* ConnectX-5 Ex VF */
		0x101b, /* ConnectX-6 */
		0x101c, /* ConnectX-6 VF */
		0x101d, /* ConnectX-6 Dx */
		0x101e, /* ConnectX-6 Dx VF */
		0x101f, /* ConnectX-6 LX */
		0x1021, /* ConnectX-7 */
		0x1023, /* ConnectX-8 */
		0x1025, /* ConnectX-9 */
		0x1027, /* ConnectX-10 */
		0x2101, /* ConnectX-10 NVLink-C2C */
		0xa2d2, /* BlueField integrated ConnectX-5 */
		0xa2d3, /* BlueField integrated ConnectX-5 VF */
		0xa2d6, /* BlueField-2 integrated ConnectX-6 Dx */
		0xa2dc, /* BlueField-3 integrated ConnectX-7 */
		0xa2df, /* BlueField-4 integrated ConnectX-8 */
	};
	unsigned int i;
	u16 did;

	if (vfio_pci_config_readw(device, PCI_VENDOR_ID) !=
	    PCI_VENDOR_ID_MELLANOX)
		return -ENODEV;

	did = vfio_pci_config_readw(device, PCI_DEVICE_ID);
	for (i = 0; i < ARRAY_SIZE(mlx5st_pci_ids); i++) {
		if (mlx5st_pci_ids[i] == did)
			return 0;
	}

	return -ENODEV;
}

/*
 * Command interface
 */

static u8 xor8_buf(const void *buf, size_t offset, size_t len)
{
	const u8 *p = buf;
	u8 sum = 0;
	size_t i;

	for (i = offset; i < offset + len; i++)
		sum ^= p[i];
	return sum;
}

#define CMD_IF_BOX_CTRL_OFF MLX5_BYTE_OFF(cmd_if_box, reserved_at_1000)
#define CMD_IF_BOX_CTRL_SIG_OFF MLX5_BYTE_OFF(cmd_if_box, ctrl_signature)
#define CMD_IF_BOX_SIG_OFF MLX5_BYTE_OFF(cmd_if_box, signature)

static void mlx5st_cmd_calc_block_sig(struct mlx5st_cmd_if_box *blk)
{
	MLX5_SET(cmd_if_box, blk, ctrl_signature,
		 ~xor8_buf(blk, CMD_IF_BOX_CTRL_OFF,
			   CMD_IF_BOX_CTRL_SIG_OFF - CMD_IF_BOX_CTRL_OFF));
	MLX5_SET(cmd_if_box, blk, signature,
		 ~xor8_buf(blk, 0, CMD_IF_BOX_SIG_OFF));
}

static int mlx5st_cmd_verify_block_sig(struct mlx5st_cmd_if_box *blk)
{
	if (xor8_buf(blk, CMD_IF_BOX_CTRL_OFF,
		     CMD_IF_BOX_SIG_OFF - CMD_IF_BOX_CTRL_OFF) != 0xff)
		return -1;
	if (xor8_buf(blk, 0, sizeof(struct mlx5st_cmd_if_box)) != 0xff)
		return -1;
	return 0;
}

static unsigned int mlx5st_cmd_setup_mbox_chain(struct vfio_pci_device *device,
						struct mlx5st_mbox_entry *mbox,
						unsigned int nblocks, u8 token)
{
	unsigned int i;

	for (i = 0; i < nblocks; i++) {
		struct mlx5st_cmd_if_box *blk = &mbox[i].block;
		u64 next_iova;

		memset(blk, 0, sizeof(struct mlx5st_cmd_if_box));
		MLX5_SET(cmd_if_box, blk, block_number, i);
		MLX5_SET(cmd_if_box, blk, token, token);
		if (i < nblocks - 1) {
			next_iova = to_iova(device, &mbox[i + 1]);
			MLX5_SET(cmd_if_box, blk, next_pointer_63_32,
				 next_iova >> 32);
			MLX5_SET(cmd_if_box, blk, next_pointer_31_10,
				 (u32)next_iova >> 10);
		}
	}
	return nblocks;
}

static void mlx5st_cmd_copy_to_mbox(struct mlx5st_mbox_entry *mbox,
				     const void *data, unsigned int len)
{
	const u8 *src = data;
	unsigned int i = 0;

	while (len > 0) {
		unsigned int chunk = len < MLX5_CMD_DATA_BLOCK_SIZE ?
					     len :
					     MLX5_CMD_DATA_BLOCK_SIZE;

		memcpy(MLX5_ADDR_OF(cmd_if_box, &mbox[i].block, mailbox_data),
		       src, chunk);
		src += chunk;
		len -= chunk;
		i++;
	}
}

static void mlx5st_cmd_copy_from_mbox(void *data,
				       const struct mlx5st_mbox_entry *mbox,
				       unsigned int len)
{
	unsigned int i = 0;
	u8 *dst = data;

	while (len > 0) {
		unsigned int chunk = len < MLX5_CMD_DATA_BLOCK_SIZE ?
					     len :
					     MLX5_CMD_DATA_BLOCK_SIZE;

		memcpy(dst,
		       MLX5_ADDR_OF(cmd_if_box, &mbox[i].block, mailbox_data),
		       chunk);
		dst += chunk;
		len -= chunk;
		i++;
	}
}

/* Forward declaration — cmd_exec polls events during command wait */
static void mlx5st_process_events(struct mlx5st_device *dev);

static const char *mlx5st_cmd_name(u16 opcode)
{
	switch (opcode) {
	case MLX5_CMD_OP_QUERY_HCA_CAP: return "QUERY_HCA_CAP";
	case MLX5_CMD_OP_INIT_HCA: return "INIT_HCA";
	case MLX5_CMD_OP_TEARDOWN_HCA: return "TEARDOWN_HCA";
	case MLX5_CMD_OP_ENABLE_HCA: return "ENABLE_HCA";
	case MLX5_CMD_OP_DISABLE_HCA: return "DISABLE_HCA";
	case MLX5_CMD_OP_QUERY_PAGES: return "QUERY_PAGES";
	case MLX5_CMD_OP_MANAGE_PAGES: return "MANAGE_PAGES";
	case MLX5_CMD_OP_SET_HCA_CAP: return "SET_HCA_CAP";
	case MLX5_CMD_OP_SET_ISSI: return "SET_ISSI";
	case MLX5_CMD_OP_CREATE_MKEY: return "CREATE_MKEY";
	case MLX5_CMD_OP_DESTROY_MKEY: return "DESTROY_MKEY";
	case MLX5_CMD_OP_CREATE_EQ: return "CREATE_EQ";
	case MLX5_CMD_OP_DESTROY_EQ: return "DESTROY_EQ";
	case MLX5_CMD_OP_CREATE_CQ: return "CREATE_CQ";
	case MLX5_CMD_OP_DESTROY_CQ: return "DESTROY_CQ";
	case MLX5_CMD_OP_CREATE_QP: return "CREATE_QP";
	case MLX5_CMD_OP_DESTROY_QP: return "DESTROY_QP";
	case MLX5_CMD_OP_RST2INIT_QP: return "RST2INIT_QP";
	case MLX5_CMD_OP_INIT2RTR_QP: return "INIT2RTR_QP";
	case MLX5_CMD_OP_RTR2RTS_QP: return "RTR2RTS_QP";
	case MLX5_CMD_OP_ALLOC_PD: return "ALLOC_PD";
	case MLX5_CMD_OP_DEALLOC_PD: return "DEALLOC_PD";
	case MLX5_CMD_OP_ALLOC_UAR: return "ALLOC_UAR";
	case MLX5_CMD_OP_DEALLOC_UAR: return "DEALLOC_UAR";
	default: return "UNKNOWN";
	}
}

/*
 * Post a command on a given slot: fill the cmd_queue_entry, set up mailbox
 * chains, compute signatures, hand ownership to FW, and ring the doorbell.
 */
static void mlx5st_cmd_post(struct mlx5st_device *dev,
			     struct mlx5st_cmd_queue_entry *cmd,
			     struct mlx5st_mbox_entry *in_mbox,
			     struct mlx5st_mbox_entry *out_mbox,
			     void *in, unsigned int ilen, unsigned int olen,
			     u32 doorbell)
{
	struct vfio_pci_device *device = dev->device;
	unsigned int in_remain, out_remain, in_nblk, out_nblk;
	unsigned int i;
	void *cin, *cout;
	u8 token;

	/* Rotating non-zero token ties cmd entry to its mailbox blocks */
	token = ++dev->cmd_token;
	if (!token)
		token = ++dev->cmd_token;

	in_remain = ilen > MLX5_CMD_INLINE_SZ ? ilen - MLX5_CMD_INLINE_SZ : 0;
	out_remain = olen > MLX5_CMD_INLINE_SZ ? olen - MLX5_CMD_INLINE_SZ : 0;
	in_nblk = (in_remain + MLX5_CMD_DATA_BLOCK_SIZE - 1) /
		  MLX5_CMD_DATA_BLOCK_SIZE;
	out_nblk = (out_remain + MLX5_CMD_DATA_BLOCK_SIZE - 1) /
		   MLX5_CMD_DATA_BLOCK_SIZE;

	/* Set up mailbox chains */
	if (in_nblk > 0) {
		mlx5st_cmd_setup_mbox_chain(device, in_mbox, in_nblk, token);
		mlx5st_cmd_copy_to_mbox(in_mbox,
					(u8 *)in + MLX5_CMD_INLINE_SZ,
					in_remain);
	}
	if (out_nblk > 0)
		mlx5st_cmd_setup_mbox_chain(device, out_mbox, out_nblk, token);

	/* Copy inline input */
	cin = MLX5_ADDR_OF(cmd_queue_entry, cmd, command_input_inline_data);
	memset(cin, 0, MLX5_CMD_INLINE_SZ);
	memcpy(cin, in, ilen < MLX5_CMD_INLINE_SZ ? ilen : MLX5_CMD_INLINE_SZ);
	MLX5_SET(cmd_queue_entry, cmd, input_length, ilen);
	MLX5_SET(cmd_queue_entry, cmd, token, token);

	/* Zero inline output */
	cout = MLX5_ADDR_OF(cmd_queue_entry, cmd, command_output_inline_data);
	memset(cout, 0, MLX5_CMD_INLINE_SZ);
	MLX5_SET(cmd_queue_entry, cmd, output_length, olen);

	/*
	 * Compute signatures: mailbox blocks first, then cmd_queue_entry.
	 * The sig must cover the final state including ownership=0x1, but
	 * we must not set ownership until after the sig is in place -
	 * XOR in the 0x1 without storing it to memory.
	 */
	for (i = 0; i < in_nblk; i++)
		mlx5st_cmd_calc_block_sig(&in_mbox[i].block);
	for (i = 0; i < out_nblk; i++)
		mlx5st_cmd_calc_block_sig(&out_mbox[i].block);
	MLX5_SET(cmd_queue_entry, cmd, signature, 0);
	MLX5_SET(cmd_queue_entry, cmd, signature,
		 ~(xor8_buf(cmd, 0, sizeof(struct mlx5st_cmd_queue_entry)) ^
		   0x1));

	/* Ensure all cmd data (including sig) is visible, then hand to FW */
	dma_wmb();
	MLX5_SET_ONCE(cmd_queue_entry, cmd, ownership, 1);

	/* Ring doorbell */
	MLX5_SET_MMIO(initial_seg, dev->init_seg, command_doorbell_vector,
		      doorbell);
}

static void mlx5st_cmd_exec(struct mlx5st_device *dev, void *in,
			     unsigned int ilen, void *out, unsigned int olen)
{
	struct mlx5st_cmd_queue_entry *cmd = dev->cmd_lay;
	unsigned int out_remain, out_nblk;
	struct timespec start, now;
	unsigned int elapsed;
	unsigned int i;
	void *cout;

	mlx5st_cmd_post(dev, cmd, dev->cmd_in_mbox, dev->cmd_out_mbox, in,
			ilen, olen, 1);

	out_remain = olen > MLX5_CMD_INLINE_SZ ? olen - MLX5_CMD_INLINE_SZ : 0;
	out_nblk = (out_remain + MLX5_CMD_DATA_BLOCK_SIZE - 1) /
		   MLX5_CMD_DATA_BLOCK_SIZE;

	/* Poll for completion — also process EQ events for PF page requests */
	clock_gettime(CLOCK_MONOTONIC, &start);
	for (;;) {
		if (!MLX5_GET_ONCE(cmd_queue_entry, cmd, ownership))
			break;
		if (dev->have_eq)
			mlx5st_process_events(dev);
		sched_yield();
		clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed = (now.tv_sec - start.tv_sec) * 1000 +
			  (now.tv_nsec - start.tv_nsec) / 1000000;
		if (elapsed > MLX5_CMD_TIMEOUT_MS)
			VFIO_FAIL("cmd timeout after %d ms", elapsed);
	}
	/* Ensure output data reads happen after ownership is seen clear */
	dma_rmb();

	/* Verify output signatures when FW has checksums enabled */
	if (dev->cmd_sig_enabled) {
		if (xor8_buf(cmd, 0,
			     sizeof(struct mlx5st_cmd_queue_entry)) != 0xff)
			VFIO_FAIL("cmd output signature mismatch");
		for (i = 0; i < out_nblk; i++) {
			if (mlx5st_cmd_verify_block_sig(
				    &dev->cmd_out_mbox[i].block))
				VFIO_FAIL("cmd output mailbox block %d signature mismatch",
					  i);
		}
	}

	/* Copy output: inline first */
	cout = MLX5_ADDR_OF(cmd_queue_entry, cmd, command_output_inline_data);
	memcpy(out, cout, olen < MLX5_CMD_INLINE_SZ ? olen : MLX5_CMD_INLINE_SZ);

	/* Copy remaining from output mailbox chain */
	if (out_remain > 0)
		mlx5st_cmd_copy_from_mbox((u8 *)out + MLX5_CMD_INLINE_SZ,
					  dev->cmd_out_mbox, out_remain);

	/* Check command status */
	if (MLX5_GET(enable_hca_out, out, status) != MLX5_CMD_STAT_OK)
		VFIO_FAIL("%s: status=0x%x syndrome=0x%x",
			  mlx5st_cmd_name(MLX5_GET(enable_hca_in, in, opcode)),
			  MLX5_GET(enable_hca_out, out, status),
			  MLX5_GET(enable_hca_out, out, syndrome));
}

static struct mlx5st_cmd_queue_entry *
mlx5st_cmd_slot_init(struct mlx5st_device *dev, unsigned int slot,
		     struct mlx5st_mbox_entry *in_mbox,
		     struct mlx5st_mbox_entry *out_mbox)
{
	struct vfio_pci_device *device = dev->device;
	struct mlx5st_cmd_queue_entry *cmd =
		&dev->cmd_queue[(slot << dev->cmd_log_stride) /
				sizeof(struct mlx5st_cmd_queue_entry)];
	u64 iova;

	MLX5_SET(cmd_queue_entry, cmd, type,
		 MLX5_CMD_QUEUE_ENTRY_TYPE_PCIE_CMD_IF_TRANSPORT);
	iova = to_iova(device, in_mbox);
	MLX5_SET(cmd_queue_entry, cmd, input_mailbox_pointer_63_32,
		 iova >> 32);
	MLX5_SET(cmd_queue_entry, cmd, input_mailbox_pointer_31_9, iova >> 9);
	iova = to_iova(device, out_mbox);
	MLX5_SET(cmd_queue_entry, cmd, output_mailbox_pointer_63_32,
		 iova >> 32);
	MLX5_SET(cmd_queue_entry, cmd, output_mailbox_pointer_31_9,
		 iova >> 9);
	return cmd;
}

static void mlx5st_cmd_init(struct mlx5st_device *dev)
{
	struct mlx5st_initial_seg __iomem *seg = dev->init_seg;
	struct vfio_pci_device *device = dev->device;
	u16 cmdif_rev;
	u8 log_sz;
	u64 iova;

	cmdif_rev = MLX5_GET_MMIO(initial_seg, seg, cmd_interface_rev);
	VFIO_ASSERT_EQ(cmdif_rev, 5);

	/* Read command queue geometry from BAR */
	log_sz = MLX5_GET_MMIO(initial_seg, seg, log_cmdq_size);
	dev->cmd_log_stride = MLX5_GET_MMIO(initial_seg, seg, log_cmdq_stride);
	dev->pages_slot = (1 << log_sz) - 1;

	VFIO_ASSERT_LE((unsigned int)(1 << log_sz), 32u);
	VFIO_ASSERT_GE((unsigned int)(1 << dev->cmd_log_stride),
		       (unsigned int)sizeof(struct mlx5st_cmd_queue_entry));
	VFIO_ASSERT_LE((unsigned int)((dev->pages_slot + 1) <<
				      dev->cmd_log_stride),
		       (unsigned int)sizeof(dev->cmd_queue));

	/* Set up slot 0 — regular commands */
	dev->cmd_lay = mlx5st_cmd_slot_init(dev, 0, dev->cmd_in_mbox,
					    dev->cmd_out_mbox);

	/* Set up pages slot — async MANAGE_PAGES */
	dev->pages_cmd_lay = mlx5st_cmd_slot_init(dev, dev->pages_slot,
						  dev->pages_in_mbox,
						  dev->pages_out_mbox);

	/* Write command queue page address to BAR0 */
	iova = to_iova(device, dev->cmd_queue);
	MLX5_SET_MMIO(initial_seg, seg, cmdq_phy_addr_63_32, iova >> 32);
	MLX5_SET_MMIO(initial_seg, seg, cmdq_phy_addr_31_12, iova >> 12);

	dev_dbg(device,
		 "Command interface initialized (cmdif_rev=5, log_sz=%u, log_stride=%u, pages_slot=%u)\n",
		 log_sz, dev->cmd_log_stride, dev->pages_slot);
}

/*
 * FW pages: bitmap allocator + MANAGE_PAGES
 */

static void mlx5st_fw_pages_alloc(struct mlx5st_device *dev,
				   unsigned int npages, u64 *iovas)
{
	struct vfio_pci_device *device = dev->device;
	unsigned int found = 0;
	unsigned int w, b;
	u64 word;

	for (w = 0; w < MAX_FW_PAGES / 64 && found < npages; w++) {
		word = dev->fw_pages_bitmap[w];

		for (b = 0; b < 64 && found < npages; b++) {
			if (!(word & (1ULL << b))) {
				unsigned int idx = w * 64 + b;

				dev->fw_pages_bitmap[w] |= (1ULL << b);
				iovas[found++] = to_iova(device,
							 dev->fw_pages[idx]);
			}
		}
	}
	VFIO_ASSERT_EQ(found, npages);
	dev->fw_pages_given += npages;
}

static void mlx5st_fw_pages_free(struct mlx5st_device *dev,
				  unsigned int npages, const u64 *iovas)
{
	struct vfio_pci_device *device = dev->device;
	unsigned int i, idx;
	u64 off;

	for (i = 0; i < npages; i++) {
		off = iovas[i] - to_iova(device, dev->fw_pages);
		idx = off / MLX5_HW_PAGE_SIZE;

		VFIO_ASSERT_TRUE(idx < MAX_FW_PAGES);
		dev->fw_pages_bitmap[idx / 64] &= ~(1ULL << (idx % 64));
	}
	dev->fw_pages_given -= npages;
}

static void *mlx5st_build_manage_pages_give(u16 func_id, unsigned int npages,
					     const u64 *iovas,
					     unsigned int *out_inlen)
{
	unsigned int inlen = MLX5_ST_SZ_BYTES(manage_pages_in) + npages * 8;
	unsigned int i;
	void *in;

	in = calloc(1, inlen);
	VFIO_ASSERT_NOT_NULL(in);

	MLX5_SET(manage_pages_in, in, opcode, MLX5_CMD_OP_MANAGE_PAGES);
	MLX5_SET(manage_pages_in, in, op_mod,
		 MLX5_MANAGE_PAGES_IN_OP_MOD_ALLOCATION_SUCCESS);
	MLX5_SET(manage_pages_in, in, function_id, func_id);
	MLX5_SET(manage_pages_in, in, input_num_entries, npages);

	for (i = 0; i < npages; i++)
		MLX5_ARRAY_SET64(manage_pages_in, in, pas, i, iovas[i]);

	*out_inlen = inlen;
	return in;
}

static void mlx5st_fw_pages_give_one(struct mlx5st_device *dev, u16 func_id,
				      unsigned int npages, u64 *iovas)
{
	u32 out[MLX5_ST_SZ_DW(manage_pages_out)] = {};
	unsigned int inlen;
	void *in;

	in = mlx5st_build_manage_pages_give(func_id, npages, iovas, &inlen);
	mlx5st_cmd_exec(dev, in, inlen, out, sizeof(out));
	free(in);
}

static void mlx5st_fw_pages_give(struct mlx5st_device *dev, u16 func_id,
				  unsigned int npages)
{
	unsigned int remaining = npages;
	u64 *iovas;

	if (!npages)
		return;

	iovas = calloc(npages, sizeof(u64));
	VFIO_ASSERT_NOT_NULL(iovas);

	mlx5st_fw_pages_alloc(dev, npages, iovas);

	/* Batch into chunks that fit in one mailbox */
	for (unsigned int off = 0; remaining > 0;) {
		unsigned int batch = remaining < MAX_FW_PAGES_PER_CMD ?
					     remaining :
					     MAX_FW_PAGES_PER_CMD;

		mlx5st_fw_pages_give_one(dev, func_id, batch, iovas + off);
		off += batch;
		remaining -= batch;
	}

	dev_dbg(dev->device, "MANAGE_PAGES GIVE: %d pages to func_id=%u\n",
		 npages, func_id);
	free(iovas);
}

static void mlx5st_fw_pages_satisfy(struct mlx5st_device *dev, int boot)
{
	u32 qo[MLX5_ST_SZ_DW(query_pages_out)] = {};
	u32 qi[MLX5_ST_SZ_DW(query_pages_in)] = {};
	u16 func_id;
	int npages;

	MLX5_SET(query_pages_in, qi, opcode, MLX5_CMD_OP_QUERY_PAGES);
	MLX5_SET(query_pages_in, qi, op_mod, boot ? 0x01 : 0x02);
	mlx5st_cmd_exec(dev, qi, sizeof(qi), qo, sizeof(qo));

	npages = MLX5_GET(query_pages_out, qo, num_pages);
	func_id = MLX5_GET(query_pages_out, qo, function_id);
	dev_dbg(dev->device, "QUERY_PAGES (%s): %d pages (func_id=%u)\n",
		 boot ? "boot" : "init", npages, func_id);

	if (npages > 0) {
		dev->fw_func_id = func_id;
		mlx5st_fw_pages_give(dev, func_id, npages);
	}
}

/*
 * Async MANAGE_PAGES on the pages command slot.
 *
 * On PFs, firmware sends PAGE_REQUEST events via the EQ during command
 * execution.  We must respond with MANAGE_PAGES on a second command slot
 * before the first (regular) command can complete.
 */

static void mlx5st_pages_slot_post(struct mlx5st_device *dev, void *in,
				    unsigned int ilen, unsigned int olen)
{
	mlx5st_cmd_post(dev, dev->pages_cmd_lay, dev->pages_in_mbox,
			dev->pages_out_mbox, in, ilen, olen,
			1 << dev->pages_slot);
}

static void mlx5st_pages_slot_give(struct mlx5st_device *dev, u16 func_id,
				    unsigned int npages)
{
	unsigned int inlen;
	u64 *iovas;
	void *in;

	iovas = calloc(npages, sizeof(u64));
	VFIO_ASSERT_NOT_NULL(iovas);

	mlx5st_fw_pages_alloc(dev, npages, iovas);

	in = mlx5st_build_manage_pages_give(func_id, npages, iovas, &inlen);
	free(iovas);

	mlx5st_pages_slot_post(dev, in, inlen,
			       MLX5_ST_SZ_BYTES(manage_pages_out));
	dev->pages_slot_in_use = true;
	dev->pages_slot_is_reclaim = false;
	free(in);

	dev_dbg(dev->device,
		 "PAGE_REQUEST: %d pages given async to func_id=%u\n",
		 npages, func_id);
}

static void mlx5st_pages_slot_reclaim(struct mlx5st_device *dev, u16 func_id,
				       unsigned int npages)
{
	unsigned int inlen = MLX5_ST_SZ_BYTES(manage_pages_in);
	unsigned int outlen =
		MLX5_ST_SZ_BYTES(manage_pages_out) + npages * 8;
	void *in;

	in = calloc(1, inlen);
	VFIO_ASSERT_NOT_NULL(in);

	MLX5_SET(manage_pages_in, in, opcode, MLX5_CMD_OP_MANAGE_PAGES);
	MLX5_SET(manage_pages_in, in, op_mod,
		 MLX5_MANAGE_PAGES_IN_OP_MOD_HCA_RETURN_PAGES);
	MLX5_SET(manage_pages_in, in, function_id, func_id);
	MLX5_SET(manage_pages_in, in, input_num_entries, npages);

	mlx5st_pages_slot_post(dev, in, inlen, outlen);
	dev->pages_slot_in_use = true;
	dev->pages_slot_is_reclaim = true;
	dev->pages_reclaim_npages = npages;
	free(in);

	dev_dbg(dev->device,
		 "PAGE_REQUEST: reclaim %d pages async from func_id=%u\n",
		 npages, func_id);
}

static void mlx5st_pages_slot_kick(struct mlx5st_device *dev)
{
	unsigned int batch;

	if (dev->pages_slot_in_use)
		return;

	if (dev->pages_pending_give) {
		batch = dev->pages_pending_give < MAX_FW_PAGES_PER_CMD ?
				dev->pages_pending_give :
				MAX_FW_PAGES_PER_CMD;
		dev->pages_pending_give -= batch;
		mlx5st_pages_slot_give(dev, dev->pages_pending_func_id, batch);
	} else if (dev->pages_pending_reclaim) {
		batch = dev->pages_pending_reclaim < MAX_FW_PAGES_PER_CMD ?
				dev->pages_pending_reclaim :
				MAX_FW_PAGES_PER_CMD;
		dev->pages_pending_reclaim -= batch;
		mlx5st_pages_slot_reclaim(dev, dev->pages_pending_func_id,
					  batch);
	}
}

static void mlx5st_fw_pages_give_async(struct mlx5st_device *dev,
					u16 func_id, unsigned int npages)
{
	if (!npages)
		return;

	dev->pages_pending_give += npages;
	dev->pages_pending_func_id = func_id;
	mlx5st_pages_slot_kick(dev);
}

static void mlx5st_fw_pages_reclaim_async(struct mlx5st_device *dev,
					   u16 func_id, unsigned int npages)
{
	dev->pages_pending_reclaim += npages;
	dev->pages_pending_func_id = func_id;
	mlx5st_pages_slot_kick(dev);
}

static void mlx5st_pages_slot_complete(struct mlx5st_device *dev)
{
	struct mlx5st_cmd_queue_entry *cmd = dev->pages_cmd_lay;
	void *cout;

	dma_rmb();

	cout = MLX5_ADDR_OF(cmd_queue_entry, cmd, command_output_inline_data);
	if (MLX5_GET(enable_hca_out, cout, status) != MLX5_CMD_STAT_OK)
		VFIO_FAIL("async MANAGE_PAGES failed: status=0x%x syndrome=0x%x",
			  MLX5_GET(enable_hca_out, cout, status),
			  MLX5_GET(enable_hca_out, cout, syndrome));

	if (dev->pages_slot_is_reclaim) {
		unsigned int outlen = MLX5_ST_SZ_BYTES(manage_pages_out) +
				      dev->pages_reclaim_npages * 8;
		unsigned int num_claimed;
		unsigned int i;
		void *out;
		u64 *iovas;

		out = calloc(1, outlen);
		iovas = calloc(dev->pages_reclaim_npages, sizeof(u64));
		VFIO_ASSERT_NOT_NULL(out);
		VFIO_ASSERT_NOT_NULL(iovas);

		/* Copy inline output */
		memcpy(out, cout, MLX5_CMD_INLINE_SZ);
		if (outlen > MLX5_CMD_INLINE_SZ)
			mlx5st_cmd_copy_from_mbox(
				(u8 *)out + MLX5_CMD_INLINE_SZ,
				dev->pages_out_mbox,
				outlen - MLX5_CMD_INLINE_SZ);

		num_claimed =
			MLX5_GET(manage_pages_out, out, output_num_entries);
		for (i = 0; i < num_claimed; i++)
			iovas[i] = MLX5_ARRAY_GET64(manage_pages_out, out, pas,
						    i);

		mlx5st_fw_pages_free(dev, num_claimed, iovas);
		dev_dbg(dev->device, "PAGE_REQUEST: reclaimed %d pages\n",
			 num_claimed);

		free(iovas);
		free(out);
	}

	dev->pages_slot_in_use = false;
	mlx5st_pages_slot_kick(dev);
}

/*
 * UAR alloc/dealloc
 */

static void mlx5st_alloc_uar(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(alloc_uar_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_uar_in)] = {};

	MLX5_SET(alloc_uar_in, in, opcode, MLX5_CMD_OP_ALLOC_UAR);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	dev->uar_page = MLX5_GET(alloc_uar_out, out, uar);
	dev->uar_base = (u8 __iomem*)dev->bar0 + dev->uar_page * MLX5_HW_PAGE_SIZE;
	dev->uar_bf_offset = MLX5_BF_OFFSET;

	dev_dbg(dev->device,
		 "Allocated UAR page_id=%u, doorbell offset=0x%x\n",
		 dev->uar_page,
		 dev->uar_page * MLX5_HW_PAGE_SIZE + MLX5_BF_OFFSET);
}

static void mlx5st_dealloc_uar(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(dealloc_uar_out)] = {};
	u32 in[MLX5_ST_SZ_DW(dealloc_uar_in)] = {};

	MLX5_SET(dealloc_uar_in, in, opcode, MLX5_CMD_OP_DEALLOC_UAR);
	MLX5_SET(dealloc_uar_in, in, uar, dev->uar_page);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

/*
 * EQ infrastructure
 */

static struct mlx5st_eqe *mlx5st_eq_get_eqe(struct mlx5st_device *dev, u32 cc)
{
	u32 ci = dev->eq_cons_index + cc;
	struct mlx5st_eqe *eqe = &dev->eq_buf[ci % EQ_NENT];
	u8 owner = MLX5_GET_ONCE(eqe, eqe, owner);
	u8 expected = !!(ci & EQ_NENT);

	if (owner != expected)
		return NULL;
	dma_rmb();
	return eqe;
}

static void mlx5st_eq_update_ci(struct mlx5st_device *dev, u32 cc, bool arm)
{
	u32 val;

	dev->eq_cons_index += cc;
	val = (dev->eq_cons_index & 0xffffff) | (dev->eqn << 24);
	iowrite32be(val, (u8 __iomem *)dev->uar_base + MLX5_EQ_DOORBELL_OFFSET +
				 (arm ? 0 : 8));
}

static void mlx5st_create_eq(struct mlx5st_device *dev)
{
	struct vfio_pci_device *device = dev->device;
	u64 in[MLX5_ST_SZ_QW(create_eq_in) + 1] = {};
	u32 out[MLX5_ST_SZ_DW(create_eq_out)] = {};
	struct mlx5_ifc_eqc_bits *eqc;
	unsigned int i;
	__be64 *pas;

	/* Initialize EQE owner bits */
	for (i = 0; i < EQ_NENT; i++) {
		struct mlx5st_eqe *eqe = &dev->eq_buf[i];

		MLX5_SET_ONCE(eqe, eqe, owner, 1);
	}

	MLX5_SET(create_eq_in, in, opcode, MLX5_CMD_OP_CREATE_EQ);

	/* Subscribe to CMD completions and PAGE_REQUEST events */
	MLX5_ARRAY_SET64(create_eq_in, in, event_bitmask, 0,
			 (1ULL << MLX5_EVENT_TYPE_CMD) |
				 (1ULL << MLX5_EVENT_TYPE_PAGE_REQUEST));

	eqc = MLX5_ADDR_OF(create_eq_in, in, eq_context_entry);
	MLX5_SET(eqc, eqc, log_eq_size, LOG_EQ_SIZE);
	MLX5_SET(eqc, eqc, uar_page, dev->uar_page);
	pas = MLX5_ADDR_OF(create_eq_in, in, pas);
	VFIO_ASSERT_EQ(mlx5st_fill_pas(device, dev->eq_buf, pas), 0u);
	MLX5_SET(eqc, eqc, log_page_size, 0);

	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	dev->eqn = MLX5_GET(create_eq_out, out, eq_number);
	dev->eq_cons_index = 0;
	mlx5st_eq_update_ci(dev, 0, 0);
	dev->have_eq = true;

	dev_dbg(device, "Created EQ: eqn=%u, %d entries (CMD+PAGE_REQUEST)\n",
		 dev->eqn, EQ_NENT);
}

static void mlx5st_destroy_eq(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(destroy_eq_out)] = {};
	u32 in[MLX5_ST_SZ_DW(destroy_eq_in)] = {};

	MLX5_SET(destroy_eq_in, in, opcode, MLX5_CMD_OP_DESTROY_EQ);
	MLX5_SET(destroy_eq_in, in, eq_number, dev->eqn);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

/*
 * Drain all pending EQ events.  Dispatches PAGE_REQUEST to the async pages
 * slot and CMD completions to the pages slot completion handler.
 */
static void mlx5st_process_events(struct mlx5st_device *dev)
{
	struct mlx5st_eqe *eqe;
	u32 cc = 0;

	while ((eqe = mlx5st_eq_get_eqe(dev, cc))) {
		u8 type = MLX5_GET(eqe, eqe, event_type);

		switch (type) {
		case MLX5_EVENT_TYPE_PAGE_REQUEST: {
			void *evdata = MLX5_ADDR_OF(eqe, eqe, event_data);
			u16 func_id = MLX5_GET(pages_req_event, evdata,
					       function_id);
			s32 npages = (s32)MLX5_GET(pages_req_event, evdata,
						   num_pages);

			/*
			 * The selftest doesn't use more than one func_id so a
			 * simple counter approach is possible.
			 */
			if (dev->pages_func_id_seen)
				VFIO_ASSERT_EQ(func_id,
					       dev->pages_pending_func_id);
			dev->pages_func_id_seen = true;

			if (npages > 0)
				mlx5st_fw_pages_give_async(dev, func_id,
							   npages);
			else if (npages < 0)
				mlx5st_fw_pages_reclaim_async(dev, func_id,
							      -npages);
			break;
		}
		case MLX5_EVENT_TYPE_CMD: {
			void *evdata = MLX5_ADDR_OF(eqe, eqe, event_data);
			u32 vector = MLX5_GET(cmd_inter_comp_event, evdata,
					      command_completion_vector);

			if (vector & (1U << dev->pages_slot))
				mlx5st_pages_slot_complete(dev);
			break;
		}
		default:
			break;
		}
		cc++;
	}

	if (cc)
		mlx5st_eq_update_ci(dev, cc, 0);
}

/*
 * MSI EQ — dedicated EQ for CQ completion events that fires MSI-X.
 * Separate from the cmd/pages EQ so that only CQ completions (from
 * send_msi or memcpy) trigger the interrupt vector.
 */

static void mlx5st_msi_eq_drain(struct mlx5st_device *dev)
{
	u32 cc = 0;
	u32 val;

	while (cc < MSI_EQ_NENT) {
		u32 ci = dev->msi_eq_cons_index + cc;
		struct mlx5st_eqe *eqe =
			&dev->msi_eq_buf[ci % MSI_EQ_NENT];

		if (MLX5_GET_ONCE(eqe, eqe, owner) != !!(ci & MSI_EQ_NENT))
			break;
		cc++;
	}

	/* Update consumer index and re-arm for next interrupt */
	dev->msi_eq_cons_index += cc;
	val = (dev->msi_eq_cons_index & 0xffffff) | (dev->msi_eqn << 24);
	iowrite32be(val, (u8 __iomem *)dev->uar_base + MLX5_EQ_DOORBELL_OFFSET);
}

static void mlx5st_create_msi_eq(struct mlx5st_device *dev)
{
	struct vfio_pci_device *device = dev->device;
	u64 in[MLX5_ST_SZ_QW(create_eq_in) + 1] = {};
	u32 out[MLX5_ST_SZ_DW(create_eq_out)] = {};
	struct mlx5_ifc_eqc_bits *eqc;
	unsigned int i;
	__be64 *pas;

	/* Initialize EQE owner bits */
	for (i = 0; i < MSI_EQ_NENT; i++) {
		struct mlx5st_eqe *eqe = &dev->msi_eq_buf[i];

		MLX5_SET_ONCE(eqe, eqe, owner, 1);
	}

	MLX5_SET(create_eq_in, in, opcode, MLX5_CMD_OP_CREATE_EQ);

	/*
	 * No event_bitmask — completion events are routed to this EQ via
	 * the CQ's c_eqn field, not through CREATE_EQ subscription.
	 */
	eqc = MLX5_ADDR_OF(create_eq_in, in, eq_context_entry);
	MLX5_SET(eqc, eqc, log_eq_size, LOG_MSI_EQ_SIZE);
	MLX5_SET(eqc, eqc, uar_page, dev->uar_page);
	MLX5_SET(eqc, eqc, intr, MSI_VECTOR);
	pas = MLX5_ADDR_OF(create_eq_in, in, pas);
	VFIO_ASSERT_EQ(mlx5st_fill_pas(device, dev->msi_eq_buf, pas), 0u);
	MLX5_SET(eqc, eqc, log_page_size, 0);

	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	dev->msi_eqn = MLX5_GET(create_eq_out, out, eq_number);
	dev->msi_eq_cons_index = 0;
	dev->have_msi_eq = true;
	mlx5st_msi_eq_drain(dev);

	dev_dbg(device,
		 "Created MSI EQ: eqn=%u, %d entries (COMP), vector=%d\n",
		 dev->msi_eqn, MSI_EQ_NENT, MSI_VECTOR);
}

static void mlx5st_destroy_msi_eq(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(destroy_eq_out)] = {};
	u32 in[MLX5_ST_SZ_DW(destroy_eq_in)] = {};

	MLX5_SET(destroy_eq_in, in, opcode, MLX5_CMD_OP_DESTROY_EQ);
	MLX5_SET(destroy_eq_in, in, eq_number, dev->msi_eqn);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

/*
 * HCA init / teardown
 */

#define FW_INIT_TIMEOUT_MS 120000
#define FW_INIT_WAIT_MS 200

static void mlx5st_wait_fw_init(struct mlx5st_device *dev)
{
	struct timespec start, now;
	unsigned int elapsed;

	clock_gettime(CLOCK_MONOTONIC, &start);
	while (MLX5_GET_MMIO(initial_seg, dev->init_seg, initializing)) {
		usleep(FW_INIT_WAIT_MS * 1000);
		clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed = (now.tv_sec - start.tv_sec) * 1000 +
			  (now.tv_nsec - start.tv_nsec) / 1000000;
		if (elapsed > FW_INIT_TIMEOUT_MS)
			VFIO_FAIL("FW init timeout after %d ms", elapsed);
	}
}

static void mlx5st_set_issi(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(set_issi_out)] = {};
	u32 in[MLX5_ST_SZ_DW(set_issi_in)] = {};

	MLX5_SET(set_issi_in, in, opcode, MLX5_CMD_OP_SET_ISSI);
	MLX5_SET(set_issi_in, in, current_issi, 1);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	dev_dbg(dev->device, "SET_ISSI: OK (issi=1)\n");
}

static void mlx5st_set_hca_caps(struct mlx5st_device *dev)
{
	u32 qout[MLX5_ST_SZ_DW(query_hca_cap_out)] = {};
	u32 qin[MLX5_ST_SZ_DW(query_hca_cap_in)] = {};
	u32 sout[MLX5_ST_SZ_DW(set_hca_cap_out)] = {};
	u32 sin[MLX5_ST_SZ_DW(set_hca_cap_in)] = {};
	struct mlx5_ifc_cmd_hca_cap_bits *set_hca_cap;
	u32 max_checksum;

	/* Query max caps to learn cmdif_checksum support */
	MLX5_SET(query_hca_cap_in, qin, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, qin, op_mod,
		 (MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE << 1));
	mlx5st_cmd_exec(dev, qin, sizeof(qin), qout, sizeof(qout));

	max_checksum = MLX5_GET(
		cmd_hca_cap,
		MLX5_ADDR_OF(query_hca_cap_out, qout, capability),
		cmdif_checksum);

	/* Query current caps as base for SET */
	memset(qout, 0, sizeof(qout));
	MLX5_SET(query_hca_cap_in, qin, op_mod,
		 (MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE << 1) |
			 HCA_CAP_OPMOD_GET_CUR);
	mlx5st_cmd_exec(dev, qin, sizeof(qin), qout, sizeof(qout));

	set_hca_cap = MLX5_ADDR_OF(set_hca_cap_in, sin, capability);
	memcpy(set_hca_cap,
	       MLX5_ADDR_OF(query_hca_cap_out, qout, capability),
	       MLX5_ST_SZ_BYTES(cmd_hca_cap));

	MLX5_SET(cmd_hca_cap, set_hca_cap, cmdif_checksum, max_checksum);
	MLX5_SET(cmd_hca_cap, set_hca_cap, log_uar_page_sz, 0);

	MLX5_SET(set_hca_cap_in, sin, opcode, MLX5_CMD_OP_SET_HCA_CAP);
	MLX5_SET(set_hca_cap_in, sin, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE << 1);

	mlx5st_cmd_exec(dev, sin, sizeof(sin), sout, sizeof(sout));

	dev->cmd_sig_enabled = max_checksum == 0x3;
	dev_dbg(dev->device, "SET_HCA_CAP: OK (cmdif_checksum=%u)\n",
		 max_checksum);
}

static void mlx5st_hca_init(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(enable_hca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(enable_hca_in)] = {};

	mlx5st_wait_fw_init(dev);
	dev_dbg(dev->device, "Firmware ready\n");

	MLX5_SET(enable_hca_in, in, opcode, MLX5_CMD_OP_ENABLE_HCA);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	dev_dbg(dev->device, "ENABLE_HCA: OK\n");

	mlx5st_set_issi(dev);
	mlx5st_fw_pages_satisfy(dev, 1);

	mlx5st_set_hca_caps(dev);
	mlx5st_fw_pages_satisfy(dev, 0);

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));
	MLX5_SET(init_hca_in, in, opcode, MLX5_CMD_OP_INIT_HCA);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	dev_dbg(dev->device, "INIT_HCA: OK\n");

	/*
	 * Create EQ immediately after INIT_HCA so PAGE_REQUEST events
	 * are captured during all subsequent commands.
	 */
	mlx5st_alloc_uar(dev);
	mlx5st_create_eq(dev);
}

static void mlx5st_disable_hca(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(disable_hca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(disable_hca_in)] = {};

	MLX5_SET(disable_hca_in, in, opcode, MLX5_CMD_OP_DISABLE_HCA);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static void mlx5st_fw_pages_reclaim(struct mlx5st_device *dev, u16 func_id)
{
	unsigned int npages = dev->fw_pages_given;
	unsigned int total_claimed = 0;

	while (npages > 0) {
		unsigned int batch = npages < MAX_FW_PAGES_PER_CMD ?
					     npages :
					     MAX_FW_PAGES_PER_CMD;
		unsigned int outlen =
			MLX5_ST_SZ_BYTES(manage_pages_out) + batch * 8;
		unsigned int inlen = MLX5_ST_SZ_BYTES(manage_pages_in);
		unsigned int num_claimed;
		unsigned int i;
		void *in, *out;
		u64 *iovas;

		in = calloc(1, inlen);
		out = calloc(1, outlen);
		iovas = calloc(batch, sizeof(u64));
		VFIO_ASSERT_NOT_NULL(in);
		VFIO_ASSERT_NOT_NULL(out);
		VFIO_ASSERT_NOT_NULL(iovas);

		MLX5_SET(manage_pages_in, in, opcode,
			 MLX5_CMD_OP_MANAGE_PAGES);
		MLX5_SET(manage_pages_in, in, op_mod,
			 MLX5_MANAGE_PAGES_IN_OP_MOD_HCA_RETURN_PAGES);
		MLX5_SET(manage_pages_in, in, function_id, func_id);
		MLX5_SET(manage_pages_in, in, input_num_entries, batch);

		mlx5st_cmd_exec(dev, in, inlen, out, outlen);

		num_claimed =
			MLX5_GET(manage_pages_out, out, output_num_entries);
		for (i = 0; i < num_claimed; i++)
			iovas[i] = MLX5_ARRAY_GET64(manage_pages_out, out, pas,
						    i);

		mlx5st_fw_pages_free(dev, num_claimed, iovas);
		total_claimed += num_claimed;
		npages -= num_claimed;

		free(iovas);
		free(in);
		free(out);

		if (!num_claimed && !dev->fw_pages_given)
			break;
		if (!num_claimed)
			VFIO_FAIL("MANAGE_PAGES RECLAIM: FW returned 0 but %d pages still given",
				  dev->fw_pages_given);
	}

	dev_dbg(dev->device,
		 "MANAGE_PAGES RECLAIM: %d pages (%d still given)\n",
		 total_claimed, dev->fw_pages_given);
}

static void mlx5st_hca_teardown(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(teardown_hca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(teardown_hca_in)] = {};

	/* Drain async pages slot, then stop EQ processing */
	while (dev->pages_slot_in_use) {
		if (!MLX5_GET_ONCE(cmd_queue_entry, dev->pages_cmd_lay,
				   ownership))
			mlx5st_pages_slot_complete(dev);
		else
			sched_yield();
	}
	dev->have_eq = false;

	if (dev->eqn) {
		mlx5st_destroy_eq(dev);
		dev->eqn = 0;
	}
	if (dev->uar_page) {
		mlx5st_dealloc_uar(dev);
		dev->uar_page = 0;
	}

	dev_dbg(dev->device, "  hca_teardown: TEARDOWN_HCA\n");
	MLX5_SET(teardown_hca_in, in, opcode, MLX5_CMD_OP_TEARDOWN_HCA);
	MLX5_SET(teardown_hca_in, in, profile,
		 MLX5_TEARDOWN_HCA_IN_PROFILE_GRACEFUL_CLOSE);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	if (dev->fw_pages_given > 0) {
		dev_dbg(dev->device, "  hca_teardown: reclaim %d pages\n",
			 dev->fw_pages_given);
		mlx5st_fw_pages_reclaim(dev, dev->fw_func_id);
	}

	dev_dbg(dev->device, "  hca_teardown: DISABLE_HCA\n");
	mlx5st_disable_hca(dev);
}

/*
 * Query capabilities
 */
static void mlx5st_query_fl_caps(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(query_hca_cap_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_hca_cap_in)] = {};
	bool fl_roce_en, fl_roce_dis;

	/* Query RoCE capabilities */
	MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, in, op_mod,
		 (MLX5_SET_HCA_CAP_OP_MOD_ROCE << 1) | HCA_CAP_OPMOD_GET_CUR);

	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	fl_roce_en = MLX5_GET(query_hca_cap_out, out,
			      capability.roce_cap.fl_rc_qp_when_roce_enabled);
	fl_roce_dis = MLX5_GET(query_hca_cap_out, out,
			       capability.roce_cap.fl_rc_qp_when_roce_disabled);

	/* Also check general caps */
	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));
	MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	MLX5_SET(query_hca_cap_in, in, op_mod,
		 (MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE << 1) |
			 HCA_CAP_OPMOD_GET_CUR);

	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	fl_roce_dis |=
		MLX5_GET(query_hca_cap_out, out,
			 capability.cmd_hca_cap.fl_rc_qp_when_roce_disabled);

	dev->fl_supported = fl_roce_en || fl_roce_dis;
	dev->log_max_msg = MLX5_GET(query_hca_cap_out, out,
				    capability.cmd_hca_cap.log_max_msg);
	dev_dbg(dev->device,
		 "HCA capabilities: fl_roce_enabled=%d fl_roce_disabled=%d log_max_msg=%u\n",
		 fl_roce_en, fl_roce_dis, dev->log_max_msg);

	VFIO_ASSERT_TRUE(dev->fl_supported,
			 "Force-loopback not supported on this device");
}

/*
 * Resource allocation
 */

static void mlx5st_alloc_pd(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(alloc_pd_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_pd_in)] = {};

	MLX5_SET(alloc_pd_in, in, opcode, MLX5_CMD_OP_ALLOC_PD);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	dev->pdn = MLX5_GET(alloc_pd_out, out, pd);
	dev_dbg(dev->device, "Allocated PD pdn=%u\n", dev->pdn);
}

static void mlx5st_dealloc_pd(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(dealloc_pd_out)] = {};
	u32 in[MLX5_ST_SZ_DW(dealloc_pd_in)] = {};

	MLX5_SET(dealloc_pd_in, in, opcode, MLX5_CMD_OP_DEALLOC_PD);
	MLX5_SET(dealloc_pd_in, in, pd, dev->pdn);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

static void mlx5st_create_mkey(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(create_mkey_out)] = {};
	u32 in[MLX5_ST_SZ_DW(create_mkey_in)] = {};
	struct mlx5_ifc_mkc_bits *mkc;

	MLX5_SET(create_mkey_in, in, opcode, MLX5_CMD_OP_CREATE_MKEY);

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_PA);
	MLX5_SET(mkc, mkc, length64, 1);
	MLX5_SET(mkc, mkc, pd, dev->pdn);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, lr, 1);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, rw, 1);
	MLX5_SET(mkc, mkc, rr, 1);

	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	dev->mkey_index = MLX5_GET(create_mkey_out, out, mkey_index);
	dev->global_lkey = mlx5st_idx_to_mkey(dev->mkey_index);
	dev->global_rkey = dev->global_lkey;

	dev_dbg(dev->device, "Created global PA-mode MKEY: lkey=0x%x\n",
		 dev->global_lkey);
}

static void mlx5st_destroy_mkey(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(destroy_mkey_out)] = {};
	u32 in[MLX5_ST_SZ_DW(destroy_mkey_in)] = {};

	MLX5_SET(destroy_mkey_in, in, opcode, MLX5_CMD_OP_DESTROY_MKEY);
	MLX5_SET(destroy_mkey_in, in, mkey_index, dev->mkey_index);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

/*
 * CQ create/destroy
 */

static void mlx5st_create_cq(struct mlx5st_device *dev)
{
	struct vfio_pci_device *device = dev->device;
	u64 in[MLX5_ST_SZ_QW(create_cq_in) + 1] = {};
	u32 out[MLX5_ST_SZ_DW(create_cq_out)] = {};
	struct mlx5_ifc_cqc_bits *cqc;
	unsigned int i;
	__be64 *pas;

	/* Initialize CQEs before CREATE_CQ: opcode=0xF, owner=1 */
	for (i = 0; i < CQ_CQE_CNT; i++) {
		struct mlx5st_cqe64 *cqe = &dev->cq_buf[i];

		MLX5_SET(cqe64, cqe, opcode, 0xF);
		MLX5_SET_ONCE(cqe64, cqe, owner, 1);
	}

	MLX5_SET(create_cq_in, in, opcode, MLX5_CMD_OP_CREATE_CQ);

	cqc = MLX5_ADDR_OF(create_cq_in, in, cq_context);
	MLX5_SET(cqc, cqc, log_cq_size, LOG_CQ_SIZE);
	MLX5_SET(cqc, cqc, uar_page, dev->uar_page);
	MLX5_SET(cqc, cqc, c_eqn_or_apu_element, dev->msi_eqn);
	MLX5_SET(cqc, cqc, cqe_sz, 0);
	pas = MLX5_ADDR_OF(create_cq_in, in, pas);
	MLX5_SET(cqc, cqc, page_offset, mlx5st_fill_pas(device, dev->cq_buf, pas));
	MLX5_SET(cqc, cqc, log_page_size, 0);
	MLX5_SET64(cqc, cqc, dbr_addr, to_iova(device, &dev->cq_dbrec));

	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	dev->cqn = MLX5_GET(create_cq_out, out, cqn);
	dev->cq_ci = 0;
	dev_dbg(device, "Created CQ: cqn=%u, %d entries\n", dev->cqn,
		 CQ_CQE_CNT);
}

static void mlx5st_destroy_cq(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(destroy_cq_out)] = {};
	u32 in[MLX5_ST_SZ_DW(destroy_cq_in)] = {};

	MLX5_SET(destroy_cq_in, in, opcode, MLX5_CMD_OP_DESTROY_CQ);
	MLX5_SET(destroy_cq_in, in, cqn, dev->cqn);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

/*
 * Arm CQ for event generation.  The CQ event delivery state machine is
 * single-shot: after generating one EQE the CQ enters "Fired" state and
 * won't generate another until re-armed via ARM_NEXT.  Both the CQ doorbell
 * record and the UAR CQ doorbell register must be written.
 */
static void mlx5st_arm_cq(struct mlx5st_device *dev)
{
	u32 sn = dev->cq_arm_sn & 3;
	u32 ci = dev->cq_ci & 0xffffff;
	u64 doorbell;

	/* Update CQ doorbell record arm word */
	WRITE_ONCE(dev->cq_dbrec.send_counter,
		   cpu_to_be32(sn << 28 | ci));

	/* Ring CQ doorbell register, iowrite has an internal dma_wmb() */
	doorbell = ((u64)(sn << 28 | ci) << 32) | dev->cqn;
	iowrite64be(doorbell,
		    (u8 __iomem *)dev->uar_base + MLX5_CQ_DOORBELL_OFFSET);

	dev->cq_arm_sn++;
}

/*
 * QP create/destroy
 */

static void mlx5st_create_qp(struct mlx5st_device *dev)
{
	struct vfio_pci_device *device = dev->device;
	u64 in[MLX5_ST_SZ_QW(create_qp_in) + 1] = {};
	u32 out[MLX5_ST_SZ_DW(create_qp_out)] = {};
	struct mlx5_ifc_qpc_bits *qpc;
	__be64 *pas;

	MLX5_SET(create_qp_in, in, opcode, MLX5_CMD_OP_CREATE_QP);

	qpc = MLX5_ADDR_OF(create_qp_in, in, qpc);
	MLX5_SET(qpc, qpc, st, MLX5_QPC_ST_RC);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QPC_PM_STATE_MIGRATED);
	MLX5_SET(qpc, qpc, pd, dev->pdn);
	MLX5_SET(qpc, qpc, uar_page, dev->uar_page);
	MLX5_SET(qpc, qpc, cqn_snd, dev->cqn);
	MLX5_SET(qpc, qpc, cqn_rcv, dev->cqn);
	MLX5_SET(qpc, qpc, log_sq_size, LOG_SQ_SIZE);
	MLX5_SET(qpc, qpc, log_msg_max, dev->log_max_msg);
	MLX5_SET(qpc, qpc, rq_type, 0x3);
	MLX5_SET(qpc, qpc, ts_format, 1);
	pas = MLX5_ADDR_OF(create_qp_in, in, pas);
	MLX5_SET(qpc, qpc, page_offset,
		 mlx5st_fill_pas(device, dev->sq_buf, pas));
	MLX5_SET(qpc, qpc, log_page_size, 0);
	MLX5_SET64(qpc, qpc, dbr_addr, to_iova(device, &dev->qp_dbrec));

	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));

	dev->qpn = MLX5_GET(create_qp_out, out, qpn);
	dev->sq_pi = 0;
	dev_dbg(device, "Created QP: qpn=%u, RC, sq=%d wqes\n", dev->qpn,
		 SQ_WQE_CNT);
}

static void mlx5st_destroy_qp(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(destroy_qp_out)] = {};
	u32 in[MLX5_ST_SZ_DW(destroy_qp_in)] = {};

	MLX5_SET(destroy_qp_in, in, opcode, MLX5_CMD_OP_DESTROY_QP);
	MLX5_SET(destroy_qp_in, in, qpn, dev->qpn);
	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

/*
 * QP state transitions
 */

static void mlx5st_qp_rst2init(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(rst2init_qp_out)] = {};
	u32 in[MLX5_ST_SZ_DW(rst2init_qp_in)] = {};
	struct mlx5_ifc_qpc_bits *qpc = MLX5_ADDR_OF(rst2init_qp_in, in, qpc);

	MLX5_SET(rst2init_qp_in, in, opcode, MLX5_CMD_OP_RST2INIT_QP);
	MLX5_SET(rst2init_qp_in, in, qpn, dev->qpn);

	MLX5_SET(qpc, qpc, primary_address_path.vhca_port_num, 1);
	MLX5_SET(qpc, qpc, pm_state, MLX5_QPC_PM_STATE_MIGRATED);
	MLX5_SET(qpc, qpc, rre, 1);
	MLX5_SET(qpc, qpc, rwe, 1);

	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	dev_dbg(dev->device, "QP RST->INIT\n");
}

static void mlx5st_qp_init2rtr(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(init2rtr_qp_out)] = {};
	u32 in[MLX5_ST_SZ_DW(init2rtr_qp_in)] = {};
	struct mlx5_ifc_qpc_bits *qpc = MLX5_ADDR_OF(init2rtr_qp_in, in, qpc);

	MLX5_SET(init2rtr_qp_in, in, opcode, MLX5_CMD_OP_INIT2RTR_QP);
	MLX5_SET(init2rtr_qp_in, in, qpn, dev->qpn);

	MLX5_SET(qpc, qpc, mtu, 3);
	MLX5_SET(qpc, qpc, log_msg_max, dev->log_max_msg);
	MLX5_SET(qpc, qpc, remote_qpn, dev->qpn);
	MLX5_SET(qpc, qpc, min_rnr_nak, 12);
	MLX5_SET(qpc, qpc, primary_address_path.vhca_port_num, 1);
	MLX5_SET(qpc, qpc, primary_address_path.fl, 1);

	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	dev_dbg(dev->device, "QP INIT->RTR (fl=1)\n");
}

static void mlx5st_qp_rtr2rts(struct mlx5st_device *dev)
{
	u32 out[MLX5_ST_SZ_DW(rtr2rts_qp_out)] = {};
	u32 in[MLX5_ST_SZ_DW(rtr2rts_qp_in)] = {};
	struct mlx5_ifc_qpc_bits *qpc = MLX5_ADDR_OF(rtr2rts_qp_in, in, qpc);

	MLX5_SET(rtr2rts_qp_in, in, opcode, MLX5_CMD_OP_RTR2RTS_QP);
	MLX5_SET(rtr2rts_qp_in, in, qpn, dev->qpn);

	MLX5_SET(qpc, qpc, log_ack_req_freq, 0);
	MLX5_SET(qpc, qpc, retry_count, 7);
	MLX5_SET(qpc, qpc, rnr_retry, 7);
	MLX5_SET(qpc, qpc, primary_address_path.ack_timeout, 14);

	mlx5st_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	dev_dbg(dev->device, "QP RTR->RTS\n");
}

/*
 * Post RDMA Write WQE
 */
static void mlx5st_post_rdma_write(struct mlx5st_device *dev, u64 src_addr,
				    u32 src_lkey, u64 dst_addr, u32 dst_rkey,
				    u32 length, bool signaled)
{
	struct mlx5st_send_wqe *wqe;
	unsigned int idx;

	idx = dev->sq_pi % SQ_WQE_CNT;
	wqe = &dev->sq_buf[idx];

	memset(wqe, 0, sizeof(*wqe));
	MLX5_SET(wqe_ctrl_seg, &wqe->ctrl, opcode, MLX5_OPCODE_RDMA_WRITE);
	MLX5_SET(wqe_ctrl_seg, &wqe->ctrl, wqe_index, dev->sq_pi);
	MLX5_SET(wqe_ctrl_seg, &wqe->ctrl, qp_or_sq, dev->qpn);
	MLX5_SET(wqe_ctrl_seg, &wqe->ctrl, ds, MLX5_RDMA_WRITE_DS);
	if (signaled)
		MLX5_SET(wqe_ctrl_seg, &wqe->ctrl, ce, MLX5_WQE_CE_CQE_ALWAYS);

	MLX5_SET64(wqe_raddr_seg, &wqe->raddr, raddr, dst_addr);
	MLX5_SET(wqe_raddr_seg, &wqe->raddr, rkey, dst_rkey);

	MLX5_SET(wqe_data_seg, &wqe->data, byte_count, length);
	MLX5_SET(wqe_data_seg, &wqe->data, lkey, src_lkey);
	MLX5_SET64(wqe_data_seg, &wqe->data, addr, src_addr);

	dev->sq_pi++;

	/* Ensure WQE is visible to device before doorbell record */
	dma_wmb();

	WRITE_ONCE(dev->qp_dbrec.send_counter,
		   cpu_to_be32(dev->sq_pi & 0xffff));

	/*
	 * Ring doorbell: write first 8 bytes of ctrl to UAR BF register,
	 * iowrite has an internal dma_wmb() so the doorbell record will be
	 * visible.
	 */
	iowrite64be(be64_to_cpu(*(__be64 *)wqe),
		    (u8 __iomem *)dev->uar_base + dev->uar_bf_offset);
	dev->uar_bf_offset ^= MLX5_BF_SIZE;
}

/*
 * Poll CQ
 */
static int mlx5st_poll_cq_batch(struct mlx5st_device *dev,
				unsigned int max_cqe)
{
	unsigned int polled = 0;

	while (polled < max_cqe) {
		unsigned int idx = dev->cq_ci % CQ_CQE_CNT;
		struct mlx5st_cqe64 *cqe = &dev->cq_buf[idx];
		u8 owner, opcode;

		owner = MLX5_GET_ONCE(cqe64, cqe, owner);
		if (owner != ((dev->cq_ci >> LOG_CQ_SIZE) & 1))
			break;

		dma_rmb();

		opcode = MLX5_GET(cqe64, cqe, opcode);

		dev->cq_ci++;
		WRITE_ONCE(dev->cq_dbrec.recv_counter,
			   cpu_to_be32(dev->cq_ci & 0xffffff));

		if (opcode == MLX5_CQE_REQ) {
			dev->sq_ci =
				(u16)(MLX5_GET(cqe64, cqe, wqe_counter) + 1);
			polled++;
			continue;
		}
		if (opcode == MLX5_CQE_REQ_ERR ||
		    opcode == MLX5_CQE_RESP_ERR) {
			dev_dbg(dev->device,
				"CQE error: opcode=0x%x syndrome=0x%x vendor=0x%x\n",
				opcode,
				MLX5_GET(cqe64, cqe, error_syndrome.syndrome),
				MLX5_GET(cqe64, cqe,
					 error_syndrome.vendor_error_syndrome));
			return -1;
		}
		dev_err(dev->device, "CQE unexpected opcode=0x%x\n", opcode);
		return -1;
	}

	return polled;
}

static int mlx5st_poll_cq(struct mlx5st_device *dev, unsigned int timeout_ms)
{
	struct timespec start, now;
	unsigned int elapsed;
	int ret;

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (;;) {
		ret = mlx5st_poll_cq_batch(dev, 1);
		if (ret < 0)
			return -1;
		if (ret > 0)
			return 0;

		if (dev->have_eq)
			mlx5st_process_events(dev);

		clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed = (now.tv_sec - start.tv_sec) * 1000 +
			  (now.tv_nsec - start.tv_nsec) / 1000000;
		if (elapsed > timeout_ms) {
			dev_err(dev->device, "CQ poll timeout after %u ms\n",
				timeout_ms);
			return -1;
		}
	}
}

/*
 * Data path setup/teardown helpers
 */

static void mlx5st_setup_datapath(struct mlx5st_device *dev)
{
	mlx5st_create_cq(dev);
	mlx5st_create_qp(dev);
	mlx5st_qp_rst2init(dev);
	mlx5st_qp_init2rtr(dev);
	mlx5st_qp_rtr2rts(dev);
}

static void mlx5st_teardown_datapath(struct mlx5st_device *dev)
{
	if (dev->qpn) {
		mlx5st_destroy_qp(dev);
		dev->qpn = 0;
	}
	if (dev->cqn) {
		mlx5st_destroy_cq(dev);
		dev->cqn = 0;
	}
	dev->sq_pi = 0;
	dev->sq_ci = 0;
	dev->cq_arm_sn = 0;
	memset(&dev->qp_dbrec, 0, sizeof(dev->qp_dbrec));
	memset(&dev->cq_dbrec, 0, sizeof(dev->cq_dbrec));
}

/*
 * memcpy callbacks
 */

#define MLX5ST_MEMCPY_TIMEOUT_MS 60000

static void mlx5st_memcpy_start(struct vfio_pci_device *device,
				 iova_t src, iova_t dst, u64 size, u64 count)
{
	struct mlx5st_device *dev = to_mlx5st(device);
	u64 i;

	for (i = 0; i < count; i++) {
		bool signaled = (i == count - 1);

		mlx5st_post_rdma_write(dev, src, dev->global_lkey, dst,
				       dev->global_rkey, size, signaled);
	}
}

static int mlx5st_memcpy_wait(struct vfio_pci_device *device)
{
	struct mlx5st_device *dev = to_mlx5st(device);
	int ret;

	ret = mlx5st_poll_cq(dev, MLX5ST_MEMCPY_TIMEOUT_MS);
	if (ret) {
		/*
		 * CQE error puts the QP in error state.  Rebuild the data path
		 * so subsequent operations can succeed.
		 */
		mlx5st_teardown_datapath(dev);
		mlx5st_setup_datapath(dev);
	}
	return ret;
}

/*
 * send_msi callback — trigger CQE -> EQE -> MSI-X via a small RDMA Write.
 *
 * Both the CQ and MSI EQ use single-shot arming: the CQ must be armed so the
 * CQE generates an EQE, and the MSI EQ must be armed so the EQE fires MSI-X.
 */
static void mlx5st_send_msi(struct vfio_pci_device *device)
{
	struct mlx5st_device *dev = to_mlx5st(device);

	/* Drain accumulated MSI EQ events and re-arm for next interrupt */
	mlx5st_msi_eq_drain(dev);

	/* Arm CQ so the next CQE generates an EQE on the MSI EQ */
	mlx5st_arm_cq(dev);

	/* Post a signaled RDMA Write to trigger CQE -> EQE -> MSI-X */
	mlx5st_post_rdma_write(dev,
			       to_iova(device, &dev->send_msi_src),
			       dev->global_lkey,
			       to_iova(device, &dev->send_msi_dst),
			       dev->global_rkey,
			       sizeof(dev->send_msi_src), true);

	/* Consume the CQE to avoid stale completions */
	VFIO_ASSERT_EQ(mlx5st_poll_cq(dev, MLX5ST_MEMCPY_TIMEOUT_MS), 0);
}

/*
 * Driver ops callbacks
 */

static void mlx5st_init(struct vfio_pci_device *device)
{
	struct mlx5st_device *dev = to_mlx5st(device);
	iova_t iova_align =
		device->driver.region.iova % __alignof__(struct mlx5st_device);

	VFIO_ASSERT_GE(device->driver.region.size, sizeof(*dev));
	VFIO_ASSERT_EQ(iova_align, 0);
	memset(dev, 0, sizeof(*dev));

	dev->device = device;
	dev->bar0 = device->bars[0].vaddr;
	dev->init_seg = dev->bar0;

	vfio_pci_config_writew(device, PCI_COMMAND,
			       vfio_pci_config_readw(device, PCI_COMMAND) |
				       PCI_COMMAND_MASTER);

	mlx5st_wait_fw_init(dev);

	mlx5st_cmd_init(dev);
	mlx5st_hca_init(dev);
	mlx5st_query_fl_caps(dev);
	mlx5st_alloc_pd(dev);
	mlx5st_create_mkey(dev);

	/* MSI EQ must be created before CQ so CQ can reference its eqn */
	mlx5st_create_msi_eq(dev);
	mlx5st_setup_datapath(dev);

	vfio_pci_msix_enable(device, MSI_VECTOR, 1);
	device->driver.msi = MSI_VECTOR;

	device->driver.max_memcpy_size = 1ULL << dev->log_max_msg;
	device->driver.max_memcpy_count = SQ_WQE_CNT - 1;

	dev_dbg(device, "mlx5 driver initialized\n");
}

static void mlx5st_remove(struct vfio_pci_device *device)
{
	struct mlx5st_device *dev = to_mlx5st(device);

	vfio_pci_msix_disable(device);
	mlx5st_teardown_datapath(dev);

	if (dev->have_msi_eq) {
		mlx5st_destroy_msi_eq(dev);
		dev->have_msi_eq = false;
	}

	dev_dbg(device, "teardown: destroy_mkey\n");
	if (dev->mkey_index) {
		mlx5st_destroy_mkey(dev);
		dev->mkey_index = 0;
	}

	dev_dbg(device, "teardown: dealloc_pd\n");
	if (dev->pdn) {
		mlx5st_dealloc_pd(dev);
		dev->pdn = 0;
	}

	dev_dbg(device, "teardown: hca_teardown\n");
	mlx5st_hca_teardown(dev);

	vfio_pci_config_writew(device, PCI_COMMAND,
			       vfio_pci_config_readw(device, PCI_COMMAND) &
				       ~(u16)PCI_COMMAND_MASTER);

	dev_dbg(device, "Teardown complete\n");
}

struct vfio_pci_driver_ops mlx5st_ops = {
	.name = "mlx5",
	.region_size = sizeof(struct mlx5st_device),
	.probe = mlx5st_probe,
	.init = mlx5st_init,
	.remove = mlx5st_remove,
	.memcpy_start = mlx5st_memcpy_start,
	.memcpy_wait = mlx5st_memcpy_wait,
	.send_msi = mlx5st_send_msi,
};
