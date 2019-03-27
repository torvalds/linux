/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * 1. Redistributions of source code must retain the
 * Copyright (c) 1997 Amancio Hasty, 1999 Roger Hardiman
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
 *    must display the following acknowledgement:
 *      This product includes software developed by Amancio Hasty and
 *      Roger Hardiman
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * This is part of the Driver for Video Capture Cards (Frame grabbers)
 * and TV Tuner cards using the Brooktree Bt848, Bt848A, Bt849A, Bt878, Bt879
 * chipset.
 * Copyright Roger Hardiman and Amancio Hasty.
 *
 * bktr_audio : This deals with controlling the audio on TV cards,
 *                controlling the Audio Multiplexer (audio source selector).
 *                controlling any MSP34xx stereo audio decoders.
 *                controlling any DPL35xx dolby surroud sound audio decoders.
 *                initialising TDA98xx audio devices.
 *
 */

#include "opt_bktr.h"               /* Include any kernel config options */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#ifdef __FreeBSD__

#if (__FreeBSD_version < 500000)
#include <machine/clock.h>              /* for DELAY */
#include <pci/pcivar.h>
#else
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/selinfo.h>
#include <dev/pci/pcivar.h>
#endif

#include <machine/bus.h>
#include <sys/bus.h>
#endif

#ifdef __NetBSD__
#include <sys/proc.h>
#include <dev/ic/bt8xx.h>	/* NetBSD location of .h files */
#include <dev/pci/bktr/bktr_reg.h>
#include <dev/pci/bktr/bktr_core.h>
#include <dev/pci/bktr/bktr_tuner.h>
#include <dev/pci/bktr/bktr_card.h>
#include <dev/pci/bktr/bktr_audio.h>
#else
#include <dev/bktr/ioctl_meteor.h>
#include <dev/bktr/ioctl_bt848.h>	/* extensions to ioctl_meteor.h */
#include <dev/bktr/bktr_reg.h>
#include <dev/bktr/bktr_core.h>
#include <dev/bktr/bktr_tuner.h>
#include <dev/bktr/bktr_card.h>
#include <dev/bktr/bktr_audio.h>
#endif

/*
 * Prototypes for the GV_BCTV2 specific functions.
 */
void    set_bctv2_audio( bktr_ptr_t bktr );
void    bctv2_gpio_write( bktr_ptr_t bktr, int port, int val );
/*int   bctv2_gpio_read( bktr_ptr_t bktr, int port );*/ /* Not used */

/*
 * init_audio_devices
 * Reset any MSP34xx or TDA98xx audio devices.
 */
void init_audio_devices( bktr_ptr_t bktr ) {

        /* enable stereo if appropriate on TDA audio chip */
        if ( bktr->card.dbx )
                init_BTSC( bktr );
 
        /* reset the MSP34xx stereo audio chip */
        if ( bktr->card.msp3400c )
                msp_dpl_reset( bktr, bktr->msp_addr );

        /* reset the DPL35xx dolby audio chip */
        if ( bktr->card.dpl3518a )
                msp_dpl_reset( bktr, bktr->dpl_addr );

}


/*
 * 
 */
#define AUDIOMUX_DISCOVER_NOT
int
set_audio( bktr_ptr_t bktr, int cmd )
{
	u_long		temp;
	volatile u_char	idx;

#if defined( AUDIOMUX_DISCOVER )
	if ( cmd >= 200 )
		cmd -= 200;
	else
#endif /* AUDIOMUX_DISCOVER */

	/* check for existence of audio MUXes */
	if ( !bktr->card.audiomuxs[ 4 ] )
		return( -1 );

	switch (cmd) {
	case AUDIO_TUNER:
#ifdef BKTR_REVERSEMUTE
		bktr->audio_mux_select = 3;
#else
		bktr->audio_mux_select = 0;
#endif

		if (bktr->reverse_mute ) 
		      bktr->audio_mux_select = 0;
		else	
		    bktr->audio_mux_select = 3;

		break;
	case AUDIO_EXTERN:
		bktr->audio_mux_select = 1;
		break;
	case AUDIO_INTERN:
		bktr->audio_mux_select = 2;
		break;
	case AUDIO_MUTE:
		bktr->audio_mute_state = TRUE;	/* set mute */
		break;
	case AUDIO_UNMUTE:
		bktr->audio_mute_state = FALSE;	/* clear mute */
		break;
	default:
		printf("%s: audio cmd error %02x\n", bktr_name(bktr),
		       cmd);
		return( -1 );
	}


	/* Most cards have a simple audio multiplexer to select the
	 * audio source. The I/O_GV card has a more advanced multiplexer
	 * and requires special handling.
	 */
        if ( bktr->bt848_card == CARD_IO_BCTV2 ) {
                set_bctv2_audio( bktr );
                return( 0 );
	}

	/* Proceed with the simpler audio multiplexer code for the majority
	 * of Bt848 cards.
	 */

	/*
	 * Leave the upper bits of the GPIO port alone in case they control
	 * something like the dbx or teletext chips.  This doesn't guarantee
	 * success, but follows the rule of least astonishment.
	 */

	if ( bktr->audio_mute_state == TRUE ) {
#ifdef BKTR_REVERSEMUTE
		idx = 0;
#else
		idx = 3;
#endif

		if (bktr->reverse_mute )
		  idx  = 3;
		else	
		  idx  = 0;

	}
	else
		idx = bktr->audio_mux_select;


	temp = INL(bktr, BKTR_GPIO_DATA) & ~bktr->card.gpio_mux_bits;
#if defined( AUDIOMUX_DISCOVER )
	OUTL(bktr, BKTR_GPIO_DATA, temp | (cmd & 0xff));
	printf("%s: cmd: %d audio mux %x temp %x \n", bktr_name(bktr),
	  	cmd, bktr->card.audiomuxs[ idx ], temp );
#else
	OUTL(bktr, BKTR_GPIO_DATA, temp | bktr->card.audiomuxs[ idx ]);
#endif /* AUDIOMUX_DISCOVER */



	/* Some new Hauppauge cards do not have an audio mux */
	/* Instead we use the MSP34xx chip to select TV audio, Line-In */
	/* FM Radio and Mute */
	/* Examples of this are the Hauppauge 44xxx MSP34xx models */
	/* It is ok to drive both the mux and the MSP34xx chip. */
	/* If there is no mux, the MSP does the switching of the audio source */
	/* If there is a mux, it does the switching of the audio source */

	if ((bktr->card.msp3400c) && (bktr->audio_mux_present == 0)) {

	  if (bktr->audio_mute_state == TRUE ) {
		 msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0000, 0x0000); /* volume to MUTE */
	  } else {
		 if(bktr->audio_mux_select == 0) { /* TV Tuner */
		    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0000, 0x7300); /* 0 db volume */
		    if (bktr->msp_source_selected != 0) msp_autodetect(bktr);  /* setup TV audio mode */
		    bktr->msp_source_selected = 0;
		 }
		 if(bktr->audio_mux_select == 1) { /* Line In */
		    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0000, 0x7300); /* 0 db volume */
		    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x000d, 0x1900); /* scart prescale */
		    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0008, 0x0220); /* SCART | STEREO */
		    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0013, 0x0000); /* DSP In = SC1_IN_L/R */
		    bktr->msp_source_selected = 1;
		 }

		 if(bktr->audio_mux_select == 2) { /* FM Radio */
		    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0000, 0x7300); /* 0 db volume */
		    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x000d, 0x1900); /* scart prescale */
		    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0008, 0x0220); /* SCART | STEREO */
		    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0013, 0x0200); /* DSP In = SC2_IN_L/R */
		    bktr->msp_source_selected = 2;
		 }
	  }
	}


	return( 0 );
}


/*
 * 
 */
void
temp_mute( bktr_ptr_t bktr, int flag )
{
	static int	muteState = FALSE;

	if ( flag == TRUE ) {
		muteState = bktr->audio_mute_state;
		set_audio( bktr, AUDIO_MUTE );		/* prevent 'click' */
	}
	else {
		tsleep( BKTR_SLEEP, PZERO, "tuning", hz/8 );
		if ( muteState == FALSE )
			set_audio( bktr, AUDIO_UNMUTE );
	}
}

/* address of BTSC/SAP decoder chip */
#define TDA9850_WADDR           0xb6
#define TDA9850_RADDR           0xb7


/* registers in the TDA9850 BTSC/dbx chip */
#define CON1ADDR                0x04
#define CON2ADDR                0x05
#define CON3ADDR                0x06 
#define CON4ADDR                0x07
#define ALI1ADDR                0x08 
#define ALI2ADDR                0x09
#define ALI3ADDR                0x0a

/*
 * initialise the dbx chip
 * taken from the Linux bttv driver TDA9850 initialisation code
 */
void 
init_BTSC( bktr_ptr_t bktr )
{
    i2cWrite(bktr, TDA9850_WADDR, CON1ADDR, 0x08); /* noise threshold st */
    i2cWrite(bktr, TDA9850_WADDR, CON2ADDR, 0x08); /* noise threshold sap */
    i2cWrite(bktr, TDA9850_WADDR, CON3ADDR, 0x40); /* stereo mode */
    i2cWrite(bktr, TDA9850_WADDR, CON4ADDR, 0x07); /* 0 dB input gain? */
    i2cWrite(bktr, TDA9850_WADDR, ALI1ADDR, 0x10); /* wideband alignment? */
    i2cWrite(bktr, TDA9850_WADDR, ALI2ADDR, 0x10); /* spectral alignment? */
    i2cWrite(bktr, TDA9850_WADDR, ALI3ADDR, 0x03);
}

/*
 * setup the dbx chip
 * XXX FIXME: a lot of work to be done here, this merely unmutes it.
 */
int
set_BTSC( bktr_ptr_t bktr, int control )
{
	return( i2cWrite( bktr, TDA9850_WADDR, CON3ADDR, control ) );
}

/*
 * CARD_GV_BCTV2 specific functions.
 */

#define BCTV2_AUDIO_MAIN              0x10    /* main audio program */
#define BCTV2_AUDIO_SUB               0x20    /* sub audio program */
#define BCTV2_AUDIO_BOTH              0x30    /* main(L) + sub(R) program */

#define BCTV2_GPIO_REG0          1
#define BCTV2_GPIO_REG1          3

#define BCTV2_GR0_AUDIO_MODE     3
#define BCTV2_GR0_AUDIO_MAIN     0       /* main program */
#define BCTV2_GR0_AUDIO_SUB      3       /* sub program */
#define BCTV2_GR0_AUDIO_BOTH     1       /* main(L) + sub(R) */
#define BCTV2_GR0_AUDIO_MUTE     4       /* audio mute */
#define BCTV2_GR0_AUDIO_MONO     8       /* force mono */

void
set_bctv2_audio( bktr_ptr_t bktr )
{
        int data;

        switch (bktr->audio_mux_select) {
        case 1:         /* external */
        case 2:         /* internal */
                bctv2_gpio_write(bktr, BCTV2_GPIO_REG1, 0);
                break;
        default:        /* tuner */
                bctv2_gpio_write(bktr, BCTV2_GPIO_REG1, 1);
                break;
        }
/*      switch (bktr->audio_sap_select) { */
        switch (BCTV2_AUDIO_BOTH) {
        case BCTV2_AUDIO_SUB:
                data = BCTV2_GR0_AUDIO_SUB;
                break;
        case BCTV2_AUDIO_BOTH:
                data = BCTV2_GR0_AUDIO_BOTH;
                break;
        case BCTV2_AUDIO_MAIN:
        default:
                data = BCTV2_GR0_AUDIO_MAIN;
                break;
        }
        if (bktr->audio_mute_state == TRUE)
                data |= BCTV2_GR0_AUDIO_MUTE;

        bctv2_gpio_write(bktr, BCTV2_GPIO_REG0, data);

        return;
}

/* gpio_data bit assignment */
#define BCTV2_GPIO_ADDR_MASK     0x000300
#define BCTV2_GPIO_WE            0x000400
#define BCTV2_GPIO_OE            0x000800
#define BCTV2_GPIO_VAL_MASK      0x00f000

#define BCTV2_GPIO_PORT_MASK     3
#define BCTV2_GPIO_ADDR_SHIFT    8
#define BCTV2_GPIO_VAL_SHIFT     12

/* gpio_out_en value for read/write */
#define BCTV2_GPIO_OUT_RMASK     0x000f00
#define BCTV2_GPIO_OUT_WMASK     0x00ff00

#define BCTV2_BITS       100

void
bctv2_gpio_write( bktr_ptr_t bktr, int port, int val )
{
        u_long data, outbits;

        port &= BCTV2_GPIO_PORT_MASK;
        switch (port) {
        case 1:
        case 3:
                data = ((val << BCTV2_GPIO_VAL_SHIFT) & BCTV2_GPIO_VAL_MASK) |
                       ((port << BCTV2_GPIO_ADDR_SHIFT) & BCTV2_GPIO_ADDR_MASK) |
                       BCTV2_GPIO_WE | BCTV2_GPIO_OE;
                outbits = BCTV2_GPIO_OUT_WMASK;
                break;
        default:
                return;
        }
        OUTL(bktr, BKTR_GPIO_OUT_EN, 0);
        OUTL(bktr, BKTR_GPIO_DATA, data);
        OUTL(bktr, BKTR_GPIO_OUT_EN, outbits);
        DELAY(BCTV2_BITS);
        OUTL(bktr, BKTR_GPIO_DATA, data & ~BCTV2_GPIO_WE);
        DELAY(BCTV2_BITS);
        OUTL(bktr, BKTR_GPIO_DATA, data);
        DELAY(BCTV2_BITS);
        OUTL(bktr, BKTR_GPIO_DATA, ~0);
        OUTL(bktr, BKTR_GPIO_OUT_EN, 0);
}

/* Not yet used
int
bctv2_gpio_read( bktr_ptr_t bktr, int port )
{
        u_long data, outbits, ret;

        port &= BCTV2_GPIO_PORT_MASK;
        switch (port) {
        case 1:
        case 3:
                data = ((port << BCTV2_GPIO_ADDR_SHIFT) & BCTV2_GPIO_ADDR_MASK) |
                       BCTV2_GPIO_WE | BCTV2_GPIO_OE;
                outbits = BCTV2_GPIO_OUT_RMASK;
                break;
        default:
                return( -1 );
        }
        OUTL(bktr, BKTR_GPIO_OUT_EN, 0);
        OUTL(bktr, BKTR_GPIO_DATA, data);
        OUTL(bktr, BKTR_GPIO_OUT_EN, outbits);
        DELAY(BCTV2_BITS);
        OUTL(bktr, BKTR_GPIO_DATA, data & ~BCTV2_GPIO_OE);
        DELAY(BCTV2_BITS);
        ret = INL(bktr, BKTR_GPIO_DATA);
        DELAY(BCTV2_BITS);
        OUTL(bktr, BKTR_GPIO_DATA, data);
        DELAY(BCTV2_BITS);
        OUTL(bktr, BKTR_GPIO_DATA, ~0);
        OUTL(bktr, BKTR_GPIO_OUT_EN, 0);
        return( (ret & BCTV2_GPIO_VAL_MASK) >> BCTV2_GPIO_VAL_SHIFT );
}
*/

/*
 * setup the MSP34xx Stereo Audio Chip
 * This uses the Auto Configuration Option on MSP3410D and MSP3415D chips
 * and DBX mode selection for MSP3430G chips.
 * For MSP3400C support, the full programming sequence is required and is
 * not yet supported.
 */

/* Read the MSP version string */
void msp_read_id( bktr_ptr_t bktr ){
    int rev1=0, rev2=0;
    rev1 = msp_dpl_read(bktr, bktr->msp_addr, 0x12, 0x001e);
    rev2 = msp_dpl_read(bktr, bktr->msp_addr, 0x12, 0x001f);

    sprintf(bktr->msp_version_string, "34%02d%c-%c%d",
      (rev2>>8)&0xff, (rev1&0xff)+'@', ((rev1>>8)&0xff)+'@', rev2&0x1f);

}


/* Configure the MSP chip to Auto-detect the audio format.
 * For the MSP3430G, we use fast autodetect mode
 * For the MSP3410/3415 there are two schemes for this
 *  a) Fast autodetection - the chip is put into autodetect mode, and the function
 *     returns immediately. This works in most cases and is the Default Mode.
 *  b) Slow mode. The function sets the MSP3410/3415 chip, then waits for feedback from 
 *     the chip and re-programs it if needed.
 */
void msp_autodetect( bktr_ptr_t bktr ) {

#ifdef BKTR_NEW_MSP34XX_DRIVER

  /* Just wake up the (maybe) sleeping thread, it'll do everything for us */
  msp_wake_thread(bktr);

#else
  int auto_detect, loops;
  int stereo;

  /* MSP3430G - countries with mono and DBX stereo */
  if (strncmp("3430G", bktr->msp_version_string, 5) == 0){

    msp_dpl_write(bktr, bktr->msp_addr, 0x10, 0x0030,0x2003);/* Enable Auto format detection */
    msp_dpl_write(bktr, bktr->msp_addr, 0x10, 0x0020,0x0020);/* Standard Select Reg. = BTSC-Stereo*/
    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x000E,0x2403);/* darned if I know */
    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0008,0x0320);/* Source select = (St or A) */
					                     /* & Ch. Matrix = St */
    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0000,0x7300);/* Set volume to 0db gain */
  }


  /* MSP3415D SPECIAL CASE Use the Tuner's Mono audio output for the MSP */
  /* (for Hauppauge 44xxx card with Tuner Type 0x2a) */
  else if (  ( (strncmp("3415D", bktr->msp_version_string, 5) == 0)
               &&(bktr->msp_use_mono_source == 1)
              )
           || (bktr->slow_msp_audio == 2) ){
    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0000, 0x7300); /* 0 db volume */
    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x000d, 0x1900); /* scart prescale */
    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0008, 0x0220); /* SCART | STEREO */
    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0013, 0x0100); /* DSP In = MONO IN */
  }


  /* MSP3410/MSP3415 - countries with mono, stereo using 2 FM channels and NICAM */
  /* FAST sound scheme */
  else if (bktr->slow_msp_audio == 0) {
    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0000,0x7300);/* Set volume to 0db gain */
    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0008,0x0000);/* Spkr Source = default(FM/AM) */
    msp_dpl_write(bktr, bktr->msp_addr, 0x10, 0x0020,0x0001);/* Enable Auto format detection */
    msp_dpl_write(bktr, bktr->msp_addr, 0x10, 0x0021,0x0001);/* Auto selection of NICAM/MONO mode */
  }


  /* MSP3410/MSP3415 - European Countries where the fast MSP3410/3415 programming fails */
  /* SLOW sound scheme */
  else if ( bktr->slow_msp_audio == 1) {
    msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0000,0x7300);/* Set volume to 0db gain */
    msp_dpl_write(bktr, bktr->msp_addr, 0x10, 0x0020,0x0001);/* Enable Auto format detection */
    
    /* wait for 0.5s max for terrestrial sound autodetection */
    loops = 10;
    do {
      DELAY(100000);
      auto_detect = msp_dpl_read(bktr, bktr->msp_addr, 0x10, 0x007e);
      loops++;
    } while (auto_detect > 0xff && loops < 50);
    if (bootverbose)printf ("%s: Result of autodetect after %dms: %d\n",
			    bktr_name(bktr), loops*10, auto_detect);

    /* Now set the audio baseband processing */
    switch (auto_detect) {
    case 0:                    /* no TV sound standard detected */
      break;
    case 2:                    /* M Dual FM */
      break;
    case 3:                    /* B/G Dual FM; German stereo */
      /* Read the stereo detection value from DSP reg 0x0018 */
      DELAY(20000);
      stereo = msp_dpl_read(bktr, bktr->msp_addr, 0x12, 0x0018);
      if (bootverbose)printf ("%s: Stereo reg 0x18 a: %d\n",
			      bktr_name(bktr), stereo);
      DELAY(20000);
      stereo = msp_dpl_read(bktr, bktr->msp_addr, 0x12, 0x0018);
      if (bootverbose)printf ("%s: Stereo reg 0x18 b: %d\n",
			      bktr_name(bktr), stereo); 
      DELAY(20000); 
      stereo = msp_dpl_read(bktr, bktr->msp_addr, 0x12, 0x0018);
      if (bootverbose)printf ("%s: Stereo reg 0x18 c: %d\n",
			      bktr_name(bktr), stereo);
      if (stereo > 0x0100 && stereo < 0x8000) { /* Seems to be stereo */
        msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0008,0x0020);/* Loudspeaker set stereo*/
        /*
          set spatial effect strength to 50% enlargement
          set spatial effect mode b, stereo basewidth enlargement only
        */
        msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0005,0x3f28);
      } else if (stereo > 0x8000) {    /* bilingual mode */
        if (bootverbose) printf ("%s: Bilingual mode detected\n",
				 bktr_name(bktr));
        msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0008,0x0000);/* Loudspeaker */
        msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0005,0x0000);/* all spatial effects off */
       } else {                 /* must be mono */
        msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0008,0x0030);/* Loudspeaker */
        /*
          set spatial effect strength to 50% enlargement
          set spatial effect mode a, stereo basewidth enlargement
          and pseudo stereo effect with automatic high-pass filter
        */
        msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0005,0x3f08);
      }
#if 0
       /* The reset value for Channel matrix mode is FM/AM and SOUNDA/LEFT */
       /* We would like STEREO instead val: 0x0020 */
       msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0008,0x0020);/* Loudspeaker */
       msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0009,0x0020);/* Headphone */
       msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x000a,0x0020);/* SCART1 */
       msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0041,0x0020);/* SCART2 */
       msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x000b,0x0020);/* I2S */
       msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x000c,0x0020);/* Quasi-Peak Detector Source */
       msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x000e,0x0001);
#endif
      break;
    case 8:                    /* B/G FM NICAM */
       msp_dpl_write(bktr, bktr->msp_addr, 0x10, 0x0021,0x0001);/* Auto selection of NICAM/MONO mode */
       break;
     case 9:                    /* L_AM NICAM or D/K*/
     case 10:                   /* i-FM NICAM */
       break;
     default:
       if (bootverbose) printf ("%s: Unknown autodetection result value: %d\n",
				bktr_name(bktr), auto_detect); 
     }

  }


  /* uncomment the following line to enable the MSP34xx 1Khz Tone Generator */
  /* turn your speaker volume down low before trying this */
  /* msp_dpl_write(bktr, bktr->msp_addr, 0x12, 0x0014, 0x7f40); */

#endif /* BKTR_NEW_MSP34XX_DRIVER */
}

/* Read the DPL version string */
void dpl_read_id( bktr_ptr_t bktr ){
    int rev1=0, rev2=0;
    rev1 = msp_dpl_read(bktr, bktr->dpl_addr, 0x12, 0x001e);
    rev2 = msp_dpl_read(bktr, bktr->dpl_addr, 0x12, 0x001f);

    sprintf(bktr->dpl_version_string, "34%02d%c-%c%d",
      ((rev2>>8)&0xff)-1, (rev1&0xff)+'@', ((rev1>>8)&0xff)+'@', rev2&0x1f);
}

/* Configure the DPL chip to Auto-detect the audio format */
void dpl_autodetect( bktr_ptr_t bktr ) {

    /* The following are empiric values tried from the DPL35xx data sheet */
    msp_dpl_write(bktr, bktr->dpl_addr, 0x12, 0x000c,0x0320);	/* quasi peak detector source dolby
								lr 0x03xx; quasi peak detector matrix
								stereo 0xXX20 */
    msp_dpl_write(bktr, bktr->dpl_addr, 0x12, 0x0040,0x0060);	/* Surround decoder mode;
								ADAPTIVE/3D-PANORAMA, that means two
								speakers and no center speaker, all
								channels L/R/C/S mixed to L and R */
    msp_dpl_write(bktr, bktr->dpl_addr, 0x12, 0x0041,0x0620);	/* surround source matrix;I2S2/STEREO*/
    msp_dpl_write(bktr, bktr->dpl_addr, 0x12, 0x0042,0x1F00);	/* surround delay 31ms max */
    msp_dpl_write(bktr, bktr->dpl_addr, 0x12, 0x0043,0x0000);	/* automatic surround input balance */
    msp_dpl_write(bktr, bktr->dpl_addr, 0x12, 0x0044,0x4000);	/* surround spatial effect 50%
								recommended*/
    msp_dpl_write(bktr, bktr->dpl_addr, 0x12, 0x0045,0x5400);	/* surround panorama effect 66%
								recommended with PANORAMA mode
								in 0x0040 set to panorama */
}

