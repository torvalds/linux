/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * XHCI driver for Tegra SoCs.
 */
#include "opt_bus.h"
#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/condvar.h>
#include <sys/firmware.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/resource.h>


#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/extres/phy/phy.h>
#include <dev/extres/regulator/regulator.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usb_busdma.h>
#include <dev/usb/usb_process.h>
#include <dev/usb/usb_controller.h>
#include <dev/usb/usb_bus.h>
#include <dev/usb/controller/xhci.h>
#include <dev/usb/controller/xhcireg.h>

#include <arm/nvidia/tegra_pmc.h>

#include "usbdevs.h"

/* FPCI address space */
#define	T_XUSB_CFG_0				0x000
#define	T_XUSB_CFG_1				0x004
#define	 CFG_1_BUS_MASTER				(1 << 2)
#define	 CFG_1_MEMORY_SPACE				(1 << 1)
#define	 CFG_1_IO_SPACE					(1 << 0)

#define	T_XUSB_CFG_2				0x008
#define	T_XUSB_CFG_3				0x00C
#define	T_XUSB_CFG_4				0x010
#define	 CFG_4_BASE_ADDRESS(x)				(((x) & 0x1FFFF) << 15)

#define	T_XUSB_CFG_5				0x014
#define	T_XUSB_CFG_ARU_MAILBOX_CMD		0x0E4
#define  ARU_MAILBOX_CMD_INT_EN				(1U << 31)
#define  ARU_MAILBOX_CMD_DEST_XHCI			(1  << 30)
#define  ARU_MAILBOX_CMD_DEST_SMI			(1  << 29)
#define  ARU_MAILBOX_CMD_DEST_PME			(1  << 28)
#define  ARU_MAILBOX_CMD_DEST_FALC			(1  << 27)

#define	T_XUSB_CFG_ARU_MAILBOX_DATA_IN		0x0E8
#define	 ARU_MAILBOX_DATA_IN_DATA(x)			(((x) & 0xFFFFFF) <<  0)
#define	 ARU_MAILBOX_DATA_IN_TYPE(x)			(((x) & 0x0000FF) << 24)

#define	T_XUSB_CFG_ARU_MAILBOX_DATA_OUT		0x0EC
#define	 ARU_MAILBOX_DATA_OUT_DATA(x)			(((x) >>  0) & 0xFFFFFF)
#define	 ARU_MAILBOX_DATA_OUT_TYPE(x)			(((x) >> 24) & 0x0000FF)

#define	T_XUSB_CFG_ARU_MAILBOX_OWNER		0x0F0
#define	 ARU_MAILBOX_OWNER_SW				2
#define	 ARU_MAILBOX_OWNER_FW				1
#define	 ARU_MAILBOX_OWNER_NONE				0

#define	XUSB_CFG_ARU_C11_CSBRANGE		0x41C	/* ! UNDOCUMENTED ! */
#define	 ARU_C11_CSBRANGE_PAGE(x)			((x) >> 9)
#define	 ARU_C11_CSBRANGE_ADDR(x)			(0x800 + ((x) & 0x1FF))
#define	XUSB_CFG_ARU_SMI_INTR			0x428	/* ! UNDOCUMENTED ! */
#define  ARU_SMI_INTR_EN				(1 << 3)
#define  ARU_SMI_INTR_FW_HANG				(1 << 1)
#define	XUSB_CFG_ARU_RST			0x42C	/* ! UNDOCUMENTED ! */
#define	 ARU_RST_RESET					(1 << 0)

#define	XUSB_HOST_CONFIGURATION			0x180
#define	 CONFIGURATION_CLKEN_OVERRIDE			(1U<< 31)
#define	 CONFIGURATION_PW_NO_DEVSEL_ERR_CYA		(1 << 19)
#define	 CONFIGURATION_INITIATOR_READ_IDLE		(1 << 18)
#define	 CONFIGURATION_INITIATOR_WRITE_IDLE		(1 << 17)
#define	 CONFIGURATION_WDATA_LEAD_CYA			(1 << 15)
#define	 CONFIGURATION_WR_INTRLV_CYA			(1 << 14)
#define	 CONFIGURATION_TARGET_READ_IDLE			(1 << 11)
#define	 CONFIGURATION_TARGET_WRITE_IDLE		(1 << 10)
#define	 CONFIGURATION_MSI_VEC_EMPTY			(1 <<  9)
#define	 CONFIGURATION_UFPCI_MSIAW			(1 <<  7)
#define	 CONFIGURATION_UFPCI_PWPASSPW			(1 <<  6)
#define	 CONFIGURATION_UFPCI_PASSPW			(1 <<  5)
#define	 CONFIGURATION_UFPCI_PWPASSNPW			(1 <<  4)
#define	 CONFIGURATION_DFPCI_PWPASSNPW			(1 <<  3)
#define	 CONFIGURATION_DFPCI_RSPPASSPW			(1 <<  2)
#define	 CONFIGURATION_DFPCI_PASSPW			(1 <<  1)
#define	 CONFIGURATION_EN_FPCI				(1 <<  0)

/* IPFS address space */
#define	XUSB_HOST_FPCI_ERROR_MASKS		0x184
#define	 FPCI_ERROR_MASTER_ABORT			(1 <<  2)
#define	 FPCI_ERRORI_DATA_ERROR				(1 <<  1)
#define	 FPCI_ERROR_TARGET_ABORT			(1 <<  0)

#define	XUSB_HOST_INTR_MASK			0x188
#define	 INTR_IP_INT_MASK				(1 << 16)
#define	 INTR_MSI_MASK					(1 <<  8)
#define	 INTR_INT_MASK					(1 <<  0)

#define	XUSB_HOST_CLKGATE_HYSTERESIS		0x1BC

 /* CSB Falcon CPU */
#define	XUSB_FALCON_CPUCTL			0x100
#define	 CPUCTL_STOPPED					(1 << 5)
#define	 CPUCTL_HALTED					(1 << 4)
#define	 CPUCTL_HRESET					(1 << 3)
#define	 CPUCTL_SRESET					(1 << 2)
#define	 CPUCTL_STARTCPU				(1 << 1)
#define	 CPUCTL_IINVAL					(1 << 0)

#define	XUSB_FALCON_BOOTVEC			0x104
#define	XUSB_FALCON_DMACTL			0x10C
#define	XUSB_FALCON_IMFILLRNG1			0x154
#define	 IMFILLRNG1_TAG_HI(x)				(((x) & 0xFFF) << 16)
#define	 IMFILLRNG1_TAG_LO(x)				(((x) & 0xFFF) <<  0)
#define	XUSB_FALCON_IMFILLCTL			0x158

/* CSB mempool */
#define	XUSB_CSB_MEMPOOL_APMAP			0x10181C
#define	 APMAP_BOOTPATH					(1U << 31)

#define	XUSB_CSB_MEMPOOL_ILOAD_ATTR		0x101A00
#define	XUSB_CSB_MEMPOOL_ILOAD_BASE_LO		0x101A04
#define	XUSB_CSB_MEMPOOL_ILOAD_BASE_HI		0x101A08
#define	XUSB_CSB_MEMPOOL_L2IMEMOP_SIZE		0x101A10
#define	 L2IMEMOP_SIZE_OFFSET(x)			(((x) & 0x3FF) <<  8)
#define	 L2IMEMOP_SIZE_SIZE(x)				(((x) & 0x0FF) << 24)

#define	XUSB_CSB_MEMPOOL_L2IMEMOP_TRIG		0x101A14
#define	 L2IMEMOP_INVALIDATE_ALL			(0x40 << 24)
#define	 L2IMEMOP_LOAD_LOCKED_RESULT			(0x11 << 24)

#define	XUSB_CSB_MEMPOOL_L2IMEMOP_RESULT        0x101A18
#define	 L2IMEMOP_RESULT_VLD       (1U << 31)

#define XUSB_CSB_IMEM_BLOCK_SIZE	256

#define	TEGRA_XHCI_SS_HIGH_SPEED	120000000
#define	TEGRA_XHCI_SS_LOW_SPEED		 12000000

/* MBOX commands. */
#define	MBOX_CMD_MSG_ENABLED			 1
#define	MBOX_CMD_INC_FALC_CLOCK			 2
#define	MBOX_CMD_DEC_FALC_CLOCK			 3
#define	MBOX_CMD_INC_SSPI_CLOCK			 4
#define	MBOX_CMD_DEC_SSPI_CLOCK			 5
#define	MBOX_CMD_SET_BW				 6
#define	MBOX_CMD_SET_SS_PWR_GATING		 7
#define	MBOX_CMD_SET_SS_PWR_UNGATING		 8
#define	MBOX_CMD_SAVE_DFE_CTLE_CTX		 9
#define	MBOX_CMD_AIRPLANE_MODE_ENABLED		10
#define	MBOX_CMD_AIRPLANE_MODE_DISABLED		11
#define	MBOX_CMD_START_HSIC_IDLE		12
#define	MBOX_CMD_STOP_HSIC_IDLE			13
#define	MBOX_CMD_DBC_WAKE_STACK			14
#define	MBOX_CMD_HSIC_PRETEND_CONNECT		15
#define	MBOX_CMD_RESET_SSPI			16
#define	MBOX_CMD_DISABLE_SS_LFPS_DETECTION	17
#define	MBOX_CMD_ENABLE_SS_LFPS_DETECTION	18

/* MBOX responses. */
#define	MBOX_CMD_ACK				(0x80 + 0)
#define	MBOX_CMD_NAK				(0x80 + 1)


#define	IPFS_WR4(_sc, _r, _v)	bus_write_4((_sc)->mem_res_ipfs, (_r), (_v))
#define	IPFS_RD4(_sc, _r)	bus_read_4((_sc)->mem_res_ipfs, (_r))
#define	FPCI_WR4(_sc, _r, _v)	bus_write_4((_sc)->mem_res_fpci, (_r), (_v))
#define	FPCI_RD4(_sc, _r)	bus_read_4((_sc)->mem_res_fpci, (_r))

#define	LOCK(_sc)		mtx_lock(&(_sc)->mtx)
#define	UNLOCK(_sc)		mtx_unlock(&(_sc)->mtx)
#define	SLEEP(_sc, timeout)						\
    mtx_sleep(sc, &sc->mtx, 0, "tegra_xhci", timeout);
#define	LOCK_INIT(_sc)							\
    mtx_init(&_sc->mtx, device_get_nameunit(_sc->dev), "tegra_xhci", MTX_DEF)
#define	LOCK_DESTROY(_sc)	mtx_destroy(&_sc->mtx)
#define	ASSERT_LOCKED(_sc)	mtx_assert(&_sc->mtx, MA_OWNED)
#define	ASSERT_UNLOCKED(_sc)	mtx_assert(&_sc->mtx, MA_NOTOWNED)

struct tegra_xusb_fw_hdr {
	uint32_t	boot_loadaddr_in_imem;
	uint32_t	boot_codedfi_offset;
	uint32_t	boot_codetag;
	uint32_t	boot_codesize;

	uint32_t	phys_memaddr;
	uint16_t	reqphys_memsize;
	uint16_t	alloc_phys_memsize;

	uint32_t	rodata_img_offset;
	uint32_t	rodata_section_start;
	uint32_t	rodata_section_end;
	uint32_t	main_fnaddr;

	uint32_t	fwimg_cksum;
	uint32_t	fwimg_created_time;

	uint32_t	imem_resident_start;
	uint32_t	imem_resident_end;
	uint32_t	idirect_start;
	uint32_t	idirect_end;
	uint32_t	l2_imem_start;
	uint32_t	l2_imem_end;
	uint32_t	version_id;
	uint8_t		init_ddirect;
	uint8_t		reserved[3];
	uint32_t	phys_addr_log_buffer;
	uint32_t	total_log_entries;
	uint32_t	dequeue_ptr;
	uint32_t	dummy[2];
	uint32_t	fwimg_len;
	uint8_t		magic[8];
	uint32_t	ss_low_power_entry_timeout;
	uint8_t		num_hsic_port;
	uint8_t		ss_portmap;
	uint8_t		build;
	uint8_t		padding[137]; /* Pad to 256 bytes */
};

/* Compatible devices. */
static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-xusb",	1},
	{NULL,		 		0}
};

struct tegra_xhci_softc {
	struct xhci_softc 	xhci_softc;
	device_t		dev;
	struct mtx		mtx;
	struct resource		*mem_res_fpci;
	struct resource		*mem_res_ipfs;
	struct resource		*irq_res_mbox;
	void			*irq_hdl_mbox;

	clk_t			clk_xusb_host;
	clk_t			clk_xusb_gate;
	clk_t			clk_xusb_falcon_src;
	clk_t			clk_xusb_ss;
	clk_t			clk_xusb_hs_src;
	clk_t			clk_xusb_fs_src;
	hwreset_t		hwreset_xusb_host;
	hwreset_t		hwreset_xusb_ss;
	regulator_t		supply_avddio_pex;
	regulator_t		supply_dvddio_pex;
	regulator_t		supply_avdd_usb;
	regulator_t		supply_avdd_pll_utmip;
	regulator_t		supply_avdd_pll_erefe;
	regulator_t		supply_avdd_usb_ss_pll;
	regulator_t		supply_hvdd_usb_ss;
	regulator_t		supply_hvdd_usb_ss_pll_e;
	phy_t 			phy_usb2_0;
	phy_t 			phy_usb2_1;
	phy_t 			phy_usb2_2;
	phy_t 			phy_usb3_0;

	struct intr_config_hook	irq_hook;
	bool			xhci_inited;
	char			*fw_name;
	vm_offset_t		fw_vaddr;
	vm_size_t		fw_size;
};

static uint32_t
CSB_RD4(struct tegra_xhci_softc *sc, uint32_t addr)
{

	FPCI_WR4(sc, XUSB_CFG_ARU_C11_CSBRANGE, ARU_C11_CSBRANGE_PAGE(addr));
	return (FPCI_RD4(sc, ARU_C11_CSBRANGE_ADDR(addr)));
}

static void
CSB_WR4(struct tegra_xhci_softc *sc, uint32_t addr, uint32_t val)
{

	FPCI_WR4(sc, XUSB_CFG_ARU_C11_CSBRANGE, ARU_C11_CSBRANGE_PAGE(addr));
	FPCI_WR4(sc, ARU_C11_CSBRANGE_ADDR(addr), val);
}

static int
get_fdt_resources(struct tegra_xhci_softc *sc, phandle_t node)
{
	int rv;

	rv = regulator_get_by_ofw_property(sc->dev, 0, "avddio-pex-supply",
	    &sc->supply_avddio_pex);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'avddio-pex' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "dvddio-pex-supply",
	    &sc->supply_dvddio_pex);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'dvddio-pex' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "avdd-usb-supply",
	    &sc->supply_avdd_usb);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'avdd-usb' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "avdd-pll-utmip-supply",
	    &sc->supply_avdd_pll_utmip);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'avdd-pll-utmip' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "avdd-pll-erefe-supply",
	    &sc->supply_avdd_pll_erefe);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'avdd-pll-erefe' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "avdd-usb-ss-pll-supply",
	    &sc->supply_avdd_usb_ss_pll);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'avdd-usb-ss-pll' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "hvdd-usb-ss-supply",
	    &sc->supply_hvdd_usb_ss);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'hvdd-usb-ss' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0,
	    "hvdd-usb-ss-pll-e-supply", &sc->supply_hvdd_usb_ss_pll_e);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'hvdd-usb-ss-pll-e' regulator\n");
		return (ENXIO);
	}

	rv = hwreset_get_by_ofw_name(sc->dev, 0, "xusb_host",
	    &sc->hwreset_xusb_host);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'xusb_host' reset\n");
		return (ENXIO);
	}
	rv = hwreset_get_by_ofw_name(sc->dev, 0, "xusb_ss",
	    &sc->hwreset_xusb_ss);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'xusb_ss' reset\n");
		return (ENXIO);
	}

	rv = phy_get_by_ofw_name(sc->dev, 0, "usb2-0", &sc->phy_usb2_0);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'usb2-0' phy\n");
		return (ENXIO);
	}
	rv = phy_get_by_ofw_name(sc->dev, 0, "usb2-1", &sc->phy_usb2_1);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'usb2-1' phy\n");
		return (ENXIO);
	}
	rv = phy_get_by_ofw_name(sc->dev, 0, "usb2-2", &sc->phy_usb2_2);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'usb2-2' phy\n");
		return (ENXIO);
	}
	rv = phy_get_by_ofw_name(sc->dev, 0, "usb3-0", &sc->phy_usb3_0);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'usb3-0' phy\n");
		return (ENXIO);
	}

	rv = clk_get_by_ofw_name(sc->dev, 0, "xusb_host",
	    &sc->clk_xusb_host);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'xusb_host' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "xusb_falcon_src",
	    &sc->clk_xusb_falcon_src);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'xusb_falcon_src' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "xusb_ss",
	    &sc->clk_xusb_ss);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'xusb_ss' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "xusb_hs_src",
	    &sc->clk_xusb_hs_src);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'xusb_hs_src' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "xusb_fs_src",
	    &sc->clk_xusb_fs_src);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'xusb_fs_src' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_index_prop(sc->dev, 0, "freebsd,clock-xusb-gate", 0,
	    &sc->clk_xusb_gate);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'xusb_gate' clock\n");
		return (ENXIO);
	}
	return (0);
}

static int
enable_fdt_resources(struct tegra_xhci_softc *sc)
{
	int rv;

	rv = hwreset_assert(sc->hwreset_xusb_host);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot reset 'xusb_host' reset\n");
		return (rv);
	}
	rv = hwreset_assert(sc->hwreset_xusb_ss);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot reset 'xusb_ss' reset\n");
		return (rv);
	}

	rv = regulator_enable(sc->supply_avddio_pex);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'avddio_pex' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_dvddio_pex);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'dvddio_pex' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_avdd_usb);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'avdd_usb' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_avdd_pll_utmip);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'avdd_pll_utmip-5v' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_avdd_pll_erefe);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'avdd_pll_erefe' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_avdd_usb_ss_pll);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'avdd_usb_ss_pll' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_hvdd_usb_ss);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'hvdd_usb_ss' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_hvdd_usb_ss_pll_e);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'hvdd_usb_ss_pll_e' regulator\n");
		return (rv);
	}

	/* Power off XUSB host and XUSB SS domains. */
	rv = tegra_powergate_power_off(TEGRA_POWERGATE_XUSBA);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot powerdown  'xusba' domain\n");
		return (rv);
	}
	rv = tegra_powergate_power_off(TEGRA_POWERGATE_XUSBC);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot powerdown  'xusbc' domain\n");
		return (rv);
	}

	/* Setup XUSB ss_src clock first */
	clk_set_freq(sc->clk_xusb_ss, TEGRA_XHCI_SS_HIGH_SPEED, 0);
	if (rv != 0)
		return (rv);

	/* The XUSB gate clock must be enabled before XUSBA can be powered. */
	rv = clk_enable(sc->clk_xusb_gate);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'xusb_gate' clock\n");
		return (rv);
	}

	/* Power on XUSB host and XUSB SS domains. */
	rv = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_XUSBC,
	    sc->clk_xusb_host, sc->hwreset_xusb_host);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot powerup 'xusbc' domain\n");
		return (rv);
	}
	rv = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_XUSBA,
	    sc->clk_xusb_ss, sc->hwreset_xusb_ss);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot powerup 'xusba' domain\n");
		return (rv);
	}

	/* Enable rest of clocks */
	rv = clk_enable(sc->clk_xusb_falcon_src);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'xusb_falcon_src' clock\n");
		return (rv);
	}
	rv = clk_enable(sc->clk_xusb_fs_src);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'xusb_fs_src' clock\n");
		return (rv);
	}
	rv = clk_enable(sc->clk_xusb_hs_src);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'xusb_hs_src' clock\n");
		return (rv);
	}

	rv = phy_enable(sc->phy_usb2_0);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable USB2_0 phy\n");
		return (rv);
	}
	rv = phy_enable(sc->phy_usb2_1);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable USB2_1 phy\n");
		return (rv);
	}
	rv = phy_enable(sc->phy_usb2_2);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable USB2_2 phy\n");
		return (rv);
	}
	rv = phy_enable(sc->phy_usb3_0);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable USB3_0 phy\n");
		return (rv);
	}

	return (0);
}

/* Respond by ACK/NAK back to FW */
static void
mbox_send_ack(struct tegra_xhci_softc *sc, uint32_t cmd, uint32_t data)
{
	uint32_t reg;

	reg = ARU_MAILBOX_DATA_IN_TYPE(cmd) | ARU_MAILBOX_DATA_IN_DATA(data);
	FPCI_WR4(sc, T_XUSB_CFG_ARU_MAILBOX_DATA_IN, reg);

	reg = FPCI_RD4(sc, T_XUSB_CFG_ARU_MAILBOX_CMD);
	reg |= ARU_MAILBOX_CMD_DEST_FALC | ARU_MAILBOX_CMD_INT_EN;
	FPCI_WR4(sc, T_XUSB_CFG_ARU_MAILBOX_CMD, reg);
}

/* Sent command to FW */
static int
mbox_send_cmd(struct tegra_xhci_softc *sc, uint32_t cmd, uint32_t data)
{
	uint32_t reg;
	int i;

	reg = FPCI_RD4(sc, T_XUSB_CFG_ARU_MAILBOX_OWNER);
	if (reg != ARU_MAILBOX_OWNER_NONE) {
		device_printf(sc->dev,
		    "CPU mailbox is busy: 0x%08X\n", reg);
		return (EBUSY);
	}
	/* XXX Is this right? Retry loop? Wait before send? */
	FPCI_WR4(sc, T_XUSB_CFG_ARU_MAILBOX_OWNER, ARU_MAILBOX_OWNER_SW);
	reg = FPCI_RD4(sc, T_XUSB_CFG_ARU_MAILBOX_OWNER);
	if (reg != ARU_MAILBOX_OWNER_SW) {
		device_printf(sc->dev,
		    "Cannot acquire CPU mailbox: 0x%08X\n", reg);
		return (EBUSY);
	}
	reg = ARU_MAILBOX_DATA_IN_TYPE(cmd) | ARU_MAILBOX_DATA_IN_DATA(data);
	FPCI_WR4(sc, T_XUSB_CFG_ARU_MAILBOX_DATA_IN, reg);

	reg = FPCI_RD4(sc, T_XUSB_CFG_ARU_MAILBOX_CMD);
	reg |= ARU_MAILBOX_CMD_DEST_FALC | ARU_MAILBOX_CMD_INT_EN;
	FPCI_WR4(sc, T_XUSB_CFG_ARU_MAILBOX_CMD, reg);

	for (i = 250; i > 0; i--) {
		reg = FPCI_RD4(sc, T_XUSB_CFG_ARU_MAILBOX_OWNER);
		if (reg == ARU_MAILBOX_OWNER_NONE)
			break;
		DELAY(100);
	}
	if (i <= 0) {
		device_printf(sc->dev,
		    "Command response timeout: 0x%08X\n", reg);
		return (ETIMEDOUT);
	}

	return(0);
}

static void
process_msg(struct tegra_xhci_softc *sc, uint32_t req_cmd, uint32_t req_data,
    uint32_t *resp_cmd, uint32_t *resp_data)
{
	uint64_t freq;
	int rv;

	/* In most cases, data are echoed back. */
	*resp_data = req_data;
	switch (req_cmd) {
	case MBOX_CMD_INC_FALC_CLOCK:
	case MBOX_CMD_DEC_FALC_CLOCK:
		rv = clk_set_freq(sc->clk_xusb_falcon_src, req_data * 1000ULL,
		    0);
		if (rv == 0) {
			rv = clk_get_freq(sc->clk_xusb_falcon_src, &freq);
			*resp_data = (uint32_t)(freq / 1000);
		}
		*resp_cmd = rv == 0 ? MBOX_CMD_ACK: MBOX_CMD_NAK;
		break;

	case MBOX_CMD_INC_SSPI_CLOCK:
	case MBOX_CMD_DEC_SSPI_CLOCK:
		rv = clk_set_freq(sc->clk_xusb_ss, req_data * 1000ULL,
		    0);
		if (rv == 0) {
			rv = clk_get_freq(sc->clk_xusb_ss, &freq);
			*resp_data = (uint32_t)(freq / 1000);
		}
		*resp_cmd = rv == 0 ? MBOX_CMD_ACK: MBOX_CMD_NAK;
		break;

	case MBOX_CMD_SET_BW:
		/* No respense is expected. */
		*resp_cmd = 0;
		break;

	case MBOX_CMD_SET_SS_PWR_GATING:
	case MBOX_CMD_SET_SS_PWR_UNGATING:
		*resp_cmd = MBOX_CMD_NAK;
		break;

	case MBOX_CMD_SAVE_DFE_CTLE_CTX:
		/* Not implemented yet. */
		*resp_cmd = MBOX_CMD_ACK;
		break;


	case MBOX_CMD_START_HSIC_IDLE:
	case MBOX_CMD_STOP_HSIC_IDLE:
		/* Not implemented yet. */
		*resp_cmd = MBOX_CMD_NAK;
		break;

	case MBOX_CMD_DISABLE_SS_LFPS_DETECTION:
	case MBOX_CMD_ENABLE_SS_LFPS_DETECTION:
		/* Not implemented yet. */
		*resp_cmd = MBOX_CMD_NAK;
		break;

	case MBOX_CMD_AIRPLANE_MODE_ENABLED:
	case MBOX_CMD_AIRPLANE_MODE_DISABLED:
	case MBOX_CMD_DBC_WAKE_STACK:
	case MBOX_CMD_HSIC_PRETEND_CONNECT:
	case MBOX_CMD_RESET_SSPI:
		device_printf(sc->dev,
		    "Received unused/unexpected command: %u\n", req_cmd);
		*resp_cmd = 0;
		break;

	default:
		device_printf(sc->dev,
		    "Received unknown command: %u\n", req_cmd);
	}
}

static void
intr_mbox(void *arg)
{
	struct tegra_xhci_softc *sc;
	uint32_t reg, msg, resp_cmd, resp_data;

	sc = (struct tegra_xhci_softc *)arg;

	/* Clear interrupt first */
	reg = FPCI_RD4(sc, XUSB_CFG_ARU_SMI_INTR);
	FPCI_WR4(sc, XUSB_CFG_ARU_SMI_INTR, reg);
	if (reg & ARU_SMI_INTR_FW_HANG) {
		device_printf(sc->dev,
		    "XUSB CPU firmware hang!!! CPUCTL: 0x%08X\n",
		    CSB_RD4(sc, XUSB_FALCON_CPUCTL));
	}

	msg = FPCI_RD4(sc, T_XUSB_CFG_ARU_MAILBOX_DATA_OUT);
	resp_cmd = 0;
	process_msg(sc, ARU_MAILBOX_DATA_OUT_TYPE(msg),
	   ARU_MAILBOX_DATA_OUT_DATA(msg), &resp_cmd, &resp_data);
	if (resp_cmd != 0)
		mbox_send_ack(sc, resp_cmd, resp_data);
	else
		FPCI_WR4(sc, T_XUSB_CFG_ARU_MAILBOX_OWNER,
		    ARU_MAILBOX_OWNER_NONE);

	reg = FPCI_RD4(sc, T_XUSB_CFG_ARU_MAILBOX_CMD);
	reg &= ~ARU_MAILBOX_CMD_DEST_SMI;
	FPCI_WR4(sc, T_XUSB_CFG_ARU_MAILBOX_CMD, reg);

}

static int
load_fw(struct tegra_xhci_softc *sc)
{
	const struct firmware *fw;
	const struct tegra_xusb_fw_hdr *fw_hdr;
	vm_paddr_t fw_paddr, fw_base;
	vm_offset_t fw_vaddr;
	vm_size_t fw_size;
	uint32_t code_tags, code_size;
	struct clocktime fw_clock;
	struct timespec	fw_timespec;
	int i;

	/* Reset ARU */
	FPCI_WR4(sc, XUSB_CFG_ARU_RST, ARU_RST_RESET);
	DELAY(3000);

	/* Check if FALCON already runs */
	if (CSB_RD4(sc, XUSB_CSB_MEMPOOL_ILOAD_BASE_LO) != 0) {
		device_printf(sc->dev,
		    "XUSB CPU is already loaded, CPUCTL: 0x%08X\n",
			 CSB_RD4(sc, XUSB_FALCON_CPUCTL));
		return (0);
	}

	fw = firmware_get(sc->fw_name);
	if (fw == NULL) {
		device_printf(sc->dev, "Cannot read xusb firmware\n");
		return (ENOENT);
	}

	/* Allocate uncached memory and copy firmware into. */
	fw_hdr = (const struct tegra_xusb_fw_hdr *)fw->data;
	fw_size = fw_hdr->fwimg_len;

	fw_vaddr = kmem_alloc_contig(fw_size, M_WAITOK, 0, -1UL, PAGE_SIZE, 0,
	    VM_MEMATTR_UNCACHEABLE);
	fw_paddr = vtophys(fw_vaddr);
	fw_hdr = (const struct tegra_xusb_fw_hdr *)fw_vaddr;
	memcpy((void *)fw_vaddr, fw->data, fw_size);

	firmware_put(fw, FIRMWARE_UNLOAD);
	sc->fw_vaddr = fw_vaddr;
	sc->fw_size = fw_size;

	/* Setup firmware physical address and size. */
	fw_base = fw_paddr + sizeof(*fw_hdr);
	CSB_WR4(sc, XUSB_CSB_MEMPOOL_ILOAD_ATTR, fw_size);
	CSB_WR4(sc, XUSB_CSB_MEMPOOL_ILOAD_BASE_LO, fw_base & 0xFFFFFFFF);
	CSB_WR4(sc, XUSB_CSB_MEMPOOL_ILOAD_BASE_HI, (uint64_t)fw_base >> 32);
	CSB_WR4(sc, XUSB_CSB_MEMPOOL_APMAP, APMAP_BOOTPATH);

	/* Invalidate full L2IMEM context. */
	CSB_WR4(sc, XUSB_CSB_MEMPOOL_L2IMEMOP_TRIG,
	    L2IMEMOP_INVALIDATE_ALL);

	/* Program load of L2IMEM by boot code. */
	code_tags = howmany(fw_hdr->boot_codetag, XUSB_CSB_IMEM_BLOCK_SIZE);
	code_size = howmany(fw_hdr->boot_codesize, XUSB_CSB_IMEM_BLOCK_SIZE);
	CSB_WR4(sc, XUSB_CSB_MEMPOOL_L2IMEMOP_SIZE,
	    L2IMEMOP_SIZE_OFFSET(code_tags) |
	    L2IMEMOP_SIZE_SIZE(code_size));

	/* Execute L2IMEM boot code fetch. */
	CSB_WR4(sc, XUSB_CSB_MEMPOOL_L2IMEMOP_TRIG,
	    L2IMEMOP_LOAD_LOCKED_RESULT);

	/* Program FALCON auto-fill range and block count */
	CSB_WR4(sc, XUSB_FALCON_IMFILLCTL, code_size);
	CSB_WR4(sc, XUSB_FALCON_IMFILLRNG1,
	    IMFILLRNG1_TAG_LO(code_tags) |
	    IMFILLRNG1_TAG_HI(code_tags + code_size));

	CSB_WR4(sc, XUSB_FALCON_DMACTL, 0);
	/* Wait for CPU */
	for (i = 500; i > 0; i--) {
		if (CSB_RD4(sc, XUSB_CSB_MEMPOOL_L2IMEMOP_RESULT) &
		     L2IMEMOP_RESULT_VLD)
			break;
		DELAY(100);
	}
	if (i <= 0) {
		device_printf(sc->dev, "Timedout while wating for DMA, "
		    "state: 0x%08X\n",
		    CSB_RD4(sc, XUSB_CSB_MEMPOOL_L2IMEMOP_RESULT));
		return (ETIMEDOUT);
	}

	/* Boot FALCON cpu */
	CSB_WR4(sc, XUSB_FALCON_BOOTVEC, fw_hdr->boot_codetag);
	CSB_WR4(sc, XUSB_FALCON_CPUCTL, CPUCTL_STARTCPU);

	/* Wait for CPU */
	for (i = 50; i > 0; i--) {
		if (CSB_RD4(sc, XUSB_FALCON_CPUCTL) == CPUCTL_STOPPED)
			break;
		DELAY(100);
	}
	if (i <= 0) {
		device_printf(sc->dev, "Timedout while wating for FALCON cpu, "
		    "state: 0x%08X\n", CSB_RD4(sc, XUSB_FALCON_CPUCTL));
		return (ETIMEDOUT);
	}

	fw_timespec.tv_sec = fw_hdr->fwimg_created_time;
	fw_timespec.tv_nsec = 0;
	clock_ts_to_ct(&fw_timespec, &fw_clock);
	device_printf(sc->dev,
	    " Falcon firmware version: %02X.%02X.%04X,"
	    " (%d/%d/%d %d:%02d:%02d UTC)\n",
	    (fw_hdr->version_id >> 24) & 0xFF,(fw_hdr->version_id >> 15) & 0xFF,
	    fw_hdr->version_id & 0xFFFF,
	    fw_clock.day, fw_clock.mon, fw_clock.year,
	    fw_clock.hour, fw_clock.min, fw_clock.sec);

	return (0);
}

static int
init_hw(struct tegra_xhci_softc *sc)
{
	int rv;
	uint32_t reg;
	rman_res_t base_addr;

	base_addr = rman_get_start(sc->xhci_softc.sc_io_res);

	/* Enable FPCI access */
	reg = IPFS_RD4(sc, XUSB_HOST_CONFIGURATION);
	reg |= CONFIGURATION_EN_FPCI;
	IPFS_WR4(sc, XUSB_HOST_CONFIGURATION, reg);
	IPFS_RD4(sc, XUSB_HOST_CONFIGURATION);


	/* Program bar for XHCI base address */
	reg = FPCI_RD4(sc, T_XUSB_CFG_4);
	reg &= ~CFG_4_BASE_ADDRESS(~0);
	reg |= CFG_4_BASE_ADDRESS((uint32_t)base_addr >> 15);
	FPCI_WR4(sc, T_XUSB_CFG_4, reg);
	FPCI_WR4(sc, T_XUSB_CFG_5, (uint32_t)((uint64_t)(base_addr) >> 32));

	/* Enable bus master */
	reg = FPCI_RD4(sc, T_XUSB_CFG_1);
	reg |= CFG_1_IO_SPACE;
	reg |= CFG_1_MEMORY_SPACE;
	reg |= CFG_1_BUS_MASTER;
	FPCI_WR4(sc, T_XUSB_CFG_1, reg);

	/* Enable Interrupts */
	reg = IPFS_RD4(sc, XUSB_HOST_INTR_MASK);
	reg |= INTR_IP_INT_MASK;
	IPFS_WR4(sc, XUSB_HOST_INTR_MASK, reg);

	/* Set hysteresis */
	IPFS_WR4(sc, XUSB_HOST_CLKGATE_HYSTERESIS, 128);

	rv = load_fw(sc);
	if (rv != 0)
		return rv;
	return (0);
}

static int
tegra_xhci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "Nvidia Tegra XHCI controller");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
tegra_xhci_detach(device_t dev)
{
	struct tegra_xhci_softc *sc;
	struct xhci_softc *xsc;

	sc = device_get_softc(dev);
	xsc = &sc->xhci_softc;

	/* during module unload there are lots of children leftover */
	device_delete_children(dev);
	if (sc->xhci_inited) {
		usb_callout_drain(&xsc->sc_callout);
		xhci_halt_controller(xsc);
	}

	if (xsc->sc_irq_res && xsc->sc_intr_hdl) {
		bus_teardown_intr(dev, xsc->sc_irq_res, xsc->sc_intr_hdl);
		xsc->sc_intr_hdl = NULL;
	}
	if (xsc->sc_irq_res) {
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(xsc->sc_irq_res), xsc->sc_irq_res);
		xsc->sc_irq_res = NULL;
	}
	if (xsc->sc_io_res != NULL) {
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(xsc->sc_io_res), xsc->sc_io_res);
		xsc->sc_io_res = NULL;
	}
	if (sc->xhci_inited)
		xhci_uninit(xsc);
	if (sc->irq_hdl_mbox != NULL)
		bus_teardown_intr(dev, sc->irq_res_mbox, sc->irq_hdl_mbox);
	if (sc->fw_vaddr != 0)
		kmem_free(sc->fw_vaddr, sc->fw_size);
	LOCK_DESTROY(sc);
	return (0);
}

static int
tegra_xhci_attach(device_t dev)
{
	struct tegra_xhci_softc *sc;
	struct xhci_softc *xsc;
	int rv, rid;
	phandle_t node;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->fw_name = "tegra124_xusb_fw";
	node = ofw_bus_get_node(dev);
	xsc = &sc->xhci_softc;
	LOCK_INIT(sc);

	rv = get_fdt_resources(sc, node);
	if (rv != 0) {
		rv = ENXIO;
		goto error;
	}
	rv = enable_fdt_resources(sc);
	if (rv != 0) {
		rv = ENXIO;
		goto error;
	}

	/* Allocate resources. */
	rid = 0;
	xsc->sc_io_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (xsc->sc_io_res == NULL) {
		device_printf(dev,
		    "Could not allocate HCD memory resources\n");
		rv = ENXIO;
		goto error;
	}
	rid = 1;
	sc->mem_res_fpci = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res_fpci == NULL) {
		device_printf(dev,
		    "Could not allocate FPCI memory resources\n");
		rv = ENXIO;
		goto error;
	}
	rid = 2;
	sc->mem_res_ipfs = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->mem_res_ipfs == NULL) {
		device_printf(dev,
		    "Could not allocate IPFS memory resources\n");
		rv = ENXIO;
		goto error;
	}

	rid = 0;
	xsc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (xsc->sc_irq_res == NULL) {
		device_printf(dev, "Could not allocate HCD IRQ resources\n");
		rv = ENXIO;
		goto error;
	}
	rid = 1;
	sc->irq_res_mbox = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->irq_res_mbox == NULL) {
		device_printf(dev, "Could not allocate MBOX IRQ resources\n");
		rv = ENXIO;
		goto error;
	}

	rv = init_hw(sc);
	if (rv != 0) {
		device_printf(dev, "Could not initialize  XUSB hardware\n");
		goto error;
	}

	/* Wakeup and enable firmaware */
	rv = mbox_send_cmd(sc, MBOX_CMD_MSG_ENABLED, 0);
	if (rv != 0) {
		device_printf(sc->dev, "Could not enable XUSB firmware\n");
		goto error;
	}

	/* Fill data for XHCI driver. */
	xsc->sc_bus.parent = dev;
	xsc->sc_bus.devices = xsc->sc_devices;
	xsc->sc_bus.devices_max = XHCI_MAX_DEVICES;

	xsc->sc_io_tag = rman_get_bustag(xsc->sc_io_res);
	xsc->sc_io_hdl = rman_get_bushandle(xsc->sc_io_res);
	xsc->sc_io_size = rman_get_size(xsc->sc_io_res);
	strlcpy(xsc->sc_vendor, "Nvidia", sizeof(xsc->sc_vendor));

	/* Add USB bus device. */
	xsc->sc_bus.bdev = device_add_child(sc->dev, "usbus", -1);
	if (xsc->sc_bus.bdev == NULL) {
		device_printf(sc->dev, "Could not add USB device\n");
		rv = ENXIO;
		goto error;
	}
	device_set_ivars(xsc->sc_bus.bdev, &xsc->sc_bus);
	device_set_desc(xsc->sc_bus.bdev, "Nvidia USB 3.0 controller");

	rv = xhci_init(xsc, sc->dev, 1);
	if (rv != 0) {
		device_printf(sc->dev, "USB init failed: %d\n", rv);
		goto error;
	}
	sc->xhci_inited = true;
	rv = xhci_start_controller(xsc);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Could not start XHCI controller: %d\n", rv);
		goto error;
	}

	rv = bus_setup_intr(dev, sc->irq_res_mbox, INTR_TYPE_MISC | INTR_MPSAFE,
	    NULL, intr_mbox, sc, &sc->irq_hdl_mbox);
	if (rv != 0) {
		device_printf(dev, "Could not setup error IRQ: %d\n",rv);
		xsc->sc_intr_hdl = NULL;
		goto error;
	}

	rv = bus_setup_intr(dev, xsc->sc_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    NULL, (driver_intr_t *)xhci_interrupt, xsc, &xsc->sc_intr_hdl);
	if (rv != 0) {
		device_printf(dev, "Could not setup error IRQ: %d\n",rv);
		xsc->sc_intr_hdl = NULL;
		goto error;
	}

	/* Probe the bus. */
	rv = device_probe_and_attach(xsc->sc_bus.bdev);
	if (rv != 0) {
		device_printf(sc->dev, "Could not initialize USB: %d\n", rv);
		goto error;
	}

	return (0);

error:
panic("XXXXX");
	tegra_xhci_detach(dev);
	return (rv);
}

static device_method_t xhci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, tegra_xhci_probe),
	DEVMETHOD(device_attach, tegra_xhci_attach),
	DEVMETHOD(device_detach, tegra_xhci_detach),
	DEVMETHOD(device_suspend, bus_generic_suspend),
	DEVMETHOD(device_resume, bus_generic_resume),
	DEVMETHOD(device_shutdown, bus_generic_shutdown),

	/* Bus interface */
	DEVMETHOD(bus_print_child, bus_generic_print_child),

	DEVMETHOD_END
};

static devclass_t xhci_devclass;
static DEFINE_CLASS_0(xhci, xhci_driver, xhci_methods,
    sizeof(struct tegra_xhci_softc));
DRIVER_MODULE(tegra_xhci, simplebus, xhci_driver, xhci_devclass, NULL, NULL);
MODULE_DEPEND(tegra_xhci, usb, 1, 1, 1);
