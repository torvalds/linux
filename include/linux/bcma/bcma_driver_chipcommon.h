#ifndef LINUX_BCMA_DRIVER_CC_H_
#define LINUX_BCMA_DRIVER_CC_H_

/** ChipCommon core registers. **/
#define BCMA_CC_ID			0x0000
#define  BCMA_CC_ID_ID			0x0000FFFF
#define  BCMA_CC_ID_ID_SHIFT		0
#define  BCMA_CC_ID_REV			0x000F0000
#define  BCMA_CC_ID_REV_SHIFT		16
#define  BCMA_CC_ID_PKG			0x00F00000
#define  BCMA_CC_ID_PKG_SHIFT		20
#define  BCMA_CC_ID_NRCORES		0x0F000000
#define  BCMA_CC_ID_NRCORES_SHIFT	24
#define  BCMA_CC_ID_TYPE		0xF0000000
#define  BCMA_CC_ID_TYPE_SHIFT		28
#define BCMA_CC_CAP			0x0004		/* Capabilities */
#define  BCMA_CC_CAP_NRUART		0x00000003	/* # of UARTs */
#define  BCMA_CC_CAP_MIPSEB		0x00000004	/* MIPS in BigEndian Mode */
#define  BCMA_CC_CAP_UARTCLK		0x00000018	/* UART clock select */
#define   BCMA_CC_CAP_UARTCLK_INT	0x00000008	/* UARTs are driven by internal divided clock */
#define  BCMA_CC_CAP_UARTGPIO		0x00000020	/* UARTs on GPIO 15-12 */
#define  BCMA_CC_CAP_EXTBUS		0x000000C0	/* External buses present */
#define  BCMA_CC_CAP_FLASHT		0x00000700	/* Flash Type */
#define   BCMA_CC_FLASHT_NONE		0x00000000	/* No flash */
#define   BCMA_CC_FLASHT_STSER		0x00000100	/* ST serial flash */
#define   BCMA_CC_FLASHT_ATSER		0x00000200	/* Atmel serial flash */
#define   BCMA_CC_FLASHT_NFLASH		0x00000200	/* NAND flash */
#define	  BCMA_CC_FLASHT_PARA		0x00000700	/* Parallel flash */
#define  BCMA_CC_CAP_PLLT		0x00038000	/* PLL Type */
#define   BCMA_PLLTYPE_NONE		0x00000000
#define   BCMA_PLLTYPE_1		0x00010000	/* 48Mhz base, 3 dividers */
#define   BCMA_PLLTYPE_2		0x00020000	/* 48Mhz, 4 dividers */
#define   BCMA_PLLTYPE_3		0x00030000	/* 25Mhz, 2 dividers */
#define   BCMA_PLLTYPE_4		0x00008000	/* 48Mhz, 4 dividers */
#define   BCMA_PLLTYPE_5		0x00018000	/* 25Mhz, 4 dividers */
#define   BCMA_PLLTYPE_6		0x00028000	/* 100/200 or 120/240 only */
#define   BCMA_PLLTYPE_7		0x00038000	/* 25Mhz, 4 dividers */
#define  BCMA_CC_CAP_PCTL		0x00040000	/* Power Control */
#define  BCMA_CC_CAP_OTPS		0x00380000	/* OTP size */
#define  BCMA_CC_CAP_OTPS_SHIFT		19
#define  BCMA_CC_CAP_OTPS_BASE		5
#define  BCMA_CC_CAP_JTAGM		0x00400000	/* JTAG master present */
#define  BCMA_CC_CAP_BROM		0x00800000	/* Internal boot ROM active */
#define  BCMA_CC_CAP_64BIT		0x08000000	/* 64-bit Backplane */
#define  BCMA_CC_CAP_PMU		0x10000000	/* PMU available (rev >= 20) */
#define  BCMA_CC_CAP_ECI		0x20000000	/* ECI available (rev >= 20) */
#define  BCMA_CC_CAP_SPROM		0x40000000	/* SPROM present */
#define  BCMA_CC_CAP_NFLASH		0x80000000	/* NAND flash present (rev >= 35 or BCM4706?) */
#define BCMA_CC_CORECTL			0x0008
#define  BCMA_CC_CORECTL_UARTCLK0	0x00000001	/* Drive UART with internal clock */
#define	 BCMA_CC_CORECTL_SE		0x00000002	/* sync clk out enable (corerev >= 3) */
#define  BCMA_CC_CORECTL_UARTCLKEN	0x00000008	/* UART clock enable (rev >= 21) */
#define BCMA_CC_BIST			0x000C
#define BCMA_CC_OTPS			0x0010		/* OTP status */
#define	 BCMA_CC_OTPS_PROGFAIL		0x80000000
#define	 BCMA_CC_OTPS_PROTECT		0x00000007
#define	 BCMA_CC_OTPS_HW_PROTECT	0x00000001
#define	 BCMA_CC_OTPS_SW_PROTECT	0x00000002
#define	 BCMA_CC_OTPS_CID_PROTECT	0x00000004
#define  BCMA_CC_OTPS_GU_PROG_IND	0x00000F00	/* General Use programmed indication */
#define  BCMA_CC_OTPS_GU_PROG_IND_SHIFT	8
#define  BCMA_CC_OTPS_GU_PROG_HW	0x00000100	/* HW region programmed */
#define BCMA_CC_OTPC			0x0014		/* OTP control */
#define	 BCMA_CC_OTPC_RECWAIT		0xFF000000
#define	 BCMA_CC_OTPC_PROGWAIT		0x00FFFF00
#define	 BCMA_CC_OTPC_PRW_SHIFT		8
#define	 BCMA_CC_OTPC_MAXFAIL		0x00000038
#define	 BCMA_CC_OTPC_VSEL		0x00000006
#define	 BCMA_CC_OTPC_SELVL		0x00000001
#define BCMA_CC_OTPP			0x0018		/* OTP prog */
#define	 BCMA_CC_OTPP_COL		0x000000FF
#define	 BCMA_CC_OTPP_ROW		0x0000FF00
#define	 BCMA_CC_OTPP_ROW_SHIFT		8
#define	 BCMA_CC_OTPP_READERR		0x10000000
#define	 BCMA_CC_OTPP_VALUE		0x20000000
#define	 BCMA_CC_OTPP_READ		0x40000000
#define	 BCMA_CC_OTPP_START		0x80000000
#define	 BCMA_CC_OTPP_BUSY		0x80000000
#define BCMA_CC_OTPL			0x001C		/* OTP layout */
#define  BCMA_CC_OTPL_GURGN_OFFSET	0x00000FFF	/* offset of general use region */
#define BCMA_CC_IRQSTAT			0x0020
#define BCMA_CC_IRQMASK			0x0024
#define	 BCMA_CC_IRQ_GPIO		0x00000001	/* gpio intr */
#define	 BCMA_CC_IRQ_EXT		0x00000002	/* ro: ext intr pin (corerev >= 3) */
#define	 BCMA_CC_IRQ_WDRESET		0x80000000	/* watchdog reset occurred */
#define BCMA_CC_CHIPCTL			0x0028		/* Rev >= 11 only */
#define BCMA_CC_CHIPSTAT		0x002C		/* Rev >= 11 only */
#define  BCMA_CC_CHIPST_4313_SPROM_PRESENT	1
#define  BCMA_CC_CHIPST_4313_OTP_PRESENT	2
#define  BCMA_CC_CHIPST_4331_SPROM_PRESENT	2
#define  BCMA_CC_CHIPST_4331_OTP_PRESENT	4
#define  BCMA_CC_CHIPST_43228_ILP_DIV_EN	0x00000001
#define  BCMA_CC_CHIPST_43228_OTP_PRESENT	0x00000002
#define  BCMA_CC_CHIPST_43228_SERDES_REFCLK_PADSEL	0x00000004
#define  BCMA_CC_CHIPST_43228_SDIO_MODE		0x00000008
#define  BCMA_CC_CHIPST_43228_SDIO_OTP_PRESENT	0x00000010
#define  BCMA_CC_CHIPST_43228_SDIO_RESET	0x00000020
#define  BCMA_CC_CHIPST_4706_PKG_OPTION		BIT(0) /* 0: full-featured package 1: low-cost package */
#define  BCMA_CC_CHIPST_4706_SFLASH_PRESENT	BIT(1) /* 0: parallel, 1: serial flash is present */
#define  BCMA_CC_CHIPST_4706_SFLASH_TYPE	BIT(2) /* 0: 8b-p/ST-s flash, 1: 16b-p/Atmal-s flash */
#define  BCMA_CC_CHIPST_4706_MIPS_BENDIAN	BIT(3) /* 0: little, 1: big endian */
#define  BCMA_CC_CHIPST_4706_PCIE1_DISABLE	BIT(5) /* PCIE1 enable strap pin */
#define  BCMA_CC_CHIPST_5357_NAND_BOOT		BIT(4) /* NAND boot, valid for CC rev 38 and/or BCM5357 */
#define BCMA_CC_JCMD			0x0030		/* Rev >= 10 only */
#define  BCMA_CC_JCMD_START		0x80000000
#define  BCMA_CC_JCMD_BUSY		0x80000000
#define  BCMA_CC_JCMD_PAUSE		0x40000000
#define  BCMA_CC_JCMD0_ACC_MASK		0x0000F000
#define  BCMA_CC_JCMD0_ACC_IRDR		0x00000000
#define  BCMA_CC_JCMD0_ACC_DR		0x00001000
#define  BCMA_CC_JCMD0_ACC_IR		0x00002000
#define  BCMA_CC_JCMD0_ACC_RESET	0x00003000
#define  BCMA_CC_JCMD0_ACC_IRPDR	0x00004000
#define  BCMA_CC_JCMD0_ACC_PDR		0x00005000
#define  BCMA_CC_JCMD0_IRW_MASK		0x00000F00
#define  BCMA_CC_JCMD_ACC_MASK		0x000F0000	/* Changes for corerev 11 */
#define  BCMA_CC_JCMD_ACC_IRDR		0x00000000
#define  BCMA_CC_JCMD_ACC_DR		0x00010000
#define  BCMA_CC_JCMD_ACC_IR		0x00020000
#define  BCMA_CC_JCMD_ACC_RESET		0x00030000
#define  BCMA_CC_JCMD_ACC_IRPDR		0x00040000
#define  BCMA_CC_JCMD_ACC_PDR		0x00050000
#define  BCMA_CC_JCMD_IRW_MASK		0x00001F00
#define  BCMA_CC_JCMD_IRW_SHIFT		8
#define  BCMA_CC_JCMD_DRW_MASK		0x0000003F
#define BCMA_CC_JIR			0x0034		/* Rev >= 10 only */
#define BCMA_CC_JDR			0x0038		/* Rev >= 10 only */
#define BCMA_CC_JCTL			0x003C		/* Rev >= 10 only */
#define  BCMA_CC_JCTL_FORCE_CLK		4		/* Force clock */
#define  BCMA_CC_JCTL_EXT_EN		2		/* Enable external targets */
#define  BCMA_CC_JCTL_EN		1		/* Enable Jtag master */
#define BCMA_CC_FLASHCTL		0x0040
/* Start/busy bit in flashcontrol */
#define  BCMA_CC_FLASHCTL_OPCODE	0x000000ff
#define  BCMA_CC_FLASHCTL_ACTION	0x00000700
#define  BCMA_CC_FLASHCTL_CS_ACTIVE	0x00001000	/* Chip Select Active, rev >= 20 */
#define  BCMA_CC_FLASHCTL_START		0x80000000
#define  BCMA_CC_FLASHCTL_BUSY		BCMA_CC_FLASHCTL_START
/* Flashcontrol action + opcodes for ST flashes */
#define  BCMA_CC_FLASHCTL_ST_WREN	0x0006		/* Write Enable */
#define  BCMA_CC_FLASHCTL_ST_WRDIS	0x0004		/* Write Disable */
#define  BCMA_CC_FLASHCTL_ST_RDSR	0x0105		/* Read Status Register */
#define  BCMA_CC_FLASHCTL_ST_WRSR	0x0101		/* Write Status Register */
#define  BCMA_CC_FLASHCTL_ST_READ	0x0303		/* Read Data Bytes */
#define  BCMA_CC_FLASHCTL_ST_PP		0x0302		/* Page Program */
#define  BCMA_CC_FLASHCTL_ST_SE		0x02d8		/* Sector Erase */
#define  BCMA_CC_FLASHCTL_ST_BE		0x00c7		/* Bulk Erase */
#define  BCMA_CC_FLASHCTL_ST_DP		0x00b9		/* Deep Power-down */
#define  BCMA_CC_FLASHCTL_ST_RES	0x03ab		/* Read Electronic Signature */
#define  BCMA_CC_FLASHCTL_ST_CSA	0x1000		/* Keep chip select asserted */
#define  BCMA_CC_FLASHCTL_ST_SSE	0x0220		/* Sub-sector Erase */
/* Flashcontrol action + opcodes for Atmel flashes */
#define  BCMA_CC_FLASHCTL_AT_READ			0x07e8
#define  BCMA_CC_FLASHCTL_AT_PAGE_READ			0x07d2
#define  BCMA_CC_FLASHCTL_AT_STATUS			0x01d7
#define  BCMA_CC_FLASHCTL_AT_BUF1_WRITE			0x0384
#define  BCMA_CC_FLASHCTL_AT_BUF2_WRITE			0x0387
#define  BCMA_CC_FLASHCTL_AT_BUF1_ERASE_PROGRAM		0x0283
#define  BCMA_CC_FLASHCTL_AT_BUF2_ERASE_PROGRAM		0x0286
#define  BCMA_CC_FLASHCTL_AT_BUF1_PROGRAM		0x0288
#define  BCMA_CC_FLASHCTL_AT_BUF2_PROGRAM		0x0289
#define  BCMA_CC_FLASHCTL_AT_PAGE_ERASE			0x0281
#define  BCMA_CC_FLASHCTL_AT_BLOCK_ERASE		0x0250
#define  BCMA_CC_FLASHCTL_AT_BUF1_WRITE_ERASE_PROGRAM	0x0382
#define  BCMA_CC_FLASHCTL_AT_BUF2_WRITE_ERASE_PROGRAM	0x0385
#define  BCMA_CC_FLASHCTL_AT_BUF1_LOAD			0x0253
#define  BCMA_CC_FLASHCTL_AT_BUF2_LOAD			0x0255
#define  BCMA_CC_FLASHCTL_AT_BUF1_COMPARE		0x0260
#define  BCMA_CC_FLASHCTL_AT_BUF2_COMPARE		0x0261
#define  BCMA_CC_FLASHCTL_AT_BUF1_REPROGRAM		0x0258
#define  BCMA_CC_FLASHCTL_AT_BUF2_REPROGRAM		0x0259
#define BCMA_CC_FLASHADDR		0x0044
#define BCMA_CC_FLASHDATA		0x0048
/* Status register bits for ST flashes */
#define  BCMA_CC_FLASHDATA_ST_WIP	0x01		/* Write In Progress */
#define  BCMA_CC_FLASHDATA_ST_WEL	0x02		/* Write Enable Latch */
#define  BCMA_CC_FLASHDATA_ST_BP_MASK	0x1c		/* Block Protect */
#define  BCMA_CC_FLASHDATA_ST_BP_SHIFT	2
#define  BCMA_CC_FLASHDATA_ST_SRWD	0x80		/* Status Register Write Disable */
/* Status register bits for Atmel flashes */
#define  BCMA_CC_FLASHDATA_AT_READY	0x80
#define  BCMA_CC_FLASHDATA_AT_MISMATCH	0x40
#define  BCMA_CC_FLASHDATA_AT_ID_MASK	0x38
#define  BCMA_CC_FLASHDATA_AT_ID_SHIFT	3
#define BCMA_CC_BCAST_ADDR		0x0050
#define BCMA_CC_BCAST_DATA		0x0054
#define BCMA_CC_GPIOPULLUP		0x0058		/* Rev >= 20 only */
#define BCMA_CC_GPIOPULLDOWN		0x005C		/* Rev >= 20 only */
#define BCMA_CC_GPIOIN			0x0060
#define BCMA_CC_GPIOOUT			0x0064
#define BCMA_CC_GPIOOUTEN		0x0068
#define BCMA_CC_GPIOCTL			0x006C
#define BCMA_CC_GPIOPOL			0x0070
#define BCMA_CC_GPIOIRQ			0x0074
#define BCMA_CC_WATCHDOG		0x0080
#define BCMA_CC_GPIOTIMER		0x0088		/* LED powersave (corerev >= 16) */
#define  BCMA_CC_GPIOTIMER_OFFTIME	0x0000FFFF
#define  BCMA_CC_GPIOTIMER_OFFTIME_SHIFT	0
#define  BCMA_CC_GPIOTIMER_ONTIME	0xFFFF0000
#define  BCMA_CC_GPIOTIMER_ONTIME_SHIFT	16
#define BCMA_CC_GPIOTOUTM		0x008C		/* LED powersave (corerev >= 16) */
#define BCMA_CC_CLOCK_N			0x0090
#define BCMA_CC_CLOCK_SB		0x0094
#define BCMA_CC_CLOCK_PCI		0x0098
#define BCMA_CC_CLOCK_M2		0x009C
#define BCMA_CC_CLOCK_MIPS		0x00A0
#define BCMA_CC_CLKDIV			0x00A4		/* Rev >= 3 only */
#define	 BCMA_CC_CLKDIV_SFLASH		0x0F000000
#define	 BCMA_CC_CLKDIV_SFLASH_SHIFT	24
#define	 BCMA_CC_CLKDIV_OTP		0x000F0000
#define	 BCMA_CC_CLKDIV_OTP_SHIFT	16
#define	 BCMA_CC_CLKDIV_JTAG		0x00000F00
#define	 BCMA_CC_CLKDIV_JTAG_SHIFT	8
#define	 BCMA_CC_CLKDIV_UART		0x000000FF
#define BCMA_CC_CAP_EXT			0x00AC		/* Capabilities */
#define BCMA_CC_PLLONDELAY		0x00B0		/* Rev >= 4 only */
#define BCMA_CC_FREFSELDELAY		0x00B4		/* Rev >= 4 only */
#define BCMA_CC_SLOWCLKCTL		0x00B8		/* 6 <= Rev <= 9 only */
#define  BCMA_CC_SLOWCLKCTL_SRC		0x00000007	/* slow clock source mask */
#define	  BCMA_CC_SLOWCLKCTL_SRC_LPO	0x00000000	/* source of slow clock is LPO */
#define   BCMA_CC_SLOWCLKCTL_SRC_XTAL	0x00000001	/* source of slow clock is crystal */
#define	  BCMA_CC_SLOECLKCTL_SRC_PCI	0x00000002	/* source of slow clock is PCI */
#define  BCMA_CC_SLOWCLKCTL_LPOFREQ	0x00000200	/* LPOFreqSel, 1: 160Khz, 0: 32KHz */
#define  BCMA_CC_SLOWCLKCTL_LPOPD	0x00000400	/* LPOPowerDown, 1: LPO is disabled, 0: LPO is enabled */
#define  BCMA_CC_SLOWCLKCTL_FSLOW	0x00000800	/* ForceSlowClk, 1: sb/cores running on slow clock, 0: power logic control */
#define  BCMA_CC_SLOWCLKCTL_IPLL	0x00001000	/* IgnorePllOffReq, 1/0: power logic ignores/honors PLL clock disable requests from core */
#define  BCMA_CC_SLOWCLKCTL_ENXTAL	0x00002000	/* XtalControlEn, 1/0: power logic does/doesn't disable crystal when appropriate */
#define  BCMA_CC_SLOWCLKCTL_XTALPU	0x00004000	/* XtalPU (RO), 1/0: crystal running/disabled */
#define  BCMA_CC_SLOWCLKCTL_CLKDIV	0xFFFF0000	/* ClockDivider (SlowClk = 1/(4+divisor)) */
#define  BCMA_CC_SLOWCLKCTL_CLKDIV_SHIFT	16
#define BCMA_CC_SYSCLKCTL		0x00C0		/* Rev >= 3 only */
#define	 BCMA_CC_SYSCLKCTL_IDLPEN	0x00000001	/* ILPen: Enable Idle Low Power */
#define	 BCMA_CC_SYSCLKCTL_ALPEN	0x00000002	/* ALPen: Enable Active Low Power */
#define	 BCMA_CC_SYSCLKCTL_PLLEN	0x00000004	/* ForcePLLOn */
#define	 BCMA_CC_SYSCLKCTL_FORCEALP	0x00000008	/* Force ALP (or HT if ALPen is not set */
#define	 BCMA_CC_SYSCLKCTL_FORCEHT	0x00000010	/* Force HT */
#define  BCMA_CC_SYSCLKCTL_CLKDIV	0xFFFF0000	/* ClkDiv  (ILP = 1/(4+divisor)) */
#define  BCMA_CC_SYSCLKCTL_CLKDIV_SHIFT	16
#define BCMA_CC_CLKSTSTR		0x00C4		/* Rev >= 3 only */
#define BCMA_CC_EROM			0x00FC
#define BCMA_CC_PCMCIA_CFG		0x0100
#define BCMA_CC_PCMCIA_MEMWAIT		0x0104
#define BCMA_CC_PCMCIA_ATTRWAIT		0x0108
#define BCMA_CC_PCMCIA_IOWAIT		0x010C
#define BCMA_CC_IDE_CFG			0x0110
#define BCMA_CC_IDE_MEMWAIT		0x0114
#define BCMA_CC_IDE_ATTRWAIT		0x0118
#define BCMA_CC_IDE_IOWAIT		0x011C
#define BCMA_CC_PROG_CFG		0x0120
#define BCMA_CC_PROG_WAITCNT		0x0124
#define BCMA_CC_FLASH_CFG		0x0128
#define  BCMA_CC_FLASH_CFG_DS		0x0010	/* Data size, 0=8bit, 1=16bit */
#define BCMA_CC_FLASH_WAITCNT		0x012C
#define BCMA_CC_SROM_CONTROL		0x0190
#define  BCMA_CC_SROM_CONTROL_START	0x80000000
#define  BCMA_CC_SROM_CONTROL_BUSY	0x80000000
#define  BCMA_CC_SROM_CONTROL_OPCODE	0x60000000
#define  BCMA_CC_SROM_CONTROL_OP_READ	0x00000000
#define  BCMA_CC_SROM_CONTROL_OP_WRITE	0x20000000
#define  BCMA_CC_SROM_CONTROL_OP_WRDIS	0x40000000
#define  BCMA_CC_SROM_CONTROL_OP_WREN	0x60000000
#define  BCMA_CC_SROM_CONTROL_OTPSEL	0x00000010
#define  BCMA_CC_SROM_CONTROL_LOCK	0x00000008
#define  BCMA_CC_SROM_CONTROL_SIZE_MASK	0x00000006
#define  BCMA_CC_SROM_CONTROL_SIZE_1K	0x00000000
#define  BCMA_CC_SROM_CONTROL_SIZE_4K	0x00000002
#define  BCMA_CC_SROM_CONTROL_SIZE_16K	0x00000004
#define  BCMA_CC_SROM_CONTROL_SIZE_SHIFT	1
#define  BCMA_CC_SROM_CONTROL_PRESENT	0x00000001
/* Block 0x140 - 0x190 registers are chipset specific */
#define BCMA_CC_4706_FLASHSCFG		0x18C		/* Flash struct configuration */
#define  BCMA_CC_4706_FLASHSCFG_MASK	0x000000ff
#define  BCMA_CC_4706_FLASHSCFG_SF1	0x00000001	/* 2nd serial flash present */
#define  BCMA_CC_4706_FLASHSCFG_PF1	0x00000002	/* 2nd parallel flash present */
#define  BCMA_CC_4706_FLASHSCFG_SF1_TYPE	0x00000004	/* 2nd serial flash type : 0 : ST, 1 : Atmel */
#define  BCMA_CC_4706_FLASHSCFG_NF1	0x00000008	/* 2nd NAND flash present */
#define  BCMA_CC_4706_FLASHSCFG_1ST_MADDR_SEG_MASK	0x000000f0
#define  BCMA_CC_4706_FLASHSCFG_1ST_MADDR_SEG_4MB	0x00000010	/* 4MB */
#define  BCMA_CC_4706_FLASHSCFG_1ST_MADDR_SEG_8MB	0x00000020	/* 8MB */
#define  BCMA_CC_4706_FLASHSCFG_1ST_MADDR_SEG_16MB	0x00000030	/* 16MB */
#define  BCMA_CC_4706_FLASHSCFG_1ST_MADDR_SEG_32MB	0x00000040	/* 32MB */
#define  BCMA_CC_4706_FLASHSCFG_1ST_MADDR_SEG_64MB	0x00000050	/* 64MB */
#define  BCMA_CC_4706_FLASHSCFG_1ST_MADDR_SEG_128MB	0x00000060	/* 128MB */
#define  BCMA_CC_4706_FLASHSCFG_1ST_MADDR_SEG_256MB	0x00000070	/* 256MB */
/* NAND flash registers for BCM4706 (corerev = 31) */
#define BCMA_CC_NFLASH_CTL		0x01A0
#define  BCMA_CC_NFLASH_CTL_ERR		0x08000000
#define BCMA_CC_NFLASH_CONF		0x01A4
#define BCMA_CC_NFLASH_COL_ADDR		0x01A8
#define BCMA_CC_NFLASH_ROW_ADDR		0x01AC
#define BCMA_CC_NFLASH_DATA		0x01B0
#define BCMA_CC_NFLASH_WAITCNT0		0x01B4
/* 0x1E0 is defined as shared BCMA_CLKCTLST */
#define BCMA_CC_HW_WORKAROUND		0x01E4 /* Hardware workaround (rev >= 20) */
#define BCMA_CC_UART0_DATA		0x0300
#define BCMA_CC_UART0_IMR		0x0304
#define BCMA_CC_UART0_FCR		0x0308
#define BCMA_CC_UART0_LCR		0x030C
#define BCMA_CC_UART0_MCR		0x0310
#define BCMA_CC_UART0_LSR		0x0314
#define BCMA_CC_UART0_MSR		0x0318
#define BCMA_CC_UART0_SCRATCH		0x031C
#define BCMA_CC_UART1_DATA		0x0400
#define BCMA_CC_UART1_IMR		0x0404
#define BCMA_CC_UART1_FCR		0x0408
#define BCMA_CC_UART1_LCR		0x040C
#define BCMA_CC_UART1_MCR		0x0410
#define BCMA_CC_UART1_LSR		0x0414
#define BCMA_CC_UART1_MSR		0x0418
#define BCMA_CC_UART1_SCRATCH		0x041C
/* PMU registers (rev >= 20) */
#define BCMA_CC_PMU_CTL			0x0600 /* PMU control */
#define  BCMA_CC_PMU_CTL_ILP_DIV	0xFFFF0000 /* ILP div mask */
#define  BCMA_CC_PMU_CTL_ILP_DIV_SHIFT	16
#define  BCMA_CC_PMU_CTL_PLL_UPD	0x00000400
#define  BCMA_CC_PMU_CTL_NOILPONW	0x00000200 /* No ILP on wait */
#define  BCMA_CC_PMU_CTL_HTREQEN	0x00000100 /* HT req enable */
#define  BCMA_CC_PMU_CTL_ALPREQEN	0x00000080 /* ALP req enable */
#define  BCMA_CC_PMU_CTL_XTALFREQ	0x0000007C /* Crystal freq */
#define  BCMA_CC_PMU_CTL_XTALFREQ_SHIFT	2
#define  BCMA_CC_PMU_CTL_ILPDIVEN	0x00000002 /* ILP div enable */
#define  BCMA_CC_PMU_CTL_LPOSEL		0x00000001 /* LPO sel */
#define BCMA_CC_PMU_CAP			0x0604 /* PMU capabilities */
#define  BCMA_CC_PMU_CAP_REVISION	0x000000FF /* Revision mask */
#define BCMA_CC_PMU_STAT		0x0608 /* PMU status */
#define  BCMA_CC_PMU_STAT_INTPEND	0x00000040 /* Interrupt pending */
#define  BCMA_CC_PMU_STAT_SBCLKST	0x00000030 /* Backplane clock status? */
#define  BCMA_CC_PMU_STAT_HAVEALP	0x00000008 /* ALP available */
#define  BCMA_CC_PMU_STAT_HAVEHT	0x00000004 /* HT available */
#define  BCMA_CC_PMU_STAT_RESINIT	0x00000003 /* Res init */
#define BCMA_CC_PMU_RES_STAT		0x060C /* PMU res status */
#define BCMA_CC_PMU_RES_PEND		0x0610 /* PMU res pending */
#define BCMA_CC_PMU_TIMER		0x0614 /* PMU timer */
#define BCMA_CC_PMU_MINRES_MSK		0x0618 /* PMU min res mask */
#define BCMA_CC_PMU_MAXRES_MSK		0x061C /* PMU max res mask */
#define BCMA_CC_PMU_RES_TABSEL		0x0620 /* PMU res table sel */
#define BCMA_CC_PMU_RES_DEPMSK		0x0624 /* PMU res dep mask */
#define BCMA_CC_PMU_RES_UPDNTM		0x0628 /* PMU res updown timer */
#define BCMA_CC_PMU_RES_TIMER		0x062C /* PMU res timer */
#define BCMA_CC_PMU_CLKSTRETCH		0x0630 /* PMU clockstretch */
#define BCMA_CC_PMU_WATCHDOG		0x0634 /* PMU watchdog */
#define BCMA_CC_PMU_RES_REQTS		0x0640 /* PMU res req timer sel */
#define BCMA_CC_PMU_RES_REQT		0x0644 /* PMU res req timer */
#define BCMA_CC_PMU_RES_REQM		0x0648 /* PMU res req mask */
#define BCMA_CC_CHIPCTL_ADDR		0x0650
#define BCMA_CC_CHIPCTL_DATA		0x0654
#define BCMA_CC_REGCTL_ADDR		0x0658
#define BCMA_CC_REGCTL_DATA		0x065C
#define BCMA_CC_PLLCTL_ADDR		0x0660
#define BCMA_CC_PLLCTL_DATA		0x0664
#define BCMA_CC_SPROM			0x0800 /* SPROM beginning */
/* NAND flash MLC controller registers (corerev >= 38) */
#define BCMA_CC_NAND_REVISION		0x0C00
#define BCMA_CC_NAND_CMD_START		0x0C04
#define BCMA_CC_NAND_CMD_ADDR_X		0x0C08
#define BCMA_CC_NAND_CMD_ADDR		0x0C0C
#define BCMA_CC_NAND_CMD_END_ADDR	0x0C10
#define BCMA_CC_NAND_CS_NAND_SELECT	0x0C14
#define BCMA_CC_NAND_CS_NAND_XOR	0x0C18
#define BCMA_CC_NAND_SPARE_RD0		0x0C20
#define BCMA_CC_NAND_SPARE_RD4		0x0C24
#define BCMA_CC_NAND_SPARE_RD8		0x0C28
#define BCMA_CC_NAND_SPARE_RD12		0x0C2C
#define BCMA_CC_NAND_SPARE_WR0		0x0C30
#define BCMA_CC_NAND_SPARE_WR4		0x0C34
#define BCMA_CC_NAND_SPARE_WR8		0x0C38
#define BCMA_CC_NAND_SPARE_WR12		0x0C3C
#define BCMA_CC_NAND_ACC_CONTROL	0x0C40
#define BCMA_CC_NAND_CONFIG		0x0C48
#define BCMA_CC_NAND_TIMING_1		0x0C50
#define BCMA_CC_NAND_TIMING_2		0x0C54
#define BCMA_CC_NAND_SEMAPHORE		0x0C58
#define BCMA_CC_NAND_DEVID		0x0C60
#define BCMA_CC_NAND_DEVID_X		0x0C64
#define BCMA_CC_NAND_BLOCK_LOCK_STATUS	0x0C68
#define BCMA_CC_NAND_INTFC_STATUS	0x0C6C
#define BCMA_CC_NAND_ECC_CORR_ADDR_X	0x0C70
#define BCMA_CC_NAND_ECC_CORR_ADDR	0x0C74
#define BCMA_CC_NAND_ECC_UNC_ADDR_X	0x0C78
#define BCMA_CC_NAND_ECC_UNC_ADDR	0x0C7C
#define BCMA_CC_NAND_READ_ERROR_COUNT	0x0C80
#define BCMA_CC_NAND_CORR_STAT_THRESHOLD	0x0C84
#define BCMA_CC_NAND_READ_ADDR_X	0x0C90
#define BCMA_CC_NAND_READ_ADDR		0x0C94
#define BCMA_CC_NAND_PAGE_PROGRAM_ADDR_X	0x0C98
#define BCMA_CC_NAND_PAGE_PROGRAM_ADDR	0x0C9C
#define BCMA_CC_NAND_COPY_BACK_ADDR_X	0x0CA0
#define BCMA_CC_NAND_COPY_BACK_ADDR	0x0CA4
#define BCMA_CC_NAND_BLOCK_ERASE_ADDR_X	0x0CA8
#define BCMA_CC_NAND_BLOCK_ERASE_ADDR	0x0CAC
#define BCMA_CC_NAND_INV_READ_ADDR_X	0x0CB0
#define BCMA_CC_NAND_INV_READ_ADDR	0x0CB4
#define BCMA_CC_NAND_BLK_WR_PROTECT	0x0CC0
#define BCMA_CC_NAND_ACC_CONTROL_CS1	0x0CD0
#define BCMA_CC_NAND_CONFIG_CS1		0x0CD4
#define BCMA_CC_NAND_TIMING_1_CS1	0x0CD8
#define BCMA_CC_NAND_TIMING_2_CS1	0x0CDC
#define BCMA_CC_NAND_SPARE_RD16		0x0D30
#define BCMA_CC_NAND_SPARE_RD20		0x0D34
#define BCMA_CC_NAND_SPARE_RD24		0x0D38
#define BCMA_CC_NAND_SPARE_RD28		0x0D3C
#define BCMA_CC_NAND_CACHE_ADDR		0x0D40
#define BCMA_CC_NAND_CACHE_DATA		0x0D44
#define BCMA_CC_NAND_CTRL_CONFIG	0x0D48
#define BCMA_CC_NAND_CTRL_STATUS	0x0D4C

/* Divider allocation in 4716/47162/5356 */
#define BCMA_CC_PMU5_MAINPLL_CPU	1
#define BCMA_CC_PMU5_MAINPLL_MEM	2
#define BCMA_CC_PMU5_MAINPLL_SSB	3

/* PLL usage in 4716/47162 */
#define BCMA_CC_PMU4716_MAINPLL_PLL0	12

/* PLL usage in 5356/5357 */
#define BCMA_CC_PMU5356_MAINPLL_PLL0	0
#define BCMA_CC_PMU5357_MAINPLL_PLL0	0

/* 4706 PMU */
#define BCMA_CC_PMU4706_MAINPLL_PLL0	0
#define BCMA_CC_PMU6_4706_PROCPLL_OFF	4	/* The CPU PLL */
#define  BCMA_CC_PMU6_4706_PROC_P2DIV_MASK	0x000f0000
#define  BCMA_CC_PMU6_4706_PROC_P2DIV_SHIFT	16
#define  BCMA_CC_PMU6_4706_PROC_P1DIV_MASK	0x0000f000
#define  BCMA_CC_PMU6_4706_PROC_P1DIV_SHIFT	12
#define  BCMA_CC_PMU6_4706_PROC_NDIV_INT_MASK	0x00000ff8
#define  BCMA_CC_PMU6_4706_PROC_NDIV_INT_SHIFT	3
#define  BCMA_CC_PMU6_4706_PROC_NDIV_MODE_MASK	0x00000007
#define  BCMA_CC_PMU6_4706_PROC_NDIV_MODE_SHIFT	0

/* ALP clock on pre-PMU chips */
#define BCMA_CC_PMU_ALP_CLOCK		20000000
/* HT clock for systems with PMU-enabled chipcommon */
#define BCMA_CC_PMU_HT_CLOCK		80000000

/* PMU rev 5 (& 6) */
#define BCMA_CC_PPL_P1P2_OFF		0
#define BCMA_CC_PPL_P1_MASK		0x0f000000
#define BCMA_CC_PPL_P1_SHIFT		24
#define BCMA_CC_PPL_P2_MASK		0x00f00000
#define BCMA_CC_PPL_P2_SHIFT		20
#define BCMA_CC_PPL_M14_OFF		1
#define BCMA_CC_PPL_MDIV_MASK		0x000000ff
#define BCMA_CC_PPL_MDIV_WIDTH		8
#define BCMA_CC_PPL_NM5_OFF		2
#define BCMA_CC_PPL_NDIV_MASK		0xfff00000
#define BCMA_CC_PPL_NDIV_SHIFT		20
#define BCMA_CC_PPL_FMAB_OFF		3
#define BCMA_CC_PPL_MRAT_MASK		0xf0000000
#define BCMA_CC_PPL_MRAT_SHIFT		28
#define BCMA_CC_PPL_ABRAT_MASK		0x08000000
#define BCMA_CC_PPL_ABRAT_SHIFT		27
#define BCMA_CC_PPL_FDIV_MASK		0x07ffffff
#define BCMA_CC_PPL_PLLCTL_OFF		4
#define BCMA_CC_PPL_PCHI_OFF		5
#define BCMA_CC_PPL_PCHI_MASK		0x0000003f

#define BCMA_CC_PMU_PLL_CTL0		0
#define BCMA_CC_PMU_PLL_CTL1		1
#define BCMA_CC_PMU_PLL_CTL2		2
#define BCMA_CC_PMU_PLL_CTL3		3
#define BCMA_CC_PMU_PLL_CTL4		4
#define BCMA_CC_PMU_PLL_CTL5		5

#define BCMA_CC_PMU1_PLL0_PC0_P1DIV_MASK	0x00f00000
#define BCMA_CC_PMU1_PLL0_PC0_P1DIV_SHIFT	20

#define BCMA_CC_PMU1_PLL0_PC2_NDIV_INT_MASK	0x1ff00000
#define BCMA_CC_PMU1_PLL0_PC2_NDIV_INT_SHIFT	20

/* BCM4331 ChipControl numbers. */
#define BCMA_CHIPCTL_4331_BT_COEXIST		BIT(0)	/* 0 disable */
#define BCMA_CHIPCTL_4331_SECI			BIT(1)	/* 0 SECI is disabled (JATG functional) */
#define BCMA_CHIPCTL_4331_EXT_LNA		BIT(2)	/* 0 disable */
#define BCMA_CHIPCTL_4331_SPROM_GPIO13_15	BIT(3)	/* sprom/gpio13-15 mux */
#define BCMA_CHIPCTL_4331_EXTPA_EN		BIT(4)	/* 0 ext pa disable, 1 ext pa enabled */
#define BCMA_CHIPCTL_4331_GPIOCLK_ON_SPROMCS	BIT(5)	/* set drive out GPIO_CLK on sprom_cs pin */
#define BCMA_CHIPCTL_4331_PCIE_MDIO_ON_SPROMCS	BIT(6)	/* use sprom_cs pin as PCIE mdio interface */
#define BCMA_CHIPCTL_4331_EXTPA_ON_GPIO2_5	BIT(7)	/* aband extpa will be at gpio2/5 and sprom_dout */
#define BCMA_CHIPCTL_4331_OVR_PIPEAUXCLKEN	BIT(8)	/* override core control on pipe_AuxClkEnable */
#define BCMA_CHIPCTL_4331_OVR_PIPEAUXPWRDOWN	BIT(9)	/* override core control on pipe_AuxPowerDown */
#define BCMA_CHIPCTL_4331_PCIE_AUXCLKEN		BIT(10)	/* pcie_auxclkenable */
#define BCMA_CHIPCTL_4331_PCIE_PIPE_PLLDOWN	BIT(11)	/* pcie_pipe_pllpowerdown */
#define BCMA_CHIPCTL_4331_EXTPA_EN2		BIT(12)	/* 0 ext pa disable, 1 ext pa enabled */
#define BCMA_CHIPCTL_4331_BT_SHD0_ON_GPIO4	BIT(16)	/* enable bt_shd0 at gpio4 */
#define BCMA_CHIPCTL_4331_BT_SHD1_ON_GPIO5	BIT(17)	/* enable bt_shd1 at gpio5 */

/* 43224 chip-specific ChipControl register bits */
#define BCMA_CCTRL_43224_GPIO_TOGGLE		0x8000		/* gpio[3:0] pins as btcoex or s/w gpio */
#define BCMA_CCTRL_43224A0_12MA_LED_DRIVE	0x00F000F0	/* 12 mA drive strength */
#define BCMA_CCTRL_43224B0_12MA_LED_DRIVE	0xF0		/* 12 mA drive strength for later 43224s */

/* 4313 Chip specific ChipControl register bits */
#define BCMA_CCTRL_4313_12MA_LED_DRIVE		0x00000007	/* 12 mA drive strengh for later 4313 */

/* BCM5357 ChipControl register bits */
#define BCMA_CHIPCTL_5357_EXTPA			BIT(14)
#define BCMA_CHIPCTL_5357_ANT_MUX_2O3		BIT(15)
#define BCMA_CHIPCTL_5357_NFLASH		BIT(16)
#define BCMA_CHIPCTL_5357_I2S_PINS_ENABLE	BIT(18)
#define BCMA_CHIPCTL_5357_I2CSPI_PINS_ENABLE	BIT(19)

/* Data for the PMU, if available.
 * Check availability with ((struct bcma_chipcommon)->capabilities & BCMA_CC_CAP_PMU)
 */
struct bcma_chipcommon_pmu {
	u8 rev;			/* PMU revision */
	u32 crystalfreq;	/* The active crystal frequency (in kHz) */
};

#ifdef CONFIG_BCMA_DRIVER_MIPS
struct bcma_pflash {
	u8 buswidth;
	u32 window;
	u32 window_size;
};

#ifdef CONFIG_BCMA_SFLASH
struct bcma_sflash {
	bool present;
	u32 window;
	u32 blocksize;
	u16 numblocks;
	u32 size;

	struct mtd_info *mtd;
};
#endif

#ifdef CONFIG_BCMA_NFLASH
struct mtd_info;

struct bcma_nflash {
	bool present;

	struct mtd_info *mtd;
};
#endif

struct bcma_serial_port {
	void *regs;
	unsigned long clockspeed;
	unsigned int irq;
	unsigned int baud_base;
	unsigned int reg_shift;
};
#endif /* CONFIG_BCMA_DRIVER_MIPS */

struct bcma_drv_cc {
	struct bcma_device *core;
	u32 status;
	u32 capabilities;
	u32 capabilities_ext;
	u8 setup_done:1;
	/* Fast Powerup Delay constant */
	u16 fast_pwrup_delay;
	struct bcma_chipcommon_pmu pmu;
#ifdef CONFIG_BCMA_DRIVER_MIPS
	struct bcma_pflash pflash;
#ifdef CONFIG_BCMA_SFLASH
	struct bcma_sflash sflash;
#endif
#ifdef CONFIG_BCMA_NFLASH
	struct bcma_nflash nflash;
#endif

	int nr_serial_ports;
	struct bcma_serial_port serial_ports[4];
#endif /* CONFIG_BCMA_DRIVER_MIPS */

	/* Lock for GPIO register access. */
	spinlock_t gpio_lock;
};

/* Register access */
#define bcma_cc_read32(cc, offset) \
	bcma_read32((cc)->core, offset)
#define bcma_cc_write32(cc, offset, val) \
	bcma_write32((cc)->core, offset, val)

#define bcma_cc_mask32(cc, offset, mask) \
	bcma_cc_write32(cc, offset, bcma_cc_read32(cc, offset) & (mask))
#define bcma_cc_set32(cc, offset, set) \
	bcma_cc_write32(cc, offset, bcma_cc_read32(cc, offset) | (set))
#define bcma_cc_maskset32(cc, offset, mask, set) \
	bcma_cc_write32(cc, offset, (bcma_cc_read32(cc, offset) & (mask)) | (set))

extern void bcma_core_chipcommon_init(struct bcma_drv_cc *cc);

extern void bcma_chipco_suspend(struct bcma_drv_cc *cc);
extern void bcma_chipco_resume(struct bcma_drv_cc *cc);

void bcma_chipco_bcm4331_ext_pa_lines_ctl(struct bcma_drv_cc *cc, bool enable);

extern void bcma_chipco_watchdog_timer_set(struct bcma_drv_cc *cc,
					  u32 ticks);

void bcma_chipco_irq_mask(struct bcma_drv_cc *cc, u32 mask, u32 value);

u32 bcma_chipco_irq_status(struct bcma_drv_cc *cc, u32 mask);

/* Chipcommon GPIO pin access. */
u32 bcma_chipco_gpio_in(struct bcma_drv_cc *cc, u32 mask);
u32 bcma_chipco_gpio_out(struct bcma_drv_cc *cc, u32 mask, u32 value);
u32 bcma_chipco_gpio_outen(struct bcma_drv_cc *cc, u32 mask, u32 value);
u32 bcma_chipco_gpio_control(struct bcma_drv_cc *cc, u32 mask, u32 value);
u32 bcma_chipco_gpio_intmask(struct bcma_drv_cc *cc, u32 mask, u32 value);
u32 bcma_chipco_gpio_polarity(struct bcma_drv_cc *cc, u32 mask, u32 value);
u32 bcma_chipco_gpio_pullup(struct bcma_drv_cc *cc, u32 mask, u32 value);
u32 bcma_chipco_gpio_pulldown(struct bcma_drv_cc *cc, u32 mask, u32 value);

/* PMU support */
extern void bcma_pmu_init(struct bcma_drv_cc *cc);

extern void bcma_chipco_pll_write(struct bcma_drv_cc *cc, u32 offset,
				  u32 value);
extern void bcma_chipco_pll_maskset(struct bcma_drv_cc *cc, u32 offset,
				    u32 mask, u32 set);
extern void bcma_chipco_chipctl_maskset(struct bcma_drv_cc *cc,
					u32 offset, u32 mask, u32 set);
extern void bcma_chipco_regctl_maskset(struct bcma_drv_cc *cc,
				       u32 offset, u32 mask, u32 set);
extern void bcma_pmu_spuravoid_pllupdate(struct bcma_drv_cc *cc, int spuravoid);

#endif /* LINUX_BCMA_DRIVER_CC_H_ */
