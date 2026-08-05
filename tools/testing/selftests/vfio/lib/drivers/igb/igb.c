// SPDX-License-Identifier: GPL-2.0-only
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <linux/io.h>
#include <linux/pci_regs.h>
#include <linux/pci_ids.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <asm/barrier.h>
#include <linux/mii.h>
#include <libvfio/vfio_pci_device.h>

#include "e1000_regs.h"
#include "e1000_defines.h"
#include "e1000_82575.h"

#define PCI_DEVICE_ID_INTEL_82576 0x10C9
#define IGB_MAX_CHUNK_SIZE 1024
#define MSIX_VECTOR 0
#define MSIX_VECTOR_MASK (1 << MSIX_VECTOR)
#define RING_SIZE 4096 /* Number of descriptors in ring */

struct igb_tx_desc {
	union {
		struct {
			u64 buffer_addr; /* Address of descriptor's data buffer */
			u32 cmd_type_len; /* Command/Type/Length */
			u32 olinfo_status; /* Context/Buffer info */
		} read;

		struct {
			u64 rsvd;        /* Reserved */
			u32 nxtseq_seed; /* Next sequence seed */
			u32 status;      /* Descriptor status */
		} wb;
	};
};

struct igb_rx_desc {
	union {
		struct {
			u64 pkt_addr; /* Packet buffer address */
			u64 hdr_addr; /* Header buffer address */
		} read;
		struct {
			u16 pkt_info;     /* RSS type, Packet type */
			u16 hdr_info;     /* Split Head, buf len */
			u32 rss;          /* RSS Hash */
			u32 status_error; /* ext status/error */
			u16 length;       /* Packet length */
			u16 vlan;         /* VLAN tag */
		} wb; /* writeback */
	};
};

struct igb {
	void *bar0;
	u32 tx_tail;
	u32 rx_tail;
	struct igb_tx_desc tx_ring[RING_SIZE] __attribute__((aligned(128)));
	struct igb_rx_desc rx_ring[RING_SIZE] __attribute__((aligned(128)));
};

static inline struct igb *to_igb_state(struct vfio_pci_device *device)
{
	return (struct igb *)device->driver.region.vaddr;
}

static inline void igb_write32(struct igb *igb, u32 reg, u32 val)
{
	writel(val, igb->bar0 + reg);
}

static inline u32 igb_read32(struct igb *igb, u32 reg)
{
	return readl(igb->bar0 + reg);
}

static int igb_write_phy(struct igb *igb, u32 offset, u16 data)
{
	u32 mdic;
	int i;

	/*
	 * Write a PHY register over MDIO.
	 *
	 * A production driver would hold the SW/FW semaphore (SWSM.SWESMBI + the
	 * SW_FW_SYNC PHY bit) across the MDIO transaction to serialize against the
	 * device's management firmware.  The selftest owns the assigned function
	 * exclusively on a dedicated test device with no active manageability
	 * contending for the PHY, so the sync is omitted; it should be added here
	 * if this ever needs to run on a manageability-enabled NIC.
	 */
	mdic = (((u32)data) |
		(offset << E1000_MDIC_REG_SHIFT) |
		(1 << E1000_MDIC_PHY_SHIFT) |
		E1000_MDIC_OP_WRITE);

	igb_write32(igb, E1000_MDIC, mdic);

	for (i = 0; i < 1000; i++) {
		usleep(50);
		mdic = igb_read32(igb, E1000_MDIC);
		if (mdic & E1000_MDIC_READY)
			break;
	}

	if (!(mdic & E1000_MDIC_READY))
		return -1;

	if (mdic & E1000_MDIC_ERROR)
		return -1;

	return 0;
}

/*
 * Configure the device for PHY internal loopback per 82576 datasheet
 * section 3.5.6.3.1.  Force the PHY to 1Gb/s full duplex with loopback
 * enabled, then force the MAC link state to match.  Internal loopback
 * wraps data at the end of the PHY datapath (section 3.5.6.3), so the
 * physical link state is irrelevant.
 *
 * Section 3.5.6.1 directs to "Use PHY Loopback instead of MAC Loopback
 * on the 82576", and section 3.5.6.2 states "MAC Loopback is not used
 * on this device."  RCTL.LBM_MAC is still set elsewhere as a QEMU-only
 * accommodation; see the RCTL programming in the caller for the
 * rationale.
 */
static void igb_setup_loopback(struct igb *igb)
{
	u32 ctrl;
	int ret;

	/*
	 * Kick the autoneg machinery solely to bring STATUS.LU up under
	 * QEMU's igb emulation: QEMU only updates STATUS.LU via its
	 * autoneg-done timer, and without LU set its receive path
	 * (e1000x_hw_rx_enabled) drops every loopback frame.  On real
	 * hardware autoneg cannot complete before the next PHY write
	 * below clears the autoneg-enable bit, so this is effectively a
	 * no-op there.
	 */
	(void)igb_write_phy(igb, MII_BMCR,
			    BMCR_ANENABLE | BMCR_ANRESTART);

	/* PHY control: loopback + 1Gb/s full duplex, autoneg disabled. */
	ret = igb_write_phy(igb, MII_BMCR,
			    BMCR_LOOPBACK |
			    BMCR_SPEED1000 |
			    BMCR_FULLDPLX);
	VFIO_ASSERT_EQ(ret, 0, "Failed to write PHY control register");

	/*
	 * Brief delay before forcing the MAC, mirroring the kernel ethtool
	 * selftest in igb_integrated_phy_loopback().  Not specified by the
	 * datasheet, but empirically required by the kernel driver.
	 */
	usleep(50000);

	/*
	 * Force the MAC to 1Gb/s full duplex with link up.  Without forcing
	 * the link state the descriptor engine does not run, since the chip
	 * normally waits for a real negotiated link.
	 */
	ctrl = igb_read32(igb, E1000_CTRL);
	ctrl &= ~E1000_CTRL_SPD_SEL;
	ctrl |= E1000_CTRL_FRCSPD |
		E1000_CTRL_FRCDPX |
		E1000_CTRL_SPD_1000 |
		E1000_CTRL_FD |
		E1000_CTRL_SLU;
	igb_write32(igb, E1000_CTRL, ctrl);

	/*
	 * Settling delay matching the kernel ethtool selftest's msleep(500)
	 * at the tail of igb_integrated_phy_loopback().  Not specified by
	 * the datasheet; empirical, and inherited from the kernel driver.
	 */
	usleep(500000);
}

static int igb_probe(struct vfio_pci_device *device)
{
	if (!vfio_pci_device_match(device, PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82576))
		return -EINVAL;

	return 0;
}

static void igb_reset(struct igb *igb)
{
	int retries = 20;

	igb_write32(igb, E1000_CTRL, igb_read32(igb, E1000_CTRL) | E1000_CTRL_RST);
	/*
	 * Must wait at least 1 millisecond after setting the reset bit before
	 * checking if this device is ready to be used (82576 datasheet section
	 * 4.2.1.6.1).  The delay also ensures the reset has taken effect and
	 * cleared EECD.AUTO_RD before it is polled below.
	 */
	usleep(1000);

	/*
	 * Poll NVM Auto Read Done rather than CTRL.RST, matching
	 * igb_get_auto_rd_done() in the igb driver: AUTO_RD implies both that
	 * the reset completed and that the device finished re-reading its
	 * configuration from NVM, which is what actually makes it usable.
	 */
	while (retries-- > 0 && !(igb_read32(igb, E1000_EECD) & E1000_EECD_AUTO_RD))
		usleep(1000);

	/*
	 * QEMU's igb emulation does not set E1000_EECD_AUTO_RD. If we timed out,
	 * check if CTRL.RST is cleared, which is what QEMU uses to signal reset
	 * completion.
	 */
	if (retries < 0) {
		VFIO_ASSERT_EQ(igb_read32(igb, E1000_CTRL) & E1000_CTRL_RST, 0,
			       "Device reset did not complete (CTRL.RST not cleared)");
	}

	igb_write32(igb, E1000_IMC, 0xFFFFFFFF);
}

/*
 * Program the device into a usable state.  Split out of igb_init() so it
 * can be reused after a device reset to re-program the registers that
 * CTRL.RST clears.  Expects bar0 to be mapped and MSI-X already enabled
 * via VFIO.
 */
static void igb_hw_init(struct vfio_pci_device *device)
{
	struct igb *igb = to_igb_state(device);
	u64 iova_tx, iova_rx;
	u32 ctrl, rctl;
	u16 cmd_reg;
	int retries;

	iova_tx = to_iova(device, igb->tx_ring);
	iova_rx = to_iova(device, igb->rx_ring);



	/* Signal that the driver is loaded */
	ctrl = igb_read32(igb, E1000_CTRL_EXT);
	ctrl |= E1000_CTRL_EXT_DRV_LOAD;
	ctrl &= ~E1000_CTRL_EXT_LINK_MODE_MASK;
	igb_write32(igb, E1000_CTRL_EXT, ctrl);

	/* Enable PCI Bus Master. */
	cmd_reg = vfio_pci_config_readw(device, PCI_COMMAND);
	if ((cmd_reg & (PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY)) !=
	    (PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY)) {
		cmd_reg |= (PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY);
		vfio_pci_config_writew(device, PCI_COMMAND, cmd_reg);
	}

	/* Configure PHY internal loopback for testing. */
	igb_setup_loopback(igb);

	/*
	 * Disable DMA re-send on PCIe completion timeout (82576 datasheet
	 * section 8.6.1, GCR.Completion_Timeout_Resend, bit 16).  The
	 * mix_and_match test intentionally submits descriptors targeting
	 * unmapped IOVAs; with the default (set) value, the device keeps
	 * retrying the failed read indefinitely, which keeps PCIe AER and
	 * IOMMU error handling busy and interferes with reset recovery.
	 */
	ctrl = igb_read32(igb, E1000_GCR);
	ctrl &= ~E1000_GCR_CMPL_TMOUT_RESEND;
	igb_write32(igb, E1000_GCR, ctrl);

	/* Configure TX and RX descriptor rings */
	igb_write32(igb, E1000_TDBAL(0), (u32)iova_tx);
	igb_write32(igb, E1000_TDBAH(0), (u32)(iova_tx >> 32));
	igb_write32(igb, E1000_TDLEN(0), RING_SIZE * sizeof(struct igb_tx_desc));
	igb_write32(igb, E1000_TDH(0), 0);
	igb_write32(igb, E1000_TDT(0), 0);
	igb_write32(igb, E1000_TXDCTL(0), E1000_TXDCTL_QUEUE_ENABLE);

	igb_write32(igb, E1000_RDBAL(0), (u32)iova_rx);
	igb_write32(igb, E1000_RDBAH(0), (u32)(iova_rx >> 32));
	igb_write32(igb, E1000_RDLEN(0), RING_SIZE * sizeof(struct igb_rx_desc));
	igb_write32(igb, E1000_RDH(0), 0);
	igb_write32(igb, E1000_RDT(0), 0);

	/*
	 * Select the advanced one-buffer descriptor format.  Per 82576
	 * datasheet section 7.1.5.2: "SRRCTL[n].DESCTYPE must be set to a
	 * value other than 000b for the 82576 to write back the special
	 * descriptors."  struct igb_rx_desc matches the advanced one-buffer
	 * writeback layout (section 7.1.5.2), so polling rx.wb.status_error
	 * requires this format.  Section 8.10.2 specifies DESCTYPE[27:25].
	 *
	 * The direct write also zeroes SRRCTL.BSIZEPACKET, which is
	 * intentional: per section 7.1.3.1 a zero BSIZEPACKET falls back to
	 * the RCTL.BSIZE buffer size, whose reset default (00b) is 2048
	 * bytes -- ample for the loopback frames here.
	 */
	igb_write32(igb, E1000_SRRCTL(0), E1000_SRRCTL_DESCTYPE_ADV_ONEBUF);

	igb_write32(igb, E1000_RXDCTL(0), E1000_RXDCTL_QUEUE_ENABLE);

	/*
	 * Enable Receiver and Transmitter.  RCTL.LBM_MAC is set in addition
	 * to PHY loopback as a QEMU-only accommodation: QEMU's emulated igb
	 * does not honor PHY register 0 bit 14 (PHY internal loopback) and
	 * relies on RCTL.LBM_MAC to wrap TX descriptors back to the RX
	 * queue.  Datasheet 8.10.1 (RCTL register) advises "When using the
	 * internal PHY, LBM should remain set to 00b", so setting LBM_MAC
	 * here deviates from datasheet guidance; empirically the bit has
	 * no observable effect on real 82576 hardware because MAC loopback
	 * is not implemented (datasheet 3.5.6.2).  Setting both lets the
	 * selftest work on both real hardware and QEMU without conditional
	 * code paths.
	 */
	rctl = E1000_RCTL_EN |       /* Receiver Enable */
	       E1000_RCTL_UPE |      /* Unicast Promiscuous (for dummy MAC) */
	       E1000_RCTL_MPE |      /* Multicast Promiscuous */
	       E1000_RCTL_BAM |      /* Broadcast Accept Mode */
	       E1000_RCTL_LBM_MAC |  /* MAC Loopback - for QEMU emulation only */
	       E1000_RCTL_SECRC;     /* Strip CRC (needed for memcmp) */
	igb_write32(igb, E1000_RCTL, rctl);
	igb_write32(igb, E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP);

	/*
	 * Wait for TX and RX queues to be enabled.  Per the RXDCTL/TXDCTL
	 * register definitions (8.10.10/8.12.13), the per-queue enable bit
	 * "remains zero" until the global RCTL.RXEN/TCTL.TXEN are set, so
	 * E1000_RCTL_EN and E1000_TCTL_EN must already be written above.
	 */
	retries = 2000;
	while (retries-- > 0) {
		if ((igb_read32(igb, E1000_TXDCTL(0)) & E1000_TXDCTL_QUEUE_ENABLE) &&
		    (igb_read32(igb, E1000_RXDCTL(0)) & E1000_RXDCTL_QUEUE_ENABLE))
			break;
		usleep(10);
	}
	VFIO_ASSERT_GE(retries, 0);

	/*
	 * Program MSI-X interrupt routing per 82576 datasheet:
	 *
	 * GPIE (section 7.3.2.11, Table 7-47): set Multiple_MSIX (bit 4) to
	 * route interrupt causes through IVAR mapping, and EIAME (bit 30)
	 * to apply EIAM on MSI-X assertion (without EIAME, EIAM only
	 * applies on EICR read/write).
	 *
	 * EIAC (section 8.8.5): enable auto-clear of EICR for vector 0.
	 * Without auto-clear the cause stays set after delivery and the
	 * test can see spurious interrupts on the next memcpy batch.
	 *
	 * EIAM (section 8.8.6): enable auto-mask of EIMS for vector 0 on
	 * MSI-X assertion (effective because EIAME is set).
	 *
	 * IVAR (section 7.3.1.2, register definition in 8.8.13): map RX
	 * cause 0 to MSI-X vector 0 and mark the entry valid.
	 */
	igb_write32(igb, E1000_GPIE, E1000_GPIE_MSIX_MODE | E1000_GPIE_EIAME);
	igb_write32(igb, E1000_EIAC, MSIX_VECTOR_MASK);
	igb_write32(igb, E1000_EIAM, MSIX_VECTOR_MASK);

	/* Map vector 0 to interrupt cause 0 and mark it valid */
	igb_write32(igb, E1000_IVAR0, E1000_IVAR_VALID);

	/* Enable interrupts on vector 0 */
	igb_write32(igb, E1000_EIMS, MSIX_VECTOR_MASK);

	/* Initialize driver state and capability limits */
	igb->tx_tail = 0;
	igb->rx_tail = 0;

	device->driver.max_memcpy_size = IGB_MAX_CHUNK_SIZE;
	device->driver.max_memcpy_count = RING_SIZE - 1;
	device->driver.msi = MSIX_VECTOR;
}

static void igb_init(struct vfio_pci_device *device)
{
	struct igb *igb = to_igb_state(device);

	VFIO_ASSERT_GE(device->driver.region.size, sizeof(struct igb));

	igb->bar0 = device->bars[0].vaddr;

	igb_reset(igb);

	/*
	 * Enable MSI-X via VFIO before device-side register programming.
	 * vfio_pci_msix_enable() only touches the VFIO IRQ machinery and the
	 * PCI MSI-X capability via config space; it has no ordering
	 * dependency on the device-side writes performed by igb_hw_init().
	 * Placing it here keeps igb_hw_init() reusable from the reset
	 * recovery path (which calls vfio_pci_irq_reenable() instead).
	 */
	vfio_pci_msix_enable(device, MSIX_VECTOR, 1);

	igb_hw_init(device);
}

static void igb_remove(struct vfio_pci_device *device)
{
	struct igb *igb = to_igb_state(device);

	igb_write32(igb, E1000_RCTL, 0);
	igb_write32(igb, E1000_TCTL, 0);
	igb_reset(igb);

	vfio_pci_msix_disable(device);
}

static void igb_irq_disable(struct igb *igb)
{
	igb_write32(igb, E1000_EIMC, MSIX_VECTOR_MASK);
}

static void igb_irq_enable(struct igb *igb)
{
	igb_write32(igb, E1000_EIMS, MSIX_VECTOR_MASK);
}

static void igb_irq_clear(struct igb *igb)
{
	/*
	 * Use write-to-clear (datasheet 7.3.4.2).  In MSI-X mode with EIAC
	 * programmed, section 8.8.5 explicitly states "If any bits are set
	 * in EIAC, the EICR register should not be read", which rules out
	 * the read-to-clear path in 7.3.4.3.  Bits not in EIAC are still
	 * cleared by writing 1.
	 */
	igb_write32(igb, E1000_EICR, 0xFFFFFFFF);
}

static void igb_memcpy_start(struct vfio_pci_device *device, iova_t src,
			     iova_t dst, u64 size, u64 count)
{
	struct igb *igb = to_igb_state(device);
	struct igb_rx_desc *rx;
	struct igb_tx_desc *tx;
	u32 i;

	VFIO_ASSERT_GE(size, 60,
		       "IGB driver requires memcpy size to be at least 60 bytes (Ethernet minimum payload size)");

	igb_irq_disable(igb);

	for (i = 0; i < count; i++) {
		tx = &igb->tx_ring[igb->tx_tail];
		rx = &igb->rx_ring[igb->rx_tail];

		memset(tx, 0, sizeof(struct igb_tx_desc));
		memset(rx, 0, sizeof(struct igb_rx_desc));

		rx->read.pkt_addr = cpu_to_le64(dst);
		rx->read.hdr_addr = cpu_to_le64(0);

		tx->read.buffer_addr = cpu_to_le64(src);
		/*
		 * Build an advanced data descriptor per 82576 datasheet
		 * section 7.2.2.3.  DEXT marks the descriptor as advanced
		 * (required by hardware); DTYP=data selects the data
		 * descriptor; IFCS asks the MAC to append the Ethernet
		 * FCS (without it the frame is dropped as malformed);
		 * EOP marks end of packet.  DTALEN is the buffer length
		 * in bits 15:0 of cmd_type_len.
		 */
		tx->read.cmd_type_len = cpu_to_le32((uint32_t)size |
			E1000_ADVTXD_DTYP_DATA |
			E1000_ADVTXD_DCMD_DEXT |
			E1000_ADVTXD_DCMD_IFCS |
			E1000_ADVTXD_DCMD_EOP);
		/*
		 * PAYLEN (section 7.2.2.3.11) is the total payload size
		 * in olinfo_status[31:14].
		 */
		tx->read.olinfo_status =
			cpu_to_le32((uint32_t)size << E1000_ADVTXD_PAYLEN_SHIFT);

		igb->tx_tail = (igb->tx_tail + 1) % RING_SIZE;
		igb->rx_tail = (igb->rx_tail + 1) % RING_SIZE;
	}

	igb_write32(igb, E1000_RDT(0), igb->rx_tail);
	igb_write32(igb, E1000_TDT(0), igb->tx_tail);
}

/*
 * Reset the device via VFIO_DEVICE_RESET (PCIe FLR on the 82576) and
 * re-program it.  VFIO_DEVICE_RESET tears down the kernel-side MSI-X
 * trigger but leaves user-side eventfds intact, so re-arm the trigger
 * via vfio_pci_irq_reenable() before reprogramming so any caller-cached
 * eventfd remains valid.
 *
 * FLR clears device-side state to power-on reset values (datasheet
 * 4.2.1.5.1: a PF FLR is "equivalent to a D0->D3->D0 transition"), so
 * EIMS and EICR come back as 0 from their register-defined initial
 * values, and igb_hw_init() resets tx_tail/rx_tail to 0.  The next
 * igb_memcpy_start() will memset each descriptor it touches before
 * submission, so no explicit IMC/EICR writes or ring memsets are
 * needed here.
 */
static void igb_error_reset_and_reinit(struct vfio_pci_device *device)
{
	vfio_pci_device_reset(device);
	vfio_pci_msix_reenable(device, MSIX_VECTOR, 1);
	igb_hw_init(device);
}

static int igb_memcpy_wait(struct vfio_pci_device *device)
{
	struct igb *igb = to_igb_state(device);
	struct igb_rx_desc *rx;
	u32 status = 0;
	u32 prev_tail;
	int retries;

	prev_tail = (igb->rx_tail + RING_SIZE - 1) % RING_SIZE;
	rx = &igb->rx_ring[prev_tail];

	/*
	 * Real 82576 hardware processes the descriptor ring at line rate.
	 * max_memcpy_size = (RING_SIZE - 1) * IGB_MAX_CHUNK_SIZE ~= 4 MB,
	 * split into 4095 1 KB frames.  At 1 Gb/s (~125 MB/s) the worst
	 * valid memcpy takes ~32 ms on the wire, plus per-frame preamble,
	 * SFD, IFG and FCS overhead (~3%) and descriptor fetch/writeback
	 * latency.  Wait up to ~200 ms before declaring the device hung;
	 * ~6x the line-rate floor leaves comfortable headroom for host
	 * scheduling jitter while keeping the intentional invalid-DMA
	 * tests bounded.
	 */
	retries = 200;
	while (retries-- > 0) {
		status = le32_to_cpu(READ_ONCE(rx->wb.status_error));
		if (status & 1)
			break;
		usleep(1000);
	}

	if (status & 1)
		/*
		 * Ensure the test code doesn't speculatively read the DMA
		 * destination buffer before we have verified that the
		 * descriptor writeback is complete.
		 */
		rmb();

	igb_irq_clear(igb);

	igb_irq_enable(igb);

	if (status & 1)
		return 0;

	/*
	 * The descriptor never completed.  On real 82576 hardware this
	 * typically follows a DMA-read fault from one of the intentional
	 * unmapped-IOVA tests; the fault leaves the descriptor engine
	 * unable to service subsequent valid descriptors.  CTRL.RST alone
	 * reinitializes the queue registers but leaves the engine wedged
	 * for the current process, so a broader VFIO_DEVICE_RESET (FLR)
	 * is required.
	 */
	igb_error_reset_and_reinit(device);

	return -ETIMEDOUT;
}

static void igb_send_msi(struct vfio_pci_device *device)
{
	struct igb *igb = to_igb_state(device);

	igb_write32(igb, E1000_EICS, MSIX_VECTOR_MASK);
}

const struct vfio_pci_driver_ops igb_ops = {
	.name = "igb",
	.probe = igb_probe,
	.init = igb_init,
	.remove = igb_remove,
	.memcpy_start = igb_memcpy_start,
	.memcpy_wait = igb_memcpy_wait,
	.send_msi = igb_send_msi,
};
