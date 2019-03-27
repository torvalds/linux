/*-
 * Copyright (c) 2016 Ganbold Tsagaankhuu <ganbold@freebsd.org>
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

/*
 * Allwinner Consumer IR controller
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/sysctl.h>
#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>

#include <dev/evdev/input.h>
#include <dev/evdev/evdev.h>

#define	READ(_sc, _r)		bus_read_4((_sc)->res[0], (_r))
#define	WRITE(_sc, _r, _v)	bus_write_4((_sc)->res[0], (_r), (_v))

/* IR Control */
#define	AW_IR_CTL			0x00
/* Global Enable */
#define	 AW_IR_CTL_GEN			(1 << 0)
/* RX enable */
#define	 AW_IR_CTL_RXEN			(1 << 1)
/* CIR mode enable */
#define	 AW_IR_CTL_MD			(1 << 4) | (1 << 5)

/* RX Config Reg */
#define	AW_IR_RXCTL			0x10
/* Pulse Polarity Invert flag */
#define	 AW_IR_RXCTL_RPPI		(1 << 2)

/* RX Data */
#define	AW_IR_RXFIFO			0x20

/* RX Interrupt Control */
#define	AW_IR_RXINT			0x2C
/* RX FIFO Overflow */
#define	 AW_IR_RXINT_ROI_EN		(1 << 0)
/* RX Packet End */
#define	 AW_IR_RXINT_RPEI_EN		(1 << 1)
/* RX FIFO Data Available */
#define	 AW_IR_RXINT_RAI_EN		(1 << 4)
/* RX FIFO available byte level */
#define	 AW_IR_RXINT_RAL(val)		((val) << 8)

/* RX Interrupt Status Reg */
#define	AW_IR_RXSTA			0x30
/* RX FIFO Get Available Counter */
#define	 AW_IR_RXSTA_COUNTER(val)	(((val) >> 8) & (sc->fifo_size * 2 - 1))
/* Clear all interrupt status */
#define	 AW_IR_RXSTA_CLEARALL		0xff

/* IR Sample Configure Reg */
#define	AW_IR_CIR			0x34

/*
 * Frequency sample: 23437.5Hz (Cycle: 42.7us)
 * Pulse of NEC Remote > 560us
 */
/* Filter Threshold = 8 * 42.7 = ~341us < 500us */
#define	 AW_IR_RXFILT_VAL		(((8) & 0x3f) << 2)
/* Idle Threshold = (2 + 1) * 128 * 42.7 = ~16.4ms > 9ms */
#define	 AW_IR_RXIDLE_VAL		(((2) & 0xff) << 8)

/* Bit 15 - value (pulse/space) */
#define	VAL_MASK			0x80
/* Bits 0:14 - sample duration  */
#define	PERIOD_MASK			0x7f

/* Clock rate for IR0 or IR1 clock in CIR mode */
#define	AW_IR_BASE_CLK			3000000
/* Frequency sample 3MHz/64 = 46875Hz (21.3us) */
#define	AW_IR_SAMPLE_64			(0 << 0)
/* Frequency sample 3MHz/128 = 23437.5Hz (42.7us) */
#define	AW_IR_SAMPLE_128		(1 << 0)

#define	AW_IR_ERROR_CODE		0xffffffff
#define	AW_IR_REPEAT_CODE		0x0

/* 80 * 42.7 = ~3.4ms, Lead1(4.5ms) > AW_IR_L1_MIN */
#define	AW_IR_L1_MIN			80
/* 40 * 42.7 = ~1.7ms, Lead0(4.5ms) Lead0R(2.25ms) > AW_IR_L0_MIN */
#define	AW_IR_L0_MIN			40
/* 26 * 42.7 = ~1109us ~= 561 * 2, Pulse < AW_IR_PMAX */
#define	AW_IR_PMAX			26
/* 26 * 42.7 = ~1109us ~= 561 * 2, D1 > AW_IR_DMID, D0 <= AW_IR_DMID */
#define	AW_IR_DMID			26
/* 53 * 42.7 = ~2263us ~= 561 * 4, D < AW_IR_DMAX */
#define	AW_IR_DMAX			53

/* Active Thresholds */
#define	AW_IR_ACTIVE_T			((0 & 0xff) << 16)
#define	AW_IR_ACTIVE_T_C		((1 & 0xff) << 23)

/* Code masks */
#define	CODE_MASK			0x00ff00ff
#define	INV_CODE_MASK			0xff00ff00
#define	VALID_CODE_MASK			0x00ff0000

#define	A10_IR				1
#define	A13_IR				2

#define	AW_IR_RAW_BUF_SIZE		128

struct aw_ir_softc {
	device_t		dev;
	struct resource		*res[2];
	void *			intrhand;
	int			fifo_size;
	int			dcnt;	/* Packet Count */
	unsigned char		buf[AW_IR_RAW_BUF_SIZE];
	struct evdev_dev	*sc_evdev;
};

static struct resource_spec aw_ir_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE | RF_SHAREABLE },
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{ "allwinner,sun4i-a10-ir",	A10_IR },
	{ "allwinner,sun5i-a13-ir",	A13_IR },
	{ NULL,				0 }
};

static void
aw_ir_buf_reset(struct aw_ir_softc *sc)
{

	sc->dcnt = 0;
}

static void
aw_ir_buf_write(struct aw_ir_softc *sc, unsigned char data)
{

	if (sc->dcnt < AW_IR_RAW_BUF_SIZE)
		sc->buf[sc->dcnt++] = data;
	else
		if (bootverbose)
			device_printf(sc->dev, "IR RX Buffer Full!\n");
}

static int
aw_ir_buf_full(struct aw_ir_softc *sc)
{

	return (sc->dcnt >= AW_IR_RAW_BUF_SIZE);
}

static unsigned char
aw_ir_read_data(struct aw_ir_softc *sc)
{

	return (unsigned char)(READ(sc, AW_IR_RXFIFO) & 0xff);
}

static unsigned long
aw_ir_decode_packets(struct aw_ir_softc *sc)
{
	unsigned long len, code;
	unsigned char val, last;
	unsigned int active_delay;
	int i, bitcount;

	if (bootverbose)
		device_printf(sc->dev, "sc->dcnt = %d\n", sc->dcnt);

	/* Find Lead 1 (bit separator) */
	active_delay = (AW_IR_ACTIVE_T + 1) * (AW_IR_ACTIVE_T_C ? 128 : 1);
	len = 0;
	len += (active_delay >> 1);
	if (bootverbose)
		device_printf(sc->dev, "Initial len: %ld\n", len);
	for (i = 0;  i < sc->dcnt; i++) {
		val = sc->buf[i];
		if (val & VAL_MASK)
			len += val & PERIOD_MASK;
		else {
			if (len > AW_IR_L1_MIN)
				break;
			len = 0;
		}
	}
	if (bootverbose)
		device_printf(sc->dev, "len = %ld\n", len);
	if ((val & VAL_MASK) || (len <= AW_IR_L1_MIN)) {
		if (bootverbose)
			device_printf(sc->dev, "Bit separator error\n");
		goto error_code;
	}

	/* Find Lead 0 (bit length) */
	len = 0;
	for (; i < sc->dcnt; i++) {
		val = sc->buf[i];
		if (val & VAL_MASK) {
			if(len > AW_IR_L0_MIN)
				break;
			len = 0;
		} else
			len += val & PERIOD_MASK;
	}
	if ((!(val & VAL_MASK)) || (len <= AW_IR_L0_MIN)) {
		if (bootverbose)
			device_printf(sc->dev, "Bit length error\n");
		goto error_code;
	}

	/* Start decoding */
	code = 0;
	bitcount = 0;
	last = 1;
	len = 0;
	for (; i < sc->dcnt; i++) {
		val = sc->buf[i];
		if (last) {
			if (val & VAL_MASK)
				len += val & PERIOD_MASK;
			else {
				if (len > AW_IR_PMAX) {
					if (bootverbose)
						device_printf(sc->dev,
						    "Pulse error\n");
					goto error_code;
				}
				last = 0;
				len = val & PERIOD_MASK;
			}
		} else {
			if (val & VAL_MASK) {
				if (len > AW_IR_DMAX) {
					if (bootverbose)
						device_printf(sc->dev,
						    "Distant error\n");
					goto error_code;
				} else {
					if (len > AW_IR_DMID) {
						/* Decode */
						code |= 1 << bitcount;
					}
					bitcount++;
					if (bitcount == 32)
						break;  /* Finish decoding */
				}
				last = 1;
				len = val & PERIOD_MASK;
			} else
				len += val & PERIOD_MASK;
		}
	}
	return (code);

error_code:

	return (AW_IR_ERROR_CODE);
}

static int
aw_ir_validate_code(unsigned long code)
{
	unsigned long v1, v2;

	/* Don't check address */
	v1 = code & CODE_MASK;
	v2 = (code & INV_CODE_MASK) >> 8;

	if (((v1 ^ v2) & VALID_CODE_MASK) == VALID_CODE_MASK)
		return (0);	/* valid */
	else
		return (1);	/* invalid */
}

static void
aw_ir_intr(void *arg)
{
	struct aw_ir_softc *sc;
	uint32_t val;
	int i, dcnt;
	unsigned long ir_code;
	int stat;

	sc = (struct aw_ir_softc *)arg;

	/* Read RX interrupt status */
	val = READ(sc, AW_IR_RXSTA);
	if (bootverbose)
		device_printf(sc->dev, "RX interrupt status: %x\n", val);

	/* Clean all pending interrupt statuses */
	WRITE(sc, AW_IR_RXSTA, val | AW_IR_RXSTA_CLEARALL);

	/* When Rx FIFO Data available or Packet end */
	if (val & (AW_IR_RXINT_RAI_EN | AW_IR_RXINT_RPEI_EN)) {
		if (bootverbose)
			device_printf(sc->dev,
			    "RX FIFO Data available or Packet end\n");
		/* Get available message count in RX FIFO */
		dcnt  = AW_IR_RXSTA_COUNTER(val);
		/* Read FIFO */
		for (i = 0; i < dcnt; i++) {
			if (aw_ir_buf_full(sc)) {
				if (bootverbose)
					device_printf(sc->dev,
					    "raw buffer full\n");
				break;
			} else
				aw_ir_buf_write(sc, aw_ir_read_data(sc));
		}
	}

	if (val & AW_IR_RXINT_RPEI_EN) {
		/* RX Packet end */
		if (bootverbose)
			device_printf(sc->dev, "RX Packet end\n");
		ir_code = aw_ir_decode_packets(sc);
		stat = aw_ir_validate_code(ir_code);
		if (stat == 0) {
			evdev_push_event(sc->sc_evdev,
			    EV_MSC, MSC_SCAN, ir_code);
			evdev_sync(sc->sc_evdev);
		}
		if (bootverbose) {
			device_printf(sc->dev, "Final IR code: %lx\n",
			    ir_code);
			device_printf(sc->dev, "IR code status: %d\n",
			    stat);
		}
		sc->dcnt = 0;
	}
	if (val & AW_IR_RXINT_ROI_EN) {
		/* RX FIFO overflow */
		if (bootverbose)
			device_printf(sc->dev, "RX FIFO overflow\n");
		/* Flush raw buffer */
		aw_ir_buf_reset(sc);
	}
}

static int
aw_ir_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Allwinner CIR controller");
	return (BUS_PROBE_DEFAULT);
}

static int
aw_ir_attach(device_t dev)
{
	struct aw_ir_softc *sc;
	hwreset_t rst_apb;
	clk_t clk_ir, clk_gate;
	int err;
	uint32_t val = 0;

	clk_ir = clk_gate = NULL;
	rst_apb = NULL;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (bus_alloc_resources(dev, aw_ir_spec, sc->res) != 0) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	switch (ofw_bus_search_compatible(dev, compat_data)->ocd_data) {
	case A10_IR:
		sc->fifo_size = 16;
		break;
	case A13_IR:
		sc->fifo_size = 64;
		break;
	}

	/* De-assert reset */
	if (hwreset_get_by_ofw_idx(dev, 0, 0, &rst_apb) == 0) {
		err = hwreset_deassert(rst_apb);
		if (err != 0) {
			device_printf(dev, "cannot de-assert reset\n");
			goto error;
		}
	}

	/* Reset buffer */
	aw_ir_buf_reset(sc);

	/* Get clocks and enable them */
	err = clk_get_by_ofw_name(dev, 0, "apb", &clk_gate);
	if (err != 0) {
		device_printf(dev, "Cannot get gate clock\n");
		goto error;
	}
	err = clk_get_by_ofw_name(dev, 0, "ir", &clk_ir);
	if (err != 0) {
		device_printf(dev, "Cannot get IR clock\n");
		goto error;
	}
	/* Set clock rate */
	err = clk_set_freq(clk_ir, AW_IR_BASE_CLK, 0);
	if (err != 0) {
		device_printf(dev, "cannot set IR clock rate\n");
		goto error;
	}
	/* Enable clocks */
	err = clk_enable(clk_gate);
	if (err != 0) {
		device_printf(dev, "Cannot enable clk gate\n");
		goto error;
	}
	err = clk_enable(clk_ir);
	if (err != 0) {
		device_printf(dev, "Cannot enable IR clock\n");
		goto error;
	}

	if (bus_setup_intr(dev, sc->res[1],
	    INTR_TYPE_MISC | INTR_MPSAFE, NULL, aw_ir_intr, sc,
	    &sc->intrhand)) {
		bus_release_resources(dev, aw_ir_spec, sc->res);
		device_printf(dev, "cannot setup interrupt handler\n");
		return (ENXIO);
	}

	/* Enable CIR Mode */
	WRITE(sc, AW_IR_CTL, AW_IR_CTL_MD);

	/*
	 * Set clock sample, filter, idle thresholds.
	 * Frequency sample = 3MHz/128 = 23437.5Hz (42.7us)
	 */
	val = AW_IR_SAMPLE_128;
	val |= (AW_IR_RXFILT_VAL | AW_IR_RXIDLE_VAL);
	val |= (AW_IR_ACTIVE_T | AW_IR_ACTIVE_T_C);
	WRITE(sc, AW_IR_CIR, val);

	/* Invert Input Signal */
	WRITE(sc, AW_IR_RXCTL, AW_IR_RXCTL_RPPI);

	/* Clear All RX Interrupt Status */
	WRITE(sc, AW_IR_RXSTA, AW_IR_RXSTA_CLEARALL);

	/*
	 * Enable RX interrupt in case of overflow, packet end
	 * and FIFO available.
	 * RX FIFO Threshold = FIFO size / 2
	 */
	WRITE(sc, AW_IR_RXINT, AW_IR_RXINT_ROI_EN | AW_IR_RXINT_RPEI_EN |
	    AW_IR_RXINT_RAI_EN | AW_IR_RXINT_RAL((sc->fifo_size >> 1) - 1));

	/* Enable IR Module */
	val = READ(sc, AW_IR_CTL);
	WRITE(sc, AW_IR_CTL, val | AW_IR_CTL_GEN | AW_IR_CTL_RXEN);

	sc->sc_evdev = evdev_alloc();
	evdev_set_name(sc->sc_evdev, device_get_desc(sc->dev));
	evdev_set_phys(sc->sc_evdev, device_get_nameunit(sc->dev));
	evdev_set_id(sc->sc_evdev, BUS_HOST, 0, 0, 0);
	evdev_support_event(sc->sc_evdev, EV_SYN);
	evdev_support_event(sc->sc_evdev, EV_MSC);
	evdev_support_msc(sc->sc_evdev, MSC_SCAN);

	err = evdev_register(sc->sc_evdev);
	if (err) {
		device_printf(dev,
		    "failed to register evdev: error=%d\n", err);
		goto error;
	}

	return (0);
error:
	if (clk_gate != NULL)
		clk_release(clk_gate);
	if (clk_ir != NULL)
		clk_release(clk_ir);
	if (rst_apb != NULL)
		hwreset_release(rst_apb);
	evdev_free(sc->sc_evdev);
	sc->sc_evdev = NULL;	/* Avoid double free */

	bus_release_resources(dev, aw_ir_spec, sc->res);
	return (ENXIO);
}

static device_method_t aw_ir_methods[] = {
	DEVMETHOD(device_probe, aw_ir_probe),
	DEVMETHOD(device_attach, aw_ir_attach),

	DEVMETHOD_END
};

static driver_t aw_ir_driver = {
	"aw_ir",
	aw_ir_methods,
	sizeof(struct aw_ir_softc),
};
static devclass_t aw_ir_devclass;

DRIVER_MODULE(aw_ir, simplebus, aw_ir_driver, aw_ir_devclass, 0, 0);
MODULE_DEPEND(aw_ir, evdev, 1, 1, 1);
