/*	$OpenBSD: zs.c,v 1.35 2024/09/04 07:54:52 mglocker Exp $	*/
/*	$NetBSD: zs.c,v 1.29 2001/05/30 15:24:24 lukem Exp $	*/

/*-
 * Copyright (c) 1996 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Gordon W. Ross.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Zilog Z8530 Dual UART driver (machine-dependent part)
 *
 * Runs two serial lines per chip using slave drivers.
 * Plain tty/async lines use the zstty slave.
 * Sun keyboard/mouse uses the zskbd/zsms slaves.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/tty.h>
#include <sys/time.h>
#include <sys/syslog.h>

#include <machine/autoconf.h>
#include <machine/openfirm.h>
#include <machine/conf.h>
#include <machine/cpu.h>
#include <machine/psl.h>
#include <machine/z8530var.h>

#include <dev/cons.h>
#include <dev/ic/z8530reg.h>
#include <sparc64/dev/fhcvar.h>
#include <ddb/db_output.h>

#include <sparc64/dev/cons.h>

struct cfdriver zs_cd = {
	NULL, "zs", DV_TTY
};

/*
 * Some warts needed by z8530tty.c -
 * The default parity REALLY needs to be the same as the PROM uses,
 * or you can not see messages done with printf during boot-up...
 */
int zs_def_cflag = (CREAD | CS8 | HUPCL);
int zs_major = 12;

/*
 * The Sun provides a 4.9152 MHz clock to the ZS chips.
 */
#define PCLK	(9600 * 512)	/* PCLK pin input clock rate */

#define	ZS_DELAY()

/* The layout of this is hardware-dependent (padding, order). */
struct zschan {
	volatile u_char	zc_csr;		/* ctrl,status, and indirect access */
	u_char		zc_xxx0;
	volatile u_char	zc_data;	/* data */
	u_char		zc_xxx1;
};
struct zsdevice {
	/* Yes, they are backwards. */
	struct	zschan zs_chan_b;
	struct	zschan zs_chan_a;
};

/* ZS channel used as the console device (if any) */
void *zs_conschan_get, *zs_conschan_put;

static u_char zs_init_reg[16] = {
	0,	/* 0: CMD (reset, etc.) */
	0,	/* 1: No interrupts yet. */
	0,	/* 2: IVECT */
	ZSWR3_RX_8 | ZSWR3_RX_ENABLE,
	ZSWR4_CLK_X16 | ZSWR4_ONESB,
	ZSWR5_TX_8 | ZSWR5_TX_ENABLE,
	0,	/* 6: TXSYNC/SYNCLO */
	0,	/* 7: RXSYNC/SYNCHI */
	0,	/* 8: alias for data port */
	ZSWR9_MASTER_IE | ZSWR9_NO_VECTOR,
	0,	/*10: Misc. TX/RX control bits */
	ZSWR11_TXCLK_BAUD | ZSWR11_RXCLK_BAUD,
	((PCLK/32)/9600)-2,	/*12: BAUDLO (default=9600) */
	0,			/*13: BAUDHI (default=9600) */
	ZSWR14_BAUD_ENA | ZSWR14_BAUD_FROM_PCLK,
	ZSWR15_BREAK_IE,
};

/* Console ops */
static int  zscngetc(dev_t);
static void zscnputc(dev_t, int);

struct consdev zs_consdev = {
	NULL,
	NULL,
	zscngetc,
	zscnputc,
	nullcnpollc,
	NULL,
};


/****************************************************************
 * Autoconfig
 ****************************************************************/

/* Definition of the driver for autoconfig. */
static int  zs_match_sbus(struct device *, void *, void *);
static void zs_attach_sbus(struct device *, struct device *, void *);

static int  zs_match_fhc(struct device *, void *, void *);
static void zs_attach_fhc(struct device *, struct device *, void *);

static void zs_attach(struct zsc_softc *, struct zsdevice *, int);
static int  zs_print(void *, const char *name);

const struct cfattach zs_sbus_ca = {
	sizeof(struct zsc_softc), zs_match_sbus, zs_attach_sbus
};

const struct cfattach zs_fhc_ca = {
	sizeof(struct zsc_softc), zs_match_fhc, zs_attach_fhc
};

/* Interrupt handlers. */
static int zshard(void *);
static void zssoft(void *);

static int zs_get_speed(struct zs_chanstate *);

/* Console device support */
static int zs_console_flags(int, int, int);

/*
 * Is the zs chip present?
 */
static int
zs_match_sbus(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct sbus_attach_args *sa = aux;

	if (strcmp(cf->cf_driver->cd_name, sa->sa_name) != 0)
		return (0);

	return (1);
}

static int
zs_match_fhc(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = vcf;
	struct fhc_attach_args *fa = aux;

	if (strcmp(cf->cf_driver->cd_name, fa->fa_name) != 0)
		return (0);
	return (1);
}

static void
zs_attach_sbus(struct device *parent, struct device *self, void *aux)
{
	struct zsc_softc *zsc = (void *) self;
	struct sbus_attach_args *sa = aux;
	struct zsdevice *zsaddr;
	bus_space_handle_t kvaddr;

	if (sa->sa_nintr == 0) {
		printf(" no interrupt lines\n");
		return;
	}

	/* Only map registers once. */
	if (sa->sa_npromvaddrs) {
		/*
		 * We're converting from a 32-bit pointer to a 64-bit
		 * pointer.  Since the 32-bit entity is negative, but
		 * the kernel is still mapped into the lower 4GB
		 * range, this needs to be zero-extended.
		 *
		 * XXXXX If we map the kernel and devices into the
		 * high 4GB range, this needs to be changed to
		 * sign-extend the address.
		 */
		zsaddr = (struct zsdevice *)
		    (unsigned long int)sa->sa_promvaddrs[0];
	} else {
		if (sbus_bus_map(sa->sa_bustag, sa->sa_slot, sa->sa_offset,
		    sa->sa_size, BUS_SPACE_MAP_LINEAR, 0, &kvaddr) != 0) {
			printf("%s @ sbus: cannot map registers\n",
			       self->dv_xname);
			return;
		}
		zsaddr = (struct zsdevice *)
		    bus_space_vaddr(sa->sa_bustag, kvaddr);
	}

	zsc->zsc_bustag = sa->sa_bustag;
	zsc->zsc_dmatag = sa->sa_dmatag;
	zsc->zsc_promunit = getpropint(sa->sa_node, "slave", -2);
	zsc->zsc_node = sa->sa_node;

	zs_attach(zsc, zsaddr, sa->sa_pri);
}

static void
zs_attach_fhc(struct device *parent, struct device *self, void *aux)
{
	struct zsc_softc *zsc = (void *) self;
	struct fhc_attach_args *fa = aux;
	struct zsdevice *zsaddr;
	bus_space_handle_t kvaddr;

	if (fa->fa_nreg < 1 && fa->fa_npromvaddrs < 1) {
		printf(": no registers\n");
		return;
	}

	if (fa->fa_nintr < 1) {
		printf(": no interrupts\n");
		return;
	}

	if (fa->fa_npromvaddrs) {
		/*
		 * We're converting from a 32-bit pointer to a 64-bit
		 * pointer.  Since the 32-bit entity is negative, but
		 * the kernel is still mapped into the lower 4GB
		 * range, this needs to be zero-extended.
		 *
		 * XXXXX If we map the kernel and devices into the
		 * high 4GB range, this needs to be changed to
		 * sign-extend the address.
		 */
		zsaddr = (struct zsdevice *)
		    (unsigned long int)fa->fa_promvaddrs[0];
	} else {
		if (fhc_bus_map(fa->fa_bustag, fa->fa_reg[0].fbr_slot,
		    fa->fa_reg[0].fbr_offset, fa->fa_reg[0].fbr_size,
		    BUS_SPACE_MAP_LINEAR, &kvaddr) != 0) {
			printf("%s @ fhc: cannot map registers\n",
			    self->dv_xname);
			return;
		}
		zsaddr = (struct zsdevice *)
		    bus_space_vaddr(fa->fa_bustag, kvaddr);
	}

	zsc->zsc_bustag = fa->fa_bustag;
	zsc->zsc_dmatag = NULL;
	zsc->zsc_promunit = getpropint(fa->fa_node, "slave", -2);
	zsc->zsc_node = fa->fa_node;

	zs_attach(zsc, zsaddr, fa->fa_intr[0]);
}

/*
 * Attach a found zs.
 */
static void
zs_attach(struct zsc_softc *zsc, struct zsdevice *zsd, int pri)
{
	struct zsc_attach_args zsc_args;
	struct zs_chanstate *cs;
	int s, channel, softpri = PIL_TTY;

	printf(" softpri %d\n", softpri);

	/*
	 * Initialize software state for each channel.
	 */
	for (channel = 0; channel < 2; channel++) {
		struct zschan *zc;
		struct device *child;

		zsc_args.type = "serial";
		if (getproplen(zsc->zsc_node, "keyboard") == 0) {
			if (channel == 0)
				zsc_args.type = "keyboard";
			if (channel == 1)
				zsc_args.type = "mouse";
		}

		zsc_args.channel = channel;
		cs = &zsc->zsc_cs_store[channel];
		zsc->zsc_cs[channel] = cs;

		cs->cs_channel = channel;
		cs->cs_private = NULL;
		cs->cs_ops = &zsops_null;
		cs->cs_brg_clk = PCLK / 16;

		zc = (channel == 0) ? &zsd->zs_chan_a : &zsd->zs_chan_b;

		zsc_args.consdev = NULL;
		zsc_args.hwflags = zs_console_flags(zsc->zsc_promunit,
						    zsc->zsc_node,
						    channel);

		if (zsc_args.hwflags & ZS_HWFLAG_CONSOLE) {
			zsc_args.hwflags |= ZS_HWFLAG_USE_CONSDEV;
			zsc_args.consdev = &zs_consdev;
		}

		if (getproplen(zsc->zsc_node, channel == 0 ?
		    "port-a-ignore-cd" : "port-b-ignore-cd") == 0) {
			zsc_args.hwflags |= ZS_HWFLAG_NO_DCD;
		}

		if ((zsc_args.hwflags & ZS_HWFLAG_CONSOLE_INPUT) != 0) {
			zs_conschan_get = zc;
		}
		if ((zsc_args.hwflags & ZS_HWFLAG_CONSOLE_OUTPUT) != 0) {
			zs_conschan_put = zc;
		}
		/* Children need to set cn_dev, etc */

		cs->cs_reg_csr  = &zc->zc_csr;
		cs->cs_reg_data = &zc->zc_data;

		bcopy(zs_init_reg, cs->cs_creg, 16);
		bcopy(zs_init_reg, cs->cs_preg, 16);

		/* XXX: Consult PROM properties for this?! */
		cs->cs_defspeed = zs_get_speed(cs);
		cs->cs_defcflag = zs_def_cflag;

		/* Make these correspond to cs_defcflag (-crtscts) */
		cs->cs_rr0_dcd = ZSRR0_DCD;
		cs->cs_rr0_cts = 0;
		cs->cs_wr5_dtr = ZSWR5_DTR | ZSWR5_RTS;
		cs->cs_wr5_rts = 0;

		/*
		 * Clear the master interrupt enable.
		 * The INTENA is common to both channels,
		 * so just do it on the A channel.
		 */
		if (channel == 0) {
			zs_write_reg(cs, 9, 0);
		}

		/*
		 * Look for a child driver for this channel.
		 * The child attach will setup the hardware.
		 */
		if (!(child = 
		      config_found(&zsc->zsc_dev, (void *)&zsc_args, zs_print))) {
			/* No sub-driver.  Just reset it. */
			u_char reset = (channel == 0) ?
				ZSWR9_A_RESET : ZSWR9_B_RESET;
			s = splzs();
			zs_write_reg(cs,  9, reset);
			splx(s);
		} 
	}

	/*
	 * Now safe to install interrupt handlers.
	 */
	if (bus_intr_establish(zsc->zsc_bustag, pri, IPL_SERIAL, 0, zshard,
	    zsc, zsc->zsc_dev.dv_xname) == NULL)
		panic("zsattach: could not establish interrupt");
	if (!(zsc->zsc_softintr = softintr_establish(softpri, zssoft, zsc)))
		panic("zsattach: could not establish soft interrupt");

	/*
	 * Set the master interrupt enable and interrupt vector.
	 * (common to both channels, do it on A)
	 */
	cs = zsc->zsc_cs[0];
	s = splhigh();
	/* interrupt vector */
	zs_write_reg(cs, 2, zs_init_reg[2]);
	/* master interrupt control (enable) */
	zs_write_reg(cs, 9, zs_init_reg[9]);
	splx(s);

}

static int
zs_print(void *aux, const char *name)
{
	struct zsc_attach_args *args = aux;

	if (name != NULL)
		printf("%s: ", name);

	if (args->channel != -1)
		printf(" channel %d", args->channel);

	return (UNCONF);
}

static int
zshard(void *arg)
{
	struct zsc_softc *zsc = (struct zsc_softc *)arg;
	int rval = 0;

	while (zsc_intr_hard(zsc))
		rval = 1;
	if ((zsc->zsc_cs[0] && zsc->zsc_cs[0]->cs_softreq) ||
	    (zsc->zsc_cs[1] && zsc->zsc_cs[1]->cs_softreq))
		softintr_schedule(zsc->zsc_softintr);
	return (rval);
}

/*
 * We need this only for TTY_DEBUG purposes.
 */
static void
zssoft(void *arg)
{
	struct zsc_softc *zsc = (struct zsc_softc *)arg;
	int s;

	/* Make sure we call the tty layer at spltty. */
	s = spltty();
	(void)zsc_intr_soft(zsc);
#ifdef TTY_DEBUG
	{
		struct zstty_softc *zst0 = zsc->zsc_cs[0]->cs_private;
		struct zstty_softc *zst1 = zsc->zsc_cs[1]->cs_private;
		if (zst0->zst_overflows || zst1->zst_overflows ) {
			struct trapframe *frame = (struct trapframe *)arg;
			
			printf("zs silo overflow from %p\n",
			       (long)frame->tf_pc);
		}
	}
#endif
	splx(s);
}


/*
 * Compute the current baud rate given a ZS channel.
 */
static int
zs_get_speed(struct zs_chanstate *cs)
{
	int tconst;

	tconst = zs_read_reg(cs, 12);
	tconst |= zs_read_reg(cs, 13) << 8;
	return (TCONST_TO_BPS(cs->cs_brg_clk, tconst));
}

/*
 * MD functions for setting the baud rate and control modes.
 */
int
zs_set_speed(struct zs_chanstate *cs, int bps)
{
	int tconst, real_bps;

	if (bps == 0)
		return (0);

#ifdef	DIAGNOSTIC
	if (cs->cs_brg_clk == 0)
		panic("zs_set_speed");
#endif

	tconst = BPS_TO_TCONST(cs->cs_brg_clk, bps);
	if (tconst < 0)
		return (EINVAL);

	/* Convert back to make sure we can do it. */
	real_bps = TCONST_TO_BPS(cs->cs_brg_clk, tconst);

	/* XXX - Allow some tolerance here? */
	if (real_bps != bps)
		return (EINVAL);

	cs->cs_preg[12] = tconst;
	cs->cs_preg[13] = tconst >> 8;

	/* Caller will stuff the pending registers. */
	return (0);
}

int
zs_set_modes(struct zs_chanstate *cs, int cflag)
{
	int s;

	/*
	 * Output hardware flow control on the chip is horrendous:
	 * if carrier detect drops, the receiver is disabled, and if
	 * CTS drops, the transmitter is stopped IN MID CHARACTER!
	 * Therefore, NEVER set the HFC bit, and instead use the
	 * status interrupt to detect CTS changes.
	 */
	s = splzs();
	cs->cs_rr0_pps = 0;
	if ((cflag & (CLOCAL | MDMBUF)) != 0) {
		cs->cs_rr0_dcd = 0;
		if ((cflag & MDMBUF) == 0)
			cs->cs_rr0_pps = ZSRR0_DCD;
	} else
		cs->cs_rr0_dcd = ZSRR0_DCD;
	if ((cflag & CRTSCTS) != 0) {
		cs->cs_wr5_dtr = ZSWR5_DTR;
		cs->cs_wr5_rts = ZSWR5_RTS;
		cs->cs_rr0_cts = ZSRR0_CTS;
#if 0 /* JLW */
	} else if ((cflag & CDTRCTS) != 0) {
		cs->cs_wr5_dtr = 0;
		cs->cs_wr5_rts = ZSWR5_DTR;
		cs->cs_rr0_cts = ZSRR0_CTS;
#endif
	} else if ((cflag & MDMBUF) != 0) {
		cs->cs_wr5_dtr = 0;
		cs->cs_wr5_rts = ZSWR5_DTR;
		cs->cs_rr0_cts = ZSRR0_DCD;
	} else {
		cs->cs_wr5_dtr = ZSWR5_DTR | ZSWR5_RTS;
		cs->cs_wr5_rts = 0;
		cs->cs_rr0_cts = 0;
	}
	splx(s);

	/* Caller will stuff the pending registers. */
	return (0);
}


/*
 * Read or write the chip with suitable delays.
 */

u_char
zs_read_reg(struct zs_chanstate *cs, u_char reg)
{
	u_char val;

	*cs->cs_reg_csr = reg;
	ZS_DELAY();
	val = *cs->cs_reg_csr;
	ZS_DELAY();
	return (val);
}

void
zs_write_reg(struct zs_chanstate *cs, u_char reg, u_char val)
{
	*cs->cs_reg_csr = reg;
	ZS_DELAY();
	*cs->cs_reg_csr = val;
	ZS_DELAY();
}

u_char
zs_read_csr(struct zs_chanstate *cs)
{
	u_char val;

	val = *cs->cs_reg_csr;
	ZS_DELAY();
	return (val);
}

void
zs_write_csr(struct zs_chanstate *cs, u_char val)
{
	*cs->cs_reg_csr = val;
	ZS_DELAY();
}

u_char
zs_read_data(struct zs_chanstate *cs)
{
	u_char val;

	val = *cs->cs_reg_data;
	ZS_DELAY();
	return (val);
}

void
zs_write_data(struct zs_chanstate *cs, u_char val)
{
	*cs->cs_reg_data = val;
	ZS_DELAY();
}

/****************************************************************
 * Console support functions (Sun specific!)
 * Note: this code is allowed to know about the layout of
 * the chip registers, and uses that to keep things simple.
 * XXX - I think I like the mvme167 code better. -gwr
 ****************************************************************/

/*
 * Handle user request to enter kernel debugger.
 */
void
zs_abort(struct zs_chanstate *cs)
{
	volatile struct zschan *zc = zs_conschan_get;
	int rr0;

	/* Wait for end of break to avoid PROM abort. */
	/* XXX - Limit the wait? */
	do {
		rr0 = zc->zc_csr;
		ZS_DELAY();
	} while (rr0 & ZSRR0_BREAK);

#if defined(DDB)
	{
		extern int db_active;
		
		if (!db_active)
			db_enter();
		else
			/* Debugger is probably hozed */
			callrom();
	}
#else
	printf("stopping on keyboard abort\n");
	callrom();
#endif
}


/*
 * Polled input char.
 */
int
zs_getc(void *arg)
{
	volatile struct zschan *zc = arg;
	int s, c, rr0;

	s = splhigh();
	/* Wait for a character to arrive. */
	do {
		rr0 = zc->zc_csr;
		ZS_DELAY();
	} while ((rr0 & ZSRR0_RX_READY) == 0);

	c = zc->zc_data;
	ZS_DELAY();
	splx(s);

	return (c);
}

/*
 * Polled output char.
 */
void
zs_putc(void *arg, int c)
{
	volatile struct zschan *zc = arg;
	int s, rr0;

	s = splhigh();

	/* Wait for transmitter to become ready. */
	do {
		rr0 = zc->zc_csr;
		ZS_DELAY();
	} while ((rr0 & ZSRR0_TX_READY) == 0);

	/*
	 * Send the next character.
	 * Now you'd think that this could be followed by a ZS_DELAY()
	 * just like all the other chip accesses, but it turns out that
	 * the `transmit-ready' interrupt isn't de-asserted until
	 * some period of time after the register write completes
	 * (more than a couple instructions).  So to avoid stray
	 * interrupts we put in the 2us delay regardless of cpu model.
	 */
	zc->zc_data = c;
	delay(2);

	splx(s);
}

/*****************************************************************/




/*
 * Polled console input putchar.
 */
static int
zscngetc(dev_t dev)
{
	return (zs_getc(zs_conschan_get));
}

/*
 * Polled console output putchar.
 */
static void
zscnputc(dev_t dev, int c)
{
	zs_putc(zs_conschan_put, c);
}

int
zs_console_flags(int promunit, int node, int channel)
{
	int cookie, flags = 0;
	u_int options;
	char buf[255];

	/*
	 * We'll just to the OBP grovelling down here since that's
	 * the only type of firmware we support.
	 */
	options = OF_finddevice("/options");

	/* Default to channel 0 if there are no explicit prom args */
	cookie = 0;

	if (node == OF_instance_to_package(OF_stdin())) {
		if (OF_getprop(options, "input-device",
		    buf, sizeof(buf)) != -1) {
			if (strncmp("ttyb", buf, strlen("ttyb")) == 0)
				cookie = 1;
		}

		if (channel == cookie)
			flags |= ZS_HWFLAG_CONSOLE_INPUT;
	}

	if (node == OF_instance_to_package(OF_stdout())) { 
		if (OF_getprop(options, "output-device",
		    buf, sizeof(buf)) != -1) {
			if (strncmp("ttyb", buf, strlen("ttyb")) == 0)
				cookie = 1;
		}
		
		if (channel == cookie)
			flags |= ZS_HWFLAG_CONSOLE_OUTPUT;
	}

	return (flags);
}
