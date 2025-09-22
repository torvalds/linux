/*	$OpenBSD: magma.c,v 1.36 2024/04/24 09:30:30 claudio Exp $	*/

/*-
 * Copyright (c) 1998 Iain Hibbert
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* #define MAGMA_DEBUG */

/*
 * Driver for Magma SBus Serial/Parallel cards using the Cirrus Logic
 * CD1400 & CD1190 chips
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/device.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/malloc.h>
#include <sys/tty.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/syslog.h>
#include <sys/conf.h>
#include <sys/errno.h>

#include <machine/autoconf.h>
#include <machine/conf.h>
#include <machine/bus.h>
#include <machine/bppioctl.h>

#include <dev/sbus/sbusvar.h>
#include <dev/ic/cd1400reg.h>
#include <dev/ic/cd1190reg.h>

#include <dev/sbus/magmareg.h>

/* supported cards
 *
 *  The table below lists the cards that this driver is likely to
 *  be able to support.
 *
 *  Cards with parallel ports: except for the LC2+1Sp, they all use
 *  the CD1190 chip which I know nothing about.  I've tried to leave
 *  hooks for it so it shouldn't be too hard to add support later.
 *  (I think somebody is working on this separately)
 *
 *  Thanks to Bruce at Magma for telling me the hardware offsets.
 */
static const struct magma_board_info supported_cards[] = {
	{
		"MAGMA_Sp", "MAGMA,4_Sp", "Magma 4 Sp", 4, 0,
		1, 0xa000, 0xc000, 0xe000, { 0x8000, 0, 0, 0 },
		0, { 0, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,8_Sp", "Magma 8 Sp", 8, 0,
		2, 0xa000, 0xc000, 0xe000, { 0x4000, 0x6000, 0, 0 },
		0, { 0, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,_8HS_Sp", "Magma Fast 8 Sp", 8, 0,
		2, 0x2000, 0x4000, 0x6000, { 0x8000, 0xa000, 0, 0 },
		0, { 0, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,_8SP_422", "Magma 8 Sp - 422", 8, 0,
		2, 0x2000, 0x4000, 0x6000, { 0x8000, 0xa000, 0, 0 },
		0, { 0, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,12_Sp", "Magma 12 Sp", 12, 0,
		3, 0xa000, 0xc000, 0xe000, { 0x2000, 0x4000, 0x6000, 0 },
		0, { 0, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,16_Sp", "Magma 16 Sp", 16, 0,
		4, 0xd000, 0xe000, 0xf000, { 0x8000, 0x9000, 0xa000, 0xb000 },
		0, { 0, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,16_Sp_2", "Magma 16 Sp", 16, 0,
		4, 0x2000, 0x4000, 0x6000, { 0x8000, 0xa000, 0xc000, 0xe000 },
		0, { 0, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,16HS_Sp", "Magma Fast 16 Sp", 16, 0,
		4, 0x2000, 0x4000, 0x6000, { 0x8000, 0xa000, 0xc000, 0xe000 },
		0, { 0, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,21_Sp", "Magma LC 2+1 Sp", 2, 1,
		1, 0xa000, 0xc000, 0xe000, { 0x8000, 0, 0, 0 },
		0, { 0, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,21HS_Sp", "Magma 2+1 Sp", 2, 1,
		1, 0xa000, 0xc000, 0xe000, { 0x4000, 0, 0, 0 },
		1, { 0x6000, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,41_Sp", "Magma 4+1 Sp", 4, 1,
		1, 0xa000, 0xc000, 0xe000, { 0x4000, 0, 0, 0 },
		1, { 0x6000, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,82_Sp", "Magma 8+2 Sp", 8, 2,
		2, 0xd000, 0xe000, 0xf000, { 0x8000, 0x9000, 0, 0 },
		2, { 0xa000, 0xb000 }
	},
	{
		"MAGMA_Sp", "MAGMA,P1_Sp", "Magma P1 Sp", 0, 1,
		0, 0, 0, 0, { 0, 0, 0, 0 },
		1, { 0x8000, 0 }
	},
	{
		"MAGMA_Sp", "MAGMA,P2_Sp", "Magma P2 Sp", 0, 2,
		0, 0, 0, 0, { 0, 0, 0, 0 },
		2, { 0x4000, 0x8000 }
	},
	{
		"MAGMA 2+1HS Sp", "", "Magma 2+1HS Sp", 2, 0,
		1, 0xa000, 0xc000, 0xe000, { 0x4000, 0, 0, 0 },
		1, { 0x8000, 0 }
	},
	{
		NULL, NULL, NULL, 0, 0,
		0, 0, 0, 0, { 0, 0, 0, 0 },
		0, { 0, 0 }
	}
};

/************************************************************************
 *
 *  Autoconfig Stuff
 */

const struct cfattach magma_ca = {
	sizeof(struct magma_softc), magma_match, magma_attach
};

struct cfdriver magma_cd = {
	NULL, "magma", DV_DULL
};

const struct cfattach mtty_ca = {
	sizeof(struct mtty_softc), mtty_match, mtty_attach
};

struct cfdriver mtty_cd = {
	NULL, "mtty", DV_TTY
};

const struct cfattach mbpp_ca = {
	sizeof(struct mbpp_softc), mbpp_match, mbpp_attach
};

struct cfdriver mbpp_cd = {
	NULL, "mbpp", DV_DULL
};

/************************************************************************
 *
 *  CD1400 Routines
 *
 *	cd1400_compute_baud		calculate COR/BPR register values
 *	cd1400_write_ccr		write a value to CD1400 ccr
 *	cd1400_enable_transmitter	enable transmitting on CD1400 channel
 */

/*
 * compute the bpr/cor pair for any baud rate
 * returns 0 for success, 1 for failure
 */
int
cd1400_compute_baud(speed_t speed, int clock, int *cor, int *bpr)
{
	int c, co, br;

	if (speed < 50 || speed > 150000)
		return (1);

	for (c = 0, co = 8 ; co <= 2048 ; co <<= 2, c++) {
		br = ((clock * 1000000) + (co * speed) / 2) / (co * speed);
		if (br < 0x100) {
			*bpr = br;
			*cor = c;
			return (0);
		}
	}

	return (1);
}

#define	CD1400_READ_REG(cd,reg) \
    bus_space_read_1((cd)->cd_regt, (cd)->cd_regh, (reg))
#define	CD1400_WRITE_REG(cd,reg,value) \
    bus_space_write_1((cd)->cd_regt, (cd)->cd_regh, (reg), (value))

/*
 * Write a CD1400 channel command, should have a timeout?
 */
static inline void
cd1400_write_ccr(struct cd1400 *cd, u_char cmd)
{
	while (CD1400_READ_REG(cd, CD1400_CCR))
		continue;

	CD1400_WRITE_REG(cd, CD1400_CCR, cmd);
}

/*
 * enable transmit service requests for cd1400 channel
 */
void
cd1400_enable_transmitter(struct cd1400 *cd, int channel)
{
	int s, srer;

	s = spltty();
	CD1400_WRITE_REG(cd, CD1400_CAR, channel);
	srer = CD1400_READ_REG(cd, CD1400_SRER);
	SET(srer, CD1400_SRER_TXRDY);
	CD1400_WRITE_REG(cd, CD1400_SRER, srer);
	splx(s);
}

/************************************************************************
 *
 *  CD1190 Routines
 */

/* well, there are none yet */

/************************************************************************
 *
 *  Magma Routines
 *
 * magma_match		reports if we have a magma board available
 * magma_attach		attaches magma boards to the sbus
 * magma_hard		hardware level interrupt routine
 * magma_soft		software level interrupt routine
 */

int
magma_match(struct device *parent, void *vcf, void *aux)
{
	struct sbus_attach_args *sa = aux;
	const struct magma_board_info *card;

	/* See if we support this device */
	for (card = supported_cards; ; card++) {
		if (card->mb_sbusname == NULL)
			/* End of table: no match */
			return (0);
		if (strcmp(sa->sa_name, card->mb_sbusname) == 0)
			break;
	}
	return (1);
}

void
magma_attach(struct device *parent, struct device *dev, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct magma_softc *sc = (struct magma_softc *)dev;
	const struct magma_board_info *card;
	char magma_prom[40], *clockstr;
	int chip, cd_clock;

	getpropstringA(sa->sa_node, "magma_prom", magma_prom);
	for (card = supported_cards; card->mb_name != NULL; card++) {
		if (strcmp(sa->sa_name, card->mb_sbusname) != 0)
			continue;
		if (strcmp(magma_prom, card->mb_name) == 0)
			break;
	}
	if (card->mb_name == NULL) {
		printf(": %s (unsupported)\n", magma_prom);
		return;
	}

	sc->sc_bustag = sa->sa_bustag;

	clockstr = getpropstring(sa->sa_node, "clock");
	if (strlen(clockstr) == 0)
		cd_clock = 25;
	else {
		cd_clock = 0;
		while (*clockstr != '\0')
			cd_clock = cd_clock * 10 + *clockstr++ - '0';
	}

	if (sbus_bus_map(sa->sa_bustag, sa->sa_reg[0].sbr_slot,
	    sa->sa_reg[0].sbr_offset, sa->sa_reg[0].sbr_size,
	    0, 0, &sc->sc_iohandle) != 0) {
		printf(": can't map registers\n");
		return;
	}

	if (sa->sa_nintr < 1) {
		printf(": can't find interrupt\n");
		return;
	}
	sc->sc_ih = bus_intr_establish(sa->sa_bustag, sa->sa_pri, IPL_TTY, 0,
	    magma_hard, sc, dev->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt, pri %d\n",
		    INTLEV(sa->sa_pri));
		bus_space_unmap(sc->sc_bustag, sc->sc_iohandle,
		    sa->sa_reg[0].sbr_size);
		return;
	}

	sc->sc_sih = softintr_establish(IPL_TTY, magma_soft, sc);
	if (sc->sc_sih == NULL) {
		printf(": can't get soft intr\n");
		bus_space_unmap(sc->sc_bustag, sc->sc_iohandle,
		    sa->sa_reg[0].sbr_size);
		return;
	}

	printf(": %s\n", card->mb_realname);

	sc->ms_board = card;
	sc->ms_ncd1400 = card->mb_ncd1400;
	sc->ms_ncd1190 = card->mb_ncd1190;

	/* the SVCACK* lines are daisychained */
	if (bus_space_subregion(sc->sc_bustag, sc->sc_iohandle,
	    card->mb_svcackr, 1, &sc->sc_svcackrh)) {
		printf(": failed to map svcackr\n");
		return;
	}
	if (bus_space_subregion(sc->sc_bustag, sc->sc_iohandle,
	    card->mb_svcackt, 1, &sc->sc_svcackth)) {
		printf(": failed to map svcackt\n");
		return;
	}
	if (bus_space_subregion(sc->sc_bustag, sc->sc_iohandle,
	    card->mb_svcackm, 1, &sc->sc_svcackmh)) {
		printf(": failed to map svcackm\n");
		return;
	}

	/* init the cd1400 chips */
	for (chip = 0 ; chip < card->mb_ncd1400 ; chip++) {
		struct cd1400 *cd = &sc->ms_cd1400[chip];

		cd->cd_clock = cd_clock;

		if (bus_space_subregion(sc->sc_bustag, sc->sc_iohandle,
		    card->mb_cd1400[chip], CD1400_REGMAPSIZE, &cd->cd_regh)) {
			printf(": failed to map cd1400 regs\n");
			return;
		}
		cd->cd_regt = sc->sc_bustag;

		/* getpropstring(sa->sa_node, "chiprev"); */
		/* seemingly the Magma drivers just ignore the propstring */
		cd->cd_chiprev = CD1400_READ_REG(cd, CD1400_GFRCR);

		dprintf(("%s attach CD1400 %d addr 0x%x rev %x clock %dMHz\n",
			    sc->ms_dev.dv_xname, chip, cd->cd_reg,
			    cd->cd_chiprev, cd->cd_clock));

		/* clear GFRCR */
		CD1400_WRITE_REG(cd, CD1400_GFRCR, 0x00);

		/* reset whole chip */
		cd1400_write_ccr(cd,
		    CD1400_CCR_CMDRESET | CD1400_CCR_FULLRESET);

		/* wait for revision code to be restored */
		while (CD1400_READ_REG(cd, CD1400_GFRCR) != cd->cd_chiprev)
		        ;

		/* set the Prescaler Period Register to tick at 1ms */
		CD1400_WRITE_REG(cd, CD1400_PPR,
		    ((cd->cd_clock * 1000000 / CD1400_PPR_PRESCALER + 500)
		    / 1000));

		/*
		 * The LC2+1Sp card is the only card that doesn't have a
		 * CD1190 for the parallel port, but uses channel 0 of the
		 * CD1400, so we make a note of it for later and set up the
		 * CD1400 for parallel mode operation.
		 */
		if (card->mb_npar && card->mb_ncd1190 == 0) {
			CD1400_WRITE_REG(cd, CD1400_GCR, CD1400_GCR_PARALLEL);
			cd->cd_parmode = 1;
		}
	}

	/* init the cd1190 chips */
	for (chip = 0 ; chip < card->mb_ncd1190 ; chip++) {
		struct cd1190 *cd = &sc->ms_cd1190[chip];

		if (bus_space_subregion(sc->sc_bustag, sc->sc_iohandle,
		    card->mb_cd1190[chip], CD1190_REGMAPSIZE, &cd->cd_regh)) {
			printf(": failed to map cd1190 regs\n");
			return;
		}
		cd->cd_regt = sc->sc_bustag;
		dprintf(("%s attach CD1190 %d addr 0x%x (failed)\n",
		    sc->ms_dev.dv_xname, chip, cd->cd_reg));
		/* XXX don't know anything about these chips yet */
	}

	/* configure the children */
	(void)config_found(dev, mtty_match, NULL);
	(void)config_found(dev, mbpp_match, NULL);
}

/*
 * hard interrupt routine
 *
 *  returns 1 if it handled it, otherwise 0
 *
 *  runs at interrupt priority
 */
int
magma_hard(void *arg)
{
	struct magma_softc *sc = arg;
	struct cd1400 *cd;
	int chip, status = 0;
	int serviced = 0;
	int needsoftint = 0;

	/*
	 * check status of all the CD1400 chips
	 */
	for (chip = 0 ; chip < sc->ms_ncd1400 ; chip++)
		status |= CD1400_READ_REG(&sc->ms_cd1400[chip], CD1400_SVRR);

	if (ISSET(status, CD1400_SVRR_RXRDY)) {
		/* enter rx service context */
		u_int8_t rivr = bus_space_read_1(sc->sc_bustag, sc->sc_svcackrh, 0);
		int port = rivr >> 4;

		if (rivr & (1<<3)) {			/* parallel port */
			struct mbpp_port *mbpp;
			int n_chars;

			mbpp = &sc->ms_mbpp->ms_port[port];
			cd = mbpp->mp_cd1400;

			/* don't think we have to handle exceptions */
			n_chars = CD1400_READ_REG(cd, CD1400_RDCR);
			while (n_chars--) {
				if (mbpp->mp_cnt == 0) {
					SET(mbpp->mp_flags, MBPPF_WAKEUP);
					needsoftint = 1;
					break;
				}
				*mbpp->mp_ptr = CD1400_READ_REG(cd, CD1400_RDSR);
				mbpp->mp_ptr++;
				mbpp->mp_cnt--;
			}
		} else {				/* serial port */
			struct mtty_port *mtty;
			u_char *ptr, n_chars, line_stat;

			mtty = &sc->ms_mtty->ms_port[port];
			cd = mtty->mp_cd1400;

			if (ISSET(rivr, CD1400_RIVR_EXCEPTION)) {
				line_stat = CD1400_READ_REG(cd, CD1400_RDSR);
				n_chars = 1;
			} else { /* no exception, received data OK */
				line_stat = 0;
				n_chars = CD1400_READ_REG(cd, CD1400_RDCR);
			}

			ptr = mtty->mp_rput;
			while (n_chars--) {
				*ptr++ = line_stat;
				*ptr++ = CD1400_READ_REG(cd, CD1400_RDSR);
				if (ptr == mtty->mp_rend)
					ptr = mtty->mp_rbuf;
				if (ptr == mtty->mp_rget) {
					if (ptr == mtty->mp_rbuf)
						ptr = mtty->mp_rend;
					ptr -= 2;
					SET(mtty->mp_flags,
					    MTTYF_RING_OVERFLOW);
					break;
				}
			}
			mtty->mp_rput = ptr;

			needsoftint = 1;
		}

		CD1400_WRITE_REG(cd, CD1400_EOSRR, 0);	/* end service context */
		serviced = 1;
	} /* if(rx_service...) */

	if (ISSET(status, CD1400_SVRR_MDMCH)) {
		u_int8_t mivr = bus_space_read_1(sc->sc_bustag, sc->sc_svcackmh, 0);
		int port = mivr >> 4;
		struct mtty_port *mtty;
		int carrier;
		u_char msvr;

		/*
		 * Handle CD (LC2+1Sp = DSR) changes.
		 */
		mtty = &sc->ms_mtty->ms_port[port];
		cd = mtty->mp_cd1400;
		msvr = CD1400_READ_REG(cd, CD1400_MSVR2);
		carrier = ISSET(msvr, cd->cd_parmode ? CD1400_MSVR2_DSR : CD1400_MSVR2_CD);

		if (mtty->mp_carrier != carrier) {
			SET(mtty->mp_flags, MTTYF_CARRIER_CHANGED);
			mtty->mp_carrier = carrier;
			needsoftint = 1;
		}

		CD1400_WRITE_REG(cd, CD1400_EOSRR, 0);	/* end service context */
		serviced = 1;
	} /* if(mdm_service...) */

	if (ISSET(status, CD1400_SVRR_TXRDY)) {
		/* enter tx service context */
		u_int8_t tivr = bus_space_read_1(sc->sc_bustag, sc->sc_svcackth, 0);
		int port = tivr >> 4;

		if (tivr & (1<<3)) {	/* parallel port */
			struct mbpp_port *mbpp;

			mbpp = &sc->ms_mbpp->ms_port[port];
			cd = mbpp->mp_cd1400;

			if (mbpp->mp_cnt) {
				int count = 0;

				/* fill the fifo */
				while (mbpp->mp_cnt && count++ < CD1400_PAR_FIFO_SIZE) {
					CD1400_WRITE_REG(cd, CD1400_TDR, *mbpp->mp_ptr);
					mbpp->mp_ptr++;
					mbpp->mp_cnt--;
				}
			} else {
				/* fifo is empty and we got no more data to send, so shut
				 * off interrupts and signal for a wakeup, which can't be
				 * done here in case we beat mbpp_send to the tsleep call
				 * (we are running at >spltty)
				 */
				CD1400_WRITE_REG(cd, CD1400_SRER, 0);
				SET(mbpp->mp_flags, MBPPF_WAKEUP);
				needsoftint = 1;
			}
		} else {		/* serial port */
			struct mtty_port *mtty;
			struct tty *tp;

			mtty = &sc->ms_mtty->ms_port[port];
			cd = mtty->mp_cd1400;
			tp = mtty->mp_tty;

			if (!ISSET(mtty->mp_flags, MTTYF_STOP)) {
				int count = 0;

				/* check if we should start/stop a break */
				if (ISSET(mtty->mp_flags, MTTYF_SET_BREAK)) {
					CD1400_WRITE_REG(cd, CD1400_TDR, 0);
					CD1400_WRITE_REG(cd, CD1400_TDR, 0x81);
					/* should we delay too? */
					CLR(mtty->mp_flags, MTTYF_SET_BREAK);
					count += 2;
				}

				if (ISSET(mtty->mp_flags, MTTYF_CLR_BREAK)) {
					CD1400_WRITE_REG(cd, CD1400_TDR, 0);
					CD1400_WRITE_REG(cd, CD1400_TDR, 0x83);
					CLR(mtty->mp_flags, MTTYF_CLR_BREAK);
					count += 2;
				}

				/* I don't quite fill the fifo in case the last one is a
				 * NULL which I have to double up because its the escape
				 * code for embedded transmit characters.
				 */
				while (mtty->mp_txc > 0 && count < CD1400_TX_FIFO_SIZE - 1) {
					u_char ch;

					ch = *mtty->mp_txp;

					mtty->mp_txc--;
					mtty->mp_txp++;

					if (ch == 0) {
						CD1400_WRITE_REG(cd, CD1400_TDR, ch);
						count++;
					}

					CD1400_WRITE_REG(cd, CD1400_TDR, ch);
					count++;
				}
			}

			/* if we ran out of work or are requested to STOP then
			 * shut off the txrdy interrupts and signal DONE to flush
			 * out the chars we have sent.
			 */
			if (mtty->mp_txc == 0 || ISSET(mtty->mp_flags, MTTYF_STOP)) {
				int srer;

				srer = CD1400_READ_REG(cd, CD1400_SRER);
				CLR(srer, CD1400_SRER_TXRDY);
				CD1400_WRITE_REG(cd, CD1400_SRER, srer);
				CLR(mtty->mp_flags, MTTYF_STOP);

				SET(mtty->mp_flags, MTTYF_DONE);
				needsoftint = 1;
			}
		}

		CD1400_WRITE_REG(cd, CD1400_EOSRR, 0);	/* end service context */
		serviced = 1;
	} /* if(tx_service...) */

	/* XXX service CD1190 interrupts too
	for (chip = 0 ; chip < sc->ms_ncd1190 ; chip++) {
	}
	*/

	if (needsoftint)
		softintr_schedule(sc->sc_sih);

	return (serviced);
}

/*
 * magma soft interrupt handler
 *
 *  returns 1 if it handled it, 0 otherwise
 *
 *  runs at spltty()
 */
void
magma_soft(void *arg)
{
	struct magma_softc *sc = arg;
	struct mtty_softc *mtty = sc->ms_mtty;
	struct mbpp_softc *mbpp = sc->ms_mbpp;
	int port;
	int serviced = 0;
	int s, flags;

	/*
	 * check the tty ports (if any) to see what needs doing
	 */
	if (mtty) {
		for (port = 0 ; port < mtty->ms_nports ; port++) {
			struct mtty_port *mp = &mtty->ms_port[port];
			struct tty *tp = mp->mp_tty;

			if (!ISSET(tp->t_state, TS_ISOPEN))
				continue;

			/*
			 * handle any received data
			 */
			while (mp->mp_rget != mp->mp_rput) {
				u_char stat;
				int data;

				stat = mp->mp_rget[0];
				data = mp->mp_rget[1];
				mp->mp_rget = ((mp->mp_rget + 2) == mp->mp_rend) ? mp->mp_rbuf : (mp->mp_rget + 2);

				if (stat & (CD1400_RDSR_BREAK | CD1400_RDSR_FE))
					data |= TTY_FE;
				if (stat & CD1400_RDSR_PE)
					data |= TTY_PE;

				if (stat & CD1400_RDSR_OE)
					log(LOG_WARNING, "%s%x: fifo overflow\n", mtty->ms_dev.dv_xname, port);

				(*linesw[tp->t_line].l_rint)(data, tp);
				serviced = 1;
			}

			s = splhigh();	/* block out hard interrupt routine */
			flags = mp->mp_flags;
			CLR(mp->mp_flags, MTTYF_DONE | MTTYF_CARRIER_CHANGED | MTTYF_RING_OVERFLOW);
			splx(s);	/* ok */

			if (ISSET(flags, MTTYF_CARRIER_CHANGED)) {
				dprintf(("%s%x: cd %s\n", mtty->ms_dev.dv_xname, port, mp->mp_carrier ? "on" : "off"));
				(*linesw[tp->t_line].l_modem)(tp, mp->mp_carrier);
				serviced = 1;
			}

			if (ISSET(flags, MTTYF_RING_OVERFLOW)) {
				log(LOG_WARNING, "%s%x: ring buffer overflow\n", mtty->ms_dev.dv_xname, port);
				serviced = 1;
			}

			if (ISSET(flags, MTTYF_DONE)) {
				ndflush(&tp->t_outq, mp->mp_txp - tp->t_outq.c_cf);
				CLR(tp->t_state, TS_BUSY);
				(*linesw[tp->t_line].l_start)(tp);	/* might be some more */
				serviced = 1;
			}
		} /* for (each mtty...) */
	}

	/*
	 * check the bpp ports (if any) to see what needs doing
	 */
	if (mbpp) {
		for (port = 0 ; port < mbpp->ms_nports ; port++) {
			struct mbpp_port *mp = &mbpp->ms_port[port];

			if (!ISSET(mp->mp_flags, MBPPF_OPEN))
				continue;

			s = splhigh();	/* block out hard intr routine */
			flags = mp->mp_flags;
			CLR(mp->mp_flags, MBPPF_WAKEUP);
			splx(s);

			if (ISSET(flags, MBPPF_WAKEUP)) {
				wakeup(mp);
				serviced = 1;
			}
		} /* for (each mbpp...) */
	}
}

/************************************************************************
 *
 *  MTTY Routines
 *
 *	mtty_match		match one mtty device
 *	mtty_attach		attach mtty devices
 *	mttyopen		open mtty device
 *	mttyclose		close mtty device
 *	mttyread		read from mtty
 *	mttywrite		write to mtty
 *	mttyioctl		do ioctl on mtty
 *	mttytty			return tty pointer for mtty
 *	mttystop		stop mtty device
 *	mtty_start		start mtty device
 *	mtty_param		set mtty parameters
 *	mtty_modem_control	set modem control lines
 */

int
mtty_match(struct device *parent, void *vcf, void *args)
{
	struct magma_softc *sc = (struct magma_softc *)parent;

	return (args == mtty_match && sc->ms_board->mb_nser &&
	    sc->ms_mtty == NULL);
}

void
mtty_attach(struct device *parent, struct device *dev, void *args)
{
	struct magma_softc *sc = (struct magma_softc *)parent;
	struct mtty_softc *ms = (struct mtty_softc *)dev;
	int port, chip, chan;

	sc->ms_mtty = ms;
	dprintf((" addr 0x%x", ms));

	for (port = 0, chip = 0, chan = 0;
	     port < sc->ms_board->mb_nser; port++) {
		struct mtty_port *mp = &ms->ms_port[port];
		struct tty *tp;

		mp->mp_cd1400 = &sc->ms_cd1400[chip];
		if (mp->mp_cd1400->cd_parmode && chan == 0) {
			/* skip channel 0 if parmode */
			chan = 1;
		}
		mp->mp_channel = chan;

		tp = ttymalloc(0);
		tp->t_oproc = mtty_start;
		tp->t_param = mtty_param;

		mp->mp_tty = tp;

		mp->mp_rbuf = malloc(MTTY_RBUF_SIZE, M_DEVBUF, M_NOWAIT);
		if (mp->mp_rbuf == NULL)
			break;

		mp->mp_rend = mp->mp_rbuf + MTTY_RBUF_SIZE;

		chan = (chan + 1) % CD1400_NO_OF_CHANNELS;
		if (chan == 0)
			chip++;
	}

	ms->ms_nports = port;
	printf(": %d tty%s\n", port, port == 1 ? "" : "s");
}

/*
 * open routine. returns zero if successful, else error code
 */
int
mttyopen(dev_t dev, int flags, int mode, struct proc *p)
{
	int card = MAGMA_CARD(dev);
	int port = MAGMA_PORT(dev);
	struct mtty_softc *ms;
	struct mtty_port *mp;
	struct tty *tp;
	struct cd1400 *cd;
	int s;

	if (card >= mtty_cd.cd_ndevs || (ms = mtty_cd.cd_devs[card]) == NULL
	    || port >= ms->ms_nports)
		return (ENXIO);	/* device not configured */

	mp = &ms->ms_port[port];
	tp = mp->mp_tty;
	tp->t_dev = dev;

	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		SET(tp->t_state, TS_WOPEN);

		/* set defaults */
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		if (ISSET(mp->mp_openflags, TIOCFLAG_CLOCAL))
			SET(tp->t_cflag, CLOCAL);
		if (ISSET(mp->mp_openflags, TIOCFLAG_CRTSCTS))
			SET(tp->t_cflag, CRTSCTS);
		if (ISSET(mp->mp_openflags, TIOCFLAG_MDMBUF))
			SET(tp->t_cflag, MDMBUF);
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed = TTYDEF_SPEED;

		/* init ring buffer */
		mp->mp_rput = mp->mp_rget = mp->mp_rbuf;

		s = spltty();

		/* reset CD1400 channel */
		cd = mp->mp_cd1400;
		CD1400_WRITE_REG(cd, CD1400_CAR, mp->mp_channel);
		cd1400_write_ccr(cd, CD1400_CCR_CMDRESET);

		/* encode the port number in top half of LIVR */
		CD1400_WRITE_REG(cd, CD1400_LIVR, port << 4);

		/* sets parameters and raises DTR */
		(void)mtty_param(tp, &tp->t_termios);

		/* set tty watermarks */
		ttsetwater(tp);

		/* enable service requests */
		CD1400_WRITE_REG(cd, CD1400_SRER, CD1400_SRER_RXDATA | CD1400_SRER_MDMCH);

		/* tell the tty about the carrier status */
		if (ISSET(mp->mp_openflags, TIOCFLAG_SOFTCAR) || mp->mp_carrier)
			SET(tp->t_state, TS_CARR_ON);
		else
			CLR(tp->t_state, TS_CARR_ON);
	} else if (ISSET(tp->t_state, TS_XCLUDE) && suser(p) != 0) {
		return (EBUSY);	/* superuser can break exclusive access */
	} else {
		s = spltty();
	}

	/* wait for carrier if necessary */
	if (!ISSET(flags, O_NONBLOCK)) {
		while (!ISSET(tp->t_cflag, CLOCAL) && !ISSET(tp->t_state, TS_CARR_ON)) {
			int error;

			SET(tp->t_state, TS_WOPEN);
			error = ttysleep(tp, &tp->t_rawq, TTIPRI | PCATCH,
			    ttopen);
			if (error != 0) {
				splx(s);
				CLR(tp->t_state, TS_WOPEN);
				return (error);
			}
		}
	}

	splx(s);

	return ((*linesw[tp->t_line].l_open)(dev, tp, p));
}

/*
 * close routine. returns zero if successful, else error code
 */
int
mttyclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct mtty_softc *ms = mtty_cd.cd_devs[MAGMA_CARD(dev)];
	struct mtty_port *mp = &ms->ms_port[MAGMA_PORT(dev)];
	struct tty *tp = mp->mp_tty;
	int s;

	(*linesw[tp->t_line].l_close)(tp, flag, p);
	s = spltty();

	/* if HUPCL is set, and the tty is no longer open
	 * shut down the port
	 */
	if (ISSET(tp->t_cflag, HUPCL) || !ISSET(tp->t_state, TS_ISOPEN)) {
	/* XXX wait until FIFO is empty before turning off the channel
		struct cd1400 *cd = mp->mp_cd1400;
	*/

		/* drop DTR and RTS */
		(void)mtty_modem_control(mp, 0, DMSET);

		/* turn off the channel
		CD1400_WRITE_REG(cd, CD1400_CAR, mp->mp_channel);
		cd1400_write_ccr(cd, CD1400_CCR_CMDRESET);
		*/
	}

	splx(s);
	ttyclose(tp);

	return (0);
}

/*
 * Read routine
 */
int
mttyread(dev_t dev, struct uio *uio, int flags)
{
	struct mtty_softc *ms = mtty_cd.cd_devs[MAGMA_CARD(dev)];
	struct mtty_port *mp = &ms->ms_port[MAGMA_PORT(dev)];
	struct tty *tp = mp->mp_tty;

	return ((*linesw[tp->t_line].l_read)(tp, uio, flags));
}

/*
 * Write routine
 */
int
mttywrite(dev_t dev, struct uio *uio, int flags)
{
	struct mtty_softc *ms = mtty_cd.cd_devs[MAGMA_CARD(dev)];
	struct mtty_port *mp = &ms->ms_port[MAGMA_PORT(dev)];
	struct tty *tp = mp->mp_tty;

	return ((*linesw[tp->t_line].l_write)(tp, uio, flags));
}

/*
 * return tty pointer
 */
struct tty *
mttytty(dev_t dev)
{
	struct mtty_softc *ms = mtty_cd.cd_devs[MAGMA_CARD(dev)];
	struct mtty_port *mp = &ms->ms_port[MAGMA_PORT(dev)];

	return (mp->mp_tty);
}

/*
 * ioctl routine
 */
int
mttyioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct mtty_softc *ms = mtty_cd.cd_devs[MAGMA_CARD(dev)];
	struct mtty_port *mp = &ms->ms_port[MAGMA_PORT(dev)];
	struct tty *tp = mp->mp_tty;
	int error;

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flags, p);
	if (error >= 0)
		return (error);

	error = ttioctl(tp, cmd, data, flags, p);
	if (error >= 0)
		return (error);

	error = 0;

	switch(cmd) {
	case TIOCSBRK:	/* set break */
		SET(mp->mp_flags, MTTYF_SET_BREAK);
		cd1400_enable_transmitter(mp->mp_cd1400, mp->mp_channel);
		break;

	case TIOCCBRK:	/* clear break */
		SET(mp->mp_flags, MTTYF_CLR_BREAK);
		cd1400_enable_transmitter(mp->mp_cd1400, mp->mp_channel);
		break;

	case TIOCSDTR:	/* set DTR */
		mtty_modem_control(mp, TIOCM_DTR, DMBIS);
		break;

	case TIOCCDTR:	/* clear DTR */
		mtty_modem_control(mp, TIOCM_DTR, DMBIC);
		break;

	case TIOCMSET:	/* set modem lines */
		mtty_modem_control(mp, *((int *)data), DMSET);
		break;

	case TIOCMBIS:	/* bit set modem lines */
		mtty_modem_control(mp, *((int *)data), DMBIS);
		break;

	case TIOCMBIC:	/* bit clear modem lines */
		mtty_modem_control(mp, *((int *)data), DMBIC);
		break;

	case TIOCMGET:	/* get modem lines */
		*((int *)data) = mtty_modem_control(mp, 0, DMGET);
		break;

	case TIOCGFLAGS:
		*((int *)data) = mp->mp_openflags;
		break;

	case TIOCSFLAGS:
		if (suser(p))
			error = EPERM;
		else
			mp->mp_openflags = *((int *)data) &
				(TIOCFLAG_SOFTCAR | TIOCFLAG_CLOCAL |
				TIOCFLAG_CRTSCTS | TIOCFLAG_MDMBUF);
		break;

	default:
		error = ENOTTY;
	}

	return (error);
}

/*
 * Stop output, e.g., for ^S or output flush.
 */
int
mttystop(struct tty *tp, int flags)
{
	struct mtty_softc *ms = mtty_cd.cd_devs[MAGMA_CARD(tp->t_dev)];
	struct mtty_port *mp = &ms->ms_port[MAGMA_PORT(tp->t_dev)];
	int s;

	s = spltty();

	if (ISSET(tp->t_state, TS_BUSY)) {
		if (!ISSET(tp->t_state, TS_TTSTOP))
			SET(tp->t_state, TS_FLUSH);

		/*
		 * the transmit interrupt routine will disable transmit when it
		 * notices that MTTYF_STOP has been set.
		 */
		SET(mp->mp_flags, MTTYF_STOP);
	}

	splx(s);
	return (0);
}

/*
 * Start output, after a stop.
 */
void
mtty_start(struct tty *tp)
{
	struct mtty_softc *ms = mtty_cd.cd_devs[MAGMA_CARD(tp->t_dev)];
	struct mtty_port *mp = &ms->ms_port[MAGMA_PORT(tp->t_dev)];
	int s;

	s = spltty();

	/* we only need to do something if we are not already busy
	 * or delaying or stopped
	 */
	if (!ISSET(tp->t_state, TS_TTSTOP | TS_TIMEOUT | TS_BUSY)) {

		/* if we are sleeping and output has drained below
		 * low water mark, awaken
		 */
		ttwakeupwr(tp);

		/* if something to send, start transmitting
		 */
		if (tp->t_outq.c_cc) {
			mp->mp_txc = ndqb(&tp->t_outq, 0);
			mp->mp_txp = tp->t_outq.c_cf;
			SET(tp->t_state, TS_BUSY);
			cd1400_enable_transmitter(mp->mp_cd1400, mp->mp_channel);
		}
	}

	splx(s);
}

/*
 * set/get modem line status
 *
 * bits can be: TIOCM_DTR, TIOCM_RTS, TIOCM_CTS, TIOCM_CD, TIOCM_RI, TIOCM_DSR
 *
 * note that DTR and RTS lines are exchanged, and that DSR is
 * not available on the LC2+1Sp card (used as CD)
 *
 * only let them fiddle with RTS if CRTSCTS is not enabled
 */
int
mtty_modem_control(struct mtty_port *mp, int bits, int howto)
{
	struct cd1400 *cd = mp->mp_cd1400;
	struct tty *tp = mp->mp_tty;
	int s, msvr;

	s = spltty();

	CD1400_WRITE_REG(cd, CD1400_CAR, mp->mp_channel);

	switch(howto) {
	case DMGET:	/* get bits */
		bits = 0;

		bits |= TIOCM_LE;

		msvr = CD1400_READ_REG(cd, CD1400_MSVR1);
		if (msvr & CD1400_MSVR1_RTS)
			bits |= TIOCM_DTR;

		msvr = CD1400_READ_REG(cd, CD1400_MSVR2);
		if (msvr & CD1400_MSVR2_DTR)
			bits |= TIOCM_RTS;
		if (msvr & CD1400_MSVR2_CTS)
			bits |= TIOCM_CTS;
		if (msvr & CD1400_MSVR2_RI)
			bits |= TIOCM_RI;
		if (msvr & CD1400_MSVR2_DSR)
			bits |= (cd->cd_parmode ? TIOCM_CD : TIOCM_DSR);
		if (msvr & CD1400_MSVR2_CD)
			bits |= (cd->cd_parmode ? 0 : TIOCM_CD);

		break;

	case DMSET:	/* reset bits */
		if (!ISSET(tp->t_cflag, CRTSCTS))
			CD1400_WRITE_REG(cd, CD1400_MSVR2,
			    ((bits & TIOCM_RTS) ? CD1400_MSVR2_DTR : 0));

		CD1400_WRITE_REG(cd, CD1400_MSVR1,
		    ((bits & TIOCM_DTR) ? CD1400_MSVR1_RTS : 0));

		break;

	case DMBIS:	/* set bits */
		if ((bits & TIOCM_RTS) && !ISSET(tp->t_cflag, CRTSCTS))
			CD1400_WRITE_REG(cd, CD1400_MSVR2, CD1400_MSVR2_DTR);

		if (bits & TIOCM_DTR)
			CD1400_WRITE_REG(cd, CD1400_MSVR1, CD1400_MSVR1_RTS);

		break;

	case DMBIC:	/* clear bits */
		if ((bits & TIOCM_RTS) && !ISSET(tp->t_cflag, CRTSCTS))
			CD1400_WRITE_REG(cd, CD1400_MSVR2, 0);

		if (bits & TIOCM_DTR)
			CD1400_WRITE_REG(cd, CD1400_MSVR1, 0);

		break;
	}

	splx(s);
	return (bits);
}

/*
 * Set tty parameters, returns error or 0 on success
 */
int
mtty_param(struct tty *tp, struct termios *t)
{
	struct mtty_softc *ms = mtty_cd.cd_devs[MAGMA_CARD(tp->t_dev)];
	struct mtty_port *mp = &ms->ms_port[MAGMA_PORT(tp->t_dev)];
	struct cd1400 *cd = mp->mp_cd1400;
	int rbpr, tbpr, rcor, tcor;
	u_char mcor1 = 0, mcor2 = 0;
	int s, opt;

	if (t->c_ospeed &&
	    cd1400_compute_baud(t->c_ospeed, cd->cd_clock, &tcor, &tbpr))
		return (EINVAL);

	if (t->c_ispeed &&
	    cd1400_compute_baud(t->c_ispeed, cd->cd_clock, &rcor, &rbpr))
		return (EINVAL);

	s = spltty();

	/* hang up the line if ospeed is zero, else raise DTR */
	(void)mtty_modem_control(mp, TIOCM_DTR,
	    (t->c_ospeed == 0 ? DMBIC : DMBIS));

	/* select channel, done in mtty_modem_control() */
	/* CD1400_WRITE_REG(cd, CD1400_CAR, mp->mp_channel); */

	/* set transmit speed */
	if (t->c_ospeed) {
		CD1400_WRITE_REG(cd, CD1400_TCOR, tcor);
		CD1400_WRITE_REG(cd, CD1400_TBPR, tbpr);
	}

	/* set receive speed */
	if (t->c_ispeed) {
		CD1400_WRITE_REG(cd, CD1400_RCOR, rcor);
		CD1400_WRITE_REG(cd, CD1400_RBPR, rbpr);
	}

	/* enable transmitting and receiving on this channel */
	opt = CD1400_CCR_CMDCHANCTL | CD1400_CCR_XMTEN | CD1400_CCR_RCVEN;
	cd1400_write_ccr(cd, opt);

	/* set parity, data and stop bits */
	opt = 0;
	if (ISSET(t->c_cflag, PARENB))
		opt |= (ISSET(t->c_cflag, PARODD) ? CD1400_COR1_PARODD : CD1400_COR1_PARNORMAL);

	if (!ISSET(t->c_iflag, INPCK))
		opt |= CD1400_COR1_NOINPCK; /* no parity checking */

	if (ISSET(t->c_cflag, CSTOPB))
		opt |= CD1400_COR1_STOP2;

	switch( t->c_cflag & CSIZE) {
	case CS5:
		opt |= CD1400_COR1_CS5;
		break;

	case CS6:
		opt |= CD1400_COR1_CS6;
		break;

	case CS7:
		opt |= CD1400_COR1_CS7;
		break;

	default:
		opt |= CD1400_COR1_CS8;
		break;
	}

	CD1400_WRITE_REG(cd, CD1400_COR1, opt);

	/*
	 * enable Embedded Transmit Commands (for breaks)
	 * use the CD1400 automatic CTS flow control if CRTSCTS is set
	 */
	opt = CD1400_COR2_ETC;
	if (ISSET(t->c_cflag, CRTSCTS))
		opt |= CD1400_COR2_CCTS_OFLOW;
	CD1400_WRITE_REG(cd, CD1400_COR2, opt);

	CD1400_WRITE_REG(cd, CD1400_COR3, MTTY_RX_FIFO_THRESHOLD);

	cd1400_write_ccr(cd, CD1400_CCR_CMDCORCHG | CD1400_CCR_COR1 | CD1400_CCR_COR2 | CD1400_CCR_COR3);

	CD1400_WRITE_REG(cd, CD1400_COR4, CD1400_COR4_PFO_EXCEPTION);
	CD1400_WRITE_REG(cd, CD1400_COR5, 0);

	/*
	 * if automatic RTS handshaking enabled, set DTR threshold
	 * (RTS and DTR lines are switched, CD1400 thinks its DTR)
	 */
	if (ISSET(t->c_cflag, CRTSCTS))
		mcor1 = MTTY_RX_DTR_THRESHOLD;

	/* set up `carrier detect' interrupts */
	if (cd->cd_parmode) {
		SET(mcor1, CD1400_MCOR1_DSRzd);
		SET(mcor2, CD1400_MCOR2_DSRod);
	} else {
		SET(mcor1, CD1400_MCOR1_CDzd);
		SET(mcor2, CD1400_MCOR2_CDod);
	}

	CD1400_WRITE_REG(cd, CD1400_MCOR1, mcor1);
	CD1400_WRITE_REG(cd, CD1400_MCOR2, mcor2);

	/* receive timeout 2ms */
	CD1400_WRITE_REG(cd, CD1400_RTPR, 2);

	splx(s);
	return (0);
}

/************************************************************************
 *
 *  MBPP Routines
 *
 *	mbpp_match	match one mbpp device
 *	mbpp_attach	attach mbpp devices
 *	mbppopen	open mbpp device
 *	mbppclose	close mbpp device
 *	mbppread	read from mbpp
 *	mbppwrite	write to mbpp
 *	mbppioctl	do ioctl on mbpp
 *	mbppkqfilter	kqueue on mbpp
 *	mbpp_rw		general rw routine
 *	mbpp_timeout	rw timeout
 *	mbpp_start	rw start after delay
 *	mbpp_send	send data
 *	mbpp_recv	recv data
 */

int
mbpp_match(struct device *parent, void *vcf, void *args)
{
	struct magma_softc *sc = (struct magma_softc *)parent;

	return (args == mbpp_match && sc->ms_board->mb_npar &&
	    sc->ms_mbpp == NULL);
}

void
mbpp_attach(struct device *parent, struct device *dev, void *args)
{
	struct magma_softc *sc = (struct magma_softc *)parent;
	struct mbpp_softc *ms = (struct mbpp_softc *)dev;
	struct mbpp_port *mp;
	int port;

	sc->ms_mbpp = ms;
	dprintf((" addr 0x%x", ms));

	for (port = 0 ; port < sc->ms_board->mb_npar ; port++) {
		mp = &ms->ms_port[port];

		if (sc->ms_ncd1190)
			mp->mp_cd1190 = &sc->ms_cd1190[port];
		else
			mp->mp_cd1400 = &sc->ms_cd1400[0];

		timeout_set(&mp->mp_timeout_tmo, mbpp_timeout, mp);
		timeout_set(&mp->mp_start_tmo, mbpp_start, mp);
	}

	ms->ms_nports = port;
	printf(": %d port%s\n", port, port == 1 ? "" : "s");
}

/*
 * open routine. returns zero if successful, else error code
 */
int
mbppopen(dev_t dev, int flags, int mode, struct proc *p)
{
	int card = MAGMA_CARD(dev);
	int port = MAGMA_PORT(dev);
	struct mbpp_softc *ms;
	struct mbpp_port *mp;
	int s;

	if (card >= mbpp_cd.cd_ndevs || (ms = mbpp_cd.cd_devs[card]) == NULL || port >= ms->ms_nports)
		return (ENXIO);

	mp = &ms->ms_port[port];

	s = spltty();
	if (ISSET(mp->mp_flags, MBPPF_OPEN)) {
		splx(s);
		return (EBUSY);
	}
	SET(mp->mp_flags, MBPPF_OPEN);
	splx(s);

	/* set defaults */
	mp->mp_burst = BPP_BURST;
	mp->mp_timeout = BPP_TIMEOUT;
	mp->mp_delay = BPP_DELAY;

	/* init chips */
	if (mp->mp_cd1400) {	/* CD1400 */
		struct cd1400 *cd = mp->mp_cd1400;

		/* set up CD1400 channel */
		s = spltty();
		CD1400_WRITE_REG(cd, CD1400_CAR, 0);
		cd1400_write_ccr(cd, CD1400_CCR_CMDRESET);
		CD1400_WRITE_REG(cd, CD1400_LIVR, (1<<3));
		splx(s);
	} else {		/* CD1190 */
		mp->mp_flags = 0;
		return (ENXIO);
	}

	return (0);
}

/*
 * close routine. returns zero if successful, else error code
 */
int
mbppclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct mbpp_softc *ms = mbpp_cd.cd_devs[MAGMA_CARD(dev)];
	struct mbpp_port *mp = &ms->ms_port[MAGMA_PORT(dev)];

	mp->mp_flags = 0;
	return (0);
}

/*
 * Read routine
 */
int
mbppread(dev_t dev, struct uio *uio, int flags)
{
	return (mbpp_rw(dev, uio));
}

/*
 * Write routine
 */
int
mbppwrite(dev_t dev, struct uio *uio, int flags)
{
	return (mbpp_rw(dev, uio));
}

/*
 * ioctl routine
 */
int
mbppioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	struct mbpp_softc *ms = mbpp_cd.cd_devs[MAGMA_CARD(dev)];
	struct mbpp_port *mp = &ms->ms_port[MAGMA_PORT(dev)];
	struct bpp_param *bp;
	int error = 0;
	int s;

	switch(cmd) {
	case BPPIOCSPARAM:
		bp = (struct bpp_param *)data;
		if (bp->bp_burst < BPP_BURST_MIN || bp->bp_burst > BPP_BURST_MAX ||
		    bp->bp_delay < BPP_DELAY_MIN || bp->bp_delay > BPP_DELAY_MIN) {
			error = EINVAL;
		} else {
			mp->mp_burst = bp->bp_burst;
			mp->mp_timeout = bp->bp_timeout;
			mp->mp_delay = bp->bp_delay;
		}
		break;
	case BPPIOCGPARAM:
		bp = (struct bpp_param *)data;
		bp->bp_burst = mp->mp_burst;
		bp->bp_timeout = mp->mp_timeout;
		bp->bp_delay = mp->mp_delay;
		break;
	case BPPIOCGSTAT:
		/* XXX make this more generic */
		s = spltty();
		CD1400_WRITE_REG(mp->mp_cd1400, CD1400_CAR, 0);
		*(int *)data = CD1400_READ_REG(mp->mp_cd1400, CD1400_PSVR);
		splx(s);
		break;
	default:
		error = ENOTTY;
	}

	return (error);
}

int
mbppkqfilter(dev_t dev, struct knote *kn)
{
	return (seltrue_kqfilter(dev, kn));
}

int
mbpp_rw(dev_t dev, struct uio *uio)
{
	int card = MAGMA_CARD(dev);
	int port = MAGMA_PORT(dev);
	struct mbpp_softc *ms = mbpp_cd.cd_devs[card];
	struct mbpp_port *mp = &ms->ms_port[port];
	caddr_t buffer, ptr;
	size_t buflen, cnt, len;
	int s, error = 0;
	int gotdata = 0;

	if (uio->uio_resid == 0)
		return (0);

	buflen = ulmin(uio->uio_resid, mp->mp_burst);
	buffer = malloc(buflen, M_DEVBUF, M_WAITOK);

	SET(mp->mp_flags, MBPPF_UIO);

	/*
	 * start timeout, if needed
	 */
	if (mp->mp_timeout > 0) {
		SET(mp->mp_flags, MBPPF_TIMEOUT);
		timeout_add_msec(&mp->mp_timeout_tmo, mp->mp_timeout);
	}

	len = cnt = 0;
	while (uio->uio_resid > 0) {
		len = ulmin(buflen, uio->uio_resid);
		ptr = buffer;

		if (uio->uio_rw == UIO_WRITE) {
			error = uiomove(ptr, len, uio);
			if (error)
				break;
		}
	again:		/* goto bad */
		/* timed out?  */
		if (!ISSET(mp->mp_flags, MBPPF_UIO))
			break;

		/*
		 * perform the operation
		 */
		if (uio->uio_rw == UIO_WRITE) {
			cnt = mbpp_send(mp, ptr, len);
		} else {
			cnt = mbpp_recv(mp, ptr, len);
		}

		if (uio->uio_rw == UIO_READ) {
			if (cnt) {
				error = uiomove(ptr, cnt, uio);
				if (error)
					break;
				gotdata++;
			}
			else if (gotdata)	/* consider us done */
				break;
		}

		/* timed out?  */
		if (!ISSET(mp->mp_flags, MBPPF_UIO))
			break;

		/*
		 * poll delay?
		 */
		if (mp->mp_delay > 0) {
			s = spltty();	/* XXX */
			SET(mp->mp_flags, MBPPF_DELAY);
			timeout_add_msec(&mp->mp_start_tmo, mp->mp_delay);
			error = tsleep_nsec(mp, PCATCH | PZERO, "mbppdelay",
			    INFSLP);
			splx(s);
			if (error)
				break;
		}

		/*
		 * don't call uiomove again until we used all the data we grabbed
		 */
		if (uio->uio_rw == UIO_WRITE && cnt != len) {
			ptr += cnt;
			len -= cnt;
			cnt = 0;
			goto again;
		}
	}

	/*
	 * clear timeouts
	 */
	s = spltty();	/* XXX */
	if (ISSET(mp->mp_flags, MBPPF_TIMEOUT)) {
		timeout_del(&mp->mp_timeout_tmo);
		CLR(mp->mp_flags, MBPPF_TIMEOUT);
	}
	if (ISSET(mp->mp_flags, MBPPF_DELAY)) {
		timeout_del(&mp->mp_start_tmo);
		CLR(mp->mp_flags, MBPPF_DELAY);
	}
	splx(s);

	/*
	 * adjust for those chars that we uiomoved but never actually wrote
	 */
	if (uio->uio_rw == UIO_WRITE && cnt != len) {
		uio->uio_resid += (len - cnt);
	}

	free(buffer, M_DEVBUF, 0);
	return (error);
}

void
mbpp_timeout(void *arg)
{
	struct mbpp_port *mp = arg;

	CLR(mp->mp_flags, MBPPF_UIO | MBPPF_TIMEOUT);
	wakeup(mp);
}

void
mbpp_start(void *arg)
{
	struct mbpp_port *mp = arg;

	CLR(mp->mp_flags, MBPPF_DELAY);
	wakeup(mp);
}

int
mbpp_send(struct mbpp_port *mp, caddr_t ptr, int len)
{
	int s;
	struct cd1400 *cd = mp->mp_cd1400;

	/* set up io information */
	mp->mp_ptr = ptr;
	mp->mp_cnt = len;

	/* start transmitting */
	s = spltty();
	if (cd) {
		CD1400_WRITE_REG(cd, CD1400_CAR, 0);

		/* output strobe width ~1microsecond */
		CD1400_WRITE_REG(cd, CD1400_TBPR, 10);

		/* enable channel */
		cd1400_write_ccr(cd, CD1400_CCR_CMDCHANCTL | CD1400_CCR_XMTEN);
		CD1400_WRITE_REG(cd, CD1400_SRER, CD1400_SRER_TXRDY);
	}

	/* ZZzzz... */
	tsleep_nsec(mp, PCATCH | PZERO, "mbpp_send", INFSLP);

	/* stop transmitting */
	if (cd) {
		CD1400_WRITE_REG(cd, CD1400_CAR, 0);

		/* disable transmitter */
		CD1400_WRITE_REG(cd, CD1400_SRER, 0);
		cd1400_write_ccr(cd, CD1400_CCR_CMDCHANCTL | CD1400_CCR_XMTDIS);

		/* flush fifo */
		cd1400_write_ccr(cd, CD1400_CCR_CMDRESET | CD1400_CCR_FTF);
	}
	splx(s);

	/* return number of chars sent */
	return (len - mp->mp_cnt);
}

int
mbpp_recv(struct mbpp_port *mp, caddr_t ptr, int len)
{
	int s;
	struct cd1400 *cd = mp->mp_cd1400;

	/* set up io information */
	mp->mp_ptr = ptr;
	mp->mp_cnt = len;

	/* start receiving */
	s = spltty();
	if (cd) {
		int rcor, rbpr;

		CD1400_WRITE_REG(cd, CD1400_CAR, 0);

		/* input strobe at 100kbaud (10microseconds) */
		cd1400_compute_baud(100000, cd->cd_clock, &rcor, &rbpr);
		CD1400_WRITE_REG(cd, CD1400_RCOR, rcor);
		CD1400_WRITE_REG(cd, CD1400_RBPR, rbpr);

		/* rx threshold */
		CD1400_WRITE_REG(cd, CD1400_COR3, MBPP_RX_FIFO_THRESHOLD);
		cd1400_write_ccr(cd, CD1400_CCR_CMDCORCHG | CD1400_CCR_COR3);

		/* enable channel */
		cd1400_write_ccr(cd, CD1400_CCR_CMDCHANCTL | CD1400_CCR_RCVEN);
		CD1400_WRITE_REG(cd, CD1400_SRER, CD1400_SRER_RXDATA);
	}

	/* ZZzzz... */
	tsleep_nsec(mp, PCATCH | PZERO, "mbpp_recv", INFSLP);

	/* stop receiving */
	if (cd) {
		CD1400_WRITE_REG(cd, CD1400_CAR, 0);

		/* disable receiving */
		CD1400_WRITE_REG(cd, CD1400_SRER, 0);
		cd1400_write_ccr(cd, CD1400_CCR_CMDCHANCTL | CD1400_CCR_RCVDIS);
	}
	splx(s);

	/* return number of chars received */
	return (len - mp->mp_cnt);
}
