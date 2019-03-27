/*
 * Copyright (c) 2003 Hidetoshi Shimokawa
 * Copyright (c) 1998-2002 Katsushi Kobayashi and Hidetoshi Shimokawa
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the acknowledgement as bellow:
 *
 *    This product includes software developed by K. Kobayashi and H. Shimokawa
 *
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * $FreeBSD$
 *
 */

#include <stand.h>
#include <btxv86.h>
#include <bootstrap.h>

#include <dev/firewire/firewire.h>
#include "fwohci.h"
#include <dev/firewire/fwohcireg.h>
#include <dev/firewire/firewire_phy.h>

static uint32_t fwphy_wrdata ( struct fwohci_softc *, uint32_t, uint32_t);
static uint32_t fwphy_rddata ( struct fwohci_softc *, uint32_t);
int firewire_debug=0;

#if 0
#define device_printf(a, x, ...)	printf("FW1394: " x, ## __VA_ARGS__)
#else
#define device_printf(a, x, ...)
#endif

#define device_t int
#define	DELAY(x)	delay(x)

#define MAX_SPEED 3
#define MAXREC(x)  (2 << (x))
char *linkspeed[] = {
	"S100", "S200", "S400", "S800",
	"S1600", "S3200", "undef", "undef"
};

/*
 * Communication with PHY device
 */
static uint32_t
fwphy_wrdata( struct fwohci_softc *sc, uint32_t addr, uint32_t data)
{
	uint32_t fun;

	addr &= 0xf;
	data &= 0xff;

	fun = (PHYDEV_WRCMD | (addr << PHYDEV_REGADDR) | (data << PHYDEV_WRDATA));
	OWRITE(sc, OHCI_PHYACCESS, fun);
	DELAY(100);

	return(fwphy_rddata( sc, addr));
}

static uint32_t
fwphy_rddata(struct fwohci_softc *sc,  u_int addr)
{
	uint32_t fun, stat;
	u_int i, retry = 0;

	addr &= 0xf;
#define MAX_RETRY 100
again:
	OWRITE(sc, FWOHCI_INTSTATCLR, OHCI_INT_REG_FAIL);
	fun = PHYDEV_RDCMD | (addr << PHYDEV_REGADDR);
	OWRITE(sc, OHCI_PHYACCESS, fun);
	for ( i = 0 ; i < MAX_RETRY ; i ++ ){
		fun = OREAD(sc, OHCI_PHYACCESS);
		if ((fun & PHYDEV_RDCMD) == 0 && (fun & PHYDEV_RDDONE) != 0)
			break;
		DELAY(100);
	}
	if(i >= MAX_RETRY) {
		if (firewire_debug)
			device_printf(sc->fc.dev, "phy read failed(1).\n");
		if (++retry < MAX_RETRY) {
			DELAY(100);
			goto again;
		}
	}
	/* Make sure that SCLK is started */
	stat = OREAD(sc, FWOHCI_INTSTAT);
	if ((stat & OHCI_INT_REG_FAIL) != 0 ||
			((fun >> PHYDEV_REGADDR) & 0xf) != addr) {
		if (firewire_debug)
			device_printf(sc->fc.dev, "phy read failed(2).\n");
		if (++retry < MAX_RETRY) {
			DELAY(100);
			goto again;
		}
	}
	if (firewire_debug || retry >= MAX_RETRY)
		device_printf(sc->fc.dev, 
		    "fwphy_rddata: 0x%x loop=%d, retry=%d\n", addr, i, retry);
#undef MAX_RETRY
	return((fun >> PHYDEV_RDDATA )& 0xff);
}


static int
fwohci_probe_phy(struct fwohci_softc *sc, device_t dev)
{
	uint32_t reg, reg2;
	int e1394a = 1;
	int nport, speed;
/*
 * probe PHY parameters
 * 0. to prove PHY version, whether compliance of 1394a.
 * 1. to probe maximum speed supported by the PHY and 
 *    number of port supported by core-logic.
 *    It is not actually available port on your PC .
 */
	OWRITE(sc, OHCI_HCCCTL, OHCI_HCC_LPS);
	DELAY(500);

	reg = fwphy_rddata(sc, FW_PHY_SPD_REG);

	if((reg >> 5) != 7 ){
		nport = reg & FW_PHY_NP;
		speed = reg & FW_PHY_SPD >> 6;
		if (speed > MAX_SPEED) {
			device_printf(dev, "invalid speed %d (fixed to %d).\n",
				speed, MAX_SPEED);
			speed = MAX_SPEED;
		}
		device_printf(dev,
			"Phy 1394 only %s, %d ports.\n",
			linkspeed[speed], nport);
	}else{
		reg2 = fwphy_rddata(sc, FW_PHY_ESPD_REG);
		nport = reg & FW_PHY_NP;
		speed = (reg2 & FW_PHY_ESPD) >> 5;
		if (speed > MAX_SPEED) {
			device_printf(dev, "invalid speed %d (fixed to %d).\n",
				speed, MAX_SPEED);
			speed = MAX_SPEED;
		}
		device_printf(dev,
			"Phy 1394a available %s, %d ports.\n",
			linkspeed[speed], nport);

		/* check programPhyEnable */
		reg2 = fwphy_rddata(sc, 5);
#if 0
		if (e1394a && (OREAD(sc, OHCI_HCCCTL) & OHCI_HCC_PRPHY)) {
#else	/* XXX force to enable 1394a */
		if (e1394a) {
#endif
			if (firewire_debug)
				device_printf(dev,
					"Enable 1394a Enhancements\n");
			/* enable EAA EMC */
			reg2 |= 0x03;
			/* set aPhyEnhanceEnable */
			OWRITE(sc, OHCI_HCCCTL, OHCI_HCC_PHYEN);
			OWRITE(sc, OHCI_HCCCTLCLR, OHCI_HCC_PRPHY);
		} else {
			/* for safe */
			reg2 &= ~0x83;
		}
		reg2 = fwphy_wrdata(sc, 5, reg2);
	}
	sc->speed = speed;

	reg = fwphy_rddata(sc, FW_PHY_SPD_REG);
	if((reg >> 5) == 7 ){
		reg = fwphy_rddata(sc, 4);
		reg |= 1 << 6;
		fwphy_wrdata(sc, 4, reg);
		reg = fwphy_rddata(sc, 4);
	}
	return 0;
}


void
fwohci_reset(struct fwohci_softc *sc, device_t dev)
{
	int i, max_rec, speed;
	uint32_t reg, reg2;

	/* Disable interrupts */ 
	OWRITE(sc, FWOHCI_INTMASKCLR, ~0);

	/* FLUSH FIFO and reset Transmitter/Receiver */
	OWRITE(sc, OHCI_HCCCTL, OHCI_HCC_RESET);
	if (firewire_debug)
		device_printf(dev, "resetting OHCI...");
	i = 0;
	while(OREAD(sc, OHCI_HCCCTL) & OHCI_HCC_RESET) {
		if (i++ > 100) break;
		DELAY(1000);
	}
	if (firewire_debug)
		printf("done (loop=%d)\n", i);

	/* Probe phy */
	fwohci_probe_phy(sc, dev);

	/* Probe link */
	reg = OREAD(sc,  OHCI_BUS_OPT);
	reg2 = reg | OHCI_BUSFNC;
	max_rec = (reg & 0x0000f000) >> 12;
	speed = (reg & 0x00000007);
	device_printf(dev, "Link %s, max_rec %d bytes.\n",
			linkspeed[speed], MAXREC(max_rec));
	/* XXX fix max_rec */
	sc->maxrec = sc->speed + 8;
	if (max_rec != sc->maxrec) {
		reg2 = (reg2 & 0xffff0fff) | (sc->maxrec << 12);
		device_printf(dev, "max_rec %d -> %d\n",
				MAXREC(max_rec), MAXREC(sc->maxrec));
	}
	if (firewire_debug)
		device_printf(dev, "BUS_OPT 0x%x -> 0x%x\n", reg, reg2);
	OWRITE(sc,  OHCI_BUS_OPT, reg2);

	/* Initialize registers */
	OWRITE(sc, OHCI_CROMHDR, sc->config_rom[0]);
	OWRITE(sc, OHCI_CROMPTR, VTOP(sc->config_rom));
#if 0
	OWRITE(sc, OHCI_SID_BUF, sc->sid_dma.bus_addr);
#endif
	OWRITE(sc, OHCI_HCCCTLCLR, OHCI_HCC_BIGEND);
	OWRITE(sc, OHCI_HCCCTL, OHCI_HCC_POSTWR);
#if 0
	OWRITE(sc, OHCI_LNKCTL, OHCI_CNTL_SID);
#endif

	/* Enable link */
	OWRITE(sc, OHCI_HCCCTL, OHCI_HCC_LINKEN);
}

int
fwohci_init(struct fwohci_softc *sc, device_t dev)
{
	int i, mver;
	uint32_t reg;
	uint8_t ui[8];

/* OHCI version */
	reg = OREAD(sc, OHCI_VERSION);
	mver = (reg >> 16) & 0xff;
	device_printf(dev, "OHCI version %x.%x (ROM=%d)\n",
			mver, reg & 0xff, (reg>>24) & 1);
	if (mver < 1 || mver > 9) {
		device_printf(dev, "invalid OHCI version\n");
		return (ENXIO);
	}

/* Available Isochronous DMA channel probe */
	OWRITE(sc, OHCI_IT_MASK, 0xffffffff);
	OWRITE(sc, OHCI_IR_MASK, 0xffffffff);
	reg = OREAD(sc, OHCI_IT_MASK) & OREAD(sc, OHCI_IR_MASK);
	OWRITE(sc, OHCI_IT_MASKCLR, 0xffffffff);
	OWRITE(sc, OHCI_IR_MASKCLR, 0xffffffff);
	for (i = 0; i < 0x20; i++)
		if ((reg & (1 << i)) == 0)
			break;
	device_printf(dev, "No. of Isochronous channels is %d.\n", i);
	if (i == 0)
		return (ENXIO);

#if 0
/* SID receive buffer must align 2^11 */
#define	OHCI_SIDSIZE	(1 << 11)
	sc->sid_buf = fwdma_malloc(&sc->fc, OHCI_SIDSIZE, OHCI_SIDSIZE,
						&sc->sid_dma, BUS_DMA_WAITOK);
	if (sc->sid_buf == NULL) {
		device_printf(dev, "sid_buf alloc failed.");
		return ENOMEM;
	}
#endif

	sc->eui.hi = OREAD(sc, FWOHCIGUID_H);
	sc->eui.lo = OREAD(sc, FWOHCIGUID_L);
	for( i = 0 ; i < 8 ; i ++)
		ui[i] = FW_EUI64_BYTE(&sc->eui,i);
	device_printf(dev, "EUI64 %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
		ui[0], ui[1], ui[2], ui[3], ui[4], ui[5], ui[6], ui[7]);
	fwohci_reset(sc, dev);

	return 0;
}

void
fwohci_ibr(struct fwohci_softc *sc)
{
	uint32_t fun;

	device_printf(sc->dev, "Initiate bus reset\n");

	/*
	 * Make sure our cached values from the config rom are
	 * initialised.
	 */
	OWRITE(sc, OHCI_CROMHDR, ntohl(sc->config_rom[0]));
	OWRITE(sc, OHCI_BUS_OPT, ntohl(sc->config_rom[2]));

	/*
	 * Set root hold-off bit so that non cyclemaster capable node
	 * shouldn't became the root node.
	 */
#if 1
	fun = fwphy_rddata(sc, FW_PHY_IBR_REG);
	fun |= FW_PHY_IBR;
	fun = fwphy_wrdata(sc, FW_PHY_IBR_REG, fun);
#else	/* Short bus reset */
	fun = fwphy_rddata(sc, FW_PHY_ISBR_REG);
	fun |= FW_PHY_ISBR;
	fun = fwphy_wrdata(sc, FW_PHY_ISBR_REG, fun);
#endif
}


void
fwohci_sid(struct fwohci_softc *sc)
{
		uint32_t node_id;
		int plen;

		node_id = OREAD(sc, FWOHCI_NODEID);
		if (!(node_id & OHCI_NODE_VALID)) {
#if 0
			printf("Bus reset failure\n");
#endif
			return;
		}

		/* Enable bus reset interrupt */
		OWRITE(sc, FWOHCI_INTMASK,  OHCI_INT_PHY_BUS_R);
		/* Allow async. request to us */
		OWRITE(sc, OHCI_AREQHI, 1 << 31);
		/* XXX insecure ?? */
		OWRITE(sc, OHCI_PREQHI, 0x7fffffff);
		OWRITE(sc, OHCI_PREQLO, 0xffffffff);
		OWRITE(sc, OHCI_PREQUPPER, 0x10000);
		/* Set ATRetries register */
		OWRITE(sc, OHCI_ATRETRY, 1<<(13+16) | 0xfff);
/*
** Checking whether the node is root or not. If root, turn on 
** cycle master.
*/
		plen = OREAD(sc, OHCI_SID_CNT);
		device_printf(fc->dev, "node_id=0x%08x, gen=%d, ",
			node_id, (plen >> 16) & 0xff);
		if (node_id & OHCI_NODE_ROOT) {
			device_printf(sc->dev, "CYCLEMASTER mode\n");
			OWRITE(sc, OHCI_LNKCTL,
				OHCI_CNTL_CYCMTR | OHCI_CNTL_CYCTIMER);
		} else {
			device_printf(sc->dev, "non CYCLEMASTER mode\n");
			OWRITE(sc, OHCI_LNKCTLCLR, OHCI_CNTL_CYCMTR);
			OWRITE(sc, OHCI_LNKCTL, OHCI_CNTL_CYCTIMER);
		}
		if (plen & OHCI_SID_ERR) {
			device_printf(fc->dev, "SID Error\n");
			return;
		}
		device_printf(sc->dev, "bus reset phase done\n");
		sc->state = FWOHCI_STATE_NORMAL;
}

static void
fwohci_intr_body(struct fwohci_softc *sc, uint32_t stat, int count)
{
#undef OHCI_DEBUG
#ifdef OHCI_DEBUG
#if 0
	if(stat & OREAD(sc, FWOHCI_INTMASK))
#else
	if (1)
#endif
		device_printf(fc->dev, "INTERRUPT < %s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s> 0x%08x, 0x%08x\n",
			stat & OHCI_INT_EN ? "DMA_EN ":"",
			stat & OHCI_INT_PHY_REG ? "PHY_REG ":"",
			stat & OHCI_INT_CYC_LONG ? "CYC_LONG ":"",
			stat & OHCI_INT_ERR ? "INT_ERR ":"",
			stat & OHCI_INT_CYC_ERR ? "CYC_ERR ":"",
			stat & OHCI_INT_CYC_LOST ? "CYC_LOST ":"",
			stat & OHCI_INT_CYC_64SECOND ? "CYC_64SECOND ":"",
			stat & OHCI_INT_CYC_START ? "CYC_START ":"",
			stat & OHCI_INT_PHY_INT ? "PHY_INT ":"",
			stat & OHCI_INT_PHY_BUS_R ? "BUS_RESET ":"",
			stat & OHCI_INT_PHY_SID ? "SID ":"",
			stat & OHCI_INT_LR_ERR ? "DMA_LR_ERR ":"",
			stat & OHCI_INT_PW_ERR ? "DMA_PW_ERR ":"",
			stat & OHCI_INT_DMA_IR ? "DMA_IR ":"",
			stat & OHCI_INT_DMA_IT  ? "DMA_IT " :"",
			stat & OHCI_INT_DMA_PRRS  ? "DMA_PRRS " :"",
			stat & OHCI_INT_DMA_PRRQ  ? "DMA_PRRQ " :"",
			stat & OHCI_INT_DMA_ARRS  ? "DMA_ARRS " :"",
			stat & OHCI_INT_DMA_ARRQ  ? "DMA_ARRQ " :"",
			stat & OHCI_INT_DMA_ATRS  ? "DMA_ATRS " :"",
			stat & OHCI_INT_DMA_ATRQ  ? "DMA_ATRQ " :"",
			stat, OREAD(sc, FWOHCI_INTMASK) 
		);
#endif
/* Bus reset */
	if(stat & OHCI_INT_PHY_BUS_R ){
		device_printf(fc->dev, "BUS reset\n");
		if (sc->state == FWOHCI_STATE_BUSRESET)
			goto busresetout;
		sc->state = FWOHCI_STATE_BUSRESET;
		/* Disable bus reset interrupt until sid recv. */
		OWRITE(sc, FWOHCI_INTMASKCLR,  OHCI_INT_PHY_BUS_R);
	
		OWRITE(sc, FWOHCI_INTMASKCLR,  OHCI_INT_CYC_LOST);
		OWRITE(sc, OHCI_LNKCTLCLR, OHCI_CNTL_CYCSRC);

		OWRITE(sc, OHCI_CROMHDR, ntohl(sc->config_rom[0]));
		OWRITE(sc, OHCI_BUS_OPT, ntohl(sc->config_rom[2]));
	} else if (sc->state == FWOHCI_STATE_BUSRESET) {
		fwohci_sid(sc);
	}
busresetout:
	return;
}

static uint32_t
fwochi_check_stat(struct fwohci_softc *sc)
{
	uint32_t stat;

	stat = OREAD(sc, FWOHCI_INTSTAT);
	if (stat == 0xffffffff) {
		device_printf(sc->fc.dev, 
			"device physically ejected?\n");
		return(stat);
	}
	if (stat)
		OWRITE(sc, FWOHCI_INTSTATCLR, stat);
	return(stat);
}

void
fwohci_poll(struct fwohci_softc *sc)
{
	uint32_t stat;

	stat = fwochi_check_stat(sc);
	if (stat != 0xffffffff)
		fwohci_intr_body(sc, stat, 1);
}
