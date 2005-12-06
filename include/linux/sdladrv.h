/*****************************************************************************
* sdladrv.h	SDLA Support Module.  Kernel API Definitions.
*
* Author: 	Gideon Hack	
*
* Copyright:	(c) 1995-2000 Sangoma Technologies Inc.
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* Jun 02, 1999 	Gideon Hack	Added support for the S514 PCI adapter.
* Dec 11, 1996	Gene Kozin	Complete overhaul.
* Oct 17, 1996	Gene Kozin	Minor bug fixes.
* Jun 12, 1996	Gene Kozin 	Added support for S503 card.
* Dec 06, 1995	Gene Kozin	Initial version.
*****************************************************************************/
#ifndef	_SDLADRV_H
#define	_SDLADRV_H


#define	SDLA_MAXIORANGE	4	/* maximum I/O port range */
#define	SDLA_WINDOWSIZE	0x2000	/* default dual-port memory window size */
/****** Data Structures *****************************************************/

/*----------------------------------------------------------------------------
 * Adapter hardware configuration. Pointer to this structure is passed to all
 * APIs.
 */
typedef struct sdlahw
{
	unsigned type;			/* adapter type */
	unsigned fwid;			/* firmware ID */
	unsigned port;			/* adapter I/O port base */
	int irq;			/* interrupt request level */
	char S514_cpu_no[1];		/* PCI CPU Number */
	unsigned char S514_slot_no;	/* PCI Slot Number */
	char auto_pci_cfg;		/* Autodetect PCI Slot */
	struct pci_dev *pci_dev;	/* PCI device */
	void * dpmbase;			/* dual-port memory base */
	unsigned dpmsize;		/* dual-port memory size */
	unsigned pclk;			/* CPU clock rate, kHz */
	unsigned long memory;		/* memory size */
	unsigned long vector;		/* local offset of the DPM window */
	unsigned io_range;		/* I/O port range */
	unsigned char regs[SDLA_MAXIORANGE]; /* was written to registers */
	unsigned reserved[5];
} sdlahw_t;

/****** Function Prototypes *************************************************/

extern int sdla_setup	(sdlahw_t* hw, void* sfm, unsigned len);
extern int sdla_down	(sdlahw_t* hw);
extern void S514_intack  (sdlahw_t* hw, u32 int_status);
extern void read_S514_int_stat (sdlahw_t* hw, u32* int_status);
extern int sdla_mapmem	(sdlahw_t* hw, unsigned long addr);
extern int sdla_peek	(sdlahw_t* hw, unsigned long addr, void* buf,
			 unsigned len);
extern int sdla_poke	(sdlahw_t* hw, unsigned long addr, void* buf,
			 unsigned len);
extern int sdla_exec	(void* opflag);

extern unsigned wanpipe_hw_probe(void);

#endif	/* _SDLADRV_H */
