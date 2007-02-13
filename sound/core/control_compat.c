/*
 * compat ioctls for control API
 *
 *   Copyright (c) by Takashi Iwai <tiwai@suse.de>
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
 */

/* this file included from control.c */

#include <linux/compat.h>

struct snd_ctl_elem_list32 {
	u32 offset;
	u32 space;
	u32 used;
	u32 count;
	u32 pids;
	unsigned char reserved[50];
} /* don't set packed attribute here */;

static int snd_ctl_elem_list_compat(struct snd_card *card,
				    struct snd_ctl_elem_list32 __user *data32)
{
	struct snd_ctl_elem_list __user *data;
	compat_caddr_t ptr;
	int err;

	data = compat_alloc_user_space(sizeof(*data));

	/* offset, space, used, count */
	if (copy_in_user(data, data32, 4 * sizeof(u32)))
		return -EFAULT;
	/* pids */
	if (get_user(ptr, &data32->pids) ||
	    put_user(compat_ptr(ptr), &data->pids))
		return -EFAULT;
	err = snd_ctl_elem_list(card, data);
	if (err < 0)
		return err;
	/* copy the result */
	if (copy_in_user(data32, data, 4 * sizeof(u32)))
		return -EFAULT;
	return 0;
}

/*
 * control element info
 * it uses union, so the things are not easy..
 */

struct snd_ctl_elem_info32 {
	struct snd_ctl_elem_id id; // the size of struct is same
	s32 type;
	u32 access;
	u32 count;
	s32 owner;
	union {
		struct {
			s32 min;
			s32 max;
			s32 step;
		} integer;
		struct {
			u64 min;
			u64 max;
			u64 step;
		} integer64;
		struct {
			u32 items;
			u32 item;
			char name[64];
		} enumerated;
		unsigned char reserved[128];
	} value;
	unsigned char reserved[64];
} __attribute__((packed));

static int snd_ctl_elem_info_compat(struct snd_ctl_file *ctl,
				    struct snd_ctl_elem_info32 __user *data32)
{
	struct snd_ctl_elem_info *data;
	int err;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (! data)
		return -ENOMEM;

	err = -EFAULT;
	/* copy id */
	if (copy_from_user(&data->id, &data32->id, sizeof(data->id)))
		goto error;
	/* we need to copy the item index.
	 * hope this doesn't break anything..
	 */
	if (get_user(data->value.enumerated.item, &data32->value.enumerated.item))
		goto error;

	snd_power_lock(ctl->card);
	err = snd_power_wait(ctl->card, SNDRV_CTL_POWER_D0);
	if (err >= 0)
		err = snd_ctl_elem_info(ctl, data);
	snd_power_unlock(ctl->card);

	if (err < 0)
		goto error;
	/* restore info to 32bit */
	err = -EFAULT;
	/* id, type, access, count */
	if (copy_to_user(&data32->id, &data->id, sizeof(data->id)) ||
	    copy_to_user(&data32->type, &data->type, 3 * sizeof(u32)))
		goto error;
	if (put_user(data->owner, &data32->owner))
		goto error;
	switch (data->type) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		if (put_user(data->value.integer.min, &data32->value.integer.min) ||
		    put_user(data->value.integer.max, &data32->value.integer.max) ||
		    put_user(data->value.integer.step, &data32->value.integer.step))
			goto error;
		break;
	case SNDRV_CTL_ELEM_TYPE_INTEGER64:
		if (copy_to_user(&data32->value.integer64,
				 &data->value.integer64,
				 sizeof(data->value.integer64)))
			goto error;
		break;
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
		if (copy_to_user(&data32->value.enumerated,
				 &data->value.enumerated,
				 sizeof(data->value.enumerated)))
			goto error;
		break;
	default:
		break;
	}
	err = 0;
 error:
	kfree(data);
	return err;
}

/* read / write */
struct snd_ctl_elem_value32 {
	struct snd_ctl_elem_id id;
	unsigned int indirect;	/* bit-field causes misalignment */
        union {
		s32 integer[128];
		unsigned char data[512];
#ifndef CONFIG_X86_64
		s64 integer64[64];
#endif
        } value;
        unsigned char reserved[128];
};


/* get the value type and count of the control */
static int get_ctl_type(struct snd_card *card, struct snd_ctl_elem_id *id,
			int *countp)
{
	struct snd_kcontrol *kctl;
	struct snd_ctl_elem_info *info;
	int err;

	down_read(&card->controls_rwsem);
	kctl = snd_ctl_find_id(card, id);
	if (! kctl) {
		up_read(&card->controls_rwsem);
		return -ENXIO;
	}
	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL) {
		up_read(&card->controls_rwsem);
		return -ENOMEM;
	}
	info->id = *id;
	err = kctl->info(kctl, info);
	up_read(&card->controls_rwsem);
	if (err >= 0) {
		err = info->type;
		*countp = info->count;
	}
	kfree(info);
	return err;
}

static int get_elem_size(int type, int count)
{
	switch (type) {
	case SNDRV_CTL_ELEM_TYPE_INTEGER64:
		return sizeof(s64) * count;
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
		return sizeof(int) * count;
	case SNDRV_CTL_ELEM_TYPE_BYTES:
		return 512;
	case SNDRV_CTL_ELEM_TYPE_IEC958:
		return sizeof(struct snd_aes_iec958);
	default:
		return -1;
	}
}

static int copy_ctl_value_from_user(struct snd_card *card,
				    struct snd_ctl_elem_value *data,
				    struct snd_ctl_elem_value32 __user *data32,
				    int *typep, int *countp)
{
	int i, type, count, size;
	unsigned int indirect;

	if (copy_from_user(&data->id, &data32->id, sizeof(data->id)))
		return -EFAULT;
	if (get_user(indirect, &data32->indirect))
		return -EFAULT;
	if (indirect)
		return -EINVAL;
	type = get_ctl_type(card, &data->id, &count);
	if (type < 0)
		return type;

	if (type == SNDRV_CTL_ELEM_TYPE_BOOLEAN ||
	    type == SNDRV_CTL_ELEM_TYPE_INTEGER) {
		for (i = 0; i < count; i++) {
			int val;
			if (get_user(val, &data32->value.integer[i]))
				return -EFAULT;
			data->value.integer.value[i] = val;
		}
	} else {
		size = get_elem_size(type, count);
		if (size < 0) {
			printk(KERN_ERR "snd_ioctl32_ctl_elem_value: unknown type %d\n", type);
			return -EINVAL;
		}
		if (copy_from_user(data->value.bytes.data,
				   data32->value.data, size))
			return -EFAULT;
	}

	*typep = type;
	*countp = count;
	return 0;
}

/* restore the value to 32bit */
static int copy_ctl_value_to_user(struct snd_ctl_elem_value32 __user *data32,
				  struct snd_ctl_elem_value *data,
				  int type, int count)
{
	int i, size;

	if (type == SNDRV_CTL_ELEM_TYPE_BOOLEAN ||
	    type == SNDRV_CTL_ELEM_TYPE_INTEGER) {
		for (i = 0; i < count; i++) {
			int val;
			val = data->value.integer.value[i];
			if (put_user(val, &data32->value.integer[i]))
				return -EFAULT;
		}
	} else {
		size = get_elem_size(type, count);
		if (copy_to_user(data32->value.data,
				 data->value.bytes.data, size))
			return -EFAULT;
	}
	return 0;
}

static int snd_ctl_elem_read_user_compat(struct snd_card *card, 
					 struct snd_ctl_elem_value32 __user *data32)
{
	struct snd_ctl_elem_value *data;
	int err, type, count;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	if ((err = copy_ctl_value_from_user(card, data, data32, &type, &count)) < 0)
		goto error;

	snd_power_lock(card);
	err = snd_power_wait(card, SNDRV_CTL_POWER_D0);
	if (err >= 0)
		err = snd_ctl_elem_read(card, data);
	snd_power_unlock(card);
	if (err >= 0)
		err = copy_ctl_value_to_user(data32, data, type, count);
 error:
	kfree(data);
	return err;
}

static int snd_ctl_elem_write_user_compat(struct snd_ctl_file *file,
					  struct snd_ctl_elem_value32 __user *data32)
{
	struct snd_ctl_elem_value *data;
	struct snd_card *card = file->card;
	int err, type, count;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;

	if ((err = copy_ctl_value_from_user(card, data, data32, &type, &count)) < 0)
		goto error;

	snd_power_lock(card);
	err = snd_power_wait(card, SNDRV_CTL_POWER_D0);
	if (err >= 0)
		err = snd_ctl_elem_write(card, file, data);
	snd_power_unlock(card);
	if (err >= 0)
		err = copy_ctl_value_to_user(data32, data, type, count);
 error:
	kfree(data);
	return err;
}

/* add or replace a user control */
static int snd_ctl_elem_add_compat(struct snd_ctl_file *file,
				   struct snd_ctl_elem_info32 __user *data32,
				   int replace)
{
	struct snd_ctl_elem_info *data;
	int err;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (! data)
		return -ENOMEM;

	err = -EFAULT;
	/* id, type, access, count */ \
	if (copy_from_user(&data->id, &data32->id, sizeof(data->id)) ||
	    copy_from_user(&data->type, &data32->type, 3 * sizeof(u32)))
		goto error;
	if (get_user(data->owner, &data32->owner) ||
	    get_user(data->type, &data32->type))
		goto error;
	switch (data->type) {
	case SNDRV_CTL_ELEM_TYPE_BOOLEAN:
	case SNDRV_CTL_ELEM_TYPE_INTEGER:
		if (get_user(data->value.integer.min, &data32->value.integer.min) ||
		    get_user(data->value.integer.max, &data32->value.integer.max) ||
		    get_user(data->value.integer.step, &data32->value.integer.step))
			goto error;
		break;
	case SNDRV_CTL_ELEM_TYPE_INTEGER64:
		if (copy_from_user(&data->value.integer64,
				   &data32->value.integer64,
				   sizeof(data->value.integer64)))
			goto error;
		break;
	case SNDRV_CTL_ELEM_TYPE_ENUMERATED:
		if (copy_from_user(&data->value.enumerated,
				   &data32->value.enumerated,
				   sizeof(data->value.enumerated)))
			goto error;
		break;
	default:
		break;
	}
	err = snd_ctl_elem_add(file, data, replace);
 error:
	kfree(data);
	return err;
}  

enum {
	SNDRV_CTL_IOCTL_ELEM_LIST32 = _IOWR('U', 0x10, struct snd_ctl_elem_list32),
	SNDRV_CTL_IOCTL_ELEM_INFO32 = _IOWR('U', 0x11, struct snd_ctl_elem_info32),
	SNDRV_CTL_IOCTL_ELEM_READ32 = _IOWR('U', 0x12, struct snd_ctl_elem_value32),
	SNDRV_CTL_IOCTL_ELEM_WRITE32 = _IOWR('U', 0x13, struct snd_ctl_elem_value32),
	SNDRV_CTL_IOCTL_ELEM_ADD32 = _IOWR('U', 0x17, struct snd_ctl_elem_info32),
	SNDRV_CTL_IOCTL_ELEM_REPLACE32 = _IOWR('U', 0x18, struct snd_ctl_elem_info32),
};

static inline long snd_ctl_ioctl_compat(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct snd_ctl_file *ctl;
	struct snd_kctl_ioctl *p;
	void __user *argp = compat_ptr(arg);
	int err;

	ctl = file->private_data;
	snd_assert(ctl && ctl->card, return -ENXIO);

	switch (cmd) {
	case SNDRV_CTL_IOCTL_PVERSION:
	case SNDRV_CTL_IOCTL_CARD_INFO:
	case SNDRV_CTL_IOCTL_SUBSCRIBE_EVENTS:
	case SNDRV_CTL_IOCTL_POWER:
	case SNDRV_CTL_IOCTL_POWER_STATE:
	case SNDRV_CTL_IOCTL_ELEM_LOCK:
	case SNDRV_CTL_IOCTL_ELEM_UNLOCK:
	case SNDRV_CTL_IOCTL_ELEM_REMOVE:
	case SNDRV_CTL_IOCTL_TLV_READ:
	case SNDRV_CTL_IOCTL_TLV_WRITE:
	case SNDRV_CTL_IOCTL_TLV_COMMAND:
		return snd_ctl_ioctl(file, cmd, (unsigned long)argp);
	case SNDRV_CTL_IOCTL_ELEM_LIST32:
		return snd_ctl_elem_list_compat(ctl->card, argp);
	case SNDRV_CTL_IOCTL_ELEM_INFO32:
		return snd_ctl_elem_info_compat(ctl, argp);
	case SNDRV_CTL_IOCTL_ELEM_READ32:
		return snd_ctl_elem_read_user_compat(ctl->card, argp);
	case SNDRV_CTL_IOCTL_ELEM_WRITE32:
		return snd_ctl_elem_write_user_compat(ctl, argp);
	case SNDRV_CTL_IOCTL_ELEM_ADD32:
		return snd_ctl_elem_add_compat(ctl, argp, 0);
	case SNDRV_CTL_IOCTL_ELEM_REPLACE32:
		return snd_ctl_elem_add_compat(ctl, argp, 1);
	}

	down_read(&snd_ioctl_rwsem);
	list_for_each_entry(p, &snd_control_compat_ioctls, list) {
		if (p->fioctl) {
			err = p->fioctl(ctl->card, ctl, cmd, arg);
			if (err != -ENOIOCTLCMD) {
				up_read(&snd_ioctl_rwsem);
				return err;
			}
		}
	}
	up_read(&snd_ioctl_rwsem);
	return -ENOIOCTLCMD;
}
