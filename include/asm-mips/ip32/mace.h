/*
 * Definitions for the SGI MACE (Multimedia, Audio and Communications Engine)
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 Harald Koerfgen
 * Copyright (C) 2004 Ladislav Michl
 */

#ifndef __ASM_MACE_H__
#define __ASM_MACE_H__

/*
 * Address map
 */
#define MACE_BASE	0x1f000000	/* physical */

#undef BIT
#define BIT(x) (1UL << (x))

/*
 * PCI interface
 */
struct mace_pci {
	volatile unsigned int error_addr;
	volatile unsigned int error;
#define MACEPCI_ERROR_MASTER_ABORT		BIT(31)
#define MACEPCI_ERROR_TARGET_ABORT		BIT(30)
#define MACEPCI_ERROR_DATA_PARITY_ERR		BIT(29)
#define MACEPCI_ERROR_RETRY_ERR			BIT(28)
#define MACEPCI_ERROR_ILLEGAL_CMD		BIT(27)
#define MACEPCI_ERROR_SYSTEM_ERR		BIT(26)
#define MACEPCI_ERROR_INTERRUPT_TEST		BIT(25)
#define MACEPCI_ERROR_PARITY_ERR		BIT(24)
#define MACEPCI_ERROR_OVERRUN			BIT(23)
#define MACEPCI_ERROR_RSVD			BIT(22)
#define MACEPCI_ERROR_MEMORY_ADDR		BIT(21)
#define MACEPCI_ERROR_CONFIG_ADDR		BIT(20)
#define MACEPCI_ERROR_MASTER_ABORT_ADDR_VALID	BIT(19)
#define MACEPCI_ERROR_TARGET_ABORT_ADDR_VALID	BIT(18)
#define MACEPCI_ERROR_DATA_PARITY_ADDR_VALID	BIT(17)
#define MACEPCI_ERROR_RETRY_ADDR_VALID		BIT(16)
#define MACEPCI_ERROR_SIG_TABORT		BIT(4)
#define MACEPCI_ERROR_DEVSEL_MASK		0xc0
#define MACEPCI_ERROR_DEVSEL_FAST		0
#define MACEPCI_ERROR_DEVSEL_MED		0x40
#define MACEPCI_ERROR_DEVSEL_SLOW		0x80
#define MACEPCI_ERROR_FBB			BIT(1)
#define MACEPCI_ERROR_66MHZ			BIT(0)
	volatile unsigned int control;
#define MACEPCI_CONTROL_INT(x)			BIT(x)
#define MACEPCI_CONTROL_INT_MASK		0xff
#define MACEPCI_CONTROL_SERR_ENA		BIT(8)
#define MACEPCI_CONTROL_ARB_N6			BIT(9)
#define MACEPCI_CONTROL_PARITY_ERR		BIT(10)
#define MACEPCI_CONTROL_MRMRA_ENA		BIT(11)
#define MACEPCI_CONTROL_ARB_N3			BIT(12)
#define MACEPCI_CONTROL_ARB_N4			BIT(13)
#define MACEPCI_CONTROL_ARB_N5			BIT(14)
#define MACEPCI_CONTROL_PARK_LIU		BIT(15)
#define MACEPCI_CONTROL_INV_INT(x)		BIT(16+x)
#define MACEPCI_CONTROL_INV_INT_MASK		0x00ff0000
#define MACEPCI_CONTROL_OVERRUN_INT		BIT(24)
#define MACEPCI_CONTROL_PARITY_INT		BIT(25)
#define MACEPCI_CONTROL_SERR_INT		BIT(26)
#define MACEPCI_CONTROL_IT_INT			BIT(27)
#define MACEPCI_CONTROL_RE_INT			BIT(28)
#define MACEPCI_CONTROL_DPED_INT		BIT(29)
#define MACEPCI_CONTROL_TAR_INT			BIT(30)
#define MACEPCI_CONTROL_MAR_INT			BIT(31)
	volatile unsigned int rev;
	unsigned int _pad[0xcf8/4 - 4];
	volatile unsigned int config_addr;
	union {
		volatile unsigned char b[4];
		volatile unsigned short w[2];
		volatile unsigned int l;
	} config_data;
};
#define MACEPCI_LOW_MEMORY		0x1a000000
#define MACEPCI_LOW_IO			0x18000000
#define MACEPCI_SWAPPED_VIEW		0
#define MACEPCI_NATIVE_VIEW		0x40000000
#define MACEPCI_IO			0x80000000
#define MACEPCI_HI_MEMORY		0x280000000
#define MACEPCI_HI_IO			0x100000000

/*
 * Video interface
 */
struct mace_video {
	unsigned long xxx;	/* later... */
};

/*
 * Ethernet interface
 */
struct mace_ethernet {
	volatile unsigned long mac_ctrl;
	volatile unsigned long int_stat;
	volatile unsigned long dma_ctrl;
	volatile unsigned long timer;
	volatile unsigned long tx_int_al;
	volatile unsigned long rx_int_al;
	volatile unsigned long tx_info;
	volatile unsigned long tx_info_al;
	volatile unsigned long rx_buff;
	volatile unsigned long rx_buff_al1;
	volatile unsigned long rx_buff_al2;
	volatile unsigned long diag;
	volatile unsigned long phy_data;
	volatile unsigned long phy_regs;
	volatile unsigned long phy_trans_go;
	volatile unsigned long backoff_seed;
	/*===================================*/
	volatile unsigned long imq_reserved[4];
	volatile unsigned long mac_addr;
	volatile unsigned long mac_addr2;
	volatile unsigned long mcast_filter;
	volatile unsigned long tx_ring_base;
	/* Following are read-only registers for debugging */
	volatile unsigned long tx_pkt1_hdr;
	volatile unsigned long tx_pkt1_ptr[3];
	volatile unsigned long tx_pkt2_hdr;
	volatile unsigned long tx_pkt2_ptr[3];
	/*===================================*/
	volatile unsigned long rx_fifo;
};

/*
 * Peripherals
 */

/* Audio registers */
struct mace_audio {
	volatile unsigned long control;
	volatile unsigned long codec_control;		/* codec status control */
	volatile unsigned long codec_mask;		/* codec status input mask */
	volatile unsigned long codec_read;		/* codec status read data */
	struct {
		volatile unsigned long control;		/* channel control */
		volatile unsigned long read_ptr;	/* channel read pointer */
		volatile unsigned long write_ptr;	/* channel write pointer */
		volatile unsigned long depth;		/* channel depth */
	} chan[3];
};

/* ISA Control and DMA registers */
struct mace_isactrl {
	volatile unsigned long ringbase;
#define MACEISA_RINGBUFFERS_SIZE	(8 * 4096)

	volatile unsigned long misc;
#define MACEISA_FLASH_WE		BIT(0)	/* 1=> Enable FLASH writes */
#define MACEISA_PWD_CLEAR		BIT(1)	/* 1=> PWD CLEAR jumper detected */
#define MACEISA_NIC_DEASSERT		BIT(2)
#define MACEISA_NIC_DATA		BIT(3)
#define MACEISA_LED_RED			BIT(4)	/* 0=> Illuminate red LED */
#define MACEISA_LED_GREEN		BIT(5)	/* 0=> Illuminate green LED */
#define MACEISA_DP_RAM_ENABLE		BIT(6)

	volatile unsigned long istat;
	volatile unsigned long imask;
#define MACEISA_AUDIO_SW_INT		BIT(0)
#define MACEISA_AUDIO_SC_INT		BIT(1)
#define MACEISA_AUDIO1_DMAT_INT		BIT(2)
#define MACEISA_AUDIO1_OF_INT		BIT(3)
#define MACEISA_AUDIO2_DMAT_INT		BIT(4)
#define MACEISA_AUDIO2_MERR_INT		BIT(5)
#define MACEISA_AUDIO3_DMAT_INT		BIT(6)
#define MACEISA_AUDIO3_MERR_INT		BIT(7)
#define MACEISA_RTC_INT			BIT(8)
#define MACEISA_KEYB_INT		BIT(9)
#define MACEISA_KEYB_POLL_INT		BIT(10)
#define MACEISA_MOUSE_INT		BIT(11)
#define MACEISA_MOUSE_POLL_INT		BIT(12)
#define MACEISA_TIMER0_INT		BIT(13)
#define MACEISA_TIMER1_INT		BIT(14)
#define MACEISA_TIMER2_INT		BIT(15)
#define MACEISA_PARALLEL_INT		BIT(16)
#define MACEISA_PAR_CTXA_INT		BIT(17)
#define MACEISA_PAR_CTXB_INT		BIT(18)
#define MACEISA_PAR_MERR_INT		BIT(19)
#define MACEISA_SERIAL1_INT		BIT(20)
#define MACEISA_SERIAL1_TDMAT_INT	BIT(21)
#define MACEISA_SERIAL1_TDMAPR_INT	BIT(22)
#define MACEISA_SERIAL1_TDMAME_INT	BIT(23)
#define MACEISA_SERIAL1_RDMAT_INT	BIT(24)
#define MACEISA_SERIAL1_RDMAOR_INT	BIT(25)
#define MACEISA_SERIAL2_INT		BIT(26)
#define MACEISA_SERIAL2_TDMAT_INT	BIT(27)
#define MACEISA_SERIAL2_TDMAPR_INT	BIT(28)
#define MACEISA_SERIAL2_TDMAME_INT	BIT(29)
#define MACEISA_SERIAL2_RDMAT_INT	BIT(30)
#define MACEISA_SERIAL2_RDMAOR_INT	BIT(31)

	volatile unsigned long _pad[0x2000/8 - 4];

	volatile unsigned long dp_ram[0x400];
};

/* Keyboard & Mouse registers
 * -> drivers/input/serio/maceps2.c */
struct mace_ps2port {
	volatile unsigned long tx;
	volatile unsigned long rx;
	volatile unsigned long control;
	volatile unsigned long status;
};

struct mace_ps2 {
	struct mace_ps2port keyb;
	struct mace_ps2port mouse;
};

/* I2C registers
 * -> drivers/i2c/algos/i2c-algo-sgi.c */
struct mace_i2c {
	volatile unsigned long config;
#define MACEI2C_RESET           BIT(0)
#define MACEI2C_FAST            BIT(1)
#define MACEI2C_DATA_OVERRIDE   BIT(2)
#define MACEI2C_CLOCK_OVERRIDE  BIT(3)
#define MACEI2C_DATA_STATUS     BIT(4)
#define MACEI2C_CLOCK_STATUS    BIT(5)
	volatile unsigned long control;
	volatile unsigned long data;
};

/* Timer registers */
typedef union {
	volatile unsigned long ust_msc;
	struct reg {
		volatile unsigned int ust;
		volatile unsigned int msc;
	} reg;
} timer_reg;

struct mace_timers {
	volatile unsigned long ust;
#define MACE_UST_PERIOD_NS	960

	volatile unsigned long compare1;
	volatile unsigned long compare2;
	volatile unsigned long compare3;

	timer_reg audio_in;
	timer_reg audio_out1;
	timer_reg audio_out2;
	timer_reg video_in1;
	timer_reg video_in2;
	timer_reg video_out;
};

struct mace_perif {
	struct mace_audio audio;
	char _pad0[0x10000 - sizeof(struct mace_audio)];

	struct mace_isactrl ctrl;
	char _pad1[0x10000 - sizeof(struct mace_isactrl)];

	struct mace_ps2 ps2;
	char _pad2[0x10000 - sizeof(struct mace_ps2)];

	struct mace_i2c i2c;
	char _pad3[0x10000 - sizeof(struct mace_i2c)];

	struct mace_timers timers;
	char _pad4[0x10000 - sizeof(struct mace_timers)];
};


/*
 * ISA peripherals
 */

/* Parallel port */
struct mace_parallel {	/* later... */
};

struct mace_ecp1284 {	/* later... */
};

/* Serial port */
struct mace_serial {
	volatile unsigned long xxx;	/* later... */
};

struct mace_isa {
	struct mace_parallel parallel;
	char _pad1[0x8000 - sizeof(struct mace_parallel)];

	struct mace_ecp1284 ecp1284;
	char _pad2[0x8000 - sizeof(struct mace_ecp1284)];

	struct mace_serial serial1;
	char _pad3[0x8000 - sizeof(struct mace_serial)];

	struct mace_serial serial2;
	char _pad4[0x8000 - sizeof(struct mace_serial)];

	volatile unsigned char rtc[0x10000];
};

struct sgi_mace {
	char _reserved[0x80000];

	struct mace_pci pci;
	char _pad0[0x80000 - sizeof(struct mace_pci)];

	struct mace_video video_in1;
	char _pad1[0x80000 - sizeof(struct mace_video)];

	struct mace_video video_in2;
	char _pad2[0x80000 - sizeof(struct mace_video)];

	struct mace_video video_out;
	char _pad3[0x80000 - sizeof(struct mace_video)];

	struct mace_ethernet eth;
	char _pad4[0x80000 - sizeof(struct mace_ethernet)];

	struct mace_perif perif;
	char _pad5[0x80000 - sizeof(struct mace_perif)];

	struct mace_isa isa;
	char _pad6[0x80000 - sizeof(struct mace_isa)];
};

extern struct sgi_mace *mace;

#endif /* __ASM_MACE_H__ */
