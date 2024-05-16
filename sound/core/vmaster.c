// SPDX-License-Identifier: GPL-2.0-only
/*
 * Virtual master and follower controls
 *
 *  Copyright (c) 2008 by Takashi Iwai <tiwai@suse.de>
 */

#include <linux/slab.h>
#include <linux/export.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>

/*
 * a subset of information returned via ctl info callback
 */
struct link_ctl_info {
	snd_ctl_elem_type_t type; /* value type */
	int count;		/* item count */
	int min_val, max_val;	/* min, max values */
};

/*
 * link master - this contains a list of follower controls that are
 * identical types, i.e. info returns the same value type and value
 * ranges, but may have different number of counts.
 *
 * The master control is so far only mono volume/switch for simplicity.
 * The same value will be applied to all followers.
 */
struct link_master {
	struct list_head followers;
	struct link_ctl_info info;
	int val;		/* the master value */
	unsigned int tlv[4];
	void (*hook)(void *private_data, int);
	void *hook_private_data;
};

/*
 * link follower - this contains a follower control element
 *
 * It fakes the control callbacks with additional attenuation by the
 * master control.  A follower may have either one or two channels.
 */

struct link_follower {
	struct list_head list;
	struct link_master *master;
	struct link_ctl_info info;
	int vals[2];		/* current values */
	unsigned int flags;
	struct snd_kcontrol *kctl; /* original kcontrol pointer */
	struct snd_kcontrol follower; /* the copy of original control entry */
};

static int follower_update(struct link_follower *follower)
{
	struct snd_ctl_elem_value *uctl;
	int err, ch;

	uctl = kzalloc(sizeof(*uctl), GFP_KERNEL);
	if (!uctl)
		return -ENOMEM;
	uctl->id = follower->follower.id;
	err = follower->follower.get(&follower->follower, uctl);
	if (err < 0)
		goto error;
	for (ch = 0; ch < follower->info.count; ch++)
		follower->vals[ch] = uctl->value.integer.value[ch];
 error:
	kfree(uctl);
	return err < 0 ? err : 0;
}

/* get the follower ctl info and save the initial values */
static int follower_init(struct link_follower *follower)
{
	struct snd_ctl_elem_info *uinfo;
	int err;

	if (follower->info.count) {
		/* already initialized */
		if (follower->flags & SND_CTL_FOLLOWER_NEED_UPDATE)
			return follower_update(follower);
		return 0;
	}

	uinfo = kmalloc(sizeof(*uinfo), GFP_KERNEL);
	if (!uinfo)
		return -ENOMEM;
	uinfo->id = follower->follower.id;
	err = follower->follower.info(&follower->follower, uinfo);
	if (err < 0) {
		kfree(uinfo);
		return err;
	}
	follower->info.type = uinfo->type;
	follower->info.count = uinfo->count;
	if (follower->info.count > 2  ||
	    (follower->info.type != SNDRV_CTL_ELEM_TYPE_INTEGER &&
	     follower->info.type != SNDRV_CTL_ELEM_TYPE_BOOLEAN)) {
		pr_err("ALSA: vmaster: invalid follower element\n");
		kfree(uinfo);
		return -EINVAL;
	}
	follower->info.min_val = uinfo->value.integer.min;
	follower->info.max_val = uinfo->value.integer.max;
	kfree(uinfo);

	return follower_update(follower);
}

/* initialize master volume */
static int master_init(struct link_master *master)
{
	struct link_follower *follower;

	if (master->info.count)
		return 0; /* already initialized */

	list_for_each_entry(follower, &master->followers, list) {
		int err = follower_init(follower);
		if (err < 0)
			return err;
		master->info = follower->info;
		master->info.count = 1; /* always mono */
		/* set full volume as default (= no attenuation) */
		master->val = master->info.max_val;
		if (master->hook)
			master->hook(master->hook_private_data, master->val);
		return 1;
	}
	return -ENOENT;
}

static int follower_get_val(struct link_follower *follower,
			    struct snd_ctl_elem_value *ucontrol)
{
	int err, ch;

	err = follower_init(follower);
	if (err < 0)
		return err;
	for (ch = 0; ch < follower->info.count; ch++)
		ucontrol->value.integer.value[ch] = follower->vals[ch];
	return 0;
}

static int follower_put_val(struct link_follower *follower,
			    struct snd_ctl_elem_value *ucontrol)
{
	int err, ch, vol;

	err = master_init(follower->master);
	if (err < 0)
		return err;

	switch (follower->info.type) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		for (ch = 0; ch < follower->info.count; ch++)
			ucontrol->value.integer.value[ch] &=
				!!follower->master->val;
		break;
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		for (ch = 0; ch < follower->info.count; ch++) {
			/* max master volume is supposed to be 0 dB */
			vol = ucontrol->value.integer.value[ch];
			vol += follower->master->val - follower->master->info.max_val;
			if (vol < follower->info.min_val)
				vol = follower->info.min_val;
			else if (vol > follower->info.max_val)
				vol = follower->info.max_val;
			ucontrol->value.integer.value[ch] = vol;
		}
		break;
	}
	return follower->follower.put(&follower->follower, ucontrol);
}

/*
 * ctl callbacks for followers
 */
static int follower_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	struct link_follower *follower = snd_kcontrol_chip(kcontrol);
	return follower->follower.info(&follower->follower, uinfo);
}

static int follower_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct link_follower *follower = snd_kcontrol_chip(kcontrol);
	return follower_get_val(follower, ucontrol);
}

static int follower_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct link_follower *follower = snd_kcontrol_chip(kcontrol);
	int err, ch, changed = 0;

	err = follower_init(follower);
	if (err < 0)
		return err;
	for (ch = 0; ch < follower->info.count; ch++) {
		if (follower->vals[ch] != ucontrol->value.integer.value[ch]) {
			changed = 1;
			follower->vals[ch] = ucontrol->value.integer.value[ch];
		}
	}
	if (!changed)
		return 0;
	err = follower_put_val(follower, ucontrol);
	if (err < 0)
		return err;
	return 1;
}

static int follower_tlv_cmd(struct snd_kcontrol *kcontrol,
			    int op_flag, unsigned int size,
			    unsigned int __user *tlv)
{
	struct link_follower *follower = snd_kcontrol_chip(kcontrol);
	/* FIXME: this assumes that the max volume is 0 dB */
	return follower->follower.tlv.c(&follower->follower, op_flag, size, tlv);
}

static void follower_free(struct snd_kcontrol *kcontrol)
{
	struct link_follower *follower = snd_kcontrol_chip(kcontrol);
	if (follower->follower.private_free)
		follower->follower.private_free(&follower->follower);
	if (follower->master)
		list_del(&follower->list);
	kfree(follower);
}

/*
 * Add a follower control to the group with the given master control
 *
 * All followers must be the same type (returning the same information
 * via info callback).  The function doesn't check it, so it's your
 * responsibility.
 *
 * Also, some additional limitations:
 * - at most two channels
 * - logarithmic volume control (dB level), no linear volume
 * - master can only attenuate the volume, no gain
 */
int _snd_ctl_add_follower(struct snd_kcontrol *master,
			  struct snd_kcontrol *follower,
			  unsigned int flags)
{
	struct link_master *master_link = snd_kcontrol_chip(master);
	struct link_follower *srec;

	srec = kzalloc(struct_size(srec, follower.vd, follower->count),
		       GFP_KERNEL);
	if (!srec)
		return -ENOMEM;
	srec->kctl = follower;
	srec->follower = *follower;
	memcpy(srec->follower.vd, follower->vd, follower->count * sizeof(*follower->vd));
	srec->master = master_link;
	srec->flags = flags;

	/* override callbacks */
	follower->info = follower_info;
	follower->get = follower_get;
	follower->put = follower_put;
	if (follower->vd[0].access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK)
		follower->tlv.c = follower_tlv_cmd;
	follower->private_data = srec;
	follower->private_free = follower_free;

	list_add_tail(&srec->list, &master_link->followers);
	return 0;
}
EXPORT_SYMBOL(_snd_ctl_add_follower);

/**
 * snd_ctl_add_followers - add multiple followers to vmaster
 * @card: card instance
 * @master: the target vmaster kcontrol object
 * @list: NULL-terminated list of name strings of followers to be added
 *
 * Adds the multiple follower kcontrols with the given names.
 * Returns 0 for success or a negative error code.
 */
int snd_ctl_add_followers(struct snd_card *card, struct snd_kcontrol *master,
			  const char * const *list)
{
	struct snd_kcontrol *follower;
	int err;

	for (; *list; list++) {
		follower = snd_ctl_find_id_mixer(card, *list);
		if (follower) {
			err = snd_ctl_add_follower(master, follower);
			if (err < 0)
				return err;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_ctl_add_followers);

/*
 * ctl callbacks for master controls
 */
static int master_info(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_info *uinfo)
{
	struct link_master *master = snd_kcontrol_chip(kcontrol);
	int ret;

	ret = master_init(master);
	if (ret < 0)
		return ret;
	uinfo->type = master->info.type;
	uinfo->count = master->info.count;
	uinfo->value.integer.min = master->info.min_val;
	uinfo->value.integer.max = master->info.max_val;
	return 0;
}

static int master_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct link_master *master = snd_kcontrol_chip(kcontrol);
	int err = master_init(master);
	if (err < 0)
		return err;
	ucontrol->value.integer.value[0] = master->val;
	return 0;
}

static int sync_followers(struct link_master *master, int old_val, int new_val)
{
	struct link_follower *follower;
	struct snd_ctl_elem_value *uval;

	uval = kmalloc(sizeof(*uval), GFP_KERNEL);
	if (!uval)
		return -ENOMEM;
	list_for_each_entry(follower, &master->followers, list) {
		master->val = old_val;
		uval->id = follower->follower.id;
		follower_get_val(follower, uval);
		master->val = new_val;
		follower_put_val(follower, uval);
	}
	kfree(uval);
	return 0;
}

static int master_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct link_master *master = snd_kcontrol_chip(kcontrol);
	int err, new_val, old_val;
	bool first_init;

	err = master_init(master);
	if (err < 0)
		return err;
	first_init = err;
	old_val = master->val;
	new_val = ucontrol->value.integer.value[0];
	if (new_val == old_val)
		return 0;

	err = sync_followers(master, old_val, new_val);
	if (err < 0)
		return err;
	if (master->hook && !first_init)
		master->hook(master->hook_private_data, master->val);
	return 1;
}

static void master_free(struct snd_kcontrol *kcontrol)
{
	struct link_master *master = snd_kcontrol_chip(kcontrol);
	struct link_follower *follower, *n;

	/* free all follower links and retore the original follower kctls */
	list_for_each_entry_safe(follower, n, &master->followers, list) {
		struct snd_kcontrol *sctl = follower->kctl;
		struct list_head olist = sctl->list;
		memcpy(sctl, &follower->follower, sizeof(*sctl));
		memcpy(sctl->vd, follower->follower.vd,
		       sctl->count * sizeof(*sctl->vd));
		sctl->list = olist; /* keep the current linked-list */
		kfree(follower);
	}
	kfree(master);
}


/**
 * snd_ctl_make_virtual_master - Create a virtual master control
 * @name: name string of the control element to create
 * @tlv: optional TLV int array for dB information
 *
 * Creates a virtual master control with the given name string.
 *
 * After creating a vmaster element, you can add the follower controls
 * via snd_ctl_add_follower() or snd_ctl_add_follower_uncached().
 *
 * The optional argument @tlv can be used to specify the TLV information
 * for dB scale of the master control.  It should be a single element
 * with #SNDRV_CTL_TLVT_DB_SCALE, #SNDRV_CTL_TLV_DB_MINMAX or
 * #SNDRV_CTL_TLVT_DB_MINMAX_MUTE type, and should be the max 0dB.
 *
 * Return: The created control element, or %NULL for errors (ENOMEM).
 */
struct snd_kcontrol *snd_ctl_make_virtual_master(char *name,
						 const unsigned int *tlv)
{
	struct link_master *master;
	struct snd_kcontrol *kctl;
	struct snd_kcontrol_new knew;

	memset(&knew, 0, sizeof(knew));
	knew.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	knew.name = name;
	knew.info = master_info;

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return NULL;
	INIT_LIST_HEAD(&master->followers);

	kctl = snd_ctl_new1(&knew, master);
	if (!kctl) {
		kfree(master);
		return NULL;
	}
	/* override some callbacks */
	kctl->info = master_info;
	kctl->get = master_get;
	kctl->put = master_put;
	kctl->private_free = master_free;

	/* additional (constant) TLV read */
	if (tlv) {
		unsigned int type = tlv[SNDRV_CTL_TLVO_TYPE];
		if (type == SNDRV_CTL_TLVT_DB_SCALE ||
		    type == SNDRV_CTL_TLVT_DB_MINMAX ||
		    type == SNDRV_CTL_TLVT_DB_MINMAX_MUTE) {
			kctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;
			memcpy(master->tlv, tlv, sizeof(master->tlv));
			kctl->tlv.p = master->tlv;
		}
	}

	return kctl;
}
EXPORT_SYMBOL(snd_ctl_make_virtual_master);

/**
 * snd_ctl_add_vmaster_hook - Add a hook to a vmaster control
 * @kcontrol: vmaster kctl element
 * @hook: the hook function
 * @private_data: the private_data pointer to be saved
 *
 * Adds the given hook to the vmaster control element so that it's called
 * at each time when the value is changed.
 *
 * Return: Zero.
 */
int snd_ctl_add_vmaster_hook(struct snd_kcontrol *kcontrol,
			     void (*hook)(void *private_data, int),
			     void *private_data)
{
	struct link_master *master = snd_kcontrol_chip(kcontrol);
	master->hook = hook;
	master->hook_private_data = private_data;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_ctl_add_vmaster_hook);

/**
 * snd_ctl_sync_vmaster - Sync the vmaster followers and hook
 * @kcontrol: vmaster kctl element
 * @hook_only: sync only the hook
 *
 * Forcibly call the put callback of each follower and call the hook function
 * to synchronize with the current value of the given vmaster element.
 * NOP when NULL is passed to @kcontrol.
 */
void snd_ctl_sync_vmaster(struct snd_kcontrol *kcontrol, bool hook_only)
{
	struct link_master *master;
	bool first_init = false;

	if (!kcontrol)
		return;
	master = snd_kcontrol_chip(kcontrol);
	if (!hook_only) {
		int err = master_init(master);
		if (err < 0)
			return;
		first_init = err;
		err = sync_followers(master, master->val, master->val);
		if (err < 0)
			return;
	}

	if (master->hook && !first_init)
		master->hook(master->hook_private_data, master->val);
}
EXPORT_SYMBOL_GPL(snd_ctl_sync_vmaster);

/**
 * snd_ctl_apply_vmaster_followers - Apply function to each vmaster follower
 * @kctl: vmaster kctl element
 * @func: function to apply
 * @arg: optional function argument
 *
 * Apply the function @func to each follower kctl of the given vmaster kctl.
 *
 * Return: 0 if successful, or a negative error code
 */
int snd_ctl_apply_vmaster_followers(struct snd_kcontrol *kctl,
				    int (*func)(struct snd_kcontrol *vfollower,
						struct snd_kcontrol *follower,
						void *arg),
				    void *arg)
{
	struct link_master *master;
	struct link_follower *follower;
	int err;

	master = snd_kcontrol_chip(kctl);
	err = master_init(master);
	if (err < 0)
		return err;
	list_for_each_entry(follower, &master->followers, list) {
		err = func(follower->kctl, &follower->follower, arg);
		if (err < 0)
			return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_ctl_apply_vmaster_followers);
