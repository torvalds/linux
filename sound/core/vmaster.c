/*
 * Virtual master and slave controls
 *
 *  Copyright (c) 2008 by Takashi Iwai <tiwai@suse.de>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License as
 *  published by the Free Software Foundation, version 2.
 *
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
 * link master - this contains a list of slave controls that are
 * identical types, i.e. info returns the same value type and value
 * ranges, but may have different number of counts.
 *
 * The master control is so far only mono volume/switch for simplicity.
 * The same value will be applied to all slaves.
 */
struct link_master {
	struct list_head slaves;
	struct link_ctl_info info;
	int val;		/* the master value */
	unsigned int tlv[4];
	void (*hook)(void *private_data, int);
	void *hook_private_data;
};

/*
 * link slave - this contains a slave control element
 *
 * It fakes the control callbacsk with additional attenuation by the
 * master control.  A slave may have either one or two channels.
 */

struct link_slave {
	struct list_head list;
	struct link_master *master;
	struct link_ctl_info info;
	int vals[2];		/* current values */
	unsigned int flags;
	struct snd_kcontrol *kctl; /* original kcontrol pointer */
	struct snd_kcontrol slave; /* the copy of original control entry */
};

static int slave_update(struct link_slave *slave)
{
	struct snd_ctl_elem_value *uctl;
	int err, ch;

	uctl = kmalloc(sizeof(*uctl), GFP_KERNEL);
	if (!uctl)
		return -ENOMEM;
	uctl->id = slave->slave.id;
	err = slave->slave.get(&slave->slave, uctl);
	for (ch = 0; ch < slave->info.count; ch++)
		slave->vals[ch] = uctl->value.integer.value[ch];
	kfree(uctl);
	return 0;
}

/* get the slave ctl info and save the initial values */
static int slave_init(struct link_slave *slave)
{
	struct snd_ctl_elem_info *uinfo;
	int err;

	if (slave->info.count) {
		/* already initialized */
		if (slave->flags & SND_CTL_SLAVE_NEED_UPDATE)
			return slave_update(slave);
		return 0;
	}

	uinfo = kmalloc(sizeof(*uinfo), GFP_KERNEL);
	if (!uinfo)
		return -ENOMEM;
	uinfo->id = slave->slave.id;
	err = slave->slave.info(&slave->slave, uinfo);
	if (err < 0) {
		kfree(uinfo);
		return err;
	}
	slave->info.type = uinfo->type;
	slave->info.count = uinfo->count;
	if (slave->info.count > 2  ||
	    (slave->info.type != SNDRV_CTL_ELEM_TYPE_INTEGER &&
	     slave->info.type != SNDRV_CTL_ELEM_TYPE_BOOLEAN)) {
		pr_err("ALSA: vmaster: invalid slave element\n");
		kfree(uinfo);
		return -EINVAL;
	}
	slave->info.min_val = uinfo->value.integer.min;
	slave->info.max_val = uinfo->value.integer.max;
	kfree(uinfo);

	return slave_update(slave);
}

/* initialize master volume */
static int master_init(struct link_master *master)
{
	struct link_slave *slave;

	if (master->info.count)
		return 0; /* already initialized */

	list_for_each_entry(slave, &master->slaves, list) {
		int err = slave_init(slave);
		if (err < 0)
			return err;
		master->info = slave->info;
		master->info.count = 1; /* always mono */
		/* set full volume as default (= no attenuation) */
		master->val = master->info.max_val;
		if (master->hook)
			master->hook(master->hook_private_data, master->val);
		return 1;
	}
	return -ENOENT;
}

static int slave_get_val(struct link_slave *slave,
			 struct snd_ctl_elem_value *ucontrol)
{
	int err, ch;

	err = slave_init(slave);
	if (err < 0)
		return err;
	for (ch = 0; ch < slave->info.count; ch++)
		ucontrol->value.integer.value[ch] = slave->vals[ch];
	return 0;
}

static int slave_put_val(struct link_slave *slave,
			 struct snd_ctl_elem_value *ucontrol)
{
	int err, ch, vol;

	err = master_init(slave->master);
	if (err < 0)
		return err;

	switch (slave->info.type) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
		for (ch = 0; ch < slave->info.count; ch++)
			ucontrol->value.integer.value[ch] &=
				!!slave->master->val;
		break;
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		for (ch = 0; ch < slave->info.count; ch++) {
			/* max master volume is supposed to be 0 dB */
			vol = ucontrol->value.integer.value[ch];
			vol += slave->master->val - slave->master->info.max_val;
			if (vol < slave->info.min_val)
				vol = slave->info.min_val;
			else if (vol > slave->info.max_val)
				vol = slave->info.max_val;
			ucontrol->value.integer.value[ch] = vol;
		}
		break;
	}
	return slave->slave.put(&slave->slave, ucontrol);
}

/*
 * ctl callbacks for slaves
 */
static int slave_info(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_info *uinfo)
{
	struct link_slave *slave = snd_kcontrol_chip(kcontrol);
	return slave->slave.info(&slave->slave, uinfo);
}

static int slave_get(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol)
{
	struct link_slave *slave = snd_kcontrol_chip(kcontrol);
	return slave_get_val(slave, ucontrol);
}

static int slave_put(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol)
{
	struct link_slave *slave = snd_kcontrol_chip(kcontrol);
	int err, ch, changed = 0;

	err = slave_init(slave);
	if (err < 0)
		return err;
	for (ch = 0; ch < slave->info.count; ch++) {
		if (slave->vals[ch] != ucontrol->value.integer.value[ch]) {
			changed = 1;
			slave->vals[ch] = ucontrol->value.integer.value[ch];
		}
	}
	if (!changed)
		return 0;
	err = slave_put_val(slave, ucontrol);
	if (err < 0)
		return err;
	return 1;
}

static int slave_tlv_cmd(struct snd_kcontrol *kcontrol,
			 int op_flag, unsigned int size,
			 unsigned int __user *tlv)
{
	struct link_slave *slave = snd_kcontrol_chip(kcontrol);
	/* FIXME: this assumes that the max volume is 0 dB */
	return slave->slave.tlv.c(&slave->slave, op_flag, size, tlv);
}

static void slave_free(struct snd_kcontrol *kcontrol)
{
	struct link_slave *slave = snd_kcontrol_chip(kcontrol);
	if (slave->slave.private_free)
		slave->slave.private_free(&slave->slave);
	if (slave->master)
		list_del(&slave->list);
	kfree(slave);
}

/*
 * Add a slave control to the group with the given master control
 *
 * All slaves must be the same type (returning the same information
 * via info callback).  The function doesn't check it, so it's your
 * responsibility.
 *
 * Also, some additional limitations:
 * - at most two channels
 * - logarithmic volume control (dB level), no linear volume
 * - master can only attenuate the volume, no gain
 */
int _snd_ctl_add_slave(struct snd_kcontrol *master, struct snd_kcontrol *slave,
		       unsigned int flags)
{
	struct link_master *master_link = snd_kcontrol_chip(master);
	struct link_slave *srec;

	srec = kzalloc(sizeof(*srec) +
		       slave->count * sizeof(*slave->vd), GFP_KERNEL);
	if (!srec)
		return -ENOMEM;
	srec->kctl = slave;
	srec->slave = *slave;
	memcpy(srec->slave.vd, slave->vd, slave->count * sizeof(*slave->vd));
	srec->master = master_link;
	srec->flags = flags;

	/* override callbacks */
	slave->info = slave_info;
	slave->get = slave_get;
	slave->put = slave_put;
	if (slave->vd[0].access & SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK)
		slave->tlv.c = slave_tlv_cmd;
	slave->private_data = srec;
	slave->private_free = slave_free;

	list_add_tail(&srec->list, &master_link->slaves);
	return 0;
}
EXPORT_SYMBOL(_snd_ctl_add_slave);

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

static int sync_slaves(struct link_master *master, int old_val, int new_val)
{
	struct link_slave *slave;
	struct snd_ctl_elem_value *uval;

	uval = kmalloc(sizeof(*uval), GFP_KERNEL);
	if (!uval)
		return -ENOMEM;
	list_for_each_entry(slave, &master->slaves, list) {
		master->val = old_val;
		uval->id = slave->slave.id;
		slave_get_val(slave, uval);
		master->val = new_val;
		slave_put_val(slave, uval);
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

	err = sync_slaves(master, old_val, new_val);
	if (err < 0)
		return err;
	if (master->hook && !first_init)
		master->hook(master->hook_private_data, master->val);
	return 1;
}

static void master_free(struct snd_kcontrol *kcontrol)
{
	struct link_master *master = snd_kcontrol_chip(kcontrol);
	struct link_slave *slave, *n;

	/* free all slave links and retore the original slave kctls */
	list_for_each_entry_safe(slave, n, &master->slaves, list) {
		struct snd_kcontrol *sctl = slave->kctl;
		struct list_head olist = sctl->list;
		memcpy(sctl, &slave->slave, sizeof(*sctl));
		memcpy(sctl->vd, slave->slave.vd,
		       sctl->count * sizeof(*sctl->vd));
		sctl->list = olist; /* keep the current linked-list */
		kfree(slave);
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
 * After creating a vmaster element, you can add the slave controls
 * via snd_ctl_add_slave() or snd_ctl_add_slave_uncached().
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
	INIT_LIST_HEAD(&master->slaves);

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
	if (tlv &&
	    (tlv[0] == SNDRV_CTL_TLVT_DB_SCALE ||
	     tlv[0] == SNDRV_CTL_TLVT_DB_MINMAX ||
	     tlv[0] == SNDRV_CTL_TLVT_DB_MINMAX_MUTE)) {
		kctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		memcpy(master->tlv, tlv, sizeof(master->tlv));
		kctl->tlv.p = master->tlv;
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
 * snd_ctl_sync_vmaster - Sync the vmaster slaves and hook
 * @kcontrol: vmaster kctl element
 * @hook_only: sync only the hook
 *
 * Forcibly call the put callback of each slave and call the hook function
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
		err = sync_slaves(master, master->val, master->val);
		if (err < 0)
			return;
	}

	if (master->hook && !first_init)
		master->hook(master->hook_private_data, master->val);
}
EXPORT_SYMBOL_GPL(snd_ctl_sync_vmaster);
