/*
** asm-m68k/amigahw.h -- This header defines some macros and pointers for
**                    the various Amiga custom hardware registers.
**                    The naming conventions used here conform to those
**                    used in the Amiga Hardware Reference Manual, 3rd Edition
**
** Copyright 1992 by Greg Harp
**
** This file is subject to the terms and conditions of the GNU General Public
** License.  See the file COPYING in the main directory of this archive
** for more details.
**
** Created: 9/24/92 by Greg Harp
*/

#ifndef _M68K_AMIGAHW_H
#define _M68K_AMIGAHW_H

#include <linux/ioport.h>

    /*
     *  Different Amiga models
     */

extern unsigned long amiga_model;

#define AMI_UNKNOWN	(0)
#define AMI_500		(1)
#define AMI_500PLUS	(2)
#define AMI_600		(3)
#define AMI_1000	(4)
#define AMI_1200	(5)
#define AMI_2000	(6)
#define AMI_2500	(7)
#define AMI_3000	(8)
#define AMI_3000T	(9)
#define AMI_3000PLUS	(10)
#define AMI_4000	(11)
#define AMI_4000T	(12)
#define AMI_CDTV	(13)
#define AMI_CD32	(14)
#define AMI_DRACO	(15)


    /*
     *  Chipsets
     */

extern unsigned long amiga_chipset;

#define CS_STONEAGE	(0)
#define CS_OCS		(1)
#define CS_ECS		(2)
#define CS_AGA		(3)


    /*
     *  Miscellaneous
     */

extern unsigned long amiga_eclock;	/* 700 kHz E Peripheral Clock */
extern unsigned long amiga_masterclock;	/* 28 MHz Master Clock */
extern unsigned long amiga_colorclock;	/* 3.5 MHz Color Clock */
extern unsigned long amiga_chip_size;	/* Chip RAM Size (bytes) */
extern unsigned char amiga_vblank;	/* VBLANK Frequency */
extern unsigned char amiga_psfreq;	/* Power Supply Frequency */


#define AMIGAHW_DECLARE(name)	unsigned name : 1
#define AMIGAHW_SET(name)	(amiga_hw_present.name = 1)
#define AMIGAHW_PRESENT(name)	(amiga_hw_present.name)

struct amiga_hw_present {
    /* video hardware */
    AMIGAHW_DECLARE(AMI_VIDEO);		/* Amiga Video */
    AMIGAHW_DECLARE(AMI_BLITTER);	/* Amiga Blitter */
    AMIGAHW_DECLARE(AMBER_FF);		/* Amber Flicker Fixer */
    /* sound hardware */
    AMIGAHW_DECLARE(AMI_AUDIO);		/* Amiga Audio */
    /* disk storage interfaces */
    AMIGAHW_DECLARE(AMI_FLOPPY);	/* Amiga Floppy */
    AMIGAHW_DECLARE(A3000_SCSI);	/* SCSI (wd33c93, A3000 alike) */
    AMIGAHW_DECLARE(A4000_SCSI);	/* SCSI (ncr53c710, A4000T alike) */
    AMIGAHW_DECLARE(A1200_IDE);		/* IDE (A1200 alike) */
    AMIGAHW_DECLARE(A4000_IDE);		/* IDE (A4000 alike) */
    AMIGAHW_DECLARE(CD_ROM);		/* CD ROM drive */
    /* other I/O hardware */
    AMIGAHW_DECLARE(AMI_KEYBOARD);	/* Amiga Keyboard */
    AMIGAHW_DECLARE(AMI_MOUSE);		/* Amiga Mouse */
    AMIGAHW_DECLARE(AMI_SERIAL);	/* Amiga Serial */
    AMIGAHW_DECLARE(AMI_PARALLEL);	/* Amiga Parallel */
    /* real time clocks */
    AMIGAHW_DECLARE(A2000_CLK);		/* Hardware Clock (A2000 alike) */
    AMIGAHW_DECLARE(A3000_CLK);		/* Hardware Clock (A3000 alike) */
    /* supporting hardware */
    AMIGAHW_DECLARE(CHIP_RAM);		/* Chip RAM */
    AMIGAHW_DECLARE(PAULA);		/* Paula (8364) */
    AMIGAHW_DECLARE(DENISE);		/* Denise (8362) */
    AMIGAHW_DECLARE(DENISE_HR);		/* Denise (8373) */
    AMIGAHW_DECLARE(LISA);		/* Lisa (8375) */
    AMIGAHW_DECLARE(AGNUS_PAL);		/* Normal/Fat PAL Agnus (8367/8371) */
    AMIGAHW_DECLARE(AGNUS_NTSC);	/* Normal/Fat NTSC Agnus (8361/8370) */
    AMIGAHW_DECLARE(AGNUS_HR_PAL);	/* Fat Hires PAL Agnus (8372) */
    AMIGAHW_DECLARE(AGNUS_HR_NTSC);	/* Fat Hires NTSC Agnus (8372) */
    AMIGAHW_DECLARE(ALICE_PAL);		/* PAL Alice (8374) */
    AMIGAHW_DECLARE(ALICE_NTSC);	/* NTSC Alice (8374) */
    AMIGAHW_DECLARE(MAGIC_REKICK);	/* A3000 Magic Hard Rekick */
    AMIGAHW_DECLARE(PCMCIA);		/* PCMCIA Slot */
    AMIGAHW_DECLARE(GG2_ISA);		/* GG2 Zorro2ISA Bridge */
    AMIGAHW_DECLARE(ZORRO);		/* Zorro AutoConfig */
    AMIGAHW_DECLARE(ZORRO3);		/* Zorro III */
};

extern struct amiga_hw_present amiga_hw_present;

struct CUSTOM {
    unsigned short bltddat;
    unsigned short dmaconr;
    unsigned short vposr;
    unsigned short vhposr;
    unsigned short dskdatr;
    unsigned short joy0dat;
    unsigned short joy1dat;
    unsigned short clxdat;
    unsigned short adkconr;
    unsigned short pot0dat;
    unsigned short pot1dat;
    unsigned short potgor;
    unsigned short serdatr;
    unsigned short dskbytr;
    unsigned short intenar;
    unsigned short intreqr;
    unsigned char  *dskptr;
    unsigned short dsklen;
    unsigned short dskdat;
    unsigned short refptr;
    unsigned short vposw;
    unsigned short vhposw;
    unsigned short copcon;
    unsigned short serdat;
    unsigned short serper;
    unsigned short potgo;
    unsigned short joytest;
    unsigned short strequ;
    unsigned short strvbl;
    unsigned short strhor;
    unsigned short strlong;
    unsigned short bltcon0;
    unsigned short bltcon1;
    unsigned short bltafwm;
    unsigned short bltalwm;
    unsigned char  *bltcpt;
    unsigned char  *bltbpt;
    unsigned char  *bltapt;
    unsigned char  *bltdpt;
    unsigned short bltsize;
    unsigned char  pad2d;
    unsigned char  bltcon0l;
    unsigned short bltsizv;
    unsigned short bltsizh;
    unsigned short bltcmod;
    unsigned short bltbmod;
    unsigned short bltamod;
    unsigned short bltdmod;
    unsigned short spare2[4];
    unsigned short bltcdat;
    unsigned short bltbdat;
    unsigned short bltadat;
    unsigned short spare3[3];
    unsigned short deniseid;
    unsigned short dsksync;
    unsigned short *cop1lc;
    unsigned short *cop2lc;
    unsigned short copjmp1;
    unsigned short copjmp2;
    unsigned short copins;
    unsigned short diwstrt;
    unsigned short diwstop;
    unsigned short ddfstrt;
    unsigned short ddfstop;
    unsigned short dmacon;
    unsigned short clxcon;
    unsigned short intena;
    unsigned short intreq;
    unsigned short adkcon;
    struct {
	unsigned short	*audlc;
	unsigned short audlen;
	unsigned short audper;
	unsigned short audvol;
	unsigned short auddat;
	unsigned short audspare[2];
    } aud[4];
    unsigned char  *bplpt[8];
    unsigned short bplcon0;
    unsigned short bplcon1;
    unsigned short bplcon2;
    unsigned short bplcon3;
    unsigned short bpl1mod;
    unsigned short bpl2mod;
    unsigned short bplcon4;
    unsigned short clxcon2;
    unsigned short bpldat[8];
    unsigned char  *sprpt[8];
    struct {
	unsigned short pos;
	unsigned short ctl;
	unsigned short dataa;
	unsigned short datab;
    } spr[8];
    unsigned short color[32];
    unsigned short htotal;
    unsigned short hsstop;
    unsigned short hbstrt;
    unsigned short hbstop;
    unsigned short vtotal;
    unsigned short vsstop;
    unsigned short vbstrt;
    unsigned short vbstop;
    unsigned short sprhstrt;
    unsigned short sprhstop;
    unsigned short bplhstrt;
    unsigned short bplhstop;
    unsigned short hhposw;
    unsigned short hhposr;
    unsigned short beamcon0;
    unsigned short hsstrt;
    unsigned short vsstrt;
    unsigned short hcenter;
    unsigned short diwhigh;
    unsigned short spare4[11];
    unsigned short fmode;
};

/*
 * DMA register bits
 */
#define DMAF_SETCLR		(0x8000)
#define DMAF_AUD0		(0x0001)
#define DMAF_AUD1		(0x0002)
#define DMAF_AUD2		(0x0004)
#define DMAF_AUD3		(0x0008)
#define DMAF_DISK		(0x0010)
#define DMAF_SPRITE		(0x0020)
#define DMAF_BLITTER		(0x0040)
#define DMAF_COPPER		(0x0080)
#define DMAF_RASTER		(0x0100)
#define DMAF_MASTER		(0x0200)
#define DMAF_BLITHOG		(0x0400)
#define DMAF_BLTNZERO		(0x2000)
#define DMAF_BLTDONE		(0x4000)
#define DMAF_ALL		(0x01FF)

struct CIA {
    unsigned char pra;		char pad0[0xff];
    unsigned char prb;		char pad1[0xff];
    unsigned char ddra;		char pad2[0xff];
    unsigned char ddrb;		char pad3[0xff];
    unsigned char talo;		char pad4[0xff];
    unsigned char tahi;		char pad5[0xff];
    unsigned char tblo;		char pad6[0xff];
    unsigned char tbhi;		char pad7[0xff];
    unsigned char todlo;	char pad8[0xff];
    unsigned char todmid;	char pad9[0xff];
    unsigned char todhi;	char pada[0x1ff];
    unsigned char sdr;		char padb[0xff];
    unsigned char icr;		char padc[0xff];
    unsigned char cra;		char padd[0xff];
    unsigned char crb;		char pade[0xff];
};

#define zTwoBase (0x80000000)
#define ZTWO_PADDR(x) (((unsigned long)(x))-zTwoBase)
#define ZTWO_VADDR(x) (((unsigned long)(x))+zTwoBase)

#define CUSTOM_PHYSADDR     (0xdff000)
#define custom ((*(volatile struct CUSTOM *)(zTwoBase+CUSTOM_PHYSADDR)))

#define CIAA_PHYSADDR	  (0xbfe001)
#define CIAB_PHYSADDR	  (0xbfd000)
#define ciaa   ((*(volatile struct CIA *)(zTwoBase + CIAA_PHYSADDR)))
#define ciab   ((*(volatile struct CIA *)(zTwoBase + CIAB_PHYSADDR)))

#define CHIP_PHYSADDR	    (0x000000)

void amiga_chip_init (void);
void *amiga_chip_alloc(unsigned long size, const char *name);
void *amiga_chip_alloc_res(unsigned long size, struct resource *res);
void amiga_chip_free(void *ptr);
unsigned long amiga_chip_avail( void ); /*MILAN*/
extern volatile unsigned short amiga_audio_min_period;

static inline void amifb_video_off(void)
{
	if (amiga_chipset == CS_ECS || amiga_chipset == CS_AGA) {
		/* program Denise/Lisa for a higher maximum play rate */
		custom.htotal = 113;        /* 31 kHz */
		custom.vtotal = 223;        /* 70 Hz */
		custom.beamcon0 = 0x4390;   /* HARDDIS, VAR{BEAM,VSY,HSY,CSY}EN */
		/* suspend the monitor */
		custom.hsstrt = custom.hsstop = 116;
		custom.vsstrt = custom.vsstop = 226;
		amiga_audio_min_period = 57;
	}
}

struct tod3000 {
  unsigned int  :28, second2:4;	/* lower digit */
  unsigned int  :28, second1:4;	/* upper digit */
  unsigned int  :28, minute2:4;	/* lower digit */
  unsigned int  :28, minute1:4;	/* upper digit */
  unsigned int  :28, hour2:4;	/* lower digit */
  unsigned int  :28, hour1:4;	/* upper digit */
  unsigned int  :28, weekday:4;
  unsigned int  :28, day2:4;	/* lower digit */
  unsigned int  :28, day1:4;	/* upper digit */
  unsigned int  :28, month2:4;	/* lower digit */
  unsigned int  :28, month1:4;	/* upper digit */
  unsigned int  :28, year2:4;	/* lower digit */
  unsigned int  :28, year1:4;	/* upper digit */
  unsigned int  :28, cntrl1:4;	/* control-byte 1 */
  unsigned int  :28, cntrl2:4;	/* control-byte 2 */
  unsigned int  :28, cntrl3:4;	/* control-byte 3 */
};
#define TOD3000_CNTRL1_HOLD	0
#define TOD3000_CNTRL1_FREE	9
#define tod_3000 ((*(volatile struct tod3000 *)(zTwoBase+0xDC0000)))

struct tod2000 {
  unsigned int  :28, second2:4;	/* lower digit */
  unsigned int  :28, second1:4;	/* upper digit */
  unsigned int  :28, minute2:4;	/* lower digit */
  unsigned int  :28, minute1:4;	/* upper digit */
  unsigned int  :28, hour2:4;	/* lower digit */
  unsigned int  :28, hour1:4;	/* upper digit */
  unsigned int  :28, day2:4;	/* lower digit */
  unsigned int  :28, day1:4;	/* upper digit */
  unsigned int  :28, month2:4;	/* lower digit */
  unsigned int  :28, month1:4;	/* upper digit */
  unsigned int  :28, year2:4;	/* lower digit */
  unsigned int  :28, year1:4;	/* upper digit */
  unsigned int  :28, weekday:4;
  unsigned int  :28, cntrl1:4;	/* control-byte 1 */
  unsigned int  :28, cntrl2:4;	/* control-byte 2 */
  unsigned int  :28, cntrl3:4;	/* control-byte 3 */
};

#define TOD2000_CNTRL1_HOLD	(1<<0)
#define TOD2000_CNTRL1_BUSY	(1<<1)
#define TOD2000_CNTRL3_24HMODE	(1<<2)
#define TOD2000_HOUR1_PM	(1<<2)
#define tod_2000 ((*(volatile struct tod2000 *)(zTwoBase+0xDC0000)))

#endif /* _M68K_AMIGAHW_H */
