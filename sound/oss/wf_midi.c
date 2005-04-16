/*
 * sound/wf_midi.c
 *
 * The low level driver for the WaveFront ICS2115 MIDI interface(s)
 * Note that there is also an MPU-401 emulation (actually, a UART-401
 * emulation) on the CS4232 on the Tropez Plus. This code has nothing
 * to do with that interface at all.
 *
 * The interface is essentially just a UART-401, but is has the
 * interesting property of supporting what Turtle Beach called
 * "Virtual MIDI" mode. In this mode, there are effectively *two*
 * MIDI buses accessible via the interface, one that is routed
 * solely to/from the external WaveFront synthesizer and the other
 * corresponding to the pin/socket connector used to link external
 * MIDI devices to the board.
 *
 * This driver fully supports this mode, allowing two distinct
 * midi devices (/dev/midiNN and /dev/midiNN+1) to be used
 * completely independently, giving 32 channels of MIDI routing,
 * 16 to the WaveFront synth and 16 to the external MIDI bus.
 *
 * Switching between the two is accomplished externally by the driver
 * using the two otherwise unused MIDI bytes. See the code for more details.
 *
 * NOTE: VIRTUAL MIDI MODE IS ON BY DEFAULT (see wavefront.c)
 *
 * The main reason to turn off Virtual MIDI mode is when you want to
 * tightly couple the WaveFront synth with an external MIDI
 * device. You won't be able to distinguish the source of any MIDI
 * data except via SysEx ID, but thats probably OK, since for the most
 * part, the WaveFront won't be sending any MIDI data at all.
 *  
 * The main reason to turn on Virtual MIDI Mode is to provide two
 * completely independent 16-channel MIDI buses, one to the
 * WaveFront and one to any external MIDI devices. Given the 32
 * voice nature of the WaveFront, its pretty easy to find a use
 * for all 16 channels driving just that synth.
 *
 */

/*
 * Copyright (C) by Paul Barton-Davis 1998
 * Some portions of this file are derived from work that is:
 *
 *    CopyriGht (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include "sound_config.h"

#include <linux/wavefront.h>

#ifdef MODULE

struct wf_mpu_config {
	int             base;
#define	DATAPORT(d)   (d)->base
#define	COMDPORT(d)   (d)->base+1
#define	STATPORT(d)   (d)->base+1

	int             irq;
	int             opened;
	int             devno;
	int             synthno;
	int             mode;
#define MODE_MIDI	1
#define MODE_SYNTH	2

	void            (*inputintr) (int dev, unsigned char data);
	char isvirtual;                /* do virtual I/O stuff */
};

static struct wf_mpu_config  devs[2];
static struct wf_mpu_config *phys_dev = &devs[0];
static struct wf_mpu_config *virt_dev = &devs[1];

static void start_uart_mode (void);
static DEFINE_SPINLOCK(lock);

#define	OUTPUT_READY	0x40
#define	INPUT_AVAIL	0x80
#define	MPU_ACK		0xFE
#define	UART_MODE_ON	0x3F

static inline int wf_mpu_status (void)
{
	return inb (STATPORT (phys_dev));
}

static inline int input_avail (void)
{
	return !(wf_mpu_status() & INPUT_AVAIL);
}

static inline int output_ready (void)
{
	return !(wf_mpu_status() & OUTPUT_READY);
}

static inline int  read_data (void)
{
	return inb (DATAPORT (phys_dev));
}

static inline void write_data (unsigned char byte)
{
	outb (byte, DATAPORT (phys_dev));
}

/*
 * States for the input scanner (should be in dev_table.h)
 */

#define MST_SYSMSG		100	/* System message (sysx etc). */
#define MST_MTC			102	/* Midi Time Code (MTC) qframe msg */
#define MST_SONGSEL		103	/* Song select */
#define MST_SONGPOS		104	/* Song position pointer */
#define MST_TIMED		105	/* Leading timing byte rcvd */

/* buffer space check for input scanner */

#define BUFTEST(mi) if (mi->m_ptr >= MI_MAX || mi->m_ptr < 0) \
{printk(KERN_ERR "WF-MPU: Invalid buffer pointer %d/%d, s=%d\n", \
	mi->m_ptr, mi->m_left, mi->m_state);mi->m_ptr--;}

static unsigned char len_tab[] =	/* # of data bytes following a status
					 */
{
	2,				/* 8x */
	2,				/* 9x */
	2,				/* Ax */
	2,				/* Bx */
	1,				/* Cx */
	1,				/* Dx */
	2,				/* Ex */
	0				/* Fx */
};

static int
wf_mpu_input_scanner (int devno, int synthdev, unsigned char midic)

{
	struct midi_input_info *mi = &midi_devs[devno]->in_info;

	switch (mi->m_state) {
	case MST_INIT:
		switch (midic) {
		case 0xf8:
			/* Timer overflow */
			break;
		
		case 0xfc:
			break;
		
		case 0xfd:
			/* XXX do something useful with this. If there is
			   an external MIDI timer (e.g. a hardware sequencer,
			   a useful timer can be derived ...
		   
			   For now, no timer support.
			*/
			break;
		
		case 0xfe:
			return MPU_ACK;
			break;
		
		case 0xf0:
		case 0xf1:
		case 0xf2:
		case 0xf3:
		case 0xf4:
		case 0xf5:
		case 0xf6:
		case 0xf7:
			break;
		
		case 0xf9:
			break;
		
		case 0xff:
			mi->m_state = MST_SYSMSG;
			break;
		
		default:
			if (midic <= 0xef) {
				mi->m_state = MST_TIMED;
			}
			else
				printk (KERN_ERR "<MPU: Unknown event %02x> ",
					midic);
		}
		break;
	  
	case MST_TIMED:
	{
		int             msg = ((int) (midic & 0xf0) >> 4);
	  
		mi->m_state = MST_DATA;
	  
		if (msg < 8) {	/* Data byte */
	      
			msg = ((int) (mi->m_prev_status & 0xf0) >> 4);
			msg -= 8;
			mi->m_left = len_tab[msg] - 1;
	      
			mi->m_ptr = 2;
			mi->m_buf[0] = mi->m_prev_status;
			mi->m_buf[1] = midic;

			if (mi->m_left <= 0) {
				mi->m_state = MST_INIT;
				do_midi_msg (synthdev, mi->m_buf, mi->m_ptr);
				mi->m_ptr = 0;
			}
		} else if (msg == 0xf) {	/* MPU MARK */
	      
			mi->m_state = MST_INIT;

			switch (midic) {
			case 0xf8:
				break;
		    
			case 0xf9:
				break;
		    
			case 0xfc:
				break;
		    
			default:
				break;
			}
		} else {
			mi->m_prev_status = midic;
			msg -= 8;
			mi->m_left = len_tab[msg];
	      
			mi->m_ptr = 1;
			mi->m_buf[0] = midic;
	      
			if (mi->m_left <= 0) {
				mi->m_state = MST_INIT;
				do_midi_msg (synthdev, mi->m_buf, mi->m_ptr);
				mi->m_ptr = 0;
			}
		}
	}
	break;

	case MST_SYSMSG:
		switch (midic) {
		case 0xf0:
			mi->m_state = MST_SYSEX;
			break;
	    
		case 0xf1:
			mi->m_state = MST_MTC;
			break;

		case 0xf2:
			mi->m_state = MST_SONGPOS;
			mi->m_ptr = 0;
			break;
	    
		case 0xf3:
			mi->m_state = MST_SONGSEL;
			break;
	    
		case 0xf6:
			mi->m_state = MST_INIT;
	    
			/*
			 *    Real time messages
			 */
		case 0xf8:
			/* midi clock */
			mi->m_state = MST_INIT;
			/* XXX need ext MIDI timer support */
			break;
	    
		case 0xfA:
			mi->m_state = MST_INIT;
			/* XXX need ext MIDI timer support */
			break;
	    
		case 0xFB:
			mi->m_state = MST_INIT;
			/* XXX need ext MIDI timer support */
			break;
	    
		case 0xFC:
			mi->m_state = MST_INIT;
			/* XXX need ext MIDI timer support */
			break;
	    
		case 0xFE:
			/* active sensing */
			mi->m_state = MST_INIT;
			break;
	    
		case 0xff:
			mi->m_state = MST_INIT;
			break;

		default:
			printk (KERN_ERR "unknown MIDI sysmsg %0x\n", midic);
			mi->m_state = MST_INIT;
		}
		break;

	case MST_MTC:
		mi->m_state = MST_INIT;
		break;

	case MST_SYSEX:
		if (midic == 0xf7) {
			mi->m_state = MST_INIT;
		} else {
			/* XXX fix me */
		}
		break;

	case MST_SONGPOS:
		BUFTEST (mi);
		mi->m_buf[mi->m_ptr++] = midic;
		if (mi->m_ptr == 2) {
			mi->m_state = MST_INIT;
			mi->m_ptr = 0;
			/* XXX need ext MIDI timer support */
		}
		break;

	case MST_DATA:
		BUFTEST (mi);
		mi->m_buf[mi->m_ptr++] = midic;
		if ((--mi->m_left) <= 0) {
			mi->m_state = MST_INIT;
			do_midi_msg (synthdev, mi->m_buf, mi->m_ptr);
			mi->m_ptr = 0;
		}
		break;

	default:
		printk (KERN_ERR "Bad state %d ", mi->m_state);
		mi->m_state = MST_INIT;
	}

	return 1;
}

static irqreturn_t
wf_mpuintr(int irq, void *dev_id, struct pt_regs *dummy)

{
	struct wf_mpu_config *physical_dev = dev_id;
	static struct wf_mpu_config *input_dev;
	struct midi_input_info *mi = &midi_devs[physical_dev->devno]->in_info;
	int n;

	if (!input_avail()) { /* not for us */
		return IRQ_NONE;
	}

	if (mi->m_busy)
		return IRQ_HANDLED;
	spin_lock(&lock);
	mi->m_busy = 1;

	if (!input_dev) {
		input_dev = physical_dev;
	}

	n = 50; /* XXX why ? */

	do {
		unsigned char c = read_data ();
      
		if (phys_dev->isvirtual) {

			if (c == WF_EXTERNAL_SWITCH) {
				input_dev = virt_dev;
				continue;
			} else if (c == WF_INTERNAL_SWITCH) { 
				input_dev = phys_dev;
				continue;
			} /* else just leave it as it is */

		} else {
			input_dev = phys_dev;
		}

		if (input_dev->mode == MODE_SYNTH) {
	  
			wf_mpu_input_scanner (input_dev->devno,
					      input_dev->synthno, c);
	  
		} else if (input_dev->opened & OPEN_READ) {
	  
			if (input_dev->inputintr) {
				input_dev->inputintr (input_dev->devno, c);
			} 
		}

	} while (input_avail() && n-- > 0);

	mi->m_busy = 0;
	spin_unlock(&lock);
	return IRQ_HANDLED;
}

static int
wf_mpu_open (int dev, int mode,
	     void            (*input) (int dev, unsigned char data),
	     void            (*output) (int dev)
	)
{
	struct wf_mpu_config *devc;

	if (dev < 0 || dev >= num_midis || midi_devs[dev]==NULL)
		return -(ENXIO);

	if (phys_dev->devno == dev) {
		devc = phys_dev;
	} else if (phys_dev->isvirtual && virt_dev->devno == dev) {
		devc = virt_dev;
	} else {
		printk (KERN_ERR "WF-MPU: unknown device number %d\n", dev);
		return -(EINVAL);
	}

	if (devc->opened) {
		return -(EBUSY);
	}

	devc->mode = MODE_MIDI;
	devc->opened = mode;
	devc->synthno = 0;

	devc->inputintr = input;
	return 0;
}
 
static void
wf_mpu_close (int dev)
{
	struct wf_mpu_config *devc;

	if (dev < 0 || dev >= num_midis || midi_devs[dev]==NULL)
		return;

	if (phys_dev->devno == dev) {
		devc = phys_dev;
	} else if (phys_dev->isvirtual && virt_dev->devno == dev) {
		devc = virt_dev;
	} else {
		printk (KERN_ERR "WF-MPU: unknown device number %d\n", dev);
		return;
	}

	devc->mode = 0;
	devc->inputintr = NULL;
	devc->opened = 0;
}

static int
wf_mpu_out (int dev, unsigned char midi_byte)
{
	int             timeout;
	unsigned long   flags;
	static int lastoutdev = -1;
	unsigned char switchch;

	if (phys_dev->isvirtual && lastoutdev != dev) {
      
		if (dev == phys_dev->devno) { 
			switchch = WF_INTERNAL_SWITCH;
		} else if (dev == virt_dev->devno) { 
			switchch = WF_EXTERNAL_SWITCH;
		} else {
			printk (KERN_ERR "WF-MPU: bad device number %d", dev);
			return (0);
		}

		/* XXX fix me */
      
		for (timeout = 30000; timeout > 0 && !output_ready ();
		     timeout--);
      
		spin_lock_irqsave(&lock,flags);
      
		if (!output_ready ()) {
			printk (KERN_WARNING "WF-MPU: Send switch "
				"byte timeout\n");
			spin_unlock_irqrestore(&lock,flags);
			return 0;
		}
      
		write_data (switchch);
		spin_unlock_irqrestore(&lock,flags);
	} 

	lastoutdev = dev;

	/*
	 * Sometimes it takes about 30000 loops before the output becomes ready
	 * (After reset). Normally it takes just about 10 loops.
	 */

	/* XXX fix me */

	for (timeout = 30000; timeout > 0 && !output_ready (); timeout--);

	spin_lock_irqsave(&lock,flags);
	if (!output_ready ()) {
		spin_unlock_irqrestore(&lock,flags);
		printk (KERN_WARNING "WF-MPU: Send data timeout\n");
		return 0;
	}

	write_data (midi_byte);
	spin_unlock_irqrestore(&lock,flags);

	return 1;
}

static inline int wf_mpu_start_read (int dev) {
	return 0;
}

static inline int wf_mpu_end_read (int dev) {
	return 0;
}

static int wf_mpu_ioctl (int dev, unsigned cmd, void __user *arg)
{
	printk (KERN_WARNING
		"WF-MPU: Intelligent mode not supported by hardware.\n");
	return -(EINVAL);
}

static int wf_mpu_buffer_status (int dev)
{
	return 0;
}

static struct synth_operations wf_mpu_synth_operations[2];
static struct midi_operations  wf_mpu_midi_operations[2];

static struct midi_operations wf_mpu_midi_proto =
{
	.owner		= THIS_MODULE,
	.info		= {"WF-MPU MIDI", 0, MIDI_CAP_MPU401, SNDCARD_MPU401},
	.in_info	= {0},   /* in_info */
	.open		= wf_mpu_open,
	.close		= wf_mpu_close,
	.ioctl		= wf_mpu_ioctl,
	.outputc	= wf_mpu_out,
	.start_read	= wf_mpu_start_read,
	.end_read	= wf_mpu_end_read,
	.buffer_status	= wf_mpu_buffer_status,
};

static struct synth_info wf_mpu_synth_info_proto =
{"WaveFront MPU-401 interface", 0,
 SYNTH_TYPE_MIDI, MIDI_TYPE_MPU401, 0, 128, 0, 128, SYNTH_CAP_INPUT};

static struct synth_info wf_mpu_synth_info[2];

static int
wf_mpu_synth_ioctl (int dev, unsigned int cmd, void __user *arg)
{
	int             midi_dev;
	int index;

	midi_dev = synth_devs[dev]->midi_dev;

	if (midi_dev < 0 || midi_dev > num_midis || midi_devs[midi_dev]==NULL)
		return -(ENXIO);

	if (midi_dev == phys_dev->devno) {
		index = 0;
	} else if (phys_dev->isvirtual && midi_dev == virt_dev->devno) {
		index = 1;
	} else {
		return -(EINVAL);
	}

	switch (cmd) {

	case SNDCTL_SYNTH_INFO:
		if (copy_to_user(arg,
			      &wf_mpu_synth_info[index],
			      sizeof (struct synth_info)))
			return -EFAULT;
		return 0;
	
	case SNDCTL_SYNTH_MEMAVL:
		return 0x7fffffff;
	
	default:
		return -EINVAL;
	}
}

static int
wf_mpu_synth_open (int dev, int mode)
{
	int             midi_dev;
	struct wf_mpu_config *devc;

	midi_dev = synth_devs[dev]->midi_dev;

	if (midi_dev < 0 || midi_dev > num_midis || midi_devs[midi_dev]==NULL) {
		return -(ENXIO);
	}
  
	if (phys_dev->devno == midi_dev) {
		devc = phys_dev;
	} else if (phys_dev->isvirtual && virt_dev->devno == midi_dev) {
		devc = virt_dev;
	} else {
		printk (KERN_ERR "WF-MPU: unknown device number %d\n", dev);
		return -(EINVAL);
	}

	if (devc->opened) {
		return -(EBUSY);
	}
  
	devc->mode = MODE_SYNTH;
	devc->synthno = dev;
	devc->opened = mode;
	devc->inputintr = NULL;
	return 0;
}

static void
wf_mpu_synth_close (int dev)
{
	int             midi_dev;
	struct wf_mpu_config *devc;

	midi_dev = synth_devs[dev]->midi_dev;

	if (phys_dev->devno == midi_dev) {
		devc = phys_dev;
	} else if (phys_dev->isvirtual && virt_dev->devno == midi_dev) {
		devc = virt_dev;
	} else {
		printk (KERN_ERR "WF-MPU: unknown device number %d\n", dev);
		return;
	}

	devc->inputintr = NULL;
	devc->opened = 0;
	devc->mode = 0;
}

#define _MIDI_SYNTH_C_
#define MIDI_SYNTH_NAME	"WaveFront (MIDI)"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

static struct synth_operations wf_mpu_synth_proto =
{
	.owner		= THIS_MODULE,
	.id		= "WaveFront (ICS2115)",
	.info		= NULL,  /* info field, filled in during configuration */
	.midi_dev	= 0,     /* MIDI dev XXX should this be -1 ? */
	.synth_type	= SYNTH_TYPE_MIDI,
	.synth_subtype	= SAMPLE_TYPE_WAVEFRONT,
	.open		= wf_mpu_synth_open,
	.close		= wf_mpu_synth_close,
	.ioctl		= wf_mpu_synth_ioctl,
	.kill_note	= midi_synth_kill_note,
	.start_note	= midi_synth_start_note,
	.set_instr	= midi_synth_set_instr,
	.reset		= midi_synth_reset,
	.hw_control	= midi_synth_hw_control,
	.load_patch	= midi_synth_load_patch,
	.aftertouch	= midi_synth_aftertouch,
	.controller	= midi_synth_controller,
	.panning	= midi_synth_panning,
	.bender		= midi_synth_bender,
	.setup_voice	= midi_synth_setup_voice,
	.send_sysex	= midi_synth_send_sysex
};

static int
config_wf_mpu (struct wf_mpu_config *dev)

{
	int is_external;
	char *name;
	int index;

	if (dev == phys_dev) {
		name = "WaveFront internal MIDI";
		is_external = 0;
		index = 0;
		memcpy ((char *) &wf_mpu_synth_operations[index],
			(char *) &wf_mpu_synth_proto,
			sizeof (struct synth_operations));
	} else {
		name = "WaveFront external MIDI";
		is_external = 1;
		index = 1;
		/* no synth operations for an external MIDI interface */
	}

	memcpy ((char *) &wf_mpu_synth_info[dev->devno],
		(char *) &wf_mpu_synth_info_proto,
		sizeof (struct synth_info));

	strcpy (wf_mpu_synth_info[index].name, name);

	wf_mpu_synth_operations[index].midi_dev = dev->devno;
	wf_mpu_synth_operations[index].info = &wf_mpu_synth_info[index];

	memcpy ((char *) &wf_mpu_midi_operations[index],
		(char *) &wf_mpu_midi_proto,
		sizeof (struct midi_operations));
  
	if (is_external) {
		wf_mpu_midi_operations[index].converter = NULL;
	} else {
		wf_mpu_midi_operations[index].converter =
			&wf_mpu_synth_operations[index];
	}

	strcpy (wf_mpu_midi_operations[index].info.name, name);

	midi_devs[dev->devno] = &wf_mpu_midi_operations[index];
	midi_devs[dev->devno]->in_info.m_busy = 0;
	midi_devs[dev->devno]->in_info.m_state = MST_INIT;
	midi_devs[dev->devno]->in_info.m_ptr = 0;
	midi_devs[dev->devno]->in_info.m_left = 0;
	midi_devs[dev->devno]->in_info.m_prev_status = 0;

	devs[index].opened = 0;
	devs[index].mode = 0;

	return (0);
}

int virtual_midi_enable (void)

{
	if ((virt_dev->devno < 0) &&
	    (virt_dev->devno = sound_alloc_mididev()) == -1) {
		printk (KERN_ERR
			"WF-MPU: too many midi devices detected\n");
		return -1;
	}

	config_wf_mpu (virt_dev);

	phys_dev->isvirtual = 1;
	return virt_dev->devno;
}

int
virtual_midi_disable (void)

{
	unsigned long flags;

	spin_lock_irqsave(&lock,flags);

	wf_mpu_close (virt_dev->devno);
	/* no synth on virt_dev, so no need to call wf_mpu_synth_close() */
	phys_dev->isvirtual = 0;

	spin_unlock_irqrestore(&lock,flags);

	return 0;
}

int __init detect_wf_mpu (int irq, int io_base)
{
	if (!request_region(io_base, 2, "wavefront midi")) {
		printk (KERN_WARNING "WF-MPU: I/O port %x already in use.\n",
			io_base);
		return -1;
	}

	phys_dev->base = io_base;
	phys_dev->irq = irq;
	phys_dev->devno = -1;
	virt_dev->devno = -1;

	return 0;
}

int __init install_wf_mpu (void)
{
	if ((phys_dev->devno = sound_alloc_mididev()) < 0){

		printk (KERN_ERR "WF-MPU: Too many MIDI devices detected.\n");
		release_region(phys_dev->base, 2);
		return -1;
	}

	phys_dev->isvirtual = 0;

	if (config_wf_mpu (phys_dev)) {

		printk (KERN_WARNING
			"WF-MPU: configuration for MIDI device %d failed\n",
			phys_dev->devno);
		sound_unload_mididev (phys_dev->devno);

	}

	/* OK, now we're configured to handle an interrupt ... */

	if (request_irq (phys_dev->irq, wf_mpuintr, SA_INTERRUPT|SA_SHIRQ,
			 "wavefront midi", phys_dev) < 0) {

		printk (KERN_ERR "WF-MPU: Failed to allocate IRQ%d\n",
			phys_dev->irq);
		return -1;

	}

	/* This being a WaveFront (ICS-2115) emulated MPU-401, we have
	   to switch it into UART (dumb) mode, because otherwise, it
	   won't do anything at all.
	*/
  
	start_uart_mode ();

	return phys_dev->devno;
}
 
void
uninstall_wf_mpu (void)

{
	release_region (phys_dev->base, 2); 
	free_irq (phys_dev->irq, phys_dev);
	sound_unload_mididev (phys_dev->devno);

	if (virt_dev->devno >= 0) {
		sound_unload_mididev (virt_dev->devno);
	}
}

static void
start_uart_mode (void)

{
	int             ok, i;
	unsigned long   flags;

	spin_lock_irqsave(&lock,flags);

	/* XXX fix me */

	for (i = 0; i < 30000 && !output_ready (); i++);

	outb (UART_MODE_ON, COMDPORT(phys_dev));

	for (ok = 0, i = 50000; i > 0 && !ok; i--) {
		if (input_avail ()) {
			if (read_data () == MPU_ACK) {
				ok = 1;
			}
		}
	}

	spin_unlock_irqrestore(&lock,flags);
}
#endif
