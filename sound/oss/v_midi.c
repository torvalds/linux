/*
 * sound/oss/v_midi.c
 *
 * The low level driver for the Sound Blaster DS chips.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 * ??
 *
 * Changes
 *	Alan Cox		Modularisation, changed memory allocations
 *	Christoph Hellwig	Adapted to module_init/module_exit
 *
 * Status
 *	Untested
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include "sound_config.h"

#include "v_midi.h"

static vmidi_devc *v_devc[2] = { NULL, NULL};
static int midi1,midi2;
static void *midi_mem = NULL;

/*
 * The DSP channel can be used either for input or output. Variable
 * 'sb_irq_mode' will be set when the program calls read or write first time
 * after open. Current version doesn't support mode changes without closing
 * and reopening the device. Support for this feature may be implemented in a
 * future version of this driver.
 */


static int v_midi_open (int dev, int mode,
	      void            (*input) (int dev, unsigned char data),
	      void            (*output) (int dev)
)
{
	vmidi_devc *devc = midi_devs[dev]->devc;
	unsigned long flags;

	if (devc == NULL)
		return -(ENXIO);

	spin_lock_irqsave(&devc->lock,flags);
	if (devc->opened)
	{
		spin_unlock_irqrestore(&devc->lock,flags);
		return -(EBUSY);
	}
	devc->opened = 1;
	spin_unlock_irqrestore(&devc->lock,flags);

	devc->intr_active = 1;

	if (mode & OPEN_READ)
	{
		devc->input_opened = 1;
		devc->midi_input_intr = input;
	}

	return 0;
}

static void v_midi_close (int dev)
{
	vmidi_devc *devc = midi_devs[dev]->devc;
	unsigned long flags;

	if (devc == NULL)
		return;

	spin_lock_irqsave(&devc->lock,flags);
	devc->intr_active = 0;
	devc->input_opened = 0;
	devc->opened = 0;
	spin_unlock_irqrestore(&devc->lock,flags);
}

static int v_midi_out (int dev, unsigned char midi_byte)
{
	vmidi_devc *devc = midi_devs[dev]->devc;
	vmidi_devc *pdevc;

	if (devc == NULL)
		return -ENXIO;

	pdevc = midi_devs[devc->pair_mididev]->devc;
	if (pdevc->input_opened > 0){
		if (MIDIbuf_avail(pdevc->my_mididev) > 500)
			return 0;
		pdevc->midi_input_intr (pdevc->my_mididev, midi_byte);
	}
	return 1;
}

static inline int v_midi_start_read (int dev)
{
	return 0;
}

static int v_midi_end_read (int dev)
{
	vmidi_devc *devc = midi_devs[dev]->devc;
	if (devc == NULL)
		return -ENXIO;

	devc->intr_active = 0;
	return 0;
}

/* why -EPERM and not -EINVAL?? */

static inline int v_midi_ioctl (int dev, unsigned cmd, void __user *arg)
{
	return -EPERM;
}


#define MIDI_SYNTH_NAME	"Loopback MIDI"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT

#include "midi_synth.h"

static struct midi_operations v_midi_operations =
{
	.owner		= THIS_MODULE,
	.info		= {"Loopback MIDI Port 1", 0, 0, SNDCARD_VMIDI},
	.converter	= &std_midi_synth,
	.in_info	= {0},
	.open		= v_midi_open,
	.close		= v_midi_close,
	.ioctl		= v_midi_ioctl,
	.outputc	= v_midi_out,
	.start_read	= v_midi_start_read,
	.end_read	= v_midi_end_read,
};

static struct midi_operations v_midi_operations2 =
{
	.owner		= THIS_MODULE,
	.info		= {"Loopback MIDI Port 2", 0, 0, SNDCARD_VMIDI},
	.converter	= &std_midi_synth,
	.in_info	= {0},
	.open		= v_midi_open,
	.close		= v_midi_close,
	.ioctl		= v_midi_ioctl,
	.outputc	= v_midi_out,
	.start_read	= v_midi_start_read,
	.end_read	= v_midi_end_read,
};

/*
 *	We kmalloc just one of these - it makes life simpler and the code
 *	cleaner and the memory handling far more efficient
 */
 
struct vmidi_memory
{
	/* Must be first */
	struct midi_operations m_ops[2];
	struct synth_operations s_ops[2];
	struct vmidi_devc v_ops[2];
};

static void __init attach_v_midi (struct address_info *hw_config)
{
	struct vmidi_memory *m;
	/* printk("Attaching v_midi device.....\n"); */

	midi1 = sound_alloc_mididev();
	if (midi1 == -1)
	{
		printk(KERN_ERR "v_midi: Too many midi devices detected\n");
		return;
	}
	
	m=(struct vmidi_memory *)kmalloc(sizeof(struct vmidi_memory), GFP_KERNEL);
	if (m == NULL)
	{
		printk(KERN_WARNING "Loopback MIDI: Failed to allocate memory\n");
		sound_unload_mididev(midi1);
		return;
	}
	
	midi_mem = m;
	
	midi_devs[midi1] = &m->m_ops[0];
	

	midi2 = sound_alloc_mididev();
	if (midi2 == -1)
	{
		printk (KERN_ERR "v_midi: Too many midi devices detected\n");
		kfree(m);
		sound_unload_mididev(midi1);
		return;
	}

	midi_devs[midi2] = &m->m_ops[1];

	/* printk("VMIDI1: %d   VMIDI2: %d\n",midi1,midi2); */

	/* for MIDI-1 */
	v_devc[0] = &m->v_ops[0];
	memcpy ((char *) midi_devs[midi1], (char *) &v_midi_operations,
		sizeof (struct midi_operations));

	v_devc[0]->my_mididev = midi1;
	v_devc[0]->pair_mididev = midi2;
	v_devc[0]->opened = v_devc[0]->input_opened = 0;
	v_devc[0]->intr_active = 0;
	v_devc[0]->midi_input_intr = NULL;
	spin_lock_init(&v_devc[0]->lock);

	midi_devs[midi1]->devc = v_devc[0];

	midi_devs[midi1]->converter = &m->s_ops[0];
	std_midi_synth.midi_dev = midi1;
	memcpy ((char *) midi_devs[midi1]->converter, (char *) &std_midi_synth,
		sizeof (struct synth_operations));
	midi_devs[midi1]->converter->id = "V_MIDI 1";

	/* for MIDI-2 */
	v_devc[1] = &m->v_ops[1];

	memcpy ((char *) midi_devs[midi2], (char *) &v_midi_operations2,
		sizeof (struct midi_operations));

	v_devc[1]->my_mididev = midi2;
	v_devc[1]->pair_mididev = midi1;
	v_devc[1]->opened = v_devc[1]->input_opened = 0;
	v_devc[1]->intr_active = 0;
	v_devc[1]->midi_input_intr = NULL;
	spin_lock_init(&v_devc[1]->lock);

	midi_devs[midi2]->devc = v_devc[1];
	midi_devs[midi2]->converter = &m->s_ops[1];

	std_midi_synth.midi_dev = midi2;
	memcpy ((char *) midi_devs[midi2]->converter, (char *) &std_midi_synth,
		sizeof (struct synth_operations));
	midi_devs[midi2]->converter->id = "V_MIDI 2";

	sequencer_init();
	/* printk("Attached v_midi device\n"); */
}

static inline int __init probe_v_midi(struct address_info *hw_config)
{
	return(1);	/* always OK */
}


static void __exit unload_v_midi(struct address_info *hw_config)
{
	sound_unload_mididev(midi1);
	sound_unload_mididev(midi2);
	kfree(midi_mem);
}

static struct address_info cfg; /* dummy */

static int __init init_vmidi(void)
{
	printk("MIDI Loopback device driver\n");
	if (!probe_v_midi(&cfg))
		return -ENODEV;
	attach_v_midi(&cfg);

	return 0;
}

static void __exit cleanup_vmidi(void)
{
	unload_v_midi(&cfg);
}

module_init(init_vmidi);
module_exit(cleanup_vmidi);
MODULE_LICENSE("GPL");
