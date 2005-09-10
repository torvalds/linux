/*
* cycx_drv.h	CYCX Support Module.  Kernel API Definitions.
*
* Author:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
*
* Copyright:	(c) 1998-2003 Arnaldo Carvalho de Melo
*
* Based on sdladrv.h by Gene Kozin <genek@compuserve.com>
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* 1999/10/23	acme		cycxhw_t cleanup
* 1999/01/03	acme		more judicious use of data types...
*				uclong, ucchar, etc deleted, the u8, u16, u32
*				types are the portable way to go.
* 1999/01/03	acme		judicious use of data types... u16, u32, etc
* 1998/12/26	acme	 	FIXED_BUFFERS, CONF_OFFSET,
*                               removal of cy_read{bwl}
* 1998/08/08	acme	 	Initial version.
*/
#ifndef	_CYCX_DRV_H
#define	_CYCX_DRV_H

#define	CYCX_WINDOWSIZE	0x4000	/* default dual-port memory window size */
#define	GEN_CYCX_INTR	0x02
#define	RST_ENABLE	0x04
#define	START_CPU	0x06
#define	RST_DISABLE	0x08
#define	FIXED_BUFFERS	0x08
#define	TEST_PATTERN	0xaa55
#define	CMD_OFFSET	0x20
#define CONF_OFFSET     0x0380
#define	RESET_OFFSET	0x3c00	/* For reset file load */
#define	DATA_OFFSET	0x0100	/* For code and data files load */
#define	START_OFFSET	0x3ff0	/* 80186 starts here */

/**
 *	struct cycx_hw - Adapter hardware configuration
 *	@fwid - firmware ID
 *	@irq - interrupt request level
 *	@dpmbase - dual-port memory base
 *	@dpmsize - dual-port memory size
 *	@reserved - reserved for future use
 */
struct cycx_hw {
	u32 fwid;
	int irq;
	void __iomem *dpmbase;
	u32 dpmsize;
	u32 reserved[5];
};

/* Function Prototypes */
extern int cycx_setup(struct cycx_hw *hw, void *sfm, u32 len, unsigned long base);
extern int cycx_down(struct cycx_hw *hw);
extern int cycx_peek(struct cycx_hw *hw, u32 addr, void *buf, u32 len);
extern int cycx_poke(struct cycx_hw *hw, u32 addr, void *buf, u32 len);
extern int cycx_exec(void __iomem *addr);

extern void cycx_intr(struct cycx_hw *hw);
#endif	/* _CYCX_DRV_H */
