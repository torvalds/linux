// SPDX-License-Identifier: GPL-2.0-only
#include <stdint.h>
#include <unistd.h>

#include <linux/errno.h>
#include <linux/io.h>
#include <linux/pci_ids.h>
#include <linux/sizes.h>

#include <vfio_util.h>

#include "hw.h"
#include "registers.h"

#define IOAT_DMACOUNT_MAX UINT16_MAX

struct ioat_state {
	/* Single descriptor used to issue DMA memcpy operations */
	struct ioat_dma_descriptor desc;

	/* Copy buffers used by ioat_send_msi() to generate an interrupt. */
	u64 send_msi_src;
	u64 send_msi_dst;
};

static inline struct ioat_state *to_ioat_state(struct vfio_pci_device *device)
{
	return device->driver.region.vaddr;
}

static inline void *ioat_channel_registers(struct vfio_pci_device *device)
{
	return device->bars[0].vaddr + IOAT_CHANNEL_MMIO_SIZE;
}

static int ioat_probe(struct vfio_pci_device *device)
{
	u8 version;
	int r;

	if (!vfio_pci_device_match(device, PCI_VENDOR_ID_INTEL,
				   PCI_DEVICE_ID_INTEL_IOAT_SKX))
		return -EINVAL;

	VFIO_ASSERT_NOT_NULL(device->bars[0].vaddr);

	version = readb(device->bars[0].vaddr + IOAT_VER_OFFSET);
	switch (version) {
	case IOAT_VER_3_2:
	case IOAT_VER_3_3:
		r = 0;
		break;
	default:
		printf("ioat: Unsupported version: 0x%x\n", version);
		r = -EINVAL;
	}
	return r;
}

static u64 ioat_channel_status(void *bar)
{
	return readq(bar + IOAT_CHANSTS_OFFSET) & IOAT_CHANSTS_STATUS;
}

static void ioat_clear_errors(struct vfio_pci_device *device)
{
	void *registers = ioat_channel_registers(device);
	u32 errors;

	errors = vfio_pci_config_readl(device, IOAT_PCI_CHANERR_INT_OFFSET);
	vfio_pci_config_writel(device, IOAT_PCI_CHANERR_INT_OFFSET, errors);

	errors = vfio_pci_config_readl(device, IOAT_PCI_DMAUNCERRSTS_OFFSET);
	vfio_pci_config_writel(device, IOAT_PCI_CHANERR_INT_OFFSET, errors);

	errors = readl(registers + IOAT_CHANERR_OFFSET);
	writel(errors, registers + IOAT_CHANERR_OFFSET);
}

static void ioat_reset(struct vfio_pci_device *device)
{
	void *registers = ioat_channel_registers(device);
	u32 sleep_ms = 1, attempts = 5000 / sleep_ms;
	u8 chancmd;

	ioat_clear_errors(device);

	writeb(IOAT_CHANCMD_RESET, registers + IOAT2_CHANCMD_OFFSET);

	for (;;) {
		chancmd = readb(registers + IOAT2_CHANCMD_OFFSET);
		if (!(chancmd & IOAT_CHANCMD_RESET))
			break;

		VFIO_ASSERT_GT(--attempts, 0);
		usleep(sleep_ms * 1000);
	}

	VFIO_ASSERT_EQ(ioat_channel_status(registers), IOAT_CHANSTS_HALTED);
}

static void ioat_init(struct vfio_pci_device *device)
{
	struct ioat_state *ioat = to_ioat_state(device);
	u8 intrctrl;

	VFIO_ASSERT_GE(device->driver.region.size, sizeof(*ioat));

	vfio_pci_config_writew(device, PCI_COMMAND,
			       PCI_COMMAND_MEMORY |
			       PCI_COMMAND_MASTER |
			       PCI_COMMAND_INTX_DISABLE);

	ioat_reset(device);

	/* Enable the use of MXI-x interrupts for channel interrupts. */
	intrctrl = IOAT_INTRCTRL_MSIX_VECTOR_CONTROL;
	writeb(intrctrl, device->bars[0].vaddr + IOAT_INTRCTRL_OFFSET);

	vfio_pci_msix_enable(device, 0, device->msix_info.count);

	device->driver.msi = 0;
	device->driver.max_memcpy_size =
		1UL << readb(device->bars[0].vaddr + IOAT_XFERCAP_OFFSET);
	device->driver.max_memcpy_count = IOAT_DMACOUNT_MAX;
}

static void ioat_remove(struct vfio_pci_device *device)
{
	ioat_reset(device);
	vfio_pci_msix_disable(device);
}

static void ioat_handle_error(struct vfio_pci_device *device)
{
	void *registers = ioat_channel_registers(device);

	printf("Error detected during memcpy operation!\n"
	       "  CHANERR: 0x%x\n"
	       "  CHANERR_INT: 0x%x\n"
	       "  DMAUNCERRSTS: 0x%x\n",
	       readl(registers + IOAT_CHANERR_OFFSET),
	       vfio_pci_config_readl(device, IOAT_PCI_CHANERR_INT_OFFSET),
	       vfio_pci_config_readl(device, IOAT_PCI_DMAUNCERRSTS_OFFSET));

	ioat_reset(device);
}

static int ioat_memcpy_wait(struct vfio_pci_device *device)
{
	void *registers = ioat_channel_registers(device);
	u64 status;
	int r = 0;

	/* Wait until all operations complete. */
	for (;;) {
		status = ioat_channel_status(registers);
		if (status == IOAT_CHANSTS_DONE)
			break;

		if (status == IOAT_CHANSTS_HALTED) {
			ioat_handle_error(device);
			return -1;
		}
	}

	/* Put the channel into the SUSPENDED state. */
	writeb(IOAT_CHANCMD_SUSPEND, registers + IOAT2_CHANCMD_OFFSET);
	for (;;) {
		status = ioat_channel_status(registers);
		if (status == IOAT_CHANSTS_SUSPENDED)
			break;
	}

	return r;
}

static void __ioat_memcpy_start(struct vfio_pci_device *device,
				iova_t src, iova_t dst, u64 size,
				u16 count, bool interrupt)
{
	void *registers = ioat_channel_registers(device);
	struct ioat_state *ioat = to_ioat_state(device);
	u64 desc_iova;
	u16 chanctrl;

	desc_iova = to_iova(device, &ioat->desc);
	ioat->desc = (struct ioat_dma_descriptor) {
		.ctl_f.op = IOAT_OP_COPY,
		.ctl_f.int_en = interrupt,
		.src_addr = src,
		.dst_addr = dst,
		.size = size,
		.next = desc_iova,
	};

	/* Tell the device the address of the descriptor. */
	writeq(desc_iova, registers + IOAT2_CHAINADDR_OFFSET);

	/* (Re)Enable the channel interrupt and abort on any errors */
	chanctrl = IOAT_CHANCTRL_INT_REARM | IOAT_CHANCTRL_ANY_ERR_ABORT_EN;
	writew(chanctrl, registers + IOAT_CHANCTRL_OFFSET);

	/* Kick off @count DMA copy operation(s). */
	writew(count, registers + IOAT_CHAN_DMACOUNT_OFFSET);
}

static void ioat_memcpy_start(struct vfio_pci_device *device,
			      iova_t src, iova_t dst, u64 size,
			      u64 count)
{
	__ioat_memcpy_start(device, src, dst, size, count, false);
}

static void ioat_send_msi(struct vfio_pci_device *device)
{
	struct ioat_state *ioat = to_ioat_state(device);

	__ioat_memcpy_start(device,
			    to_iova(device, &ioat->send_msi_src),
			    to_iova(device, &ioat->send_msi_dst),
			    sizeof(ioat->send_msi_src), 1, true);

	VFIO_ASSERT_EQ(ioat_memcpy_wait(device), 0);
}

const struct vfio_pci_driver_ops ioat_ops = {
	.name = "ioat",
	.probe = ioat_probe,
	.init = ioat_init,
	.remove = ioat_remove,
	.memcpy_start = ioat_memcpy_start,
	.memcpy_wait = ioat_memcpy_wait,
	.send_msi = ioat_send_msi,
};
