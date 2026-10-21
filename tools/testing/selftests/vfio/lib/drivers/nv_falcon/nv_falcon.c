// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 */
#include <stdint.h>
#include <strings.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/pci_ids.h>

#include <libvfio.h>

#include "hw.h"

struct gpu_device {
	enum gpu_arch arch;
	void *bar0;
	bool is_memory_clear_supported;
	const struct falcon *falcon;
	u32 pmc_enable_mask;
	bool fsp_dma_enabled;

	/* Pending memcpy parameters, set by memcpy_start() */
	u64 memcpy_src;
	u64 memcpy_dst;
	u64 memcpy_size;
};

static inline struct gpu_device *to_gpu_device(struct vfio_pci_device *device)
{
	return device->driver.region.vaddr;
}

static enum gpu_arch nv_gpu_arch_lookup(u32 pmc_boot_0)
{
	u32 arch = (pmc_boot_0 >> 24) & 0x1f;

	switch (arch) {
	case 0x0e:
	case 0x0f:
	case 0x10:
		return GPU_ARCH_KEPLER;
	case 0x11:
		return GPU_ARCH_MAXWELL_GEN1;
	case 0x12:
		return GPU_ARCH_MAXWELL_GEN2;
	case 0x13:
		/* P100 (impl 0) uses PMC reset; P4/P40 use engine reset */
		if (((pmc_boot_0 >> 20) & 0xf) == 0)
			return GPU_ARCH_PASCAL;
		return GPU_ARCH_PASCAL_10X;
	case 0x14:
		return GPU_ARCH_VOLTA;
	case 0x16:
		return GPU_ARCH_TURING;
	case 0x17:
		return GPU_ARCH_AMPERE;
	case 0x18:
		return GPU_ARCH_HOPPER;
	case 0x19:
		return GPU_ARCH_ADA;
	default:
		return GPU_ARCH_UNKNOWN;
	}
}

static inline u32 gpu_read32(struct gpu_device *gpu, u32 offset)
{
	return readl(gpu->bar0 + offset);
}

static inline void gpu_write32(struct gpu_device *gpu, u32 offset, u32 value)
{
	writel(value, gpu->bar0 + offset);
}

static u64 get_elapsed_ms(struct timespec *start)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	return (now.tv_sec - start->tv_sec) * 1000
	       + (now.tv_nsec - start->tv_nsec) / 1000000;
}

static int gpu_poll_register(struct vfio_pci_device *device,
			     const char *name, u32 offset,
			     u32 expected, u32 mask, u32 timeout_ms)
{
	struct gpu_device *gpu = to_gpu_device(device);
	struct timespec start;
	u64 elapsed_ms;
	u32 value;

	clock_gettime(CLOCK_MONOTONIC, &start);

	for (;;) {
		value = gpu_read32(gpu, offset);
		if ((value & mask) == expected)
			return 0;

		elapsed_ms = get_elapsed_ms(&start);

		if (elapsed_ms >= timeout_ms)
			break;

		usleep(1000);
	}

	dev_err(device,
		"Timeout polling %s (0x%x): value=0x%x expected=0x%x mask=0x%x after %lu ms\n",
		name, offset, value, expected, mask, elapsed_ms);
	return -ETIMEDOUT;
}

static int fsp_poll_queue(struct vfio_pci_device *device, const char *name,
			  u32 head_reg, u32 tail_reg, bool wait_empty,
			  u32 timeout_ms)
{
	struct gpu_device *gpu = to_gpu_device(device);
	struct timespec start;
	u64 elapsed_ms;
	u32 head, tail;

	clock_gettime(CLOCK_MONOTONIC, &start);

	for (;;) {
		head = gpu_read32(gpu, head_reg);
		tail = gpu_read32(gpu, tail_reg);
		if (wait_empty ? (head == tail) : (head != tail))
			return 0;

		elapsed_ms = get_elapsed_ms(&start);

		if (elapsed_ms >= timeout_ms)
			break;

		usleep(1000);
	}

	dev_err(device,
		"Timeout polling %s: head=0x%x tail=0x%x wait_empty=%d after %lu ms\n",
		name, head, tail, wait_empty, elapsed_ms);
	return -ETIMEDOUT;
}

static void fsp_emem_write(struct vfio_pci_device *device, u32 offset,
			   const u32 *data, u32 count)
{
	struct gpu_device *gpu = to_gpu_device(device);
	u32 i;

	/* Configure port with auto-increment for read and write */
	gpu_write32(gpu, NV_FSP_EMEM_PORT2_CTRL,
		    offset | NV_FALCON_EMEMC_AINCR | NV_FALCON_EMEMC_AINCW);

	for (i = 0; i < count; i++)
		gpu_write32(gpu, NV_FSP_EMEM_PORT2_DATA, data[i]);
}

static void fsp_emem_read(struct vfio_pci_device *device, u32 offset,
			  u32 *data, u32 count)
{
	struct gpu_device *gpu = to_gpu_device(device);
	u32 i;

	/* Configure port with auto-increment for read and write */
	gpu_write32(gpu, NV_FSP_EMEM_PORT2_CTRL,
		    offset | NV_FALCON_EMEMC_AINCR | NV_FALCON_EMEMC_AINCW);

	for (i = 0; i < count; i++)
		data[i] = gpu_read32(gpu, NV_FSP_EMEM_PORT2_DATA);
}

static int fsp_rpc_send_data(struct vfio_pci_device *device, const u32 *data,
			     u32 count)
{
	struct gpu_device *gpu = to_gpu_device(device);
	int ret;

	ret = fsp_poll_queue(device, "fsp_cmd_queue_empty",
			     NV_FSP_QUEUE_HEAD, NV_FSP_QUEUE_TAIL, true, 1000);
	if (ret)
		return ret;

	fsp_emem_write(device, NV_FSP_RPC_EMEM_BASE, data, count);

	/* Update queue head/tail to signal data is ready */
	gpu_write32(gpu, NV_FSP_QUEUE_TAIL,
		    NV_FSP_RPC_EMEM_BASE + (count - 1) * 4);
	gpu_write32(gpu, NV_FSP_QUEUE_HEAD, NV_FSP_RPC_EMEM_BASE);

	return ret;
}

static int fsp_rpc_receive_data(struct vfio_pci_device *device, u32 *data,
				u32 max_count, u32 timeout_ms)
{
	struct gpu_device *gpu = to_gpu_device(device);
	u32 head, tail;
	u32 msg_size_words;
	int ret;

	ret = fsp_poll_queue(device, "fsp_msg_queue_ready",
			     NV_FSP_MSG_QUEUE_HEAD, NV_FSP_MSG_QUEUE_TAIL,
			     false, timeout_ms);
	if (ret)
		return ret;

	head = gpu_read32(gpu, NV_FSP_MSG_QUEUE_HEAD);
	tail = gpu_read32(gpu, NV_FSP_MSG_QUEUE_TAIL);

	msg_size_words = (tail - head + 4) / 4;
	if (msg_size_words > max_count)
		msg_size_words = max_count;

	fsp_emem_read(device, NV_FSP_RPC_EMEM_BASE, data, msg_size_words);

	/* Reset message queue tail to acknowledge receipt */
	gpu_write32(gpu, NV_FSP_MSG_QUEUE_TAIL, head);

	return msg_size_words;
}

static void fsp_reset_rpc_state(struct vfio_pci_device *device)
{
	struct gpu_device *gpu = to_gpu_device(device);
	u32 head, tail;

	head = gpu_read32(gpu, NV_FSP_QUEUE_HEAD);
	tail = gpu_read32(gpu, NV_FSP_QUEUE_TAIL);

	if (head == tail) {
		head = gpu_read32(gpu, NV_FSP_MSG_QUEUE_HEAD);
		tail = gpu_read32(gpu, NV_FSP_MSG_QUEUE_TAIL);
		if (head == tail)
			return;
	}

	/* Best-effort drain; timeout is expected if no pending message. */
	fsp_poll_queue(device, "fsp_msg_queue_drain",
		       NV_FSP_MSG_QUEUE_HEAD, NV_FSP_MSG_QUEUE_TAIL,
		       false, 5000);

	gpu_write32(gpu, NV_FSP_QUEUE_TAIL, NV_FSP_RPC_EMEM_BASE);
	gpu_write32(gpu, NV_FSP_QUEUE_HEAD, NV_FSP_RPC_EMEM_BASE);
	gpu_write32(gpu, NV_FSP_MSG_QUEUE_TAIL, NV_FSP_RPC_EMEM_BASE);
	gpu_write32(gpu, NV_FSP_MSG_QUEUE_HEAD, NV_FSP_RPC_EMEM_BASE);
}

static inline u32 mctp_header_build(u8 seid, u8 seq, bool som, bool eom)
{
	u32 hdr = 0;

	hdr |= (seid & NV_MCTP_HDR_SEID_MASK) << NV_MCTP_HDR_SEID_SHIFT;
	hdr |= (seq & NV_MCTP_HDR_SEQ_MASK) << NV_MCTP_HDR_SEQ_SHIFT;
	if (som)
		hdr |= NV_MCTP_HDR_SOM_BIT;
	if (eom)
		hdr |= NV_MCTP_HDR_EOM_BIT;

	return hdr;
}

static inline u32 mctp_msg_header_build(u8 nvdm_type)
{
	u32 hdr = 0;

	hdr |= (NV_MCTP_MSG_TYPE_VENDOR_DEFINED & NV_MCTP_MSG_TYPE_MASK)
		<< NV_MCTP_MSG_TYPE_SHIFT;
	hdr |= (NV_MCTP_MSG_VENDOR_ID_NVIDIA & NV_MCTP_MSG_VENDOR_ID_MASK)
		<< NV_MCTP_MSG_VENDOR_ID_SHIFT;
	hdr |= (nvdm_type & NV_MCTP_MSG_NVDM_TYPE_MASK)
		<< NV_MCTP_MSG_NVDM_TYPE_SHIFT;

	return hdr;
}

static inline u8 mctp_msg_header_get_nvdm_type(u32 hdr)
{
	return (hdr >> NV_MCTP_MSG_NVDM_TYPE_SHIFT) &
	       NV_MCTP_MSG_NVDM_TYPE_MASK;
}

static int fsp_rpc_send_cmd(struct vfio_pci_device *device, u8 nvdm_type,
			    const u32 *data, u32 data_count, u32 timeout_ms)
{
	u32 max_packet_words = NV_FSP_RPC_MAX_PACKET_SIZE / 4;
	u32 packet[256];
	u32 resp_buf[256];
	u32 total_words;
	int resp_words;
	u8 resp_nvdm_type;
	int ret;

	total_words = 2 + data_count;
	if (total_words > max_packet_words)
		return -EINVAL;

	packet[0] = mctp_header_build(0, 0, true, true);
	packet[1] = mctp_msg_header_build(nvdm_type);

	if (data_count > 0)
		memcpy(&packet[2], data, data_count * sizeof(u32));

	ret = fsp_rpc_send_data(device, packet, total_words);
	if (ret)
		return ret;

	resp_words = fsp_rpc_receive_data(device, resp_buf, 256, timeout_ms);
	if (resp_words < 0)
		return resp_words;

	if (resp_words < NV_FSP_RPC_MIN_RESPONSE_WORDS)
		return -EPROTO;

	resp_nvdm_type = mctp_msg_header_get_nvdm_type(resp_buf[1]);
	if (resp_nvdm_type != NV_NVDM_TYPE_RESPONSE)
		return -EPROTO;

	if (resp_buf[3] != nvdm_type)
		return -EPROTO;

	if (resp_buf[4] != 0)
		return -resp_buf[4];

	return 0;
}

static int fsp_init(struct vfio_pci_device *device)
{
	int ret;

	ret = gpu_poll_register(device, "fsp_boot_complete",
				NV_FSP_BOOT_COMPLETE_OFFSET,
				NV_FSP_BOOT_COMPLETE_SUCCESS, 0xffffffff, 5000);
	if (ret)
		return ret;

	fsp_reset_rpc_state(device);
	return ret;
}

static int fsp_fbdma_enable(struct vfio_pci_device *device)
{
	struct gpu_device *gpu = to_gpu_device(device);
	u32 cmd_data = NV_FBDMA_SUBCMD_ENABLE;
	int ret = 0;

	if (gpu->fsp_dma_enabled)
		return ret;

	ret = fsp_rpc_send_cmd(device, NV_NVDM_TYPE_FBDMA, &cmd_data, 1, 5000);
	if (ret)
		return ret;

	gpu->fsp_dma_enabled = true;
	return ret;
}

static bool fsp_check_ofa_dma_support(struct vfio_pci_device *device)
{
	struct gpu_device *gpu = to_gpu_device(device);
	u32 val = gpu_read32(gpu, NV_OFA_DMA_SUPPORT_CHECK_REG);

	return (val >> 16) != 0xbadf;
}

static u32 size_to_dma_encoding(u64 size)
{
	VFIO_ASSERT_LE(size, NV_FALCON_DMA_MAX_TRANSFER_SIZE);
	VFIO_ASSERT_GE(size, NV_FALCON_DMA_MIN_TRANSFER_SIZE);
	VFIO_ASSERT_EQ(size & (size - 1), 0, "size must be power-of-2\n");

	return ffs(size) - 3;
}

static void falcon_dmem_port_configure(struct vfio_pci_device *device,
				       u32 offset, bool auto_inc_read,
				       bool auto_inc_write)
{
	struct gpu_device *gpu = to_gpu_device(device);
	const struct falcon *falcon = gpu->falcon;
	u32 memc_value = offset;

	/* Set auto-increment flags */
	if (auto_inc_read)
		memc_value |= NV_PPWR_FALCON_DMEMC_AINCR_TRUE;
	if (auto_inc_write)
		memc_value |= NV_PPWR_FALCON_DMEMC_AINCW_TRUE;

	gpu_write32(gpu, falcon->dmem_control_reg, memc_value);
}

static void falcon_select_core_falcon(struct vfio_pci_device *device)
{
	struct gpu_device *gpu = to_gpu_device(device);
	const struct falcon *falcon = gpu->falcon;
	u32 core_select_reg = falcon->base_page + NV_FALCON_CORE_SELECT_OFFSET;
	u32 core_select;

	core_select = gpu_read32(gpu, core_select_reg);

	/* Clear bits 4:5 to select falcon core (not RISCV) */
	core_select &= ~NV_FALCON_CORE_SELECT_MASK;

	gpu_write32(gpu, core_select_reg, core_select);
}

static int falcon_enable(struct vfio_pci_device *device)
{
	struct gpu_device *gpu = to_gpu_device(device);
	const struct falcon *falcon = gpu->falcon;
	u32 mailbox_test_reg;
	u32 mailbox_val;

	if (falcon->no_outside_reset)
		return 0;

	/* Ada-specific: Check if falcon needs reset before enable */
	if (gpu->arch == GPU_ARCH_ADA) {
		mailbox_test_reg = falcon->base_page +
				   NV_FALCON_MAILBOX_TEST_OFFSET;
		mailbox_val = gpu_read32(gpu, mailbox_test_reg);
		if (mailbox_val == NV_FALCON_MAILBOX_RESET_MAGIC)
			gpu_write32(gpu, falcon->engine_reset, 1);
	}

	/* Enable the falcon based on control method */
	if (gpu->pmc_enable_mask != 0) {
		u32 pmc_enable;

		/* Enable via PMC_ENABLE register */
		pmc_enable = gpu_read32(gpu, NV_PMC_ENABLE);
		gpu_write32(gpu, NV_PMC_ENABLE,
			    pmc_enable | gpu->pmc_enable_mask);
	} else {
		/* Enable by deasserting engine reset */
		gpu_write32(gpu, falcon->engine_reset, 0);
	}

	if (gpu->arch < GPU_ARCH_HOPPER) {
		falcon_select_core_falcon(device);

		/* Wait for DMACTL to be ready (bits 1:2 should be 0) */
		return gpu_poll_register(device, "falcon_dmactl",
					 falcon->dmactl, 0,
					 NV_FALCON_DMACTL_READY_MASK, 1000);
	}

	return 0;
}

static void falcon_disable(struct vfio_pci_device *device)
{
	struct gpu_device *gpu = to_gpu_device(device);
	const struct falcon *falcon = gpu->falcon;
	u32 pmc_enable;

	if (falcon->no_outside_reset)
		return;

	if (gpu->pmc_enable_mask != 0) {
		/* Disable via PMC_ENABLE */
		pmc_enable = gpu_read32(gpu, NV_PMC_ENABLE);
		gpu_write32(gpu, NV_PMC_ENABLE,
			    pmc_enable & ~gpu->pmc_enable_mask);
	} else {
		/* Disable by asserting engine reset */
		gpu_write32(gpu, falcon->engine_reset, 1);
	}
}

static int falcon_reset(struct vfio_pci_device *device)
{
	falcon_disable(device);

	return falcon_enable(device);
}

static int nv_falcon_dma_init(struct vfio_pci_device *device)
{
	struct gpu_device *gpu = to_gpu_device(device);
	const struct falcon *falcon;
	u32 transcfg;
	u32 dmactl;
	u32 ctl;
	int ret = 0;

	falcon = gpu->falcon;

	vfio_pci_cmd_set(device, PCI_COMMAND_MASTER);

	if (gpu->arch >= GPU_ARCH_HOPPER) {
		ret = fsp_init(device);
		if (ret) {
			dev_err(device, "Failed to init FSP: %d\n", ret);
			return ret;
		}

		ret = fsp_fbdma_enable(device);
		if (ret) {
			dev_err(device,
				"Failed to enable FSP FBDMA: %d\n", ret);
			return ret;
		}

		if (!fsp_check_ofa_dma_support(device)) {
			dev_err(device,
				"OFA DMA not supported with current firmware\n");
			return -EOPNOTSUPP;
		}
	}

	if (gpu->is_memory_clear_supported) {
		/* For Turing+, wait for boot to complete first */
		if (gpu->arch >= GPU_ARCH_TURING) {
			/* Wait for boot complete - Hopper+ uses FSP register */
			if (gpu->arch >= GPU_ARCH_HOPPER) {
				ret = gpu_poll_register(device,
					"fsp_boot_complete",
					NV_FSP_BOOT_COMPLETE_OFFSET,
					NV_FSP_BOOT_COMPLETE_SUCCESS,
					0xffffffff, 5000);
			} else {
				ret = gpu_poll_register(device,
					"boot_complete",
					NV_BOOT_COMPLETE_OFFSET,
					NV_BOOT_COMPLETE_SUCCESS,
					0xffffffff, 5000);
			}
			if (ret)
				return ret;

			ret = gpu_poll_register(device,
				"memory_clear_finished",
				NV_MEM_CLEAR_OFFSET, 0x1, 0xffffffff, 5000);
			if (ret)
				return ret;
		}
	}

	ret = falcon_reset(device);
	if (ret)
		return ret;

	falcon_dmem_port_configure(device, 0, false, false);

	transcfg = gpu_read32(gpu, falcon->fbif_transcfg);
	transcfg &= ~NV_FBIF_TRANSCFG_TARGET_MASK;
	transcfg |= NV_FBIF_TRANSCFG_SYSMEM_DEFAULT;
	gpu_write32(gpu, falcon->fbif_transcfg, transcfg);

	gpu_write32(gpu, falcon->fbif_ctl2, 0x1);

	ctl = gpu_read32(gpu, falcon->fbif_ctl);
	ctl |= NV_FBIF_CTL_ALLOW_PHYS_MODE | NV_FBIF_CTL_ALLOW_FULL_PHYS_MODE;
	gpu_write32(gpu, falcon->fbif_ctl, ctl);

	dmactl = gpu_read32(gpu, falcon->dmactl);
	dmactl &= ~NV_FALCON_DMACTL_DMEM_SCRUBBING;
	gpu_write32(gpu, falcon->dmactl, dmactl);

	return ret;
}

static int nv_falcon_dma(struct vfio_pci_device *device,
			 u64 address, u64 size,
			 bool write)
{
	struct gpu_device *gpu = to_gpu_device(device);
	const struct falcon *falcon = gpu->falcon;
	u32 dma_cmd;
	int ret;

	gpu_write32(gpu, NV_GPU_DMA_ADDR_TOP_BITS_REG,
		    (address >> 47) & 0x1ffff);
	gpu_write32(gpu, falcon->base_page + NV_FALCON_DMA_ADDR_HIGH_OFFSET,
		    (address >> 40) & 0x7f);
	gpu_write32(gpu, falcon->base_page + NV_FALCON_DMA_ADDR_LOW_OFFSET,
		    (address >> 8) & 0xffffffff);
	gpu_write32(gpu, falcon->base_page + NV_FALCON_DMA_BLOCK_OFFSET,
		    address & 0xff);
	gpu_write32(gpu, falcon->base_page + NV_FALCON_DMA_MEM_OFFSET, 0);

	dma_cmd = size_to_dma_encoding(size) << NV_FALCON_DMA_CMD_SIZE_SHIFT;

	/* Set direction: write (DMEM->mem) or read (mem->DMEM) */
	if (write)
		dma_cmd |= NV_FALCON_DMA_CMD_WRITE_BIT;

	gpu_write32(gpu, falcon->base_page + NV_FALCON_DMA_CMD_OFFSET, dma_cmd);

	ret = gpu_poll_register(device, "dma_done",
				falcon->base_page + NV_FALCON_DMA_CMD_OFFSET,
				NV_FALCON_DMA_CMD_DONE_BIT,
				NV_FALCON_DMA_CMD_DONE_BIT, 1000);
	if (ret)
		dev_err(device, "Failed DMA %s (addr=0x%lx, size=%lu)\n",
			write ? "write" : "read", address, size);

	return ret;
}

static int nv_falcon_memcpy_chunk(struct vfio_pci_device *device,
				  iova_t src, iova_t dst, u64 size)
{
	int ret;

	ret = nv_falcon_dma(device, src, size, false);
	if (ret)
		return ret;

	return nv_falcon_dma(device, dst, size, true);
}

static int nv_falcon_probe(struct vfio_pci_device *device)
{
	enum gpu_arch gpu_arch;
	u32 pmc_boot_0;
	void *bar0;
	int i;

	if (vfio_pci_config_readw(device, PCI_VENDOR_ID) !=
	    PCI_VENDOR_ID_NVIDIA)
		return -ENODEV;

	if (vfio_pci_config_readw(device, PCI_CLASS_DEVICE) >> 8 !=
	    PCI_BASE_CLASS_DISPLAY)
		return -ENODEV;

	/* Get BAR0 pointer for reading GPU registers */
	bar0 = device->bars[0].vaddr;
	if (!bar0)
		return -ENODEV;

	/* Read PMC_BOOT_0 register from BAR0 to identify GPU */
	pmc_boot_0 = readl(bar0 + NV_PMC_BOOT_0);

	/* Look up GPU architecture to verify this is a supported GPU */
	gpu_arch = nv_gpu_arch_lookup(pmc_boot_0);
	if (gpu_arch == GPU_ARCH_UNKNOWN) {
		dev_err(device,
			"Unsupported GPU architecture for PMC_BOOT_0: 0x%x\n",
			pmc_boot_0);
		return -ENODEV;
	}

	/* Check verified GPU map */
	for (i = 0; i < VERIFIED_GPU_MAP_SIZE; i++) {
		if (verified_gpu_map[i] == pmc_boot_0)
			return 0;
	}

	dev_info(device,
		 "Unvalidated GPU: PMC_BOOT_0: 0x%x, possibly not supported\n",
		 pmc_boot_0);

	return 0;
}

static void nv_falcon_init(struct vfio_pci_device *device)
{
	struct gpu_device *gpu = to_gpu_device(device);
	const struct gpu_properties *props;
	u32 pmc_boot_0;
	int ret;

	VFIO_ASSERT_GE(device->driver.region.size, sizeof(*gpu));

	/* Read PMC_BOOT_0 register from BAR0 to identify GPU */
	pmc_boot_0 = readl(device->bars[0].vaddr + NV_PMC_BOOT_0);

	/* Look up GPU architecture */
	gpu->arch = nv_gpu_arch_lookup(pmc_boot_0);

	props = &gpu_properties_map[gpu->arch];

	/* Populate GPU structure */
	gpu->bar0 = device->bars[0].vaddr;
	gpu->is_memory_clear_supported = props->memory_clear_supported;
	gpu->falcon = &falcon_map[props->falcon_type];
	gpu->pmc_enable_mask = props->pmc_enable_mask;

	/* Initialize falcon for DMA */
	ret = nv_falcon_dma_init(device);
	VFIO_ASSERT_EQ(ret, 0, "Failed to initialize falcon DMA: %d\n", ret);

	device->driver.max_memcpy_size = NV_FALCON_DMA_MAX_TRANSFER_SIZE;
	device->driver.max_memcpy_count = NV_FALCON_DMA_MAX_TRANSFER_COUNT;
}

static void nv_falcon_remove(struct vfio_pci_device *device)
{
	falcon_disable(device);
	vfio_pci_cmd_clear(device, PCI_COMMAND_MASTER);
}

/*
 * Falcon DMA can only process one transfer at a time,
 * so the actual work is deferred to memcpy_wait() to conform to the
 * memcpy_start()/memcpy_wait() contract.
 */
static void nv_falcon_memcpy_start(struct vfio_pci_device *device,
				   iova_t src, iova_t dst, u64 size, u64 count)
{
	struct gpu_device *gpu = to_gpu_device(device);

	VFIO_ASSERT_EQ(count, 1);
	VFIO_ASSERT_EQ(size & (NV_FALCON_DMA_MIN_TRANSFER_SIZE - 1), 0,
		       "size 0x%lx must be %u-byte aligned\n",
		       (unsigned long)size, NV_FALCON_DMA_MIN_TRANSFER_SIZE);

	gpu->memcpy_src = src;
	gpu->memcpy_dst = dst;
	gpu->memcpy_size = size;
}

/*
 * Return the largest power-of-2 bytes we can transfer from @addr
 * without crossing a DMA block boundary.
 */
static u64 dma_block_remain(u64 addr)
{
	u64 offset = addr & (NV_FALCON_DMA_BLOCK_SIZE - 1);

	if (!offset)
		return NV_FALCON_DMA_BLOCK_SIZE;

	/* Lowest set bit of the offset is the largest aligned chunk */
	return 1ULL << (ffs(offset) - 1);
}

static u64 rounddown_pow_of_two(u64 x)
{
	return 1ULL << (63 - __builtin_clzll(x));
}

static int nv_falcon_memcpy_wait(struct vfio_pci_device *device)
{
	struct gpu_device *gpu = to_gpu_device(device);
	iova_t src = gpu->memcpy_src;
	iova_t dst = gpu->memcpy_dst;
	u64 remaining = gpu->memcpy_size;
	int ret = 0;

	/*
	 * Falcon DMA supports power-of-2 transfer sizes in [4, 256] and
	 * cannot cross 256-byte block boundaries.  Decompose the request
	 * into the largest valid chunk at each step.
	 */
	while (remaining) {
		u64 chunk = rounddown_pow_of_two(remaining);

		chunk = min(chunk, dma_block_remain(src));
		chunk = min(chunk, dma_block_remain(dst));

		ret = nv_falcon_memcpy_chunk(device, src, dst, chunk);
		if (ret)
			break;

		src += chunk;
		dst += chunk;
		remaining -= chunk;
	}

	return ret;
}

const struct vfio_pci_driver_ops nv_falcon_ops = {
	.name = "nv_falcon",
	.probe = nv_falcon_probe,
	.init = nv_falcon_init,
	.remove = nv_falcon_remove,
	.memcpy_start = nv_falcon_memcpy_start,
	.memcpy_wait = nv_falcon_memcpy_wait,
};
