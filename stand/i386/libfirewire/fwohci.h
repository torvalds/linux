/*
 * Copyright (c) 2007 Hidetoshi Shimokawa
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

#define MAX_OHCI 5
#define CROMSIZE 0x400

struct fwohci_softc {
	uint32_t locator;
	uint32_t devid;
	uint32_t base_addr;
	uint32_t bus_id;
	uint32_t handle;
	int32_t state;
	struct crom_src_buf *crom_src_buf;
	struct crom_src *crom_src;
	struct crom_chunk *crom_root;
	struct fw_eui64 eui;
	int	speed;
	int	maxrec;
	uint32_t *config_rom;
	char config_rom_buf[CROMSIZE*2]; /* double size for alignment */
};

int fwohci_init(struct fwohci_softc *, int);
void fwohci_ibr(struct fwohci_softc *);
void fwohci_poll(struct fwohci_softc *);

#define FWOHCI_STATE_DEAD	(-1)
#define FWOHCI_STATE_INIT	0
#define FWOHCI_STATE_ENABLED	1
#define FWOHCI_STATE_BUSRESET	2
#define FWOHCI_STATE_NORMAL	3

#define OREAD(f, o) (*(volatile uint32_t *)((f)->handle + (o)))
#define OWRITE(f, o, v) (*(volatile uint32_t *)((f)->handle + (o)) = (v))

#define	OHCI_VERSION		0x00
#define	OHCI_ATRETRY		0x08
#define	OHCI_CROMHDR		0x18
#define OHCI_BUS_ID		0x1c
#define	OHCI_BUS_OPT		0x20
#define	OHCI_BUSIRMC		(1U << 31)
#define	OHCI_BUSCMC		(1 << 30)
#define	OHCI_BUSISC		(1 << 29)
#define	OHCI_BUSBMC		(1 << 28)
#define	OHCI_BUSPMC		(1 << 27)
#define OHCI_BUSFNC		OHCI_BUSIRMC | OHCI_BUSCMC | OHCI_BUSISC |\
				OHCI_BUSBMC | OHCI_BUSPMC

#define	OHCI_EUID_HI		0x24
#define	OHCI_EUID_LO		0x28

#define	OHCI_CROMPTR		0x34
#define	OHCI_HCCCTL		0x50
#define	OHCI_HCCCTLCLR		0x54
#define	OHCI_AREQHI		0x100
#define	OHCI_AREQHICLR		0x104
#define	OHCI_AREQLO		0x108
#define	OHCI_AREQLOCLR		0x10c
#define	OHCI_PREQHI		0x110
#define	OHCI_PREQHICLR		0x114
#define	OHCI_PREQLO		0x118
#define	OHCI_PREQLOCLR		0x11c
#define	OHCI_PREQUPPER		0x120

#define	OHCI_SID_BUF		0x64
#define	OHCI_SID_CNT		0x68
#define OHCI_SID_ERR		(1U << 31)
#define OHCI_SID_CNT_MASK	0xffc

#define	OHCI_IT_STAT		0x90
#define	OHCI_IT_STATCLR		0x94
#define	OHCI_IT_MASK		0x98
#define	OHCI_IT_MASKCLR		0x9c

#define	OHCI_IR_STAT		0xa0
#define	OHCI_IR_STATCLR		0xa4
#define	OHCI_IR_MASK		0xa8
#define	OHCI_IR_MASKCLR		0xac

#define	OHCI_LNKCTL		0xe0
#define	OHCI_LNKCTLCLR		0xe4

#define	OHCI_PHYACCESS		0xec
#define	OHCI_CYCLETIMER		0xf0

#define	OHCI_DMACTL(off)	(off)
#define	OHCI_DMACTLCLR(off)	(off + 4)
#define	OHCI_DMACMD(off)	(off + 0xc)
#define	OHCI_DMAMATCH(off)	(off + 0x10)

#define OHCI_ATQOFF		0x180
#define OHCI_ATQCTL		OHCI_ATQOFF
#define OHCI_ATQCTLCLR		(OHCI_ATQOFF + 4)
#define OHCI_ATQCMD		(OHCI_ATQOFF + 0xc)
#define OHCI_ATQMATCH		(OHCI_ATQOFF + 0x10)

#define OHCI_ATSOFF		0x1a0
#define OHCI_ATSCTL		OHCI_ATSOFF
#define OHCI_ATSCTLCLR		(OHCI_ATSOFF + 4)
#define OHCI_ATSCMD		(OHCI_ATSOFF + 0xc)
#define OHCI_ATSMATCH		(OHCI_ATSOFF + 0x10)

#define OHCI_ARQOFF		0x1c0
#define OHCI_ARQCTL		OHCI_ARQOFF
#define OHCI_ARQCTLCLR		(OHCI_ARQOFF + 4)
#define OHCI_ARQCMD		(OHCI_ARQOFF + 0xc)
#define OHCI_ARQMATCH		(OHCI_ARQOFF + 0x10)

#define OHCI_ARSOFF		0x1e0
#define OHCI_ARSCTL		OHCI_ARSOFF
#define OHCI_ARSCTLCLR		(OHCI_ARSOFF + 4)
#define OHCI_ARSCMD		(OHCI_ARSOFF + 0xc)
#define OHCI_ARSMATCH		(OHCI_ARSOFF + 0x10)

#define OHCI_ITOFF(CH)		(0x200 + 0x10 * (CH))
#define OHCI_ITCTL(CH)		(OHCI_ITOFF(CH))
#define OHCI_ITCTLCLR(CH)	(OHCI_ITOFF(CH) + 4)
#define OHCI_ITCMD(CH)		(OHCI_ITOFF(CH) + 0xc)

#define OHCI_IROFF(CH)		(0x400 + 0x20 * (CH))
#define OHCI_IRCTL(CH)		(OHCI_IROFF(CH))
#define OHCI_IRCTLCLR(CH)	(OHCI_IROFF(CH) + 4)
#define OHCI_IRCMD(CH)		(OHCI_IROFF(CH) + 0xc)
#define OHCI_IRMATCH(CH)	(OHCI_IROFF(CH) + 0x10)
