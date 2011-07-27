/*
 *  Routines for driver control interface
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
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

#include <linux/threads.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <sound/control.h>

/* max number of user-defined controls */
#define MAX_USER_CONTROLS	32
#define MAX_CONTROL_COUNT	1028

struct snd_kctl_ioctl {
	struct list_head list;		/* list of all ioctls */
	snd_kctl_ioctl_func_t fioctl;
};

static DECLARE_RWSEM(snd_ioctl_rwsem);
static LIST_HEAD(snd_control_ioctls);
#ifdef CONFIG_COMPAT
static LIST_HEAD(snd_control_compat_ioctls);
#endif

static int snd_ctl_open(struct inode *inode, struct file *file)
{
	unsigned long flags;
	struct snd_card *card;
	struct snd_ctl_file *ctl;
	int err;

	err = nonseekable_open(inode, file);
	if (err < 0)
		return err;

	card = snd_lookup_minor_data(iminor(inode), SNDRV_DEVICE_TYPE_CONTROL);
	if (!card) {
		err = -ENODEV;
		goto __error1;
	}
	err = snd_card_file_add(card, file);
	if (err < 0) {
		err = -ENODEV;
		goto __error1;
	}
	if (!try_module_get(card->module)) {
		err = -EFAULT;
		goto __error2;
	}
	ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);
	if (ctl == NULL) {
		err = -ENOMEM;
		goto __error;
	}
	INIT_LIST_HEAD(&ctl->events);
	init_waitqueue_head(&ctl->change_sleep);
	spin_lock_init(&ctl->read_lock);
	ctl->card = card;
	ctl->prefer_pcm_subdevice = -1;
	ctl->prefer_rawmidi_subdevice = -1;
	ctl->pid = get_pid(task_pid(current));
	file->private_data = ctl;
	write_lock_irqsave(&card->ctl_files_rwlock, flags);
	list_add_tail(&ctl->list, &card->ctl_files);
	write_unlock_irqrestore(&card->ctl_files_rwlock, flags);
	return 0;

      __error:
	module_put(card->module);
      __error2:
	snd_card_file_remove(card, file);
      __error1:
      	return err;
}

static void snd_ctl_empty_read_queue(struct snd_ctl_file * ctl)
{
	unsigned long flags;
	struct snd_kctl_event *cread;
	
	spin_lock_irqsave(&ctl->read_lock, flags);
	while (!list_empty(&ctl->events)) {
		cread = snd_kctl_event(ctl->events.next);
		list_del(&cread->list);
		kfree(cread);
	}
	spin_unlock_irqrestore(&ctl->read_lock, flags);
}

static int snd_ctl_release(struct inode *inode, struct file *file)
{
	unsigned long flags;
	struct snd_card *card;
	struct snd_ctl_file *ctl;
	struct snd_kcontrol *control;
	unsigned int idx;

	ctl = file->private_data;
	file->private_data = NULL;
	card = ctl->card;
	write_lock_irqsave(&card->ctl_files_rwlock, flags);
	list_del(&ctl->list);
	write_unlock_irqrestore(&card->ctl_files_rwlock, flags);
	down_write(&card->controls_rwsem);
	list_for_each_entry(control, &card->controls, list)
		for (idx = 0; idx < control->count; idx++)
			if (control->vd[idx].owner == ctl)
				control->vd[idx].owner = NULL;
	up_write(&card->controls_rwsem);
	snd_ctl_empty_read_queue(ctl);
	put_pid(ctl->pid);
	kfree(ctl);
	module_put(card->module);
	snd_card_file_remove(card, file);
	return 0;
}

void snd_ctl_notify(struct snd_card *card, unsigned int mask,
		    struct snd_ctl_elem_id *id)
{
	unsigned long flags;
	struct snd_ctl_file *ctl;
	struct snd_kctl_event *ev;
	
	if (snd_BUG_ON(!card || !id))
		return;
	read_lock(&card->ctl_files_rwlock);
#if defined(CONFIG_SND_MIXER_OSS) || defined(CONFIG_SND_MIXER_OSS_MODULE)
	card->mixer_oss_change_count++;
#endif
	list_for_each_entry(ctl, &card->ctl_files, list) {
		if (!ctl->subscribed)
			continue;
		spin_lock_irqsave(&ctl->read_lock, flags);
		list_for_each_entry(ev, &ctl->events, list) {
			if (ev->id.numid == id->numid) {
				ev->mask |= mask;
				goto _found;
			}
		}
		ev = kzalloc(sizeof(*ev), GFP_ATOMIC);
		if (ev) {
			ev->id = *id;
			ev->mask = mask;
			list_add_tail(&ev->list, &ctl->events);
		} else {
			snd_printk(KERN_ERR "No memory available to allocate event\n");
		}
	_found:
		wake_up(&ctl->change_sleep);
		spin_unlock_irqrestore(&ctl->read_lock, flags);
		kill_fasync(&ctl->fasync, SIGIO, POLL_IN);
	}
	read_unlock(&card->ctl_files_rwlock);
}

EXPORT_SYMBOL(snd_ctl_notify);

/**
 * snd_ctl_new - create a control instance from the template
 * @control: the control template
 * @access: the default control access
 *
 * Allocates a new struct snd_kcontrol instance and copies the given template 
 * to the new instance. It does not copy volatile data (access).
 *
 * Returns the pointer of the new instance, or NULL on failure.
 */
static struct snd_kcontrol *snd_ctl_new(struct snd_kcontrol *control,
					unsigned int access)
{
	struct snd_kcontrol *kctl;
	unsigned int idx;
	
	if (snd_BUG_ON(!control || !control->count))
		return NULL;

	if (control->count > MAX_CONTROL_COUNT)
		return NULL;

	kctl = kzalloc(sizeof(*kctl) + sizeof(struct snd_kcontrol_volatile) * control->count, GFP_KERNEL);
	if (kctl == NULL) {
		snd_printk(KERN_ERR "Cannot allocate control instance\n");
		return NULL;
	}
	*kctl = *control;
	for (idx = 0; idx < kctl->count; idx++)
		kctl->vd[idx].access = access;
	return kctl;
}

/**
 * snd_ctl_new1 - create a control instance from the template
 * @ncontrol: the initialization record
 * @private_data: the private data to set
 *
 * Allocates a new struct snd_kcontrol instance and initialize from the given 
 * template.  When the access field of ncontrol is 0, it's assumed as
 * READWRITE access. When the count field is 0, it's assumes as one.
 *
 * Returns the pointer of the newly generated instance, or NULL on failure.
 */
struct snd_kcontrol *snd_ctl_new1(const struct snd_kcontrol_new *ncontrol,
				  void *private_data)
{
	struct snd_kcontrol kctl;
	unsigned int access;
	
	if (snd_BUG_ON(!ncontrol || !ncontrol->info))
		return NULL;
	memset(&kctl, 0, sizeof(kctl));
	kctl.id.iface = ncontrol->iface;
	kctl.id.device = ncontrol->device;
	kctl.id.subdevice = ncontrol->subdevice;
	if (ncontrol->name) {
		strlcpy(kctl.id.name, ncontrol->name, sizeof(kctl.id.name));
		if (strcmp(ncontrol->name, kctl.id.name) != 0)
			snd_printk(KERN_WARNING
				   "Control name '%s' truncated to '%s'\n",
				   ncontrol->name, kctl.id.name);
	}
	kctl.id.index = ncontrol->index;
	kctl.count = ncontrol->count ? ncontrol->count : 1;
	access = ncontrol->access == 0 ? SNDRV_CTL_ELEM_ACCESS_READWRITE :
		 (ncontrol->access & (SNDRV_CTL_ELEM_ACCESS_READWRITE|
				      SNDRV_CTL_ELEM_ACCESS_INACTIVE|
				      SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE|
				      SNDRV_CTL_ELEM_ACCESS_TLV_COMMAND|
				      SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK));
	kctl.info = ncontrol->info;
	kctl.get = ncontrol->get;
	kctl.put = ncontrol->put;
	kctl.tlv.p = ncontrol->tlv.p;
	kctl.private_value = ncontrol->private_value;
	kctl.private_data = private_data;
	return snd_ctl_new(&kctl, access);
}

EXPORT_SYMBOL(snd_ctl_new1);

/**
 * snd_ctl_free_one - release the control instance
 * @kcontrol: the control instance
 *
 * Releases the control instance created via snd_ctl_new()
 * or snd_ctl_new1().
 * Don't call this after the control was added to the card.
 */
void snd_ctl_free_one(struct snd_kcontrol *kcontrol)
{
	if (kcontrol) {
		if (kcontrol->private_free)
			kcontrol->private_free(kcontrol);
		kfree(kcontrol);
	}
}

EXPORT_SYMBOL(snd_ctl_free_one);

static bool snd_ctl_remove_numid_conflict(struct snd_card *card,
					  unsigned int count)
{
	struct snd_kcontrol *kctl;

	list_for_each_entry(kctl, &card->controls, list) {
		if (kctl->id.numid < card->last_numid + 1 + count &&
		    kctl->id.numid + kctl->count > card->last_numid + 1) {
		    	card->last_numid = kctl->id.numid + kctl->count - 1;
			return true;
		}
	}
	return false;
}

static int snd_ctl_find_hole(struct snd_card *card, unsigned int count)
{
	unsigned int iter = 100000;

	while (snd_ctl_remove_numid_conflict(card, count)) {
		if (--iter == 0) {
			/* this situation is very unlikely */
			snd_printk(KERN_ERR "unable to allocate new control numid\n");
			return -ENOMEM;
		}
	}
	return 0;
}

/**
 * snd_ctl_add - add the control instance to the card
 * @card: the card instance
 * @kcontrol: the control instance to add
 *
 * Adds the control instance created via snd_ctl_new() or
 * snd_ctl_new1() to the given card. Assigns also an unique
 * numid used for fast search.
 *
 * Returns zero if successful, or a negative error code on failure.
 *
 * It frees automatically the control which cannot be added.
 */
int snd_ctl_add(struct snd_card *card, struct snd_kcontrol *kcontrol)
{
	struct snd_ctl_elem_id id;
	unsigned int idx;
	int err = -EINVAL;

	if (! kcontrol)
		return err;
	if (snd_BUG_ON(!card || !kcontrol->info))
		goto error;
	id = kcontrol->id;
	down_write(&card->controls_rwsem);
	if (snd_ctl_find_id(card, &id)) {
		up_write(&card->controls_rwsem);
		snd_printd(KERN_ERR "control %i:%i:%i:%s:%i is already present\n",
					id.iface,
					id.device,
					id.subdevice,
					id.name,
					id.index);
		err = -EBUSY;
		goto error;
	}
	if (snd_ctl_find_hole(card, kcontrol->count) < 0) {
		up_write(&card->controls_rwsem);
		err = -ENOMEM;
		goto error;
	}
	list_add_tail(&kcontrol->list, &card->controls);
	card->controls_count += kcontrol->count;
	kcontrol->id.numid = card->last_numid + 1;
	card->last_numid += kcontrol->count;
	up_write(&card->controls_rwsem);
	for (idx = 0; idx < kcontrol->count; idx++, id.index++, id.numid++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_ADD, &id);
	return 0;

 error:
	snd_ctl_free_one(kcontrol);
	return err;
}

EXPORT_SYMBOL(snd_ctl_add);

/**
 * snd_ctl_replace - replace the control instance of the card
 * @card: the card instance
 * @kcontrol: the control instance to replace
 * @add_on_replace: add the control if not already added
 *
 * Replaces the given control.  If the given control does not exist
 * and the add_on_replace flag is set, the control is added.  If the
 * control exists, it is destroyed first.
 *
 * Returns zero if successful, or a negative error code on failure.
 *
 * It frees automatically the control which cannot be added or replaced.
 */
int snd_ctl_replace(struct snd_card *card, struct snd_kcontrol *kcontrol,
		    bool add_on_replace)
{
	struct snd_ctl_elem_id id;
	unsigned int idx;
	struct snd_kcontrol *old;
	int ret;

	if (!kcontrol)
		return -EINVAL;
	if (snd_BUG_ON(!card || !kcontrol->info)) {
		ret = -EINVAL;
		goto error;
	}
	id = kcontrol->id;
	down_write(&card->controls_rwsem);
	old = snd_ctl_find_id(card, &id);
	if (!old) {
		if (add_on_replace)
			goto add;
		up_write(&card->controls_rwsem);
		ret = -EINVAL;
		goto error;
	}
	ret = snd_ctl_remove(card, old);
	if (ret < 0) {
		up_write(&card->controls_rwsem);
		goto error;
	}
add:
	if (snd_ctl_find_hole(card, kcontrol->count) < 0) {
		up_write(&card->controls_rwsem);
		ret = -ENOMEM;
		goto error;
	}
	list_add_tail(&kcontrol->list, &card->controls);
	card->controls_count += kcontrol->count;
	kcontrol->id.numid = card->last_numid + 1;
	card->last_numid += kcontrol->count;
	up_write(&card->controls_rwsem);
	for (idx = 0; idx < kcontrol->count; idx++, id.index++, id.numid++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_ADD, &id);
	return 0;

error:
	snd_ctl_free_one(kcontrol);
	return ret;
}
EXPORT_SYMBOL(snd_ctl_replace);

/**
 * snd_ctl_remove - remove the control from the card and release it
 * @card: the card instance
 * @kcontrol: the control instance to remove
 *
 * Removes the control from the card and then releases the instance.
 * You don't need to call snd_ctl_free_one(). You must be in
 * the write lock - down_write(&card->controls_rwsem).
 * 
 * Returns 0 if successful, or a negative error code on failure.
 */
int snd_ctl_remove(struct snd_card *card, struct snd_kcontrol *kcontrol)
{
	struct snd_ctl_elem_id id;
	unsigned int idx;

	if (snd_BUG_ON(!card || !kcontrol))
		return -EINVAL;
	list_del(&kcontrol->list);
	card->controls_count -= kcontrol->count;
	id = kcontrol->id;
	for (idx = 0; idx < kcontrol->count; idx++, id.index++, id.numid++)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_REMOVE, &id);
	snd_ctl_free_one(kcontrol);
	return 0;
}

EXPORT_SYMBOL(snd_ctl_remove);

/**
 * snd_ctl_remove_id - remove the control of the given id and release it
 * @card: the card instance
 * @id: the control id to remove
 *
 * Finds the control instance with the given id, removes it from the
 * card list and releases it.
 * 
 * Returns 0 if successful, or a negative error code on failure.
 */
int snd_ctl_remove_id(struct snd_card *card, struct snd_ctl_elem_id *id)
{
	struct snd_kcontrol *kctl;
	int ret;

	down_write(&card->controls_rwsem);
	kctl = snd_ctl_find_id(card, id);
	if (kctl == NULL) {
		up_write(&card->controls_rwsem);
		return -ENOENT;
	}
	ret = snd_ctl_remove(card, kctl);
	up_write(&card->controls_rwsem);
	return ret;
}

EXPORT_SYMBOL(snd_ctl_remove_id);

/**
 * snd_ctl_remove_user_ctl - remove and release the unlocked user control
 * @file: active control handle
 * @id: the control id to remove
 *
 * Finds the control instance with the given id, removes it from the
 * card list and releases it.
 * 
 * Returns 0 if successful, or a negative error code on failure.
 */
static int snd_ctl_remove_user_ctl(struct snd_ctl_file * file,
				   struct snd_ctl_elem_id *id)
{
	struct snd_card *card = file->card;
	struct snd_kcontrol *kctl;
	int idx, ret;

	down_write(&card->controls_rwsem);
	kctl = snd_ctl_find_id(card, id);
	if (kctl == NULL) {
		ret = -ENOENT;
		goto error;
	}
	if (!(kctl->vd[0].access & SNDRV_CTL_ELEM_ACCESS_USER)) {
		ret = -EINVAL;
		goto error;
	}
	for (idx = 0; idx < kctl->count; idx++)
		if (kctl->vd[idx].owner != NULL && kctl->vd[idx].owner != file) {
			ret = -EBUSY;
			goto error;
		}
	ret = snd_ctl_remove(card, kctl);
	if (ret < 0)
		goto error;
	card->user_ctl_count--;
error:
	up_write(&card->controls_rwsem);
	return ret;
}

/**
 * snd_ctl_activate_id - activate/inactivate the control of the given id
 * @card: the card instance
 * @id: the control id to activate/inactivate
 * @active: non-zero to activate
 *
 * Finds the control instance with the given id, and activate or
 * inactivate the control together with notification, if changed.
 *
 * Returns 0 if unchanged, 1 if changed, or a negative error code on failure.
 */
int snd_ctl_activate_id(struct snd_card *card, struct snd_ctl_elem_id *id,
			int active)
{
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;
	unsigned int index_offset;
	int ret;

	down_write(&card->controls_rwsem);
	kctl = snd_ctl_find_id(card, id);
	if (kctl == NULL) {
		ret = -ENOENT;
		goto unlock;
	}
	index_offset = snd_ctl_get_ioff(kctl, &kctl->id);
	vd = &kctl->vd[index_offset];
	ret = 0;
	if (active) {
		if (!(vd->access & SNDRV_CTL_ELEM_ACCESS_INACTIVE))
			goto unlock;
		vd->access &= ~SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	} else {
		if (vd->access & SNDRV_CTL_ELEM_ACCESS_INACTIVE)
			goto unlock;
		vd->access |= SNDRV_CTL_ELEM_ACCESS_INACTIVE;
	}
	ret = 1;
 unlock:
	up_write(&card->controls_rwsem);
	if (ret > 0)
		snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_INFO, id);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_ctl_activate_id);

/**
 * snd_ctl_rename_id - replace the id of a control on the card
 * @card: the card instance
 * @src_id: the old id
 * @dst_id: the new id
 *
 * Finds the control with the old id from the card, and replaces the
 * id with the new one.
 *
 * Returns zero if successful, or a negative error code on failure.
 */
int snd_ctl_rename_id(struct snd_card *card, struct snd_ctl_elem_id *src_id,
		      struct snd_ctl_elem_id *dst_id)
{
	struct snd_kcontrol *kctl;

	down_write(&card->controls_rwsem);
	kctl = snd_ctl_find_id(card, src_id);
	if (kctl == NULL) {
		up_write(&card->controls_rwsem);
		return -ENOENT;
	}
	kctl->id = *dst_id;
	kctl->id.numid = card->last_numid + 1;
	card->last_numid += kctl->count;
	up_write(&card->controls_rwsem);
	return 0;
}

EXPORT_SYMBOL(snd_ctl_rename_id);

/**
 * snd_ctl_find_numid - find the control instance with the given number-id
 * @card: the card instance
 * @numid: the number-id to search
 *
 * Finds the control instance with the given number-id from the card.
 *
 * Returns the pointer of the instance if found, or NULL if not.
 *
 * The caller must down card->controls_rwsem before calling this function
 * (if the race condition can happen).
 */
struct snd_kcontrol *snd_ctl_find_numid(struct snd_card *card, unsigned int numid)
{
	struct snd_kcontrol *kctl;

	if (snd_BUG_ON(!card || !numid))
		return NULL;
	list_for_each_entry(kctl, &card->controls, list) {
		if (kctl->id.numid <= numid && kctl->id.numid + kctl->count > numid)
			return kctl;
	}
	return NULL;
}

EXPORT_SYMBOL(snd_ctl_find_numid);

/**
 * snd_ctl_find_id - find the control instance with the given id
 * @card: the card instance
 * @id: the id to search
 *
 * Finds the control instance with the given id from the card.
 *
 * Returns the pointer of the instance if found, or NULL if not.
 *
 * The caller must down card->controls_rwsem before calling this function
 * (if the race condition can happen).
 */
struct snd_kcontrol *snd_ctl_find_id(struct snd_card *card,
				     struct snd_ctl_elem_id *id)
{
	struct snd_kcontrol *kctl;

	if (snd_BUG_ON(!card || !id))
		return NULL;
	if (id->numid != 0)
		return snd_ctl_find_numid(card, id->numid);
	list_for_each_entry(kctl, &card->controls, list) {
		if (kctl->id.iface != id->iface)
			continue;
		if (kctl->id.device != id->device)
			continue;
		if (kctl->id.subdevice != id->subdevice)
			continue;
		if (strncmp(kctl->id.name, id->name, sizeof(kctl->id.name)))
			continue;
		if (kctl->id.index > id->index)
			continue;
		if (kctl->id.index + kctl->count <= id->index)
			continue;
		return kctl;
	}
	return NULL;
}

EXPORT_SYMBOL(snd_ctl_find_id);

static int snd_ctl_card_info(struct snd_card *card, struct snd_ctl_file * ctl,
			     unsigned int cmd, void __user *arg)
{
	struct snd_ctl_card_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (! info)
		return -ENOMEM;
	down_read(&snd_ioctl_rwsem);
	info->card = card->number;
	strlcpy(info->id, card->id, sizeof(info->id));
	strlcpy(info->driver, card->driver, sizeof(info->driver));
	strlcpy(info->name, card->shortname, sizeof(info->name));
	strlcpy(info->longname, card->longname, sizeof(info->longname));
	strlcpy(info->mixername, card->mixername, sizeof(info->mixername));
	strlcpy(info->components, card->components, sizeof(info->components));
	up_read(&snd_ioctl_rwsem);
	if (copy_to_user(arg, info, sizeof(struct snd_ctl_card_info))) {
		kfree(info);
		return -EFAULT;
	}
	kfree(info);
	return 0;
}

static int snd_ctl_elem_list(struct snd_card *card,
			     struct snd_ctl_elem_list __user *_list)
{
	struct list_head *plist;
	struct snd_ctl_elem_list list;
	struct snd_kcontrol *kctl;
	struct snd_ctl_elem_id *dst, *id;
	unsigned int offset, space, jidx;
	
	if (copy_from_user(&list, _list, sizeof(list)))
		return -EFAULT;
	offset = list.offset;
	space = list.space;
	/* try limit maximum space */
	if (space > 16384)
		return -ENOMEM;
	if (space > 0) {
		/* allocate temporary buffer for atomic operation */
		dst = vmalloc(space * sizeof(struct snd_ctl_elem_id));
		if (dst == NULL)
			return -ENOMEM;
		down_read(&card->controls_rwsem);
		list.count = card->controls_count;
		plist = card->controls.next;
		while (plist != &card->controls) {
			if (offset == 0)
				break;
			kctl = snd_kcontrol(plist);
			if (offset < kctl->count)
				break;
			offset -= kctl->count;
			plist = plist->next;
		}
		list.used = 0;
		id = dst;
		while (space > 0 && plist != &card->controls) {
			kctl = snd_kcontrol(plist);
			for (jidx = offset; space > 0 && jidx < kctl->count; jidx++) {
				snd_ctl_build_ioff(id, kctl, jidx);
				id++;
				space--;
				list.used++;
			}
			plist = plist->next;
			offset = 0;
		}
		up_read(&card->controls_rwsem);
		if (list.used > 0 &&
		    copy_to_user(list.pids, dst,
				 list.used * sizeof(struct snd_ctl_elem_id))) {
			vfree(dst);
			return -EFAULT;
		}
		vfree(dst);
	} else {
		down_read(&card->controls_rwsem);
		list.count = card->controls_count;
		up_read(&card->controls_rwsem);
	}
	if (copy_to_user(_list, &list, sizeof(list)))
		return -EFAULT;
	return 0;
}

static int snd_ctl_elem_info(struct snd_ctl_file *ctl,
			     struct snd_ctl_elem_info *info)
{
	struct snd_card *card = ctl->card;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;
	unsigned int index_offset;
	int result;
	
	down_read(&card->controls_rwsem);
	kctl = snd_ctl_find_id(card, &info->id);
	if (kctl == NULL) {
		up_read(&card->controls_rwsem);
		return -ENOENT;
	}
#ifdef CONFIG_SND_DEBUG
	info->access = 0;
#endif
	result = kctl->info(kctl, info);
	if (result >= 0) {
		snd_BUG_ON(info->access);
		index_offset = snd_ctl_get_ioff(kctl, &info->id);
		vd = &kctl->vd[index_offset];
		snd_ctl_build_ioff(&info->id, kctl, index_offset);
		info->access = vd->access;
		if (vd->owner) {
			info->access |= SNDRV_CTL_ELEM_ACCESS_LOCK;
			if (vd->owner == ctl)
				info->access |= SNDRV_CTL_ELEM_ACCESS_OWNER;
			info->owner = pid_vnr(vd->owner->pid);
		} else {
			info->owner = -1;
		}
	}
	up_read(&card->controls_rwsem);
	return result;
}

static int snd_ctl_elem_info_user(struct snd_ctl_file *ctl,
				  struct snd_ctl_elem_info __user *_info)
{
	struct snd_ctl_elem_info info;
	int result;

	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;
	snd_power_lock(ctl->card);
	result = snd_power_wait(ctl->card, SNDRV_CTL_POWER_D0);
	if (result >= 0)
		result = snd_ctl_elem_info(ctl, &info);
	snd_power_unlock(ctl->card);
	if (result >= 0)
		if (copy_to_user(_info, &info, sizeof(info)))
			return -EFAULT;
	return result;
}

static int snd_ctl_elem_read(struct snd_card *card,
			     struct snd_ctl_elem_value *control)
{
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;
	unsigned int index_offset;
	int result;

	down_read(&card->controls_rwsem);
	kctl = snd_ctl_find_id(card, &control->id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		index_offset = snd_ctl_get_ioff(kctl, &control->id);
		vd = &kctl->vd[index_offset];
		if ((vd->access & SNDRV_CTL_ELEM_ACCESS_READ) &&
		    kctl->get != NULL) {
			snd_ctl_build_ioff(&control->id, kctl, index_offset);
			result = kctl->get(kctl, control);
		} else
			result = -EPERM;
	}
	up_read(&card->controls_rwsem);
	return result;
}

static int snd_ctl_elem_read_user(struct snd_card *card,
				  struct snd_ctl_elem_value __user *_control)
{
	struct snd_ctl_elem_value *control;
	int result;

	control = memdup_user(_control, sizeof(*control));
	if (IS_ERR(control))
		return PTR_ERR(control);

	snd_power_lock(card);
	result = snd_power_wait(card, SNDRV_CTL_POWER_D0);
	if (result >= 0)
		result = snd_ctl_elem_read(card, control);
	snd_power_unlock(card);
	if (result >= 0)
		if (copy_to_user(_control, control, sizeof(*control)))
			result = -EFAULT;
	kfree(control);
	return result;
}

static int snd_ctl_elem_write(struct snd_card *card, struct snd_ctl_file *file,
			      struct snd_ctl_elem_value *control)
{
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;
	unsigned int index_offset;
	int result;

	down_read(&card->controls_rwsem);
	kctl = snd_ctl_find_id(card, &control->id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		index_offset = snd_ctl_get_ioff(kctl, &control->id);
		vd = &kctl->vd[index_offset];
		if (!(vd->access & SNDRV_CTL_ELEM_ACCESS_WRITE) ||
		    kctl->put == NULL ||
		    (file && vd->owner && vd->owner != file)) {
			result = -EPERM;
		} else {
			snd_ctl_build_ioff(&control->id, kctl, index_offset);
			result = kctl->put(kctl, control);
		}
		if (result > 0) {
			up_read(&card->controls_rwsem);
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &control->id);
			return 0;
		}
	}
	up_read(&card->controls_rwsem);
	return result;
}

static int snd_ctl_elem_write_user(struct snd_ctl_file *file,
				   struct snd_ctl_elem_value __user *_control)
{
	struct snd_ctl_elem_value *control;
	struct snd_card *card;
	int result;

	control = memdup_user(_control, sizeof(*control));
	if (IS_ERR(control))
		return PTR_ERR(control);

	card = file->card;
	snd_power_lock(card);
	result = snd_power_wait(card, SNDRV_CTL_POWER_D0);
	if (result >= 0)
		result = snd_ctl_elem_write(card, file, control);
	snd_power_unlock(card);
	if (result >= 0)
		if (copy_to_user(_control, control, sizeof(*control)))
			result = -EFAULT;
	kfree(control);
	return result;
}

static int snd_ctl_elem_lock(struct snd_ctl_file *file,
			     struct snd_ctl_elem_id __user *_id)
{
	struct snd_card *card = file->card;
	struct snd_ctl_elem_id id;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;
	int result;
	
	if (copy_from_user(&id, _id, sizeof(id)))
		return -EFAULT;
	down_write(&card->controls_rwsem);
	kctl = snd_ctl_find_id(card, &id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		vd = &kctl->vd[snd_ctl_get_ioff(kctl, &id)];
		if (vd->owner != NULL)
			result = -EBUSY;
		else {
			vd->owner = file;
			result = 0;
		}
	}
	up_write(&card->controls_rwsem);
	return result;
}

static int snd_ctl_elem_unlock(struct snd_ctl_file *file,
			       struct snd_ctl_elem_id __user *_id)
{
	struct snd_card *card = file->card;
	struct snd_ctl_elem_id id;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;
	int result;
	
	if (copy_from_user(&id, _id, sizeof(id)))
		return -EFAULT;
	down_write(&card->controls_rwsem);
	kctl = snd_ctl_find_id(card, &id);
	if (kctl == NULL) {
		result = -ENOENT;
	} else {
		vd = &kctl->vd[snd_ctl_get_ioff(kctl, &id)];
		if (vd->owner == NULL)
			result = -EINVAL;
		else if (vd->owner != file)
			result = -EPERM;
		else {
			vd->owner = NULL;
			result = 0;
		}
	}
	up_write(&card->controls_rwsem);
	return result;
}

struct user_element {
	struct snd_ctl_elem_info info;
	void *elem_data;		/* element data */
	unsigned long elem_data_size;	/* size of element data in bytes */
	void *tlv_data;			/* TLV data */
	unsigned long tlv_data_size;	/* TLV data size */
	void *priv_data;		/* private data (like strings for enumerated type) */
	unsigned long priv_data_size;	/* size of private data in bytes */
};

static int snd_ctl_elem_user_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct user_element *ue = kcontrol->private_data;

	*uinfo = ue->info;
	return 0;
}

static int snd_ctl_elem_user_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct user_element *ue = kcontrol->private_data;

	memcpy(&ucontrol->value, ue->elem_data, ue->elem_data_size);
	return 0;
}

static int snd_ctl_elem_user_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	int change;
	struct user_element *ue = kcontrol->private_data;
	
	change = memcmp(&ucontrol->value, ue->elem_data, ue->elem_data_size) != 0;
	if (change)
		memcpy(ue->elem_data, &ucontrol->value, ue->elem_data_size);
	return change;
}

static int snd_ctl_elem_user_tlv(struct snd_kcontrol *kcontrol,
				 int op_flag,
				 unsigned int size,
				 unsigned int __user *tlv)
{
	struct user_element *ue = kcontrol->private_data;
	int change = 0;
	void *new_data;

	if (op_flag > 0) {
		if (size > 1024 * 128)	/* sane value */
			return -EINVAL;

		new_data = memdup_user(tlv, size);
		if (IS_ERR(new_data))
			return PTR_ERR(new_data);
		change = ue->tlv_data_size != size;
		if (!change)
			change = memcmp(ue->tlv_data, new_data, size);
		kfree(ue->tlv_data);
		ue->tlv_data = new_data;
		ue->tlv_data_size = size;
	} else {
		if (! ue->tlv_data_size || ! ue->tlv_data)
			return -ENXIO;
		if (size < ue->tlv_data_size)
			return -ENOSPC;
		if (copy_to_user(tlv, ue->tlv_data, ue->tlv_data_size))
			return -EFAULT;
	}
	return change;
}

static void snd_ctl_elem_user_free(struct snd_kcontrol *kcontrol)
{
	struct user_element *ue = kcontrol->private_data;
	if (ue->tlv_data)
		kfree(ue->tlv_data);
	kfree(ue);
}

static int snd_ctl_elem_add(struct snd_ctl_file *file,
			    struct snd_ctl_elem_info *info, int replace)
{
	struct snd_card *card = file->card;
	struct snd_kcontrol kctl, *_kctl;
	unsigned int access;
	long private_size;
	struct user_element *ue;
	int idx, err;
	
	if (card->user_ctl_count >= MAX_USER_CONTROLS)
		return -ENOMEM;
	if (info->count < 1)
		return -EINVAL;
	access = info->access == 0 ? SNDRV_CTL_ELEM_ACCESS_READWRITE :
		(info->access & (SNDRV_CTL_ELEM_ACCESS_READWRITE|
				 SNDRV_CTL_ELEM_ACCESS_INACTIVE|
				 SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE));
	info->id.numid = 0;
	memset(&kctl, 0, sizeof(kctl));
	down_write(&card->controls_rwsem);
	_kctl = snd_ctl_find_id(card, &info->id);
	err = 0;
	if (_kctl) {
		if (replace)
			err = snd_ctl_remove(card, _kctl);
		else
			err = -EBUSY;
	} else {
		if (replace)
			err = -ENOENT;
	}
	up_write(&card->controls_rwsem);
	if (err < 0)
		return err;
	memcpy(&kctl.id, &info->id, sizeof(info->id));
	kctl.count = info->owner ? info->owner : 1;
	access |= SNDRV_CTL_ELEM_ACCESS_USER;
	kctl.info = snd_ctl_elem_user_info;
	if (access & SNDRV_CTL_ELEM_ACCESS_READ)
		kctl.get = snd_ctl_elem_user_get;
	if (access & SNDRV_CTL_ELEM_ACCESS_WRITE)
		kctl.put = snd_ctl_elem_user_put;
	if (access & SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE) {
		kctl.tlv.c = snd_ctl_elem_user_tlv;
		access |= SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK;
	}
	switch (info->type) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		private_size = sizeof(long);
		if (info->count > 128)
			return -EINVAL;
		break;
	case SNDRV_CTL_ELEM_TYPE_INTEGER64:
		private_size = sizeof(long long);
		if (info->count > 64)
			return -EINVAL;
		break;
	case SNDRV_CTL_ELEM_TYPE_BYTES:
		private_size = sizeof(unsigned char);
		if (info->count > 512)
			return -EINVAL;
		break;
	case SNDRV_CTL_ELEM_TYPE_IEC958:
		private_size = sizeof(struct snd_aes_iec958);
		if (info->count != 1)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}
	private_size *= info->count;
	ue = kzalloc(sizeof(struct user_element) + private_size, GFP_KERNEL);
	if (ue == NULL)
		return -ENOMEM;
	ue->info = *info;
	ue->info.access = 0;
	ue->elem_data = (char *)ue + sizeof(*ue);
	ue->elem_data_size = private_size;
	kctl.private_free = snd_ctl_elem_user_free;
	_kctl = snd_ctl_new(&kctl, access);
	if (_kctl == NULL) {
		kfree(ue);
		return -ENOMEM;
	}
	_kctl->private_data = ue;
	for (idx = 0; idx < _kctl->count; idx++)
		_kctl->vd[idx].owner = file;
	err = snd_ctl_add(card, _kctl);
	if (err < 0)
		return err;

	down_write(&card->controls_rwsem);
	card->user_ctl_count++;
	up_write(&card->controls_rwsem);

	return 0;
}

static int snd_ctl_elem_add_user(struct snd_ctl_file *file,
				 struct snd_ctl_elem_info __user *_info, int replace)
{
	struct snd_ctl_elem_info info;
	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;
	return snd_ctl_elem_add(file, &info, replace);
}

static int snd_ctl_elem_remove(struct snd_ctl_file *file,
			       struct snd_ctl_elem_id __user *_id)
{
	struct snd_ctl_elem_id id;

	if (copy_from_user(&id, _id, sizeof(id)))
		return -EFAULT;
	return snd_ctl_remove_user_ctl(file, &id);
}

static int snd_ctl_subscribe_events(struct snd_ctl_file *file, int __user *ptr)
{
	int subscribe;
	if (get_user(subscribe, ptr))
		return -EFAULT;
	if (subscribe < 0) {
		subscribe = file->subscribed;
		if (put_user(subscribe, ptr))
			return -EFAULT;
		return 0;
	}
	if (subscribe) {
		file->subscribed = 1;
		return 0;
	} else if (file->subscribed) {
		snd_ctl_empty_read_queue(file);
		file->subscribed = 0;
	}
	return 0;
}

static int snd_ctl_tlv_ioctl(struct snd_ctl_file *file,
                             struct snd_ctl_tlv __user *_tlv,
                             int op_flag)
{
	struct snd_card *card = file->card;
	struct snd_ctl_tlv tlv;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;
	unsigned int len;
	int err = 0;

	if (copy_from_user(&tlv, _tlv, sizeof(tlv)))
		return -EFAULT;
	if (tlv.length < sizeof(unsigned int) * 2)
		return -EINVAL;
	down_read(&card->controls_rwsem);
	kctl = snd_ctl_find_numid(card, tlv.numid);
	if (kctl == NULL) {
		err = -ENOENT;
		goto __kctl_end;
	}
	if (kctl->tlv.p == NULL) {
		err = -ENXIO;
		goto __kctl_end;
	}
	vd = &kctl->vd[tlv.numid - kctl->id.numid];
	if ((op_flag == 0 && (vd->access & SNDRV_CTL_ELEM_ACCESS_TLV_READ) == 0) ||
	    (op_flag > 0 && (vd->access & SNDRV_CTL_ELEM_ACCESS_TLV_WRITE) == 0) ||
	    (op_flag < 0 && (vd->access & SNDRV_CTL_ELEM_ACCESS_TLV_COMMAND) == 0)) {
	    	err = -ENXIO;
	    	goto __kctl_end;
	}
	if (vd->access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
		if (vd->owner != NULL && vd->owner != file) {
			err = -EPERM;
			goto __kctl_end;
		}
		err = kctl->tlv.c(kctl, op_flag, tlv.length, _tlv->tlv); 
		if (err > 0) {
			up_read(&card->controls_rwsem);
			snd_ctl_notify(card, SNDRV_CTL_EVENT_MASK_TLV, &kctl->id);
			return 0;
		}
	} else {
		if (op_flag) {
			err = -ENXIO;
			goto __kctl_end;
		}
		len = kctl->tlv.p[1] + 2 * sizeof(unsigned int);
		if (tlv.length < len) {
			err = -ENOMEM;
			goto __kctl_end;
		}
		if (copy_to_user(_tlv->tlv, kctl->tlv.p, len))
			err = -EFAULT;
	}
      __kctl_end:
	up_read(&card->controls_rwsem);
	return err;
}

static long snd_ctl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct snd_ctl_file *ctl;
	struct snd_card *card;
	struct snd_kctl_ioctl *p;
	void __user *argp = (void __user *)arg;
	int __user *ip = argp;
	int err;

	ctl = file->private_data;
	card = ctl->card;
	if (snd_BUG_ON(!card))
		return -ENXIO;
	switch (cmd) {
	case SNDRV_CTL_IOCTL_PVERSION:
		return put_user(SNDRV_CTL_VERSION, ip) ? -EFAULT : 0;
	case SNDRV_CTL_IOCTL_CARD_INFO:
		return snd_ctl_card_info(card, ctl, cmd, argp);
	case SNDRV_CTL_IOCTL_ELEM_LIST:
		return snd_ctl_elem_list(card, argp);
	case SNDRV_CTL_IOCTL_ELEM_INFO:
		return snd_ctl_elem_info_user(ctl, argp);
	case SNDRV_CTL_IOCTL_ELEM_READ:
		return snd_ctl_elem_read_user(card, argp);
	case SNDRV_CTL_IOCTL_ELEM_WRITE:
		return snd_ctl_elem_write_user(ctl, argp);
	case SNDRV_CTL_IOCTL_ELEM_LOCK:
		return snd_ctl_elem_lock(ctl, argp);
	case SNDRV_CTL_IOCTL_ELEM_UNLOCK:
		return snd_ctl_elem_unlock(ctl, argp);
	case SNDRV_CTL_IOCTL_ELEM_ADD:
		return snd_ctl_elem_add_user(ctl, argp, 0);
	case SNDRV_CTL_IOCTL_ELEM_REPLACE:
		return snd_ctl_elem_add_user(ctl, argp, 1);
	case SNDRV_CTL_IOCTL_ELEM_REMOVE:
		return snd_ctl_elem_remove(ctl, argp);
	case SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS:
		return snd_ctl_subscribe_events(ctl, ip);
	case SNDRV_CTL_IOCTL_TLV_READ:
		return snd_ctl_tlv_ioctl(ctl, argp, 0);
	case SNDRV_CTL_IOCTL_TLV_WRITE:
		return snd_ctl_tlv_ioctl(ctl, argp, 1);
	case SNDRV_CTL_IOCTL_TLV_COMMAND:
		return snd_ctl_tlv_ioctl(ctl, argp, -1);
	case SNDRV_CTL_IOCTL_POWER:
		return -ENOPROTOOPT;
	case SNDRV_CTL_IOCTL_POWER_STATE:
#ifdef CONFIG_PM
		return put_user(card->power_state, ip) ? -EFAULT : 0;
#else
		return put_user(SNDRV_CTL_POWER_D0, ip) ? -EFAULT : 0;
#endif
	}
	down_read(&snd_ioctl_rwsem);
	list_for_each_entry(p, &snd_control_ioctls, list) {
		err = p->fioctl(card, ctl, cmd, arg);
		if (err != -ENOIOCTLCMD) {
			up_read(&snd_ioctl_rwsem);
			return err;
		}
	}
	up_read(&snd_ioctl_rwsem);
	snd_printdd("unknown ioctl = 0x%x\n", cmd);
	return -ENOTTY;
}

static ssize_t snd_ctl_read(struct file *file, char __user *buffer,
			    size_t count, loff_t * offset)
{
	struct snd_ctl_file *ctl;
	int err = 0;
	ssize_t result = 0;

	ctl = file->private_data;
	if (snd_BUG_ON(!ctl || !ctl->card))
		return -ENXIO;
	if (!ctl->subscribed)
		return -EBADFD;
	if (count < sizeof(struct snd_ctl_event))
		return -EINVAL;
	spin_lock_irq(&ctl->read_lock);
	while (count >= sizeof(struct snd_ctl_event)) {
		struct snd_ctl_event ev;
		struct snd_kctl_event *kev;
		while (list_empty(&ctl->events)) {
			wait_queue_t wait;
			if ((file->f_flags & O_NONBLOCK) != 0 || result > 0) {
				err = -EAGAIN;
				goto __end_lock;
			}
			init_waitqueue_entry(&wait, current);
			add_wait_queue(&ctl->change_sleep, &wait);
			set_current_state(TASK_INTERRUPTIBLE);
			spin_unlock_irq(&ctl->read_lock);
			schedule();
			remove_wait_queue(&ctl->change_sleep, &wait);
			if (signal_pending(current))
				return -ERESTARTSYS;
			spin_lock_irq(&ctl->read_lock);
		}
		kev = snd_kctl_event(ctl->events.next);
		ev.type = SNDRV_CTL_EVENT_ELEM;
		ev.data.elem.mask = kev->mask;
		ev.data.elem.id = kev->id;
		list_del(&kev->list);
		spin_unlock_irq(&ctl->read_lock);
		kfree(kev);
		if (copy_to_user(buffer, &ev, sizeof(struct snd_ctl_event))) {
			err = -EFAULT;
			goto __end;
		}
		spin_lock_irq(&ctl->read_lock);
		buffer += sizeof(struct snd_ctl_event);
		count -= sizeof(struct snd_ctl_event);
		result += sizeof(struct snd_ctl_event);
	}
      __end_lock:
	spin_unlock_irq(&ctl->read_lock);
      __end:
      	return result > 0 ? result : err;
}

static unsigned int snd_ctl_poll(struct file *file, poll_table * wait)
{
	unsigned int mask;
	struct snd_ctl_file *ctl;

	ctl = file->private_data;
	if (!ctl->subscribed)
		return 0;
	poll_wait(file, &ctl->change_sleep, wait);

	mask = 0;
	if (!list_empty(&ctl->events))
		mask |= POLLIN | POLLRDNORM;

	return mask;
}

/*
 * register the device-specific control-ioctls.
 * called from each device manager like pcm.c, hwdep.c, etc.
 */
static int _snd_ctl_register_ioctl(snd_kctl_ioctl_func_t fcn, struct list_head *lists)
{
	struct snd_kctl_ioctl *pn;

	pn = kzalloc(sizeof(struct snd_kctl_ioctl), GFP_KERNEL);
	if (pn == NULL)
		return -ENOMEM;
	pn->fioctl = fcn;
	down_write(&snd_ioctl_rwsem);
	list_add_tail(&pn->list, lists);
	up_write(&snd_ioctl_rwsem);
	return 0;
}

int snd_ctl_register_ioctl(snd_kctl_ioctl_func_t fcn)
{
	return _snd_ctl_register_ioctl(fcn, &snd_control_ioctls);
}

EXPORT_SYMBOL(snd_ctl_register_ioctl);

#ifdef CONFIG_COMPAT
int snd_ctl_register_ioctl_compat(snd_kctl_ioctl_func_t fcn)
{
	return _snd_ctl_register_ioctl(fcn, &snd_control_compat_ioctls);
}

EXPORT_SYMBOL(snd_ctl_register_ioctl_compat);
#endif

/*
 * de-register the device-specific control-ioctls.
 */
static int _snd_ctl_unregister_ioctl(snd_kctl_ioctl_func_t fcn,
				     struct list_head *lists)
{
	struct snd_kctl_ioctl *p;

	if (snd_BUG_ON(!fcn))
		return -EINVAL;
	down_write(&snd_ioctl_rwsem);
	list_for_each_entry(p, lists, list) {
		if (p->fioctl == fcn) {
			list_del(&p->list);
			up_write(&snd_ioctl_rwsem);
			kfree(p);
			return 0;
		}
	}
	up_write(&snd_ioctl_rwsem);
	snd_BUG();
	return -EINVAL;
}

int snd_ctl_unregister_ioctl(snd_kctl_ioctl_func_t fcn)
{
	return _snd_ctl_unregister_ioctl(fcn, &snd_control_ioctls);
}

EXPORT_SYMBOL(snd_ctl_unregister_ioctl);

#ifdef CONFIG_COMPAT
int snd_ctl_unregister_ioctl_compat(snd_kctl_ioctl_func_t fcn)
{
	return _snd_ctl_unregister_ioctl(fcn, &snd_control_compat_ioctls);
}

EXPORT_SYMBOL(snd_ctl_unregister_ioctl_compat);
#endif

static int snd_ctl_fasync(int fd, struct file * file, int on)
{
	struct snd_ctl_file *ctl;

	ctl = file->private_data;
	return fasync_helper(fd, file, on, &ctl->fasync);
}

/*
 * ioctl32 compat
 */
#ifdef CONFIG_COMPAT
#include "control_compat.c"
#else
#define snd_ctl_ioctl_compat	NULL
#endif

/*
 *  INIT PART
 */

static const struct file_operations snd_ctl_f_ops =
{
	.owner =	THIS_MODULE,
	.read =		snd_ctl_read,
	.open =		snd_ctl_open,
	.release =	snd_ctl_release,
	.llseek =	no_llseek,
	.poll =		snd_ctl_poll,
	.unlocked_ioctl =	snd_ctl_ioctl,
	.compat_ioctl =	snd_ctl_ioctl_compat,
	.fasync =	snd_ctl_fasync,
};

/*
 * registration of the control device
 */
static int snd_ctl_dev_register(struct snd_device *device)
{
	struct snd_card *card = device->device_data;
	int err, cardnum;
	char name[16];

	if (snd_BUG_ON(!card))
		return -ENXIO;
	cardnum = card->number;
	if (snd_BUG_ON(cardnum < 0 || cardnum >= SNDRV_CARDS))
		return -ENXIO;
	sprintf(name, "controlC%i", cardnum);
	if ((err = snd_register_device(SNDRV_DEVICE_TYPE_CONTROL, card, -1,
				       &snd_ctl_f_ops, card, name)) < 0)
		return err;
	return 0;
}

/*
 * disconnection of the control device
 */
static int snd_ctl_dev_disconnect(struct snd_device *device)
{
	struct snd_card *card = device->device_data;
	struct snd_ctl_file *ctl;
	int err, cardnum;

	if (snd_BUG_ON(!card))
		return -ENXIO;
	cardnum = card->number;
	if (snd_BUG_ON(cardnum < 0 || cardnum >= SNDRV_CARDS))
		return -ENXIO;

	read_lock(&card->ctl_files_rwlock);
	list_for_each_entry(ctl, &card->ctl_files, list) {
		wake_up(&ctl->change_sleep);
		kill_fasync(&ctl->fasync, SIGIO, POLL_ERR);
	}
	read_unlock(&card->ctl_files_rwlock);

	if ((err = snd_unregister_device(SNDRV_DEVICE_TYPE_CONTROL,
					 card, -1)) < 0)
		return err;
	return 0;
}

/*
 * free all controls
 */
static int snd_ctl_dev_free(struct snd_device *device)
{
	struct snd_card *card = device->device_data;
	struct snd_kcontrol *control;

	down_write(&card->controls_rwsem);
	while (!list_empty(&card->controls)) {
		control = snd_kcontrol(card->controls.next);
		snd_ctl_remove(card, control);
	}
	up_write(&card->controls_rwsem);
	return 0;
}

/*
 * create control core:
 * called from init.c
 */
int snd_ctl_create(struct snd_card *card)
{
	static struct snd_device_ops ops = {
		.dev_free = snd_ctl_dev_free,
		.dev_register =	snd_ctl_dev_register,
		.dev_disconnect = snd_ctl_dev_disconnect,
	};

	if (snd_BUG_ON(!card))
		return -ENXIO;
	return snd_device_new(card, SNDRV_DEV_CONTROL, card, &ops);
}

/*
 * Frequently used control callbacks/helpers
 */
int snd_ctl_boolean_mono_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

EXPORT_SYMBOL(snd_ctl_boolean_mono_info);

int snd_ctl_boolean_stereo_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

EXPORT_SYMBOL(snd_ctl_boolean_stereo_info);

/**
 * snd_ctl_enum_info - fills the info structure for an enumerated control
 * @info: the structure to be filled
 * @channels: the number of the control's channels; often one
 * @items: the number of control values; also the size of @names
 * @names: an array containing the names of all control values
 *
 * Sets all required fields in @info to their appropriate values.
 * If the control's accessibility is not the default (readable and writable),
 * the caller has to fill @info->access.
 */
int snd_ctl_enum_info(struct snd_ctl_elem_info *info, unsigned int channels,
		      unsigned int items, const char *const names[])
{
	info->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	info->count = channels;
	info->value.enumerated.items = items;
	if (info->value.enumerated.item >= items)
		info->value.enumerated.item = items - 1;
	strlcpy(info->value.enumerated.name,
		names[info->value.enumerated.item],
		sizeof(info->value.enumerated.name));
	return 0;
}
EXPORT_SYMBOL(snd_ctl_enum_info);
