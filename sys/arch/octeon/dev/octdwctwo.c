/*	$OpenBSD: octdwctwo.c,v 1.15 2022/09/04 08:42:39 mglocker Exp $	*/

/*
 * Copyright (c) 2015 Masao Uebayashi <uebayasi@tombiinc.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/octeonreg.h>
#include <machine/octeonvar.h>
#include <machine/octeon_model.h>

#include <octeon/dev/iobusvar.h>
#include <octeon/dev/octhcireg.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>

#include <dev/usb/dwc2/dwc2var.h>
#include <dev/usb/dwc2/dwc2.h>
#include <dev/usb/dwc2/dwc2_core.h>

struct octdwctwo_softc {
	struct dwc2_softc	sc_dwc2;

	/* USBN bus space */
	bus_space_tag_t		sc_bust;
	bus_space_handle_t	sc_regh;
	bus_space_handle_t	sc_regh2;

	void			*sc_ih;
};

int			octdwctwo_match(struct device *, void *, void *);
void			octdwctwo_attach(struct device *, struct device *,
			    void *);
int			octdwctwo_activate(struct device *, int);
int			octdwctwo_set_dma_addr(struct device *, bus_addr_t, int);
u_int64_t		octdwctwo_reg2_rd(struct octdwctwo_softc *, bus_size_t);
void			octdwctwo_reg2_wr(struct octdwctwo_softc *, bus_size_t,
			    u_int64_t);
void			octdwctwo_reg_set(struct octdwctwo_softc *, bus_size_t,
			    u_int64_t);
void			octdwctwo_reg_clear(struct octdwctwo_softc *,
			    bus_size_t, u_int64_t);
u_int32_t		octdwctwo_read_4(bus_space_tag_t, bus_space_handle_t,
			    bus_size_t);
void			octdwctwo_write_4(bus_space_tag_t, bus_space_handle_t,
			    bus_size_t, u_int32_t);


const struct cfattach octdwctwo_ca = {
	sizeof(struct octdwctwo_softc), octdwctwo_match, octdwctwo_attach,
	NULL, octdwctwo_activate
};

struct cfdriver dwctwo_cd = {
	NULL, "dwctwo", DV_DULL
};

static struct dwc2_core_params octdwctwo_params = {
	.otg_caps.hnp_support = 0,
	.otg_caps.srp_support = 0,
	.host_dma = 1,
	.dma_desc_enable = 0,
	.speed = 0,
	.enable_dynamic_fifo = 1,
	.en_multiple_tx_fifo = 0,
	.host_rx_fifo_size = 456,
	.host_nperio_tx_fifo_size = 912,
	.host_perio_tx_fifo_size = 256,
	.max_transfer_size = 65535,
	.max_packet_count = 511,
	.host_channels = 8,
	.phy_type = 1,
	.phy_utmi_width = 16,
	.phy_ulpi_ddr = 0,
	.phy_ulpi_ext_vbus = 0,
	.i2c_enable = 0,
	.ulpi_fs_ls = 0,
	.host_support_fs_ls_low_power = 0,
	.host_ls_low_power_phy_clk = 0,
	.ts_dline = 0,
	.reload_ctl = 0,
	.ahbcfg = 0x7,
	.uframe_sched = 1,
	.external_id_pin_ctl = 0,
};

/*
 * This bus space tag adjusts register addresses to account for
 * dwc2 using little endian addressing.  dwc2 only does 32bit reads
 * and writes, so only those functions are provided.
 */
bus_space_t octdwctwo_tag = {
	.bus_base = PHYS_TO_XKPHYS(0, CCA_NC),
	.bus_private = NULL,
	._space_read_4 =	octdwctwo_read_4,
	._space_write_4 =	octdwctwo_write_4,
	._space_map =		iobus_space_map,
	._space_unmap =		iobus_space_unmap,
	._space_subregion =	generic_space_region,
	._space_vaddr =		generic_space_vaddr
};

int
octdwctwo_match(struct device *parent, void *match, void *aux)
{
	int id;

	id = octeon_get_chipid();
	switch (octeon_model_family(id)) {
	case OCTEON_MODEL_FAMILY_CN30XX:
	case OCTEON_MODEL_FAMILY_CN31XX:
	case OCTEON_MODEL_FAMILY_CN50XX:
		return (1);
	default:
		return (0);
	}
}

void
octdwctwo_attach(struct device *parent, struct device *self, void *aux)
{
	struct octdwctwo_softc *sc = (struct octdwctwo_softc *)self;
	struct iobus_attach_args *aa = aux;
	uint64_t clk;
	int rc;

	sc->sc_dwc2.sc_iot = &octdwctwo_tag;
	sc->sc_dwc2.sc_bus.pipe_size = sizeof(struct usbd_pipe);
	sc->sc_dwc2.sc_bus.dmatag = aa->aa_dmat;
	sc->sc_dwc2.sc_params = &octdwctwo_params;
	sc->sc_dwc2.sc_set_dma_addr = octdwctwo_set_dma_addr;

	rc = bus_space_map(sc->sc_dwc2.sc_iot, USBC_BASE, USBC_SIZE,
	    0, &sc->sc_dwc2.sc_ioh);
	KASSERT(rc == 0);

	sc->sc_bust = aa->aa_bust;
	rc = bus_space_map(sc->sc_bust, USBN_BASE, USBN_SIZE,
	    0, &sc->sc_regh);
	KASSERT(rc == 0);
	rc = bus_space_map(sc->sc_bust, USBN_2_BASE, USBN_2_SIZE,
	    0, &sc->sc_regh2);
	KASSERT(rc == 0);

	/*
	 * Clock setup.
	 */
	clk = bus_space_read_8(sc->sc_bust, sc->sc_regh, USBN_CLK_CTL_OFFSET);
	clk |= USBN_CLK_CTL_POR;
	clk &= ~(USBN_CLK_CTL_HRST | USBN_CLK_CTL_PRST | USBN_CLK_CTL_HCLK_RST |
	    USBN_CLK_CTL_ENABLE | USBN_CLK_CTL_P_C_SEL | USBN_CLK_CTL_P_RTYPE);
	clk |= SET_USBN_CLK_CTL_DIVIDE(0x4ULL)
	    | SET_USBN_CLK_CTL_DIVIDE2(0x0ULL);

	bus_space_write_8(sc->sc_bust, sc->sc_regh, USBN_CLK_CTL_OFFSET, clk);
	bus_space_read_8(sc->sc_bust, sc->sc_regh, USBN_CLK_CTL_OFFSET);

	/*
	 * Reset HCLK and wait for it to stabilize.
	 */
	octdwctwo_reg_set(sc, USBN_CLK_CTL_OFFSET, USBN_CLK_CTL_HCLK_RST);
	delay(64);

	octdwctwo_reg_clear(sc, USBN_CLK_CTL_OFFSET, USBN_CLK_CTL_POR);

	/*
	 * Wait for the PHY clock to start.
	 */
	delay(1000);

	octdwctwo_reg_set(sc, USBN_USBP_CTL_STATUS_OFFSET,
	    USBN_USBP_CTL_STATUS_ATE_RESET);
	delay(10);

	octdwctwo_reg_clear(sc, USBN_USBP_CTL_STATUS_OFFSET,
			USBN_USBP_CTL_STATUS_ATE_RESET);
	octdwctwo_reg_set(sc, USBN_CLK_CTL_OFFSET, USBN_CLK_CTL_PRST);

	/*
	 * Select host mode.
	 */
	octdwctwo_reg_clear(sc, USBN_USBP_CTL_STATUS_OFFSET,
	    USBN_USBP_CTL_STATUS_HST_MODE);
	delay(1);

	octdwctwo_reg_set(sc, USBN_CLK_CTL_OFFSET, USBN_CLK_CTL_HRST);

	/*
	 * Enable clock.
	 */
	octdwctwo_reg_set(sc, USBN_CLK_CTL_OFFSET, USBN_CLK_CTL_ENABLE);
	delay(1);

	strlcpy(sc->sc_dwc2.sc_vendor, "Octeon", sizeof(sc->sc_dwc2.sc_vendor));

	rc = dwc2_init(&sc->sc_dwc2);
	if (rc != 0)
		return;

	printf("\n");

	sc->sc_dwc2.sc_child = config_found(&sc->sc_dwc2.sc_bus.bdev,
	    &sc->sc_dwc2.sc_bus, usbctlprint);

	sc->sc_ih = octeon_intr_establish(CIU_INT_USB, IPL_VM | IPL_MPSAFE,
	    dwc2_intr, (void *)&sc->sc_dwc2, sc->sc_dwc2.sc_bus.bdev.dv_xname);
	KASSERT(sc->sc_ih != NULL);
}

int
octdwctwo_activate(struct device *self, int act)
{
	struct octdwctwo_softc *sc = (struct octdwctwo_softc *)self;
	uint64_t clk;
	int rv = 0;

	switch (act) {
	case DVACT_POWERDOWN:
		/*
		 * Put the controller into reset mode.
		 * It appears necessary to hold this state for a moment.
		 * Otherwise subsequent attempts to reinitialize the controller
		 * may fail because of hanging or trapping access
		 * of DWC2 core registers.
		 */
		clk = bus_space_read_8(sc->sc_bust, sc->sc_regh,
		    USBN_CLK_CTL_OFFSET);
		clk |= USBN_CLK_CTL_POR;
		clk |= USBN_CLK_CTL_HCLK_RST;
		clk |= USBN_CLK_CTL_ENABLE;
		clk &= ~USBN_CLK_CTL_HRST;
		clk &= ~USBN_CLK_CTL_PRST;
		bus_space_write_8(sc->sc_bust, sc->sc_regh,
		    USBN_CLK_CTL_OFFSET, clk);
		(void)bus_space_read_8(sc->sc_bust, sc->sc_regh,
		    USBN_CLK_CTL_OFFSET);
		delay(50000);
		break;
	default:
		break;
	}
	return rv;
}

int
octdwctwo_set_dma_addr(struct device *data, bus_addr_t dma_addr, int ch)
{
	struct octdwctwo_softc *sc = (struct octdwctwo_softc *)data;

	octdwctwo_reg2_wr(sc,
	    USBN_DMA0_INB_CHN0_OFFSET + ch * 0x8, dma_addr);
	octdwctwo_reg2_wr(sc,
	    USBN_DMA0_OUTB_CHN0_OFFSET + ch * 0x8, dma_addr);
	return 0;
}

u_int64_t
octdwctwo_reg2_rd(struct octdwctwo_softc *sc, bus_size_t offset)
{
	u_int64_t value;

	value = bus_space_read_8(sc->sc_bust, sc->sc_regh2, offset);
	return value;
}

void
octdwctwo_reg2_wr(struct octdwctwo_softc *sc, bus_size_t offset, u_int64_t value)
{
	bus_space_write_8(sc->sc_bust, sc->sc_regh2, offset, value);
	/* guarantee completion of the store operation on RSL registers*/
	bus_space_read_8(sc->sc_bust, sc->sc_regh2, offset);
}

void
octdwctwo_reg_set(struct octdwctwo_softc *sc, bus_size_t offset,
    u_int64_t bits)
{
	u_int64_t value;
	value = bus_space_read_8(sc->sc_bust, sc->sc_regh, offset);
	value |= bits;

	bus_space_write_8(sc->sc_bust, sc->sc_regh, offset, value);
	bus_space_read_8(sc->sc_bust, sc->sc_regh, offset);
}

void
octdwctwo_reg_clear(struct octdwctwo_softc *sc, bus_size_t offset,
    u_int64_t bits)
{
	u_int64_t value;
	value = bus_space_read_8(sc->sc_bust, sc->sc_regh, offset);
	value &= ~bits;

	bus_space_write_8(sc->sc_bust, sc->sc_regh, offset, value);
	bus_space_read_8(sc->sc_bust, sc->sc_regh, offset);
}

u_int32_t
octdwctwo_read_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o)
{
	return *(volatile u_int32_t *)(h + (o^4));
}

void
octdwctwo_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t o,
    u_int32_t v)
{
	*(volatile u_int32_t *)(h + (o^4)) = v;
}
