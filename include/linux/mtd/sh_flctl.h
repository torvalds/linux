/*
 * SuperH FLCTL nand controller
 *
 * Copyright © 2008 Renesas Solutions Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __SH_FLCTL_H__
#define __SH_FLCTL_H__

#include <linux/completion.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/pm_qos.h>

/* FLCTL registers */
#define FLCMNCR(f)		(f->reg + 0x0)
#define FLCMDCR(f)		(f->reg + 0x4)
#define FLCMCDR(f)		(f->reg + 0x8)
#define FLADR(f)		(f->reg + 0xC)
#define FLADR2(f)		(f->reg + 0x3C)
#define FLDATAR(f)		(f->reg + 0x10)
#define FLDTCNTR(f)		(f->reg + 0x14)
#define FLINTDMACR(f)		(f->reg + 0x18)
#define FLBSYTMR(f)		(f->reg + 0x1C)
#define FLBSYCNT(f)		(f->reg + 0x20)
#define FLDTFIFO(f)		(f->reg + 0x24)
#define FLECFIFO(f)		(f->reg + 0x28)
#define FLTRCR(f)		(f->reg + 0x2C)
#define FLHOLDCR(f)		(f->reg + 0x38)
#define	FL4ECCRESULT0(f)	(f->reg + 0x80)
#define	FL4ECCRESULT1(f)	(f->reg + 0x84)
#define	FL4ECCRESULT2(f)	(f->reg + 0x88)
#define	FL4ECCRESULT3(f)	(f->reg + 0x8C)
#define	FL4ECCCR(f)		(f->reg + 0x90)
#define	FL4ECCCNT(f)		(f->reg + 0x94)
#define	FLERRADR(f)		(f->reg + 0x98)

/* FLCMNCR control bits */
#define _4ECCCNTEN	(0x1 << 24)
#define _4ECCEN		(0x1 << 23)
#define _4ECCCORRECT	(0x1 << 22)
#define SHBUSSEL	(0x1 << 20)
#define SEL_16BIT	(0x1 << 19)
#define SNAND_E		(0x1 << 18)	/* SNAND (0=512 1=2048)*/
#define QTSEL_E		(0x1 << 17)
#define ENDIAN		(0x1 << 16)	/* 1 = little endian */
#define FCKSEL_E	(0x1 << 15)
#define ACM_SACCES_MODE	(0x01 << 10)
#define NANWF_E		(0x1 << 9)
#define SE_D		(0x1 << 8)	/* Spare area disable */
#define	CE1_ENABLE	(0x1 << 4)	/* Chip Enable 1 */
#define	CE0_ENABLE	(0x1 << 3)	/* Chip Enable 0 */
#define	TYPESEL_SET	(0x1 << 0)

/*
 * Clock settings using the PULSEx registers from FLCMNCR
 *
 * Some hardware uses bits called PULSEx instead of FCKSEL_E and QTSEL_E
 * to control the clock divider used between the High-Speed Peripheral Clock
 * and the FLCTL internal clock. If so, use CLK_8_BIT_xxx for connecting 8 bit
 * and CLK_16_BIT_xxx for connecting 16 bit bus bandwith NAND chips. For the 16
 * bit version the divider is seperate for the pulse width of high and low
 * signals.
 */
#define PULSE3	(0x1 << 27)
#define PULSE2	(0x1 << 17)
#define PULSE1	(0x1 << 15)
#define PULSE0	(0x1 << 9)
#define CLK_8B_0_5			PULSE1
#define CLK_8B_1			0x0
#define CLK_8B_1_5			(PULSE1 | PULSE2)
#define CLK_8B_2			PULSE0
#define CLK_8B_3			(PULSE0 | PULSE1 | PULSE2)
#define CLK_8B_4			(PULSE0 | PULSE2)
#define CLK_16B_6L_2H			PULSE0
#define CLK_16B_9L_3H			(PULSE0 | PULSE1 | PULSE2)
#define CLK_16B_12L_4H			(PULSE0 | PULSE2)

/* FLCMDCR control bits */
#define ADRCNT2_E	(0x1 << 31)	/* 5byte address enable */
#define ADRMD_E		(0x1 << 26)	/* Sector address access */
#define CDSRC_E		(0x1 << 25)	/* Data buffer selection */
#define DOSR_E		(0x1 << 24)	/* Status read check */
#define SELRW		(0x1 << 21)	/*  0:read 1:write */
#define DOADR_E		(0x1 << 20)	/* Address stage execute */
#define ADRCNT_1	(0x00 << 18)	/* Address data bytes: 1byte */
#define ADRCNT_2	(0x01 << 18)	/* Address data bytes: 2byte */
#define ADRCNT_3	(0x02 << 18)	/* Address data bytes: 3byte */
#define ADRCNT_4	(0x03 << 18)	/* Address data bytes: 4byte */
#define DOCMD2_E	(0x1 << 17)	/* 2nd cmd stage execute */
#define DOCMD1_E	(0x1 << 16)	/* 1st cmd stage execute */

/* FLINTDMACR control bits */
#define ESTERINTE	(0x1 << 24)	/* ECC error interrupt enable */
#define AC1CLR		(0x1 << 19)	/* ECC FIFO clear */
#define AC0CLR		(0x1 << 18)	/* Data FIFO clear */
#define DREQ0EN		(0x1 << 16)	/* FLDTFIFODMA Request Enable */
#define ECERB		(0x1 << 9)	/* ECC error */
#define STERB		(0x1 << 8)	/* Status error */
#define STERINTE	(0x1 << 4)	/* Status error enable */

/* FLTRCR control bits */
#define TRSTRT		(0x1 << 0)	/* translation start */
#define TREND		(0x1 << 1)	/* translation end */

/*
 * FLHOLDCR control bits
 *
 * HOLDEN: Bus Occupancy Enable (inverted)
 * Enable this bit when the external bus might be used in between transfers.
 * If not set and the bus gets used by other modules, a deadlock occurs.
 */
#define HOLDEN		(0x1 << 0)

/* FL4ECCCR control bits */
#define	_4ECCFA		(0x1 << 2)	/* 4 symbols correct fault */
#define	_4ECCEND	(0x1 << 1)	/* 4 symbols end */
#define	_4ECCEXST	(0x1 << 0)	/* 4 symbols exist */

#define LOOP_TIMEOUT_MAX	0x00010000

enum flctl_ecc_res_t {
	FL_SUCCESS,
	FL_REPAIRABLE,
	FL_ERROR,
	FL_TIMEOUT
};

struct dma_chan;

struct sh_flctl {
	struct nand_chip	chip;
	struct platform_device	*pdev;
	struct dev_pm_qos_request pm_qos;
	void __iomem		*reg;
	resource_size_t		fifo;

	uint8_t	done_buff[2048 + 64];	/* max size 2048 + 64 */
	int	read_bytes;
	unsigned int index;
	int	seqin_column;		/* column in SEQIN cmd */
	int	seqin_page_addr;	/* page_addr in SEQIN cmd */
	uint32_t seqin_read_cmd;		/* read cmd in SEQIN cmd */
	int	erase1_page_addr;	/* page_addr in ERASE1 cmd */
	uint32_t erase_ADRCNT;		/* bits of FLCMDCR in ERASE1 cmd */
	uint32_t rw_ADRCNT;	/* bits of FLCMDCR in READ WRITE cmd */
	uint32_t flcmncr_base;	/* base value of FLCMNCR */
	uint32_t flintdmacr_base;	/* irq enable bits */

	unsigned page_size:1;	/* NAND page size (0 = 512, 1 = 2048) */
	unsigned hwecc:1;	/* Hardware ECC (0 = disabled, 1 = enabled) */
	unsigned holden:1;	/* Hardware has FLHOLDCR and HOLDEN is set */
	unsigned qos_request:1;	/* QoS request to prevent deep power shutdown */

	/* DMA related objects */
	struct dma_chan		*chan_fifo0_rx;
	struct dma_chan		*chan_fifo0_tx;
	struct completion	dma_complete;
};

struct sh_flctl_platform_data {
	struct mtd_partition	*parts;
	int			nr_parts;
	unsigned long		flcmncr_val;

	unsigned has_hwecc:1;
	unsigned use_holden:1;

	unsigned int            slave_id_fifo0_tx;
	unsigned int            slave_id_fifo0_rx;
};

static inline struct sh_flctl *mtd_to_flctl(struct mtd_info *mtdinfo)
{
	return container_of(mtd_to_nand(mtdinfo), struct sh_flctl, chip);
}

#endif	/* __SH_FLCTL_H__ */
