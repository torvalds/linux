 /*
 **********************************************************************
 *     main.c - Creative EMU10K1 audio driver
 *     Copyright 1999, 2000 Creative Labs, Inc.
 *
 **********************************************************************
 *
 *     Date                 Author          Summary of changes
 *     ----                 ------          ------------------
 *     October 20, 1999     Bertrand Lee    base code release
 *     November 2, 1999     Alan Cox        cleaned up stuff
 *
 **********************************************************************
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public
 *     License along with this program; if not, write to the Free
 *     Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 *     USA.
 *
 **********************************************************************
 *
 *      Supported devices:
 *      /dev/dsp:        Standard /dev/dsp device, OSS-compatible
 *      /dev/dsp1:       Routes to rear speakers only	 
 *      /dev/mixer:      Standard /dev/mixer device, OSS-compatible
 *      /dev/midi:       Raw MIDI UART device, mostly OSS-compatible
 *	/dev/sequencer:  Sequencer Interface (requires sound.o)
 *
 *      Revision history:
 *      0.1 beta Initial release
 *      0.2 Lowered initial mixer vol. Improved on stuttering wave playback. Added MIDI UART support.
 *      0.3 Fixed mixer routing bug, added APS, joystick support.
 *      0.4 Added rear-channel, SPDIF support.
 *	0.5 Source cleanup, SMP fixes, multiopen support, 64 bit arch fixes,
 *	    moved bh's to tasklets, moved to the new PCI driver initialization style.
 *	0.6 Make use of pci_alloc_consistent, improve compatibility layer for 2.2 kernels,
 *	    code reorganization and cleanup.
 *	0.7 Support for the Emu-APS. Bug fixes for voice cache setup, mmaped sound + poll().
 *          Support for setting external TRAM size.
 *      0.8 Make use of the kernel ac97 interface. Support for a dsp patch manager.
 *      0.9 Re-enables rear speakers volume controls
 *     0.10 Initializes rear speaker volume.
 *	    Dynamic patch storage allocation.
 *	    New private ioctls to change control gpr values.
 *	    Enable volume control interrupts.
 *	    By default enable dsp routes to digital out. 
 *     0.11 Fixed fx / 4 problem.
 *     0.12 Implemented mmaped for recording.
 *	    Fixed bug: not unreserving mmaped buffer pages.
 *	    IRQ handler cleanup.
 *     0.13 Fixed problem with dsp1
 *          Simplified dsp patch writing (inside the driver)
 *	    Fixed several bugs found by the Stanford tools
 *     0.14 New control gpr to oss mixer mapping feature (Chris Purnell)
 *          Added AC3 Passthrough Support (Juha Yrjola)
 *          Added Support for 5.1 cards (digital out and the third analog out)
 *     0.15 Added Sequencer Support (Daniel Mack)
 *          Support for multichannel pcm playback (Eduard Hasenleithner)
 *     0.16 Mixer improvements, added old treble/bass support (Daniel Bertrand)
 *          Small code format cleanup.
 *          Deadlock bug fix for emu10k1_volxxx_irqhandler().
 *     0.17 Fix for mixer SOUND_MIXER_INFO ioctl.
 *	    Fix for HIGHMEM machines (emu10k1 can only do 31 bit bus master) 
 *	    midi poll initial implementation.
 *	    Small mixer fixes/cleanups.
 *	    Improved support for 5.1 cards.
 *     0.18 Fix for possible leak in pci_alloc_consistent()
 *          Cleaned up poll() functions (audio and midi). Don't start input.
 *	    Restrict DMA pages used to 512Mib range.
 *	    New AC97_BOOST mixer ioctl.
 *    0.19a Added Support for Audigy Cards
 *	    Real fix for kernel with highmem support (cast dma_handle to u32).
 *	    Fix recording buffering parameters calculation.
 *	    Use unsigned long for variables in bit ops.
 *    0.20a Fixed recording startup
 *	    Fixed timer rate setting (it's a 16-bit register)
 *	0.21 Converted code to use pci_name() instead of accessing slot_name
 *	    directly (Eugene Teo)
 *********************************************************************/

/* These are only included once per module */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>

#include "hwaccess.h"
#include "8010.h"
#include "efxmgr.h"
#include "cardwo.h"
#include "cardwi.h"
#include "cardmo.h"
#include "cardmi.h"
#include "recmgr.h"
#include "ecard.h"


#ifdef EMU10K1_SEQUENCER
#define MIDI_SYNTH_NAME "EMU10K1 MIDI"
#define MIDI_SYNTH_CAPS SYNTH_CAP_INPUT
 
#include "../sound_config.h"
#include "../midi_synth.h"

/* this should be in dev_table.h */
#define SNDCARD_EMU10K1 46
#endif
 

/* the emu10k1 _seems_ to only supports 29 bit (512MiB) bit bus master */
#define EMU10K1_DMA_MASK                0x1fffffff	/* DMA buffer mask for pci_alloc_consist */

#ifndef PCI_VENDOR_ID_CREATIVE
#define PCI_VENDOR_ID_CREATIVE 0x1102
#endif

#ifndef PCI_DEVICE_ID_CREATIVE_EMU10K1
#define PCI_DEVICE_ID_CREATIVE_EMU10K1 0x0002
#endif
#ifndef PCI_DEVICE_ID_CREATIVE_AUDIGY
#define PCI_DEVICE_ID_CREATIVE_AUDIGY 0x0004
#endif

#define EMU_APS_SUBID	0x40011102
 
enum {
	EMU10K1 = 0,
	AUDIGY,
};

static char *card_names[] __devinitdata = {
	"EMU10K1",
	"Audigy",
};

static struct pci_device_id emu10k1_pci_tbl[] = {
	{PCI_VENDOR_ID_CREATIVE, PCI_DEVICE_ID_CREATIVE_EMU10K1,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, EMU10K1},
	{PCI_VENDOR_ID_CREATIVE, PCI_DEVICE_ID_CREATIVE_AUDIGY,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, AUDIGY},
	{0,}
};

MODULE_DEVICE_TABLE(pci, emu10k1_pci_tbl);

/* Global var instantiation */

LIST_HEAD(emu10k1_devs);

extern struct file_operations emu10k1_audio_fops;
extern struct file_operations emu10k1_mixer_fops;
extern struct file_operations emu10k1_midi_fops;

#ifdef EMU10K1_SEQUENCER
static struct midi_operations emu10k1_midi_operations;
#endif

extern irqreturn_t emu10k1_interrupt(int, void *, struct pt_regs *s);

static int __devinit emu10k1_audio_init(struct emu10k1_card *card)
{
	/* Assign default playback voice parameters */
	if (card->is_audigy)
		card->mchannel_fx = 0;
	else
		card->mchannel_fx = 8;


	if (card->is_audigy) {
		/* mono voice */
		card->waveout.send_dcba[SEND_MONO] = 0xffffffff;
		card->waveout.send_hgfe[SEND_MONO] = 0x0000ffff;
	
		/* stereo voice */
		/* left */
		card->waveout.send_dcba[SEND_LEFT] = 0x00ff00ff;
		card->waveout.send_hgfe[SEND_LEFT] = 0x00007f7f;	
		/* right */
		card->waveout.send_dcba[SEND_RIGHT] = 0xff00ff00;
		card->waveout.send_hgfe[SEND_RIGHT] = 0x00007f7f;

		card->waveout.send_routing[ROUTE_PCM] = 0x03020100; // Regular pcm
		card->waveout.send_routing2[ROUTE_PCM] = 0x07060504;

		card->waveout.send_routing[ROUTE_PT] = 0x3f3f3d3c; // Passthrough
		card->waveout.send_routing2[ROUTE_PT] = 0x3f3f3f3f;
		
		card->waveout.send_routing[ROUTE_PCM1] = 0x03020100; // Spare
		card->waveout.send_routing2[ROUTE_PCM1] = 0x07060404;
		
	} else {
		/* mono voice */
		card->waveout.send_dcba[SEND_MONO] = 0x0000ffff;
	
		/* stereo voice */
		/* left */
		card->waveout.send_dcba[SEND_LEFT] = 0x000000ff;
		/* right */
		card->waveout.send_dcba[SEND_RIGHT] = 0x0000ff00;

		card->waveout.send_routing[ROUTE_PCM] = 0x3210; // pcm
		card->waveout.send_routing[ROUTE_PT] = 0x3210; // passthrough
		card->waveout.send_routing[ROUTE_PCM1] = 0x7654; // /dev/dsp1
	}

	/* Assign default recording parameters */
	/* FIXME */
	if (card->is_aps)
		card->wavein.recsrc = WAVERECORD_FX;
	else
		card->wavein.recsrc = WAVERECORD_AC97;

	card->wavein.fxwc = 0x0003;
	return 0;
}

static void emu10k1_audio_cleanup(struct emu10k1_card *card)
{
}

static int __devinit emu10k1_register_devices(struct emu10k1_card *card)
{
	card->audio_dev = register_sound_dsp(&emu10k1_audio_fops, -1);
	if (card->audio_dev < 0) {
		printk(KERN_ERR "emu10k1: cannot register first audio device!\n");
		goto err_dev;
	}

	card->audio_dev1 = register_sound_dsp(&emu10k1_audio_fops, -1);
	if (card->audio_dev1 < 0) {
		printk(KERN_ERR "emu10k1: cannot register second audio device!\n");
		goto err_dev1;
	}

	card->ac97->dev_mixer = register_sound_mixer(&emu10k1_mixer_fops, -1);
	if (card->ac97->dev_mixer < 0) {
		printk(KERN_ERR "emu10k1: cannot register mixer device\n");
		goto err_mixer;
        }

	card->midi_dev = register_sound_midi(&emu10k1_midi_fops, -1);
	if (card->midi_dev < 0) {
                printk(KERN_ERR "emu10k1: cannot register midi device!\n");
		goto err_midi;
        }

#ifdef EMU10K1_SEQUENCER
	card->seq_dev = sound_alloc_mididev();
	if (card->seq_dev == -1)
		printk(KERN_WARNING "emu10k1: unable to register sequencer device!");
	else {
		std_midi_synth.midi_dev = card->seq_dev;
		midi_devs[card->seq_dev] = 
			(struct midi_operations *)
			kmalloc(sizeof(struct midi_operations), GFP_KERNEL);

		if (midi_devs[card->seq_dev] == NULL) {
			printk(KERN_ERR "emu10k1: unable to allocate memory!");
			sound_unload_mididev(card->seq_dev);
			card->seq_dev = -1;
			/* return without error */
		} else {
			memcpy((char *)midi_devs[card->seq_dev], 
				(char *)&emu10k1_midi_operations, 
				sizeof(struct midi_operations));
			midi_devs[card->seq_dev]->devc = card;
			sequencer_init();
			card->seq_mididev = NULL;
		}
	}
#endif
	return 0;

err_midi:
	unregister_sound_mixer(card->ac97->dev_mixer);
err_mixer:
	unregister_sound_dsp(card->audio_dev);
err_dev1:
	unregister_sound_dsp(card->audio_dev);
err_dev:
	return -ENODEV;
}

static void emu10k1_unregister_devices(struct emu10k1_card *card)
{
#ifdef EMU10K1_SEQUENCER
	if (card->seq_dev > -1) {
		kfree(midi_devs[card->seq_dev]);
		midi_devs[card->seq_dev] = NULL;
		sound_unload_mididev(card->seq_dev);
		card->seq_dev = -1;
	}
#endif

	unregister_sound_midi(card->midi_dev);
	unregister_sound_mixer(card->ac97->dev_mixer);
	unregister_sound_dsp(card->audio_dev1);
	unregister_sound_dsp(card->audio_dev);
}

static int emu10k1_info_proc (char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	struct emu10k1_card *card = data;
	int len = 0;
	
	if (card == NULL)
		return -ENODEV;

	len += sprintf (page + len, "Driver Version : %s\n", DRIVER_VERSION);
	len += sprintf (page + len, "Card type      : %s\n", card->is_aps ? "Aps" : (card->is_audigy ? "Audigy" : "Emu10k1"));
	len += sprintf (page + len, "Revision       : %d\n", card->chiprev);
	len += sprintf (page + len, "Model          : %#06x\n", card->model);
	len += sprintf (page + len, "IO             : %#06lx-%#06lx\n", card->iobase, card->iobase + card->length - 1);
	len += sprintf (page + len, "IRQ            : %d\n\n", card->irq);
	
	len += sprintf (page + len, "Registered /dev Entries:\n");
	len += sprintf (page + len, "/dev/dsp%d\n", card->audio_dev / 16);
	len += sprintf (page + len, "/dev/dsp%d\n", card->audio_dev1 / 16);
	len += sprintf (page + len, "/dev/mixer%d\n", card->ac97->dev_mixer / 16);
	len += sprintf (page + len, "/dev/midi%d\n", card->midi_dev / 16);

#ifdef EMU10K1_SEQUENCER
	len += sprintf (page + len, "/dev/sequencer\n");
#endif

	return len;
}

static int __devinit emu10k1_proc_init(struct emu10k1_card *card)
{
	char s[48];

	if (!proc_mkdir ("driver/emu10k1", NULL)) {
		printk(KERN_ERR "emu10k1: unable to create proc directory driver/emu10k1\n");
		goto err_out;
	}

	sprintf(s, "driver/emu10k1/%s", pci_name(card->pci_dev));
	if (!proc_mkdir (s, NULL)) {
		printk(KERN_ERR "emu10k1: unable to create proc directory %s\n", s);
		goto err_emu10k1_proc;
	}

	sprintf(s, "driver/emu10k1/%s/info", pci_name(card->pci_dev));
	if (!create_proc_read_entry (s, 0, NULL, emu10k1_info_proc, card)) {
		printk(KERN_ERR "emu10k1: unable to create proc entry %s\n", s);
		goto err_dev_proc;
	}

	if (!card->is_aps) {
		sprintf(s, "driver/emu10k1/%s/ac97", pci_name(card->pci_dev));
		if (!create_proc_read_entry (s, 0, NULL, ac97_read_proc, card->ac97)) {
			printk(KERN_ERR "emu10k1: unable to create proc entry %s\n", s);
			goto err_proc_ac97;
		}
	}

	return 0;

err_proc_ac97:
	sprintf(s, "driver/emu10k1/%s/info", pci_name(card->pci_dev));
	remove_proc_entry(s, NULL);

err_dev_proc:
	sprintf(s, "driver/emu10k1/%s", pci_name(card->pci_dev));
	remove_proc_entry(s, NULL);

err_emu10k1_proc:
	remove_proc_entry("driver/emu10k1", NULL);

err_out:
	return -EIO;
}

static void emu10k1_proc_cleanup(struct emu10k1_card *card)
{
	char s[48];

	if (!card->is_aps) {
		sprintf(s, "driver/emu10k1/%s/ac97", pci_name(card->pci_dev));
		remove_proc_entry(s, NULL);
	}

	sprintf(s, "driver/emu10k1/%s/info", pci_name(card->pci_dev));
	remove_proc_entry(s, NULL);

	sprintf(s, "driver/emu10k1/%s", pci_name(card->pci_dev));
	remove_proc_entry(s, NULL);
		
	remove_proc_entry("driver/emu10k1", NULL);
}

static int __devinit emu10k1_mixer_init(struct emu10k1_card *card)
{
	struct ac97_codec *codec  = ac97_alloc_codec();
	
	if(codec == NULL)
	{
		printk(KERN_ERR "emu10k1: cannot allocate mixer\n");
		return -EIO;
	}
	card->ac97 = codec;
	card->ac97->private_data = card;

	if (!card->is_aps) {
		card->ac97->id = 0;
		card->ac97->codec_read = emu10k1_ac97_read;
        	card->ac97->codec_write = emu10k1_ac97_write;

		if (ac97_probe_codec (card->ac97) == 0) {
			printk(KERN_ERR "emu10k1: unable to probe AC97 codec\n");
			goto err_out;
		}
		/* 5.1: Enable the additional AC97 Slots and unmute extra channels on AC97 codec */
		if (codec->codec_read(codec, AC97_EXTENDED_ID) & 0x0080){
			printk(KERN_INFO "emu10k1: SBLive! 5.1 card detected\n"); 
			sblive_writeptr(card, AC97SLOT, 0, AC97SLOT_CNTR | AC97SLOT_LFE);
			codec->codec_write(codec, AC97_SURROUND_MASTER, 0x0);
		}

		// Force 5bit:		    
		//card->ac97->bit_resolution=5;

		/* these will store the original values and never be modified */
		card->ac97_supported_mixers = card->ac97->supported_mixers;
		card->ac97_stereo_mixers = card->ac97->stereo_mixers;
	}

	return 0;

 err_out:
 	ac97_release_codec(card->ac97);
	return -EIO;
}

static void emu10k1_mixer_cleanup(struct emu10k1_card *card)
{
	ac97_release_codec(card->ac97);
}

static int __devinit emu10k1_midi_init(struct emu10k1_card *card)
{
	int ret;

	card->mpuout = kmalloc(sizeof(struct emu10k1_mpuout), GFP_KERNEL);
	if (card->mpuout == NULL) {
		printk(KERN_WARNING "emu10k1: Unable to allocate emu10k1_mpuout: out of memory\n");
		ret = -ENOMEM;
		goto err_out1;
	}

	memset(card->mpuout, 0, sizeof(struct emu10k1_mpuout));

	card->mpuout->intr = 1;
	card->mpuout->status = FLAGS_AVAILABLE;
	card->mpuout->state = CARDMIDIOUT_STATE_DEFAULT;

	tasklet_init(&card->mpuout->tasklet, emu10k1_mpuout_bh, (unsigned long) card);

	spin_lock_init(&card->mpuout->lock);

	card->mpuin = kmalloc(sizeof(struct emu10k1_mpuin), GFP_KERNEL);
	if (card->mpuin == NULL) {
		printk(KERN_WARNING "emu10k1: Unable to allocate emu10k1_mpuin: out of memory\n");
		ret = -ENOMEM;
                goto err_out2;
	}

	memset(card->mpuin, 0, sizeof(struct emu10k1_mpuin));

	card->mpuin->status = FLAGS_AVAILABLE;

	tasklet_init(&card->mpuin->tasklet, emu10k1_mpuin_bh, (unsigned long) card->mpuin);

	spin_lock_init(&card->mpuin->lock);

	/* Reset the MPU port */
	if (emu10k1_mpu_reset(card) < 0) {
		ERROR();
		ret = -EIO;
		goto err_out3;
	}

	return 0;

err_out3:
	kfree(card->mpuin);
err_out2:
	kfree(card->mpuout);
err_out1:
	return ret;
}

static void emu10k1_midi_cleanup(struct emu10k1_card *card)
{
	tasklet_kill(&card->mpuout->tasklet);
	kfree(card->mpuout);

	tasklet_kill(&card->mpuin->tasklet);
	kfree(card->mpuin);
}

static void __devinit voice_init(struct emu10k1_card *card)
{
	int i;

	for (i = 0; i < NUM_G; i++)
		card->voicetable[i] = VOICE_USAGE_FREE;
}

static void __devinit timer_init(struct emu10k1_card *card)
{
	INIT_LIST_HEAD(&card->timers);
	card->timer_delay = TIMER_STOPPED;
	spin_lock_init(&card->timer_lock);
}

static void __devinit addxmgr_init(struct emu10k1_card *card)
{
	u32 count;

	for (count = 0; count < MAXPAGES; count++)
		card->emupagetable[count] = 0;

	/* Mark first page as used */
	/* This page is reserved by the driver */
	card->emupagetable[0] = 0x8001;
	card->emupagetable[1] = MAXPAGES - 1;
}

static void fx_cleanup(struct patch_manager *mgr)
{
	int i;
	for(i = 0; i < mgr->current_pages; i++)
		free_page((unsigned long) mgr->patch[i]);
}

static int __devinit fx_init(struct emu10k1_card *card)
{
	struct patch_manager *mgr = &card->mgr;
	struct dsp_patch *patch;
	struct dsp_rpatch *rpatch;
	s32 left, right;
	int i;
	u32 pc = 0;
	u32 patch_n=0;
	struct emu_efx_info_t emu_efx_info[2]=
		{{ 20, 10, 0x400, 0x100, 0x20 },
		 { 24, 12, 0x600, 0x400, 0x60 },
		}; 
			

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++) {
		mgr->ctrl_gpr[i][0] = -1;
		mgr->ctrl_gpr[i][1] = -1;
	}


	if (card->is_audigy)
		mgr->current_pages = (2 + PATCHES_PER_PAGE - 1) / PATCHES_PER_PAGE;
	else
		/* !! The number below must equal the number of patches, currently 11 !! */
		mgr->current_pages = (11 + PATCHES_PER_PAGE - 1) / PATCHES_PER_PAGE;
	
	for (i = 0; i < mgr->current_pages; i++) {
		mgr->patch[i] = (void *)__get_free_page(GFP_KERNEL);
		if (mgr->patch[i] == NULL) {
			mgr->current_pages = i;
			fx_cleanup(mgr);
			return -ENOMEM;
		}
		memset(mgr->patch[i], 0, PAGE_SIZE);
	}

	if (card->is_audigy) {
		for (i = 0; i < 1024; i++)
			OP(0xf, 0x0c0, 0x0c0, 0x0cf, 0x0c0);

		for (i = 0; i < 512 ; i++)
			sblive_writeptr(card, A_GPR_BASE+i,0,0);

		pc=0;

		//Pcm input volume
		OP(0, 0x402, 0x0c0, 0x406, 0x000);
		OP(0, 0x403, 0x0c0, 0x407, 0x001);

		//CD-Digital input Volume
		OP(0, 0x404, 0x0c0, 0x40d, 0x42);
		OP(0, 0x405, 0x0c0, 0x40f, 0x43);

		// CD + PCM 
		OP(6, 0x400, 0x0c0, 0x402, 0x404);
		OP(6, 0x401, 0x0c0, 0x403, 0x405);
		
		// Front Output + Master Volume
		OP(0, 0x68, 0x0c0, 0x408, 0x400);
		OP(0, 0x69, 0x0c0, 0x409, 0x401);

		// Add-in analog inputs for other speakers
		OP(6, 0x400, 0x40, 0x400, 0xc0);
		OP(6, 0x401, 0x41, 0x401, 0xc0);

		// Digital Front + Master Volume
		OP(0, 0x60, 0x0c0, 0x408, 0x400);
		OP(0, 0x61, 0x0c0, 0x409, 0x401);

		// Rear Output + Rear Volume
		OP(0, 0x06e, 0x0c0, 0x419, 0x400);
		OP(0, 0x06f, 0x0c0, 0x41a, 0x401);		

		// Digital Rear Output + Rear Volume
		OP(0, 0x066, 0x0c0, 0x419, 0x400);
		OP(0, 0x067, 0x0c0, 0x41a, 0x401);		

		// Audigy Drive, Headphone out
		OP(6, 0x64, 0x0c0, 0x0c0, 0x400);
		OP(6, 0x65, 0x0c0, 0x0c0, 0x401);

		// ac97 Recording
		OP(6, 0x76, 0x0c0, 0x0c0, 0x40);
		OP(6, 0x77, 0x0c0, 0x0c0, 0x41);
		
		// Center = sub = Left/2 + Right/2
		OP(0xe, 0x400, 0x401, 0xcd, 0x400);

		// center/sub  Volume (master)
		OP(0, 0x06a, 0x0c0, 0x408, 0x400);
		OP(0, 0x06b, 0x0c0, 0x409, 0x400);

		// Digital center/sub  Volume (master)
		OP(0, 0x062, 0x0c0, 0x408, 0x400);
		OP(0, 0x063, 0x0c0, 0x409, 0x400);

		ROUTING_PATCH_START(rpatch, "Routing");
		ROUTING_PATCH_END(rpatch);

		/* delimiter patch */
		patch = PATCH(mgr, patch_n);
		patch->code_size = 0;

	
		sblive_writeptr(card, 0x53, 0, 0);
	} else {
		for (i = 0; i < 512 ; i++)
			OP(6, 0x40, 0x40, 0x40, 0x40);

		for (i = 0; i < 256; i++)
			sblive_writeptr_tag(card, 0,
					    FXGPREGBASE + i, 0,
					    TANKMEMADDRREGBASE + i, 0,
					    TAGLIST_END);

		
		pc = 0;

		//first free GPR = 0x11b
	
		
		/* FX volume correction and Volume control*/
		INPUT_PATCH_START(patch, "Pcm L vol", 0x0, 0);
		GET_OUTPUT_GPR(patch, 0x100, 0x0);
		GET_CONTROL_GPR(patch, 0x106, "Vol", 0, 0x7fffffff);
		GET_DYNAMIC_GPR(patch, 0x112);

		OP(4, 0x112, 0x40, PCM_IN_L, 0x44); //*4	
		OP(0, 0x100, 0x040, 0x112, 0x106);  //*vol	
		INPUT_PATCH_END(patch);


		INPUT_PATCH_START(patch, "Pcm R vol", 0x1, 0);
		GET_OUTPUT_GPR(patch, 0x101, 0x1);
		GET_CONTROL_GPR(patch, 0x107, "Vol", 0, 0x7fffffff);
		GET_DYNAMIC_GPR(patch, 0x112);

		OP(4, 0x112, 0x40, PCM_IN_R, 0x44); 
		OP(0, 0x101, 0x040, 0x112, 0x107);

		INPUT_PATCH_END(patch);


		// CD-Digital In Volume control	
		INPUT_PATCH_START(patch, "CD-Digital Vol L", 0x12, 0);
		GET_OUTPUT_GPR(patch, 0x10c, 0x12);
		GET_CONTROL_GPR(patch, 0x10d, "Vol", 0, 0x7fffffff);

		OP(0, 0x10c, 0x040, SPDIF_CD_L, 0x10d);
		INPUT_PATCH_END(patch);

		INPUT_PATCH_START(patch, "CD-Digital Vol R", 0x13, 0);
		GET_OUTPUT_GPR(patch, 0x10e, 0x13);
		GET_CONTROL_GPR(patch, 0x10f, "Vol", 0, 0x7fffffff);

		OP(0, 0x10e, 0x040, SPDIF_CD_R, 0x10f);
		INPUT_PATCH_END(patch);

		//Volume Correction for Multi-channel Inputs	
		INPUT_PATCH_START(patch, "Multi-Channel Gain", 0x08, 0);
		patch->input=patch->output=0x3F00;

		GET_OUTPUT_GPR(patch, 0x113, MULTI_FRONT_L);
		GET_OUTPUT_GPR(patch, 0x114, MULTI_FRONT_R);
		GET_OUTPUT_GPR(patch, 0x115, MULTI_REAR_L);
		GET_OUTPUT_GPR(patch, 0x116, MULTI_REAR_R);
		GET_OUTPUT_GPR(patch, 0x117, MULTI_CENTER);
		GET_OUTPUT_GPR(patch, 0x118, MULTI_LFE);

		OP(4, 0x113, 0x40, MULTI_FRONT_L, 0x44);
		OP(4, 0x114, 0x40, MULTI_FRONT_R, 0x44);
		OP(4, 0x115, 0x40, MULTI_REAR_L, 0x44);
		OP(4, 0x116, 0x40, MULTI_REAR_R, 0x44);
		OP(4, 0x117, 0x40, MULTI_CENTER, 0x44);
		OP(4, 0x118, 0x40, MULTI_LFE, 0x44);
	
		INPUT_PATCH_END(patch);


		//Routing patch start	
		ROUTING_PATCH_START(rpatch, "Routing");
		GET_INPUT_GPR(rpatch, 0x100, 0x0);
		GET_INPUT_GPR(rpatch, 0x101, 0x1);
		GET_INPUT_GPR(rpatch, 0x10c, 0x12);
		GET_INPUT_GPR(rpatch, 0x10e, 0x13);
		GET_INPUT_GPR(rpatch, 0x113, MULTI_FRONT_L);
		GET_INPUT_GPR(rpatch, 0x114, MULTI_FRONT_R);
		GET_INPUT_GPR(rpatch, 0x115, MULTI_REAR_L);
		GET_INPUT_GPR(rpatch, 0x116, MULTI_REAR_R);
		GET_INPUT_GPR(rpatch, 0x117, MULTI_CENTER);
		GET_INPUT_GPR(rpatch, 0x118, MULTI_LFE);

		GET_DYNAMIC_GPR(rpatch, 0x102);
		GET_DYNAMIC_GPR(rpatch, 0x103);

		GET_OUTPUT_GPR(rpatch, 0x104, 0x8);
		GET_OUTPUT_GPR(rpatch, 0x105, 0x9);
		GET_OUTPUT_GPR(rpatch, 0x10a, 0x2);
		GET_OUTPUT_GPR(rpatch, 0x10b, 0x3);
		
		
		/* input buffer */
		OP(6, 0x102, AC97_IN_L, 0x40, 0x40);
		OP(6, 0x103, AC97_IN_R, 0x40, 0x40);


		/* Digital In + PCM + MULTI_FRONT-> AC97 out (front speakers)*/
		OP(6, AC97_FRONT_L, 0x100, 0x10c, 0x113);

		CONNECT(MULTI_FRONT_L, AC97_FRONT_L);
		CONNECT(PCM_IN_L, AC97_FRONT_L);
		CONNECT(SPDIF_CD_L, AC97_FRONT_L);

		OP(6, AC97_FRONT_R, 0x101, 0x10e, 0x114);

		CONNECT(MULTI_FRONT_R, AC97_FRONT_R);
		CONNECT(PCM_IN_R, AC97_FRONT_R);
		CONNECT(SPDIF_CD_R, AC97_FRONT_R);

		/* Digital In + PCM + AC97 In + PCM1 + MULTI_REAR --> Rear Channel */ 
		OP(6, 0x104, PCM1_IN_L, 0x100, 0x115);
		OP(6, 0x104, 0x104, 0x10c, 0x102);

		CONNECT(MULTI_REAR_L, ANALOG_REAR_L);
		CONNECT(AC97_IN_L, ANALOG_REAR_L);
		CONNECT(PCM_IN_L, ANALOG_REAR_L);
		CONNECT(SPDIF_CD_L, ANALOG_REAR_L);
		CONNECT(PCM1_IN_L, ANALOG_REAR_L);

		OP(6, 0x105, PCM1_IN_R, 0x101, 0x116);
		OP(6, 0x105, 0x105, 0x10e, 0x103);

		CONNECT(MULTI_REAR_R, ANALOG_REAR_R);
		CONNECT(AC97_IN_R, ANALOG_REAR_R);
		CONNECT(PCM_IN_R, ANALOG_REAR_R);
		CONNECT(SPDIF_CD_R, ANALOG_REAR_R);
		CONNECT(PCM1_IN_R, ANALOG_REAR_R);

		/* Digital In + PCM + AC97 In + MULTI_FRONT --> Digital out */
		OP(6, 0x10b, 0x100, 0x102, 0x10c);
		OP(6, 0x10b, 0x10b, 0x113, 0x40);

		CONNECT(MULTI_FRONT_L, DIGITAL_OUT_L);
		CONNECT(PCM_IN_L, DIGITAL_OUT_L);
		CONNECT(AC97_IN_L, DIGITAL_OUT_L);
		CONNECT(SPDIF_CD_L, DIGITAL_OUT_L);

		OP(6, 0x10a, 0x101, 0x103, 0x10e);
		OP(6, 0x10b, 0x10b, 0x114, 0x40);

		CONNECT(MULTI_FRONT_R, DIGITAL_OUT_R);
		CONNECT(PCM_IN_R, DIGITAL_OUT_R);
		CONNECT(AC97_IN_R, DIGITAL_OUT_R);
		CONNECT(SPDIF_CD_R, DIGITAL_OUT_R);

		/* AC97 In --> ADC Recording Buffer */
		OP(6, ADC_REC_L, 0x102, 0x40, 0x40);

		CONNECT(AC97_IN_L, ADC_REC_L);

		OP(6, ADC_REC_R, 0x103, 0x40, 0x40);

		CONNECT(AC97_IN_R, ADC_REC_R);


		/* fx12:Analog-Center */
		OP(6, ANALOG_CENTER, 0x117, 0x40, 0x40);
		CONNECT(MULTI_CENTER, ANALOG_CENTER);

		/* fx11:Analog-LFE */
		OP(6, ANALOG_LFE, 0x118, 0x40, 0x40);
		CONNECT(MULTI_LFE, ANALOG_LFE);

		/* fx12:Digital-Center */
		OP(6, DIGITAL_CENTER, 0x117, 0x40, 0x40);
		CONNECT(MULTI_CENTER, DIGITAL_CENTER);
		
		/* fx11:Analog-LFE */
		OP(6, DIGITAL_LFE, 0x118, 0x40, 0x40);
		CONNECT(MULTI_LFE, DIGITAL_LFE);
	
		ROUTING_PATCH_END(rpatch);


		// Rear volume control	
		OUTPUT_PATCH_START(patch, "Vol Rear L", 0x8, 0);
		GET_INPUT_GPR(patch, 0x104, 0x8);
		GET_CONTROL_GPR(patch, 0x119, "Vol", 0, 0x7fffffff);

		OP(0, ANALOG_REAR_L, 0x040, 0x104, 0x119);
		OUTPUT_PATCH_END(patch);

		OUTPUT_PATCH_START(patch, "Vol Rear R", 0x9, 0);
		GET_INPUT_GPR(patch, 0x105, 0x9);
		GET_CONTROL_GPR(patch, 0x11a, "Vol", 0, 0x7fffffff);

		OP(0, ANALOG_REAR_R, 0x040, 0x105, 0x11a);
		OUTPUT_PATCH_END(patch);


		//Master volume control on front-digital	
		OUTPUT_PATCH_START(patch, "Vol Master L", 0x2, 1);
		GET_INPUT_GPR(patch, 0x10a, 0x2);
		GET_CONTROL_GPR(patch, 0x108, "Vol", 0, 0x7fffffff);

		OP(0, DIGITAL_OUT_L, 0x040, 0x10a, 0x108);
		OUTPUT_PATCH_END(patch);


		OUTPUT_PATCH_START(patch, "Vol Master R", 0x3, 1);
		GET_INPUT_GPR(patch, 0x10b, 0x3);
		GET_CONTROL_GPR(patch, 0x109, "Vol", 0, 0x7fffffff);

		OP(0, DIGITAL_OUT_R, 0x040, 0x10b, 0x109);
		OUTPUT_PATCH_END(patch);


		/* delimiter patch */
		patch = PATCH(mgr, patch_n);
		patch->code_size = 0;

	
		sblive_writeptr(card, DBG, 0, 0);
	}

	spin_lock_init(&mgr->lock);

	// Set up Volume controls, try to keep this the same for both Audigy and Live

	//Master volume
	mgr->ctrl_gpr[SOUND_MIXER_VOLUME][0] = 8;
	mgr->ctrl_gpr[SOUND_MIXER_VOLUME][1] = 9;

	left = card->ac97->mixer_state[SOUND_MIXER_VOLUME] & 0xff;
	right = (card->ac97->mixer_state[SOUND_MIXER_VOLUME] >> 8) & 0xff;

	emu10k1_set_volume_gpr(card, 8, left, 1 << card->ac97->bit_resolution);
	emu10k1_set_volume_gpr(card, 9, right, 1 << card->ac97->bit_resolution);

	//Rear volume
	mgr->ctrl_gpr[ SOUND_MIXER_OGAIN ][0] = 0x19;
	mgr->ctrl_gpr[ SOUND_MIXER_OGAIN ][1] = 0x1a;

	left = right = 67;
	card->ac97->mixer_state[SOUND_MIXER_OGAIN] = (right << 8) | left;

	card->ac97->supported_mixers |= SOUND_MASK_OGAIN;
	card->ac97->stereo_mixers |= SOUND_MASK_OGAIN;

	emu10k1_set_volume_gpr(card, 0x19, left, VOL_5BIT);
	emu10k1_set_volume_gpr(card, 0x1a, right, VOL_5BIT);

	//PCM Volume
	mgr->ctrl_gpr[SOUND_MIXER_PCM][0] = 6;
	mgr->ctrl_gpr[SOUND_MIXER_PCM][1] = 7;

	left = card->ac97->mixer_state[SOUND_MIXER_PCM] & 0xff;
	right = (card->ac97->mixer_state[SOUND_MIXER_PCM] >> 8) & 0xff;

	emu10k1_set_volume_gpr(card, 6, left, VOL_5BIT);
	emu10k1_set_volume_gpr(card, 7, right, VOL_5BIT);

	//CD-Digital Volume
	mgr->ctrl_gpr[SOUND_MIXER_DIGITAL1][0] = 0xd;
	mgr->ctrl_gpr[SOUND_MIXER_DIGITAL1][1] = 0xf;

	left = right = 67;
	card->ac97->mixer_state[SOUND_MIXER_DIGITAL1] = (right << 8) | left; 

	card->ac97->supported_mixers |= SOUND_MASK_DIGITAL1;
	card->ac97->stereo_mixers |= SOUND_MASK_DIGITAL1;

	emu10k1_set_volume_gpr(card, 0xd, left, VOL_5BIT);
	emu10k1_set_volume_gpr(card, 0xf, right, VOL_5BIT);


	//hard wire the ac97's pcm, pcm volume is done above using dsp code.
	if (card->is_audigy)
		//for Audigy, we mute it and use the philips 6 channel DAC instead
		emu10k1_ac97_write(card->ac97, 0x18, 0x8000);
	else
		//For the Live we hardwire it to full volume
		emu10k1_ac97_write(card->ac97, 0x18, 0x0);

	//remove it from the ac97_codec's control
	card->ac97_supported_mixers &= ~SOUND_MASK_PCM;
	card->ac97_stereo_mixers &= ~SOUND_MASK_PCM;

	//set Igain to 0dB by default, maybe consider hardwiring it here.
	emu10k1_ac97_write(card->ac97, AC97_RECORD_GAIN, 0x0000);
	card->ac97->mixer_state[SOUND_MIXER_IGAIN] = 0x101; 

	return 0;
}

static int __devinit hw_init(struct emu10k1_card *card)
{
	int nCh;
	u32 pagecount; /* tmp */
	int ret;

	/* Disable audio and lock cache */
	emu10k1_writefn0(card, HCFG, HCFG_LOCKSOUNDCACHE | HCFG_LOCKTANKCACHE_MASK | HCFG_MUTEBUTTONENABLE);

	/* Reset recording buffers */
	sblive_writeptr_tag(card, 0,
			    MICBS, ADCBS_BUFSIZE_NONE,
			    MICBA, 0,
			    FXBS, ADCBS_BUFSIZE_NONE,
			    FXBA, 0,
			    ADCBS, ADCBS_BUFSIZE_NONE,
			    ADCBA, 0,
			    TAGLIST_END);

	/* Disable channel interrupt */
	emu10k1_writefn0(card, INTE, 0);
	sblive_writeptr_tag(card, 0,
			    CLIEL, 0,
			    CLIEH, 0,
			    SOLEL, 0,
			    SOLEH, 0,
			    TAGLIST_END);

	if (card->is_audigy) {
		sblive_writeptr_tag(card,0,
				    0x5e,0xf00,
				    0x5f,0x3,
				    TAGLIST_END);
	}

	/* Init envelope engine */
	for (nCh = 0; nCh < NUM_G; nCh++) {
		sblive_writeptr_tag(card, nCh,
				    DCYSUSV, 0,
				    IP, 0,
				    VTFT, 0xffff,
				    CVCF, 0xffff,
				    PTRX, 0,
				    //CPF, 0,
				    CCR, 0,

				    PSST, 0,
				    DSL, 0x10,
				    CCCA, 0,
				    Z1, 0,
				    Z2, 0,
				    FXRT, 0xd01c0000,

				    ATKHLDM, 0,
				    DCYSUSM, 0,
				    IFATN, 0xffff,
				    PEFE, 0,
				    FMMOD, 0,
				    TREMFRQ, 24,	/* 1 Hz */
				    FM2FRQ2, 24,	/* 1 Hz */
				    TEMPENV, 0,

				    /*** These are last so OFF prevents writing ***/
				    LFOVAL2, 0,
				    LFOVAL1, 0,
				    ATKHLDV, 0,
				    ENVVOL, 0,
				    ENVVAL, 0,
                                    TAGLIST_END);
		sblive_writeptr(card, CPF, nCh, 0);
		/*
		  Audigy FXRT initialization
		  reversed eng'd, may not be accurate.
		 */
		if (card->is_audigy) {
			sblive_writeptr_tag(card,nCh,
					    0x4c,0x0,
					    0x4d,0x0,
					    0x4e,0x0,
					    0x4f,0x0,
					    A_FXRT1, 0x3f3f3f3f,
					    A_FXRT2, 0x3f3f3f3f,
					    A_SENDAMOUNTS, 0,
					    TAGLIST_END);
		}
	}
	

	/*
	 ** Init to 0x02109204 :
	 ** Clock accuracy    = 0     (1000ppm)
	 ** Sample Rate       = 2     (48kHz)
	 ** Audio Channel     = 1     (Left of 2)
	 ** Source Number     = 0     (Unspecified)
	 ** Generation Status = 1     (Original for Cat Code 12)
	 ** Cat Code          = 12    (Digital Signal Mixer)
	 ** Mode              = 0     (Mode 0)
	 ** Emphasis          = 0     (None)
	 ** CP                = 1     (Copyright unasserted)
	 ** AN                = 0     (Digital audio)
	 ** P                 = 0     (Consumer)
	 */

	sblive_writeptr_tag(card, 0,

			    /* SPDIF0 */
			    SPCS0, (SPCS_CLKACCY_1000PPM | 0x002000000 |
				    SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC | SPCS_GENERATIONSTATUS | 0x00001200 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT),

			    /* SPDIF1 */
			    SPCS1, (SPCS_CLKACCY_1000PPM | 0x002000000 |
				    SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC | SPCS_GENERATIONSTATUS | 0x00001200 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT),

			    /* SPDIF2 & SPDIF3 */
			    SPCS2, (SPCS_CLKACCY_1000PPM | 0x002000000 |
				    SPCS_CHANNELNUM_LEFT | SPCS_SOURCENUM_UNSPEC | SPCS_GENERATIONSTATUS | 0x00001200 | SPCS_EMPHASIS_NONE | SPCS_COPYRIGHT),

			    TAGLIST_END);

	if (card->is_audigy && (card->chiprev == 4)) {
		/* Hacks for Alice3 to work independent of haP16V driver */
		u32 tmp;

		//Setup SRCMulti_I2S SamplingRate
		tmp = sblive_readptr(card, A_SPDIF_SAMPLERATE, 0);
		tmp &= 0xfffff1ff;
		tmp |= (0x2<<9);
		sblive_writeptr(card, A_SPDIF_SAMPLERATE, 0, tmp);

		/* Setup SRCSel (Enable Spdif,I2S SRCMulti) */
		emu10k1_writefn0(card, 0x20, 0x600000);
		emu10k1_writefn0(card, 0x24, 0x14);

		/* Setup SRCMulti Input Audio Enable */
		emu10k1_writefn0(card, 0x20, 0x6E0000);
		emu10k1_writefn0(card, 0x24, 0xFF00FF00);
	}

	ret = fx_init(card);		/* initialize effects engine */
	if (ret < 0)
		return ret;

	card->tankmem.size = 0;

	card->virtualpagetable.size = MAXPAGES * sizeof(u32);

	card->virtualpagetable.addr = pci_alloc_consistent(card->pci_dev, card->virtualpagetable.size, &card->virtualpagetable.dma_handle);
	if (card->virtualpagetable.addr == NULL) {
		ERROR();
		ret = -ENOMEM;
		goto err0;
	}

	card->silentpage.size = EMUPAGESIZE;

	card->silentpage.addr = pci_alloc_consistent(card->pci_dev, card->silentpage.size, &card->silentpage.dma_handle);
	if (card->silentpage.addr == NULL) {
		ERROR();
		ret = -ENOMEM;
		goto err1;
	}

	for (pagecount = 0; pagecount < MAXPAGES; pagecount++)
		((u32 *) card->virtualpagetable.addr)[pagecount] = cpu_to_le32(((u32) card->silentpage.dma_handle * 2) | pagecount);

	/* Init page table & tank memory base register */
	sblive_writeptr_tag(card, 0,
			    PTB, (u32) card->virtualpagetable.dma_handle,
			    TCB, 0,
			    TCBS, 0,
			    TAGLIST_END);

	for (nCh = 0; nCh < NUM_G; nCh++) {
		sblive_writeptr_tag(card, nCh,
				    MAPA, MAP_PTI_MASK | ((u32) card->silentpage.dma_handle * 2),
				    MAPB, MAP_PTI_MASK | ((u32) card->silentpage.dma_handle * 2),
				    TAGLIST_END);
	}

	/* Hokay, now enable the AUD bit */
	/* Enable Audio = 1 */
	/* Mute Disable Audio = 0 */
	/* Lock Tank Memory = 1 */
	/* Lock Sound Memory = 0 */
	/* Auto Mute = 1 */
	if (card->is_audigy) {
		if (card->chiprev == 4)
			emu10k1_writefn0(card, HCFG, HCFG_AUDIOENABLE  | HCFG_AC3ENABLE_CDSPDIF | HCFG_AC3ENABLE_GPSPDIF | HCFG_AUTOMUTE | HCFG_JOYENABLE);
		else
			emu10k1_writefn0(card, HCFG, HCFG_AUDIOENABLE  | HCFG_AUTOMUTE | HCFG_JOYENABLE);
	} else {
		if (card->model == 0x20 || card->model == 0xc400 ||
		 (card->model == 0x21 && card->chiprev < 6))
	        	emu10k1_writefn0(card, HCFG, HCFG_AUDIOENABLE  | HCFG_LOCKTANKCACHE_MASK | HCFG_AUTOMUTE);
		else
			emu10k1_writefn0(card, HCFG, HCFG_AUDIOENABLE  | HCFG_LOCKTANKCACHE_MASK | HCFG_AUTOMUTE | HCFG_JOYENABLE);
	}
	/* Enable Vol_Ctrl irqs */
	emu10k1_irq_enable(card, INTE_VOLINCRENABLE | INTE_VOLDECRENABLE | INTE_MUTEENABLE | INTE_FXDSPENABLE);

	if (card->is_audigy && (card->chiprev == 4)) {
		/* Unmute Analog now.  Set GPO6 to 1 for Apollo.
		 * This has to be done after init ALice3 I2SOut beyond 48KHz.
		 * So, sequence is important. */
		u32 tmp = emu10k1_readfn0(card, A_IOCFG);
		tmp |= 0x0040;
		emu10k1_writefn0(card, A_IOCFG, tmp);
	}
	
	/* FIXME: TOSLink detection */
	card->has_toslink = 0;

	/* Initialize digital passthrough variables */
	card->pt.pos_gpr = card->pt.intr_gpr = card->pt.enable_gpr = -1;
	card->pt.selected = 0;
	card->pt.state = PT_STATE_INACTIVE;
	card->pt.spcs_to_use = 0x01;
	card->pt.patch_name = "AC3pass";
	card->pt.intr_gpr_name = "count";
	card->pt.enable_gpr_name = "enable";
	card->pt.pos_gpr_name = "ptr";
	spin_lock_init(&card->pt.lock);
	init_waitqueue_head(&card->pt.wait);

/*	tmp = sblive_readfn0(card, HCFG);
	if (tmp & (HCFG_GPINPUT0 | HCFG_GPINPUT1)) {
		sblive_writefn0(card, HCFG, tmp | 0x800);

		udelay(512);

		if (tmp != (sblive_readfn0(card, HCFG) & ~0x800)) {
			card->has_toslink = 1;
			sblive_writefn0(card, HCFG, tmp);
		}
	}
*/
	return 0;

  err1:
	pci_free_consistent(card->pci_dev, card->virtualpagetable.size, card->virtualpagetable.addr, card->virtualpagetable.dma_handle);
  err0:
	fx_cleanup(&card->mgr);

	return ret;
}

static int __devinit emu10k1_init(struct emu10k1_card *card)
{
	/* Init Card */
	if (hw_init(card) < 0)
		return -1;

	voice_init(card);
	timer_init(card);
	addxmgr_init(card);

	DPD(2, "  hw control register -> %#x\n", emu10k1_readfn0(card, HCFG));

	return 0;
}

static void emu10k1_cleanup(struct emu10k1_card *card)
{
	int ch;

	emu10k1_writefn0(card, INTE, 0);

	/** Shutdown the chip **/
	for (ch = 0; ch < NUM_G; ch++)
		sblive_writeptr(card, DCYSUSV, ch, 0);

	for (ch = 0; ch < NUM_G; ch++) {
		sblive_writeptr_tag(card, ch,
				    VTFT, 0,
				    CVCF, 0,
				    PTRX, 0,
				    //CPF, 0,
				    TAGLIST_END);
		sblive_writeptr(card, CPF, ch, 0);
	}

	/* Disable audio and lock cache */
	emu10k1_writefn0(card, HCFG, HCFG_LOCKSOUNDCACHE | HCFG_LOCKTANKCACHE_MASK | HCFG_MUTEBUTTONENABLE);

	sblive_writeptr_tag(card, 0,
                            PTB, 0,

			    /* Reset recording buffers */
			    MICBS, ADCBS_BUFSIZE_NONE,
			    MICBA, 0,
			    FXBS, ADCBS_BUFSIZE_NONE,
			    FXBA, 0,
			    FXWC, 0,
			    ADCBS, ADCBS_BUFSIZE_NONE,
			    ADCBA, 0,
			    TCBS, 0,
			    TCB, 0,
			    DBG, 0x8000,

			    /* Disable channel interrupt */
			    CLIEL, 0,
			    CLIEH, 0,
			    SOLEL, 0,
			    SOLEH, 0,
			    TAGLIST_END);

	if (card->is_audigy)
		sblive_writeptr(card, 0, A_DBG,  A_DBG_SINGLE_STEP);

	pci_free_consistent(card->pci_dev, card->virtualpagetable.size, card->virtualpagetable.addr, card->virtualpagetable.dma_handle);
	pci_free_consistent(card->pci_dev, card->silentpage.size, card->silentpage.addr, card->silentpage.dma_handle);
	
	if(card->tankmem.size != 0)
		pci_free_consistent(card->pci_dev, card->tankmem.size, card->tankmem.addr, card->tankmem.dma_handle);

	/* release patch storage memory */
	fx_cleanup(&card->mgr);
}

/* Driver initialization routine */
static int __devinit emu10k1_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
	struct emu10k1_card *card;
	u32 subsysvid;
	int ret;

	if (pci_set_dma_mask(pci_dev, EMU10K1_DMA_MASK)) {
		printk(KERN_ERR "emu10k1: architecture does not support 29bit PCI busmaster DMA\n");
		return -ENODEV;
	}

	if (pci_enable_device(pci_dev))
		return -EIO;

	pci_set_master(pci_dev);

	if ((card = kmalloc(sizeof(struct emu10k1_card), GFP_KERNEL)) == NULL) {
                printk(KERN_ERR "emu10k1: out of memory\n");
                return -ENOMEM;
        }
        memset(card, 0, sizeof(struct emu10k1_card));

	card->iobase = pci_resource_start(pci_dev, 0);
	card->length = pci_resource_len(pci_dev, 0); 

	if (request_region(card->iobase, card->length, card_names[pci_id->driver_data]) == NULL) {
		printk(KERN_ERR "emu10k1: IO space in use\n");
		ret = -EBUSY;
		goto err_region;
	}

	pci_set_drvdata(pci_dev, card);

	card->irq = pci_dev->irq;
	card->pci_dev = pci_dev;

	/* Reserve IRQ Line */
	if (request_irq(card->irq, emu10k1_interrupt, SA_SHIRQ, card_names[pci_id->driver_data], card)) {
		printk(KERN_ERR "emu10k1: IRQ in use\n");
		ret = -EBUSY;
		goto err_irq;
	}

	pci_read_config_byte(pci_dev, PCI_REVISION_ID, &card->chiprev);
	pci_read_config_word(pci_dev, PCI_SUBSYSTEM_ID, &card->model);

	printk(KERN_INFO "emu10k1: %s rev %d model %#04x found, IO at %#04lx-%#04lx, IRQ %d\n",
		card_names[pci_id->driver_data], card->chiprev, card->model, card->iobase,
		card->iobase + card->length - 1, card->irq);

	if (pci_id->device == PCI_DEVICE_ID_CREATIVE_AUDIGY)
		card->is_audigy = 1;

	pci_read_config_dword(pci_dev, PCI_SUBSYSTEM_VENDOR_ID, &subsysvid);
	card->is_aps = (subsysvid == EMU_APS_SUBID);

	spin_lock_init(&card->lock);
	mutex_init(&card->open_sem);
	card->open_mode = 0;
	init_waitqueue_head(&card->open_wait);

	ret = emu10k1_audio_init(card);
	if (ret < 0) {
                printk(KERN_ERR "emu10k1: cannot initialize audio devices\n");
                goto err_audio;
        }

	ret = emu10k1_mixer_init(card);
	if (ret < 0) {
		printk(KERN_ERR "emu10k1: cannot initialize AC97 codec\n");
                goto err_mixer;
	}

	ret = emu10k1_midi_init(card);
	if (ret < 0) {
		printk(KERN_ERR "emu10k1: cannot register midi device\n");
		goto err_midi;
	}

	ret = emu10k1_init(card);
	if (ret < 0) {
		printk(KERN_ERR "emu10k1: cannot initialize device\n");
		goto err_emu10k1_init;
	}

	if (card->is_aps)
		emu10k1_ecard_init(card);

	ret = emu10k1_register_devices(card);
	if (ret < 0)
		goto err_register;

	/* proc entries must be created after registering devices, as
	 * emu10k1_info_proc prints card->audio_dev &co. */
	ret = emu10k1_proc_init(card);
	if (ret < 0) {
		printk(KERN_ERR "emu10k1: cannot initialize proc directory\n");
                goto err_proc;
	}
	
	list_add(&card->list, &emu10k1_devs);

	return 0;

err_proc:
	emu10k1_unregister_devices(card);

err_register:
	emu10k1_cleanup(card);
	
err_emu10k1_init:
	emu10k1_midi_cleanup(card);

err_midi:
	emu10k1_mixer_cleanup(card);

err_mixer:
	emu10k1_audio_cleanup(card);

err_audio:
	free_irq(card->irq, card);

err_irq:
	release_region(card->iobase, card->length);
	pci_set_drvdata(pci_dev, NULL);

err_region:
	kfree(card);

	return ret;
}

static void __devexit emu10k1_remove(struct pci_dev *pci_dev)
{
	struct emu10k1_card *card = pci_get_drvdata(pci_dev);

	list_del(&card->list);

	emu10k1_unregister_devices(card);
	emu10k1_cleanup(card);
	emu10k1_midi_cleanup(card);
	emu10k1_mixer_cleanup(card);
	emu10k1_proc_cleanup(card);
	emu10k1_audio_cleanup(card);	
	free_irq(card->irq, card);
	release_region(card->iobase, card->length);
	kfree(card);
	pci_set_drvdata(pci_dev, NULL);
}

MODULE_AUTHOR("Bertrand Lee, Cai Ying. (Email to: emu10k1-devel@lists.sourceforge.net)");
MODULE_DESCRIPTION("Creative EMU10K1 PCI Audio Driver v" DRIVER_VERSION "\nCopyright (C) 1999 Creative Technology Ltd.");
MODULE_LICENSE("GPL");

static struct pci_driver emu10k1_pci_driver = {
	.name		= "emu10k1",
	.id_table	= emu10k1_pci_tbl,
	.probe		= emu10k1_probe,
	.remove		= __devexit_p(emu10k1_remove),
};

static int __init emu10k1_init_module(void)
{
	printk(KERN_INFO "Creative EMU10K1 PCI Audio Driver, version " DRIVER_VERSION ", " __TIME__ " " __DATE__ "\n");

	return pci_register_driver(&emu10k1_pci_driver);
}

static void __exit emu10k1_cleanup_module(void)
{
	pci_unregister_driver(&emu10k1_pci_driver);

	return;
}

module_init(emu10k1_init_module);
module_exit(emu10k1_cleanup_module);

#ifdef EMU10K1_SEQUENCER

/* in midi.c */
extern int emu10k1_seq_midi_open(int dev, int mode, 
				void (*input)(int dev, unsigned char midi_byte),
				void (*output)(int dev));
extern void emu10k1_seq_midi_close(int dev);
extern int emu10k1_seq_midi_out(int dev, unsigned char midi_byte);
extern int emu10k1_seq_midi_start_read(int dev);
extern int emu10k1_seq_midi_end_read(int dev);
extern void emu10k1_seq_midi_kick(int dev);
extern int emu10k1_seq_midi_buffer_status(int dev);

static struct midi_operations emu10k1_midi_operations =
{
	THIS_MODULE,
	{"EMU10K1 MIDI", 0, 0, SNDCARD_EMU10K1},
	&std_midi_synth,
	{0},
	emu10k1_seq_midi_open,
	emu10k1_seq_midi_close,
	NULL,
	emu10k1_seq_midi_out,
	emu10k1_seq_midi_start_read,
	emu10k1_seq_midi_end_read,
	emu10k1_seq_midi_kick,
	NULL,
	emu10k1_seq_midi_buffer_status,
	NULL
};

#endif
