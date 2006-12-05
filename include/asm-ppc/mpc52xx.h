/*
 * include/asm-ppc/mpc52xx.h
 * 
 * Prototypes, etc. for the Freescale MPC52xx embedded cpu chips
 * May need to be cleaned as the port goes on ...
 *
 *
 * Maintainer : Sylvain Munaut <tnt@246tNt.com>
 *
 * Originally written by Dale Farnsworth <dfarnsworth@mvista.com> 
 * for the 2.4 kernel.
 *
 * Copyright (C) 2004-2005 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003 MontaVista, Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef __ASM_MPC52xx_H__
#define __ASM_MPC52xx_H__

#ifndef __ASSEMBLY__
#include <asm/ppcboot.h>
#include <asm/types.h>

struct pt_regs;
#endif /* __ASSEMBLY__ */


/* ======================================================================== */
/* PPC Sys devices definition                                               */
/* ======================================================================== */

enum ppc_sys_devices {
	MPC52xx_MSCAN1,
	MPC52xx_MSCAN2,
	MPC52xx_SPI,
	MPC52xx_USB,
	MPC52xx_BDLC,
	MPC52xx_PSC1,
	MPC52xx_PSC2,
	MPC52xx_PSC3,
	MPC52xx_PSC4,
	MPC52xx_PSC5,
	MPC52xx_PSC6,
	MPC52xx_FEC,
	MPC52xx_ATA,
	MPC52xx_I2C1,
	MPC52xx_I2C2,
	NUM_PPC_SYS_DEVS,
};


/* ======================================================================== */
/* Main registers/struct addresses                                          */
/* ======================================================================== */

/* MBAR position */
#define MPC52xx_MBAR		0xf0000000	/* Phys address */
#define MPC52xx_MBAR_VIRT	0xf0000000	/* Virt address */
#define MPC52xx_MBAR_SIZE	0x00010000

#define MPC52xx_PA(x)		((phys_addr_t)(MPC52xx_MBAR + (x)))
#define MPC52xx_VA(x)		((void __iomem *)(MPC52xx_MBAR_VIRT + (x)))

/* Registers zone offset/size  */
#define MPC52xx_MMAP_CTL_OFFSET		0x0000
#define MPC52xx_MMAP_CTL_SIZE		0x068
#define MPC52xx_SDRAM_OFFSET		0x0100
#define MPC52xx_SDRAM_SIZE		0x010
#define MPC52xx_CDM_OFFSET		0x0200
#define MPC52xx_CDM_SIZE		0x038
#define MPC52xx_INTR_OFFSET		0x0500
#define MPC52xx_INTR_SIZE		0x04c
#define MPC52xx_GPTx_OFFSET(x)		(0x0600 + ((x)<<4))
#define MPC52xx_GPT_SIZE		0x010
#define MPC52xx_RTC_OFFSET		0x0800
#define MPC52xx_RTC_SIZE		0x024
#define MPC52xx_GPIO_OFFSET		0x0b00
#define MPC52xx_GPIO_SIZE		0x040
#define MPC52xx_GPIO_WKUP_OFFSET	0x0c00
#define MPC52xx_GPIO_WKUP_SIZE		0x028
#define MPC52xx_PCI_OFFSET		0x0d00
#define MPC52xx_PCI_SIZE		0x100
#define MPC52xx_SDMA_OFFSET		0x1200
#define MPC52xx_SDMA_SIZE		0x100
#define MPC52xx_XLB_OFFSET		0x1f00
#define MPC52xx_XLB_SIZE		0x100
#define MPC52xx_PSCx_OFFSET(x)		(((x)!=6)?(0x1e00+((x)<<9)):0x2c00)
#define MPC52xx_PSC_SIZE		0x0a0

/* SRAM used for SDMA */
#define MPC52xx_SRAM_OFFSET		0x8000
#define MPC52xx_SRAM_SIZE		0x4000


/* ======================================================================== */
/* IRQ mapping                                                              */
/* ======================================================================== */
/* Be sure to look at mpc52xx_pic.h if you wish for whatever reason to change
 * this
 */

#define MPC52xx_CRIT_IRQ_NUM	4
#define MPC52xx_MAIN_IRQ_NUM	17
#define MPC52xx_SDMA_IRQ_NUM	17
#define MPC52xx_PERP_IRQ_NUM	23

#define MPC52xx_CRIT_IRQ_BASE	1
#define MPC52xx_MAIN_IRQ_BASE	(MPC52xx_CRIT_IRQ_BASE + MPC52xx_CRIT_IRQ_NUM)
#define MPC52xx_SDMA_IRQ_BASE	(MPC52xx_MAIN_IRQ_BASE + MPC52xx_MAIN_IRQ_NUM)
#define MPC52xx_PERP_IRQ_BASE	(MPC52xx_SDMA_IRQ_BASE + MPC52xx_SDMA_IRQ_NUM)

#define MPC52xx_IRQ0			(MPC52xx_CRIT_IRQ_BASE + 0)
#define MPC52xx_SLICE_TIMER_0_IRQ	(MPC52xx_CRIT_IRQ_BASE + 1)
#define MPC52xx_HI_INT_IRQ		(MPC52xx_CRIT_IRQ_BASE + 2)
#define MPC52xx_CCS_IRQ			(MPC52xx_CRIT_IRQ_BASE + 3)

#define MPC52xx_IRQ1			(MPC52xx_MAIN_IRQ_BASE + 1)
#define MPC52xx_IRQ2			(MPC52xx_MAIN_IRQ_BASE + 2)
#define MPC52xx_IRQ3			(MPC52xx_MAIN_IRQ_BASE + 3)

#define MPC52xx_SDMA_IRQ		(MPC52xx_PERP_IRQ_BASE + 0)
#define MPC52xx_PSC1_IRQ		(MPC52xx_PERP_IRQ_BASE + 1)
#define MPC52xx_PSC2_IRQ		(MPC52xx_PERP_IRQ_BASE + 2)
#define MPC52xx_PSC3_IRQ		(MPC52xx_PERP_IRQ_BASE + 3)
#define MPC52xx_PSC6_IRQ		(MPC52xx_PERP_IRQ_BASE + 4)
#define MPC52xx_IRDA_IRQ		(MPC52xx_PERP_IRQ_BASE + 4)
#define MPC52xx_FEC_IRQ			(MPC52xx_PERP_IRQ_BASE + 5)
#define MPC52xx_USB_IRQ			(MPC52xx_PERP_IRQ_BASE + 6)
#define MPC52xx_ATA_IRQ			(MPC52xx_PERP_IRQ_BASE + 7)
#define MPC52xx_PCI_CNTRL_IRQ		(MPC52xx_PERP_IRQ_BASE + 8)
#define MPC52xx_PCI_SCIRX_IRQ		(MPC52xx_PERP_IRQ_BASE + 9)
#define MPC52xx_PCI_SCITX_IRQ		(MPC52xx_PERP_IRQ_BASE + 10)
#define MPC52xx_PSC4_IRQ		(MPC52xx_PERP_IRQ_BASE + 11)
#define MPC52xx_PSC5_IRQ		(MPC52xx_PERP_IRQ_BASE + 12)
#define MPC52xx_SPI_MODF_IRQ		(MPC52xx_PERP_IRQ_BASE + 13)
#define MPC52xx_SPI_SPIF_IRQ		(MPC52xx_PERP_IRQ_BASE + 14)
#define MPC52xx_I2C1_IRQ		(MPC52xx_PERP_IRQ_BASE + 15)
#define MPC52xx_I2C2_IRQ		(MPC52xx_PERP_IRQ_BASE + 16)
#define MPC52xx_MSCAN1_IRQ		(MPC52xx_PERP_IRQ_BASE + 17)
#define MPC52xx_MSCAN2_IRQ		(MPC52xx_PERP_IRQ_BASE + 18)
#define MPC52xx_IR_RX_IRQ		(MPC52xx_PERP_IRQ_BASE + 19)
#define MPC52xx_IR_TX_IRQ		(MPC52xx_PERP_IRQ_BASE + 20)
#define MPC52xx_XLB_ARB_IRQ		(MPC52xx_PERP_IRQ_BASE + 21)
#define MPC52xx_BDLC_IRQ		(MPC52xx_PERP_IRQ_BASE + 22)



/* ======================================================================== */
/* Structures mapping of some unit register set                             */
/* ======================================================================== */

#ifndef __ASSEMBLY__

/* Memory Mapping Control */
struct mpc52xx_mmap_ctl {
	u32	mbar;		/* MMAP_CTRL + 0x00 */

	u32	cs0_start;	/* MMAP_CTRL + 0x04 */
	u32	cs0_stop;	/* MMAP_CTRL + 0x08 */
	u32	cs1_start;	/* MMAP_CTRL + 0x0c */
	u32	cs1_stop;	/* MMAP_CTRL + 0x10 */
	u32	cs2_start;	/* MMAP_CTRL + 0x14 */
	u32	cs2_stop;	/* MMAP_CTRL + 0x18 */
	u32	cs3_start;	/* MMAP_CTRL + 0x1c */
	u32	cs3_stop;	/* MMAP_CTRL + 0x20 */
	u32	cs4_start;	/* MMAP_CTRL + 0x24 */
	u32	cs4_stop;	/* MMAP_CTRL + 0x28 */
	u32	cs5_start;	/* MMAP_CTRL + 0x2c */
	u32	cs5_stop;	/* MMAP_CTRL + 0x30 */

	u32	sdram0;		/* MMAP_CTRL + 0x34 */
	u32	sdram1;		/* MMAP_CTRL + 0X38 */

	u32	reserved[4];	/* MMAP_CTRL + 0x3c .. 0x48 */

	u32	boot_start;	/* MMAP_CTRL + 0x4c */
	u32	boot_stop;	/* MMAP_CTRL + 0x50 */

	u32	ipbi_ws_ctrl;	/* MMAP_CTRL + 0x54 */

	u32	cs6_start;	/* MMAP_CTRL + 0x58 */
	u32	cs6_stop;	/* MMAP_CTRL + 0x5c */
	u32	cs7_start;	/* MMAP_CTRL + 0x60 */
	u32	cs7_stop;	/* MMAP_CTRL + 0x64 */
};

/* SDRAM control */
struct mpc52xx_sdram {
	u32	mode;		/* SDRAM + 0x00 */
	u32	ctrl;		/* SDRAM + 0x04 */
	u32	config1;	/* SDRAM + 0x08 */
	u32	config2;	/* SDRAM + 0x0c */
};

/* Interrupt controller */
struct mpc52xx_intr {
	u32	per_mask;	/* INTR + 0x00 */
	u32	per_pri1;	/* INTR + 0x04 */
	u32	per_pri2;	/* INTR + 0x08 */
	u32	per_pri3;	/* INTR + 0x0c */
	u32	ctrl;		/* INTR + 0x10 */
	u32	main_mask;	/* INTR + 0x14 */
	u32	main_pri1;	/* INTR + 0x18 */
	u32	main_pri2;	/* INTR + 0x1c */
	u32	reserved1;	/* INTR + 0x20 */
	u32	enc_status;	/* INTR + 0x24 */
	u32	crit_status;	/* INTR + 0x28 */
	u32	main_status;	/* INTR + 0x2c */
	u32	per_status;	/* INTR + 0x30 */
	u32	reserved2;	/* INTR + 0x34 */
	u32	per_error;	/* INTR + 0x38 */
};

/* SDMA */
struct mpc52xx_sdma {
	u32	taskBar;	/* SDMA + 0x00 */
	u32	currentPointer;	/* SDMA + 0x04 */
	u32	endPointer;	/* SDMA + 0x08 */
	u32	variablePointer;/* SDMA + 0x0c */

	u8	IntVect1;	/* SDMA + 0x10 */
	u8	IntVect2;	/* SDMA + 0x11 */
	u16	PtdCntrl;	/* SDMA + 0x12 */

	u32	IntPend;	/* SDMA + 0x14 */
	u32	IntMask;	/* SDMA + 0x18 */

	u16	tcr[16];	/* SDMA + 0x1c .. 0x3a */

	u8	ipr[32];	/* SDMA + 0x3c .. 0x5b */

	u32	cReqSelect;	/* SDMA + 0x5c */
	u32	task_size0;	/* SDMA + 0x60 */
	u32	task_size1;	/* SDMA + 0x64 */
	u32	MDEDebug;	/* SDMA + 0x68 */
	u32	ADSDebug;	/* SDMA + 0x6c */
	u32	Value1;		/* SDMA + 0x70 */
	u32	Value2;		/* SDMA + 0x74 */
	u32	Control;	/* SDMA + 0x78 */
	u32	Status;		/* SDMA + 0x7c */
	u32	PTDDebug;	/* SDMA + 0x80 */
};

/* GPT */
struct mpc52xx_gpt {
	u32	mode;		/* GPTx + 0x00 */
	u32	count;		/* GPTx + 0x04 */
	u32	pwm;		/* GPTx + 0x08 */
	u32	status;		/* GPTx + 0X0c */
};

/* RTC */
struct mpc52xx_rtc {
	u32	time_set;	/* RTC + 0x00 */
	u32	date_set;	/* RTC + 0x04 */
	u32	stopwatch;	/* RTC + 0x08 */
	u32	int_enable;	/* RTC + 0x0c */
	u32	time;		/* RTC + 0x10 */
	u32	date;		/* RTC + 0x14 */
	u32	stopwatch_intr;	/* RTC + 0x18 */
	u32	bus_error;	/* RTC + 0x1c */
	u32	dividers;	/* RTC + 0x20 */
};

/* GPIO */
struct mpc52xx_gpio {
	u32	port_config;	/* GPIO + 0x00 */
	u32	simple_gpioe;	/* GPIO + 0x04 */
	u32	simple_ode;	/* GPIO + 0x08 */
	u32	simple_ddr;	/* GPIO + 0x0c */
	u32	simple_dvo;	/* GPIO + 0x10 */
	u32	simple_ival;	/* GPIO + 0x14 */
	u8	outo_gpioe;	/* GPIO + 0x18 */
	u8	reserved1[3];	/* GPIO + 0x19 */
	u8	outo_dvo;	/* GPIO + 0x1c */
	u8	reserved2[3];	/* GPIO + 0x1d */
	u8	sint_gpioe;	/* GPIO + 0x20 */
	u8	reserved3[3];	/* GPIO + 0x21 */
	u8	sint_ode;	/* GPIO + 0x24 */
	u8	reserved4[3];	/* GPIO + 0x25 */
	u8	sint_ddr;	/* GPIO + 0x28 */
	u8	reserved5[3];	/* GPIO + 0x29 */
	u8	sint_dvo;	/* GPIO + 0x2c */
	u8	reserved6[3];	/* GPIO + 0x2d */
	u8	sint_inten;	/* GPIO + 0x30 */
	u8	reserved7[3];	/* GPIO + 0x31 */
	u16	sint_itype;	/* GPIO + 0x34 */
	u16	reserved8;	/* GPIO + 0x36 */
	u8	gpio_control;	/* GPIO + 0x38 */
	u8	reserved9[3];	/* GPIO + 0x39 */
	u8	sint_istat;	/* GPIO + 0x3c */
	u8	sint_ival;	/* GPIO + 0x3d */
	u8	bus_errs;	/* GPIO + 0x3e */
	u8	reserved10;	/* GPIO + 0x3f */
};

#define MPC52xx_GPIO_PSC_CONFIG_UART_WITHOUT_CD	4
#define MPC52xx_GPIO_PSC_CONFIG_UART_WITH_CD	5
#define MPC52xx_GPIO_PCI_DIS			(1<<15)

/* GPIO with WakeUp*/
struct mpc52xx_gpio_wkup {
	u8	wkup_gpioe;	/* GPIO_WKUP + 0x00 */
	u8	reserved1[3];	/* GPIO_WKUP + 0x03 */
	u8	wkup_ode;	/* GPIO_WKUP + 0x04 */
	u8	reserved2[3];	/* GPIO_WKUP + 0x05 */
	u8	wkup_ddr;	/* GPIO_WKUP + 0x08 */
	u8	reserved3[3];	/* GPIO_WKUP + 0x09 */
	u8	wkup_dvo;	/* GPIO_WKUP + 0x0C */
	u8	reserved4[3];	/* GPIO_WKUP + 0x0D */
	u8	wkup_inten;	/* GPIO_WKUP + 0x10 */
	u8	reserved5[3];	/* GPIO_WKUP + 0x11 */
	u8	wkup_iinten;	/* GPIO_WKUP + 0x14 */
	u8	reserved6[3];	/* GPIO_WKUP + 0x15 */
	u16	wkup_itype;	/* GPIO_WKUP + 0x18 */
	u8	reserved7[2];	/* GPIO_WKUP + 0x1A */
	u8	wkup_maste;	/* GPIO_WKUP + 0x1C */
	u8	reserved8[3];	/* GPIO_WKUP + 0x1D */
	u8	wkup_ival;	/* GPIO_WKUP + 0x20 */
	u8	reserved9[3];	/* GPIO_WKUP + 0x21 */
	u8	wkup_istat;	/* GPIO_WKUP + 0x24 */
	u8	reserved10[3];	/* GPIO_WKUP + 0x25 */
};

/* XLB Bus control */
struct mpc52xx_xlb {
	u8	reserved[0x40];
	u32	config;			/* XLB + 0x40 */
	u32	version;		/* XLB + 0x44 */
	u32	status;			/* XLB + 0x48 */
	u32	int_enable;		/* XLB + 0x4c */
	u32	addr_capture;		/* XLB + 0x50 */
	u32	bus_sig_capture;	/* XLB + 0x54 */
	u32	addr_timeout;		/* XLB + 0x58 */
	u32	data_timeout;		/* XLB + 0x5c */
	u32	bus_act_timeout;	/* XLB + 0x60 */
	u32	master_pri_enable;	/* XLB + 0x64 */
	u32	master_priority;	/* XLB + 0x68 */
	u32	base_address;		/* XLB + 0x6c */
	u32	snoop_window;		/* XLB + 0x70 */
};

#define MPC52xx_XLB_CFG_PLDIS		(1 << 31)
#define MPC52xx_XLB_CFG_SNOOP		(1 << 15)

/* Clock Distribution control */
struct mpc52xx_cdm {
	u32	jtag_id;		/* CDM + 0x00  reg0 read only */
	u32	rstcfg;			/* CDM + 0x04  reg1 read only */
	u32	breadcrumb;		/* CDM + 0x08  reg2 */

	u8	mem_clk_sel;		/* CDM + 0x0c  reg3 byte0 */
	u8	xlb_clk_sel;		/* CDM + 0x0d  reg3 byte1 read only */
	u8	ipb_clk_sel;		/* CDM + 0x0e  reg3 byte2 */
	u8	pci_clk_sel;		/* CDM + 0x0f  reg3 byte3 */

	u8	ext_48mhz_en;		/* CDM + 0x10  reg4 byte0 */
	u8	fd_enable;		/* CDM + 0x11  reg4 byte1 */
	u16	fd_counters;		/* CDM + 0x12  reg4 byte2,3 */

	u32	clk_enables;		/* CDM + 0x14  reg5 */

	u8	osc_disable;		/* CDM + 0x18  reg6 byte0 */
	u8	reserved0[3];		/* CDM + 0x19  reg6 byte1,2,3 */

	u8	ccs_sleep_enable;	/* CDM + 0x1c  reg7 byte0 */
	u8	osc_sleep_enable;	/* CDM + 0x1d  reg7 byte1 */
	u8	reserved1;		/* CDM + 0x1e  reg7 byte2 */
	u8	ccs_qreq_test;		/* CDM + 0x1f  reg7 byte3 */

	u8	soft_reset;		/* CDM + 0x20  u8 byte0 */
	u8	no_ckstp;		/* CDM + 0x21  u8 byte0 */
	u8	reserved2[2];		/* CDM + 0x22  u8 byte1,2,3 */

	u8	pll_lock;		/* CDM + 0x24  reg9 byte0 */
	u8	pll_looselock;		/* CDM + 0x25  reg9 byte1 */
	u8	pll_sm_lockwin;		/* CDM + 0x26  reg9 byte2 */
	u8	reserved3;		/* CDM + 0x27  reg9 byte3 */

	u16	reserved4;		/* CDM + 0x28  reg10 byte0,1 */
	u16	mclken_div_psc1;	/* CDM + 0x2a  reg10 byte2,3 */

	u16	reserved5;		/* CDM + 0x2c  reg11 byte0,1 */
	u16	mclken_div_psc2;	/* CDM + 0x2e  reg11 byte2,3 */

	u16	reserved6;		/* CDM + 0x30  reg12 byte0,1 */
	u16	mclken_div_psc3;	/* CDM + 0x32  reg12 byte2,3 */

	u16	reserved7;		/* CDM + 0x34  reg13 byte0,1 */
	u16	mclken_div_psc6;	/* CDM + 0x36  reg13 byte2,3 */
};

#endif /* __ASSEMBLY__ */


/* ========================================================================= */
/* Prototypes for MPC52xx syslib                                             */
/* ========================================================================= */

#ifndef __ASSEMBLY__

extern void mpc52xx_init_irq(void);
extern int mpc52xx_get_irq(void);

extern unsigned long mpc52xx_find_end_of_memory(void);
extern void mpc52xx_set_bat(void);
extern void mpc52xx_map_io(void);
extern void mpc52xx_restart(char *cmd);
extern void mpc52xx_halt(void);
extern void mpc52xx_power_off(void);
extern void mpc52xx_progress(char *s, unsigned short hex);
extern void mpc52xx_calibrate_decr(void);

extern void mpc52xx_find_bridges(void);

extern void mpc52xx_setup_cpu(void);



	/* Matching of PSC function */
struct mpc52xx_psc_func {
	int id;
	char *func;
};

extern int mpc52xx_match_psc_function(int psc_idx, const char *func);
extern struct  mpc52xx_psc_func mpc52xx_psc_functions[];
	/* This array is to be defined in platform file */

#endif /* __ASSEMBLY__ */


/* ========================================================================= */
/* Platform configuration                                                    */
/* ========================================================================= */

/* The U-Boot platform information struct */
extern bd_t __res;

/* Platform options */
#if defined(CONFIG_LITE5200)
#include <platforms/lite5200.h>
#endif


#endif /* __ASM_MPC52xx_H__ */
