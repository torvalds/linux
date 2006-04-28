/*
 *  Initialization routines
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/file.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/ctype.h>
#include <linux/pci.h>
#include <linux/pm.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>

struct snd_shutdown_f_ops {
	struct file_operations f_ops;
	struct snd_shutdown_f_ops *next;
};

unsigned int snd_cards_lock = 0;	/* locked for registering/using */
struct snd_card *snd_cards[SNDRV_CARDS] = {[0 ... (SNDRV_CARDS-1)] = NULL};
EXPORT_SYMBOL(snd_cards);

DEFINE_RWLOCK(snd_card_rwlock);

#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
int (*snd_mixer_oss_notify_callback)(struct snd_card *card, int free_flag);
EXPORT_SYMBOL(snd_mixer_oss_notify_callback);
#endif

#ifdef CONFIG_PROC_FS
static void snd_card_id_read(struct snd_info_entry *entry,
			     struct snd_info_buffer *buffer)
{
	snd_iprintf(buffer, "%s\n", entry->card->id);
}

static inline int init_info_for_card(struct snd_card *card)
{
	int err;
	struct snd_info_entry *entry;

	if ((err = snd_info_card_register(card)) < 0) {
		snd_printd("unable to create card info\n");
		return err;
	}
	if ((entry = snd_info_create_card_entry(card, "id", card->proc_root)) == NULL) {
		snd_printd("unable to create card entry\n");
		return err;
	}
	entry->c.text.read = snd_card_id_read;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		entry = NULL;
	}
	card->proc_id = entry;
	return 0;
}
#else /* !CONFIG_PROC_FS */
#define init_info_for_card(card)
#endif

static void snd_card_free_thread(void * __card);

/**
 *  snd_card_new - create and initialize a soundcard structure
 *  @idx: card index (address) [0 ... (SNDRV_CARDS-1)]
 *  @xid: card identification (ASCII string)
 *  @module: top level module for locking
 *  @extra_size: allocate this extra size after the main soundcard structure
 *
 *  Creates and initializes a soundcard structure.
 *
 *  Returns kmallocated snd_card structure. Creates the ALSA control interface
 *  (which is blocked until snd_card_register function is called).
 */
struct snd_card *snd_card_new(int idx, const char *xid,
			 struct module *module, int extra_size)
{
	struct snd_card *card;
	int err;

	if (extra_size < 0)
		extra_size = 0;
	card = kzalloc(sizeof(*card) + extra_size, GFP_KERNEL);
	if (card == NULL)
		return NULL;
	if (xid) {
		if (!snd_info_check_reserved_words(xid))
			goto __error;
		strlcpy(card->id, xid, sizeof(card->id));
	}
	err = 0;
	write_lock(&snd_card_rwlock);
	if (idx < 0) {
		int idx2;
		for (idx2 = 0; idx2 < SNDRV_CARDS; idx2++)
			if (~snd_cards_lock & idx & 1<<idx2) {
				idx = idx2;
				if (idx >= snd_ecards_limit)
					snd_ecards_limit = idx + 1;
				break;
			}
	} else if (idx < snd_ecards_limit) {
		if (snd_cards_lock & (1 << idx))
			err = -ENODEV;	/* invalid */
	} else if (idx < SNDRV_CARDS)
		snd_ecards_limit = idx + 1; /* increase the limit */
	else
		err = -ENODEV;
	if (idx < 0 || err < 0) {
		write_unlock(&snd_card_rwlock);
		snd_printk(KERN_ERR "cannot find the slot for index %d (range 0-%i)\n", idx, snd_ecards_limit - 1);
		goto __error;
	}
	snd_cards_lock |= 1 << idx;		/* lock it */
	write_unlock(&snd_card_rwlock);
	card->number = idx;
	card->module = module;
	INIT_LIST_HEAD(&card->devices);
	init_rwsem(&card->controls_rwsem);
	rwlock_init(&card->ctl_files_rwlock);
	INIT_LIST_HEAD(&card->controls);
	INIT_LIST_HEAD(&card->ctl_files);
	spin_lock_init(&card->files_lock);
	init_waitqueue_head(&card->shutdown_sleep);
	INIT_WORK(&card->free_workq, snd_card_free_thread, card);
#ifdef CONFIG_PM
	mutex_init(&card->power_lock);
	init_waitqueue_head(&card->power_sleep);
#endif
	/* the control interface cannot be accessed from the user space until */
	/* snd_cards_bitmask and snd_cards are set with snd_card_register */
	if ((err = snd_ctl_create(card)) < 0) {
		snd_printd("unable to register control minors\n");
		goto __error;
	}
	if ((err = snd_info_card_create(card)) < 0) {
		snd_printd("unable to create card info\n");
		goto __error_ctl;
	}
	if (extra_size > 0)
		card->private_data = (char *)card + sizeof(struct snd_card);
	return card;

      __error_ctl:
	snd_device_free_all(card, SNDRV_DEV_CMD_PRE);
      __error:
	kfree(card);
      	return NULL;
}

EXPORT_SYMBOL(snd_card_new);

static loff_t snd_disconnect_llseek(struct file *file, loff_t offset, int orig)
{
	return -ENODEV;
}

static ssize_t snd_disconnect_read(struct file *file, char __user *buf,
				   size_t count, loff_t *offset)
{
	return -ENODEV;
}

static ssize_t snd_disconnect_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *offset)
{
	return -ENODEV;
}

static unsigned int snd_disconnect_poll(struct file * file, poll_table * wait)
{
	return POLLERR | POLLNVAL;
}

static long snd_disconnect_ioctl(struct file *file,
				 unsigned int cmd, unsigned long arg)
{
	return -ENODEV;
}

static int snd_disconnect_mmap(struct file *file, struct vm_area_struct *vma)
{
	return -ENODEV;
}

static int snd_disconnect_fasync(int fd, struct file *file, int on)
{
	return -ENODEV;
}

/**
 *  snd_card_disconnect - disconnect all APIs from the file-operations (user space)
 *  @card: soundcard structure
 *
 *  Disconnects all APIs from the file-operations (user space).
 *
 *  Returns zero, otherwise a negative error code.
 *
 *  Note: The current implementation replaces all active file->f_op with special
 *        dummy file operations (they do nothing except release).
 */
int snd_card_disconnect(struct snd_card *card)
{
	struct snd_monitor_file *mfile;
	struct file *file;
	struct snd_shutdown_f_ops *s_f_ops;
	struct file_operations *f_ops;
	const struct file_operations *old_f_ops;
	int err;

	spin_lock(&card->files_lock);
	if (card->shutdown) {
		spin_unlock(&card->files_lock);
		return 0;
	}
	card->shutdown = 1;
	spin_unlock(&card->files_lock);

	/* phase 1: disable fops (user space) operations for ALSA API */
	write_lock(&snd_card_rwlock);
	snd_cards[card->number] = NULL;
	write_unlock(&snd_card_rwlock);
	
	/* phase 2: replace file->f_op with special dummy operations */
	
	spin_lock(&card->files_lock);
	mfile = card->files;
	while (mfile) {
		file = mfile->file;

		/* it's critical part, use endless loop */
		/* we have no room to fail */
		s_f_ops = kmalloc(sizeof(struct snd_shutdown_f_ops), GFP_ATOMIC);
		if (s_f_ops == NULL)
			panic("Atomic allocation failed for snd_shutdown_f_ops!");

		f_ops = &s_f_ops->f_ops;

		memset(f_ops, 0, sizeof(*f_ops));
		f_ops->owner = file->f_op->owner;
		f_ops->release = file->f_op->release;
		f_ops->llseek = snd_disconnect_llseek;
		f_ops->read = snd_disconnect_read;
		f_ops->write = snd_disconnect_write;
		f_ops->poll = snd_disconnect_poll;
		f_ops->unlocked_ioctl = snd_disconnect_ioctl;
#ifdef CONFIG_COMPAT
		f_ops->compat_ioctl = snd_disconnect_ioctl;
#endif
		f_ops->mmap = snd_disconnect_mmap;
		f_ops->fasync = snd_disconnect_fasync;

		s_f_ops->next = card->s_f_ops;
		card->s_f_ops = s_f_ops;
		
		f_ops = fops_get(f_ops);

		old_f_ops = file->f_op;
		file->f_op = f_ops;	/* must be atomic */
		fops_put(old_f_ops);
		
		mfile = mfile->next;
	}
	spin_unlock(&card->files_lock);	

	/* phase 3: notify all connected devices about disconnection */
	/* at this point, they cannot respond to any calls except release() */

#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
	if (snd_mixer_oss_notify_callback)
		snd_mixer_oss_notify_callback(card, SND_MIXER_OSS_NOTIFY_DISCONNECT);
#endif

	/* notify all devices that we are disconnected */
	err = snd_device_disconnect_all(card);
	if (err < 0)
		snd_printk(KERN_ERR "not all devices for card %i can be disconnected\n", card->number);

	return 0;	
}

EXPORT_SYMBOL(snd_card_disconnect);

/**
 *  snd_card_free - frees given soundcard structure
 *  @card: soundcard structure
 *
 *  This function releases the soundcard structure and the all assigned
 *  devices automatically.  That is, you don't have to release the devices
 *  by yourself.
 *
 *  Returns zero. Frees all associated devices and frees the control
 *  interface associated to given soundcard.
 */
int snd_card_free(struct snd_card *card)
{
	struct snd_shutdown_f_ops *s_f_ops;

	if (card == NULL)
		return -EINVAL;
	write_lock(&snd_card_rwlock);
	snd_cards[card->number] = NULL;
	write_unlock(&snd_card_rwlock);

#ifdef CONFIG_PM
	wake_up(&card->power_sleep);
#endif
	/* wait, until all devices are ready for the free operation */
	wait_event(card->shutdown_sleep, card->files == NULL);

#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
	if (snd_mixer_oss_notify_callback)
		snd_mixer_oss_notify_callback(card, SND_MIXER_OSS_NOTIFY_FREE);
#endif
	if (snd_device_free_all(card, SNDRV_DEV_CMD_PRE) < 0) {
		snd_printk(KERN_ERR "unable to free all devices (pre)\n");
		/* Fatal, but this situation should never occur */
	}
	if (snd_device_free_all(card, SNDRV_DEV_CMD_NORMAL) < 0) {
		snd_printk(KERN_ERR "unable to free all devices (normal)\n");
		/* Fatal, but this situation should never occur */
	}
	if (snd_device_free_all(card, SNDRV_DEV_CMD_POST) < 0) {
		snd_printk(KERN_ERR "unable to free all devices (post)\n");
		/* Fatal, but this situation should never occur */
	}
	if (card->private_free)
		card->private_free(card);
	snd_info_unregister(card->proc_id);
	if (snd_info_card_free(card) < 0) {
		snd_printk(KERN_WARNING "unable to free card info\n");
		/* Not fatal error */
	}
	while (card->s_f_ops) {
		s_f_ops = card->s_f_ops;
		card->s_f_ops = s_f_ops->next;
		kfree(s_f_ops);
	}
	write_lock(&snd_card_rwlock);
	snd_cards_lock &= ~(1 << card->number);
	write_unlock(&snd_card_rwlock);
	kfree(card);
	return 0;
}

EXPORT_SYMBOL(snd_card_free);

static void snd_card_free_thread(void * __card)
{
	struct snd_card *card = __card;
	struct module * module = card->module;

	if (!try_module_get(module)) {
		snd_printk(KERN_ERR "unable to lock toplevel module for card %i in free thread\n", card->number);
		module = NULL;
	}

	snd_card_free(card);

	module_put(module);
}

/**
 *  snd_card_free_in_thread - call snd_card_free() in thread
 *  @card: soundcard structure
 *
 *  This function schedules the call of snd_card_free() function in a
 *  work queue.  When all devices are released (non-busy), the work
 *  is woken up and calls snd_card_free().
 *
 *  When a card can be disconnected at any time by hotplug service,
 *  this function should be used in disconnect (or detach) callback
 *  instead of calling snd_card_free() directly.
 *  
 *  Returns - zero otherwise a negative error code if the start of thread failed.
 */
int snd_card_free_in_thread(struct snd_card *card)
{
	if (card->files == NULL) {
		snd_card_free(card);
		return 0;
	}

	if (schedule_work(&card->free_workq))
		return 0;

	snd_printk(KERN_ERR "schedule_work() failed in snd_card_free_in_thread for card %i\n", card->number);
	/* try to free the structure immediately */
	snd_card_free(card);
	return -EFAULT;
}

EXPORT_SYMBOL(snd_card_free_in_thread);

static void choose_default_id(struct snd_card *card)
{
	int i, len, idx_flag = 0, loops = SNDRV_CARDS;
	char *id, *spos;
	
	id = spos = card->shortname;	
	while (*id != '\0') {
		if (*id == ' ')
			spos = id + 1;
		id++;
	}
	id = card->id;
	while (*spos != '\0' && !isalnum(*spos))
		spos++;
	if (isdigit(*spos))
		*id++ = isalpha(card->shortname[0]) ? card->shortname[0] : 'D';
	while (*spos != '\0' && (size_t)(id - card->id) < sizeof(card->id) - 1) {
		if (isalnum(*spos))
			*id++ = *spos;
		spos++;
	}
	*id = '\0';

	id = card->id;
	
	if (*id == '\0')
		strcpy(id, "default");

	while (1) {
	      	if (loops-- == 0) {
      			snd_printk(KERN_ERR "unable to choose default card id (%s)\n", id);
      			strcpy(card->id, card->proc_root->name);
      			return;
      		}
	      	if (!snd_info_check_reserved_words(id))
      			goto __change;
		for (i = 0; i < snd_ecards_limit; i++) {
			if (snd_cards[i] && !strcmp(snd_cards[i]->id, id))
				goto __change;
		}
		break;

	      __change:
		len = strlen(id);
		if (idx_flag) {
			if (id[len-1] != '9')
				id[len-1]++;
			else
				id[len-1] = 'A';
		} else if ((size_t)len <= sizeof(card->id) - 3) {
			strcat(id, "_1");
			idx_flag++;
		} else {
			spos = id + len - 2;
			if ((size_t)len <= sizeof(card->id) - 2)
				spos++;
			*spos++ = '_';
			*spos++ = '1';
			*spos++ = '\0';
			idx_flag++;
		}
	}
}

/**
 *  snd_card_register - register the soundcard
 *  @card: soundcard structure
 *
 *  This function registers all the devices assigned to the soundcard.
 *  Until calling this, the ALSA control interface is blocked from the
 *  external accesses.  Thus, you should call this function at the end
 *  of the initialization of the card.
 *
 *  Returns zero otherwise a negative error code if the registrain failed.
 */
int snd_card_register(struct snd_card *card)
{
	int err;

	snd_assert(card != NULL, return -EINVAL);
	if ((err = snd_device_register_all(card)) < 0)
		return err;
	write_lock(&snd_card_rwlock);
	if (snd_cards[card->number]) {
		/* already registered */
		write_unlock(&snd_card_rwlock);
		return 0;
	}
	if (card->id[0] == '\0')
		choose_default_id(card);
	snd_cards[card->number] = card;
	write_unlock(&snd_card_rwlock);
	init_info_for_card(card);
#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
	if (snd_mixer_oss_notify_callback)
		snd_mixer_oss_notify_callback(card, SND_MIXER_OSS_NOTIFY_REGISTER);
#endif
	return 0;
}

EXPORT_SYMBOL(snd_card_register);

#ifdef CONFIG_PROC_FS
static struct snd_info_entry *snd_card_info_entry = NULL;

static void snd_card_info_read(struct snd_info_entry *entry,
			       struct snd_info_buffer *buffer)
{
	int idx, count;
	struct snd_card *card;

	for (idx = count = 0; idx < SNDRV_CARDS; idx++) {
		read_lock(&snd_card_rwlock);
		if ((card = snd_cards[idx]) != NULL) {
			count++;
			snd_iprintf(buffer, "%2i [%-15s]: %s - %s\n",
					idx,
					card->id,
					card->driver,
					card->shortname);
			snd_iprintf(buffer, "                      %s\n",
					card->longname);
		}
		read_unlock(&snd_card_rwlock);
	}
	if (!count)
		snd_iprintf(buffer, "--- no soundcards ---\n");
}

#ifdef CONFIG_SND_OSSEMUL

void snd_card_info_read_oss(struct snd_info_buffer *buffer)
{
	int idx, count;
	struct snd_card *card;

	for (idx = count = 0; idx < SNDRV_CARDS; idx++) {
		read_lock(&snd_card_rwlock);
		if ((card = snd_cards[idx]) != NULL) {
			count++;
			snd_iprintf(buffer, "%s\n", card->longname);
		}
		read_unlock(&snd_card_rwlock);
	}
	if (!count) {
		snd_iprintf(buffer, "--- no soundcards ---\n");
	}
}

#endif

#ifdef MODULE
static struct snd_info_entry *snd_card_module_info_entry;
static void snd_card_module_info_read(struct snd_info_entry *entry,
				      struct snd_info_buffer *buffer)
{
	int idx;
	struct snd_card *card;

	for (idx = 0; idx < SNDRV_CARDS; idx++) {
		read_lock(&snd_card_rwlock);
		if ((card = snd_cards[idx]) != NULL)
			snd_iprintf(buffer, "%2i %s\n",
				    idx, card->module->name);
		read_unlock(&snd_card_rwlock);
	}
}
#endif

int __init snd_card_info_init(void)
{
	struct snd_info_entry *entry;

	entry = snd_info_create_module_entry(THIS_MODULE, "cards", NULL);
	if (! entry)
		return -ENOMEM;
	entry->c.text.read = snd_card_info_read;
	if (snd_info_register(entry) < 0) {
		snd_info_free_entry(entry);
		return -ENOMEM;
	}
	snd_card_info_entry = entry;

#ifdef MODULE
	entry = snd_info_create_module_entry(THIS_MODULE, "modules", NULL);
	if (entry) {
		entry->c.text.read = snd_card_module_info_read;
		if (snd_info_register(entry) < 0)
			snd_info_free_entry(entry);
		else
			snd_card_module_info_entry = entry;
	}
#endif

	return 0;
}

int __exit snd_card_info_done(void)
{
	snd_info_unregister(snd_card_info_entry);
#ifdef MODULE
	snd_info_unregister(snd_card_module_info_entry);
#endif
	return 0;
}

#endif /* CONFIG_PROC_FS */

/**
 *  snd_component_add - add a component string
 *  @card: soundcard structure
 *  @component: the component id string
 *
 *  This function adds the component id string to the supported list.
 *  The component can be referred from the alsa-lib.
 *
 *  Returns zero otherwise a negative error code.
 */
  
int snd_component_add(struct snd_card *card, const char *component)
{
	char *ptr;
	int len = strlen(component);

	ptr = strstr(card->components, component);
	if (ptr != NULL) {
		if (ptr[len] == '\0' || ptr[len] == ' ')	/* already there */
			return 1;
	}
	if (strlen(card->components) + 1 + len + 1 > sizeof(card->components)) {
		snd_BUG();
		return -ENOMEM;
	}
	if (card->components[0] != '\0')
		strcat(card->components, " ");
	strcat(card->components, component);
	return 0;
}

EXPORT_SYMBOL(snd_component_add);

/**
 *  snd_card_file_add - add the file to the file list of the card
 *  @card: soundcard structure
 *  @file: file pointer
 *
 *  This function adds the file to the file linked-list of the card.
 *  This linked-list is used to keep tracking the connection state,
 *  and to avoid the release of busy resources by hotplug.
 *
 *  Returns zero or a negative error code.
 */
int snd_card_file_add(struct snd_card *card, struct file *file)
{
	struct snd_monitor_file *mfile;

	mfile = kmalloc(sizeof(*mfile), GFP_KERNEL);
	if (mfile == NULL)
		return -ENOMEM;
	mfile->file = file;
	mfile->next = NULL;
	spin_lock(&card->files_lock);
	if (card->shutdown) {
		spin_unlock(&card->files_lock);
		kfree(mfile);
		return -ENODEV;
	}
	mfile->next = card->files;
	card->files = mfile;
	spin_unlock(&card->files_lock);
	return 0;
}

EXPORT_SYMBOL(snd_card_file_add);

/**
 *  snd_card_file_remove - remove the file from the file list
 *  @card: soundcard structure
 *  @file: file pointer
 *
 *  This function removes the file formerly added to the card via
 *  snd_card_file_add() function.
 *  If all files are removed and the release of the card is
 *  scheduled, it will wake up the the thread to call snd_card_free()
 *  (see snd_card_free_in_thread() function).
 *
 *  Returns zero or a negative error code.
 */
int snd_card_file_remove(struct snd_card *card, struct file *file)
{
	struct snd_monitor_file *mfile, *pfile = NULL;

	spin_lock(&card->files_lock);
	mfile = card->files;
	while (mfile) {
		if (mfile->file == file) {
			if (pfile)
				pfile->next = mfile->next;
			else
				card->files = mfile->next;
			break;
		}
		pfile = mfile;
		mfile = mfile->next;
	}
	spin_unlock(&card->files_lock);
	if (card->files == NULL)
		wake_up(&card->shutdown_sleep);
	if (!mfile) {
		snd_printk(KERN_ERR "ALSA card file remove problem (%p)\n", file);
		return -ENOENT;
	}
	kfree(mfile);
	return 0;
}

EXPORT_SYMBOL(snd_card_file_remove);

#ifdef CONFIG_PM
/**
 *  snd_power_wait - wait until the power-state is changed.
 *  @card: soundcard structure
 *  @power_state: expected power state
 *
 *  Waits until the power-state is changed.
 *
 *  Note: the power lock must be active before call.
 */
int snd_power_wait(struct snd_card *card, unsigned int power_state)
{
	wait_queue_t wait;
	int result = 0;

	/* fastpath */
	if (snd_power_get_state(card) == power_state)
		return 0;
	init_waitqueue_entry(&wait, current);
	add_wait_queue(&card->power_sleep, &wait);
	while (1) {
		if (card->shutdown) {
			result = -ENODEV;
			break;
		}
		if (snd_power_get_state(card) == power_state)
			break;
		set_current_state(TASK_UNINTERRUPTIBLE);
		snd_power_unlock(card);
		schedule_timeout(30 * HZ);
		snd_power_lock(card);
	}
	remove_wait_queue(&card->power_sleep, &wait);
	return result;
}

EXPORT_SYMBOL(snd_power_wait);
#endif /* CONFIG_PM */
