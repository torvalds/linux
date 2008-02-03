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
#include <sound/core.h>
#include <sound/control.h>

/*
 * a subset of information returned via ctl info callback
 */
struct link_ctl_info {
	int type;		/* value type */
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
	struct snd_kcontrol slave; /* the copy of original control entry */
};

/* get the slave ctl info and save the initial values */
static int slave_init(struct link_slave *slave)
{
	struct snd_ctl_elem_info *uinfo;
	struct snd_ctl_elem_value *uctl;
	int err, ch;

	if (slave->info.count)
		return 0; /* already initialized */

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
		snd_printk(KERN_ERR "invalid slave element\n");
		kfree(uinfo);
		return -EINVAL;
	}
	slave->info.min_val = uinfo->value.integer.min;
	slave->info.max_val = uinfo->value.integer.max;
	kfree(uinfo);

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
		return 0;
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
	return slave_put_val(slave, ucontrol);
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
 * via info callback).  The fucntion doesn't check it, so it's your
 * responsibility.
 *
 * Also, some additional limitations:
 * - at most two channels
 * - logarithmic volume control (dB level), no linear volume
 * - master can only attenuate the volume, no gain
 */
int snd_ctl_add_slave(struct snd_kcontrol *master, struct snd_kcontrol *slave)
{
	struct link_master *master_link = snd_kcontrol_chip(master);
	struct link_slave *srec;

	srec = kzalloc(sizeof(*srec) +
		       slave->count * sizeof(*slave->vd), GFP_KERNEL);
	if (!srec)
		return -ENOMEM;
	srec->slave = *slave;
	memcpy(srec->slave.vd, slave->vd, slave->count * sizeof(*slave->vd));
	srec->master = master_link;

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

static int master_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct link_master *master = snd_kcontrol_chip(kcontrol);
	struct link_slave *slave;
	struct snd_ctl_elem_value *uval;
	int err, old_val;

	err = master_init(master);
	if (err < 0)
		return err;
	old_val = master->val;
	if (ucontrol->value.integer.value[0] == old_val)
		return 0;

	uval = kmalloc(sizeof(*uval), GFP_KERNEL);
	if (!uval)
		return -ENOMEM;
	list_for_each_entry(slave, &master->slaves, list) {
		master->val = old_val;
		uval->id = slave->slave.id;
		slave_get_val(slave, uval);
		master->val = ucontrol->value.integer.value[0];
		slave_put_val(slave, uval);
	}
	kfree(uval);
	return 1;
}

static void master_free(struct snd_kcontrol *kcontrol)
{
	struct link_master *master = snd_kcontrol_chip(kcontrol);
	struct link_slave *slave;

	list_for_each_entry(slave, &master->slaves, list)
		slave->master = NULL;
	kfree(master);
}


/*
 * Create a virtual master control with the given name
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
	if (tlv) {
		/* FIXME: this assumes that the max volume is 0 dB */
		kctl->vd[0].access |= SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		kctl->tlv.p = tlv;
	}
	return kctl;
}
