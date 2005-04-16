/*
   sound/oss/aedsp16.c

   Audio Excel DSP 16 software configuration routines
   Copyright (C) 1995,1996,1997,1998  Riccardo Facchetti (fizban@tin.it)

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 */
/*
 * Include the main OSS Lite header file. It include all the os, OSS Lite, etc
 * headers needed by this source.
 */
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include "sound_config.h"

/*
 * Sanity checks
 */

#if defined(CONFIG_SOUND_AEDSP16_SBPRO) && defined(CONFIG_SOUND_AEDSP16_MSS)
#error You have to enable only one of the MSS and SBPRO emulations.
#endif

/*

   READ THIS

   This module started to configure the Audio Excel DSP 16 Sound Card.
   Now works with the SC-6000 (old aedsp16) and new SC-6600 based cards.

   NOTE: I have NO idea about Audio Excel DSP 16 III. If someone owns this
   audio card and want to see the kernel support for it, please contact me.

   Audio Excel DSP 16 is an SB pro II, Microsoft Sound System and MPU-401
   compatible card.
   It is software-only configurable (no jumpers to hard-set irq/dma/mpu-irq),
   so before this module, the only way to configure the DSP under linux was
   boot the MS-DOS loading the sound.sys device driver (this driver soft-
   configure the sound board hardware by massaging someone of its registers),
   and then ctrl-alt-del to boot linux with the DSP configured by the DOS
   driver.

   This module works configuring your Audio Excel DSP 16's irq, dma and
   mpu-401-irq. The OSS Lite routines rely on the fact that if the
   hardware is there, they can detect it. The problem with AEDSP16 is
   that no hardware can be found by the probe routines if the sound card
   is not configured properly. Sometimes the kernel probe routines can find
   an SBPRO even when the card is not configured (this is the standard setup
   of the card), but the SBPRO emulation don't work well if the card is not
   properly initialized. For this reason

   aedsp16_init_board()

   routine is called before the OSS Lite probe routines try to detect the
   hardware.

   NOTE (READ THE NOTE TOO, IT CONTAIN USEFUL INFORMATIONS)

   NOTE: Now it works with SC-6000 and SC-6600 based audio cards. The new cards
   have no jumper switch at all. No more WSS or MPU-401 I/O port switches. They
   have to be configured by software.

   NOTE: The driver is merged with the new OSS Lite sound driver. It works
   as a lowlevel driver.

   The Audio Excel DSP 16 Sound Card emulates both SBPRO and MSS;
   the OSS Lite sound driver can be configured for SBPRO and MSS cards
   at the same time, but the aedsp16 can't be two cards!!
   When we configure it, we have to choose the SBPRO or the MSS emulation
   for AEDSP16. We also can install a *REAL* card of the other type (see [1]).

   NOTE: If someone can test the combination AEDSP16+MSS or AEDSP16+SBPRO
   please let me know if it works.

   The MPU-401 support can be compiled in together with one of the other
   two operating modes.

   NOTE: This is something like plug-and-play: we have only to plug
   the AEDSP16 board in the socket, and then configure and compile
   a kernel that uses the AEDSP16 software configuration capability.
   No jumper setting is needed!

   For example, if you want AEDSP16 to be an SBPro, on irq 10, dma 3
   you have just to make config the OSS Lite package, configuring
   the AEDSP16 sound card, then activating the SBPro emulation mode
   and at last configuring IRQ and DMA.
   Compile the kernel and run it.

   NOTE: This means for SC-6000 cards that you can choose irq and dma,
   but not the I/O addresses. To change I/O addresses you have to set
   them with jumpers. For SC-6600 cards you have no jumpers so you have
   to set up your full card configuration in the make config.

   You can change the irq/dma/mirq settings WITHOUT THE NEED to open
   your computer and massage the jumpers (there are no irq/dma/mirq
   jumpers to be configured anyway, only I/O BASE values have to be
   configured with jumpers)

   For some ununderstandable reason, the card default of irq 7, dma 1,
   don't work for me. Seems to be an IRQ or DMA conflict. Under heavy
   HDD work, the kernel start to erupt out a lot of messages like:

   'Sound: DMA timed out - IRQ/DRQ config error?'

   For what I can say, I have NOT any conflict at irq 7 (under linux I'm
   using the lp polling driver), and dma line 1 is unused as stated by
   /proc/dma. I can suppose this is a bug of AEDSP16. I know my hardware so
   I'm pretty sure I have not any conflict, but may be I'm wrong. Who knows!
   Anyway a setting of irq 10, dma 3 works really fine.

   NOTE: if someone can use AEDSP16 with irq 7, dma 1, please let me know
   the emulation mode, all the installed hardware and the hardware
   configuration (irq and dma settings of all the hardware).

   This init module should work with SBPRO+MSS, when one of the two is
   the AEDSP16 emulation and the other the real card. (see [1])
   For example:

   AEDSP16 (0x220) in SBPRO emu (0x220) + real MSS + other
   AEDSP16 (0x220) in MSS emu + real SBPRO (0x240) + other

   MPU401 should work. (see [2])

   [1]
       ---
       Date: Mon, 29 Jul 1997 08:35:40 +0100
       From: Mr S J Greenaway <sjg95@unixfe.rl.ac.uk>

       [...]
       Just to let you know got my Audio Excel (emulating a MSS) working
       with my original SB16, thanks for the driver!
       [...]
       ---

   [2] Not tested by me for lack of hardware.

   TODO, WISHES AND TECH

   - About I/O ports allocation -

   Request the 2x0h region (port base) in any case if we are using this card.

   NOTE: the "aedsp16 (base)" string with which we are requesting the aedsp16
   port base region (see code) does not mean necessarily that we are emulating
   sbpro.  Even if this region is the sbpro I/O ports region, we use this
   region to access the control registers of the card, and if emulating
   sbpro, I/O sbpro registers too. If we are emulating MSS, the sbpro
   registers are not used, in no way, to emulate an sbpro: they are
   used only for configuration purposes.

   Started Fri Mar 17 16:13:18 MET 1995

   v0.1 (ALPHA, was an user-level program called AudioExcelDSP16.c)
   - Initial code.
   v0.2 (ALPHA)
   - Cleanups.
   - Integrated with Linux voxware v 2.90-2 kernel sound driver.
   - SoundBlaster Pro mode configuration.
   - Microsoft Sound System mode configuration.
   - MPU-401 mode configuration.
   v0.3 (ALPHA)
   - Cleanups.
   - Rearranged the code to let aedsp16_init_board be more general.
   - Erased the REALLY_SLOW_IO. We don't need it. Erased the linux/io.h
   inclusion too. We rely on os.h
   - Used the  to get a variable
   len string (we are not sure about the len of Copyright string).
   This works with any SB and compatible.
   - Added the code to request_region at device init (should go in
   the main body of voxware).
   v0.4 (BETA)
   - Better configure.c patch for aedsp16 configuration (better
   logic of inclusion of AEDSP16 support)
   - Modified the conditional compilation to better support more than
   one sound card of the emulated type (read the NOTES above)
   - Moved the sb init routine from the attach to the very first
   probe in sb_card.c
   - Rearrangements and cleanups
   - Wiped out some unnecessary code and variables: this is kernel
   code so it is better save some TEXT and DATA
   - Fixed the request_region code. We must allocate the aedsp16 (sbpro)
   I/O ports in any case because they are used to access the DSP
   configuration registers and we can not allow anyone to get them.
   v0.5
   - cleanups on comments
   - prep for diffs against v3.0-proto-950402
   v0.6
   - removed the request_region()s when compiling the MODULE sound.o
   because we are not allowed (by the actual voxware structure) to
   release_region()
   v0.7 (pre ALPHA, not distributed)
   - started porting this module to kernel 1.3.84. Dummy probe/attach
   routines.
   v0.8 (ALPHA)
   - attached all the init routines.
   v0.9 (BETA)
   - Integrated with linux-pre2.0.7
   - Integrated with configuration scripts.
   - Cleaned up and beautyfied the code.
   v0.9.9 (BETA)
   - Thanks to Piercarlo Grandi: corrected the conditonal compilation code.
     Now only the code configured is compiled in, with some memory saving.
   v0.9.10
   - Integration into the sound/lowlevel/ section of the sound driver.
   - Re-organized the code.
   v0.9.11 (not distributed)
   - Rewritten the init interface-routines to initialize the AEDSP16 in
     one shot.
   - More cosmetics.
   - SC-6600 support.
   - More soft/hard configuration.
   v0.9.12
   - Refined the v0.9.11 code with conditional compilation to distinguish
     between SC-6000 and SC-6600 code.
   v1.0.0
   - Prep for merging with OSS Lite and Linux kernel 2.1.13
   - Corrected a bug in request/check/release region calls (thanks to the
     new kernel exception handling).
   v1.1
   - Revamped for integration with new modularized sound drivers: to enhance
     the flexibility of modular version, I have removed all the conditional
     compilation for SBPRO, MPU and MSS code. Now it is all managed with
     the ae_config structure.
   v1.2
   - Module informations added.
   - Removed aedsp16_delay_10msec(), now using mdelay(10)
   - All data and funcs moved to .*.init section.
   v1.3
   Arnaldo Carvalho de Melo <acme@conectiva.com.br> - 2000/09/27
   - got rid of check_region

   Known Problems:
   - Audio Excel DSP 16 III don't work with this driver.

   Credits:
   Many thanks to Gerald Britton <gbritton@CapAccess.org>. He helped me a
   lot in testing the 0.9.11 and 0.9.12 versions of this driver.

 */


#define VERSION "1.3"		/* Version of Audio Excel DSP 16 driver */

#undef	AEDSP16_DEBUG 		/* Define this to 1 to enable debug code     */
#undef	AEDSP16_DEBUG_MORE 	/* Define this to 1 to enable more debug     */
#undef	AEDSP16_INFO 		/* Define this to 1 to enable info code      */

#if defined(AEDSP16_DEBUG)
# define DBG(x)  printk x
# if defined(AEDSP16_DEBUG_MORE)
#  define DBG1(x) printk x
# else
#  define DBG1(x)
# endif
#else
# define DBG(x)
# define DBG1(x)
#endif

/*
 * Misc definitions
 */
#define TRUE	1
#define FALSE	0

/*
 * Region Size for request/check/release region.
 */
#define IOBASE_REGION_SIZE	0x10

/*
 * Hardware related defaults
 */
#define DEF_AEDSP16_IOB 0x220   /* 0x220(default) 0x240                 */
#define DEF_AEDSP16_IRQ 7	/* 5 7(default) 9 10 11                 */
#define DEF_AEDSP16_MRQ 0	/* 5 7 9 10 0(default), 0 means disable */
#define DEF_AEDSP16_DMA 1	/* 0 1(default) 3                       */

/*
 * Commands of AEDSP16's DSP (SBPRO+special).
 * Some of them are COMMAND_xx, in the future they may change.
 */
#define WRITE_MDIRQ_CFG   0x50	/* Set M&I&DRQ mask (the real config)   */
#define COMMAND_52        0x52	/*                                      */
#define READ_HARD_CFG     0x58	/* Read Hardware Config (I/O base etc)  */
#define COMMAND_5C        0x5c	/*                                      */
#define COMMAND_60        0x60	/*                                      */
#define COMMAND_66        0x66	/*                                      */
#define COMMAND_6C        0x6c	/*                                      */
#define COMMAND_6E        0x6e	/*                                      */
#define COMMAND_88        0x88	/*                                      */
#define DSP_INIT_MSS      0x8c	/* Enable Microsoft Sound System mode   */
#define COMMAND_C5        0xc5	/*                                      */
#define GET_DSP_VERSION   0xe1	/* Get DSP Version                      */
#define GET_DSP_COPYRIGHT 0xe3	/* Get DSP Copyright                    */

/*
 * Offsets of AEDSP16 DSP I/O ports. The offset is added to base I/O port
 * to have the actual I/O port.
 * Register permissions are:
 * (wo) == Write Only
 * (ro) == Read  Only
 * (w-) == Write
 * (r-) == Read
 */
#define DSP_RESET    0x06	/* offset of DSP RESET             (wo) */
#define DSP_READ     0x0a	/* offset of DSP READ              (ro) */
#define DSP_WRITE    0x0c	/* offset of DSP WRITE             (w-) */
#define DSP_COMMAND  0x0c	/* offset of DSP COMMAND           (w-) */
#define DSP_STATUS   0x0c	/* offset of DSP STATUS            (r-) */
#define DSP_DATAVAIL 0x0e	/* offset of DSP DATA AVAILABLE    (ro) */


#define RETRY           10	/* Various retry values on I/O opera-   */
#define STATUSRETRY   1000	/* tions. Sometimes we have to          */
#define HARDRETRY   500000	/* wait for previous cmd to complete    */

/*
 * Size of character arrays that store name and version of sound card
 */
#define CARDNAMELEN 15		/* Size of the card's name in chars     */
#define CARDVERLEN  2		/* Size of the card's version in chars  */

#if defined(CONFIG_SC6600)
/*
 * Bitmapped flags of hard configuration
 */
/*
 * Decode macros (xl == low byte, xh = high byte)
 */
#define IOBASE(xl)		((xl & 0x01)?0x240:0x220)
#define JOY(xl)  		(xl & 0x02)
#define MPUADDR(xl)		( 			\
				(xl & 0x0C)?0x330:	\
				(xl & 0x08)?0x320:	\
				(xl & 0x04)?0x310:	\
						0x300)
#define WSSADDR(xl)		((xl & 0x10)?0xE80:0x530)
#define CDROM(xh)		(xh & 0x20)
#define CDROMADDR(xh)		(((xh & 0x1F) << 4) + 0x200)
/*
 * Encode macros
 */
#define BLDIOBASE(xl, val) {		\
	xl &= ~0x01; 			\
	if (val == 0x240)		\
		xl |= 0x01;		\
	}
#define BLDJOY(xl, val) {		\
	xl &= ~0x02; 			\
	if (val == 1)			\
		xl |= 0x02;		\
	}
#define BLDMPUADDR(xl, val) {		\
	xl &= ~0x0C;			\
	switch (val) {			\
		case 0x330:		\
			xl |= 0x0C;	\
			break;		\
		case 0x320:		\
			xl |= 0x08;	\
			break;		\
		case 0x310:		\
			xl |= 0x04;	\
			break;		\
		case 0x300:		\
			xl |= 0x00;	\
			break;		\
		default:		\
			xl |= 0x00;	\
			break;		\
		}			\
	}
#define BLDWSSADDR(xl, val) {		\
	xl &= ~0x10; 			\
	if (val == 0xE80)		\
		xl |= 0x10;		\
	}
#define BLDCDROM(xh, val) {		\
	xh &= ~0x20; 			\
	if (val == 1)			\
		xh |= 0x20;		\
	}
#define BLDCDROMADDR(xh, val) {		\
	int tmp = val;			\
	tmp -= 0x200;			\
	tmp >>= 4;			\
	tmp &= 0x1F;			\
	xh |= tmp;			\
	xh &= 0x7F;			\
	xh |= 0x40;			\
	}
#endif /* CONFIG_SC6600 */

/*
 * Bit mapped flags for calling aedsp16_init_board(), and saving the current
 * emulation mode.
 */
#define INIT_NONE   (0   )
#define INIT_SBPRO  (1<<0)
#define INIT_MSS    (1<<1)
#define INIT_MPU401 (1<<2)

static int      soft_cfg __initdata = 0;	/* bitmapped config */
static int      soft_cfg_mss __initdata = 0;	/* bitmapped mss config */
static int      ver[CARDVERLEN] __initdata = {0, 0};	/* DSP Ver:
						   hi->ver[0] lo->ver[1] */

#if defined(CONFIG_SC6600)
static int	hard_cfg[2]     /* lo<-hard_cfg[0] hi<-hard_cfg[1]      */
                     __initdata = { 0, 0};
#endif /* CONFIG_SC6600 */

#if defined(CONFIG_SC6600)
/* Decoded hard configuration */
struct	d_hcfg {
	int iobase;
	int joystick;
	int mpubase;
	int wssbase;
	int cdrom;
	int cdrombase;
};

static struct d_hcfg decoded_hcfg __initdata = {0, };

#endif /* CONFIG_SC6600 */

/* orVals contain the values to be or'ed       				*/
struct orVals {
	int	val;		/* irq|mirq|dma                         */
	int	or;		/* soft_cfg |= TheStruct.or             */
};

/* aedsp16_info contain the audio card configuration                  */
struct aedsp16_info {
	int base_io;            /* base I/O address for accessing card  */
	int irq;                /* irq value for DSP I/O                */
	int mpu_irq;            /* irq for mpu401 interface I/O         */
	int dma;                /* dma value for DSP I/O                */
	int mss_base;           /* base I/O for Microsoft Sound System  */
	int mpu_base;           /* base I/O for MPU-401 emulation       */
	int init;               /* Initialization status of the card    */
};

/*
 * Magic values that the DSP will eat when configuring irq/mirq/dma
 */
/* DSP IRQ conversion array             */
static struct orVals orIRQ[] __initdata = {
	{0x05, 0x28},
	{0x07, 0x08},
	{0x09, 0x10},
	{0x0a, 0x18},
	{0x0b, 0x20},
	{0x00, 0x00}
};

/* MPU-401 IRQ conversion array         */
static struct orVals orMIRQ[] __initdata = {
	{0x05, 0x04},
	{0x07, 0x44},
	{0x09, 0x84},
	{0x0a, 0xc4},
	{0x00, 0x00}
};

/* DMA Channels conversion array        */
static struct orVals orDMA[] __initdata = {
	{0x00, 0x01},
	{0x01, 0x02},
	{0x03, 0x03},
	{0x00, 0x00}
};

static struct aedsp16_info ae_config = {
	DEF_AEDSP16_IOB,
	DEF_AEDSP16_IRQ,
	DEF_AEDSP16_MRQ,
	DEF_AEDSP16_DMA,
	-1,
	-1,
	INIT_NONE
};

/*
 * Buffers to store audio card informations
 */
static char     DSPCopyright[CARDNAMELEN + 1] __initdata = {0, };
static char     DSPVersion[CARDVERLEN + 1] __initdata = {0, };

static int __init aedsp16_wait_data(int port)
{
	int             loop = STATUSRETRY;
	unsigned char   ret = 0;

	DBG1(("aedsp16_wait_data (0x%x): ", port));

	do {
		  ret = inb(port + DSP_DATAVAIL);
	/*
	 * Wait for data available (bit 7 of ret == 1)
	 */
	  } while (!(ret & 0x80) && loop--);

	if (ret & 0x80) {
		DBG1(("success.\n"));
		return TRUE;
	}

	DBG1(("failure.\n"));
	return FALSE;
}

static int __init aedsp16_read(int port)
{
	int inbyte;

	DBG(("    Read DSP Byte (0x%x): ", port));

	if (aedsp16_wait_data(port) == FALSE) {
		DBG(("failure.\n"));
		return -1;
	}

	inbyte = inb(port + DSP_READ);

	DBG(("read [0x%x]/{%c}.\n", inbyte, inbyte));

	return inbyte;
}

static int __init aedsp16_test_dsp(int port)
{
	return ((aedsp16_read(port) == 0xaa) ? TRUE : FALSE);
}

static int __init aedsp16_dsp_reset(int port)
{
	/*
	 * Reset DSP
	 */

	DBG(("Reset DSP:\n"));

	outb(1, (port + DSP_RESET));
	udelay(10);
	outb(0, (port + DSP_RESET));
	udelay(10);
	udelay(10);
	if (aedsp16_test_dsp(port) == TRUE) {
		DBG(("success.\n"));
		return TRUE;
	} else
		DBG(("failure.\n"));
	return FALSE;
}

static int __init aedsp16_write(int port, int cmd)
{
	unsigned char   ret;
	int             loop = HARDRETRY;

	DBG(("    Write DSP Byte (0x%x) [0x%x]: ", port, cmd));

	do {
		ret = inb(port + DSP_STATUS);
		/*
		 * DSP ready to receive data if bit 7 of ret == 0
		 */
		if (!(ret & 0x80)) {
			outb(cmd, port + DSP_COMMAND);
			DBG(("success.\n"));
			return 0;
		}
	} while (loop--);

	DBG(("timeout.\n"));
	printk("[AEDSP16] DSP Command (0x%x) timeout.\n", cmd);

	return -1;
}

#if defined(CONFIG_SC6600)

#if defined(AEDSP16_INFO) || defined(AEDSP16_DEBUG)
void __init aedsp16_pinfo(void) {
	DBG(("\n Base address:  %x\n", decoded_hcfg.iobase));
	DBG((" Joystick    : %s present\n", decoded_hcfg.joystick?"":" not"));
	DBG((" WSS addr    :  %x\n", decoded_hcfg.wssbase));
	DBG((" MPU-401 addr:  %x\n", decoded_hcfg.mpubase));
	DBG((" CDROM       : %s present\n", (decoded_hcfg.cdrom!=4)?"":" not"));
	DBG((" CDROMADDR   :  %x\n\n", decoded_hcfg.cdrombase));
}
#endif

static void __init aedsp16_hard_decode(void) {

	DBG((" aedsp16_hard_decode: 0x%x, 0x%x\n", hard_cfg[0], hard_cfg[1]));

/*
 * Decode Cfg Bytes.
 */
	decoded_hcfg.iobase	= IOBASE(hard_cfg[0]);
	decoded_hcfg.joystick	= JOY(hard_cfg[0]);
	decoded_hcfg.wssbase	= WSSADDR(hard_cfg[0]);
	decoded_hcfg.mpubase	= MPUADDR(hard_cfg[0]);
	decoded_hcfg.cdrom	= CDROM(hard_cfg[1]);
	decoded_hcfg.cdrombase	= CDROMADDR(hard_cfg[1]);

#if defined(AEDSP16_INFO) || defined(AEDSP16_DEBUG)
	printk(" Original sound card configuration:\n");
	aedsp16_pinfo();
#endif

/*
 * Now set up the real kernel configuration.
 */
	decoded_hcfg.iobase	= ae_config.base_io;
	decoded_hcfg.wssbase	= ae_config.mss_base;
	decoded_hcfg.mpubase	= ae_config.mpu_base;

#if defined(CONFIG_SC6600_JOY)
 	decoded_hcfg.joystick	= CONFIG_SC6600_JOY; /* Enable */
#endif
#if defined(CONFIG_SC6600_CDROM)
	decoded_hcfg.cdrom	= CONFIG_SC6600_CDROM; /* 4:N-3:I-2:G-1:P-0:S */
#endif
#if defined(CONFIG_SC6600_CDROMBASE)
	decoded_hcfg.cdrombase	= CONFIG_SC6600_CDROMBASE; /* 0 Disable */
#endif

#if defined(AEDSP16_DEBUG)
	DBG((" New Values:\n"));
	aedsp16_pinfo();
#endif

	DBG(("success.\n"));
}

static void __init aedsp16_hard_encode(void) {

	DBG((" aedsp16_hard_encode: 0x%x, 0x%x\n", hard_cfg[0], hard_cfg[1]));

	hard_cfg[0] = 0;
	hard_cfg[1] = 0;

	hard_cfg[0] |= 0x20;

	BLDIOBASE (hard_cfg[0], decoded_hcfg.iobase);
	BLDWSSADDR(hard_cfg[0], decoded_hcfg.wssbase);
	BLDMPUADDR(hard_cfg[0], decoded_hcfg.mpubase);
	BLDJOY(hard_cfg[0], decoded_hcfg.joystick);
	BLDCDROM(hard_cfg[1], decoded_hcfg.cdrom);
	BLDCDROMADDR(hard_cfg[1], decoded_hcfg.cdrombase);

#if defined(AEDSP16_DEBUG)
	aedsp16_pinfo();
#endif

	DBG((" aedsp16_hard_encode: 0x%x, 0x%x\n", hard_cfg[0], hard_cfg[1]));
	DBG(("success.\n"));

}

static int __init aedsp16_hard_write(int port) {

	DBG(("aedsp16_hard_write:\n"));

	if (aedsp16_write(port, COMMAND_6C)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_6C);
		DBG(("failure.\n"));
		return FALSE;
	}
	if (aedsp16_write(port, COMMAND_5C)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_5C);
		DBG(("failure.\n"));
		return FALSE;
	}
	if (aedsp16_write(port, hard_cfg[0])) {
		printk("[AEDSP16] DATA 0x%x: failed!\n", hard_cfg[0]);
		DBG(("failure.\n"));
		return FALSE;
	}
	if (aedsp16_write(port, hard_cfg[1])) {
		printk("[AEDSP16] DATA 0x%x: failed!\n", hard_cfg[1]);
		DBG(("failure.\n"));
		return FALSE;
	}
	if (aedsp16_write(port, COMMAND_C5)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_C5);
		DBG(("failure.\n"));
		return FALSE;
	}

	DBG(("success.\n"));

	return TRUE;
}

static int __init aedsp16_hard_read(int port) {

	DBG(("aedsp16_hard_read:\n"));

	if (aedsp16_write(port, READ_HARD_CFG)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", READ_HARD_CFG);
		DBG(("failure.\n"));
		return FALSE;
	}

	if ((hard_cfg[0] = aedsp16_read(port)) == -1) {
		printk("[AEDSP16] aedsp16_read after CMD 0x%x: failed\n",
			READ_HARD_CFG);
		DBG(("failure.\n"));
		return FALSE;
	}
	if ((hard_cfg[1] = aedsp16_read(port)) == -1) {
		printk("[AEDSP16] aedsp16_read after CMD 0x%x: failed\n",
			READ_HARD_CFG);
		DBG(("failure.\n"));
		return FALSE;
	}
	if (aedsp16_read(port) == -1) {
		printk("[AEDSP16] aedsp16_read after CMD 0x%x: failed\n",
			READ_HARD_CFG);
		DBG(("failure.\n"));
		return FALSE;
	}

	DBG(("success.\n"));

	return TRUE;
}

static int __init aedsp16_ext_cfg_write(int port) {

	int extcfg, val;

	if (aedsp16_write(port, COMMAND_66)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_66);
		return FALSE;
	}

	extcfg = 7;
	if (decoded_hcfg.cdrom != 2)
		extcfg = 0x0F;
	if ((decoded_hcfg.cdrom == 4) ||
	    (decoded_hcfg.cdrom == 3))
		extcfg &= ~2;
	if (decoded_hcfg.cdrombase == 0)
		extcfg &= ~2;
	if (decoded_hcfg.mpubase == 0)
		extcfg &= ~1;

	if (aedsp16_write(port, extcfg)) {
		printk("[AEDSP16] Write extcfg: failed!\n");
		return FALSE;
	}
	if (aedsp16_write(port, 0)) {
		printk("[AEDSP16] Write extcfg: failed!\n");
		return FALSE;
	}
	if (decoded_hcfg.cdrom == 3) {
		if (aedsp16_write(port, COMMAND_52)) {
			printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_52);
			return FALSE;
		}
		if ((val = aedsp16_read(port)) == -1) {
			printk("[AEDSP16] aedsp16_read after CMD 0x%x: failed\n"
					, COMMAND_52);
			return FALSE;
		}
		val &= 0x7F;
		if (aedsp16_write(port, COMMAND_60)) {
			printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_60);
			return FALSE;
		}
		if (aedsp16_write(port, val)) {
			printk("[AEDSP16] Write val: failed!\n");
			return FALSE;
		}
	}

	return TRUE;
}

#endif /* CONFIG_SC6600 */

static int __init aedsp16_cfg_write(int port) {
	if (aedsp16_write(port, WRITE_MDIRQ_CFG)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", WRITE_MDIRQ_CFG);
		return FALSE;
	}
	if (aedsp16_write(port, soft_cfg)) {
		printk("[AEDSP16] Initialization of (M)IRQ and DMA: failed!\n");
		return FALSE;
	}
	return TRUE;
}

static int __init aedsp16_init_mss(int port)
{
	DBG(("aedsp16_init_mss:\n"));

	mdelay(10);

	if (aedsp16_write(port, DSP_INIT_MSS)) {
		printk("[AEDSP16] aedsp16_init_mss [0x%x]: failed!\n",
				DSP_INIT_MSS);
		DBG(("failure.\n"));
		return FALSE;
	}
	
	mdelay(10);

	if (aedsp16_cfg_write(port) == FALSE)
		return FALSE;

	outb(soft_cfg_mss, ae_config.mss_base);

	DBG(("success.\n"));

	return TRUE;
}

static int __init aedsp16_setup_board(int port) {
	int	loop = RETRY;

#if defined(CONFIG_SC6600)
	int	val = 0;

	if (aedsp16_hard_read(port) == FALSE) {
		printk("[AEDSP16] aedsp16_hard_read: failed!\n");
		return FALSE;
	}

	if (aedsp16_write(port, COMMAND_52)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_52);
		return FALSE;
	}

	if ((val = aedsp16_read(port)) == -1) {
		printk("[AEDSP16] aedsp16_read after CMD 0x%x: failed\n",
				COMMAND_52);
		return FALSE;
	}
#endif

	do {
		if (aedsp16_write(port, COMMAND_88)) {
			printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_88);
			return FALSE;
		}
		mdelay(10);
	} while ((aedsp16_wait_data(port) == FALSE) && loop--);

	if (aedsp16_read(port) == -1) {
		printk("[AEDSP16] aedsp16_read after CMD 0x%x: failed\n",
				COMMAND_88);
		return FALSE;
	}

#if !defined(CONFIG_SC6600)
	if (aedsp16_write(port, COMMAND_5C)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_5C);
		return FALSE;
	}
#endif

	if (aedsp16_cfg_write(port) == FALSE)
		return FALSE;

#if defined(CONFIG_SC6600)
	if (aedsp16_write(port, COMMAND_60)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_60);
		return FALSE;
	}
	if (aedsp16_write(port, val)) {
		printk("[AEDSP16] DATA 0x%x: failed!\n", val);
		return FALSE;
	}
	if (aedsp16_write(port, COMMAND_6E)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_6E);
		return FALSE;
	}
	if (aedsp16_write(port, ver[0])) {
		printk("[AEDSP16] DATA 0x%x: failed!\n", ver[0]);
		return FALSE;
	}
	if (aedsp16_write(port, ver[1])) {
		printk("[AEDSP16] DATA 0x%x: failed!\n", ver[1]);
		return FALSE;
	}

	if (aedsp16_hard_write(port) == FALSE) {
		printk("[AEDSP16] aedsp16_hard_write: failed!\n");
		return FALSE;
	}

	if (aedsp16_write(port, COMMAND_5C)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", COMMAND_5C);
		return FALSE;
	}

#if defined(THIS_IS_A_THING_I_HAVE_NOT_TESTED_YET)
	if (aedsp16_cfg_write(port) == FALSE)
		return FALSE;
#endif

#endif

	return TRUE;
}

static int __init aedsp16_stdcfg(int port) {
	if (aedsp16_write(port, WRITE_MDIRQ_CFG)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", WRITE_MDIRQ_CFG);
		return FALSE;
	}
	/*
	 * 0x0A == (IRQ 7, DMA 1, MIRQ 0)
	 */
	if (aedsp16_write(port, 0x0A)) {
		printk("[AEDSP16] aedsp16_stdcfg: failed!\n");
		return FALSE;
	}
	return TRUE;
}

static int __init aedsp16_dsp_version(int port)
{
	int             len = 0;
	int             ret;

	DBG(("Get DSP Version:\n"));

	if (aedsp16_write(ae_config.base_io, GET_DSP_VERSION)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", GET_DSP_VERSION);
		DBG(("failed.\n"));
		return FALSE;
	}

	do {
		if ((ret = aedsp16_read(port)) == -1) {
			DBG(("failed.\n"));
			return FALSE;
		}
	/*
	 * We already know how many int are stored (2), so we know when the
	 * string is finished.
	 */
		ver[len++] = ret;
	  } while (len < CARDVERLEN);
	sprintf(DSPVersion, "%d.%d", ver[0], ver[1]);

	DBG(("success.\n"));

	return TRUE;
}

static int __init aedsp16_dsp_copyright(int port)
{
	int             len = 0;
	int             ret;

	DBG(("Get DSP Copyright:\n"));

	if (aedsp16_write(ae_config.base_io, GET_DSP_COPYRIGHT)) {
		printk("[AEDSP16] CMD 0x%x: failed!\n", GET_DSP_COPYRIGHT);
		DBG(("failed.\n"));
		return FALSE;
	}

	do {
		if ((ret = aedsp16_read(port)) == -1) {
	/*
	 * If no more data available, return to the caller, no error if len>0.
	 * We have no other way to know when the string is finished.
	 */
			if (len)
				break;
			else {
				DBG(("failed.\n"));
				return FALSE;
			}
		}

		DSPCopyright[len++] = ret;

	  } while (len < CARDNAMELEN);

	DBG(("success.\n"));

	return TRUE;
}

static void __init aedsp16_init_tables(void)
{
	int i = 0;

	memset(DSPCopyright, 0, CARDNAMELEN + 1);
	memset(DSPVersion, 0, CARDVERLEN + 1);

	for (i = 0; orIRQ[i].or; i++)
		if (orIRQ[i].val == ae_config.irq) {
			soft_cfg |= orIRQ[i].or;
			soft_cfg_mss |= orIRQ[i].or;
		}

	for (i = 0; orMIRQ[i].or; i++)
		if (orMIRQ[i].or == ae_config.mpu_irq)
			soft_cfg |= orMIRQ[i].or;

	for (i = 0; orDMA[i].or; i++)
		if (orDMA[i].val == ae_config.dma) {
			soft_cfg |= orDMA[i].or;
			soft_cfg_mss |= orDMA[i].or;
		}
}

static int __init aedsp16_init_board(void)
{
	aedsp16_init_tables();

	if (aedsp16_dsp_reset(ae_config.base_io) == FALSE) {
		printk("[AEDSP16] aedsp16_dsp_reset: failed!\n");
		return FALSE;
	}
	if (aedsp16_dsp_copyright(ae_config.base_io) == FALSE) {
		printk("[AEDSP16] aedsp16_dsp_copyright: failed!\n");
		return FALSE;
	}

	/*
	 * My AEDSP16 card return SC-6000 in DSPCopyright, so
	 * if we have something different, we have to be warned.
	 */
	if (strcmp("SC-6000", DSPCopyright))
		printk("[AEDSP16] Warning: non SC-6000 audio card!\n");

	if (aedsp16_dsp_version(ae_config.base_io) == FALSE) {
		printk("[AEDSP16] aedsp16_dsp_version: failed!\n");
		return FALSE;
	}

	if (aedsp16_stdcfg(ae_config.base_io) == FALSE) {
		printk("[AEDSP16] aedsp16_stdcfg: failed!\n");
		return FALSE;
	}

#if defined(CONFIG_SC6600)
	if (aedsp16_hard_read(ae_config.base_io) == FALSE) {
		printk("[AEDSP16] aedsp16_hard_read: failed!\n");
		return FALSE;
	}

	aedsp16_hard_decode();

	aedsp16_hard_encode();

	if (aedsp16_hard_write(ae_config.base_io) == FALSE) {
		printk("[AEDSP16] aedsp16_hard_write: failed!\n");
		return FALSE;
	}

	if (aedsp16_ext_cfg_write(ae_config.base_io) == FALSE) {
		printk("[AEDSP16] aedsp16_ext_cfg_write: failed!\n");
		return FALSE;
	}
#endif /* CONFIG_SC6600 */

	if (aedsp16_setup_board(ae_config.base_io) == FALSE) {
		printk("[AEDSP16] aedsp16_setup_board: failed!\n");
		return FALSE;
	}

	if (ae_config.mss_base != -1) {
		if (ae_config.init & INIT_MSS) {
			if (aedsp16_init_mss(ae_config.base_io) == FALSE) {
				printk("[AEDSP16] Can not initialize"
				       "Microsoft Sound System mode.\n");
				return FALSE;
			}
		}
	}

#if !defined(MODULE) || defined(AEDSP16_INFO) || defined(AEDSP16_DEBUG)

	printk("Audio Excel DSP 16 init v%s (%s %s) [",
		VERSION, DSPCopyright,
		DSPVersion);

	if (ae_config.mpu_base != -1) {
		if (ae_config.init & INIT_MPU401) {
			printk("MPU401");
			if ((ae_config.init & INIT_MSS) ||
			    (ae_config.init & INIT_SBPRO))
				printk(" ");
		}
	}

	if (ae_config.mss_base == -1) {
		if (ae_config.init & INIT_SBPRO) {
			printk("SBPro");
			if (ae_config.init & INIT_MSS)
				printk(" ");
		}
	}

	if (ae_config.mss_base != -1)
		if (ae_config.init & INIT_MSS)
			printk("MSS");

	printk("]\n");
#endif /* MODULE || AEDSP16_INFO || AEDSP16_DEBUG */

	mdelay(10);

	return TRUE;
}

static int __init init_aedsp16_sb(void)
{
	DBG(("init_aedsp16_sb: "));

/*
 * If the card is already init'ed MSS, we can not init it to SBPRO too
 * because the board can not emulate simultaneously MSS and SBPRO.
 */
	if (ae_config.init & INIT_MSS)
		return FALSE;
	if (ae_config.init & INIT_SBPRO)
		return FALSE;

	ae_config.init |= INIT_SBPRO;

	DBG(("done.\n"));

	return TRUE;
}

static void uninit_aedsp16_sb(void)
{
	DBG(("uninit_aedsp16_sb: "));

	ae_config.init &= ~INIT_SBPRO;

	DBG(("done.\n"));
}

static int __init init_aedsp16_mss(void)
{
	DBG(("init_aedsp16_mss: "));

/*
 * If the card is already init'ed SBPRO, we can not init it to MSS too
 * because the board can not emulate simultaneously MSS and SBPRO.
 */
	if (ae_config.init & INIT_SBPRO)
		return FALSE;
	if (ae_config.init & INIT_MSS)
		return FALSE;
/*
 * We must allocate the CONFIG_AEDSP16_BASE region too because these are the 
 * I/O ports to access card's control registers.
 */
	if (!(ae_config.init & INIT_MPU401)) {
		if (!request_region(ae_config.base_io, IOBASE_REGION_SIZE,
				"aedsp16 (base)")) {
			printk(
			"AEDSP16 BASE I/O port region is already in use.\n");
			return FALSE;
		}
	}

	ae_config.init |= INIT_MSS;

	DBG(("done.\n"));

	return TRUE;
}

static void uninit_aedsp16_mss(void)
{
	DBG(("uninit_aedsp16_mss: "));

	if ((!(ae_config.init & INIT_MPU401)) &&
	   (ae_config.init & INIT_MSS)) {
		release_region(ae_config.base_io, IOBASE_REGION_SIZE);
		DBG(("AEDSP16 base region released.\n"));
	}

	ae_config.init &= ~INIT_MSS;
	DBG(("done.\n"));
}

static int __init init_aedsp16_mpu(void)
{
	DBG(("init_aedsp16_mpu: "));

	if (ae_config.init & INIT_MPU401)
		return FALSE;

/*
 * We must request the CONFIG_AEDSP16_BASE region too because these are the I/O 
 * ports to access card's control registers.
 */
	if (!(ae_config.init & (INIT_MSS | INIT_SBPRO))) {
		if (!request_region(ae_config.base_io, IOBASE_REGION_SIZE,
					"aedsp16 (base)")) {
			printk(
			"AEDSP16 BASE I/O port region is already in use.\n");
			return FALSE;
		}
	}

	ae_config.init |= INIT_MPU401;

	DBG(("done.\n"));

	return TRUE;
}

static void uninit_aedsp16_mpu(void)
{
	DBG(("uninit_aedsp16_mpu: "));

	if ((!(ae_config.init & (INIT_MSS | INIT_SBPRO))) &&
	   (ae_config.init & INIT_MPU401)) {
		release_region(ae_config.base_io, IOBASE_REGION_SIZE);
		DBG(("AEDSP16 base region released.\n"));
	}

	ae_config.init &= ~INIT_MPU401;

	DBG(("done.\n"));
}

static int __init init_aedsp16(void)
{
	int initialized = FALSE;

	DBG(("Initializing BASE[0x%x] IRQ[%d] DMA[%d] MIRQ[%d]\n",
	     ae_config.base_io,ae_config.irq,ae_config.dma,ae_config.mpu_irq));

	if (ae_config.mss_base == -1) {
		if (init_aedsp16_sb() == FALSE) {
			uninit_aedsp16_sb();
		} else {
			initialized = TRUE;
		}
	}

	if (ae_config.mpu_base != -1) {
		if (init_aedsp16_mpu() == FALSE) {
			uninit_aedsp16_mpu();
		} else {
			initialized = TRUE;
		}
	}

/*
 * In the sequence of init routines, the MSS init MUST be the last!
 * This because of the special register programming the MSS mode needs.
 * A board reset would disable the MSS mode restoring the default SBPRO
 * mode.
 */
	if (ae_config.mss_base != -1) {
		if (init_aedsp16_mss() == FALSE) {
			uninit_aedsp16_mss();
		} else {
			initialized = TRUE;
		}
	}

	if (initialized)
		initialized = aedsp16_init_board();
	return initialized;
}

static void __exit uninit_aedsp16(void)
{
	if (ae_config.mss_base != -1)
		uninit_aedsp16_mss();
	else
		uninit_aedsp16_sb();
	if (ae_config.mpu_base != -1)
		uninit_aedsp16_mpu();
}

static int __initdata io = -1;
static int __initdata irq = -1;
static int __initdata dma = -1;
static int __initdata mpu_irq = -1;
static int __initdata mss_base = -1;
static int __initdata mpu_base = -1;

module_param(io, int, 0);
MODULE_PARM_DESC(io, "I/O base address (0x220 0x240)");
module_param(irq, int, 0);
MODULE_PARM_DESC(irq, "IRQ line (5 7 9 10 11)");
module_param(dma, int, 0);
MODULE_PARM_DESC(dma, "dma line (0 1 3)");
module_param(mpu_irq, int, 0);
MODULE_PARM_DESC(mpu_irq, "MPU-401 IRQ line (5 7 9 10 0)");
module_param(mss_base, int, 0);
MODULE_PARM_DESC(mss_base, "MSS emulation I/O base address (0x530 0xE80)");
module_param(mpu_base, int, 0);
MODULE_PARM_DESC(mpu_base,"MPU-401 I/O base address (0x300 0x310 0x320 0x330)");
MODULE_AUTHOR("Riccardo Facchetti <fizban@tin.it>");
MODULE_DESCRIPTION("Audio Excel DSP 16 Driver Version " VERSION);
MODULE_LICENSE("GPL");

static int __init do_init_aedsp16(void) {
	printk("Audio Excel DSP 16 init driver Copyright (C) Riccardo Facchetti 1995-98\n");
	if (io == -1 || dma == -1 || irq == -1) {
		printk(KERN_INFO "aedsp16: I/O, IRQ and DMA are mandatory\n");
		return -EINVAL;
	}

	ae_config.base_io = io;
	ae_config.irq = irq;
	ae_config.dma = dma;

	ae_config.mss_base = mss_base;
	ae_config.mpu_base = mpu_base;
	ae_config.mpu_irq = mpu_irq;

	if (init_aedsp16() == FALSE) {
		printk(KERN_ERR "aedsp16: initialization failed\n");
		/*
		 * XXX
		 * What error should we return here ?
		 */
		return -EINVAL;
	}
	return 0;
}

static void __exit cleanup_aedsp16(void) {
	uninit_aedsp16();
}

module_init(do_init_aedsp16);
module_exit(cleanup_aedsp16);

#ifndef MODULE
static int __init setup_aedsp16(char *str)
{
	/* io, irq, dma, mss_io, mpu_io, mpu_irq */
	int ints[7];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);

	io	 = ints[1];
	irq	 = ints[2];
	dma	 = ints[3];
	mss_base = ints[4];
	mpu_base = ints[5];
	mpu_irq	 = ints[6];
	return 1;
}

__setup("aedsp16=", setup_aedsp16);
#endif
