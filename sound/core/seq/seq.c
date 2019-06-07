// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  ALSA sequencer main module
 *  Copyright (c) 1998-1999 by Frank van de Pol <fvdpol@coil.demon.nl>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/initval.h>

#include <sound/seq_kernel.h>
#include "seq_clientmgr.h"
#include "seq_memory.h"
#include "seq_queue.h"
#include "seq_lock.h"
#include "seq_timer.h"
#include "seq_system.h"
#include "seq_info.h"
#include <sound/minors.h>
#include <sound/seq_device.h>

#if defined(CONFIG_SND_SEQ_DUMMY_MODULE)
int seq_client_load[15] = {[0] = SNDRV_SEQ_CLIENT_DUMMY, [1 ... 14] = -1};
#else
int seq_client_load[15] = {[0 ... 14] = -1};
#endif
int seq_default_timer_class = SNDRV_TIMER_CLASS_GLOBAL;
int seq_default_timer_sclass = SNDRV_TIMER_SCLASS_NONE;
int seq_default_timer_card = -1;
int seq_default_timer_device =
#ifdef CONFIG_SND_SEQ_HRTIMER_DEFAULT
	SNDRV_TIMER_GLOBAL_HRTIMER
#else
	SNDRV_TIMER_GLOBAL_SYSTEM
#endif
	;
int seq_default_timer_subdevice = 0;
int seq_default_timer_resolution = 0;	/* Hz */

MODULE_AUTHOR("Frank van de Pol <fvdpol@coil.demon.nl>, Jaroslav Kysela <perex@perex.cz>");
MODULE_DESCRIPTION("Advanced Linux Sound Architecture sequencer.");
MODULE_LICENSE("GPL");

module_param_array(seq_client_load, int, NULL, 0444);
MODULE_PARM_DESC(seq_client_load, "The numbers of global (system) clients to load through kmod.");
module_param(seq_default_timer_class, int, 0644);
MODULE_PARM_DESC(seq_default_timer_class, "The default timer class.");
module_param(seq_default_timer_sclass, int, 0644);
MODULE_PARM_DESC(seq_default_timer_sclass, "The default timer slave class.");
module_param(seq_default_timer_card, int, 0644);
MODULE_PARM_DESC(seq_default_timer_card, "The default timer card number.");
module_param(seq_default_timer_device, int, 0644);
MODULE_PARM_DESC(seq_default_timer_device, "The default timer device number.");
module_param(seq_default_timer_subdevice, int, 0644);
MODULE_PARM_DESC(seq_default_timer_subdevice, "The default timer subdevice number.");
module_param(seq_default_timer_resolution, int, 0644);
MODULE_PARM_DESC(seq_default_timer_resolution, "The default timer resolution in Hz.");

MODULE_ALIAS_CHARDEV(CONFIG_SND_MAJOR, SNDRV_MINOR_SEQUENCER);
MODULE_ALIAS("devname:snd/seq");

/*
 *  INIT PART
 */

static int __init alsa_seq_init(void)
{
	int err;

	err = client_init_data();
	if (err < 0)
		goto error;

	/* register sequencer device */
	err = snd_sequencer_device_init();
	if (err < 0)
		goto error;

	/* register proc interface */
	err = snd_seq_info_init();
	if (err < 0)
		goto error_device;

	/* register our internal client */
	err = snd_seq_system_client_init();
	if (err < 0)
		goto error_info;

	snd_seq_autoload_init();
	return 0;

 error_info:
	snd_seq_info_done();
 error_device:
	snd_sequencer_device_done();
 error:
	return err;
}

static void __exit alsa_seq_exit(void)
{
	/* unregister our internal client */
	snd_seq_system_client_done();

	/* unregister proc interface */
	snd_seq_info_done();
	
	/* delete timing queues */
	snd_seq_queues_delete();

	/* unregister sequencer device */
	snd_sequencer_device_done();

	snd_seq_autoload_exit();
}

module_init(alsa_seq_init)
module_exit(alsa_seq_exit)
