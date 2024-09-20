// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Routines for driver control interface
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/threads.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/time.h>
#include <linux/mm.h>
#include <linux/math64.h>
#include <linux/sched/signal.h>
#include <sound/core.h>
#include <sound/minors.h>
#include <sound/info.h>
#include <sound/control.h>

// Max allocation size for user controls.
static int max_user_ctl_alloc_size = 8 * 1024 * 1024;
module_param_named(max_user_ctl_alloc_size, max_user_ctl_alloc_size, int, 0444);
MODULE_PARM_DESC(max_user_ctl_alloc_size, "Max allocation size for user controls");

#define MAX_CONTROL_COUNT	1028

struct snd_kctl_ioctl {
	struct list_head list;		/* list of all ioctls */
	snd_kctl_ioctl_func_t fioctl;
};

static DECLARE_RWSEM(snd_ioctl_rwsem);
static DECLARE_RWSEM(snd_ctl_layer_rwsem);
static LIST_HEAD(snd_control_ioctls);
#ifdef CONFIG_COMPAT
static LIST_HEAD(snd_control_compat_ioctls);
#endif
static struct snd_ctl_layer_ops *snd_ctl_layer;

static int snd_ctl_remove_locked(struct snd_card *card,
				 struct snd_kcontrol *kcontrol);

static int snd_ctl_open(struct inode *inode, struct file *file)
{
	struct snd_card *card;
	struct snd_ctl_file *ctl;
	int i, err;

	err = stream_open(inode, file);
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
	for (i = 0; i < SND_CTL_SUBDEV_ITEMS; i++)
		ctl->preferred_subdevice[i] = -1;
	ctl->pid = get_pid(task_pid(current));
	file->private_data = ctl;
	scoped_guard(write_lock_irqsave, &card->ctl_files_rwlock)
		list_add_tail(&ctl->list, &card->ctl_files);
	snd_card_unref(card);
	return 0;

      __error:
	module_put(card->module);
      __error2:
	snd_card_file_remove(card, file);
      __error1:
	if (card)
		snd_card_unref(card);
      	return err;
}

static void snd_ctl_empty_read_queue(struct snd_ctl_file * ctl)
{
	struct snd_kctl_event *cread;

	guard(spinlock_irqsave)(&ctl->read_lock);
	while (!list_empty(&ctl->events)) {
		cread = snd_kctl_event(ctl->events.next);
		list_del(&cread->list);
		kfree(cread);
	}
}

static int snd_ctl_release(struct inode *inode, struct file *file)
{
	struct snd_card *card;
	struct snd_ctl_file *ctl;
	struct snd_kcontrol *control;
	unsigned int idx;

	ctl = file->private_data;
	file->private_data = NULL;
	card = ctl->card;

	scoped_guard(write_lock_irqsave, &card->ctl_files_rwlock)
		list_del(&ctl->list);

	scoped_guard(rwsem_write, &card->controls_rwsem) {
		list_for_each_entry(control, &card->controls, list)
			for (idx = 0; idx < control->count; idx++)
				if (control->vd[idx].owner == ctl)
					control->vd[idx].owner = NULL;
	}

	snd_fasync_free(ctl->fasync);
	snd_ctl_empty_read_queue(ctl);
	put_pid(ctl->pid);
	kfree(ctl);
	module_put(card->module);
	snd_card_file_remove(card, file);
	return 0;
}

/**
 * snd_ctl_notify - Send notification to user-space for a control change
 * @card: the card to send notification
 * @mask: the event mask, SNDRV_CTL_EVENT_*
 * @id: the ctl element id to send notification
 *
 * This function adds an event record with the given id and mask, appends
 * to the list and wakes up the user-space for notification.  This can be
 * called in the atomic context.
 */
void snd_ctl_notify(struct snd_card *card, unsigned int mask,
		    struct snd_ctl_elem_id *id)
{
	struct snd_ctl_file *ctl;
	struct snd_kctl_event *ev;

	if (snd_BUG_ON(!card || !id))
		return;
	if (card->shutdown)
		return;

	guard(read_lock_irqsave)(&card->ctl_files_rwlock);
#if IS_ENABLED(CONFIG_SND_MIXER_OSS)
	card->mixer_oss_change_count++;
#endif
	list_for_each_entry(ctl, &card->ctl_files, list) {
		if (!ctl->subscribed)
			continue;
		scoped_guard(spinlock, &ctl->read_lock) {
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
				dev_err(card->dev, "No memory available to allocate event\n");
			}
_found:
			wake_up(&ctl->change_sleep);
		}
		snd_kill_fasync(ctl->fasync, SIGIO, POLL_IN);
	}
}
EXPORT_SYMBOL(snd_ctl_notify);

/**
 * snd_ctl_notify_one - Send notification to user-space for a control change
 * @card: the card to send notification
 * @mask: the event mask, SNDRV_CTL_EVENT_*
 * @kctl: the pointer with the control instance
 * @ioff: the additional offset to the control index
 *
 * This function calls snd_ctl_notify() and does additional jobs
 * like LED state changes.
 */
void snd_ctl_notify_one(struct snd_card *card, unsigned int mask,
			struct snd_kcontrol *kctl, unsigned int ioff)
{
	struct snd_ctl_elem_id id = kctl->id;
	struct snd_ctl_layer_ops *lops;

	id.index += ioff;
	id.numid += ioff;
	snd_ctl_notify(card, mask, &id);
	guard(rwsem_read)(&snd_ctl_layer_rwsem);
	for (lops = snd_ctl_layer; lops; lops = lops->next)
		lops->lnotify(card, mask, kctl, ioff);
}
EXPORT_SYMBOL(snd_ctl_notify_one);

/**
 * snd_ctl_new - create a new control instance with some elements
 * @kctl: the pointer to store new control instance
 * @count: the number of elements in this control
 * @access: the default access flags for elements in this control
 * @file: given when locking these elements
 *
 * Allocates a memory object for a new control instance. The instance has
 * elements as many as the given number (@count). Each element has given
 * access permissions (@access). Each element is locked when @file is given.
 *
 * Return: 0 on success, error code on failure
 */
static int snd_ctl_new(struct snd_kcontrol **kctl, unsigned int count,
		       unsigned int access, struct snd_ctl_file *file)
{
	unsigned int idx;

	if (count == 0 || count > MAX_CONTROL_COUNT)
		return -EINVAL;

	*kctl = kzalloc(struct_size(*kctl, vd, count), GFP_KERNEL);
	if (!*kctl)
		return -ENOMEM;

	for (idx = 0; idx < count; idx++) {
		(*kctl)->vd[idx].access = access;
		(*kctl)->vd[idx].owner = file;
	}
	(*kctl)->count = count;

	return 0;
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
 * Return: The pointer of the newly generated instance, or %NULL on failure.
 */
struct snd_kcontrol *snd_ctl_new1(const struct snd_kcontrol_new *ncontrol,
				  void *private_data)
{
	struct snd_kcontrol *kctl;
	unsigned int count;
	unsigned int access;
	int err;

	if (snd_BUG_ON(!ncontrol || !ncontrol->info))
		return NULL;

	count = ncontrol->count;
	if (count == 0)
		count = 1;

	access = ncontrol->access;
	if (access == 0)
		access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	access &= (SNDRV_CTL_ELEM_ACCESS_READWRITE |
		   SNDRV_CTL_ELEM_ACCESS_VOLATILE |
		   SNDRV_CTL_ELEM_ACCESS_INACTIVE |
		   SNDRV_CTL_ELEM_ACCESS_TLV_READWRITE |
		   SNDRV_CTL_ELEM_ACCESS_TLV_COMMAND |
		   SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK |
		   SNDRV_CTL_ELEM_ACCESS_LED_MASK |
		   SNDRV_CTL_ELEM_ACCESS_SKIP_CHECK);

	err = snd_ctl_new(&kctl, count, access, NULL);
	if (err < 0)
		return NULL;

	/* The 'numid' member is decided when calling snd_ctl_add(). */
	kctl->id.iface = ncontrol->iface;
	kctl->id.device = ncontrol->device;
	kctl->id.subdevice = ncontrol->subdevice;
	if (ncontrol->name) {
		strscpy(kctl->id.name, ncontrol->name, sizeof(kctl->id.name));
		if (strcmp(ncontrol->name, kctl->id.name) != 0)
			pr_warn("ALSA: Control name '%s' truncated to '%s'\n",
				ncontrol->name, kctl->id.name);
	}
	kctl->id.index = ncontrol->index;

	kctl->info = ncontrol->info;
	kctl->get = ncontrol->get;
	kctl->put = ncontrol->put;
	kctl->tlv.p = ncontrol->tlv.p;

	kctl->private_value = ncontrol->private_value;
	kctl->private_data = private_data;

	return kctl;
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

	/* Make sure that the ids assigned to the control do not wrap around */
	if (card->last_numid >= UINT_MAX - count)
		card->last_numid = 0;

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
			dev_err(card->dev, "unable to allocate new control numid\n");
			return -ENOMEM;
		}
	}
	return 0;
}

/* check whether the given id is contained in the given kctl */
static bool elem_id_matches(const struct snd_kcontrol *kctl,
			    const struct snd_ctl_elem_id *id)
{
	return kctl->id.iface == id->iface &&
		kctl->id.device == id->device &&
		kctl->id.subdevice == id->subdevice &&
		!strncmp(kctl->id.name, id->name, sizeof(kctl->id.name)) &&
		kctl->id.index <= id->index &&
		kctl->id.index + kctl->count > id->index;
}

#ifdef CONFIG_SND_CTL_FAST_LOOKUP
/* Compute a hash key for the corresponding ctl id
 * It's for the name lookup, hence the numid is excluded.
 * The hash key is bound in LONG_MAX to be used for Xarray key.
 */
#define MULTIPLIER	37
static unsigned long get_ctl_id_hash(const struct snd_ctl_elem_id *id)
{
	int i;
	unsigned long h;

	h = id->iface;
	h = MULTIPLIER * h + id->device;
	h = MULTIPLIER * h + id->subdevice;
	for (i = 0; i < SNDRV_CTL_ELEM_ID_NAME_MAXLEN && id->name[i]; i++)
		h = MULTIPLIER * h + id->name[i];
	h = MULTIPLIER * h + id->index;
	h &= LONG_MAX;
	return h;
}

/* add hash entries to numid and ctl xarray tables */
static void add_hash_entries(struct snd_card *card,
			     struct snd_kcontrol *kcontrol)
{
	struct snd_ctl_elem_id id = kcontrol->id;
	int i;

	xa_store_range(&card->ctl_numids, kcontrol->id.numid,
		       kcontrol->id.numid + kcontrol->count - 1,
		       kcontrol, GFP_KERNEL);

	for (i = 0; i < kcontrol->count; i++) {
		id.index = kcontrol->id.index + i;
		if (xa_insert(&card->ctl_hash, get_ctl_id_hash(&id),
			      kcontrol, GFP_KERNEL)) {
			/* skip hash for this entry, noting we had collision */
			card->ctl_hash_collision = true;
			dev_dbg(card->dev, "ctl_hash collision %d:%s:%d\n",
				id.iface, id.name, id.index);
		}
	}
}

/* remove hash entries that have been added */
static void remove_hash_entries(struct snd_card *card,
				struct snd_kcontrol *kcontrol)
{
	struct snd_ctl_elem_id id = kcontrol->id;
	struct snd_kcontrol *matched;
	unsigned long h;
	int i;

	for (i = 0; i < kcontrol->count; i++) {
		xa_erase(&card->ctl_numids, id.numid);
		h = get_ctl_id_hash(&id);
		matched = xa_load(&card->ctl_hash, h);
		if (matched && (matched == kcontrol ||
				elem_id_matches(matched, &id)))
			xa_erase(&card->ctl_hash, h);
		id.index++;
		id.numid++;
	}
}
#else /* CONFIG_SND_CTL_FAST_LOOKUP */
static inline void add_hash_entries(struct snd_card *card,
				    struct snd_kcontrol *kcontrol)
{
}
static inline void remove_hash_entries(struct snd_card *card,
				       struct snd_kcontrol *kcontrol)
{
}
#endif /* CONFIG_SND_CTL_FAST_LOOKUP */

enum snd_ctl_add_mode {
	CTL_ADD_EXCLUSIVE, CTL_REPLACE, CTL_ADD_ON_REPLACE,
};

/* add/replace a new kcontrol object; call with card->controls_rwsem locked */
static int __snd_ctl_add_replace(struct snd_card *card,
				 struct snd_kcontrol *kcontrol,
				 enum snd_ctl_add_mode mode)
{
	struct snd_ctl_elem_id id;
	unsigned int idx;
	struct snd_kcontrol *old;
	int err;

	lockdep_assert_held_write(&card->controls_rwsem);

	id = kcontrol->id;
	if (id.index > UINT_MAX - kcontrol->count)
		return -EINVAL;

	old = snd_ctl_find_id_locked(card, &id);
	if (!old) {
		if (mode == CTL_REPLACE)
			return -EINVAL;
	} else {
		if (mode == CTL_ADD_EXCLUSIVE) {
			dev_err(card->dev,
				"control %i:%i:%i:%s:%i is already present\n",
				id.iface, id.device, id.subdevice, id.name,
				id.index);
			return -EBUSY;
		}

		err = snd_ctl_remove_locked(card, old);
		if (err < 0)
			return err;
	}

	if (snd_ctl_find_hole(card, kcontrol->count) < 0)
		return -ENOMEM;

	list_add_tail(&kcontrol->list, &card->controls);
	card->controls_count += kcontrol->count;
	kcontrol->id.numid = card->last_numid + 1;
	card->last_numid += kcontrol->count;

	add_hash_entries(card, kcontrol);

	for (idx = 0; idx < kcontrol->count; idx++)
		snd_ctl_notify_one(card, SNDRV_CTL_EVENT_MASK_ADD, kcontrol, idx);

	return 0;
}

static int snd_ctl_add_replace(struct snd_card *card,
			       struct snd_kcontrol *kcontrol,
			       enum snd_ctl_add_mode mode)
{
	int err = -EINVAL;

	if (! kcontrol)
		return err;
	if (snd_BUG_ON(!card || !kcontrol->info))
		goto error;

	scoped_guard(rwsem_write, &card->controls_rwsem)
		err = __snd_ctl_add_replace(card, kcontrol, mode);

	if (err < 0)
		goto error;
	return 0;

 error:
	snd_ctl_free_one(kcontrol);
	return err;
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
 * It frees automatically the control which cannot be added.
 *
 * Return: Zero if successful, or a negative error code on failure.
 *
 */
int snd_ctl_add(struct snd_card *card, struct snd_kcontrol *kcontrol)
{
	return snd_ctl_add_replace(card, kcontrol, CTL_ADD_EXCLUSIVE);
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
 * It frees automatically the control which cannot be added or replaced.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_ctl_replace(struct snd_card *card, struct snd_kcontrol *kcontrol,
		    bool add_on_replace)
{
	return snd_ctl_add_replace(card, kcontrol,
				   add_on_replace ? CTL_ADD_ON_REPLACE : CTL_REPLACE);
}
EXPORT_SYMBOL(snd_ctl_replace);

static int __snd_ctl_remove(struct snd_card *card,
			    struct snd_kcontrol *kcontrol,
			    bool remove_hash)
{
	unsigned int idx;

	lockdep_assert_held_write(&card->controls_rwsem);

	if (snd_BUG_ON(!card || !kcontrol))
		return -EINVAL;
	list_del(&kcontrol->list);

	if (remove_hash)
		remove_hash_entries(card, kcontrol);

	card->controls_count -= kcontrol->count;
	for (idx = 0; idx < kcontrol->count; idx++)
		snd_ctl_notify_one(card, SNDRV_CTL_EVENT_MASK_REMOVE, kcontrol, idx);
	snd_ctl_free_one(kcontrol);
	return 0;
}

static inline int snd_ctl_remove_locked(struct snd_card *card,
					struct snd_kcontrol *kcontrol)
{
	return __snd_ctl_remove(card, kcontrol, true);
}

/**
 * snd_ctl_remove - remove the control from the card and release it
 * @card: the card instance
 * @kcontrol: the control instance to remove
 *
 * Removes the control from the card and then releases the instance.
 * You don't need to call snd_ctl_free_one().
 * Passing NULL to @kcontrol argument is allowed as noop.
 *
 * Return: 0 if successful, or a negative error code on failure.
 *
 * Note that this function takes card->controls_rwsem lock internally.
 */
int snd_ctl_remove(struct snd_card *card, struct snd_kcontrol *kcontrol)
{
	if (!kcontrol)
		return 0;
	guard(rwsem_write)(&card->controls_rwsem);
	return snd_ctl_remove_locked(card, kcontrol);
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
 * Return: 0 if successful, or a negative error code on failure.
 */
int snd_ctl_remove_id(struct snd_card *card, struct snd_ctl_elem_id *id)
{
	struct snd_kcontrol *kctl;

	guard(rwsem_write)(&card->controls_rwsem);
	kctl = snd_ctl_find_id_locked(card, id);
	if (kctl == NULL)
		return -ENOENT;
	return snd_ctl_remove_locked(card, kctl);
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
 * Return: 0 if successful, or a negative error code on failure.
 */
static int snd_ctl_remove_user_ctl(struct snd_ctl_file * file,
				   struct snd_ctl_elem_id *id)
{
	struct snd_card *card = file->card;
	struct snd_kcontrol *kctl;
	int idx;

	guard(rwsem_write)(&card->controls_rwsem);
	kctl = snd_ctl_find_id_locked(card, id);
	if (kctl == NULL)
		return -ENOENT;
	if (!(kctl->vd[0].access & SNDRV_CTL_ELEM_ACCESS_USER))
		return -EINVAL;
	for (idx = 0; idx < kctl->count; idx++)
		if (kctl->vd[idx].owner != NULL && kctl->vd[idx].owner != file)
			return -EBUSY;
	return snd_ctl_remove_locked(card, kctl);
}

/**
 * snd_ctl_activate_id - activate/inactivate the control of the given id
 * @card: the card instance
 * @id: the control id to activate/inactivate
 * @active: non-zero to activate
 *
 * Finds the control instance with the given id, and activate or
 * inactivate the control together with notification, if changed.
 * The given ID data is filled with full information.
 *
 * Return: 0 if unchanged, 1 if changed, or a negative error code on failure.
 */
int snd_ctl_activate_id(struct snd_card *card, struct snd_ctl_elem_id *id,
			int active)
{
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;
	unsigned int index_offset;
	int ret;

	down_write(&card->controls_rwsem);
	kctl = snd_ctl_find_id_locked(card, id);
	if (kctl == NULL) {
		ret = -ENOENT;
		goto unlock;
	}
	index_offset = snd_ctl_get_ioff(kctl, id);
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
	snd_ctl_build_ioff(id, kctl, index_offset);
	downgrade_write(&card->controls_rwsem);
	snd_ctl_notify_one(card, SNDRV_CTL_EVENT_MASK_INFO, kctl, index_offset);
	up_read(&card->controls_rwsem);
	return 1;

 unlock:
	up_write(&card->controls_rwsem);
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
 * The function tries to keep the already assigned numid while replacing
 * the rest.
 *
 * Note that this function should be used only in the card initialization
 * phase.  Calling after the card instantiation may cause issues with
 * user-space expecting persistent numids.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_ctl_rename_id(struct snd_card *card, struct snd_ctl_elem_id *src_id,
		      struct snd_ctl_elem_id *dst_id)
{
	struct snd_kcontrol *kctl;
	int saved_numid;

	guard(rwsem_write)(&card->controls_rwsem);
	kctl = snd_ctl_find_id_locked(card, src_id);
	if (kctl == NULL)
		return -ENOENT;
	saved_numid = kctl->id.numid;
	remove_hash_entries(card, kctl);
	kctl->id = *dst_id;
	kctl->id.numid = saved_numid;
	add_hash_entries(card, kctl);
	return 0;
}
EXPORT_SYMBOL(snd_ctl_rename_id);

/**
 * snd_ctl_rename - rename the control on the card
 * @card: the card instance
 * @kctl: the control to rename
 * @name: the new name
 *
 * Renames the specified control on the card to the new name.
 *
 * Note that this function takes card->controls_rwsem lock internally.
 */
void snd_ctl_rename(struct snd_card *card, struct snd_kcontrol *kctl,
		    const char *name)
{
	guard(rwsem_write)(&card->controls_rwsem);
	remove_hash_entries(card, kctl);

	if (strscpy(kctl->id.name, name, sizeof(kctl->id.name)) < 0)
		pr_warn("ALSA: Renamed control new name '%s' truncated to '%s'\n",
			name, kctl->id.name);

	add_hash_entries(card, kctl);
}
EXPORT_SYMBOL(snd_ctl_rename);

#ifndef CONFIG_SND_CTL_FAST_LOOKUP
static struct snd_kcontrol *
snd_ctl_find_numid_slow(struct snd_card *card, unsigned int numid)
{
	struct snd_kcontrol *kctl;

	list_for_each_entry(kctl, &card->controls, list) {
		if (kctl->id.numid <= numid && kctl->id.numid + kctl->count > numid)
			return kctl;
	}
	return NULL;
}
#endif /* !CONFIG_SND_CTL_FAST_LOOKUP */

/**
 * snd_ctl_find_numid_locked - find the control instance with the given number-id
 * @card: the card instance
 * @numid: the number-id to search
 *
 * Finds the control instance with the given number-id from the card.
 *
 * The caller must down card->controls_rwsem before calling this function
 * (if the race condition can happen).
 *
 * Return: The pointer of the instance if found, or %NULL if not.
 */
struct snd_kcontrol *
snd_ctl_find_numid_locked(struct snd_card *card, unsigned int numid)
{
	if (snd_BUG_ON(!card || !numid))
		return NULL;
	lockdep_assert_held(&card->controls_rwsem);
#ifdef CONFIG_SND_CTL_FAST_LOOKUP
	return xa_load(&card->ctl_numids, numid);
#else
	return snd_ctl_find_numid_slow(card, numid);
#endif
}
EXPORT_SYMBOL(snd_ctl_find_numid_locked);

/**
 * snd_ctl_find_numid - find the control instance with the given number-id
 * @card: the card instance
 * @numid: the number-id to search
 *
 * Finds the control instance with the given number-id from the card.
 *
 * Return: The pointer of the instance if found, or %NULL if not.
 *
 * Note that this function takes card->controls_rwsem lock internally.
 */
struct snd_kcontrol *snd_ctl_find_numid(struct snd_card *card,
					unsigned int numid)
{
	guard(rwsem_read)(&card->controls_rwsem);
	return snd_ctl_find_numid_locked(card, numid);
}
EXPORT_SYMBOL(snd_ctl_find_numid);

/**
 * snd_ctl_find_id_locked - find the control instance with the given id
 * @card: the card instance
 * @id: the id to search
 *
 * Finds the control instance with the given id from the card.
 *
 * The caller must down card->controls_rwsem before calling this function
 * (if the race condition can happen).
 *
 * Return: The pointer of the instance if found, or %NULL if not.
 */
struct snd_kcontrol *snd_ctl_find_id_locked(struct snd_card *card,
					    const struct snd_ctl_elem_id *id)
{
	struct snd_kcontrol *kctl;

	if (snd_BUG_ON(!card || !id))
		return NULL;
	lockdep_assert_held(&card->controls_rwsem);
	if (id->numid != 0)
		return snd_ctl_find_numid_locked(card, id->numid);
#ifdef CONFIG_SND_CTL_FAST_LOOKUP
	kctl = xa_load(&card->ctl_hash, get_ctl_id_hash(id));
	if (kctl && elem_id_matches(kctl, id))
		return kctl;
	if (!card->ctl_hash_collision)
		return NULL; /* we can rely on only hash table */
#endif
	/* no matching in hash table - try all as the last resort */
	list_for_each_entry(kctl, &card->controls, list)
		if (elem_id_matches(kctl, id))
			return kctl;

	return NULL;
}
EXPORT_SYMBOL(snd_ctl_find_id_locked);

/**
 * snd_ctl_find_id - find the control instance with the given id
 * @card: the card instance
 * @id: the id to search
 *
 * Finds the control instance with the given id from the card.
 *
 * Return: The pointer of the instance if found, or %NULL if not.
 *
 * Note that this function takes card->controls_rwsem lock internally.
 */
struct snd_kcontrol *snd_ctl_find_id(struct snd_card *card,
				     const struct snd_ctl_elem_id *id)
{
	guard(rwsem_read)(&card->controls_rwsem);
	return snd_ctl_find_id_locked(card, id);
}
EXPORT_SYMBOL(snd_ctl_find_id);

static int snd_ctl_card_info(struct snd_card *card, struct snd_ctl_file * ctl,
			     unsigned int cmd, void __user *arg)
{
	struct snd_ctl_card_info *info __free(kfree) = NULL;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (! info)
		return -ENOMEM;
	scoped_guard(rwsem_read, &snd_ioctl_rwsem) {
		info->card = card->number;
		strscpy(info->id, card->id, sizeof(info->id));
		strscpy(info->driver, card->driver, sizeof(info->driver));
		strscpy(info->name, card->shortname, sizeof(info->name));
		strscpy(info->longname, card->longname, sizeof(info->longname));
		strscpy(info->mixername, card->mixername, sizeof(info->mixername));
		strscpy(info->components, card->components, sizeof(info->components));
	}
	if (copy_to_user(arg, info, sizeof(struct snd_ctl_card_info)))
		return -EFAULT;
	return 0;
}

static int snd_ctl_elem_list(struct snd_card *card,
			     struct snd_ctl_elem_list *list)
{
	struct snd_kcontrol *kctl;
	struct snd_ctl_elem_id id;
	unsigned int offset, space, jidx;

	offset = list->offset;
	space = list->space;

	guard(rwsem_read)(&card->controls_rwsem);
	list->count = card->controls_count;
	list->used = 0;
	if (!space)
		return 0;
	list_for_each_entry(kctl, &card->controls, list) {
		if (offset >= kctl->count) {
			offset -= kctl->count;
			continue;
		}
		for (jidx = offset; jidx < kctl->count; jidx++) {
			snd_ctl_build_ioff(&id, kctl, jidx);
			if (copy_to_user(list->pids + list->used, &id, sizeof(id)))
				return -EFAULT;
			list->used++;
			if (!--space)
				return 0;
		}
		offset = 0;
	}
	return 0;
}

static int snd_ctl_elem_list_user(struct snd_card *card,
				  struct snd_ctl_elem_list __user *_list)
{
	struct snd_ctl_elem_list list;
	int err;

	if (copy_from_user(&list, _list, sizeof(list)))
		return -EFAULT;
	err = snd_ctl_elem_list(card, &list);
	if (err)
		return err;
	if (copy_to_user(_list, &list, sizeof(list)))
		return -EFAULT;

	return 0;
}

/* Check whether the given kctl info is valid */
static int snd_ctl_check_elem_info(struct snd_card *card,
				   const struct snd_ctl_elem_info *info)
{
	static const unsigned int max_value_counts[] = {
		[SNDRV_CTL_ELEM_TYPE_BOOLEAN]	= 128,
		[SNDRV_CTL_ELEM_TYPE_INTEGER]	= 128,
		[SNDRV_CTL_ELEM_TYPE_ENUMERATED] = 128,
		[SNDRV_CTL_ELEM_TYPE_BYTES]	= 512,
		[SNDRV_CTL_ELEM_TYPE_IEC958]	= 1,
		[SNDRV_CTL_ELEM_TYPE_INTEGER64] = 64,
	};

	if (info->type < SNDRV_CTL_ELEM_TYPE_BOOLEAN ||
	    info->type > SNDRV_CTL_ELEM_TYPE_INTEGER64) {
		if (card)
			dev_err(card->dev,
				"control %i:%i:%i:%s:%i: invalid type %d\n",
				info->id.iface, info->id.device,
				info->id.subdevice, info->id.name,
				info->id.index, info->type);
		return -EINVAL;
	}
	if (info->type == SNDRV_CTL_ELEM_TYPE_ENUMERATED &&
	    info->value.enumerated.items == 0) {
		if (card)
			dev_err(card->dev,
				"control %i:%i:%i:%s:%i: zero enum items\n",
				info->id.iface, info->id.device,
				info->id.subdevice, info->id.name,
				info->id.index);
		return -EINVAL;
	}
	if (info->count > max_value_counts[info->type]) {
		if (card)
			dev_err(card->dev,
				"control %i:%i:%i:%s:%i: invalid count %d\n",
				info->id.iface, info->id.device,
				info->id.subdevice, info->id.name,
				info->id.index, info->count);
		return -EINVAL;
	}

	return 0;
}

/* The capacity of struct snd_ctl_elem_value.value.*/
static const unsigned int value_sizes[] = {
	[SNDRV_CTL_ELEM_TYPE_BOOLEAN]	= sizeof(long),
	[SNDRV_CTL_ELEM_TYPE_INTEGER]	= sizeof(long),
	[SNDRV_CTL_ELEM_TYPE_ENUMERATED] = sizeof(unsigned int),
	[SNDRV_CTL_ELEM_TYPE_BYTES]	= sizeof(unsigned char),
	[SNDRV_CTL_ELEM_TYPE_IEC958]	= sizeof(struct snd_aes_iec958),
	[SNDRV_CTL_ELEM_TYPE_INTEGER64] = sizeof(long long),
};

/* fill the remaining snd_ctl_elem_value data with the given pattern */
static void fill_remaining_elem_value(struct snd_ctl_elem_value *control,
				      struct snd_ctl_elem_info *info,
				      u32 pattern)
{
	size_t offset = value_sizes[info->type] * info->count;

	offset = DIV_ROUND_UP(offset, sizeof(u32));
	memset32((u32 *)control->value.bytes.data + offset, pattern,
		 sizeof(control->value) / sizeof(u32) - offset);
}

/* check whether the given integer ctl value is valid */
static int sanity_check_int_value(struct snd_card *card,
				  const struct snd_ctl_elem_value *control,
				  const struct snd_ctl_elem_info *info,
				  int i, bool print_error)
{
	long long lval, lmin, lmax, lstep;
	u64 rem;

	switch (info->type) {
	default:
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		lval = control->value.integer.value[i];
		lmin = 0;
		lmax = 1;
		lstep = 0;
		break;
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		lval = control->value.integer.value[i];
		lmin = info->value.integer.min;
		lmax = info->value.integer.max;
		lstep = info->value.integer.step;
		break;
	case SNDRV_CTL_ELEM_TYPE_INTEGER64:
		lval = control->value.integer64.value[i];
		lmin = info->value.integer64.min;
		lmax = info->value.integer64.max;
		lstep = info->value.integer64.step;
		break;
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
		lval = control->value.enumerated.item[i];
		lmin = 0;
		lmax = info->value.enumerated.items - 1;
		lstep = 0;
		break;
	}

	if (lval < lmin || lval > lmax) {
		if (print_error)
			dev_err(card->dev,
				"control %i:%i:%i:%s:%i: value out of range %lld (%lld/%lld) at count %i\n",
				control->id.iface, control->id.device,
				control->id.subdevice, control->id.name,
				control->id.index, lval, lmin, lmax, i);
		return -EINVAL;
	}
	if (lstep) {
		div64_u64_rem(lval, lstep, &rem);
		if (rem) {
			if (print_error)
				dev_err(card->dev,
					"control %i:%i:%i:%s:%i: unaligned value %lld (step %lld) at count %i\n",
					control->id.iface, control->id.device,
					control->id.subdevice, control->id.name,
					control->id.index, lval, lstep, i);
			return -EINVAL;
		}
	}

	return 0;
}

/* check whether the all input values are valid for the given elem value */
static int sanity_check_input_values(struct snd_card *card,
				     const struct snd_ctl_elem_value *control,
				     const struct snd_ctl_elem_info *info,
				     bool print_error)
{
	int i, ret;

	switch (info->type) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
	case SNDRV_CTL_ELEM_TYPE_INTEGER64:
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
		for (i = 0; i < info->count; i++) {
			ret = sanity_check_int_value(card, control, info, i,
						     print_error);
			if (ret < 0)
				return ret;
		}
		break;
	default:
		break;
	}

	return 0;
}

/* perform sanity checks to the given snd_ctl_elem_value object */
static int sanity_check_elem_value(struct snd_card *card,
				   const struct snd_ctl_elem_value *control,
				   const struct snd_ctl_elem_info *info,
				   u32 pattern)
{
	size_t offset;
	int ret;
	u32 *p;

	ret = sanity_check_input_values(card, control, info, true);
	if (ret < 0)
		return ret;

	/* check whether the remaining area kept untouched */
	offset = value_sizes[info->type] * info->count;
	offset = DIV_ROUND_UP(offset, sizeof(u32));
	p = (u32 *)control->value.bytes.data + offset;
	for (; offset < sizeof(control->value) / sizeof(u32); offset++, p++) {
		if (*p != pattern) {
			ret = -EINVAL;
			break;
		}
		*p = 0; /* clear the checked area */
	}

	return ret;
}

static int __snd_ctl_elem_info(struct snd_card *card,
			       struct snd_kcontrol *kctl,
			       struct snd_ctl_elem_info *info,
			       struct snd_ctl_file *ctl)
{
	struct snd_kcontrol_volatile *vd;
	unsigned int index_offset;
	int result;

#ifdef CONFIG_SND_DEBUG
	info->access = 0;
#endif
	result = snd_power_ref_and_wait(card);
	if (!result)
		result = kctl->info(kctl, info);
	snd_power_unref(card);
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
		if (!snd_ctl_skip_validation(info) &&
		    snd_ctl_check_elem_info(card, info) < 0)
			result = -EINVAL;
	}
	return result;
}

static int snd_ctl_elem_info(struct snd_ctl_file *ctl,
			     struct snd_ctl_elem_info *info)
{
	struct snd_card *card = ctl->card;
	struct snd_kcontrol *kctl;

	guard(rwsem_read)(&card->controls_rwsem);
	kctl = snd_ctl_find_id_locked(card, &info->id);
	if (!kctl)
		return -ENOENT;
	return __snd_ctl_elem_info(card, kctl, info, ctl);
}

static int snd_ctl_elem_info_user(struct snd_ctl_file *ctl,
				  struct snd_ctl_elem_info __user *_info)
{
	struct snd_ctl_elem_info info;
	int result;

	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;
	result = snd_ctl_elem_info(ctl, &info);
	if (result < 0)
		return result;
	/* drop internal access flags */
	info.access &= ~(SNDRV_CTL_ELEM_ACCESS_SKIP_CHECK|
			 SNDRV_CTL_ELEM_ACCESS_LED_MASK);
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
	struct snd_ctl_elem_info info;
	const u32 pattern = 0xdeadbeef;
	int ret;

	guard(rwsem_read)(&card->controls_rwsem);
	kctl = snd_ctl_find_id_locked(card, &control->id);
	if (!kctl)
		return -ENOENT;

	index_offset = snd_ctl_get_ioff(kctl, &control->id);
	vd = &kctl->vd[index_offset];
	if (!(vd->access & SNDRV_CTL_ELEM_ACCESS_READ) || !kctl->get)
		return -EPERM;

	snd_ctl_build_ioff(&control->id, kctl, index_offset);

#ifdef CONFIG_SND_CTL_DEBUG
	/* info is needed only for validation */
	memset(&info, 0, sizeof(info));
	info.id = control->id;
	ret = __snd_ctl_elem_info(card, kctl, &info, NULL);
	if (ret < 0)
		return ret;
#endif

	if (!snd_ctl_skip_validation(&info))
		fill_remaining_elem_value(control, &info, pattern);
	ret = snd_power_ref_and_wait(card);
	if (!ret)
		ret = kctl->get(kctl, control);
	snd_power_unref(card);
	if (ret < 0)
		return ret;
	if (!snd_ctl_skip_validation(&info) &&
	    sanity_check_elem_value(card, control, &info, pattern) < 0) {
		dev_err(card->dev,
			"control %i:%i:%i:%s:%i: access overflow\n",
			control->id.iface, control->id.device,
			control->id.subdevice, control->id.name,
			control->id.index);
		return -EINVAL;
	}
	return 0;
}

static int snd_ctl_elem_read_user(struct snd_card *card,
				  struct snd_ctl_elem_value __user *_control)
{
	struct snd_ctl_elem_value *control __free(kfree) = NULL;
	int result;

	control = memdup_user(_control, sizeof(*control));
	if (IS_ERR(control))
		return PTR_ERR(no_free_ptr(control));

	result = snd_ctl_elem_read(card, control);
	if (result < 0)
		return result;

	if (copy_to_user(_control, control, sizeof(*control)))
		return -EFAULT;
	return result;
}

static int snd_ctl_elem_write(struct snd_card *card, struct snd_ctl_file *file,
			      struct snd_ctl_elem_value *control)
{
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;
	unsigned int index_offset;
	int result;

	down_write(&card->controls_rwsem);
	kctl = snd_ctl_find_id_locked(card, &control->id);
	if (kctl == NULL) {
		up_write(&card->controls_rwsem);
		return -ENOENT;
	}

	index_offset = snd_ctl_get_ioff(kctl, &control->id);
	vd = &kctl->vd[index_offset];
	if (!(vd->access & SNDRV_CTL_ELEM_ACCESS_WRITE) || kctl->put == NULL ||
	    (file && vd->owner && vd->owner != file)) {
		up_write(&card->controls_rwsem);
		return -EPERM;
	}

	snd_ctl_build_ioff(&control->id, kctl, index_offset);
	result = snd_power_ref_and_wait(card);
	/* validate input values */
	if (IS_ENABLED(CONFIG_SND_CTL_INPUT_VALIDATION) && !result) {
		struct snd_ctl_elem_info info;

		memset(&info, 0, sizeof(info));
		info.id = control->id;
		result = __snd_ctl_elem_info(card, kctl, &info, NULL);
		if (!result)
			result = sanity_check_input_values(card, control, &info,
							   false);
	}
	if (!result)
		result = kctl->put(kctl, control);
	snd_power_unref(card);
	if (result < 0) {
		up_write(&card->controls_rwsem);
		return result;
	}

	if (result > 0) {
		downgrade_write(&card->controls_rwsem);
		snd_ctl_notify_one(card, SNDRV_CTL_EVENT_MASK_VALUE, kctl, index_offset);
		up_read(&card->controls_rwsem);
	} else {
		up_write(&card->controls_rwsem);
	}

	return 0;
}

static int snd_ctl_elem_write_user(struct snd_ctl_file *file,
				   struct snd_ctl_elem_value __user *_control)
{
	struct snd_ctl_elem_value *control __free(kfree) = NULL;
	struct snd_card *card;
	int result;

	control = memdup_user(_control, sizeof(*control));
	if (IS_ERR(control))
		return PTR_ERR(no_free_ptr(control));

	card = file->card;
	result = snd_ctl_elem_write(card, file, control);
	if (result < 0)
		return result;

	if (copy_to_user(_control, control, sizeof(*control)))
		return -EFAULT;
	return result;
}

static int snd_ctl_elem_lock(struct snd_ctl_file *file,
			     struct snd_ctl_elem_id __user *_id)
{
	struct snd_card *card = file->card;
	struct snd_ctl_elem_id id;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;

	if (copy_from_user(&id, _id, sizeof(id)))
		return -EFAULT;
	guard(rwsem_write)(&card->controls_rwsem);
	kctl = snd_ctl_find_id_locked(card, &id);
	if (!kctl)
		return -ENOENT;
	vd = &kctl->vd[snd_ctl_get_ioff(kctl, &id)];
	if (vd->owner)
		return -EBUSY;
	vd->owner = file;
	return 0;
}

static int snd_ctl_elem_unlock(struct snd_ctl_file *file,
			       struct snd_ctl_elem_id __user *_id)
{
	struct snd_card *card = file->card;
	struct snd_ctl_elem_id id;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_volatile *vd;

	if (copy_from_user(&id, _id, sizeof(id)))
		return -EFAULT;
	guard(rwsem_write)(&card->controls_rwsem);
	kctl = snd_ctl_find_id_locked(card, &id);
	if (!kctl)
		return -ENOENT;
	vd = &kctl->vd[snd_ctl_get_ioff(kctl, &id)];
	if (!vd->owner)
		return -EINVAL;
	if (vd->owner != file)
		return -EPERM;
	vd->owner = NULL;
	return 0;
}

struct user_element {
	struct snd_ctl_elem_info info;
	struct snd_card *card;
	char *elem_data;		/* element data */
	unsigned long elem_data_size;	/* size of element data in bytes */
	void *tlv_data;			/* TLV data */
	unsigned long tlv_data_size;	/* TLV data size */
	void *priv_data;		/* private data (like strings for enumerated type) */
};

// check whether the addition (in bytes) of user ctl element may overflow the limit.
static bool check_user_elem_overflow(struct snd_card *card, ssize_t add)
{
	return (ssize_t)card->user_ctl_alloc_size + add > max_user_ctl_alloc_size;
}

static int snd_ctl_elem_user_info(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_info *uinfo)
{
	struct user_element *ue = kcontrol->private_data;
	unsigned int offset;

	offset = snd_ctl_get_ioff(kcontrol, &uinfo->id);
	*uinfo = ue->info;
	snd_ctl_build_ioff(&uinfo->id, kcontrol, offset);

	return 0;
}

static int snd_ctl_elem_user_enum_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	struct user_element *ue = kcontrol->private_data;
	const char *names;
	unsigned int item;
	unsigned int offset;

	item = uinfo->value.enumerated.item;

	offset = snd_ctl_get_ioff(kcontrol, &uinfo->id);
	*uinfo = ue->info;
	snd_ctl_build_ioff(&uinfo->id, kcontrol, offset);

	item = min(item, uinfo->value.enumerated.items - 1);
	uinfo->value.enumerated.item = item;

	names = ue->priv_data;
	for (; item > 0; --item)
		names += strlen(names) + 1;
	strcpy(uinfo->value.enumerated.name, names);

	return 0;
}

static int snd_ctl_elem_user_get(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	struct user_element *ue = kcontrol->private_data;
	unsigned int size = ue->elem_data_size;
	char *src = ue->elem_data +
			snd_ctl_get_ioff(kcontrol, &ucontrol->id) * size;

	memcpy(&ucontrol->value, src, size);
	return 0;
}

static int snd_ctl_elem_user_put(struct snd_kcontrol *kcontrol,
				 struct snd_ctl_elem_value *ucontrol)
{
	int err, change;
	struct user_element *ue = kcontrol->private_data;
	unsigned int size = ue->elem_data_size;
	char *dst = ue->elem_data +
			snd_ctl_get_ioff(kcontrol, &ucontrol->id) * size;

	err = sanity_check_input_values(ue->card, ucontrol, &ue->info, false);
	if (err < 0)
		return err;

	change = memcmp(&ucontrol->value, dst, size) != 0;
	if (change)
		memcpy(dst, &ucontrol->value, size);
	return change;
}

/* called in controls_rwsem write lock */
static int replace_user_tlv(struct snd_kcontrol *kctl, unsigned int __user *buf,
			    unsigned int size)
{
	struct user_element *ue = kctl->private_data;
	unsigned int *container;
	unsigned int mask = 0;
	int i;
	int change;

	lockdep_assert_held_write(&ue->card->controls_rwsem);

	if (size > 1024 * 128)	/* sane value */
		return -EINVAL;

	// does the TLV size change cause overflow?
	if (check_user_elem_overflow(ue->card, (ssize_t)(size - ue->tlv_data_size)))
		return -ENOMEM;

	container = vmemdup_user(buf, size);
	if (IS_ERR(container))
		return PTR_ERR(container);

	change = ue->tlv_data_size != size;
	if (!change)
		change = memcmp(ue->tlv_data, container, size) != 0;
	if (!change) {
		kvfree(container);
		return 0;
	}

	if (ue->tlv_data == NULL) {
		/* Now TLV data is available. */
		for (i = 0; i < kctl->count; ++i)
			kctl->vd[i].access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		mask = SNDRV_CTL_EVENT_MASK_INFO;
	} else {
		ue->card->user_ctl_alloc_size -= ue->tlv_data_size;
		ue->tlv_data_size = 0;
		kvfree(ue->tlv_data);
	}

	ue->tlv_data = container;
	ue->tlv_data_size = size;
	// decremented at private_free.
	ue->card->user_ctl_alloc_size += size;

	mask |= SNDRV_CTL_EVENT_MASK_TLV;
	for (i = 0; i < kctl->count; ++i)
		snd_ctl_notify_one(ue->card, mask, kctl, i);

	return change;
}

static int read_user_tlv(struct snd_kcontrol *kctl, unsigned int __user *buf,
			 unsigned int size)
{
	struct user_element *ue = kctl->private_data;

	if (ue->tlv_data_size == 0 || ue->tlv_data == NULL)
		return -ENXIO;

	if (size < ue->tlv_data_size)
		return -ENOSPC;

	if (copy_to_user(buf, ue->tlv_data, ue->tlv_data_size))
		return -EFAULT;

	return 0;
}

static int snd_ctl_elem_user_tlv(struct snd_kcontrol *kctl, int op_flag,
				 unsigned int size, unsigned int __user *buf)
{
	if (op_flag == SNDRV_CTL_TLV_OP_WRITE)
		return replace_user_tlv(kctl, buf, size);
	else
		return read_user_tlv(kctl, buf, size);
}

/* called in controls_rwsem write lock */
static int snd_ctl_elem_init_enum_names(struct user_element *ue)
{
	char *names, *p;
	size_t buf_len, name_len;
	unsigned int i;
	const uintptr_t user_ptrval = ue->info.value.enumerated.names_ptr;

	lockdep_assert_held_write(&ue->card->controls_rwsem);

	buf_len = ue->info.value.enumerated.names_length;
	if (buf_len > 64 * 1024)
		return -EINVAL;

	if (check_user_elem_overflow(ue->card, buf_len))
		return -ENOMEM;
	names = vmemdup_user((const void __user *)user_ptrval, buf_len);
	if (IS_ERR(names))
		return PTR_ERR(names);

	/* check that there are enough valid names */
	p = names;
	for (i = 0; i < ue->info.value.enumerated.items; ++i) {
		name_len = strnlen(p, buf_len);
		if (name_len == 0 || name_len >= 64 || name_len == buf_len) {
			kvfree(names);
			return -EINVAL;
		}
		p += name_len + 1;
		buf_len -= name_len + 1;
	}

	ue->priv_data = names;
	ue->info.value.enumerated.names_ptr = 0;
	// increment the allocation size; decremented again at private_free.
	ue->card->user_ctl_alloc_size += ue->info.value.enumerated.names_length;

	return 0;
}

static size_t compute_user_elem_size(size_t size, unsigned int count)
{
	return sizeof(struct user_element) + size * count;
}

static void snd_ctl_elem_user_free(struct snd_kcontrol *kcontrol)
{
	struct user_element *ue = kcontrol->private_data;

	// decrement the allocation size.
	ue->card->user_ctl_alloc_size -= compute_user_elem_size(ue->elem_data_size, kcontrol->count);
	ue->card->user_ctl_alloc_size -= ue->tlv_data_size;
	if (ue->priv_data)
		ue->card->user_ctl_alloc_size -= ue->info.value.enumerated.names_length;

	kvfree(ue->tlv_data);
	kvfree(ue->priv_data);
	kfree(ue);
}

static int snd_ctl_elem_add(struct snd_ctl_file *file,
			    struct snd_ctl_elem_info *info, int replace)
{
	struct snd_card *card = file->card;
	struct snd_kcontrol *kctl;
	unsigned int count;
	unsigned int access;
	long private_size;
	size_t alloc_size;
	struct user_element *ue;
	unsigned int offset;
	int err;

	if (!*info->id.name)
		return -EINVAL;
	if (strnlen(info->id.name, sizeof(info->id.name)) >= sizeof(info->id.name))
		return -EINVAL;

	/* Delete a control to replace them if needed. */
	if (replace) {
		info->id.numid = 0;
		err = snd_ctl_remove_user_ctl(file, &info->id);
		if (err)
			return err;
	}

	/* Check the number of elements for this userspace control. */
	count = info->owner;
	if (count == 0)
		count = 1;

	/* Arrange access permissions if needed. */
	access = info->access;
	if (access == 0)
		access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
	access &= (SNDRV_CTL_ELEM_ACCESS_READWRITE |
		   SNDRV_CTL_ELEM_ACCESS_INACTIVE |
		   SNDRV_CTL_ELEM_ACCESS_TLV_WRITE);

	/* In initial state, nothing is available as TLV container. */
	if (access & SNDRV_CTL_ELEM_ACCESS_TLV_WRITE)
		access |= SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK;
	access |= SNDRV_CTL_ELEM_ACCESS_USER;

	/*
	 * Check information and calculate the size of data specific to
	 * this userspace control.
	 */
	/* pass NULL to card for suppressing error messages */
	err = snd_ctl_check_elem_info(NULL, info);
	if (err < 0)
		return err;
	/* user-space control doesn't allow zero-size data */
	if (info->count < 1)
		return -EINVAL;
	private_size = value_sizes[info->type] * info->count;
	alloc_size = compute_user_elem_size(private_size, count);

	guard(rwsem_write)(&card->controls_rwsem);
	if (check_user_elem_overflow(card, alloc_size))
		return -ENOMEM;

	/*
	 * Keep memory object for this userspace control. After passing this
	 * code block, the instance should be freed by snd_ctl_free_one().
	 *
	 * Note that these elements in this control are locked.
	 */
	err = snd_ctl_new(&kctl, count, access, file);
	if (err < 0)
		return err;
	memcpy(&kctl->id, &info->id, sizeof(kctl->id));
	ue = kzalloc(alloc_size, GFP_KERNEL);
	if (!ue) {
		kfree(kctl);
		return -ENOMEM;
	}
	kctl->private_data = ue;
	kctl->private_free = snd_ctl_elem_user_free;

	// increment the allocated size; decremented again at private_free.
	card->user_ctl_alloc_size += alloc_size;

	/* Set private data for this userspace control. */
	ue->card = card;
	ue->info = *info;
	ue->info.access = 0;
	ue->elem_data = (char *)ue + sizeof(*ue);
	ue->elem_data_size = private_size;
	if (ue->info.type == SNDRV_CTL_ELEM_TYPE_ENUMERATED) {
		err = snd_ctl_elem_init_enum_names(ue);
		if (err < 0) {
			snd_ctl_free_one(kctl);
			return err;
		}
	}

	/* Set callback functions. */
	if (info->type == SNDRV_CTL_ELEM_TYPE_ENUMERATED)
		kctl->info = snd_ctl_elem_user_enum_info;
	else
		kctl->info = snd_ctl_elem_user_info;
	if (access & SNDRV_CTL_ELEM_ACCESS_READ)
		kctl->get = snd_ctl_elem_user_get;
	if (access & SNDRV_CTL_ELEM_ACCESS_WRITE)
		kctl->put = snd_ctl_elem_user_put;
	if (access & SNDRV_CTL_ELEM_ACCESS_TLV_WRITE)
		kctl->tlv.c = snd_ctl_elem_user_tlv;

	/* This function manage to free the instance on failure. */
	err = __snd_ctl_add_replace(card, kctl, CTL_ADD_EXCLUSIVE);
	if (err < 0) {
		snd_ctl_free_one(kctl);
		return err;
	}
	offset = snd_ctl_get_ioff(kctl, &info->id);
	snd_ctl_build_ioff(&info->id, kctl, offset);
	/*
	 * Here we cannot fill any field for the number of elements added by
	 * this operation because there're no specific fields. The usage of
	 * 'owner' field for this purpose may cause any bugs to userspace
	 * applications because the field originally means PID of a process
	 * which locks the element.
	 */
	return 0;
}

static int snd_ctl_elem_add_user(struct snd_ctl_file *file,
				 struct snd_ctl_elem_info __user *_info, int replace)
{
	struct snd_ctl_elem_info info;
	int err;

	if (copy_from_user(&info, _info, sizeof(info)))
		return -EFAULT;
	err = snd_ctl_elem_add(file, &info, replace);
	if (err < 0)
		return err;
	if (copy_to_user(_info, &info, sizeof(info))) {
		snd_ctl_remove_user_ctl(file, &info.id);
		return -EFAULT;
	}

	return 0;
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

static int call_tlv_handler(struct snd_ctl_file *file, int op_flag,
			    struct snd_kcontrol *kctl,
			    struct snd_ctl_elem_id *id,
			    unsigned int __user *buf, unsigned int size)
{
	static const struct {
		int op;
		int perm;
	} pairs[] = {
		{SNDRV_CTL_TLV_OP_READ,  SNDRV_CTL_ELEM_ACCESS_TLV_READ},
		{SNDRV_CTL_TLV_OP_WRITE, SNDRV_CTL_ELEM_ACCESS_TLV_WRITE},
		{SNDRV_CTL_TLV_OP_CMD,   SNDRV_CTL_ELEM_ACCESS_TLV_COMMAND},
	};
	struct snd_kcontrol_volatile *vd = &kctl->vd[snd_ctl_get_ioff(kctl, id)];
	int i, ret;

	/* Check support of the request for this element. */
	for (i = 0; i < ARRAY_SIZE(pairs); ++i) {
		if (op_flag == pairs[i].op && (vd->access & pairs[i].perm))
			break;
	}
	if (i == ARRAY_SIZE(pairs))
		return -ENXIO;

	if (kctl->tlv.c == NULL)
		return -ENXIO;

	/* Write and command operations are not allowed for locked element. */
	if (op_flag != SNDRV_CTL_TLV_OP_READ &&
	    vd->owner != NULL && vd->owner != file)
		return -EPERM;

	ret = snd_power_ref_and_wait(file->card);
	if (!ret)
		ret = kctl->tlv.c(kctl, op_flag, size, buf);
	snd_power_unref(file->card);
	return ret;
}

static int read_tlv_buf(struct snd_kcontrol *kctl, struct snd_ctl_elem_id *id,
			unsigned int __user *buf, unsigned int size)
{
	struct snd_kcontrol_volatile *vd = &kctl->vd[snd_ctl_get_ioff(kctl, id)];
	unsigned int len;

	if (!(vd->access & SNDRV_CTL_ELEM_ACCESS_TLV_READ))
		return -ENXIO;

	if (kctl->tlv.p == NULL)
		return -ENXIO;

	len = sizeof(unsigned int) * 2 + kctl->tlv.p[1];
	if (size < len)
		return -ENOMEM;

	if (copy_to_user(buf, kctl->tlv.p, len))
		return -EFAULT;

	return 0;
}

static int snd_ctl_tlv_ioctl(struct snd_ctl_file *file,
			     struct snd_ctl_tlv __user *buf,
                             int op_flag)
{
	struct snd_ctl_tlv header;
	unsigned int __user *container;
	unsigned int container_size;
	struct snd_kcontrol *kctl;
	struct snd_ctl_elem_id id;
	struct snd_kcontrol_volatile *vd;

	lockdep_assert_held(&file->card->controls_rwsem);

	if (copy_from_user(&header, buf, sizeof(header)))
		return -EFAULT;

	/* In design of control core, numerical ID starts at 1. */
	if (header.numid == 0)
		return -EINVAL;

	/* At least, container should include type and length fields.  */
	if (header.length < sizeof(unsigned int) * 2)
		return -EINVAL;
	container_size = header.length;
	container = buf->tlv;

	kctl = snd_ctl_find_numid_locked(file->card, header.numid);
	if (kctl == NULL)
		return -ENOENT;

	/* Calculate index of the element in this set. */
	id = kctl->id;
	snd_ctl_build_ioff(&id, kctl, header.numid - id.numid);
	vd = &kctl->vd[snd_ctl_get_ioff(kctl, &id)];

	if (vd->access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK) {
		return call_tlv_handler(file, op_flag, kctl, &id, container,
					container_size);
	} else {
		if (op_flag == SNDRV_CTL_TLV_OP_READ) {
			return read_tlv_buf(kctl, &id, container,
					    container_size);
		}
	}

	/* Not supported. */
	return -ENXIO;
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
		return snd_ctl_elem_list_user(card, argp);
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
		scoped_guard(rwsem_read, &ctl->card->controls_rwsem)
			err = snd_ctl_tlv_ioctl(ctl, argp, SNDRV_CTL_TLV_OP_READ);
		return err;
	case SNDRV_CTL_IOCTL_TLV_WRITE:
		scoped_guard(rwsem_write, &ctl->card->controls_rwsem)
			err = snd_ctl_tlv_ioctl(ctl, argp, SNDRV_CTL_TLV_OP_WRITE);
		return err;
	case SNDRV_CTL_IOCTL_TLV_COMMAND:
		scoped_guard(rwsem_write, &ctl->card->controls_rwsem)
			err = snd_ctl_tlv_ioctl(ctl, argp, SNDRV_CTL_TLV_OP_CMD);
		return err;
	case SNDRV_CTL_IOCTL_POWER:
		return -ENOPROTOOPT;
	case SNDRV_CTL_IOCTL_POWER_STATE:
		return put_user(SNDRV_CTL_POWER_D0, ip) ? -EFAULT : 0;
	}

	guard(rwsem_read)(&snd_ioctl_rwsem);
	list_for_each_entry(p, &snd_control_ioctls, list) {
		err = p->fioctl(card, ctl, cmd, arg);
		if (err != -ENOIOCTLCMD)
			return err;
	}
	dev_dbg(card->dev, "unknown ioctl = 0x%x\n", cmd);
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
			wait_queue_entry_t wait;
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
			if (ctl->card->shutdown)
				return -ENODEV;
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

static __poll_t snd_ctl_poll(struct file *file, poll_table * wait)
{
	__poll_t mask;
	struct snd_ctl_file *ctl;

	ctl = file->private_data;
	if (!ctl->subscribed)
		return 0;
	poll_wait(file, &ctl->change_sleep, wait);

	mask = 0;
	if (!list_empty(&ctl->events))
		mask |= EPOLLIN | EPOLLRDNORM;

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
	guard(rwsem_write)(&snd_ioctl_rwsem);
	list_add_tail(&pn->list, lists);
	return 0;
}

/**
 * snd_ctl_register_ioctl - register the device-specific control-ioctls
 * @fcn: ioctl callback function
 *
 * called from each device manager like pcm.c, hwdep.c, etc.
 *
 * Return: zero if successful, or a negative error code
 */
int snd_ctl_register_ioctl(snd_kctl_ioctl_func_t fcn)
{
	return _snd_ctl_register_ioctl(fcn, &snd_control_ioctls);
}
EXPORT_SYMBOL(snd_ctl_register_ioctl);

#ifdef CONFIG_COMPAT
/**
 * snd_ctl_register_ioctl_compat - register the device-specific 32bit compat
 * control-ioctls
 * @fcn: ioctl callback function
 *
 * Return: zero if successful, or a negative error code
 */
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
	guard(rwsem_write)(&snd_ioctl_rwsem);
	list_for_each_entry(p, lists, list) {
		if (p->fioctl == fcn) {
			list_del(&p->list);
			kfree(p);
			return 0;
		}
	}
	snd_BUG();
	return -EINVAL;
}

/**
 * snd_ctl_unregister_ioctl - de-register the device-specific control-ioctls
 * @fcn: ioctl callback function to unregister
 *
 * Return: zero if successful, or a negative error code
 */
int snd_ctl_unregister_ioctl(snd_kctl_ioctl_func_t fcn)
{
	return _snd_ctl_unregister_ioctl(fcn, &snd_control_ioctls);
}
EXPORT_SYMBOL(snd_ctl_unregister_ioctl);

#ifdef CONFIG_COMPAT
/**
 * snd_ctl_unregister_ioctl_compat - de-register the device-specific compat
 * 32bit control-ioctls
 * @fcn: ioctl callback function to unregister
 *
 * Return: zero if successful, or a negative error code
 */
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
	return snd_fasync_helper(fd, file, on, &ctl->fasync);
}

/* return the preferred subdevice number if already assigned;
 * otherwise return -1
 */
int snd_ctl_get_preferred_subdevice(struct snd_card *card, int type)
{
	struct snd_ctl_file *kctl;
	int subdevice = -1;

	guard(read_lock_irqsave)(&card->ctl_files_rwlock);
	list_for_each_entry(kctl, &card->ctl_files, list) {
		if (kctl->pid == task_pid(current)) {
			subdevice = kctl->preferred_subdevice[type];
			if (subdevice != -1)
				break;
		}
	}
	return subdevice;
}
EXPORT_SYMBOL_GPL(snd_ctl_get_preferred_subdevice);

/*
 * ioctl32 compat
 */
#ifdef CONFIG_COMPAT
#include "control_compat.c"
#else
#define snd_ctl_ioctl_compat	NULL
#endif

/*
 * control layers (audio LED etc.)
 */

/**
 * snd_ctl_request_layer - request to use the layer
 * @module_name: Name of the kernel module (NULL == build-in)
 *
 * Return: zero if successful, or an error code when the module cannot be loaded
 */
int snd_ctl_request_layer(const char *module_name)
{
	struct snd_ctl_layer_ops *lops;

	if (module_name == NULL)
		return 0;
	scoped_guard(rwsem_read, &snd_ctl_layer_rwsem) {
		for (lops = snd_ctl_layer; lops; lops = lops->next)
			if (strcmp(lops->module_name, module_name) == 0)
				return 0;
	}
	return request_module(module_name);
}
EXPORT_SYMBOL_GPL(snd_ctl_request_layer);

/**
 * snd_ctl_register_layer - register new control layer
 * @lops: operation structure
 *
 * The new layer can track all control elements and do additional
 * operations on top (like audio LED handling).
 */
void snd_ctl_register_layer(struct snd_ctl_layer_ops *lops)
{
	struct snd_card *card;
	int card_number;

	scoped_guard(rwsem_write, &snd_ctl_layer_rwsem) {
		lops->next = snd_ctl_layer;
		snd_ctl_layer = lops;
	}
	for (card_number = 0; card_number < SNDRV_CARDS; card_number++) {
		card = snd_card_ref(card_number);
		if (card) {
			scoped_guard(rwsem_read, &card->controls_rwsem)
				lops->lregister(card);
			snd_card_unref(card);
		}
	}
}
EXPORT_SYMBOL_GPL(snd_ctl_register_layer);

/**
 * snd_ctl_disconnect_layer - disconnect control layer
 * @lops: operation structure
 *
 * It is expected that the information about tracked cards
 * is freed before this call (the disconnect callback is
 * not called here).
 */
void snd_ctl_disconnect_layer(struct snd_ctl_layer_ops *lops)
{
	struct snd_ctl_layer_ops *lops2, *prev_lops2;

	guard(rwsem_write)(&snd_ctl_layer_rwsem);
	for (lops2 = snd_ctl_layer, prev_lops2 = NULL; lops2; lops2 = lops2->next) {
		if (lops2 == lops) {
			if (!prev_lops2)
				snd_ctl_layer = lops->next;
			else
				prev_lops2->next = lops->next;
			break;
		}
		prev_lops2 = lops2;
	}
}
EXPORT_SYMBOL_GPL(snd_ctl_disconnect_layer);

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

/* call lops under rwsems; called from snd_ctl_dev_*() below() */
#define call_snd_ctl_lops(_card, _op)				    \
	do {							    \
		struct snd_ctl_layer_ops *lops;			    \
		guard(rwsem_read)(&(_card)->controls_rwsem);	    \
		guard(rwsem_read)(&snd_ctl_layer_rwsem);	    \
		for (lops = snd_ctl_layer; lops; lops = lops->next) \
			lops->_op(_card);			    \
	} while (0)

/*
 * registration of the control device
 */
static int snd_ctl_dev_register(struct snd_device *device)
{
	struct snd_card *card = device->device_data;
	int err;

	err = snd_register_device(SNDRV_DEVICE_TYPE_CONTROL, card, -1,
				  &snd_ctl_f_ops, card, card->ctl_dev);
	if (err < 0)
		return err;
	call_snd_ctl_lops(card, lregister);
	return 0;
}

/*
 * disconnection of the control device
 */
static int snd_ctl_dev_disconnect(struct snd_device *device)
{
	struct snd_card *card = device->device_data;
	struct snd_ctl_file *ctl;

	scoped_guard(read_lock_irqsave, &card->ctl_files_rwlock) {
		list_for_each_entry(ctl, &card->ctl_files, list) {
			wake_up(&ctl->change_sleep);
			snd_kill_fasync(ctl->fasync, SIGIO, POLL_ERR);
		}
	}

	call_snd_ctl_lops(card, ldisconnect);
	return snd_unregister_device(card->ctl_dev);
}

/*
 * free all controls
 */
static int snd_ctl_dev_free(struct snd_device *device)
{
	struct snd_card *card = device->device_data;
	struct snd_kcontrol *control;

	scoped_guard(rwsem_write, &card->controls_rwsem) {
		while (!list_empty(&card->controls)) {
			control = snd_kcontrol(card->controls.next);
			__snd_ctl_remove(card, control, false);
		}

#ifdef CONFIG_SND_CTL_FAST_LOOKUP
		xa_destroy(&card->ctl_numids);
		xa_destroy(&card->ctl_hash);
#endif
	}
	put_device(card->ctl_dev);
	return 0;
}

/*
 * create control core:
 * called from init.c
 */
int snd_ctl_create(struct snd_card *card)
{
	static const struct snd_device_ops ops = {
		.dev_free = snd_ctl_dev_free,
		.dev_register =	snd_ctl_dev_register,
		.dev_disconnect = snd_ctl_dev_disconnect,
	};
	int err;

	if (snd_BUG_ON(!card))
		return -ENXIO;
	if (snd_BUG_ON(card->number < 0 || card->number >= SNDRV_CARDS))
		return -ENXIO;

	err = snd_device_alloc(&card->ctl_dev, card);
	if (err < 0)
		return err;
	dev_set_name(card->ctl_dev, "controlC%d", card->number);

	err = snd_device_new(card, SNDRV_DEV_CONTROL, card, &ops);
	if (err < 0)
		put_device(card->ctl_dev);
	return err;
}

/*
 * Frequently used control callbacks/helpers
 */

/**
 * snd_ctl_boolean_mono_info - Helper function for a standard boolean info
 * callback with a mono channel
 * @kcontrol: the kcontrol instance
 * @uinfo: info to store
 *
 * This is a function that can be used as info callback for a standard
 * boolean control with a single mono channel.
 *
 * Return: Zero (always successful)
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

/**
 * snd_ctl_boolean_stereo_info - Helper function for a standard boolean info
 * callback with stereo two channels
 * @kcontrol: the kcontrol instance
 * @uinfo: info to store
 *
 * This is a function that can be used as info callback for a standard
 * boolean control with stereo two channels.
 *
 * Return: Zero (always successful)
 */
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
 *
 * Return: Zero (always successful)
 */
int snd_ctl_enum_info(struct snd_ctl_elem_info *info, unsigned int channels,
		      unsigned int items, const char *const names[])
{
	info->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	info->count = channels;
	info->value.enumerated.items = items;
	if (!items)
		return 0;
	if (info->value.enumerated.item >= items)
		info->value.enumerated.item = items - 1;
	WARN(strlen(names[info->value.enumerated.item]) >= sizeof(info->value.enumerated.name),
	     "ALSA: too long item name '%s'\n",
	     names[info->value.enumerated.item]);
	strscpy(info->value.enumerated.name,
		names[info->value.enumerated.item],
		sizeof(info->value.enumerated.name));
	return 0;
}
EXPORT_SYMBOL(snd_ctl_enum_info);
