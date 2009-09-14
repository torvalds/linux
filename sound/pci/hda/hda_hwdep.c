/*
 * HWDEP Interface for HD-audio codec
 *
 * Copyright (c) 2007 Takashi Iwai <tiwai@suse.de>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/compat.h>
#include <linux/mutex.h>
#include <linux/ctype.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"
#include <sound/hda_hwdep.h>
#include <sound/minors.h>

/* hint string pair */
struct hda_hint {
	const char *key;
	const char *val;	/* contained in the same alloc as key */
};

/*
 * write/read an out-of-bound verb
 */
static int verb_write_ioctl(struct hda_codec *codec,
			    struct hda_verb_ioctl __user *arg)
{
	u32 verb, res;

	if (get_user(verb, &arg->verb))
		return -EFAULT;
	res = snd_hda_codec_read(codec, verb >> 24, 0,
				 (verb >> 8) & 0xffff, verb & 0xff);
	if (put_user(res, &arg->res))
		return -EFAULT;
	return 0;
}

static int get_wcap_ioctl(struct hda_codec *codec,
			  struct hda_verb_ioctl __user *arg)
{
	u32 verb, res;
	
	if (get_user(verb, &arg->verb))
		return -EFAULT;
	res = get_wcaps(codec, verb >> 24);
	if (put_user(res, &arg->res))
		return -EFAULT;
	return 0;
}


/*
 */
static int hda_hwdep_ioctl(struct snd_hwdep *hw, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	struct hda_codec *codec = hw->private_data;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case HDA_IOCTL_PVERSION:
		return put_user(HDA_HWDEP_VERSION, (int __user *)argp);
	case HDA_IOCTL_VERB_WRITE:
		return verb_write_ioctl(codec, argp);
	case HDA_IOCTL_GET_WCAP:
		return get_wcap_ioctl(codec, argp);
	}
	return -ENOIOCTLCMD;
}

#ifdef CONFIG_COMPAT
static int hda_hwdep_ioctl_compat(struct snd_hwdep *hw, struct file *file,
				  unsigned int cmd, unsigned long arg)
{
	return hda_hwdep_ioctl(hw, file, cmd, (unsigned long)compat_ptr(arg));
}
#endif

static int hda_hwdep_open(struct snd_hwdep *hw, struct file *file)
{
#ifndef CONFIG_SND_DEBUG_VERBOSE
	if (!capable(CAP_SYS_RAWIO))
		return -EACCES;
#endif
	return 0;
}

static void clear_hwdep_elements(struct hda_codec *codec)
{
	int i;

	/* clear init verbs */
	snd_array_free(&codec->init_verbs);
	/* clear hints */
	for (i = 0; i < codec->hints.used; i++) {
		struct hda_hint *hint = snd_array_elem(&codec->hints, i);
		kfree(hint->key); /* we don't need to free hint->val */
	}
	snd_array_free(&codec->hints);
	snd_array_free(&codec->user_pins);
}

static void hwdep_free(struct snd_hwdep *hwdep)
{
	clear_hwdep_elements(hwdep->private_data);
}

int /*__devinit*/ snd_hda_create_hwdep(struct hda_codec *codec)
{
	char hwname[16];
	struct snd_hwdep *hwdep;
	int err;

	sprintf(hwname, "HDA Codec %d", codec->addr);
	err = snd_hwdep_new(codec->bus->card, hwname, codec->addr, &hwdep);
	if (err < 0)
		return err;
	codec->hwdep = hwdep;
	sprintf(hwdep->name, "HDA Codec %d", codec->addr);
	hwdep->iface = SNDRV_HWDEP_IFACE_HDA;
	hwdep->private_data = codec;
	hwdep->private_free = hwdep_free;
	hwdep->exclusive = 1;

	hwdep->ops.open = hda_hwdep_open;
	hwdep->ops.ioctl = hda_hwdep_ioctl;
#ifdef CONFIG_COMPAT
	hwdep->ops.ioctl_compat = hda_hwdep_ioctl_compat;
#endif

	snd_array_init(&codec->init_verbs, sizeof(struct hda_verb), 32);
	snd_array_init(&codec->hints, sizeof(struct hda_hint), 32);
	snd_array_init(&codec->user_pins, sizeof(struct hda_pincfg), 16);

	return 0;
}

#ifdef CONFIG_SND_HDA_RECONFIG

/*
 * sysfs interface
 */

static int clear_codec(struct hda_codec *codec)
{
	int err;

	err = snd_hda_codec_reset(codec);
	if (err < 0) {
		snd_printk(KERN_ERR "The codec is being used, can't free.\n");
		return err;
	}
	clear_hwdep_elements(codec);
	return 0;
}

static int reconfig_codec(struct hda_codec *codec)
{
	int err;

	snd_hda_power_up(codec);
	snd_printk(KERN_INFO "hda-codec: reconfiguring\n");
	err = snd_hda_codec_reset(codec);
	if (err < 0) {
		snd_printk(KERN_ERR
			   "The codec is being used, can't reconfigure.\n");
		goto error;
	}
	err = snd_hda_codec_configure(codec);
	if (err < 0)
		goto error;
	/* rebuild PCMs */
	err = snd_hda_codec_build_pcms(codec);
	if (err < 0)
		goto error;
	/* rebuild mixers */
	err = snd_hda_codec_build_controls(codec);
	if (err < 0)
		goto error;
	err = snd_card_register(codec->bus->card);
 error:
	snd_hda_power_down(codec);
	return err;
}

/*
 * allocate a string at most len chars, and remove the trailing EOL
 */
static char *kstrndup_noeol(const char *src, size_t len)
{
	char *s = kstrndup(src, len, GFP_KERNEL);
	char *p;
	if (!s)
		return NULL;
	p = strchr(s, '\n');
	if (p)
		*p = 0;
	return s;
}

#define CODEC_INFO_SHOW(type)					\
static ssize_t type##_show(struct device *dev,			\
			   struct device_attribute *attr,	\
			   char *buf)				\
{								\
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);		\
	struct hda_codec *codec = hwdep->private_data;		\
	return sprintf(buf, "0x%x\n", codec->type);		\
}

#define CODEC_INFO_STR_SHOW(type)				\
static ssize_t type##_show(struct device *dev,			\
			     struct device_attribute *attr,	\
					char *buf)		\
{								\
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);		\
	struct hda_codec *codec = hwdep->private_data;		\
	return sprintf(buf, "%s\n",				\
		       codec->type ? codec->type : "");		\
}

CODEC_INFO_SHOW(vendor_id);
CODEC_INFO_SHOW(subsystem_id);
CODEC_INFO_SHOW(revision_id);
CODEC_INFO_SHOW(afg);
CODEC_INFO_SHOW(mfg);
CODEC_INFO_STR_SHOW(vendor_name);
CODEC_INFO_STR_SHOW(chip_name);
CODEC_INFO_STR_SHOW(modelname);

#define CODEC_INFO_STORE(type)					\
static ssize_t type##_store(struct device *dev,			\
			    struct device_attribute *attr,	\
			    const char *buf, size_t count)	\
{								\
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);		\
	struct hda_codec *codec = hwdep->private_data;		\
	char *after;						\
	codec->type = simple_strtoul(buf, &after, 0);		\
	return count;						\
}

#define CODEC_INFO_STR_STORE(type)				\
static ssize_t type##_store(struct device *dev,			\
			    struct device_attribute *attr,	\
			    const char *buf, size_t count)	\
{								\
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);		\
	struct hda_codec *codec = hwdep->private_data;		\
	char *s = kstrndup_noeol(buf, 64);			\
	if (!s)							\
		return -ENOMEM;					\
	kfree(codec->type);					\
	codec->type = s;					\
	return count;						\
}

CODEC_INFO_STORE(vendor_id);
CODEC_INFO_STORE(subsystem_id);
CODEC_INFO_STORE(revision_id);
CODEC_INFO_STR_STORE(vendor_name);
CODEC_INFO_STR_STORE(chip_name);
CODEC_INFO_STR_STORE(modelname);

#define CODEC_ACTION_STORE(type)				\
static ssize_t type##_store(struct device *dev,			\
			    struct device_attribute *attr,	\
			    const char *buf, size_t count)	\
{								\
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);		\
	struct hda_codec *codec = hwdep->private_data;		\
	int err = 0;						\
	if (*buf)						\
		err = type##_codec(codec);			\
	return err < 0 ? err : count;				\
}

CODEC_ACTION_STORE(reconfig);
CODEC_ACTION_STORE(clear);

static ssize_t init_verbs_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);
	struct hda_codec *codec = hwdep->private_data;
	int i, len = 0;
	for (i = 0; i < codec->init_verbs.used; i++) {
		struct hda_verb *v = snd_array_elem(&codec->init_verbs, i);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"0x%02x 0x%03x 0x%04x\n",
				v->nid, v->verb, v->param);
	}
	return len;
}

static ssize_t init_verbs_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);
	struct hda_codec *codec = hwdep->private_data;
	struct hda_verb *v;
	int nid, verb, param;

	if (sscanf(buf, "%i %i %i", &nid, &verb, &param) != 3)
		return -EINVAL;
	if (!nid || !verb)
		return -EINVAL;
	v = snd_array_new(&codec->init_verbs);
	if (!v)
		return -ENOMEM;
	v->nid = nid;
	v->verb = verb;
	v->param = param;
	return count;
}

static ssize_t hints_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);
	struct hda_codec *codec = hwdep->private_data;
	int i, len = 0;
	for (i = 0; i < codec->hints.used; i++) {
		struct hda_hint *hint = snd_array_elem(&codec->hints, i);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"%s = %s\n", hint->key, hint->val);
	}
	return len;
}

static struct hda_hint *get_hint(struct hda_codec *codec, const char *key)
{
	int i;

	for (i = 0; i < codec->hints.used; i++) {
		struct hda_hint *hint = snd_array_elem(&codec->hints, i);
		if (!strcmp(hint->key, key))
			return hint;
	}
	return NULL;
}

static void remove_trail_spaces(char *str)
{
	char *p;
	if (!*str)
		return;
	p = str + strlen(str) - 1;
	for (; isspace(*p); p--) {
		*p = 0;
		if (p == str)
			return;
	}
}

#define MAX_HINTS	1024

static ssize_t hints_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);
	struct hda_codec *codec = hwdep->private_data;
	char *key, *val;
	struct hda_hint *hint;

	while (isspace(*buf))
		buf++;
	if (!*buf || *buf == '#' || *buf == '\n')
		return count;
	if (*buf == '=')
		return -EINVAL;
	key = kstrndup_noeol(buf, 1024);
	if (!key)
		return -ENOMEM;
	/* extract key and val */
	val = strchr(key, '=');
	if (!val) {
		kfree(key);
		return -EINVAL;
	}
	*val++ = 0;
	while (isspace(*val))
		val++;
	remove_trail_spaces(key);
	remove_trail_spaces(val);
	hint = get_hint(codec, key);
	if (hint) {
		/* replace */
		kfree(hint->key);
		hint->key = key;
		hint->val = val;
		return count;
	}
	/* allocate a new hint entry */
	if (codec->hints.used >= MAX_HINTS)
		hint = NULL;
	else
		hint = snd_array_new(&codec->hints);
	if (!hint) {
		kfree(key);
		return -ENOMEM;
	}
	hint->key = key;
	hint->val = val;
	return count;
}

static ssize_t pin_configs_show(struct hda_codec *codec,
				struct snd_array *list,
				char *buf)
{
	int i, len = 0;
	for (i = 0; i < list->used; i++) {
		struct hda_pincfg *pin = snd_array_elem(list, i);
		len += sprintf(buf + len, "0x%02x 0x%08x\n",
			       pin->nid, pin->cfg);
	}
	return len;
}

static ssize_t init_pin_configs_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);
	struct hda_codec *codec = hwdep->private_data;
	return pin_configs_show(codec, &codec->init_pins, buf);
}

static ssize_t user_pin_configs_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);
	struct hda_codec *codec = hwdep->private_data;
	return pin_configs_show(codec, &codec->user_pins, buf);
}

static ssize_t driver_pin_configs_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);
	struct hda_codec *codec = hwdep->private_data;
	return pin_configs_show(codec, &codec->driver_pins, buf);
}

#define MAX_PIN_CONFIGS		32

static ssize_t user_pin_configs_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);
	struct hda_codec *codec = hwdep->private_data;
	int nid, cfg;
	int err;

	if (sscanf(buf, "%i %i", &nid, &cfg) != 2)
		return -EINVAL;
	if (!nid)
		return -EINVAL;
	err = snd_hda_add_pincfg(codec, &codec->user_pins, nid, cfg);
	if (err < 0)
		return err;
	return count;
}

#define CODEC_ATTR_RW(type) \
	__ATTR(type, 0644, type##_show, type##_store)
#define CODEC_ATTR_RO(type) \
	__ATTR_RO(type)
#define CODEC_ATTR_WO(type) \
	__ATTR(type, 0200, NULL, type##_store)

static struct device_attribute codec_attrs[] = {
	CODEC_ATTR_RW(vendor_id),
	CODEC_ATTR_RW(subsystem_id),
	CODEC_ATTR_RW(revision_id),
	CODEC_ATTR_RO(afg),
	CODEC_ATTR_RO(mfg),
	CODEC_ATTR_RW(vendor_name),
	CODEC_ATTR_RW(chip_name),
	CODEC_ATTR_RW(modelname),
	CODEC_ATTR_RW(init_verbs),
	CODEC_ATTR_RW(hints),
	CODEC_ATTR_RO(init_pin_configs),
	CODEC_ATTR_RW(user_pin_configs),
	CODEC_ATTR_RO(driver_pin_configs),
	CODEC_ATTR_WO(reconfig),
	CODEC_ATTR_WO(clear),
};

/*
 * create sysfs files on hwdep directory
 */
int snd_hda_hwdep_add_sysfs(struct hda_codec *codec)
{
	struct snd_hwdep *hwdep = codec->hwdep;
	int i;

	for (i = 0; i < ARRAY_SIZE(codec_attrs); i++)
		snd_add_device_sysfs_file(SNDRV_DEVICE_TYPE_HWDEP, hwdep->card,
					  hwdep->device, &codec_attrs[i]);
	return 0;
}

/*
 * Look for hint string
 */
const char *snd_hda_get_hint(struct hda_codec *codec, const char *key)
{
	struct hda_hint *hint = get_hint(codec, key);
	return hint ? hint->val : NULL;
}
EXPORT_SYMBOL_HDA(snd_hda_get_hint);

int snd_hda_get_bool_hint(struct hda_codec *codec, const char *key)
{
	const char *p = snd_hda_get_hint(codec, key);
	if (!p || !*p)
		return -ENOENT;
	switch (toupper(*p)) {
	case 'T': /* true */
	case 'Y': /* yes */
	case '1':
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_get_bool_hint);

#endif /* CONFIG_SND_HDA_RECONFIG */
