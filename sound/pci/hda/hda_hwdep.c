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
	char **head;
	int i;

	/* clear init verbs */
	snd_array_free(&codec->init_verbs);
	/* clear hints */
	head = codec->hints.list;
	for (i = 0; i < codec->hints.used; i++, head++)
		kfree(*head);
	snd_array_free(&codec->hints);
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
	snd_array_init(&codec->hints, sizeof(char *), 32);

	return 0;
}

#ifdef CONFIG_SND_HDA_RECONFIG

/*
 * sysfs interface
 */

static int clear_codec(struct hda_codec *codec)
{
	snd_hda_codec_reset(codec);
	clear_hwdep_elements(codec);
	return 0;
}

static int reconfig_codec(struct hda_codec *codec)
{
	int err;

	snd_printk(KERN_INFO "hda-codec: reconfiguring\n");
	snd_hda_codec_reset(codec);
	err = snd_hda_codec_configure(codec);
	if (err < 0)
		return err;
	/* rebuild PCMs */
	err = snd_hda_codec_build_pcms(codec);
	if (err < 0)
		return err;
	/* rebuild mixers */
	err = snd_hda_codec_build_controls(codec);
	if (err < 0)
		return err;
	return snd_card_register(codec->bus->card);
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
CODEC_INFO_STR_SHOW(name);
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
CODEC_INFO_STR_STORE(name);
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

static ssize_t hints_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct snd_hwdep *hwdep = dev_get_drvdata(dev);
	struct hda_codec *codec = hwdep->private_data;
	char *p;
	char **hint;

	if (!*buf || isspace(*buf) || *buf == '#' || *buf == '\n')
		return count;
	p = kstrndup_noeol(buf, 1024);
	if (!p)
		return -ENOMEM;
	hint = snd_array_new(&codec->hints);
	if (!hint) {
		kfree(p);
		return -ENOMEM;
	}
	*hint = p;
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
	CODEC_ATTR_RW(name),
	CODEC_ATTR_RW(modelname),
	CODEC_ATTR_WO(init_verbs),
	CODEC_ATTR_WO(hints),
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

#endif /* CONFIG_SND_HDA_RECONFIG */
