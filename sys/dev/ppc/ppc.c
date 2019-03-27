/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997-2000 Nicolas Souchu
 * Copyright (c) 2001 Alcove - Nicolas Souchu
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

#include "opt_ppc.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/interrupt.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/proc.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#ifdef __i386__
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/vmparam.h>
#include <machine/pc/bios.h>
#endif

#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/ppb_msq.h>

#include <dev/ppc/ppcvar.h>
#include <dev/ppc/ppcreg.h>

#include "ppbus_if.h"

static void ppcintr(void *arg);

#define	IO_LPTSIZE_EXTENDED	8	/* "Extended" LPT controllers */
#define	IO_LPTSIZE_NORMAL	4	/* "Normal" LPT controllers */

#define LOG_PPC(function, ppc, string) \
		if (bootverbose) printf("%s: %s\n", function, string)

#define DEVTOSOFTC(dev) ((struct ppc_data *)device_get_softc(dev))

/*
 * We use critical enter/exit for the simple config locking needed to
 * detect the devices. We just want to make sure that both of our writes
 * happen without someone else also writing to those config registers. Since
 * we just do this at startup, Giant keeps multiple threads from executing,
 * and critical_enter() then is all that's needed to keep us from being preempted
 * during the critical sequences with the hardware.
 *
 * Note: this doesn't prevent multiple threads from putting the chips into
 * config mode, but since we only do that to detect the type at startup the
 * extra overhead isn't needed since Giant protects us from multiple entry
 * and no other code changes these registers.
 */
#define PPC_CONFIG_LOCK(ppc)		critical_enter()
#define PPC_CONFIG_UNLOCK(ppc)		critical_exit()

devclass_t ppc_devclass;
const char ppc_driver_name[] = "ppc";

static char *ppc_models[] = {
	"SMC-like", "SMC FDC37C665GT", "SMC FDC37C666GT", "PC87332", "PC87306",
	"82091AA", "Generic", "W83877F", "W83877AF", "Winbond", "PC87334",
	"SMC FDC37C935", "PC87303", 0
};

/* list of available modes */
static char *ppc_avms[] = {
	"COMPATIBLE", "NIBBLE-only", "PS2-only", "PS2/NIBBLE", "EPP-only",
	"EPP/NIBBLE", "EPP/PS2", "EPP/PS2/NIBBLE", "ECP-only",
	"ECP/NIBBLE", "ECP/PS2", "ECP/PS2/NIBBLE", "ECP/EPP",
	"ECP/EPP/NIBBLE", "ECP/EPP/PS2", "ECP/EPP/PS2/NIBBLE", 0
};

/* list of current executing modes
 * Note that few modes do not actually exist.
 */
static char *ppc_modes[] = {
	"COMPATIBLE", "NIBBLE", "PS/2", "PS/2", "EPP",
	"EPP", "EPP", "EPP", "ECP",
	"ECP", "ECP+PS2", "ECP+PS2", "ECP+EPP",
	"ECP+EPP", "ECP+EPP", "ECP+EPP", 0
};

static char *ppc_epp_protocol[] = { " (EPP 1.9)", " (EPP 1.7)", 0 };

#ifdef __i386__
/*
 * BIOS printer list - used by BIOS probe.
 */
#define	BIOS_PPC_PORTS	0x408
#define	BIOS_PORTS	((short *)BIOS_PADDRTOVADDR(BIOS_PPC_PORTS))
#define	BIOS_MAX_PPC	4
#endif

/*
 * ppc_ecp_sync()		XXX
 */
int
ppc_ecp_sync(device_t dev)
{
	int i, r;
	struct ppc_data *ppc = DEVTOSOFTC(dev);

	PPC_ASSERT_LOCKED(ppc);
	if (!(ppc->ppc_avm & PPB_ECP) && !(ppc->ppc_dtm & PPB_ECP))
		return 0;

	r = r_ecr(ppc);
	if ((r & 0xe0) != PPC_ECR_EPP)
		return 0;

	for (i = 0; i < 100; i++) {
		r = r_ecr(ppc);
		if (r & 0x1)
			return 0;
		DELAY(100);
	}

	device_printf(dev, "ECP sync failed as data still present in FIFO.\n");

	return 0;
}

/*
 * ppc_detect_fifo()
 *
 * Detect parallel port FIFO
 */
static int
ppc_detect_fifo(struct ppc_data *ppc)
{
	char ecr_sav;
	char ctr_sav, ctr, cc;
	short i;

	/* save registers */
	ecr_sav = r_ecr(ppc);
	ctr_sav = r_ctr(ppc);

	/* enter ECP configuration mode, no interrupt, no DMA */
	w_ecr(ppc, 0xf4);

	/* read PWord size - transfers in FIFO mode must be PWord aligned */
	ppc->ppc_pword = (r_cnfgA(ppc) & PPC_PWORD_MASK);

	/* XXX 16 and 32 bits implementations not supported */
	if (ppc->ppc_pword != PPC_PWORD_8) {
		LOG_PPC(__func__, ppc, "PWord not supported");
		goto error;
	}

	w_ecr(ppc, 0x34);		/* byte mode, no interrupt, no DMA */
	ctr = r_ctr(ppc);
	w_ctr(ppc, ctr | PCD);		/* set direction to 1 */

	/* enter ECP test mode, no interrupt, no DMA */
	w_ecr(ppc, 0xd4);

	/* flush the FIFO */
	for (i=0; i<1024; i++) {
		if (r_ecr(ppc) & PPC_FIFO_EMPTY)
			break;
		cc = r_fifo(ppc);
	}

	if (i >= 1024) {
		LOG_PPC(__func__, ppc, "can't flush FIFO");
		goto error;
	}

	/* enable interrupts, no DMA */
	w_ecr(ppc, 0xd0);

	/* determine readIntrThreshold
	 * fill the FIFO until serviceIntr is set
	 */
	for (i=0; i<1024; i++) {
		w_fifo(ppc, (char)i);
		if (!ppc->ppc_rthr && (r_ecr(ppc) & PPC_SERVICE_INTR)) {
			/* readThreshold reached */
			ppc->ppc_rthr = i+1;
		}
		if (r_ecr(ppc) & PPC_FIFO_FULL) {
			ppc->ppc_fifo = i+1;
			break;
		}
	}

	if (i >= 1024) {
		LOG_PPC(__func__, ppc, "can't fill FIFO");
		goto error;
	}

	w_ecr(ppc, 0xd4);		/* test mode, no interrupt, no DMA */
	w_ctr(ppc, ctr & ~PCD);		/* set direction to 0 */
	w_ecr(ppc, 0xd0);		/* enable interrupts */

	/* determine writeIntrThreshold
	 * empty the FIFO until serviceIntr is set
	 */
	for (i=ppc->ppc_fifo; i>0; i--) {
		if (r_fifo(ppc) != (char)(ppc->ppc_fifo-i)) {
			LOG_PPC(__func__, ppc, "invalid data in FIFO");
			goto error;
		}
		if (r_ecr(ppc) & PPC_SERVICE_INTR) {
			/* writeIntrThreshold reached */
			ppc->ppc_wthr = ppc->ppc_fifo - i+1;
		}
		/* if FIFO empty before the last byte, error */
		if (i>1 && (r_ecr(ppc) & PPC_FIFO_EMPTY)) {
			LOG_PPC(__func__, ppc, "data lost in FIFO");
			goto error;
		}
	}

	/* FIFO must be empty after the last byte */
	if (!(r_ecr(ppc) & PPC_FIFO_EMPTY)) {
		LOG_PPC(__func__, ppc, "can't empty the FIFO");
		goto error;
	}

	w_ctr(ppc, ctr_sav);
	w_ecr(ppc, ecr_sav);

	return (0);

error:
	w_ctr(ppc, ctr_sav);
	w_ecr(ppc, ecr_sav);

	return (EINVAL);
}

static int
ppc_detect_port(struct ppc_data *ppc)
{

	w_ctr(ppc, 0x0c);	/* To avoid missing PS2 ports */
	w_dtr(ppc, 0xaa);
	if (r_dtr(ppc) != 0xaa)
		return (0);

	return (1);
}

/*
 * EPP timeout, according to the PC87332 manual
 * Semantics of clearing EPP timeout bit.
 * PC87332	- reading SPP_STR does it...
 * SMC		- write 1 to EPP timeout bit			XXX
 * Others	- (?) write 0 to EPP timeout bit
 */
static void
ppc_reset_epp_timeout(struct ppc_data *ppc)
{
	char r;

	r = r_str(ppc);
	w_str(ppc, r | 0x1);
	w_str(ppc, r & 0xfe);

	return;
}

static int
ppc_check_epp_timeout(struct ppc_data *ppc)
{
	ppc_reset_epp_timeout(ppc);

	return (!(r_str(ppc) & TIMEOUT));
}

/*
 * Configure current operating mode
 */
static int
ppc_generic_setmode(struct ppc_data *ppc, int mode)
{
	u_char ecr = 0;

	/* check if mode is available */
	if (mode && !(ppc->ppc_avm & mode))
		return (EINVAL);

	/* if ECP mode, configure ecr register */
	if ((ppc->ppc_avm & PPB_ECP) || (ppc->ppc_dtm & PPB_ECP)) {
		/* return to byte mode (keeping direction bit),
		 * no interrupt, no DMA to be able to change to
		 * ECP
		 */
		w_ecr(ppc, PPC_ECR_RESET);
		ecr = PPC_DISABLE_INTR;

		if (mode & PPB_EPP)
			return (EINVAL);
		else if (mode & PPB_ECP)
			/* select ECP mode */
			ecr |= PPC_ECR_ECP;
		else if (mode & PPB_PS2)
			/* select PS2 mode with ECP */
			ecr |= PPC_ECR_PS2;
		else
			/* select COMPATIBLE/NIBBLE mode */
			ecr |= PPC_ECR_STD;

		w_ecr(ppc, ecr);
	}

	ppc->ppc_mode = mode;

	return (0);
}

/*
 * The ppc driver is free to choose options like FIFO or DMA
 * if ECP mode is available.
 *
 * The 'RAW' option allows the upper drivers to force the ppc mode
 * even with FIFO, DMA available.
 */
static int
ppc_smclike_setmode(struct ppc_data *ppc, int mode)
{
	u_char ecr = 0;

	/* check if mode is available */
	if (mode && !(ppc->ppc_avm & mode))
		return (EINVAL);

	/* if ECP mode, configure ecr register */
	if ((ppc->ppc_avm & PPB_ECP) || (ppc->ppc_dtm & PPB_ECP)) {
		/* return to byte mode (keeping direction bit),
		 * no interrupt, no DMA to be able to change to
		 * ECP or EPP mode
		 */
		w_ecr(ppc, PPC_ECR_RESET);
		ecr = PPC_DISABLE_INTR;

		if (mode & PPB_EPP)
			/* select EPP mode */
			ecr |= PPC_ECR_EPP;
		else if (mode & PPB_ECP)
			/* select ECP mode */
			ecr |= PPC_ECR_ECP;
		else if (mode & PPB_PS2)
			/* select PS2 mode with ECP */
			ecr |= PPC_ECR_PS2;
		else
			/* select COMPATIBLE/NIBBLE mode */
			ecr |= PPC_ECR_STD;

		w_ecr(ppc, ecr);
	}

	ppc->ppc_mode = mode;

	return (0);
}

#ifdef PPC_PROBE_CHIPSET
/*
 * ppc_pc873xx_detect
 *
 * Probe for a Natsemi PC873xx-family part.
 *
 * References in this function are to the National Semiconductor
 * PC87332 datasheet TL/C/11930, May 1995 revision.
 */
static int pc873xx_basetab[] = {0x0398, 0x026e, 0x015c, 0x002e, 0};
static int pc873xx_porttab[] = {0x0378, 0x03bc, 0x0278, 0};
static int pc873xx_irqtab[] = {5, 7, 5, 0};

static int pc873xx_regstab[] = {
	PC873_FER, PC873_FAR, PC873_PTR,
	PC873_FCR, PC873_PCR, PC873_PMC,
	PC873_TUP, PC873_SID, PC873_PNP0,
	PC873_PNP1, PC873_LPTBA, -1
};

static char *pc873xx_rnametab[] = {
	"FER", "FAR", "PTR", "FCR", "PCR",
	"PMC", "TUP", "SID", "PNP0", "PNP1",
	"LPTBA", NULL
};

static int
ppc_pc873xx_detect(struct ppc_data *ppc, int chipset_mode)	/* XXX mode never forced */
{
    static int	index = 0;
    int		idport, irq;
    int		ptr, pcr, val, i;

    while ((idport = pc873xx_basetab[index++])) {

	/* XXX should check first to see if this location is already claimed */

	/*
	 * Pull the 873xx through the power-on ID cycle (2.2,1.).
	 * We can't use this to locate the chip as it may already have
	 * been used by the BIOS.
	 */
	(void)inb(idport); (void)inb(idport);
	(void)inb(idport); (void)inb(idport);

	/*
	 * Read the SID byte.  Possible values are :
	 *
	 * 01010xxx	PC87334
	 * 0001xxxx	PC87332
	 * 01110xxx	PC87306
	 * 00110xxx	PC87303
	 */
	outb(idport, PC873_SID);
	val = inb(idport + 1);
	if ((val & 0xf0) == 0x10) {
	    ppc->ppc_model = NS_PC87332;
	} else if ((val & 0xf8) == 0x70) {
	    ppc->ppc_model = NS_PC87306;
	} else if ((val & 0xf8) == 0x50) {
	    ppc->ppc_model = NS_PC87334;
	} else if ((val & 0xf8) == 0x40) { /* Should be 0x30 by the
					      documentation, but probing
					      yielded 0x40... */
	    ppc->ppc_model = NS_PC87303;
	} else {
	    if (bootverbose && (val != 0xff))
		printf("PC873xx probe at 0x%x got unknown ID 0x%x\n", idport, val);
	    continue ;		/* not recognised */
	}

	/* print registers */
	if (bootverbose) {
		printf("PC873xx");
		for (i=0; pc873xx_regstab[i] != -1; i++) {
			outb(idport, pc873xx_regstab[i]);
			printf(" %s=0x%x", pc873xx_rnametab[i],
						inb(idport + 1) & 0xff);
		}
		printf("\n");
	}

	/*
	 * We think we have one.  Is it enabled and where we want it to be?
	 */
	outb(idport, PC873_FER);
	val = inb(idport + 1);
	if (!(val & PC873_PPENABLE)) {
	    if (bootverbose)
		printf("PC873xx parallel port disabled\n");
	    continue;
	}
	outb(idport, PC873_FAR);
	val = inb(idport + 1);
	/* XXX we should create a driver instance for every port found */
	if (pc873xx_porttab[val & 0x3] != ppc->ppc_base) {

	    /* First try to change the port address to that requested... */

	    switch (ppc->ppc_base) {
		case 0x378:
		val &= 0xfc;
		break;

		case 0x3bc:
		val &= 0xfd;
		break;

		case 0x278:
		val &= 0xfe;
		break;

		default:
		val &= 0xfd;
		break;
	    }

	    outb(idport, PC873_FAR);
	    outb(idport + 1, val);
	    outb(idport + 1, val);

	    /* Check for success by reading back the value we supposedly
	       wrote and comparing...*/

	    outb(idport, PC873_FAR);
	    val = inb(idport + 1) & 0x3;

	    /* If we fail, report the failure... */

	    if (pc873xx_porttab[val] != ppc->ppc_base) {
 		if (bootverbose)
	  	    printf("PC873xx at 0x%x not for driver at port 0x%x\n",
			   pc873xx_porttab[val], ppc->ppc_base);
	    }
	    continue;
	}

	outb(idport, PC873_PTR);
	ptr = inb(idport + 1);

	/* get irq settings */
	if (ppc->ppc_base == 0x378)
		irq = (ptr & PC873_LPTBIRQ7) ? 7 : 5;
	else
		irq = pc873xx_irqtab[val];

	if (bootverbose)
		printf("PC873xx irq %d at 0x%x\n", irq, ppc->ppc_base);

	/*
	 * Check if irq settings are correct
	 */
	if (irq != ppc->ppc_irq) {
		/*
		 * If the chipset is not locked and base address is 0x378,
		 * we have another chance
		 */
		if (ppc->ppc_base == 0x378 && !(ptr & PC873_CFGLOCK)) {
			if (ppc->ppc_irq == 7) {
				outb(idport + 1, (ptr | PC873_LPTBIRQ7));
				outb(idport + 1, (ptr | PC873_LPTBIRQ7));
			} else {
				outb(idport + 1, (ptr & ~PC873_LPTBIRQ7));
				outb(idport + 1, (ptr & ~PC873_LPTBIRQ7));
			}
			if (bootverbose)
			   printf("PC873xx irq set to %d\n", ppc->ppc_irq);
		} else {
			if (bootverbose)
			   printf("PC873xx sorry, can't change irq setting\n");
		}
	} else {
		if (bootverbose)
			printf("PC873xx irq settings are correct\n");
	}

	outb(idport, PC873_PCR);
	pcr = inb(idport + 1);

	if ((ptr & PC873_CFGLOCK) || !chipset_mode) {
	    if (bootverbose)
		printf("PC873xx %s", (ptr & PC873_CFGLOCK)?"locked":"unlocked");

	    ppc->ppc_avm |= PPB_NIBBLE;
	    if (bootverbose)
		printf(", NIBBLE");

	    if (pcr & PC873_EPPEN) {
		ppc->ppc_avm |= PPB_EPP;

		if (bootverbose)
			printf(", EPP");

		if (pcr & PC873_EPP19)
			ppc->ppc_epp = EPP_1_9;
		else
			ppc->ppc_epp = EPP_1_7;

		if ((ppc->ppc_model == NS_PC87332) && bootverbose) {
			outb(idport, PC873_PTR);
			ptr = inb(idport + 1);
			if (ptr & PC873_EPPRDIR)
				printf(", Regular mode");
			else
				printf(", Automatic mode");
		}
	    } else if (pcr & PC873_ECPEN) {
		ppc->ppc_avm |= PPB_ECP;
		if (bootverbose)
			printf(", ECP");

		if (pcr & PC873_ECPCLK)	{		/* XXX */
			ppc->ppc_avm |= PPB_PS2;
			if (bootverbose)
				printf(", PS/2");
		}
	    } else {
		outb(idport, PC873_PTR);
		ptr = inb(idport + 1);
		if (ptr & PC873_EXTENDED) {
			ppc->ppc_avm |= PPB_SPP;
			if (bootverbose)
				printf(", SPP");
		}
	    }
	} else {
		if (bootverbose)
			printf("PC873xx unlocked");

		if (chipset_mode & PPB_ECP) {
			if ((chipset_mode & PPB_EPP) && bootverbose)
				printf(", ECP+EPP not supported");

			pcr &= ~PC873_EPPEN;
			pcr |= (PC873_ECPEN | PC873_ECPCLK);	/* XXX */
			outb(idport + 1, pcr);
			outb(idport + 1, pcr);

			if (bootverbose)
				printf(", ECP");

		} else if (chipset_mode & PPB_EPP) {
			pcr &= ~(PC873_ECPEN | PC873_ECPCLK);
			pcr |= (PC873_EPPEN | PC873_EPP19);
			outb(idport + 1, pcr);
			outb(idport + 1, pcr);

			ppc->ppc_epp = EPP_1_9;			/* XXX */

			if (bootverbose)
				printf(", EPP1.9");

			/* enable automatic direction turnover */
			if (ppc->ppc_model == NS_PC87332) {
				outb(idport, PC873_PTR);
				ptr = inb(idport + 1);
				ptr &= ~PC873_EPPRDIR;
				outb(idport + 1, ptr);
				outb(idport + 1, ptr);

				if (bootverbose)
					printf(", Automatic mode");
			}
		} else {
			pcr &= ~(PC873_ECPEN | PC873_ECPCLK | PC873_EPPEN);
			outb(idport + 1, pcr);
			outb(idport + 1, pcr);

			/* configure extended bit in PTR */
			outb(idport, PC873_PTR);
			ptr = inb(idport + 1);

			if (chipset_mode & PPB_PS2) {
				ptr |= PC873_EXTENDED;

				if (bootverbose)
					printf(", PS/2");

			} else {
				/* default to NIBBLE mode */
				ptr &= ~PC873_EXTENDED;

				if (bootverbose)
					printf(", NIBBLE");
			}
			outb(idport + 1, ptr);
			outb(idport + 1, ptr);
		}

		ppc->ppc_avm = chipset_mode;
	}

	if (bootverbose)
		printf("\n");

	ppc->ppc_type = PPC_TYPE_GENERIC;
	ppc_generic_setmode(ppc, chipset_mode);

	return(chipset_mode);
    }
    return(-1);
}

/*
 * ppc_smc37c66xgt_detect
 *
 * SMC FDC37C66xGT configuration.
 */
static int
ppc_smc37c66xgt_detect(struct ppc_data *ppc, int chipset_mode)
{
	int i;
	u_char r;
	int type = -1;
	int csr = SMC66x_CSR;	/* initial value is 0x3F0 */

	int port_address[] = { -1 /* disabled */ , 0x3bc, 0x378, 0x278 };


#define cio csr+1	/* config IO port is either 0x3F1 or 0x371 */

	/*
	 * Detection: enter configuration mode and read CRD register.
	 */
	PPC_CONFIG_LOCK(ppc);
	outb(csr, SMC665_iCODE);
	outb(csr, SMC665_iCODE);
	PPC_CONFIG_UNLOCK(ppc);

	outb(csr, 0xd);
	if (inb(cio) == 0x65) {
		type = SMC_37C665GT;
		goto config;
	}

	for (i = 0; i < 2; i++) {
		PPC_CONFIG_LOCK(ppc);
		outb(csr, SMC666_iCODE);
		outb(csr, SMC666_iCODE);
		PPC_CONFIG_UNLOCK(ppc);

		outb(csr, 0xd);
		if (inb(cio) == 0x66) {
			type = SMC_37C666GT;
			break;
		}

		/* Another chance, CSR may be hard-configured to be at 0x370 */
		csr = SMC666_CSR;
	}

config:
	/*
	 * If chipset not found, do not continue.
	 */
	if (type == -1) {
		outb(csr, 0xaa);	/* end config mode */
		return (-1);
	}

	/* select CR1 */
	outb(csr, 0x1);

	/* read the port's address: bits 0 and 1 of CR1 */
	r = inb(cio) & SMC_CR1_ADDR;
	if (port_address[(int)r] != ppc->ppc_base) {
		outb(csr, 0xaa);	/* end config mode */
		return (-1);
	}

	ppc->ppc_model = type;

	/*
	 * CR1 and CR4 registers bits 3 and 0/1 for mode configuration
	 * If SPP mode is detected, try to set ECP+EPP mode
	 */

	if (bootverbose) {
		outb(csr, 0x1);
		device_printf(ppc->ppc_dev, "SMC registers CR1=0x%x",
		    inb(cio) & 0xff);

		outb(csr, 0x4);
		printf(" CR4=0x%x", inb(cio) & 0xff);
	}

	/* select CR1 */
	outb(csr, 0x1);

	if (!chipset_mode) {
		/* autodetect mode */

		/* 666GT is ~certainly~ hardwired to an extended ECP+EPP mode */
		if (type == SMC_37C666GT) {
			ppc->ppc_avm |= PPB_ECP | PPB_EPP | PPB_SPP;
			if (bootverbose)
				printf(" configuration hardwired, supposing " \
					"ECP+EPP SPP");

		} else
		   if ((inb(cio) & SMC_CR1_MODE) == 0) {
			/* already in extended parallel port mode, read CR4 */
			outb(csr, 0x4);
			r = (inb(cio) & SMC_CR4_EMODE);

			switch (r) {
			case SMC_SPP:
				ppc->ppc_avm |= PPB_SPP;
				if (bootverbose)
					printf(" SPP");
				break;

			case SMC_EPPSPP:
				ppc->ppc_avm |= PPB_EPP | PPB_SPP;
				if (bootverbose)
					printf(" EPP SPP");
				break;

			case SMC_ECP:
				ppc->ppc_avm |= PPB_ECP | PPB_SPP;
				if (bootverbose)
					printf(" ECP SPP");
				break;

			case SMC_ECPEPP:
				ppc->ppc_avm |= PPB_ECP | PPB_EPP | PPB_SPP;
				if (bootverbose)
					printf(" ECP+EPP SPP");
				break;
			}
		   } else {
			/* not an extended port mode */
			ppc->ppc_avm |= PPB_SPP;
			if (bootverbose)
				printf(" SPP");
		   }

	} else {
		/* mode forced */
		ppc->ppc_avm = chipset_mode;

		/* 666GT is ~certainly~ hardwired to an extended ECP+EPP mode */
		if (type == SMC_37C666GT)
			goto end_detect;

		r = inb(cio);
		if ((chipset_mode & (PPB_ECP | PPB_EPP)) == 0) {
			/* do not use ECP when the mode is not forced to */
			outb(cio, r | SMC_CR1_MODE);
			if (bootverbose)
				printf(" SPP");
		} else {
			/* an extended mode is selected */
			outb(cio, r & ~SMC_CR1_MODE);

			/* read CR4 register and reset mode field */
			outb(csr, 0x4);
			r = inb(cio) & ~SMC_CR4_EMODE;

			if (chipset_mode & PPB_ECP) {
				if (chipset_mode & PPB_EPP) {
					outb(cio, r | SMC_ECPEPP);
					if (bootverbose)
						printf(" ECP+EPP");
				} else {
					outb(cio, r | SMC_ECP);
					if (bootverbose)
						printf(" ECP");
				}
			} else {
				/* PPB_EPP is set */
				outb(cio, r | SMC_EPPSPP);
				if (bootverbose)
					printf(" EPP SPP");
			}
		}
		ppc->ppc_avm = chipset_mode;
	}

	/* set FIFO threshold to 16 */
	if (ppc->ppc_avm & PPB_ECP) {
		/* select CRA */
		outb(csr, 0xa);
		outb(cio, 16);
	}

end_detect:

	if (bootverbose)
		printf ("\n");

	if (ppc->ppc_avm & PPB_EPP) {
		/* select CR4 */
		outb(csr, 0x4);
		r = inb(cio);

		/*
		 * Set the EPP protocol...
		 * Low=EPP 1.9 (1284 standard) and High=EPP 1.7
		 */
		if (ppc->ppc_epp == EPP_1_9)
			outb(cio, (r & ~SMC_CR4_EPPTYPE));
		else
			outb(cio, (r | SMC_CR4_EPPTYPE));
	}

	outb(csr, 0xaa);	/* end config mode */

	ppc->ppc_type = PPC_TYPE_SMCLIKE;
	ppc_smclike_setmode(ppc, chipset_mode);

	return (chipset_mode);
}

/*
 * SMC FDC37C935 configuration
 * Found on many Alpha machines
 */
static int
ppc_smc37c935_detect(struct ppc_data *ppc, int chipset_mode)
{
	int type = -1;

	PPC_CONFIG_LOCK(ppc);
	outb(SMC935_CFG, 0x55); /* enter config mode */
	outb(SMC935_CFG, 0x55);
	PPC_CONFIG_UNLOCK(ppc);

	outb(SMC935_IND, SMC935_ID); /* check device id */
	if (inb(SMC935_DAT) == 0x2)
		type = SMC_37C935;

	if (type == -1) {
		outb(SMC935_CFG, 0xaa); /* exit config mode */
		return (-1);
	}

	ppc->ppc_model = type;

	outb(SMC935_IND, SMC935_LOGDEV); /* select parallel port, */
	outb(SMC935_DAT, 3);	     /* which is logical device 3 */

	/* set io port base */
	outb(SMC935_IND, SMC935_PORTHI);
	outb(SMC935_DAT, (u_char)((ppc->ppc_base & 0xff00) >> 8));
	outb(SMC935_IND, SMC935_PORTLO);
	outb(SMC935_DAT, (u_char)(ppc->ppc_base & 0xff));

	if (!chipset_mode)
		ppc->ppc_avm = PPB_COMPATIBLE; /* default mode */
	else {
		ppc->ppc_avm = chipset_mode;
		outb(SMC935_IND, SMC935_PPMODE);
		outb(SMC935_DAT, SMC935_CENT); /* start in compatible mode */

		/* SPP + EPP or just plain SPP */
		if (chipset_mode & (PPB_SPP)) {
			if (chipset_mode & PPB_EPP) {
				if (ppc->ppc_epp == EPP_1_9) {
					outb(SMC935_IND, SMC935_PPMODE);
					outb(SMC935_DAT, SMC935_EPP19SPP);
				}
				if (ppc->ppc_epp == EPP_1_7) {
					outb(SMC935_IND, SMC935_PPMODE);
					outb(SMC935_DAT, SMC935_EPP17SPP);
				}
			} else {
				outb(SMC935_IND, SMC935_PPMODE);
				outb(SMC935_DAT, SMC935_SPP);
			}
		}

		/* ECP + EPP or just plain ECP */
		if (chipset_mode & PPB_ECP) {
			if (chipset_mode & PPB_EPP) {
				if (ppc->ppc_epp == EPP_1_9) {
					outb(SMC935_IND, SMC935_PPMODE);
					outb(SMC935_DAT, SMC935_ECPEPP19);
				}
				if (ppc->ppc_epp == EPP_1_7) {
					outb(SMC935_IND, SMC935_PPMODE);
					outb(SMC935_DAT, SMC935_ECPEPP17);
				}
			} else {
				outb(SMC935_IND, SMC935_PPMODE);
				outb(SMC935_DAT, SMC935_ECP);
			}
		}
	}

	outb(SMC935_CFG, 0xaa); /* exit config mode */

	ppc->ppc_type = PPC_TYPE_SMCLIKE;
	ppc_smclike_setmode(ppc, chipset_mode);

	return (chipset_mode);
}

/*
 * Winbond W83877F stuff
 *
 * EFER: extended function enable register
 * EFIR: extended function index register
 * EFDR: extended function data register
 */
#define efir ((efer == 0x250) ? 0x251 : 0x3f0)
#define efdr ((efer == 0x250) ? 0x252 : 0x3f1)

static int w83877f_efers[] = { 0x250, 0x3f0, 0x3f0, 0x250 };
static int w83877f_keys[] = { 0x89, 0x86, 0x87, 0x88 };
static int w83877f_keyiter[] = { 1, 2, 2, 1 };
static int w83877f_hefs[] = { WINB_HEFERE, WINB_HEFRAS, WINB_HEFERE | WINB_HEFRAS, 0 };

static int
ppc_w83877f_detect(struct ppc_data *ppc, int chipset_mode)
{
	int i, j, efer;
	unsigned char r, hefere, hefras;

	for (i = 0; i < 4; i ++) {
		/* first try to enable configuration registers */
		efer = w83877f_efers[i];

		/* write the key to the EFER */
		for (j = 0; j < w83877f_keyiter[i]; j ++)
			outb (efer, w83877f_keys[i]);

		/* then check HEFERE and HEFRAS bits */
		outb (efir, 0x0c);
		hefere = inb(efdr) & WINB_HEFERE;

		outb (efir, 0x16);
		hefras = inb(efdr) & WINB_HEFRAS;

		/*
		 * HEFRAS	HEFERE
		 *   0		   1	write 89h to 250h (power-on default)
		 *   1		   0	write 86h twice to 3f0h
		 *   1		   1	write 87h twice to 3f0h
		 *   0		   0	write 88h to 250h
		 */
		if ((hefere | hefras) == w83877f_hefs[i])
			goto found;
	}

	return (-1);	/* failed */

found:
	/* check base port address - read from CR23 */
	outb(efir, 0x23);
	if (ppc->ppc_base != inb(efdr) * 4)		/* 4 bytes boundaries */
		return (-1);

	/* read CHIP ID from CR9/bits0-3 */
	outb(efir, 0x9);

	switch (inb(efdr) & WINB_CHIPID) {
		case WINB_W83877F_ID:
			ppc->ppc_model = WINB_W83877F;
			break;

		case WINB_W83877AF_ID:
			ppc->ppc_model = WINB_W83877AF;
			break;

		default:
			ppc->ppc_model = WINB_UNKNOWN;
	}

	if (bootverbose) {
		/* dump of registers */
		device_printf(ppc->ppc_dev, "0x%x - ", w83877f_keys[i]);
		for (i = 0; i <= 0xd; i ++) {
			outb(efir, i);
			printf("0x%x ", inb(efdr));
		}
		for (i = 0x10; i <= 0x17; i ++) {
			outb(efir, i);
			printf("0x%x ", inb(efdr));
		}
		outb(efir, 0x1e);
		printf("0x%x ", inb(efdr));
		for (i = 0x20; i <= 0x29; i ++) {
			outb(efir, i);
			printf("0x%x ", inb(efdr));
		}
		printf("\n");
	}

	ppc->ppc_type = PPC_TYPE_GENERIC;

	if (!chipset_mode) {
		/* autodetect mode */

		/* select CR0 */
		outb(efir, 0x0);
		r = inb(efdr) & (WINB_PRTMODS0 | WINB_PRTMODS1);

		/* select CR9 */
		outb(efir, 0x9);
		r |= (inb(efdr) & WINB_PRTMODS2);

		switch (r) {
		case WINB_W83757:
			if (bootverbose)
				device_printf(ppc->ppc_dev,
				    "W83757 compatible mode\n");
			return (-1);	/* generic or SMC-like */

		case WINB_EXTFDC:
		case WINB_EXTADP:
		case WINB_EXT2FDD:
		case WINB_JOYSTICK:
			if (bootverbose)
				device_printf(ppc->ppc_dev,
				    "not in parallel port mode\n");
			return (-1);

		case (WINB_PARALLEL | WINB_EPP_SPP):
			ppc->ppc_avm |= PPB_EPP | PPB_SPP;
			if (bootverbose)
				device_printf(ppc->ppc_dev, "EPP SPP\n");
			break;

		case (WINB_PARALLEL | WINB_ECP):
			ppc->ppc_avm |= PPB_ECP | PPB_SPP;
			if (bootverbose)
				device_printf(ppc->ppc_dev, "ECP SPP\n");
			break;

		case (WINB_PARALLEL | WINB_ECP_EPP):
			ppc->ppc_avm |= PPB_ECP | PPB_EPP | PPB_SPP;
			ppc->ppc_type = PPC_TYPE_SMCLIKE;

			if (bootverbose)
				device_printf(ppc->ppc_dev, "ECP+EPP SPP\n");
			break;
		default:
			printf("%s: unknown case (0x%x)!\n", __func__, r);
		}

	} else {
		/* mode forced */

		/* select CR9 and set PRTMODS2 bit */
		outb(efir, 0x9);
		outb(efdr, inb(efdr) & ~WINB_PRTMODS2);

		/* select CR0 and reset PRTMODSx bits */
		outb(efir, 0x0);
		outb(efdr, inb(efdr) & ~(WINB_PRTMODS0 | WINB_PRTMODS1));

		if (chipset_mode & PPB_ECP) {
			if (chipset_mode & PPB_EPP) {
				outb(efdr, inb(efdr) | WINB_ECP_EPP);
				if (bootverbose)
					device_printf(ppc->ppc_dev,
					    "ECP+EPP\n");

				ppc->ppc_type = PPC_TYPE_SMCLIKE;

			} else {
				outb(efdr, inb(efdr) | WINB_ECP);
				if (bootverbose)
					device_printf(ppc->ppc_dev, "ECP\n");
			}
		} else {
			/* select EPP_SPP otherwise */
			outb(efdr, inb(efdr) | WINB_EPP_SPP);
			if (bootverbose)
				device_printf(ppc->ppc_dev, "EPP SPP\n");
		}
		ppc->ppc_avm = chipset_mode;
	}

	/* exit configuration mode */
	outb(efer, 0xaa);

	switch (ppc->ppc_type) {
	case PPC_TYPE_SMCLIKE:
		ppc_smclike_setmode(ppc, chipset_mode);
		break;
	default:
		ppc_generic_setmode(ppc, chipset_mode);
		break;
	}

	return (chipset_mode);
}
#endif

/*
 * ppc_generic_detect
 */
static int
ppc_generic_detect(struct ppc_data *ppc, int chipset_mode)
{
	/* default to generic */
	ppc->ppc_type = PPC_TYPE_GENERIC;

	if (bootverbose)
		device_printf(ppc->ppc_dev, "SPP");

	/* first, check for ECP */
	w_ecr(ppc, PPC_ECR_PS2);
	if ((r_ecr(ppc) & 0xe0) == PPC_ECR_PS2) {
		ppc->ppc_dtm |= PPB_ECP | PPB_SPP;
		if (bootverbose)
			printf(" ECP ");

		/* search for SMC style ECP+EPP mode */
		w_ecr(ppc, PPC_ECR_EPP);
	}

	/* try to reset EPP timeout bit */
	if (ppc_check_epp_timeout(ppc)) {
		ppc->ppc_dtm |= PPB_EPP;

		if (ppc->ppc_dtm & PPB_ECP) {
			/* SMC like chipset found */
			ppc->ppc_model = SMC_LIKE;
			ppc->ppc_type = PPC_TYPE_SMCLIKE;

			if (bootverbose)
				printf(" ECP+EPP");
		} else {
			if (bootverbose)
				printf(" EPP");
		}
	} else {
		/* restore to standard mode */
		w_ecr(ppc, PPC_ECR_STD);
	}

	/* XXX try to detect NIBBLE and PS2 modes */
	ppc->ppc_dtm |= PPB_NIBBLE;

	if (chipset_mode)
		ppc->ppc_avm = chipset_mode;
	else
		ppc->ppc_avm = ppc->ppc_dtm;

	if (bootverbose)
		printf("\n");

	switch (ppc->ppc_type) {
	case PPC_TYPE_SMCLIKE:
		ppc_smclike_setmode(ppc, chipset_mode);
		break;
	default:
		ppc_generic_setmode(ppc, chipset_mode);
		break;
	}

	return (chipset_mode);
}

/*
 * ppc_detect()
 *
 * mode is the mode suggested at boot
 */
static int
ppc_detect(struct ppc_data *ppc, int chipset_mode) {

#ifdef PPC_PROBE_CHIPSET
	int i, mode;

	/* list of supported chipsets */
	int (*chipset_detect[])(struct ppc_data *, int) = {
		ppc_pc873xx_detect,
		ppc_smc37c66xgt_detect,
		ppc_w83877f_detect,
		ppc_smc37c935_detect,
		ppc_generic_detect,
		NULL
	};
#endif

	/* if can't find the port and mode not forced return error */
	if (!ppc_detect_port(ppc) && chipset_mode == 0)
		return (EIO);			/* failed, port not present */

	/* assume centronics compatible mode is supported */
	ppc->ppc_avm = PPB_COMPATIBLE;

#ifdef PPC_PROBE_CHIPSET
	/* we have to differenciate available chipset modes,
	 * chipset running modes and IEEE-1284 operating modes
	 *
	 * after detection, the port must support running in compatible mode
	 */
	if (ppc->ppc_flags & 0x40) {
		if (bootverbose)
			printf("ppc: chipset forced to generic\n");
#endif

		ppc->ppc_mode = ppc_generic_detect(ppc, chipset_mode);

#ifdef PPC_PROBE_CHIPSET
	} else {
		for (i=0; chipset_detect[i] != NULL; i++) {
			if ((mode = chipset_detect[i](ppc, chipset_mode)) != -1) {
				ppc->ppc_mode = mode;
				break;
			}
		}
	}
#endif

	/* configure/detect ECP FIFO */
	if ((ppc->ppc_avm & PPB_ECP) && !(ppc->ppc_flags & 0x80))
		ppc_detect_fifo(ppc);

	return (0);
}

/*
 * ppc_exec_microseq()
 *
 * Execute a microsequence.
 * Microsequence mechanism is supposed to handle fast I/O operations.
 */
int
ppc_exec_microseq(device_t dev, struct ppb_microseq **p_msq)
{
	struct ppc_data *ppc = DEVTOSOFTC(dev);
	struct ppb_microseq *mi;
	char cc, *p;
	int i, iter, len;
	int error;

	int reg;
	char mask;
	int accum = 0;
	char *ptr = NULL;

	struct ppb_microseq *stack = NULL;

/* microsequence registers are equivalent to PC-like port registers */

#define r_reg(reg,ppc) (bus_read_1((ppc)->res_ioport, reg))
#define w_reg(reg, ppc, byte) (bus_write_1((ppc)->res_ioport, reg, byte))

#define INCR_PC (mi ++)		/* increment program counter */

	PPC_ASSERT_LOCKED(ppc);
	mi = *p_msq;
	for (;;) {
		switch (mi->opcode) {
		case MS_OP_RSET:
			cc = r_reg(mi->arg[0].i, ppc);
			cc &= (char)mi->arg[2].i;	/* clear mask */
			cc |= (char)mi->arg[1].i;	/* assert mask */
			w_reg(mi->arg[0].i, ppc, cc);
			INCR_PC;
			break;

		case MS_OP_RASSERT_P:
			reg = mi->arg[1].i;
			ptr = ppc->ppc_ptr;

			if ((len = mi->arg[0].i) == MS_ACCUM) {
				accum = ppc->ppc_accum;
				for (; accum; accum--)
					w_reg(reg, ppc, *ptr++);
				ppc->ppc_accum = accum;
			} else
				for (i=0; i<len; i++)
					w_reg(reg, ppc, *ptr++);
			ppc->ppc_ptr = ptr;

			INCR_PC;
			break;

		case MS_OP_RFETCH_P:
			reg = mi->arg[1].i;
			mask = (char)mi->arg[2].i;
			ptr = ppc->ppc_ptr;

			if ((len = mi->arg[0].i) == MS_ACCUM) {
				accum = ppc->ppc_accum;
				for (; accum; accum--)
					*ptr++ = r_reg(reg, ppc) & mask;
				ppc->ppc_accum = accum;
			} else
				for (i=0; i<len; i++)
					*ptr++ = r_reg(reg, ppc) & mask;
			ppc->ppc_ptr = ptr;

			INCR_PC;
			break;

		case MS_OP_RFETCH:
			*((char *) mi->arg[2].p) = r_reg(mi->arg[0].i, ppc) &
							(char)mi->arg[1].i;
			INCR_PC;
			break;

		case MS_OP_RASSERT:
		case MS_OP_DELAY:

		/* let's suppose the next instr. is the same */
		prefetch:
			for (;mi->opcode == MS_OP_RASSERT; INCR_PC)
				w_reg(mi->arg[0].i, ppc, (char)mi->arg[1].i);

			if (mi->opcode == MS_OP_DELAY) {
				DELAY(mi->arg[0].i);
				INCR_PC;
				goto prefetch;
			}
			break;

		case MS_OP_ADELAY:
			if (mi->arg[0].i) {
				PPC_UNLOCK(ppc);
				pause("ppbdelay", mi->arg[0].i * (hz/1000));
				PPC_LOCK(ppc);
			}
			INCR_PC;
			break;

		case MS_OP_TRIG:
			reg = mi->arg[0].i;
			iter = mi->arg[1].i;
			p = (char *)mi->arg[2].p;

			/* XXX delay limited to 255 us */
			for (i=0; i<iter; i++) {
				w_reg(reg, ppc, *p++);
				DELAY((unsigned char)*p++);
			}
			INCR_PC;
			break;

		case MS_OP_SET:
			ppc->ppc_accum = mi->arg[0].i;
			INCR_PC;
			break;

		case MS_OP_DBRA:
			if (--ppc->ppc_accum > 0)
				mi += mi->arg[0].i;
			INCR_PC;
			break;

		case MS_OP_BRSET:
			cc = r_str(ppc);
			if ((cc & (char)mi->arg[0].i) == (char)mi->arg[0].i)
				mi += mi->arg[1].i;
			INCR_PC;
			break;

		case MS_OP_BRCLEAR:
			cc = r_str(ppc);
			if ((cc & (char)mi->arg[0].i) == 0)
				mi += mi->arg[1].i;
			INCR_PC;
			break;

		case MS_OP_BRSTAT:
			cc = r_str(ppc);
			if ((cc & ((char)mi->arg[0].i | (char)mi->arg[1].i)) ==
							(char)mi->arg[0].i)
				mi += mi->arg[2].i;
			INCR_PC;
			break;

		case MS_OP_C_CALL:
			/*
			 * If the C call returns !0 then end the microseq.
			 * The current state of ptr is passed to the C function
			 */
			if ((error = mi->arg[0].f(mi->arg[1].p, ppc->ppc_ptr)))
				return (error);

			INCR_PC;
			break;

		case MS_OP_PTR:
			ppc->ppc_ptr = (char *)mi->arg[0].p;
			INCR_PC;
			break;

		case MS_OP_CALL:
			if (stack)
				panic("%s: too much calls", __func__);

			if (mi->arg[0].p) {
				/* store the state of the actual
				 * microsequence
				 */
				stack = mi;

				/* jump to the new microsequence */
				mi = (struct ppb_microseq *)mi->arg[0].p;
			} else
				INCR_PC;

			break;

		case MS_OP_SUBRET:
			/* retrieve microseq and pc state before the call */
			mi = stack;

			/* reset the stack */
			stack = NULL;

			/* XXX return code */

			INCR_PC;
			break;

		case MS_OP_PUT:
		case MS_OP_GET:
		case MS_OP_RET:
			/* can't return to ppb level during the execution
			 * of a submicrosequence */
			if (stack)
				panic("%s: can't return to ppb level",
								__func__);

			/* update pc for ppb level of execution */
			*p_msq = mi;

			/* return to ppb level of execution */
			return (0);

		default:
			panic("%s: unknown microsequence opcode 0x%x",
			    __func__, mi->opcode);
		}
	}

	/* unreached */
}

static void
ppcintr(void *arg)
{
	struct ppc_data *ppc = arg;
	u_char ctr, ecr, str;

	/*
	 * If we have any child interrupt handlers registered, let
	 * them handle this interrupt.
	 *
	 * XXX: If DMA is in progress should we just complete that w/o
	 * doing this?
	 */
	PPC_LOCK(ppc);
	if (ppc->ppc_intr_hook != NULL &&
	    ppc->ppc_intr_hook(ppc->ppc_intr_arg) == 0) {
		PPC_UNLOCK(ppc);
		return;
	}

	str = r_str(ppc);
	ctr = r_ctr(ppc);
	ecr = r_ecr(ppc);

#if defined(PPC_DEBUG) && PPC_DEBUG > 1
		printf("![%x/%x/%x]", ctr, ecr, str);
#endif

	/* don't use ecp mode with IRQENABLE set */
	if (ctr & IRQENABLE) {
		PPC_UNLOCK(ppc);
		return;
	}

	/* interrupts are generated by nFault signal
	 * only in ECP mode */
	if ((str & nFAULT) && (ppc->ppc_mode & PPB_ECP)) {
		/* check if ppc driver has programmed the
		 * nFault interrupt */
		if  (ppc->ppc_irqstat & PPC_IRQ_nFAULT) {

			w_ecr(ppc, ecr | PPC_nFAULT_INTR);
			ppc->ppc_irqstat &= ~PPC_IRQ_nFAULT;
		} else {
			/* shall be handled by underlying layers XXX */
			PPC_UNLOCK(ppc);
			return;
		}
	}

	if (ppc->ppc_irqstat & PPC_IRQ_DMA) {
		/* disable interrupts (should be done by hardware though) */
		w_ecr(ppc, ecr | PPC_SERVICE_INTR);
		ppc->ppc_irqstat &= ~PPC_IRQ_DMA;
		ecr = r_ecr(ppc);

		/* check if DMA completed */
		if ((ppc->ppc_avm & PPB_ECP) && (ecr & PPC_ENABLE_DMA)) {
#ifdef PPC_DEBUG
			printf("a");
#endif
			/* stop DMA */
			w_ecr(ppc, ecr & ~PPC_ENABLE_DMA);
			ecr = r_ecr(ppc);

			if (ppc->ppc_dmastat == PPC_DMA_STARTED) {
#ifdef PPC_DEBUG
				printf("d");
#endif
				ppc->ppc_dmadone(ppc);
				ppc->ppc_dmastat = PPC_DMA_COMPLETE;

				/* wakeup the waiting process */
				wakeup(ppc);
			}
		}
	} else if (ppc->ppc_irqstat & PPC_IRQ_FIFO) {

		/* classic interrupt I/O */
		ppc->ppc_irqstat &= ~PPC_IRQ_FIFO;
	}
	PPC_UNLOCK(ppc);

	return;
}

int
ppc_read(device_t dev, char *buf, int len, int mode)
{
	return (EINVAL);
}

int
ppc_write(device_t dev, char *buf, int len, int how)
{
	return (EINVAL);
}

int
ppc_reset_epp(device_t dev)
{
	struct ppc_data *ppc = DEVTOSOFTC(dev);

	PPC_ASSERT_LOCKED(ppc);
	ppc_reset_epp_timeout(ppc);

	return 0;
}

int
ppc_setmode(device_t dev, int mode)
{
	struct ppc_data *ppc = DEVTOSOFTC(dev);

	PPC_ASSERT_LOCKED(ppc);
	switch (ppc->ppc_type) {
	case PPC_TYPE_SMCLIKE:
		return (ppc_smclike_setmode(ppc, mode));
		break;

	case PPC_TYPE_GENERIC:
	default:
		return (ppc_generic_setmode(ppc, mode));
		break;
	}

	/* not reached */
	return (ENXIO);
}

int
ppc_probe(device_t dev, int rid)
{
#ifdef __i386__
	static short next_bios_ppc = 0;
#endif
	struct ppc_data *ppc;
	int error;
	rman_res_t port;

	/*
	 * Allocate the ppc_data structure.
	 */
	ppc = DEVTOSOFTC(dev);
	bzero(ppc, sizeof(struct ppc_data));

	ppc->rid_ioport = rid;

	/* retrieve ISA parameters */
	error = bus_get_resource(dev, SYS_RES_IOPORT, rid, &port, NULL);

#ifdef __i386__
	/*
	 * If port not specified, use bios list.
	 */
	if (error) {
		if ((next_bios_ppc < BIOS_MAX_PPC) &&
		    (*(BIOS_PORTS + next_bios_ppc) != 0)) {
			port = *(BIOS_PORTS + next_bios_ppc++);
			if (bootverbose)
				device_printf(dev,
				    "parallel port found at 0x%jx\n", port);
		} else {
			device_printf(dev, "parallel port not found.\n");
			return (ENXIO);
		}
		bus_set_resource(dev, SYS_RES_IOPORT, rid, port,
				 IO_LPTSIZE_EXTENDED);
	}
#endif

	/* IO port is mandatory */

	/* Try "extended" IO port range...*/
	ppc->res_ioport = bus_alloc_resource_anywhere(dev, SYS_RES_IOPORT,
						      &ppc->rid_ioport,
						      IO_LPTSIZE_EXTENDED,
						      RF_ACTIVE);

	if (ppc->res_ioport != 0) {
		if (bootverbose)
			device_printf(dev, "using extended I/O port range\n");
	} else {
		/* Failed? If so, then try the "normal" IO port range... */
		 ppc->res_ioport = bus_alloc_resource_anywhere(dev,
		 	 				       SYS_RES_IOPORT,
							       &ppc->rid_ioport,
							       IO_LPTSIZE_NORMAL,
							       RF_ACTIVE);
		if (ppc->res_ioport != 0) {
			if (bootverbose)
				device_printf(dev, "using normal I/O port range\n");
		} else {
			if (bootverbose)
				device_printf(dev, "cannot reserve I/O port range\n");
			goto error;
		}
	}

 	ppc->ppc_base = rman_get_start(ppc->res_ioport);

	ppc->ppc_flags = device_get_flags(dev);

	if (!(ppc->ppc_flags & 0x20)) {
		ppc->res_irq = bus_alloc_resource_any(dev, SYS_RES_IRQ,
						      &ppc->rid_irq,
						      RF_SHAREABLE);
		ppc->res_drq = bus_alloc_resource_any(dev, SYS_RES_DRQ,
						      &ppc->rid_drq,
						      RF_ACTIVE);
	}

	if (ppc->res_irq)
		ppc->ppc_irq = rman_get_start(ppc->res_irq);
	if (ppc->res_drq)
		ppc->ppc_dmachan = rman_get_start(ppc->res_drq);

	ppc->ppc_dev = dev;
	ppc->ppc_model = GENERIC;

	ppc->ppc_mode = PPB_COMPATIBLE;
	ppc->ppc_epp = (ppc->ppc_flags & 0x10) >> 4;

	ppc->ppc_type = PPC_TYPE_GENERIC;

	/*
	 * Try to detect the chipset and its mode.
	 */
	if (ppc_detect(ppc, ppc->ppc_flags & 0xf))
		goto error;

	return (0);

error:
	if (ppc->res_irq != 0) {
		bus_release_resource(dev, SYS_RES_IRQ, ppc->rid_irq,
				     ppc->res_irq);
	}
	if (ppc->res_ioport != 0) {
		bus_release_resource(dev, SYS_RES_IOPORT, ppc->rid_ioport,
				     ppc->res_ioport);
	}
	if (ppc->res_drq != 0) {
		bus_release_resource(dev, SYS_RES_DRQ, ppc->rid_drq,
				     ppc->res_drq);
	}
	return (ENXIO);
}

int
ppc_attach(device_t dev)
{
	struct ppc_data *ppc = DEVTOSOFTC(dev);
	int error;

	mtx_init(&ppc->ppc_lock, device_get_nameunit(dev), "ppc", MTX_DEF);

	device_printf(dev, "%s chipset (%s) in %s mode%s\n",
		      ppc_models[ppc->ppc_model], ppc_avms[ppc->ppc_avm],
		      ppc_modes[ppc->ppc_mode], (PPB_IS_EPP(ppc->ppc_mode)) ?
		      ppc_epp_protocol[ppc->ppc_epp] : "");

	if (ppc->ppc_fifo)
		device_printf(dev, "FIFO with %d/%d/%d bytes threshold\n",
			      ppc->ppc_fifo, ppc->ppc_wthr, ppc->ppc_rthr);

	if (ppc->res_irq) {
		/* default to the tty mask for registration */	/* XXX */
		error = bus_setup_intr(dev, ppc->res_irq, INTR_TYPE_TTY |
		    INTR_MPSAFE, NULL, ppcintr, ppc, &ppc->intr_cookie);
		if (error) {
			device_printf(dev,
			    "failed to register interrupt handler: %d\n",
			    error);
			mtx_destroy(&ppc->ppc_lock);
			return (error);
		}
	}

	/* add ppbus as a child of this isa to parallel bridge */
	ppc->ppbus = device_add_child(dev, "ppbus", -1);

	/*
	 * Probe the ppbus and attach devices found.
	 */
	device_probe_and_attach(ppc->ppbus);

	return (0);
}

int
ppc_detach(device_t dev)
{
	struct ppc_data *ppc = DEVTOSOFTC(dev);

	if (ppc->res_irq == 0) {
		return (ENXIO);
	}

	/* detach & delete all children */
	device_delete_children(dev);

	if (ppc->res_irq != 0) {
		bus_teardown_intr(dev, ppc->res_irq, ppc->intr_cookie);
		bus_release_resource(dev, SYS_RES_IRQ, ppc->rid_irq,
				     ppc->res_irq);
	}
	if (ppc->res_ioport != 0) {
		bus_release_resource(dev, SYS_RES_IOPORT, ppc->rid_ioport,
				     ppc->res_ioport);
	}
	if (ppc->res_drq != 0) {
		bus_release_resource(dev, SYS_RES_DRQ, ppc->rid_drq,
				     ppc->res_drq);
	}

	mtx_destroy(&ppc->ppc_lock);

	return (0);
}

u_char
ppc_io(device_t ppcdev, int iop, u_char *addr, int cnt, u_char byte)
{
	struct ppc_data *ppc = DEVTOSOFTC(ppcdev);

	PPC_ASSERT_LOCKED(ppc);
	switch (iop) {
	case PPB_OUTSB_EPP:
	    bus_write_multi_1(ppc->res_ioport, PPC_EPP_DATA, addr, cnt);
		break;
	case PPB_OUTSW_EPP:
	    bus_write_multi_2(ppc->res_ioport, PPC_EPP_DATA, (u_int16_t *)addr, cnt);
		break;
	case PPB_OUTSL_EPP:
	    bus_write_multi_4(ppc->res_ioport, PPC_EPP_DATA, (u_int32_t *)addr, cnt);
		break;
	case PPB_INSB_EPP:
	    bus_read_multi_1(ppc->res_ioport, PPC_EPP_DATA, addr, cnt);
		break;
	case PPB_INSW_EPP:
	    bus_read_multi_2(ppc->res_ioport, PPC_EPP_DATA, (u_int16_t *)addr, cnt);
		break;
	case PPB_INSL_EPP:
	    bus_read_multi_4(ppc->res_ioport, PPC_EPP_DATA, (u_int32_t *)addr, cnt);
		break;
	case PPB_RDTR:
		return (r_dtr(ppc));
	case PPB_RSTR:
		return (r_str(ppc));
	case PPB_RCTR:
		return (r_ctr(ppc));
	case PPB_REPP_A:
		return (r_epp_A(ppc));
	case PPB_REPP_D:
		return (r_epp_D(ppc));
	case PPB_RECR:
		return (r_ecr(ppc));
	case PPB_RFIFO:
		return (r_fifo(ppc));
	case PPB_WDTR:
		w_dtr(ppc, byte);
		break;
	case PPB_WSTR:
		w_str(ppc, byte);
		break;
	case PPB_WCTR:
		w_ctr(ppc, byte);
		break;
	case PPB_WEPP_A:
		w_epp_A(ppc, byte);
		break;
	case PPB_WEPP_D:
		w_epp_D(ppc, byte);
		break;
	case PPB_WECR:
		w_ecr(ppc, byte);
		break;
	case PPB_WFIFO:
		w_fifo(ppc, byte);
		break;
	default:
		panic("%s: unknown I/O operation", __func__);
		break;
	}

	return (0);	/* not significative */
}

int
ppc_read_ivar(device_t bus, device_t dev, int index, uintptr_t *val)
{
	struct ppc_data *ppc = (struct ppc_data *)device_get_softc(bus);

	switch (index) {
	case PPC_IVAR_EPP_PROTO:
		PPC_ASSERT_LOCKED(ppc);
		*val = (u_long)ppc->ppc_epp;
		break;
	case PPC_IVAR_LOCK:
		*val = (uintptr_t)&ppc->ppc_lock;
		break;
	default:
		return (ENOENT);
	}

	return (0);
}

int
ppc_write_ivar(device_t bus, device_t dev, int index, uintptr_t val)
{
	struct ppc_data *ppc = (struct ppc_data *)device_get_softc(bus);

	switch (index) {
	case PPC_IVAR_INTR_HANDLER:
		PPC_ASSERT_LOCKED(ppc);
		if (dev != ppc->ppbus)
			return (EINVAL);
		if (val == 0) {
			ppc->ppc_intr_hook = NULL;
			break;
		}
		if (ppc->ppc_intr_hook != NULL)
			return (EBUSY);
		ppc->ppc_intr_hook = (void *)val;
		ppc->ppc_intr_arg = device_get_softc(dev);
		break;
	default:
		return (ENOENT);
	}

	return (0);
}

/*
 * We allow child devices to allocate an IRQ resource at rid 0 for their
 * interrupt handlers.
 */
struct resource *
ppc_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct ppc_data *ppc = DEVTOSOFTC(bus);

	switch (type) {
	case SYS_RES_IRQ:
		if (*rid == 0)
			return (ppc->res_irq);
		break;
	}
	return (NULL);
}

int
ppc_release_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
#ifdef INVARIANTS
	struct ppc_data *ppc = DEVTOSOFTC(bus);
#endif

	switch (type) {
	case SYS_RES_IRQ:
		if (rid == 0) {
			KASSERT(r == ppc->res_irq,
			    ("ppc child IRQ resource mismatch"));
			return (0);
		}
		break;
	}
	return (EINVAL);
}

MODULE_DEPEND(ppc, ppbus, 1, 1, 1);
