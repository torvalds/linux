/*
 * sound/oss/dev_table.c
 *
 * Device call tables.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

#include <linux/init.h>

#include "sound_config.h"

struct audio_operations *audio_devs[MAX_AUDIO_DEV];
EXPORT_SYMBOL(audio_devs);

int num_audiodevs;
EXPORT_SYMBOL(num_audiodevs);

struct mixer_operations *mixer_devs[MAX_MIXER_DEV];
EXPORT_SYMBOL(mixer_devs);

int num_mixers;
EXPORT_SYMBOL(num_mixers);

struct synth_operations *synth_devs[MAX_SYNTH_DEV+MAX_MIDI_DEV];
EXPORT_SYMBOL(synth_devs);

int num_synths;

struct midi_operations *midi_devs[MAX_MIDI_DEV];
EXPORT_SYMBOL(midi_devs);

int num_midis;
EXPORT_SYMBOL(num_midis);

struct sound_timer_operations *sound_timer_devs[MAX_TIMER_DEV] = {
	&default_sound_timer, NULL
};
EXPORT_SYMBOL(sound_timer_devs);

int num_sound_timers = 1;


static int sound_alloc_audiodev(void);

int sound_install_audiodrv(int vers, char *name, struct audio_driver *driver,
			int driver_size, int flags, unsigned int format_mask,
			void *devc, int dma1, int dma2)
{
	struct audio_driver *d;
	struct audio_operations *op;
	int num;

	if (vers != AUDIO_DRIVER_VERSION || driver_size > sizeof(struct audio_driver)) {
		printk(KERN_ERR "Sound: Incompatible audio driver for %s\n", name);
		return -(EINVAL);
	}
	num = sound_alloc_audiodev();

	if (num == -1) {
		printk(KERN_ERR "sound: Too many audio drivers\n");
		return -(EBUSY);
	}
	d = (struct audio_driver *) (sound_mem_blocks[sound_nblocks] = vmalloc(sizeof(struct audio_driver)));
	sound_nblocks++;
	if (sound_nblocks >= MAX_MEM_BLOCKS)
		sound_nblocks = MAX_MEM_BLOCKS - 1;

	op = (struct audio_operations *) (sound_mem_blocks[sound_nblocks] = vmalloc(sizeof(struct audio_operations)));
	sound_nblocks++;
	if (sound_nblocks >= MAX_MEM_BLOCKS)
		sound_nblocks = MAX_MEM_BLOCKS - 1;

	if (d == NULL || op == NULL) {
		printk(KERN_ERR "Sound: Can't allocate driver for (%s)\n", name);
		sound_unload_audiodev(num);
		return -(ENOMEM);
	}
	memset((char *) op, 0, sizeof(struct audio_operations));
	init_waitqueue_head(&op->in_sleeper);
	init_waitqueue_head(&op->out_sleeper);	
	init_waitqueue_head(&op->poll_sleeper);
	if (driver_size < sizeof(struct audio_driver))
		memset((char *) d, 0, sizeof(struct audio_driver));

	memcpy((char *) d, (char *) driver, driver_size);

	op->d = d;
	strlcpy(op->name, name, sizeof(op->name));
	op->flags = flags;
	op->format_mask = format_mask;
	op->devc = devc;

	/*
	 *    Hardcoded defaults
	 */
	audio_devs[num] = op;

	DMAbuf_init(num, dma1, dma2);

	audio_init_devices();
	return num;
}
EXPORT_SYMBOL(sound_install_audiodrv);

int sound_install_mixer(int vers, char *name, struct mixer_operations *driver,
	int driver_size, void *devc)
{
	struct mixer_operations *op;

	int n = sound_alloc_mixerdev();

	if (n == -1) {
		printk(KERN_ERR "Sound: Too many mixer drivers\n");
		return -EBUSY;
	}
	if (vers != MIXER_DRIVER_VERSION ||
		driver_size > sizeof(struct mixer_operations)) {
		printk(KERN_ERR "Sound: Incompatible mixer driver for %s\n", name);
		return -EINVAL;
	}
	
	/* FIXME: This leaks a mixer_operations struct every time its called
	   until you unload sound! */
	   
	op = (struct mixer_operations *) (sound_mem_blocks[sound_nblocks] = vmalloc(sizeof(struct mixer_operations)));
	sound_nblocks++;
	if (sound_nblocks >= MAX_MEM_BLOCKS)
		sound_nblocks = MAX_MEM_BLOCKS - 1;

	if (op == NULL) {
		printk(KERN_ERR "Sound: Can't allocate mixer driver for (%s)\n", name);
		return -ENOMEM;
	}
	memset((char *) op, 0, sizeof(struct mixer_operations));
	memcpy((char *) op, (char *) driver, driver_size);

	strlcpy(op->name, name, sizeof(op->name));
	op->devc = devc;

	mixer_devs[n] = op;
	return n;
}
EXPORT_SYMBOL(sound_install_mixer);

void sound_unload_audiodev(int dev)
{
	if (dev != -1) {
		DMAbuf_deinit(dev);
		audio_devs[dev] = NULL;
		unregister_sound_dsp((dev<<4)+3);
	}
}
EXPORT_SYMBOL(sound_unload_audiodev);

static int sound_alloc_audiodev(void)
{ 
	int i = register_sound_dsp(&oss_sound_fops, -1);
	if(i==-1)
		return i;
	i>>=4;
	if(i>=num_audiodevs)
		num_audiodevs = i + 1;
	return i;
}

int sound_alloc_mididev(void)
{
	int i = register_sound_midi(&oss_sound_fops, -1);
	if(i==-1)
		return i;
	i>>=4;
	if(i>=num_midis)
		num_midis = i + 1;
	return i;
}
EXPORT_SYMBOL(sound_alloc_mididev);

int sound_alloc_synthdev(void)
{
	int i;

	for (i = 0; i < MAX_SYNTH_DEV; i++) {
		if (synth_devs[i] == NULL) {
			if (i >= num_synths)
				num_synths++;
			return i;
		}
	}
	return -1;
}
EXPORT_SYMBOL(sound_alloc_synthdev);

int sound_alloc_mixerdev(void)
{
	int i = register_sound_mixer(&oss_sound_fops, -1);
	if(i==-1)
		return -1;
	i>>=4;
	if(i>=num_mixers)
		num_mixers = i + 1;
	return i;
}
EXPORT_SYMBOL(sound_alloc_mixerdev);

int sound_alloc_timerdev(void)
{
	int i;

	for (i = 0; i < MAX_TIMER_DEV; i++) {
		if (sound_timer_devs[i] == NULL) {
			if (i >= num_sound_timers)
				num_sound_timers++;
			return i;
		}
	}
	return -1;
}
EXPORT_SYMBOL(sound_alloc_timerdev);

void sound_unload_mixerdev(int dev)
{
	if (dev != -1) {
		mixer_devs[dev] = NULL;
		unregister_sound_mixer(dev<<4);
		num_mixers--;
	}
}
EXPORT_SYMBOL(sound_unload_mixerdev);

void sound_unload_mididev(int dev)
{
	if (dev != -1) {
		midi_devs[dev] = NULL;
		unregister_sound_midi((dev<<4)+2);
	}
}
EXPORT_SYMBOL(sound_unload_mididev);

void sound_unload_synthdev(int dev)
{
	if (dev != -1)
		synth_devs[dev] = NULL;
}
EXPORT_SYMBOL(sound_unload_synthdev);

void sound_unload_timerdev(int dev)
{
	if (dev != -1)
		sound_timer_devs[dev] = NULL;
}
EXPORT_SYMBOL(sound_unload_timerdev);

