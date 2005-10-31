/*  -*- linux-c -*-
 *
 * sound/wavfront.c
 *
 * A Linux driver for Turtle Beach WaveFront Series (Maui, Tropez, Tropez Plus)
 *
 * This driver supports the onboard wavetable synthesizer (an ICS2115),
 * including patch, sample and program loading and unloading, conversion
 * of GUS patches during loading, and full user-level access to all
 * WaveFront commands. It tries to provide semi-intelligent patch and
 * sample management as well.
 *
 * It also provides support for the ICS emulation of an MPU-401.  Full
 * support for the ICS emulation's "virtual MIDI mode" is provided in
 * wf_midi.c.
 *
 * Support is also provided for the Tropez Plus' onboard FX processor,
 * a Yamaha YSS225. Currently, code exists to configure the YSS225,
 * and there is an interface allowing tweaking of any of its memory
 * addresses. However, I have been unable to decipher the logical
 * positioning of the configuration info for various effects, so for
 * now, you just get the YSS225 in the same state as Turtle Beach's
 * "SETUPSND.EXE" utility leaves it.
 *
 * The boards' DAC/ADC (a Crystal CS4232) is supported by cs4232.[co],
 * This chip also controls the configuration of the card: the wavefront
 * synth is logical unit 4.
 *
 *
 * Supported devices:
 *
 *   /dev/dsp                      - using cs4232+ad1848 modules, OSS compatible
 *   /dev/midiNN and /dev/midiNN+1 - using wf_midi code, OSS compatible
 *   /dev/synth00                  - raw synth interface
 * 
 **********************************************************************
 *
 * Copyright (C) by Paul Barton-Davis 1998
 *
 * Some portions of this file are taken from work that is
 * copyright (C) by Hannu Savolainen 1993-1996
 *
 * Although the relevant code here is all new, the handling of
 * sample/alias/multi- samples is entirely based on a driver by Matt
 * Martin and Rutger Nijlunsing which demonstrated how to get things
 * to work correctly. The GUS patch loading code has been almost
 * unaltered by me, except to fit formatting and function names in the
 * rest of the file. Many thanks to them.
 *
 * Appreciation and thanks to Hannu Savolainen for his early work on the Maui
 * driver, and answering a few questions while this one was developed.
 *
 * Absolutely NO thanks to Turtle Beach/Voyetra and Yamaha for their
 * complete lack of help in developing this driver, and in particular
 * for their utter silence in response to questions about undocumented
 * aspects of configuring a WaveFront soundcard, particularly the
 * effects processor.
 *
 * $Id: wavfront.c,v 0.7 1998/09/09 15:47:36 pbd Exp $
 *
 * This program is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes:
 * 11-10-2000	Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
 *		Added some __init and __initdata to entries in yss225.c
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/ptrace.h>
#include <linux/fcntl.h>
#include <linux/syscalls.h>
#include <linux/ioport.h>    
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/config.h>

#include <linux/delay.h>

#include "sound_config.h"

#include <linux/wavefront.h>

#define _MIDI_SYNTH_C_
#define MIDI_SYNTH_NAME	"WaveFront MIDI"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

/* Compile-time control of the extent to which OSS is supported.

   I consider /dev/sequencer to be an anachronism, but given its
   widespread usage by various Linux MIDI software, it seems worth
   offering support to it if it's not too painful. Instead of using
   /dev/sequencer, I recommend:

     for synth programming and patch loading: /dev/synthNN
     for kernel-synchronized MIDI sequencing: the ALSA sequencer
     for direct MIDI control: /dev/midiNN

   I have never tried static compilation into the kernel. The #if's
   for this are really just notes to myself about what the code is
   for.
*/

#define OSS_SUPPORT_SEQ            0x1  /* use of /dev/sequencer */
#define OSS_SUPPORT_STATIC_INSTALL 0x2  /* static compilation into kernel */

#define OSS_SUPPORT_LEVEL          0x1  /* just /dev/sequencer for now */

#if    OSS_SUPPORT_LEVEL & OSS_SUPPORT_SEQ
static int (*midi_load_patch) (int devno, int format, const char __user *addr,
			       int offs, int count, int pmgr_flag) = NULL;
#endif /* OSS_SUPPORT_SEQ */

/* if WF_DEBUG not defined, no run-time debugging messages will
   be available via the debug flag setting. Given the current
   beta state of the driver, this will remain set until a future 
   version.
*/

#define WF_DEBUG 1

#ifdef WF_DEBUG

/* Thank goodness for gcc's preprocessor ... */

#define DPRINT(cond, format, args...) \
       if ((dev.debug & (cond)) == (cond)) { \
	     printk (KERN_DEBUG LOGNAME format, ## args); \
       }
#else
#define DPRINT(cond, format, args...)
#endif

#define LOGNAME "WaveFront: "

/* bitmasks for WaveFront status port value */

#define STAT_RINTR_ENABLED	0x01
#define STAT_CAN_READ		0x02
#define STAT_INTR_READ		0x04
#define STAT_WINTR_ENABLED	0x10
#define STAT_CAN_WRITE		0x20
#define STAT_INTR_WRITE		0x40

/*** Module-accessible parameters ***************************************/

static int wf_raw;     /* we normally check for "raw state" to firmware
			   loading. if set, then during driver loading, the
			   state of the board is ignored, and we reset the
			   board and load the firmware anyway.
			*/
		   
static int fx_raw = 1; /* if this is zero, we'll leave the FX processor in
		          whatever state it is when the driver is loaded.
		          The default is to download the microprogram and
		          associated coefficients to set it up for "default"
		          operation, whatever that means.
		       */

static int debug_default;  /* you can set this to control debugging
			      during driver loading. it takes any combination
			      of the WF_DEBUG_* flags defined in
			      wavefront.h
			   */

/* XXX this needs to be made firmware and hardware version dependent */

static char *ospath = "/etc/sound/wavefront.os"; /* where to find a processed
					            version of the WaveFront OS
					          */

static int wait_polls = 2000; /* This is a number of tries we poll the
				 status register before resorting to sleeping.
				 WaveFront being an ISA card each poll takes
				 about 1.2us. So before going to
			         sleep we wait up to 2.4ms in a loop.
			     */

static int sleep_length = HZ/100; /* This says how long we're going to
				     sleep between polls.
			             10ms sounds reasonable for fast response.
			          */

static int sleep_tries = 50;       /* Wait for status 0.5 seconds total. */

static int reset_time = 2; /* hundreths of a second we wait after a HW reset for
			      the expected interrupt.
			   */

static int ramcheck_time = 20;    /* time in seconds to wait while ROM code
			             checks on-board RAM.
			          */

static int osrun_time = 10;  /* time in seconds we wait for the OS to
			        start running.
			     */

module_param(wf_raw, int, 0);
module_param(fx_raw, int, 0);
module_param(debug_default, int, 0);
module_param(wait_polls, int, 0);
module_param(sleep_length, int, 0);
module_param(sleep_tries, int, 0);
module_param(ospath, charp, 0);
module_param(reset_time, int, 0);
module_param(ramcheck_time, int, 0);
module_param(osrun_time, int, 0);

/***************************************************************************/

/* Note: because this module doesn't export any symbols, this really isn't
   a global variable, even if it looks like one. I was quite confused by
   this when I started writing this as a (newer) module -- pbd.
*/

struct wf_config {
	int devno;            /* device number from kernel */
	int irq;              /* "you were one, one of the few ..." */
	int base;             /* low i/o port address */

#define mpu_data_port    base 
#define mpu_command_port base + 1 /* write semantics */
#define mpu_status_port  base + 1 /* read semantics */
#define data_port        base + 2 
#define status_port      base + 3 /* read semantics */
#define control_port     base + 3 /* write semantics  */
#define block_port       base + 4 /* 16 bit, writeonly */
#define last_block_port  base + 6 /* 16 bit, writeonly */

	/* FX ports. These are mapped through the ICS2115 to the YS225.
	   The ICS2115 takes care of flipping the relevant pins on the
	   YS225 so that access to each of these ports does the right
	   thing. Note: these are NOT documented by Turtle Beach.
	*/

#define fx_status       base + 8 
#define fx_op           base + 8 
#define fx_lcr          base + 9 
#define fx_dsp_addr     base + 0xa
#define fx_dsp_page     base + 0xb 
#define fx_dsp_lsb      base + 0xc 
#define fx_dsp_msb      base + 0xd 
#define fx_mod_addr     base + 0xe
#define fx_mod_data     base + 0xf 

	volatile int irq_ok;               /* set by interrupt handler */
        volatile int irq_cnt;              /* ditto */
	int opened;                        /* flag, holds open(2) mode */
	char debug;                        /* debugging flags */
	int freemem;                       /* installed RAM, in bytes */ 

	int synth_dev;                     /* devno for "raw" synth */
	int mididev;                       /* devno for internal MIDI */
	int ext_mididev;                   /* devno for external MIDI */ 
        int fx_mididev;                    /* devno for FX MIDI interface */
#if OSS_SUPPORT_LEVEL & OSS_SUPPORT_SEQ
	int oss_dev;                      /* devno for OSS sequencer synth */
#endif /* OSS_SUPPORT_SEQ */

	char fw_version[2];                /* major = [0], minor = [1] */
	char hw_version[2];                /* major = [0], minor = [1] */
	char israw;                        /* needs Motorola microcode */
	char has_fx;                       /* has FX processor (Tropez+) */
	char prog_status[WF_MAX_PROGRAM];  /* WF_SLOT_* */
	char patch_status[WF_MAX_PATCH];   /* WF_SLOT_* */
	char sample_status[WF_MAX_SAMPLE]; /* WF_ST_* | WF_SLOT_* */
	int samples_used;                  /* how many */
	char interrupts_on;                /* h/w MPU interrupts enabled ? */
	char rom_samples_rdonly;           /* can we write on ROM samples */
	wait_queue_head_t interrupt_sleeper; 
} dev;

static DEFINE_SPINLOCK(lock);
static int  detect_wffx(void);
static int  wffx_ioctl (wavefront_fx_info *);
static int  wffx_init (void);

static int wavefront_delete_sample (int sampnum);
static int wavefront_find_free_sample (void);

/* From wf_midi.c */

extern int  virtual_midi_enable  (void);
extern int  virtual_midi_disable (void);
extern int  detect_wf_mpu (int, int);
extern int  install_wf_mpu (void);
extern int  uninstall_wf_mpu (void);

typedef struct {
	int cmd;
	char *action;
	unsigned int read_cnt;
	unsigned int write_cnt;
	int need_ack;
} wavefront_command;

static struct {
	int errno;
	const char *errstr;
} wavefront_errors[] = {
	{ 0x01, "Bad sample number" },
	{ 0x02, "Out of sample memory" },
	{ 0x03, "Bad patch number" },
	{ 0x04, "Error in number of voices" },
	{ 0x06, "Sample load already in progress" },
	{ 0x0B, "No sample load request pending" },
	{ 0x0E, "Bad MIDI channel number" },
	{ 0x10, "Download Record Error" },
	{ 0x80, "Success" },
	{ 0 }
};

#define NEEDS_ACK 1

static wavefront_command wavefront_commands[] = {
	{ WFC_SET_SYNTHVOL, "set synthesizer volume", 0, 1, NEEDS_ACK },
	{ WFC_GET_SYNTHVOL, "get synthesizer volume", 1, 0, 0},
	{ WFC_SET_NVOICES, "set number of voices", 0, 1, NEEDS_ACK },
	{ WFC_GET_NVOICES, "get number of voices", 1, 0, 0 },
	{ WFC_SET_TUNING, "set synthesizer tuning", 0, 2, NEEDS_ACK },
	{ WFC_GET_TUNING, "get synthesizer tuning", 2, 0, 0 },
	{ WFC_DISABLE_CHANNEL, "disable synth channel", 0, 1, NEEDS_ACK },
	{ WFC_ENABLE_CHANNEL, "enable synth channel", 0, 1, NEEDS_ACK },
	{ WFC_GET_CHANNEL_STATUS, "get synth channel status", 3, 0, 0 },
	{ WFC_MISYNTH_OFF, "disable midi-in to synth", 0, 0, NEEDS_ACK },
	{ WFC_MISYNTH_ON, "enable midi-in to synth", 0, 0, NEEDS_ACK },
	{ WFC_VMIDI_ON, "enable virtual midi mode", 0, 0, NEEDS_ACK },
	{ WFC_VMIDI_OFF, "disable virtual midi mode", 0, 0, NEEDS_ACK },
	{ WFC_MIDI_STATUS, "report midi status", 1, 0, 0 },
	{ WFC_FIRMWARE_VERSION, "report firmware version", 2, 0, 0 },
	{ WFC_HARDWARE_VERSION, "report hardware version", 2, 0, 0 },
	{ WFC_GET_NSAMPLES, "report number of samples", 2, 0, 0 },
	{ WFC_INSTOUT_LEVELS, "report instantaneous output levels", 7, 0, 0 },
	{ WFC_PEAKOUT_LEVELS, "report peak output levels", 7, 0, 0 },
	{ WFC_DOWNLOAD_SAMPLE, "download sample",
	  0, WF_SAMPLE_BYTES, NEEDS_ACK },
	{ WFC_DOWNLOAD_BLOCK, "download block", 0, 0, NEEDS_ACK},
	{ WFC_DOWNLOAD_SAMPLE_HEADER, "download sample header",
	  0, WF_SAMPLE_HDR_BYTES, NEEDS_ACK },
	{ WFC_UPLOAD_SAMPLE_HEADER, "upload sample header", 13, 2, 0 },

	/* This command requires a variable number of bytes to be written.
	   There is a hack in wavefront_cmd() to support this. The actual
	   count is passed in as the read buffer ptr, cast appropriately.
	   Ugh.
	*/

	{ WFC_DOWNLOAD_MULTISAMPLE, "download multisample", 0, 0, NEEDS_ACK },

	/* This one is a hack as well. We just read the first byte of the
	   response, don't fetch an ACK, and leave the rest to the 
	   calling function. Ugly, ugly, ugly.
	*/

	{ WFC_UPLOAD_MULTISAMPLE, "upload multisample", 2, 1, 0 },
	{ WFC_DOWNLOAD_SAMPLE_ALIAS, "download sample alias",
	  0, WF_ALIAS_BYTES, NEEDS_ACK },
	{ WFC_UPLOAD_SAMPLE_ALIAS, "upload sample alias", WF_ALIAS_BYTES, 2, 0},
	{ WFC_DELETE_SAMPLE, "delete sample", 0, 2, NEEDS_ACK },
	{ WFC_IDENTIFY_SAMPLE_TYPE, "identify sample type", 5, 2, 0 },
	{ WFC_UPLOAD_SAMPLE_PARAMS, "upload sample parameters" },
	{ WFC_REPORT_FREE_MEMORY, "report free memory", 4, 0, 0 },
	{ WFC_DOWNLOAD_PATCH, "download patch", 0, 134, NEEDS_ACK },
	{ WFC_UPLOAD_PATCH, "upload patch", 132, 2, 0 },
	{ WFC_DOWNLOAD_PROGRAM, "download program", 0, 33, NEEDS_ACK },
	{ WFC_UPLOAD_PROGRAM, "upload program", 32, 1, 0 },
	{ WFC_DOWNLOAD_EDRUM_PROGRAM, "download enhanced drum program", 0, 9,
	  NEEDS_ACK},
	{ WFC_UPLOAD_EDRUM_PROGRAM, "upload enhanced drum program", 8, 1, 0},
	{ WFC_SET_EDRUM_CHANNEL, "set enhanced drum program channel",
	  0, 1, NEEDS_ACK },
	{ WFC_DISABLE_DRUM_PROGRAM, "disable drum program", 0, 1, NEEDS_ACK },
	{ WFC_REPORT_CHANNEL_PROGRAMS, "report channel program numbers",
	  32, 0, 0 },
	{ WFC_NOOP, "the no-op command", 0, 0, NEEDS_ACK },
	{ 0x00 }
};

static const char *
wavefront_errorstr (int errnum)

{
	int i;

	for (i = 0; wavefront_errors[i].errstr; i++) {
		if (wavefront_errors[i].errno == errnum) {
			return wavefront_errors[i].errstr;
		}
	}

	return "Unknown WaveFront error";
}

static wavefront_command *
wavefront_get_command (int cmd) 

{
	int i;

	for (i = 0; wavefront_commands[i].cmd != 0; i++) {
		if (cmd == wavefront_commands[i].cmd) {
			return &wavefront_commands[i];
		}
	}

	return (wavefront_command *) 0;
}

static inline int
wavefront_status (void) 

{
	return inb (dev.status_port);
}

static int
wavefront_wait (int mask)

{
	int i;

	for (i = 0; i < wait_polls; i++)
		if (wavefront_status() & mask)
			return 1;

	for (i = 0; i < sleep_tries; i++) {

		if (wavefront_status() & mask) {
			set_current_state(TASK_RUNNING);
			return 1;
		}

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(sleep_length);
		if (signal_pending(current))
			break;
	}

	set_current_state(TASK_RUNNING);
	return 0;
}

static int
wavefront_read (void)

{
	if (wavefront_wait (STAT_CAN_READ))
		return inb (dev.data_port);

	DPRINT (WF_DEBUG_DATA, "read timeout.\n");

	return -1;
}

static int
wavefront_write (unsigned char data)

{
	if (wavefront_wait (STAT_CAN_WRITE)) {
		outb (data, dev.data_port);
		return 0;
	}

	DPRINT (WF_DEBUG_DATA, "write timeout.\n");

	return -1;
}

static int
wavefront_cmd (int cmd, unsigned char *rbuf, unsigned char *wbuf)

{
	int ack;
	int i;
	int c;
	wavefront_command *wfcmd;

	if ((wfcmd = wavefront_get_command (cmd)) == (wavefront_command *) 0) {
		printk (KERN_WARNING LOGNAME "command 0x%x not supported.\n",
			cmd);
		return 1;
	}

	/* Hack to handle the one variable-size write command. See
	   wavefront_send_multisample() for the other half of this
	   gross and ugly strategy.
	*/

	if (cmd == WFC_DOWNLOAD_MULTISAMPLE) {
		wfcmd->write_cnt = (unsigned int) rbuf;
		rbuf = NULL;
	}

	DPRINT (WF_DEBUG_CMD, "0x%x [%s] (%d,%d,%d)\n",
			       cmd, wfcmd->action, wfcmd->read_cnt,
			       wfcmd->write_cnt, wfcmd->need_ack);
    
	if (wavefront_write (cmd)) { 
		DPRINT ((WF_DEBUG_IO|WF_DEBUG_CMD), "cannot request "
						     "0x%x [%s].\n",
						     cmd, wfcmd->action);
		return 1;
	} 

	if (wfcmd->write_cnt > 0) {
		DPRINT (WF_DEBUG_DATA, "writing %d bytes "
					"for 0x%x\n",
					wfcmd->write_cnt, cmd);

		for (i = 0; i < wfcmd->write_cnt; i++) {
			if (wavefront_write (wbuf[i])) {
				DPRINT (WF_DEBUG_IO, "bad write for byte "
						      "%d of 0x%x [%s].\n",
						      i, cmd, wfcmd->action);
				return 1;
			}

			DPRINT (WF_DEBUG_DATA, "write[%d] = 0x%x\n",
						i, wbuf[i]);
		}
	}

	if (wfcmd->read_cnt > 0) {
		DPRINT (WF_DEBUG_DATA, "reading %d ints "
					"for 0x%x\n",
					wfcmd->read_cnt, cmd);

		for (i = 0; i < wfcmd->read_cnt; i++) {

			if ((c = wavefront_read()) == -1) {
				DPRINT (WF_DEBUG_IO, "bad read for byte "
						      "%d of 0x%x [%s].\n",
						      i, cmd, wfcmd->action);
				return 1;
			}

			/* Now handle errors. Lots of special cases here */
	    
			if (c == 0xff) { 
				if ((c = wavefront_read ()) == -1) {
					DPRINT (WF_DEBUG_IO, "bad read for "
							      "error byte at "
							      "read byte %d "
							      "of 0x%x [%s].\n",
							      i, cmd,
							      wfcmd->action);
					return 1;
				}

				/* Can you believe this madness ? */

				if (c == 1 &&
				    wfcmd->cmd == WFC_IDENTIFY_SAMPLE_TYPE) {
					rbuf[0] = WF_ST_EMPTY;
					return (0);

				} else if (c == 3 &&
					   wfcmd->cmd == WFC_UPLOAD_PATCH) {

					return 3;

				} else if (c == 1 &&
					   wfcmd->cmd == WFC_UPLOAD_PROGRAM) {

					return 1;

				} else {

					DPRINT (WF_DEBUG_IO, "error %d (%s) "
							      "during "
							      "read for byte "
							      "%d of 0x%x "
							      "[%s].\n",
							      c,
							      wavefront_errorstr (c),
							      i, cmd,
							      wfcmd->action);
					return 1;

				}
		
		} else {
				rbuf[i] = c;
			}
			
			DPRINT (WF_DEBUG_DATA, "read[%d] = 0x%x\n",i, rbuf[i]);
		}
	}
	
	if ((wfcmd->read_cnt == 0 && wfcmd->write_cnt == 0) || wfcmd->need_ack) {

		DPRINT (WF_DEBUG_CMD, "reading ACK for 0x%x\n", cmd);

		/* Some commands need an ACK, but return zero instead
		   of the standard value.
		*/
	    
		if ((ack = wavefront_read()) == 0) {
			ack = WF_ACK;
		}
	
		if (ack != WF_ACK) {
			if (ack == -1) {
				DPRINT (WF_DEBUG_IO, "cannot read ack for "
						      "0x%x [%s].\n",
						      cmd, wfcmd->action);
				return 1;
		
			} else {
				int err = -1; /* something unknown */

				if (ack == 0xff) { /* explicit error */
		    
					if ((err = wavefront_read ()) == -1) {
						DPRINT (WF_DEBUG_DATA,
							"cannot read err "
							"for 0x%x [%s].\n",
							cmd, wfcmd->action);
					}
				}
				
				DPRINT (WF_DEBUG_IO, "0x%x [%s] "
					"failed (0x%x, 0x%x, %s)\n",
					cmd, wfcmd->action, ack, err,
					wavefront_errorstr (err));
				
				return -err;
			}
		}
		
		DPRINT (WF_DEBUG_DATA, "ack received "
					"for 0x%x [%s]\n",
					cmd, wfcmd->action);
	} else {

		DPRINT (WF_DEBUG_CMD, "0x%x [%s] does not need "
				       "ACK (%d,%d,%d)\n",
				       cmd, wfcmd->action, wfcmd->read_cnt,
				       wfcmd->write_cnt, wfcmd->need_ack);
	}

	return 0;
	
}

/***********************************************************************
WaveFront: data munging   

Things here are weird. All data written to the board cannot 
have its most significant bit set. Any data item with values 
potentially > 0x7F (127) must be split across multiple bytes.

Sometimes, we need to munge numeric values that are represented on
the x86 side as 8-32 bit values. Sometimes, we need to munge data
that is represented on the x86 side as an array of bytes. The most
efficient approach to handling both cases seems to be to use 2
different functions for munging and 2 for de-munging. This avoids
weird casting and worrying about bit-level offsets.

**********************************************************************/

static 
unsigned char *
munge_int32 (unsigned int src,
	     unsigned char *dst,
	     unsigned int dst_size)
{
	int i;

	for (i = 0;i < dst_size; i++) {
		*dst = src & 0x7F;  /* Mask high bit of LSB */
		src = src >> 7;     /* Rotate Right 7 bits  */
	                            /* Note: we leave the upper bits in place */ 

		dst++;
 	};
	return dst;
};

static int 
demunge_int32 (unsigned char* src, int src_size)

{
	int i;
 	int outval = 0;
	
 	for (i = src_size - 1; i >= 0; i--) {
		outval=(outval<<7)+src[i];
	}

	return outval;
};

static 
unsigned char *
munge_buf (unsigned char *src, unsigned char *dst, unsigned int dst_size)

{
	int i;
	unsigned int last = dst_size / 2;

	for (i = 0; i < last; i++) {
		*dst++ = src[i] & 0x7f;
		*dst++ = src[i] >> 7;
	}
	return dst;
}

static 
unsigned char *
demunge_buf (unsigned char *src, unsigned char *dst, unsigned int src_bytes)

{
	int i;
	unsigned char *end = src + src_bytes;
    
	end = src + src_bytes;

	/* NOTE: src and dst *CAN* point to the same address */

	for (i = 0; src != end; i++) {
		dst[i] = *src++;
		dst[i] |= (*src++)<<7;
	}

	return dst;
}

/***********************************************************************
WaveFront: sample, patch and program management.
***********************************************************************/

static int
wavefront_delete_sample (int sample_num)

{
	unsigned char wbuf[2];
	int x;

	wbuf[0] = sample_num & 0x7f;
	wbuf[1] = sample_num >> 7;

	if ((x = wavefront_cmd (WFC_DELETE_SAMPLE, NULL, wbuf)) == 0) {
		dev.sample_status[sample_num] = WF_ST_EMPTY;
	}

	return x;
}

static int
wavefront_get_sample_status (int assume_rom)

{
	int i;
	unsigned char rbuf[32], wbuf[32];
	unsigned int    sc_real, sc_alias, sc_multi;

	/* check sample status */
    
	if (wavefront_cmd (WFC_GET_NSAMPLES, rbuf, wbuf)) {
		printk (KERN_WARNING LOGNAME "cannot request sample count.\n");
		return -1;
	} 
    
	sc_real = sc_alias = sc_multi = dev.samples_used = 0;
    
	for (i = 0; i < WF_MAX_SAMPLE; i++) {
	
		wbuf[0] = i & 0x7f;
		wbuf[1] = i >> 7;

		if (wavefront_cmd (WFC_IDENTIFY_SAMPLE_TYPE, rbuf, wbuf)) {
			printk (KERN_WARNING LOGNAME
				"cannot identify sample "
				"type of slot %d\n", i);
			dev.sample_status[i] = WF_ST_EMPTY;
			continue;
		}

		dev.sample_status[i] = (WF_SLOT_FILLED|rbuf[0]);

		if (assume_rom) {
			dev.sample_status[i] |= WF_SLOT_ROM;
		}

		switch (rbuf[0] & WF_ST_MASK) {
		case WF_ST_SAMPLE:
			sc_real++;
			break;
		case WF_ST_MULTISAMPLE:
			sc_multi++;
			break;
		case WF_ST_ALIAS:
			sc_alias++;
			break;
		case WF_ST_EMPTY:
			break;

		default:
			printk (KERN_WARNING LOGNAME "unknown sample type for "
				"slot %d (0x%x)\n", 
				i, rbuf[0]);
		}

		if (rbuf[0] != WF_ST_EMPTY) {
			dev.samples_used++;
		} 
	}

	printk (KERN_INFO LOGNAME
		"%d samples used (%d real, %d aliases, %d multi), "
		"%d empty\n", dev.samples_used, sc_real, sc_alias, sc_multi,
		WF_MAX_SAMPLE - dev.samples_used);


	return (0);

}

static int
wavefront_get_patch_status (void)

{
	unsigned char patchbuf[WF_PATCH_BYTES];
	unsigned char patchnum[2];
	wavefront_patch *p;
	int i, x, cnt, cnt2;

	for (i = 0; i < WF_MAX_PATCH; i++) {
		patchnum[0] = i & 0x7f;
		patchnum[1] = i >> 7;

		if ((x = wavefront_cmd (WFC_UPLOAD_PATCH, patchbuf,
					patchnum)) == 0) {

			dev.patch_status[i] |= WF_SLOT_FILLED;
			p = (wavefront_patch *) patchbuf;
			dev.sample_status
				[p->sample_number|(p->sample_msb<<7)] |=
				WF_SLOT_USED;
	    
		} else if (x == 3) { /* Bad patch number */
			dev.patch_status[i] = 0;
		} else {
			printk (KERN_ERR LOGNAME "upload patch "
				"error 0x%x\n", x);
			dev.patch_status[i] = 0;
			return 1;
		}
	}

	/* program status has already filled in slot_used bits */

	for (i = 0, cnt = 0, cnt2 = 0; i < WF_MAX_PATCH; i++) {
		if (dev.patch_status[i] & WF_SLOT_FILLED) {
			cnt++;
		}
		if (dev.patch_status[i] & WF_SLOT_USED) {
			cnt2++;
		}
	
	}
	printk (KERN_INFO LOGNAME
		"%d patch slots filled, %d in use\n", cnt, cnt2);

	return (0);
}

static int
wavefront_get_program_status (void)

{
	unsigned char progbuf[WF_PROGRAM_BYTES];
	wavefront_program prog;
	unsigned char prognum;
	int i, x, l, cnt;

	for (i = 0; i < WF_MAX_PROGRAM; i++) {
		prognum = i;

		if ((x = wavefront_cmd (WFC_UPLOAD_PROGRAM, progbuf,
					&prognum)) == 0) {

			dev.prog_status[i] |= WF_SLOT_USED;

			demunge_buf (progbuf, (unsigned char *) &prog,
				     WF_PROGRAM_BYTES);

			for (l = 0; l < WF_NUM_LAYERS; l++) {
				if (prog.layer[l].mute) {
					dev.patch_status
						[prog.layer[l].patch_number] |=
						WF_SLOT_USED;
				}
			}
		} else if (x == 1) { /* Bad program number */
			dev.prog_status[i] = 0;
		} else {
			printk (KERN_ERR LOGNAME "upload program "
				"error 0x%x\n", x);
			dev.prog_status[i] = 0;
		}
	}

	for (i = 0, cnt = 0; i < WF_MAX_PROGRAM; i++) {
		if (dev.prog_status[i]) {
			cnt++;
		}
	}

	printk (KERN_INFO LOGNAME "%d programs slots in use\n", cnt);

	return (0);
}

static int
wavefront_send_patch (wavefront_patch_info *header)

{
	unsigned char buf[WF_PATCH_BYTES+2];
	unsigned char *bptr;

	DPRINT (WF_DEBUG_LOAD_PATCH, "downloading patch %d\n",
				      header->number);

	dev.patch_status[header->number] |= WF_SLOT_FILLED;

	bptr = buf;
	bptr = munge_int32 (header->number, buf, 2);
	munge_buf ((unsigned char *)&header->hdr.p, bptr, WF_PATCH_BYTES);
    
	if (wavefront_cmd (WFC_DOWNLOAD_PATCH, NULL, buf)) {
		printk (KERN_ERR LOGNAME "download patch failed\n");
		return -(EIO);
	}

	return (0);
}

static int
wavefront_send_program (wavefront_patch_info *header)

{
	unsigned char buf[WF_PROGRAM_BYTES+1];
	int i;

	DPRINT (WF_DEBUG_LOAD_PATCH, "downloading program %d\n",
		header->number);

	dev.prog_status[header->number] = WF_SLOT_USED;

	/* XXX need to zero existing SLOT_USED bit for program_status[i]
	   where `i' is the program that's being (potentially) overwritten.
	*/
    
	for (i = 0; i < WF_NUM_LAYERS; i++) {
		if (header->hdr.pr.layer[i].mute) {
			dev.patch_status[header->hdr.pr.layer[i].patch_number] |=
				WF_SLOT_USED;

			/* XXX need to mark SLOT_USED for sample used by
			   patch_number, but this means we have to load it. Ick.
			*/
		}
	}

	buf[0] = header->number;
	munge_buf ((unsigned char *)&header->hdr.pr, &buf[1], WF_PROGRAM_BYTES);
    
	if (wavefront_cmd (WFC_DOWNLOAD_PROGRAM, NULL, buf)) {
		printk (KERN_WARNING LOGNAME "download patch failed\n");	
		return -(EIO);
	}

	return (0);
}

static int
wavefront_freemem (void)

{
	char rbuf[8];

	if (wavefront_cmd (WFC_REPORT_FREE_MEMORY, rbuf, NULL)) {
		printk (KERN_WARNING LOGNAME "can't get memory stats.\n");
		return -1;
	} else {
		return demunge_int32 (rbuf, 4);
	}
}

static int
wavefront_send_sample (wavefront_patch_info *header,
		       UINT16 __user *dataptr,
		       int data_is_unsigned)

{
	/* samples are downloaded via a 16-bit wide i/o port
	   (you could think of it as 2 adjacent 8-bit wide ports
	   but its less efficient that way). therefore, all
	   the blocksizes and so forth listed in the documentation,
	   and used conventionally to refer to sample sizes,
	   which are given in 8-bit units (bytes), need to be
	   divided by 2.
        */

	UINT16 sample_short;
	UINT32 length;
	UINT16 __user *data_end = NULL;
	unsigned int i;
	const int max_blksize = 4096/2;
	unsigned int written;
	unsigned int blocksize;
	int dma_ack;
	int blocknum;
	unsigned char sample_hdr[WF_SAMPLE_HDR_BYTES];
	unsigned char *shptr;
	int skip = 0;
	int initial_skip = 0;

	DPRINT (WF_DEBUG_LOAD_PATCH, "sample %sdownload for slot %d, "
				      "type %d, %d bytes from %p\n",
				      header->size ? "" : "header ", 
				      header->number, header->subkey,
				      header->size,
				      header->dataptr);

	if (header->number == WAVEFRONT_FIND_FREE_SAMPLE_SLOT) {
		int x;

		if ((x = wavefront_find_free_sample ()) < 0) {
			return -ENOMEM;
		}
		printk (KERN_DEBUG LOGNAME "unspecified sample => %d\n", x);
		header->number = x;
	}

	if (header->size) {

		/* XXX it's a debatable point whether or not RDONLY semantics
		   on the ROM samples should cover just the sample data or
		   the sample header. For now, it only covers the sample data,
		   so anyone is free at all times to rewrite sample headers.

		   My reason for this is that we have the sample headers
		   available in the WFB file for General MIDI, and so these
		   can always be reset if needed. The sample data, however,
		   cannot be recovered without a complete reset and firmware
		   reload of the ICS2115, which is a very expensive operation.

		   So, doing things this way allows us to honor the notion of
		   "RESETSAMPLES" reasonably cheaply. Note however, that this
		   is done purely at user level: there is no WFB parser in
		   this driver, and so a complete reset (back to General MIDI,
		   or theoretically some other configuration) is the
		   responsibility of the user level library. 

		   To try to do this in the kernel would be a little
		   crazy: we'd need 158K of kernel space just to hold
		   a copy of the patch/program/sample header data.
		*/

		if (dev.rom_samples_rdonly) {
			if (dev.sample_status[header->number] & WF_SLOT_ROM) {
				printk (KERN_ERR LOGNAME "sample slot %d "
					"write protected\n",
					header->number);
				return -EACCES;
			}
		}

		wavefront_delete_sample (header->number);
	}

	if (header->size) {
		dev.freemem = wavefront_freemem ();

		if (dev.freemem < header->size) {
			printk (KERN_ERR LOGNAME
				"insufficient memory to "
				"load %d byte sample.\n",
				header->size);
			return -ENOMEM;
		}
	
	}

	skip = WF_GET_CHANNEL(&header->hdr.s);

	if (skip > 0 && header->hdr.s.SampleResolution != LINEAR_16BIT) {
		printk (KERN_ERR LOGNAME "channel selection only "
			"possible on 16-bit samples");
		return -(EINVAL);
	}

	switch (skip) {
	case 0:
		initial_skip = 0;
		skip = 1;
		break;
	case 1:
		initial_skip = 0;
		skip = 2;
		break;
	case 2:
		initial_skip = 1;
		skip = 2;
		break;
	case 3:
		initial_skip = 2;
		skip = 3;
		break;
	case 4:
		initial_skip = 3;
		skip = 4;
		break;
	case 5:
		initial_skip = 4;
		skip = 5;
		break;
	case 6:
		initial_skip = 5;
		skip = 6;
		break;
	}

	DPRINT (WF_DEBUG_LOAD_PATCH, "channel selection: %d => "
				      "initial skip = %d, skip = %d\n",
				      WF_GET_CHANNEL (&header->hdr.s),
				      initial_skip, skip);
    
	/* Be safe, and zero the "Unused" bits ... */

	WF_SET_CHANNEL(&header->hdr.s, 0);

	/* adjust size for 16 bit samples by dividing by two.  We always
	   send 16 bits per write, even for 8 bit samples, so the length
	   is always half the size of the sample data in bytes.
	*/

	length = header->size / 2;

	/* the data we're sent has not been munged, and in fact, the
	   header we have to send isn't just a munged copy either.
	   so, build the sample header right here.
	*/

	shptr = &sample_hdr[0];

	shptr = munge_int32 (header->number, shptr, 2);

	if (header->size) {
		shptr = munge_int32 (length, shptr, 4);
	}

	/* Yes, a 4 byte result doesn't contain all of the offset bits,
	   but the offset only uses 24 bits.
	*/

	shptr = munge_int32 (*((UINT32 *) &header->hdr.s.sampleStartOffset),
			     shptr, 4);
	shptr = munge_int32 (*((UINT32 *) &header->hdr.s.loopStartOffset),
			     shptr, 4);
	shptr = munge_int32 (*((UINT32 *) &header->hdr.s.loopEndOffset),
			     shptr, 4);
	shptr = munge_int32 (*((UINT32 *) &header->hdr.s.sampleEndOffset),
			     shptr, 4);
	
	/* This one is truly weird. What kind of weirdo decided that in
	   a system dominated by 16 and 32 bit integers, they would use
	   a just 12 bits ?
	*/
	
	shptr = munge_int32 (header->hdr.s.FrequencyBias, shptr, 3);
	
	/* Why is this nybblified, when the MSB is *always* zero ? 
	   Anyway, we can't take address of bitfield, so make a
	   good-faith guess at where it starts.
	*/
	
	shptr = munge_int32 (*(&header->hdr.s.FrequencyBias+1),
			     shptr, 2);

	if (wavefront_cmd (header->size ?
			   WFC_DOWNLOAD_SAMPLE : WFC_DOWNLOAD_SAMPLE_HEADER,
			   NULL, sample_hdr)) {
		printk (KERN_WARNING LOGNAME "sample %sdownload refused.\n",
			header->size ? "" : "header ");
		return -(EIO);
	}

	if (header->size == 0) {
		goto sent; /* Sorry. Just had to have one somewhere */
	}
    
	data_end = dataptr + length;

	/* Do any initial skip over an unused channel's data */

	dataptr += initial_skip;
    
	for (written = 0, blocknum = 0;
	     written < length; written += max_blksize, blocknum++) {
	
		if ((length - written) > max_blksize) {
			blocksize = max_blksize;
		} else {
			/* round to nearest 16-byte value */
			blocksize = ((length-written+7)&~0x7);
		}

		if (wavefront_cmd (WFC_DOWNLOAD_BLOCK, NULL, NULL)) {
			printk (KERN_WARNING LOGNAME "download block "
				"request refused.\n");
			return -(EIO);
		}

		for (i = 0; i < blocksize; i++) {

			if (dataptr < data_end) {
		
				__get_user (sample_short, dataptr);
				dataptr += skip;
		
				if (data_is_unsigned) { /* GUS ? */

					if (WF_SAMPLE_IS_8BIT(&header->hdr.s)) {
			
						/* 8 bit sample
						 resolution, sign
						 extend both bytes.
						*/
			
						((unsigned char*)
						 &sample_short)[0] += 0x7f;
						((unsigned char*)
						 &sample_short)[1] += 0x7f;
			
					} else {
			
						/* 16 bit sample
						 resolution, sign
						 extend the MSB.
						*/
			
						sample_short += 0x7fff;
					}
				}

			} else {

				/* In padding section of final block:

				   Don't fetch unsupplied data from
				   user space, just continue with
				   whatever the final value was.
				*/
			}
	    
			if (i < blocksize - 1) {
				outw (sample_short, dev.block_port);
			} else {
				outw (sample_short, dev.last_block_port);
			}
		}

		/* Get "DMA page acknowledge", even though its really
		   nothing to do with DMA at all.
		*/
	
		if ((dma_ack = wavefront_read ()) != WF_DMA_ACK) {
			if (dma_ack == -1) {
				printk (KERN_ERR LOGNAME "upload sample "
					"DMA ack timeout\n");
				return -(EIO);
			} else {
				printk (KERN_ERR LOGNAME "upload sample "
					"DMA ack error 0x%x\n",
					dma_ack);
				return -(EIO);
			}
		}
	}

	dev.sample_status[header->number] = (WF_SLOT_FILLED|WF_ST_SAMPLE);

	/* Note, label is here because sending the sample header shouldn't
	   alter the sample_status info at all.
	*/

 sent:
	return (0);
}

static int
wavefront_send_alias (wavefront_patch_info *header)

{
	unsigned char alias_hdr[WF_ALIAS_BYTES];

	DPRINT (WF_DEBUG_LOAD_PATCH, "download alias, %d is "
				      "alias for %d\n",
				      header->number,
				      header->hdr.a.OriginalSample);
    
	munge_int32 (header->number, &alias_hdr[0], 2);
	munge_int32 (header->hdr.a.OriginalSample, &alias_hdr[2], 2);
	munge_int32 (*((unsigned int *)&header->hdr.a.sampleStartOffset),
		     &alias_hdr[4], 4);
	munge_int32 (*((unsigned int *)&header->hdr.a.loopStartOffset),
		     &alias_hdr[8], 4);
	munge_int32 (*((unsigned int *)&header->hdr.a.loopEndOffset),
		     &alias_hdr[12], 4);
	munge_int32 (*((unsigned int *)&header->hdr.a.sampleEndOffset),
		     &alias_hdr[16], 4);
	munge_int32 (header->hdr.a.FrequencyBias, &alias_hdr[20], 3);
	munge_int32 (*(&header->hdr.a.FrequencyBias+1), &alias_hdr[23], 2);

	if (wavefront_cmd (WFC_DOWNLOAD_SAMPLE_ALIAS, NULL, alias_hdr)) {
		printk (KERN_ERR LOGNAME "download alias failed.\n");
		return -(EIO);
	}

	dev.sample_status[header->number] = (WF_SLOT_FILLED|WF_ST_ALIAS);

	return (0);
}

static int
wavefront_send_multisample (wavefront_patch_info *header)
{
	int i;
	int num_samples;
	unsigned char msample_hdr[WF_MSAMPLE_BYTES];

	munge_int32 (header->number, &msample_hdr[0], 2);

	/* You'll recall at this point that the "number of samples" value
	   in a wavefront_multisample struct is actually the log2 of the
	   real number of samples.
	*/

	num_samples = (1<<(header->hdr.ms.NumberOfSamples&7));
	msample_hdr[2] = (unsigned char) header->hdr.ms.NumberOfSamples;

	DPRINT (WF_DEBUG_LOAD_PATCH, "multi %d with %d=%d samples\n",
				      header->number,
				      header->hdr.ms.NumberOfSamples,
				      num_samples);

	for (i = 0; i < num_samples; i++) {
		DPRINT(WF_DEBUG_LOAD_PATCH|WF_DEBUG_DATA, "sample[%d] = %d\n",
		       i, header->hdr.ms.SampleNumber[i]);
		munge_int32 (header->hdr.ms.SampleNumber[i],
		     &msample_hdr[3+(i*2)], 2);
	}
    
	/* Need a hack here to pass in the number of bytes
	   to be written to the synth. This is ugly, and perhaps
	   one day, I'll fix it.
	*/

	if (wavefront_cmd (WFC_DOWNLOAD_MULTISAMPLE, 
			   (unsigned char *) ((num_samples*2)+3),
			   msample_hdr)) {
		printk (KERN_ERR LOGNAME "download of multisample failed.\n");
		return -(EIO);
	}

	dev.sample_status[header->number] = (WF_SLOT_FILLED|WF_ST_MULTISAMPLE);

	return (0);
}

static int
wavefront_fetch_multisample (wavefront_patch_info *header)
{
	int i;
	unsigned char log_ns[1];
	unsigned char number[2];
	int num_samples;

	munge_int32 (header->number, number, 2);
    
	if (wavefront_cmd (WFC_UPLOAD_MULTISAMPLE, log_ns, number)) {
		printk (KERN_ERR LOGNAME "upload multisample failed.\n");
		return -(EIO);
	}
    
	DPRINT (WF_DEBUG_DATA, "msample %d has %d samples\n",
				header->number, log_ns[0]);

	header->hdr.ms.NumberOfSamples = log_ns[0];

	/* get the number of samples ... */

	num_samples = (1 << log_ns[0]);
    
	for (i = 0; i < num_samples; i++) {
		s8 d[2];
	
		if ((d[0] = wavefront_read ()) == -1) {
			printk (KERN_ERR LOGNAME "upload multisample failed "
				"during sample loop.\n");
			return -(EIO);
		}

		if ((d[1] = wavefront_read ()) == -1) {
			printk (KERN_ERR LOGNAME "upload multisample failed "
				"during sample loop.\n");
			return -(EIO);
		}
	
		header->hdr.ms.SampleNumber[i] =
			demunge_int32 ((unsigned char *) d, 2);
	
		DPRINT (WF_DEBUG_DATA, "msample sample[%d] = %d\n",
					i, header->hdr.ms.SampleNumber[i]);
	}

	return (0);
}


static int
wavefront_send_drum (wavefront_patch_info *header)

{
	unsigned char drumbuf[WF_DRUM_BYTES];
	wavefront_drum *drum = &header->hdr.d;
	int i;

	DPRINT (WF_DEBUG_LOAD_PATCH, "downloading edrum for MIDI "
		"note %d, patch = %d\n", 
		header->number, drum->PatchNumber);

	drumbuf[0] = header->number & 0x7f;

	for (i = 0; i < 4; i++) {
		munge_int32 (((unsigned char *)drum)[i], &drumbuf[1+(i*2)], 2);
	}

	if (wavefront_cmd (WFC_DOWNLOAD_EDRUM_PROGRAM, NULL, drumbuf)) {
		printk (KERN_ERR LOGNAME "download drum failed.\n");
		return -(EIO);
	}

	return (0);
}

static int 
wavefront_find_free_sample (void)

{
	int i;

	for (i = 0; i < WF_MAX_SAMPLE; i++) {
		if (!(dev.sample_status[i] & WF_SLOT_FILLED)) {
			return i;
		}
	}
	printk (KERN_WARNING LOGNAME "no free sample slots!\n");
	return -1;
}

static int 
wavefront_find_free_patch (void)

{
	int i;

	for (i = 0; i < WF_MAX_PATCH; i++) {
		if (!(dev.patch_status[i] & WF_SLOT_FILLED)) {
			return i;
		}
	}
	printk (KERN_WARNING LOGNAME "no free patch slots!\n");
	return -1;
}

static int 
log2_2048(int n)

{
	int tbl[]={0, 0, 2048, 3246, 4096, 4755, 5294, 5749, 6143,
		   6492, 6803, 7084, 7342, 7578, 7797, 8001, 8192,
		   8371, 8540, 8699, 8851, 8995, 9132, 9264, 9390,
		   9510, 9626, 9738, 9845, 9949, 10049, 10146};
	int i;

	/* Returns 2048*log2(n) */

	/* FIXME: this is like doing integer math
	   on quantum particles (RuN) */

	i=0;
	while(n>=32*256) {
		n>>=8;
		i+=2048*8;
	}
	while(n>=32) {
		n>>=1;
		i+=2048;
	}
	i+=tbl[n];
	return(i);
}

static int
wavefront_load_gus_patch (int devno, int format, const char __user *addr,
			  int offs, int count, int pmgr_flag)
{
	struct patch_info guspatch;
	wavefront_patch_info *samp, *pat, *prog;
	wavefront_patch *patp;
	wavefront_sample *sampp;
	wavefront_program *progp;

	int i,base_note;
	long sizeof_patch;
	int rc = -ENOMEM;

	samp = kmalloc(3 * sizeof(wavefront_patch_info), GFP_KERNEL);
	if (!samp)
		goto free_fail;
	pat = samp + 1;
	prog = pat + 1;

	/* Copy in the header of the GUS patch */

	sizeof_patch = (long) &guspatch.data[0] - (long) &guspatch; 
	if (copy_from_user(&((char *) &guspatch)[offs],
			   &(addr)[offs], sizeof_patch - offs)) {
		rc = -EFAULT;
		goto free_fail;
	}

	if ((i = wavefront_find_free_patch ()) == -1) {
		rc = -EBUSY;
		goto free_fail;
	}
	pat->number = i;
	pat->subkey = WF_ST_PATCH;
	patp = &pat->hdr.p;

	if ((i = wavefront_find_free_sample ()) == -1) {
		rc = -EBUSY;
		goto free_fail;
	}
	samp->number = i;
	samp->subkey = WF_ST_SAMPLE;
	samp->size = guspatch.len;
	sampp = &samp->hdr.s;

	prog->number = guspatch.instr_no;
	progp = &prog->hdr.pr;

	/* Setup the patch structure */

	patp->amplitude_bias=guspatch.volume;
	patp->portamento=0;
	patp->sample_number= samp->number & 0xff;
	patp->sample_msb= samp->number >> 8;
	patp->pitch_bend= /*12*/ 0;
	patp->mono=1;
	patp->retrigger=1;
	patp->nohold=(guspatch.mode & WAVE_SUSTAIN_ON) ? 0:1;
	patp->frequency_bias=0;
	patp->restart=0;
	patp->reuse=0;
	patp->reset_lfo=1;
	patp->fm_src2=0;
	patp->fm_src1=WF_MOD_MOD_WHEEL;
	patp->am_src=WF_MOD_PRESSURE;
	patp->am_amount=127;
	patp->fc1_mod_amount=0;
	patp->fc2_mod_amount=0; 
	patp->fm_amount1=0;
	patp->fm_amount2=0;
	patp->envelope1.attack_level=127;
	patp->envelope1.decay1_level=127;
	patp->envelope1.decay2_level=127;
	patp->envelope1.sustain_level=127;
	patp->envelope1.release_level=0;
	patp->envelope2.attack_velocity=127;
	patp->envelope2.attack_level=127;
	patp->envelope2.decay1_level=127;
	patp->envelope2.decay2_level=127;
	patp->envelope2.sustain_level=127;
	patp->envelope2.release_level=0;
	patp->envelope2.attack_velocity=127;
	patp->randomizer=0;

	/* Program for this patch */

	progp->layer[0].patch_number= pat->number; /* XXX is this right ? */
	progp->layer[0].mute=1;
	progp->layer[0].pan_or_mod=1;
	progp->layer[0].pan=7;
	progp->layer[0].mix_level=127  /* guspatch.volume */;
	progp->layer[0].split_type=0;
	progp->layer[0].split_point=0;
	progp->layer[0].play_below=0;

	for (i = 1; i < 4; i++) {
		progp->layer[i].mute=0;
	}

	/* Sample data */

	sampp->SampleResolution=((~guspatch.mode & WAVE_16_BITS)<<1);

	for (base_note=0;
	     note_to_freq (base_note) < guspatch.base_note;
	     base_note++);

	if ((guspatch.base_note-note_to_freq(base_note))
	    >(note_to_freq(base_note)-guspatch.base_note))
		base_note++;

	printk(KERN_DEBUG "ref freq=%d,base note=%d\n",
	       guspatch.base_freq,
	       base_note);

	sampp->FrequencyBias = (29550 - log2_2048(guspatch.base_freq)
				+ base_note*171);
	printk(KERN_DEBUG "Freq Bias is %d\n", sampp->FrequencyBias);
	sampp->Loop=(guspatch.mode & WAVE_LOOPING) ? 1:0;
	sampp->sampleStartOffset.Fraction=0;
	sampp->sampleStartOffset.Integer=0;
	sampp->loopStartOffset.Fraction=0;
	sampp->loopStartOffset.Integer=guspatch.loop_start
		>>((guspatch.mode&WAVE_16_BITS) ? 1:0);
	sampp->loopEndOffset.Fraction=0;
	sampp->loopEndOffset.Integer=guspatch.loop_end
		>>((guspatch.mode&WAVE_16_BITS) ? 1:0);
	sampp->sampleEndOffset.Fraction=0;
	sampp->sampleEndOffset.Integer=guspatch.len >> (guspatch.mode&1);
	sampp->Bidirectional=(guspatch.mode&WAVE_BIDIR_LOOP) ? 1:0;
	sampp->Reverse=(guspatch.mode&WAVE_LOOP_BACK) ? 1:0;

	/* Now ship it down */

	wavefront_send_sample (samp,
			       (unsigned short __user *) &(addr)[sizeof_patch],
			       (guspatch.mode & WAVE_UNSIGNED) ? 1:0);
	wavefront_send_patch (pat);
	wavefront_send_program (prog);

	/* Now pan as best we can ... use the slave/internal MIDI device
	   number if it exists (since it talks to the WaveFront), or the
	   master otherwise.
	*/

	if (dev.mididev > 0) {
		midi_synth_controller (dev.mididev, guspatch.instr_no, 10,
				       ((guspatch.panning << 4) > 127) ?
				       127 : (guspatch.panning << 4));
	}
	rc = 0;

free_fail:
	kfree(samp);
	return rc;
}

static int
wavefront_load_patch (const char __user *addr)


{
	wavefront_patch_info header;
	
	if (copy_from_user (&header, addr, sizeof(wavefront_patch_info) -
			    sizeof(wavefront_any))) {
		printk (KERN_WARNING LOGNAME "bad address for load patch.\n");
		return -EFAULT;
	}

	DPRINT (WF_DEBUG_LOAD_PATCH, "download "
				      "Sample type: %d "
				      "Sample number: %d "
				      "Sample size: %d\n",
				      header.subkey,
				      header.number,
				      header.size);

	switch (header.subkey) {
	case WF_ST_SAMPLE:  /* sample or sample_header, based on patch->size */

		if (copy_from_user((unsigned char *) &header.hdr.s,
				   (unsigned char __user *) header.hdrptr,
				   sizeof (wavefront_sample)))
			return -EFAULT;

		return wavefront_send_sample (&header, header.dataptr, 0);

	case WF_ST_MULTISAMPLE:

		if (copy_from_user(&header.hdr.s, header.hdrptr,
				   sizeof(wavefront_multisample)))
			return -EFAULT;

		return wavefront_send_multisample (&header);


	case WF_ST_ALIAS:

		if (copy_from_user(&header.hdr.a, header.hdrptr,
				   sizeof (wavefront_alias)))
			return -EFAULT;

		return wavefront_send_alias (&header);

	case WF_ST_DRUM:
		if (copy_from_user(&header.hdr.d, header.hdrptr,
				   sizeof (wavefront_drum)))
			return -EFAULT;

		return wavefront_send_drum (&header);

	case WF_ST_PATCH:
		if (copy_from_user(&header.hdr.p, header.hdrptr,
				   sizeof (wavefront_patch)))
			return -EFAULT;

		return wavefront_send_patch (&header);

	case WF_ST_PROGRAM:
		if (copy_from_user(&header.hdr.pr, header.hdrptr,
				   sizeof (wavefront_program)))
			return -EFAULT;

		return wavefront_send_program (&header);

	default:
		printk (KERN_ERR LOGNAME "unknown patch type %d.\n",
			header.subkey);
		return -(EINVAL);
	}

	return 0;
}

/***********************************************************************
WaveFront: /dev/sequencer{,2} and other hardware-dependent interfaces
***********************************************************************/

static void
process_sample_hdr (UCHAR8 *buf)

{
	wavefront_sample s;
	UCHAR8 *ptr;

	ptr = buf;

	/* The board doesn't send us an exact copy of a "wavefront_sample"
	   in response to an Upload Sample Header command. Instead, we 
	   have to convert the data format back into our data structure,
	   just as in the Download Sample command, where we have to do
	   something very similar in the reverse direction.
	*/

	*((UINT32 *) &s.sampleStartOffset) = demunge_int32 (ptr, 4); ptr += 4;
	*((UINT32 *) &s.loopStartOffset) = demunge_int32 (ptr, 4); ptr += 4;
	*((UINT32 *) &s.loopEndOffset) = demunge_int32 (ptr, 4); ptr += 4;
	*((UINT32 *) &s.sampleEndOffset) = demunge_int32 (ptr, 4); ptr += 4;
	*((UINT32 *) &s.FrequencyBias) = demunge_int32 (ptr, 3); ptr += 3;

	s.SampleResolution = *ptr & 0x3;
	s.Loop = *ptr & 0x8;
	s.Bidirectional = *ptr & 0x10;
	s.Reverse = *ptr & 0x40;

	/* Now copy it back to where it came from */

	memcpy (buf, (unsigned char *) &s, sizeof (wavefront_sample));
}

static int
wavefront_synth_control (int cmd, wavefront_control *wc)

{
	unsigned char patchnumbuf[2];
	int i;

	DPRINT (WF_DEBUG_CMD, "synth control with "
		"cmd 0x%x\n", wc->cmd);

	/* Pre-handling of or for various commands */

	switch (wc->cmd) {
	case WFC_DISABLE_INTERRUPTS:
		printk (KERN_INFO LOGNAME "interrupts disabled.\n");
		outb (0x80|0x20, dev.control_port);
		dev.interrupts_on = 0;
		return 0;

	case WFC_ENABLE_INTERRUPTS:
		printk (KERN_INFO LOGNAME "interrupts enabled.\n");
		outb (0x80|0x40|0x20, dev.control_port);
		dev.interrupts_on = 1;
		return 0;

	case WFC_INTERRUPT_STATUS:
		wc->rbuf[0] = dev.interrupts_on;
		return 0;

	case WFC_ROMSAMPLES_RDONLY:
		dev.rom_samples_rdonly = wc->wbuf[0];
		wc->status = 0;
		return 0;

	case WFC_IDENTIFY_SLOT_TYPE:
		i = wc->wbuf[0] | (wc->wbuf[1] << 7);
		if (i <0 || i >= WF_MAX_SAMPLE) {
			printk (KERN_WARNING LOGNAME "invalid slot ID %d\n",
				i);
			wc->status = EINVAL;
			return 0;
		}
		wc->rbuf[0] = dev.sample_status[i];
		wc->status = 0;
		return 0;

	case WFC_DEBUG_DRIVER:
		dev.debug = wc->wbuf[0];
		printk (KERN_INFO LOGNAME "debug = 0x%x\n", dev.debug);
		return 0;

	case WFC_FX_IOCTL:
		wffx_ioctl ((wavefront_fx_info *) &wc->wbuf[0]);
		return 0;

	case WFC_UPLOAD_PATCH:
		munge_int32 (*((UINT32 *) wc->wbuf), patchnumbuf, 2);
		memcpy (wc->wbuf, patchnumbuf, 2);
		break;

	case WFC_UPLOAD_MULTISAMPLE:
		/* multisamples have to be handled differently, and
		   cannot be dealt with properly by wavefront_cmd() alone.
		*/
		wc->status = wavefront_fetch_multisample
			((wavefront_patch_info *) wc->rbuf);
		return 0;

	case WFC_UPLOAD_SAMPLE_ALIAS:
		printk (KERN_INFO LOGNAME "support for sample alias upload "
			"being considered.\n");
		wc->status = EINVAL;
		return -EINVAL;
	}

	wc->status = wavefront_cmd (wc->cmd, wc->rbuf, wc->wbuf);

	/* Post-handling of certain commands.

	   In particular, if the command was an upload, demunge the data
	   so that the user-level doesn't have to think about it.
	*/

	if (wc->status == 0) {
		switch (wc->cmd) {
			/* intercept any freemem requests so that we know
			   we are always current with the user-level view
			   of things.
			*/

		case WFC_REPORT_FREE_MEMORY:
			dev.freemem = demunge_int32 (wc->rbuf, 4);
			break;

		case WFC_UPLOAD_PATCH:
			demunge_buf (wc->rbuf, wc->rbuf, WF_PATCH_BYTES);
			break;

		case WFC_UPLOAD_PROGRAM:
			demunge_buf (wc->rbuf, wc->rbuf, WF_PROGRAM_BYTES);
			break;

		case WFC_UPLOAD_EDRUM_PROGRAM:
			demunge_buf (wc->rbuf, wc->rbuf, WF_DRUM_BYTES - 1);
			break;

		case WFC_UPLOAD_SAMPLE_HEADER:
			process_sample_hdr (wc->rbuf);
			break;

		case WFC_UPLOAD_SAMPLE_ALIAS:
			printk (KERN_INFO LOGNAME "support for "
				"sample aliases still "
				"being considered.\n");
			break;

		case WFC_VMIDI_OFF:
			if (virtual_midi_disable () < 0) {
				return -(EIO);
			}
			break;

		case WFC_VMIDI_ON:
			if (virtual_midi_enable () < 0) {
				return -(EIO);
			}
			break;
		}
	}

	return 0;
}


/***********************************************************************/
/* WaveFront: Linux file system interface (for access via raw synth)    */
/***********************************************************************/

static int 
wavefront_open (struct inode *inode, struct file *file)
{
	/* XXX fix me */
	dev.opened = file->f_flags;
	return 0;
}

static int
wavefront_release(struct inode *inode, struct file *file)
{
	lock_kernel();
	dev.opened = 0;
	dev.debug = 0;
	unlock_kernel();
	return 0;
}

static int
wavefront_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	wavefront_control wc;
	int err;

	switch (cmd) {

	case WFCTL_WFCMD:
		if (copy_from_user(&wc, (void __user *) arg, sizeof (wc)))
			return -EFAULT;
		
		if ((err = wavefront_synth_control (cmd, &wc)) == 0) {
			if (copy_to_user ((void __user *) arg, &wc, sizeof (wc)))
				return -EFAULT;
		}

		return err;
		
	case WFCTL_LOAD_SPP:
		return wavefront_load_patch ((const char __user *) arg);
		
	default:
		printk (KERN_WARNING LOGNAME "invalid ioctl %#x\n", cmd);
		return -(EINVAL);

	}
	return 0;
}

static /*const*/ struct file_operations wavefront_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.ioctl		= wavefront_ioctl,
	.open		= wavefront_open,
	.release	= wavefront_release,
};


/***********************************************************************/
/* WaveFront: OSS installation and support interface                   */
/***********************************************************************/

#if OSS_SUPPORT_LEVEL & OSS_SUPPORT_SEQ

static struct synth_info wavefront_info =
{"Turtle Beach WaveFront", 0, SYNTH_TYPE_SAMPLE, SAMPLE_TYPE_WAVEFRONT,
 0, 32, 0, 0, SYNTH_CAP_INPUT};

static int
wavefront_oss_open (int devno, int mode)

{
	dev.opened = mode;
	return 0;
}

static void
wavefront_oss_close (int devno)

{
	dev.opened = 0;
	dev.debug = 0;
	return;
}

static int
wavefront_oss_ioctl (int devno, unsigned int cmd, void __user * arg)

{
	wavefront_control wc;
	int err;

	switch (cmd) {
	case SNDCTL_SYNTH_INFO:
		if(copy_to_user(arg, &wavefront_info, sizeof (wavefront_info)))
			return -EFAULT;
		return 0;

	case SNDCTL_SEQ_RESETSAMPLES:
//		printk (KERN_WARNING LOGNAME "driver cannot reset samples.\n");
		return 0; /* don't force an error */

	case SNDCTL_SEQ_PERCMODE:
		return 0; /* don't force an error */

	case SNDCTL_SYNTH_MEMAVL:
		if ((dev.freemem = wavefront_freemem ()) < 0) {
			printk (KERN_ERR LOGNAME "cannot get memory size\n");
			return -EIO;
		} else {
			return dev.freemem;
		}
		break;

	case SNDCTL_SYNTH_CONTROL:
		if(copy_from_user (&wc, arg, sizeof (wc)))
			err = -EFAULT;
		else if ((err = wavefront_synth_control (cmd, &wc)) == 0) {
			if(copy_to_user (arg, &wc, sizeof (wc)))
				err = -EFAULT;
		}

		return err;

	default:
		return -(EINVAL);
	}
}

static int
wavefront_oss_load_patch (int devno, int format, const char __user *addr,
			  int offs, int count, int pmgr_flag)
{

	if (format == SYSEX_PATCH) {	/* Handled by midi_synth.c */
		if (midi_load_patch == NULL) {
			printk (KERN_ERR LOGNAME
				"SYSEX not loadable: "
				"no midi patch loader!\n");
			return -(EINVAL);
		}

		return midi_load_patch (devno, format, addr,
					offs, count, pmgr_flag);

	} else if (format == GUS_PATCH) {
		return wavefront_load_gus_patch (devno, format,
						 addr, offs, count, pmgr_flag);

	} else if (format != WAVEFRONT_PATCH) {
		printk (KERN_ERR LOGNAME "unknown patch format %d\n", format);
		return -(EINVAL);
	}

	if (count < sizeof (wavefront_patch_info)) {
		printk (KERN_ERR LOGNAME "sample header too short\n");
		return -(EINVAL);
	}

	/* "addr" points to a user-space wavefront_patch_info */

	return wavefront_load_patch (addr);
}	

static struct synth_operations wavefront_operations =
{
	.owner		= THIS_MODULE,
	.id		= "WaveFront",
	.info		= &wavefront_info,
	.midi_dev	= 0,
	.synth_type	= SYNTH_TYPE_SAMPLE,
	.synth_subtype	= SAMPLE_TYPE_WAVEFRONT,
	.open		= wavefront_oss_open,
	.close		= wavefront_oss_close,
	.ioctl		= wavefront_oss_ioctl,
	.kill_note	= midi_synth_kill_note,
	.start_note	= midi_synth_start_note,
	.set_instr	= midi_synth_set_instr,
	.reset		= midi_synth_reset,
	.load_patch	= midi_synth_load_patch,
	.aftertouch	= midi_synth_aftertouch,
	.controller	= midi_synth_controller,
	.panning	= midi_synth_panning,
	.bender		= midi_synth_bender,
	.setup_voice	= midi_synth_setup_voice
};
#endif /* OSS_SUPPORT_SEQ */

#if OSS_SUPPORT_LEVEL & OSS_SUPPORT_STATIC_INSTALL

static void __init attach_wavefront (struct address_info *hw_config)
{
    (void) install_wavefront ();
}

static int __init probe_wavefront (struct address_info *hw_config)
{
    return !detect_wavefront (hw_config->irq, hw_config->io_base);
}

static void __exit unload_wavefront (struct address_info *hw_config) 
{
    (void) uninstall_wavefront ();
}

#endif /* OSS_SUPPORT_STATIC_INSTALL */

/***********************************************************************/
/* WaveFront: Linux modular sound kernel installation interface        */
/***********************************************************************/

static irqreturn_t
wavefrontintr(int irq, void *dev_id, struct pt_regs *dummy)
{
	struct wf_config *hw = dev_id;

	/*
	   Some comments on interrupts. I attempted a version of this
	   driver that used interrupts throughout the code instead of
	   doing busy and/or sleep-waiting. Alas, it appears that once
	   the Motorola firmware is downloaded, the card *never*
	   generates an RX interrupt. These are successfully generated
	   during firmware loading, and after that wavefront_status()
	   reports that an interrupt is pending on the card from time
	   to time, but it never seems to be delivered to this
	   driver. Note also that wavefront_status() continues to
	   report that RX interrupts are enabled, suggesting that I
	   didn't goof up and disable them by mistake.

	   Thus, I stepped back to a prior version of
	   wavefront_wait(), the only place where this really
	   matters. Its sad, but I've looked through the code to check
	   on things, and I really feel certain that the Motorola
	   firmware prevents RX-ready interrupts.
	*/

	if ((wavefront_status() & (STAT_INTR_READ|STAT_INTR_WRITE)) == 0) {
		return IRQ_NONE;
	}

	hw->irq_ok = 1;
	hw->irq_cnt++;
	wake_up_interruptible (&hw->interrupt_sleeper);
	return IRQ_HANDLED;
}

/* STATUS REGISTER 

0 Host Rx Interrupt Enable (1=Enabled)
1 Host Rx Register Full (1=Full)
2 Host Rx Interrupt Pending (1=Interrupt)
3 Unused
4 Host Tx Interrupt (1=Enabled)
5 Host Tx Register empty (1=Empty)
6 Host Tx Interrupt Pending (1=Interrupt)
7 Unused
*/

static int
wavefront_interrupt_bits (int irq)

{
	int bits;

	switch (irq) {
	case 9:
		bits = 0x00;
		break;
	case 5:
		bits = 0x08;
		break;
	case 12:
		bits = 0x10;
		break;
	case 15:
		bits = 0x18;
		break;
	
	default:
		printk (KERN_WARNING LOGNAME "invalid IRQ %d\n", irq);
		bits = -1;
	}

	return bits;
}

static void
wavefront_should_cause_interrupt (int val, int port, int timeout)

{
	unsigned long flags;

	/* this will not help on SMP - but at least it compiles */
	spin_lock_irqsave(&lock, flags);
	dev.irq_ok = 0;
	outb (val,port);
	interruptible_sleep_on_timeout (&dev.interrupt_sleeper, timeout);
	spin_unlock_irqrestore(&lock,flags);
}

static int __init wavefront_hw_reset (void)
{
	int bits;
	int hwv[2];
	unsigned long irq_mask;
	short reported_irq;

	/* IRQ already checked in init_module() */

	bits = wavefront_interrupt_bits (dev.irq);

	printk (KERN_DEBUG LOGNAME "autodetecting WaveFront IRQ\n");

	irq_mask = probe_irq_on ();

	outb (0x0, dev.control_port); 
	outb (0x80 | 0x40 | bits, dev.data_port);	
	wavefront_should_cause_interrupt(0x80|0x40|0x10|0x1,
					 dev.control_port,
					 (reset_time*HZ)/100);

	reported_irq = probe_irq_off (irq_mask);

	if (reported_irq != dev.irq) {
		if (reported_irq == 0) {
			printk (KERN_ERR LOGNAME
				"No unassigned interrupts detected "
				"after h/w reset\n");
		} else if (reported_irq < 0) {
			printk (KERN_ERR LOGNAME
				"Multiple unassigned interrupts detected "
				"after h/w reset\n");
		} else {
			printk (KERN_ERR LOGNAME "autodetected IRQ %d not the "
				"value provided (%d)\n", reported_irq,
				dev.irq);
		}
		dev.irq = -1;
		return 1;
	} else {
		printk (KERN_INFO LOGNAME "autodetected IRQ at %d\n",
			reported_irq);
	}

	if (request_irq (dev.irq, wavefrontintr,
			 SA_INTERRUPT|SA_SHIRQ,
			 "wavefront synth", &dev) < 0) {
		printk (KERN_WARNING LOGNAME "IRQ %d not available!\n",
			dev.irq);
		return 1;
	}

	/* try reset of port */
      
	outb (0x0, dev.control_port); 
  
	/* At this point, the board is in reset, and the H/W initialization
	   register is accessed at the same address as the data port.
     
	   Bit 7 - Enable IRQ Driver	
	   0 - Tri-state the Wave-Board drivers for the PC Bus IRQs
	   1 - Enable IRQ selected by bits 5:3 to be driven onto the PC Bus.
     
	   Bit 6 - MIDI Interface Select

	   0 - Use the MIDI Input from the 26-pin WaveBlaster
	   compatible header as the serial MIDI source
	   1 - Use the MIDI Input from the 9-pin D connector as the
	   serial MIDI source.
     
	   Bits 5:3 - IRQ Selection
	   0 0 0 - IRQ 2/9
	   0 0 1 - IRQ 5
	   0 1 0 - IRQ 12
	   0 1 1 - IRQ 15
	   1 0 0 - Reserved
	   1 0 1 - Reserved
	   1 1 0 - Reserved
	   1 1 1 - Reserved
     
	   Bits 2:1 - Reserved
	   Bit 0 - Disable Boot ROM
	   0 - memory accesses to 03FC30-03FFFFH utilize the internal Boot ROM
	   1 - memory accesses to 03FC30-03FFFFH are directed to external 
	   storage.
     
	*/

	/* configure hardware: IRQ, enable interrupts, 
	   plus external 9-pin MIDI interface selected
	*/

	outb (0x80 | 0x40 | bits, dev.data_port);	
  
	/* CONTROL REGISTER

	   0 Host Rx Interrupt Enable (1=Enabled)      0x1
	   1 Unused                                    0x2
	   2 Unused                                    0x4
	   3 Unused                                    0x8
	   4 Host Tx Interrupt Enable                 0x10
	   5 Mute (0=Mute; 1=Play)                    0x20
	   6 Master Interrupt Enable (1=Enabled)      0x40
	   7 Master Reset (0=Reset; 1=Run)            0x80

	   Take us out of reset, mute output, master + TX + RX interrupts on.
	   
	   We'll get an interrupt presumably to tell us that the TX
	   register is clear.
	*/

	wavefront_should_cause_interrupt(0x80|0x40|0x10|0x1,
					 dev.control_port,
					 (reset_time*HZ)/100);

	/* Note: data port is now the data port, not the h/w initialization
	   port.
	 */

	if (!dev.irq_ok) {
		printk (KERN_WARNING LOGNAME
			"intr not received after h/w un-reset.\n");
		goto gone_bad;
	} 

	dev.interrupts_on = 1;
	
	/* Note: data port is now the data port, not the h/w initialization
	   port.

	   At this point, only "HW VERSION" or "DOWNLOAD OS" commands
	   will work. So, issue one of them, and wait for TX
	   interrupt. This can take a *long* time after a cold boot,
	   while the ISC ROM does its RAM test. The SDK says up to 4
	   seconds - with 12MB of RAM on a Tropez+, it takes a lot
	   longer than that (~16secs). Note that the card understands
	   the difference between a warm and a cold boot, so
	   subsequent ISC2115 reboots (say, caused by module
	   reloading) will get through this much faster.

	   XXX Interesting question: why is no RX interrupt received first ?
	*/

	wavefront_should_cause_interrupt(WFC_HARDWARE_VERSION, 
					 dev.data_port, ramcheck_time*HZ);

	if (!dev.irq_ok) {
		printk (KERN_WARNING LOGNAME
			"post-RAM-check interrupt not received.\n");
		goto gone_bad;
	} 

	if (!wavefront_wait (STAT_CAN_READ)) {
		printk (KERN_WARNING LOGNAME
			"no response to HW version cmd.\n");
		goto gone_bad;
	}
	
	if ((hwv[0] = wavefront_read ()) == -1) {
		printk (KERN_WARNING LOGNAME
			"board not responding correctly.\n");
		goto gone_bad;
	}

	if (hwv[0] == 0xFF) { /* NAK */

		/* Board's RAM test failed. Try to read error code,
		   and tell us about it either way.
		*/
		
		if ((hwv[0] = wavefront_read ()) == -1) {
			printk (KERN_WARNING LOGNAME "on-board RAM test failed "
				"(bad error code).\n");
		} else {
			printk (KERN_WARNING LOGNAME "on-board RAM test failed "
				"(error code: 0x%x).\n",
				hwv[0]);
		}
		goto gone_bad;
	}

	/* We're OK, just get the next byte of the HW version response */

	if ((hwv[1] = wavefront_read ()) == -1) {
		printk (KERN_WARNING LOGNAME "incorrect h/w response.\n");
		goto gone_bad;
	}

	printk (KERN_INFO LOGNAME "hardware version %d.%d\n",
		hwv[0], hwv[1]);

	return 0;


     gone_bad:
	if (dev.irq >= 0) {
		free_irq (dev.irq, &dev);
		dev.irq = -1;
	}
	return (1);
}

static int __init detect_wavefront (int irq, int io_base)
{
	unsigned char   rbuf[4], wbuf[4];

	/* TB docs say the device takes up 8 ports, but we know that
	   if there is an FX device present (i.e. a Tropez+) it really
	   consumes 16.
	*/

	if (!request_region (io_base, 16, "wavfront")) {
		printk (KERN_ERR LOGNAME "IO address range 0x%x - 0x%x "
			"already in use - ignored\n", dev.base,
			dev.base+15);
		return -1;
	}
  
	dev.irq = irq;
	dev.base = io_base;
	dev.israw = 0;
	dev.debug = debug_default;
	dev.interrupts_on = 0;
	dev.irq_cnt = 0;
	dev.rom_samples_rdonly = 1; /* XXX default lock on ROM sample slots */

	if (wavefront_cmd (WFC_FIRMWARE_VERSION, rbuf, wbuf) == 0) {

		dev.fw_version[0] = rbuf[0];
		dev.fw_version[1] = rbuf[1];
		printk (KERN_INFO LOGNAME
			"firmware %d.%d already loaded.\n",
			rbuf[0], rbuf[1]);

		/* check that a command actually works */
      
		if (wavefront_cmd (WFC_HARDWARE_VERSION,
				   rbuf, wbuf) == 0) {
			dev.hw_version[0] = rbuf[0];
			dev.hw_version[1] = rbuf[1];
		} else {
			printk (KERN_WARNING LOGNAME "not raw, but no "
				"hardware version!\n");
			release_region (io_base, 16);
			return 0;
		}

		if (!wf_raw) {
			/* will re-acquire region in install_wavefront() */
			release_region (io_base, 16);
			return 1;
		} else {
			printk (KERN_INFO LOGNAME
				"reloading firmware anyway.\n");
			dev.israw = 1;
		}

	} else {

		dev.israw = 1;
		printk (KERN_INFO LOGNAME
			"no response to firmware probe, assume raw.\n");

	}

	init_waitqueue_head (&dev.interrupt_sleeper);

	if (wavefront_hw_reset ()) {
		printk (KERN_WARNING LOGNAME "hardware reset failed\n");
		release_region (io_base, 16);
		return 0;
	}

	/* Check for FX device, present only on Tropez+ */

	dev.has_fx = (detect_wffx () == 0);

	/* will re-acquire region in install_wavefront() */
	release_region (io_base, 16);
	return 1;
}

#include "os.h"
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <asm/uaccess.h>


static int
wavefront_download_firmware (char *path)

{
	unsigned char section[WF_SECTION_MAX];
	char section_length; /* yes, just a char; max value is WF_SECTION_MAX */
	int section_cnt_downloaded = 0;
	int fd;
	int c;
	int i;
	mm_segment_t fs;

	/* This tries to be a bit cleverer than the stuff Alan Cox did for
	   the generic sound firmware, in that it actually knows
	   something about the structure of the Motorola firmware. In
	   particular, it uses a version that has been stripped of the
	   20K of useless header information, and had section lengths
	   added, making it possible to load the entire OS without any
	   [kv]malloc() activity, since the longest entity we ever read is
	   42 bytes (well, WF_SECTION_MAX) long.
	*/

	fs = get_fs();
	set_fs (get_ds());

	if ((fd = sys_open (path, 0, 0)) < 0) {
		printk (KERN_WARNING LOGNAME "Unable to load \"%s\".\n",
			path);
		return 1;
	}

	while (1) {
		int x;

		if ((x = sys_read (fd, &section_length, sizeof (section_length))) !=
		    sizeof (section_length)) {
			printk (KERN_ERR LOGNAME "firmware read error.\n");
			goto failure;
		}

		if (section_length == 0) {
			break;
		}

		if (sys_read (fd, section, section_length) != section_length) {
			printk (KERN_ERR LOGNAME "firmware section "
				"read error.\n");
			goto failure;
		}

		/* Send command */
	
		if (wavefront_write (WFC_DOWNLOAD_OS)) {
			goto failure;
		}
	
		for (i = 0; i < section_length; i++) {
			if (wavefront_write (section[i])) {
				goto failure;
			}
		}
	
		/* get ACK */
	
		if (wavefront_wait (STAT_CAN_READ)) {

			if ((c = inb (dev.data_port)) != WF_ACK) {

				printk (KERN_ERR LOGNAME "download "
					"of section #%d not "
					"acknowledged, ack = 0x%x\n",
					section_cnt_downloaded + 1, c);
				goto failure;
		
			}

		} else {
			printk (KERN_ERR LOGNAME "time out for firmware ACK.\n");
			goto failure;
		}

	}

	sys_close (fd);
	set_fs (fs);
	return 0;

 failure:
	sys_close (fd);
	set_fs (fs);
	printk (KERN_ERR "\nWaveFront: firmware download failed!!!\n");
	return 1;
}

static int __init wavefront_config_midi (void)
{
	unsigned char rbuf[4], wbuf[4];
    
	if (detect_wf_mpu (dev.irq, dev.base) < 0) {
		printk (KERN_WARNING LOGNAME
			"could not find working MIDI device\n");
		return -1;
	} 

	if ((dev.mididev = install_wf_mpu ()) < 0) {
		printk (KERN_WARNING LOGNAME
			"MIDI interfaces not configured\n");
		return -1;
	}

	/* Route external MIDI to WaveFront synth (by default) */
    
	if (wavefront_cmd (WFC_MISYNTH_ON, rbuf, wbuf)) {
		printk (KERN_WARNING LOGNAME
			"cannot enable MIDI-IN to synth routing.\n");
		/* XXX error ? */
	}


#if OSS_SUPPORT_LEVEL & OSS_SUPPORT_SEQ
	/* Get the regular MIDI patch loading function, so we can
	   use it if we ever get handed a SYSEX patch. This is
	   unlikely, because its so damn slow, but we may as well
	   leave this functionality from maui.c behind, since it
	   could be useful for sequencer applications that can
	   only use MIDI to do patch loading.
	*/

	if (midi_devs[dev.mididev]->converter != NULL) {
		midi_load_patch = midi_devs[dev.mididev]->converter->load_patch;
		midi_devs[dev.mididev]->converter->load_patch =
		    &wavefront_oss_load_patch;
	}

#endif /* OSS_SUPPORT_SEQ */
	
	/* Turn on Virtual MIDI, but first *always* turn it off,
	   since otherwise consectutive reloads of the driver will
	   never cause the hardware to generate the initial "internal" or 
	   "external" source bytes in the MIDI data stream. This
	   is pretty important, since the internal hardware generally will
	   be used to generate none or very little MIDI output, and
	   thus the only source of MIDI data is actually external. Without
	   the switch bytes, the driver will think it all comes from
	   the internal interface. Duh.
	*/

	if (wavefront_cmd (WFC_VMIDI_OFF, rbuf, wbuf)) { 
		printk (KERN_WARNING LOGNAME
			"virtual MIDI mode not disabled\n");
		return 0; /* We're OK, but missing the external MIDI dev */
	}

	if ((dev.ext_mididev = virtual_midi_enable ()) < 0) {
		printk (KERN_WARNING LOGNAME "no virtual MIDI access.\n");
	} else {
		if (wavefront_cmd (WFC_VMIDI_ON, rbuf, wbuf)) {
			printk (KERN_WARNING LOGNAME
				"cannot enable virtual MIDI mode.\n");
			virtual_midi_disable ();
		} 
	}
    
	return 0;
}

static int __init wavefront_do_reset (int atboot)
{
	char voices[1];

	if (!atboot && wavefront_hw_reset ()) {
		printk (KERN_WARNING LOGNAME "hw reset failed.\n");
		goto gone_bad;
	}

	if (dev.israw) {
		if (wavefront_download_firmware (ospath)) {
			goto gone_bad;
		}

		dev.israw = 0;

		/* Wait for the OS to get running. The protocol for
		   this is non-obvious, and was determined by
		   using port-IO tracing in DOSemu and some
		   experimentation here.
		   
		   Rather than using timed waits, use interrupts creatively.
		*/

		wavefront_should_cause_interrupt (WFC_NOOP,
						  dev.data_port,
						  (osrun_time*HZ));

		if (!dev.irq_ok) {
			printk (KERN_WARNING LOGNAME
				"no post-OS interrupt.\n");
			goto gone_bad;
		}
		
		/* Now, do it again ! */
		
		wavefront_should_cause_interrupt (WFC_NOOP,
						  dev.data_port, (10*HZ));
		
		if (!dev.irq_ok) {
			printk (KERN_WARNING LOGNAME
				"no post-OS interrupt(2).\n");
			goto gone_bad;
		}

		/* OK, no (RX/TX) interrupts any more, but leave mute
		   in effect. 
		*/
		
		outb (0x80|0x40, dev.control_port); 

		/* No need for the IRQ anymore */
		
		free_irq (dev.irq, &dev);

	}

	if (dev.has_fx && fx_raw) {
		wffx_init ();
	}

	/* SETUPSND.EXE asks for sample memory config here, but since i
	   have no idea how to interpret the result, we'll forget
	   about it.
	*/
	
	if ((dev.freemem = wavefront_freemem ()) < 0) {
		goto gone_bad;
	}
		
	printk (KERN_INFO LOGNAME "available DRAM %dk\n", dev.freemem / 1024);

	if (wavefront_write (0xf0) ||
	    wavefront_write (1) ||
	    (wavefront_read () < 0)) {
		dev.debug = 0;
		printk (KERN_WARNING LOGNAME "MPU emulation mode not set.\n");
		goto gone_bad;
	}

	voices[0] = 32;

	if (wavefront_cmd (WFC_SET_NVOICES, NULL, voices)) {
		printk (KERN_WARNING LOGNAME
			"cannot set number of voices to 32.\n");
		goto gone_bad;
	}


	return 0;

 gone_bad:
	/* reset that sucker so that it doesn't bother us. */

	outb (0x0, dev.control_port);
	dev.interrupts_on = 0;
	if (dev.irq >= 0) {
		free_irq (dev.irq, &dev);
	}
	return 1;
}

static int __init wavefront_init (int atboot)
{
	int samples_are_from_rom;

	if (dev.israw) {
		samples_are_from_rom = 1;
	} else {
		/* XXX is this always true ? */
		samples_are_from_rom = 0;
	}

	if (dev.israw || fx_raw) {
		if (wavefront_do_reset (atboot)) {
			return -1;
		}
	}

	wavefront_get_sample_status (samples_are_from_rom);
	wavefront_get_program_status ();
	wavefront_get_patch_status ();

	/* Start normal operation: unreset, master interrupt enabled, no mute
	*/

	outb (0x80|0x40|0x20, dev.control_port); 

	return (0);
}

static int __init install_wavefront (void)
{
	if (!request_region (dev.base+2, 6, "wavefront synth"))
		return -1;

	if (dev.has_fx) {
		if (!request_region (dev.base+8, 8, "wavefront fx")) {
			release_region (dev.base+2, 6);
			return -1;
		}
	}

	if ((dev.synth_dev = register_sound_synth (&wavefront_fops, -1)) < 0) {
		printk (KERN_ERR LOGNAME "cannot register raw synth\n");
		goto err_out;
	}

#if OSS_SUPPORT_LEVEL & OSS_SUPPORT_SEQ
	if ((dev.oss_dev = sound_alloc_synthdev()) == -1) {
		printk (KERN_ERR LOGNAME "Too many sequencers\n");
		/* FIXME: leak: should unregister sound synth */
		goto err_out;
	} else {
		synth_devs[dev.oss_dev] = &wavefront_operations;
	}
#endif /* OSS_SUPPORT_SEQ */

	if (wavefront_init (1) < 0) {
		printk (KERN_WARNING LOGNAME "initialization failed.\n");

#if OSS_SUPPORT_LEVEL & OSS_SUPPORT_SEQ
		sound_unload_synthdev (dev.oss_dev);
#endif /* OSS_SUPPORT_SEQ */ 

		goto err_out;
	}
    
	if (wavefront_config_midi ()) {
		printk (KERN_WARNING LOGNAME "could not initialize MIDI.\n");
	}

	return dev.oss_dev;

err_out:
	release_region (dev.base+2, 6);
	if (dev.has_fx)
		release_region (dev.base+8, 8);
	return -1;
}

static void __exit uninstall_wavefront (void)
{
	/* the first two i/o addresses are freed by the wf_mpu code */
	release_region (dev.base+2, 6);

	if (dev.has_fx) {
		release_region (dev.base+8, 8);
	}

	unregister_sound_synth (dev.synth_dev);

#if OSS_SUPPORT_LEVEL & OSS_SUPPORT_SEQ
	sound_unload_synthdev (dev.oss_dev);
#endif /* OSS_SUPPORT_SEQ */ 
	uninstall_wf_mpu ();
}

/***********************************************************************/
/*   WaveFront FX control                                              */
/***********************************************************************/

#include "yss225.h"

/* Control bits for the Load Control Register
 */

#define FX_LSB_TRANSFER 0x01    /* transfer after DSP LSB byte written */
#define FX_MSB_TRANSFER 0x02    /* transfer after DSP MSB byte written */
#define FX_AUTO_INCR    0x04    /* auto-increment DSP address after transfer */

static int
wffx_idle (void) 
    
{
	int i;
	unsigned int x = 0x80;
    
	for (i = 0; i < 1000; i++) {
		x = inb (dev.fx_status);
		if ((x & 0x80) == 0) {
			break;
		}
	}
    
	if (x & 0x80) {
		printk (KERN_ERR LOGNAME "FX device never idle.\n");
		return 0;
	}
    
	return (1);
}

int __init detect_wffx (void)
{
	/* This is a crude check, but its the best one I have for now.
	   Certainly on the Maui and the Tropez, wffx_idle() will
	   report "never idle", which suggests that this test should
	   work OK.
	*/

	if (inb (dev.fx_status) & 0x80) {
		printk (KERN_INFO LOGNAME "Hmm, probably a Maui or Tropez.\n");
		return -1;
	}

	return 0;
}	

static void
wffx_mute (int onoff)
    
{
	if (!wffx_idle()) {
		return;
	}
    
	outb (onoff ? 0x02 : 0x00, dev.fx_op);
}

static int
wffx_memset (int page,
	     int addr, int cnt, unsigned short *data)
{
	if (page < 0 || page > 7) {
		printk (KERN_ERR LOGNAME "FX memset: "
			"page must be >= 0 and <= 7\n");
		return -(EINVAL);
	}

	if (addr < 0 || addr > 0x7f) {
		printk (KERN_ERR LOGNAME "FX memset: "
			"addr must be >= 0 and <= 7f\n");
		return -(EINVAL);
	}

	if (cnt == 1) {

		outb (FX_LSB_TRANSFER, dev.fx_lcr);
		outb (page, dev.fx_dsp_page);
		outb (addr, dev.fx_dsp_addr);
		outb ((data[0] >> 8), dev.fx_dsp_msb);
		outb ((data[0] & 0xff), dev.fx_dsp_lsb);

		printk (KERN_INFO LOGNAME "FX: addr %d:%x set to 0x%x\n",
			page, addr, data[0]);
	
	} else {
		int i;

		outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
		outb (page, dev.fx_dsp_page);
		outb (addr, dev.fx_dsp_addr);

		for (i = 0; i < cnt; i++) {
			outb ((data[i] >> 8), dev.fx_dsp_msb);
			outb ((data[i] & 0xff), dev.fx_dsp_lsb);
			if (!wffx_idle ()) {
				break;
			}
		}

		if (i != cnt) {
			printk (KERN_WARNING LOGNAME
				"FX memset "
				"(0x%x, 0x%x, %p, %d) incomplete\n",
				page, addr, data, cnt);
			return -(EIO);
		}
	}

	return 0;
}

static int
wffx_ioctl (wavefront_fx_info *r)

{
	unsigned short page_data[256];
	unsigned short *pd;

	switch (r->request) {
	case WFFX_MUTE:
		wffx_mute (r->data[0]);
		return 0;

	case WFFX_MEMSET:

		if (r->data[2] <= 0) {
			printk (KERN_ERR LOGNAME "cannot write "
				"<= 0 bytes to FX\n");
			return -(EINVAL);
		} else if (r->data[2] == 1) {
			pd = (unsigned short *) &r->data[3];
		} else {
			if (r->data[2] > sizeof (page_data)) {
				printk (KERN_ERR LOGNAME "cannot write "
					"> 255 bytes to FX\n");
				return -(EINVAL);
			}
			if (copy_from_user(page_data,
					   (unsigned char __user *)r->data[3],
					   r->data[2]))
				return -EFAULT;
			pd = page_data;
		}

		return wffx_memset (r->data[0], /* page */
				    r->data[1], /* addr */
				    r->data[2], /* cnt */
				    pd);

	default:
		printk (KERN_WARNING LOGNAME
			"FX: ioctl %d not yet supported\n",
			r->request);
		return -(EINVAL);
	}
}

/* YSS225 initialization.

   This code was developed using DOSEMU. The Turtle Beach SETUPSND
   utility was run with I/O tracing in DOSEMU enabled, and a reconstruction
   of the port I/O done, using the Yamaha faxback document as a guide
   to add more logic to the code. Its really pretty weird.

   There was an alternative approach of just dumping the whole I/O
   sequence as a series of port/value pairs and a simple loop
   that output it. However, I hope that eventually I'll get more
   control over what this code does, and so I tried to stick with
   a somewhat "algorithmic" approach.
*/

static int __init wffx_init (void)
{
	int i;
	int j;

	/* Set all bits for all channels on the MOD unit to zero */
	/* XXX But why do this twice ? */

	for (j = 0; j < 2; j++) {
		for (i = 0x10; i <= 0xff; i++) {
	    
			if (!wffx_idle ()) {
				return (-1);
			}
	    
			outb (i, dev.fx_mod_addr);
			outb (0x0, dev.fx_mod_data);
		}
	}

	if (!wffx_idle()) return (-1);
	outb (0x02, dev.fx_op);                        /* mute on */

	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x44, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x42, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x43, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x7c, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x7e, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x46, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x49, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x47, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x4a, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);

	/* either because of stupidity by TB's programmers, or because it
	   actually does something, rezero the MOD page.
	*/
	for (i = 0x10; i <= 0xff; i++) {
	
		if (!wffx_idle ()) {
			return (-1);
		}
	
		outb (i, dev.fx_mod_addr);
		outb (0x0, dev.fx_mod_data);
	}
	/* load page zero */

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x00, dev.fx_dsp_page);
	outb (0x00, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_zero); i += 2) {
		outb (page_zero[i], dev.fx_dsp_msb);
		outb (page_zero[i+1], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}

	/* Now load page one */

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x01, dev.fx_dsp_page);
	outb (0x00, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_one); i += 2) {
		outb (page_one[i], dev.fx_dsp_msb);
		outb (page_one[i+1], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x02, dev.fx_dsp_page);
	outb (0x00, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_two); i++) {
		outb (page_two[i], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x03, dev.fx_dsp_page);
	outb (0x00, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_three); i++) {
		outb (page_three[i], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x04, dev.fx_dsp_page);
	outb (0x00, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_four); i++) {
		outb (page_four[i], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}

	/* Load memory area (page six) */
    
	outb (FX_LSB_TRANSFER, dev.fx_lcr); 
	outb (0x06, dev.fx_dsp_page); 

	for (i = 0; i < sizeof (page_six); i += 3) {
		outb (page_six[i], dev.fx_dsp_addr);
		outb (page_six[i+1], dev.fx_dsp_msb);
		outb (page_six[i+2], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x07, dev.fx_dsp_page);
	outb (0x00, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_seven); i += 2) {
		outb (page_seven[i], dev.fx_dsp_msb);
		outb (page_seven[i+1], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}

	/* Now setup the MOD area. We do this algorithmically in order to
	   save a little data space. It could be done in the same fashion
	   as the "pages".
	*/

	for (i = 0x00; i <= 0x0f; i++) {
		outb (0x01, dev.fx_mod_addr);
		outb (i, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
		outb (0x02, dev.fx_mod_addr);
		outb (0x00, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	for (i = 0xb0; i <= 0xbf; i++) {
		outb (i, dev.fx_mod_addr);
		outb (0x20, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	for (i = 0xf0; i <= 0xff; i++) {
		outb (i, dev.fx_mod_addr);
		outb (0x20, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	for (i = 0x10; i <= 0x1d; i++) {
		outb (i, dev.fx_mod_addr);
		outb (0xff, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	outb (0x1e, dev.fx_mod_addr);
	outb (0x40, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);

	for (i = 0x1f; i <= 0x2d; i++) {
		outb (i, dev.fx_mod_addr);
		outb (0xff, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	outb (0x2e, dev.fx_mod_addr);
	outb (0x00, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);

	for (i = 0x2f; i <= 0x3e; i++) {
		outb (i, dev.fx_mod_addr);
		outb (0x00, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	outb (0x3f, dev.fx_mod_addr);
	outb (0x20, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);

	for (i = 0x40; i <= 0x4d; i++) {
		outb (i, dev.fx_mod_addr);
		outb (0x00, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	outb (0x4e, dev.fx_mod_addr);
	outb (0x0e, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);
	outb (0x4f, dev.fx_mod_addr);
	outb (0x0e, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);


	for (i = 0x50; i <= 0x6b; i++) {
		outb (i, dev.fx_mod_addr);
		outb (0x00, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	outb (0x6c, dev.fx_mod_addr);
	outb (0x40, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);

	outb (0x6d, dev.fx_mod_addr);
	outb (0x00, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);

	outb (0x6e, dev.fx_mod_addr);
	outb (0x40, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);

	outb (0x6f, dev.fx_mod_addr);
	outb (0x40, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);

	for (i = 0x70; i <= 0x7f; i++) {
		outb (i, dev.fx_mod_addr);
		outb (0xc0, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}
    
	for (i = 0x80; i <= 0xaf; i++) {
		outb (i, dev.fx_mod_addr);
		outb (0x00, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	for (i = 0xc0; i <= 0xdd; i++) {
		outb (i, dev.fx_mod_addr);
		outb (0x00, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	outb (0xde, dev.fx_mod_addr);
	outb (0x10, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);
	outb (0xdf, dev.fx_mod_addr);
	outb (0x10, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);

	for (i = 0xe0; i <= 0xef; i++) {
		outb (i, dev.fx_mod_addr);
		outb (0x00, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	for (i = 0x00; i <= 0x0f; i++) {
		outb (0x01, dev.fx_mod_addr);
		outb (i, dev.fx_mod_data);
		outb (0x02, dev.fx_mod_addr);
		outb (0x01, dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	outb (0x02, dev.fx_op); /* mute on */

	/* Now set the coefficients and so forth for the programs above */

	for (i = 0; i < sizeof (coefficients); i += 4) {
		outb (coefficients[i], dev.fx_dsp_page);
		outb (coefficients[i+1], dev.fx_dsp_addr);
		outb (coefficients[i+2], dev.fx_dsp_msb);
		outb (coefficients[i+3], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}

	/* Some settings (?) that are too small to bundle into loops */

	if (!wffx_idle()) return (-1);
	outb (0x1e, dev.fx_mod_addr);
	outb (0x14, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);
	outb (0xde, dev.fx_mod_addr);
	outb (0x20, dev.fx_mod_data);
	if (!wffx_idle()) return (-1);
	outb (0xdf, dev.fx_mod_addr);
	outb (0x20, dev.fx_mod_data);
    
	/* some more coefficients */

	if (!wffx_idle()) return (-1);
	outb (0x06, dev.fx_dsp_page);
	outb (0x78, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x40, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x03, dev.fx_dsp_addr);
	outb (0x0f, dev.fx_dsp_msb);
	outb (0xff, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x0b, dev.fx_dsp_addr);
	outb (0x0f, dev.fx_dsp_msb);
	outb (0xff, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x02, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x0a, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x46, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
	if (!wffx_idle()) return (-1);
	outb (0x07, dev.fx_dsp_page);
	outb (0x49, dev.fx_dsp_addr);
	outb (0x00, dev.fx_dsp_msb);
	outb (0x00, dev.fx_dsp_lsb);
    
	/* Now, for some strange reason, lets reload every page
	   and all the coefficients over again. I have *NO* idea
	   why this is done. I do know that no sound is produced
	   is this phase is omitted.
	*/

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x00, dev.fx_dsp_page);  
	outb (0x10, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_zero_v2); i += 2) {
		outb (page_zero_v2[i], dev.fx_dsp_msb);
		outb (page_zero_v2[i+1], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x01, dev.fx_dsp_page);
	outb (0x10, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_one_v2); i += 2) {
		outb (page_one_v2[i], dev.fx_dsp_msb);
		outb (page_one_v2[i+1], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}
    
	if (!wffx_idle()) return (-1);
	if (!wffx_idle()) return (-1);
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x02, dev.fx_dsp_page);
	outb (0x10, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_two_v2); i++) {
		outb (page_two_v2[i], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x03, dev.fx_dsp_page);
	outb (0x10, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_three_v2); i++) {
		outb (page_three_v2[i], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x04, dev.fx_dsp_page);
	outb (0x10, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_four_v2); i++) {
		outb (page_four_v2[i], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}
    
	outb (FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x06, dev.fx_dsp_page);

	/* Page six v.2 is algorithmic */
    
	for (i = 0x10; i <= 0x3e; i += 2) {
		outb (i, dev.fx_dsp_addr);
		outb (0x00, dev.fx_dsp_msb);
		outb (0x00, dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev.fx_lcr);
	outb (0x07, dev.fx_dsp_page);
	outb (0x10, dev.fx_dsp_addr);

	for (i = 0; i < sizeof (page_seven_v2); i += 2) {
		outb (page_seven_v2[i], dev.fx_dsp_msb);
		outb (page_seven_v2[i+1], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}

	for (i = 0x00; i < sizeof(mod_v2); i += 2) {
		outb (mod_v2[i], dev.fx_mod_addr);
		outb (mod_v2[i+1], dev.fx_mod_data);
		if (!wffx_idle()) return (-1);
	}

	for (i = 0; i < sizeof (coefficients2); i += 4) {
		outb (coefficients2[i], dev.fx_dsp_page);
		outb (coefficients2[i+1], dev.fx_dsp_addr);
		outb (coefficients2[i+2], dev.fx_dsp_msb);
		outb (coefficients2[i+3], dev.fx_dsp_lsb);
		if (!wffx_idle()) return (-1);
	}

	for (i = 0; i < sizeof (coefficients3); i += 2) {
		int x;

		outb (0x07, dev.fx_dsp_page);
		x = (i % 4) ? 0x4e : 0x4c;
		outb (x, dev.fx_dsp_addr);
		outb (coefficients3[i], dev.fx_dsp_msb);
		outb (coefficients3[i+1], dev.fx_dsp_lsb);
	}

	outb (0x00, dev.fx_op); /* mute off */
	if (!wffx_idle()) return (-1);

	return (0);
}

static int io = -1;
static int irq = -1;

MODULE_AUTHOR      ("Paul Barton-Davis <pbd@op.net>");
MODULE_DESCRIPTION ("Turtle Beach WaveFront Linux Driver");
MODULE_LICENSE("GPL");
module_param       (io, int, 0);
module_param       (irq, int, 0);

static int __init init_wavfront (void)
{
	printk ("Turtle Beach WaveFront Driver\n"
		"Copyright (C) by Hannu Solvainen, "
		"Paul Barton-Davis 1993-1998.\n");

	/* XXX t'would be lovely to ask the CS4232 for these values, eh ? */

	if (io == -1 || irq == -1) {
		printk (KERN_INFO LOGNAME "irq and io options must be set.\n");
		return -EINVAL;
	}

	if (wavefront_interrupt_bits (irq) < 0) {
		printk (KERN_INFO LOGNAME
			"IRQ must be 9, 5, 12 or 15 (not %d)\n", irq);
		return -ENODEV;
	}

	if (detect_wavefront (irq, io) < 0) {
		return -ENODEV;
	} 

	if (install_wavefront () < 0) {
		return -EIO;
	}

	return 0;
}

static void __exit cleanup_wavfront (void)
{
	uninstall_wavefront ();
}

module_init(init_wavfront);
module_exit(cleanup_wavfront);
