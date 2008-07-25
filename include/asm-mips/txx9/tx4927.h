/*
 * Author: MontaVista Software, Inc.
 *         source@mvista.com
 *
 * Copyright 2001-2006 MontaVista Software Inc.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 *  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 *  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 *  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 *  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 *  TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 *  USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef __ASM_TXX9_TX4927_H
#define __ASM_TXX9_TX4927_H

#include <linux/types.h>
#include <linux/io.h>
#include <asm/txx9irq.h>
#include <asm/txx9/tx4927pcic.h>

#ifdef CONFIG_64BIT
#define TX4927_REG_BASE	0xffffffffff1f0000UL
#else
#define TX4927_REG_BASE	0xff1f0000UL
#endif
#define TX4927_REG_SIZE	0x00010000

#define TX4927_SDRAMC_REG	(TX4927_REG_BASE + 0x8000)
#define TX4927_EBUSC_REG	(TX4927_REG_BASE + 0x9000)
#define TX4927_PCIC_REG		(TX4927_REG_BASE + 0xd000)
#define TX4927_CCFG_REG		(TX4927_REG_BASE + 0xe000)
#define TX4927_IRC_REG		(TX4927_REG_BASE + 0xf600)
#define TX4927_NR_TMR	3
#define TX4927_TMR_REG(ch)	(TX4927_REG_BASE + 0xf000 + (ch) * 0x100)
#define TX4927_NR_SIO	2
#define TX4927_SIO_REG(ch)	(TX4927_REG_BASE + 0xf300 + (ch) * 0x100)
#define TX4927_PIO_REG		(TX4927_REG_BASE + 0xf500)

#define TX4927_IR_INT(n)	(2 + (n))
#define TX4927_IR_SIO(n)	(8 + (n))
#define TX4927_IR_PCIC		16
#define TX4927_NUM_IR_TMR	3
#define TX4927_IR_TMR(n)	(17 + (n))
#define TX4927_IR_PCIERR	22
#define TX4927_NUM_IR	32

#define TX4927_IRC_INT	2	/* IP[2] in Status register */

#define TX4927_NUM_PIO	16

struct tx4927_sdramc_reg {
	u64 cr[4];
	u64 unused0[4];
	u64 tr;
	u64 unused1[2];
	u64 cmd;
};

struct tx4927_ebusc_reg {
	u64 cr[8];
};

struct tx4927_ccfg_reg {
	u64 ccfg;
	u64 crir;
	u64 pcfg;
	u64 toea;
	u64 clkctr;
	u64 unused0;
	u64 garbc;
	u64 unused1;
	u64 unused2;
	u64 ramp;
};

/*
 * CCFG
 */
/* CCFG : Chip Configuration */
#define TX4927_CCFG_WDRST	0x0000020000000000ULL
#define TX4927_CCFG_WDREXEN	0x0000010000000000ULL
#define TX4927_CCFG_BCFG_MASK	0x000000ff00000000ULL
#define TX4927_CCFG_TINTDIS	0x01000000
#define TX4927_CCFG_PCI66	0x00800000
#define TX4927_CCFG_PCIMODE	0x00400000
#define TX4927_CCFG_DIVMODE_MASK	0x000e0000
#define TX4927_CCFG_DIVMODE_8	(0x0 << 17)
#define TX4927_CCFG_DIVMODE_12	(0x1 << 17)
#define TX4927_CCFG_DIVMODE_16	(0x2 << 17)
#define TX4927_CCFG_DIVMODE_10	(0x3 << 17)
#define TX4927_CCFG_DIVMODE_2	(0x4 << 17)
#define TX4927_CCFG_DIVMODE_3	(0x5 << 17)
#define TX4927_CCFG_DIVMODE_4	(0x6 << 17)
#define TX4927_CCFG_DIVMODE_2_5	(0x7 << 17)
#define TX4927_CCFG_BEOW	0x00010000
#define TX4927_CCFG_WR	0x00008000
#define TX4927_CCFG_TOE	0x00004000
#define TX4927_CCFG_PCIARB	0x00002000
#define TX4927_CCFG_PCIDIVMODE_MASK	0x00001800
#define TX4927_CCFG_PCIDIVMODE_2_5	0x00000000
#define TX4927_CCFG_PCIDIVMODE_3	0x00000800
#define TX4927_CCFG_PCIDIVMODE_5	0x00001000
#define TX4927_CCFG_PCIDIVMODE_6	0x00001800
#define TX4927_CCFG_SYSSP_MASK	0x000000c0
#define TX4927_CCFG_ENDIAN	0x00000004
#define TX4927_CCFG_HALT	0x00000002
#define TX4927_CCFG_ACEHOLD	0x00000001
#define TX4927_CCFG_W1CBITS	(TX4927_CCFG_WDRST | TX4927_CCFG_BEOW)

/* PCFG : Pin Configuration */
#define TX4927_PCFG_SDCLKDLY_MASK	0x30000000
#define TX4927_PCFG_SDCLKDLY(d)	((d)<<28)
#define TX4927_PCFG_SYSCLKEN	0x08000000
#define TX4927_PCFG_SDCLKEN_ALL	0x07800000
#define TX4927_PCFG_SDCLKEN(ch)	(0x00800000<<(ch))
#define TX4927_PCFG_PCICLKEN_ALL	0x003f0000
#define TX4927_PCFG_PCICLKEN(ch)	(0x00010000<<(ch))
#define TX4927_PCFG_SEL2	0x00000200
#define TX4927_PCFG_SEL1	0x00000100
#define TX4927_PCFG_DMASEL_ALL	0x000000ff
#define TX4927_PCFG_DMASEL0_MASK	0x00000003
#define TX4927_PCFG_DMASEL1_MASK	0x0000000c
#define TX4927_PCFG_DMASEL2_MASK	0x00000030
#define TX4927_PCFG_DMASEL3_MASK	0x000000c0
#define TX4927_PCFG_DMASEL0_DRQ0	0x00000000
#define TX4927_PCFG_DMASEL0_SIO1	0x00000001
#define TX4927_PCFG_DMASEL0_ACL0	0x00000002
#define TX4927_PCFG_DMASEL0_ACL2	0x00000003
#define TX4927_PCFG_DMASEL1_DRQ1	0x00000000
#define TX4927_PCFG_DMASEL1_SIO1	0x00000004
#define TX4927_PCFG_DMASEL1_ACL1	0x00000008
#define TX4927_PCFG_DMASEL1_ACL3	0x0000000c
#define TX4927_PCFG_DMASEL2_DRQ2	0x00000000	/* SEL2=0 */
#define TX4927_PCFG_DMASEL2_SIO0	0x00000010	/* SEL2=0 */
#define TX4927_PCFG_DMASEL2_ACL1	0x00000000	/* SEL2=1 */
#define TX4927_PCFG_DMASEL2_ACL2	0x00000020	/* SEL2=1 */
#define TX4927_PCFG_DMASEL2_ACL0	0x00000030	/* SEL2=1 */
#define TX4927_PCFG_DMASEL3_DRQ3	0x00000000
#define TX4927_PCFG_DMASEL3_SIO0	0x00000040
#define TX4927_PCFG_DMASEL3_ACL3	0x00000080
#define TX4927_PCFG_DMASEL3_ACL1	0x000000c0

/* CLKCTR : Clock Control */
#define TX4927_CLKCTR_ACLCKD	0x02000000
#define TX4927_CLKCTR_PIOCKD	0x01000000
#define TX4927_CLKCTR_DMACKD	0x00800000
#define TX4927_CLKCTR_PCICKD	0x00400000
#define TX4927_CLKCTR_TM0CKD	0x00100000
#define TX4927_CLKCTR_TM1CKD	0x00080000
#define TX4927_CLKCTR_TM2CKD	0x00040000
#define TX4927_CLKCTR_SIO0CKD	0x00020000
#define TX4927_CLKCTR_SIO1CKD	0x00010000
#define TX4927_CLKCTR_ACLRST	0x00000200
#define TX4927_CLKCTR_PIORST	0x00000100
#define TX4927_CLKCTR_DMARST	0x00000080
#define TX4927_CLKCTR_PCIRST	0x00000040
#define TX4927_CLKCTR_TM0RST	0x00000010
#define TX4927_CLKCTR_TM1RST	0x00000008
#define TX4927_CLKCTR_TM2RST	0x00000004
#define TX4927_CLKCTR_SIO0RST	0x00000002
#define TX4927_CLKCTR_SIO1RST	0x00000001

#define tx4927_sdramcptr \
		((struct tx4927_sdramc_reg __iomem *)TX4927_SDRAMC_REG)
#define tx4927_pcicptr \
		((struct tx4927_pcic_reg __iomem *)TX4927_PCIC_REG)
#define tx4927_ccfgptr \
		((struct tx4927_ccfg_reg __iomem *)TX4927_CCFG_REG)
#define tx4927_ebuscptr \
		((struct tx4927_ebusc_reg __iomem *)TX4927_EBUSC_REG)
#define tx4927_pioptr		((struct txx9_pio_reg __iomem *)TX4927_PIO_REG)

#define TX4927_REV_PCODE()	\
	((__u32)__raw_readq(&tx4927_ccfgptr->crir) >> 16)

#define TX4927_SDRAMC_CR(ch)	__raw_readq(&tx4927_sdramcptr->cr[(ch)])
#define TX4927_SDRAMC_BA(ch)	((TX4927_SDRAMC_CR(ch) >> 49) << 21)
#define TX4927_SDRAMC_SIZE(ch)	\
	((((TX4927_SDRAMC_CR(ch) >> 33) & 0x7fff) + 1) << 21)

#define TX4927_EBUSC_CR(ch)	__raw_readq(&tx4927_ebuscptr->cr[(ch)])
#define TX4927_EBUSC_BA(ch)	((TX4927_EBUSC_CR(ch) >> 48) << 20)
#define TX4927_EBUSC_SIZE(ch)	\
	(0x00100000 << ((unsigned long)(TX4927_EBUSC_CR(ch) >> 8) & 0xf))

/* utilities */
static inline void txx9_clear64(__u64 __iomem *adr, __u64 bits)
{
#ifdef CONFIG_32BIT
	unsigned long flags;
	local_irq_save(flags);
#endif
	____raw_writeq(____raw_readq(adr) & ~bits, adr);
#ifdef CONFIG_32BIT
	local_irq_restore(flags);
#endif
}
static inline void txx9_set64(__u64 __iomem *adr, __u64 bits)
{
#ifdef CONFIG_32BIT
	unsigned long flags;
	local_irq_save(flags);
#endif
	____raw_writeq(____raw_readq(adr) | bits, adr);
#ifdef CONFIG_32BIT
	local_irq_restore(flags);
#endif
}

/* These functions are not interrupt safe. */
static inline void tx4927_ccfg_clear(__u64 bits)
{
	____raw_writeq(____raw_readq(&tx4927_ccfgptr->ccfg)
		       & ~(TX4927_CCFG_W1CBITS | bits),
		       &tx4927_ccfgptr->ccfg);
}
static inline void tx4927_ccfg_set(__u64 bits)
{
	____raw_writeq((____raw_readq(&tx4927_ccfgptr->ccfg)
			& ~TX4927_CCFG_W1CBITS) | bits,
		       &tx4927_ccfgptr->ccfg);
}
static inline void tx4927_ccfg_change(__u64 change, __u64 new)
{
	____raw_writeq((____raw_readq(&tx4927_ccfgptr->ccfg)
			& ~(TX4927_CCFG_W1CBITS | change)) |
		       new,
		       &tx4927_ccfgptr->ccfg);
}

unsigned int tx4927_get_mem_size(void);
void tx4927_wdt_init(void);
void tx4927_setup(void);
void tx4927_time_init(unsigned int tmrnr);
void tx4927_sio_init(unsigned int sclk, unsigned int cts_mask);
int tx4927_report_pciclk(void);
int tx4927_pciclk66_setup(void);
void tx4927_setup_pcierr_irq(void);
void tx4927_irq_init(void);

#endif /* __ASM_TXX9_TX4927_H */
