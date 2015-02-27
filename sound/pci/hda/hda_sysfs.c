/*
 * sysfs interface for HD-audio codec
 *
 * Copyright (c) 2014 Takashi Iwai <tiwai@suse.de>
 *
 * split from hda_hwdep.c
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/mutex.h>
#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/export.h>
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

#ifdef CONFIG_PM
static ssize_t power_on_acct_show(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct hda_codec *codec = dev_get_drvdata(dev);
	snd_hda_update_power_acct(codec);
	return sprintf(buf, "%u\n", jiffies_to_msecs(codec->power_on_acct));
}

static ssize_t power_off_acct_show(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct hda_codec *codec = dev_get_drvdata(dev);
	snd_hda_update_power_acct(codec);
	return sprintf(buf, "%u\n", jiffies_to_msecs(codec->power_off_acct));
}

static DEVICE_ATTR_RO(power_on_acct);
static DEVICE_ATTR_RO(power_off_acct);
#endif /* CONFIG_PM */

#define CODEC_INFO_SHOW(type)					\
static ssize_t type##_show(struct device *dev,			\
			   struct device_attribute *attr,	\
			   char *buf)				\
{								\
	struct hda_codec *codec = dev_get_drvdata(dev);		\
	return sprintf(buf, "0x%x\n", codec->type);		\
}

#define CODEC_INFO_STR_SHOW(type)				\
static ssize_t type##_show(struct device *dev,			\
			     struct device_attribute *attr,	\
					char *buf)		\
{								\
	struct hda_codec *codec = dev_get_drvdata(dev);		\
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

static ssize_t pin_configs_show(struct hda_codec *codec,
				struct snd_array *list,
				char *buf)
{
	int i, len = 0;
	mutex_lock(&codec->user_mutex);
	for (i = 0; i < list->used; i++) {
		struct hda_pincfg *pin = snd_array_elem(list, i);
		len += sprintf(buf + len, "0x%02x 0x%08x\n",
			       pin->nid, pin->cfg);
	}
	mutex_unlock(&codec->user_mutex);
	return len;
}

static ssize_t init_pin_configs_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct hda_codec *codec = dev_get_drvdata(dev);
	return pin_configs_show(codec, &codec->init_pins, buf);
}

static ssize_t driver_pin_configs_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct hda_codec *codec = dev_get_drvdata(dev);
	return pin_configs_show(codec, &codec->driver_pins, buf);
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
		codec_err(codec, "The codec is being used, can't free.\n");
		return err;
	}
	snd_hda_sysfs_clear(codec);
	return 0;
}

static int reconfig_codec(struct hda_codec *codec)
{
	int err;

	snd_hda_power_up(codec);
	codec_info(codec, "hda-codec: reconfiguring\n");
	err = snd_hda_codec_reset(codec);
	if (err < 0) {
		codec_err(codec,
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
	err = snd_card_register(codec->card);
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

#define CODEC_INFO_STORE(type)					\
static ssize_t type##_store(struct device *dev,			\
			    struct device_attribute *attr,	\
			    const char *buf, size_t count)	\
{								\
	struct hda_codec *codec = dev_get_drvdata(dev);		\
	unsigned long val;					\
	int err = kstrtoul(buf, 0, &val);			\
	if (err < 0)						\
		return err;					\
	codec->type = val;					\
	return count;						\
}

#define CODEC_INFO_STR_STORE(type)				\
static ssize_t type##_store(struct device *dev,			\
			    struct device_attribute *attr,	\
			    const char *buf, size_t count)	\
{								\
	struct hda_codec *codec = dev_get_drvdata(dev);		\
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
	struct hda_codec *codec = dev_get_drvdata(dev);		\
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
	struct hda_codec *codec = dev_get_drvdata(dev);
	int i, len = 0;
	mutex_lock(&codec->user_mutex);
	for (i = 0; i < codec->init_verbs.used; i++) {
		struct hda_verb *v = snd_array_elem(&codec->init_verbs, i);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"0x%02x 0x%03x 0x%04x\n",
				v->nid, v->verb, v->param);
	}
	mutex_unlock(&codec->user_mutex);
	return len;
}

static int parse_init_verbs(struct hda_codec *codec, const char *buf)
{
	struct hda_verb *v;
	int nid, verb, param;

	if (sscanf(buf, "%i %i %i", &nid, &verb, &param) != 3)
		return -EINVAL;
	if (!nid || !verb)
		return -EINVAL;
	mutex_lock(&codec->user_mutex);
	v = snd_array_new(&codec->init_verbs);
	if (!v) {
		mutex_unlock(&codec->user_mutex);
		return -ENOMEM;
	}
	v->nid = nid;
	v->verb = verb;
	v->param = param;
	mutex_unlock(&codec->user_mutex);
	return 0;
}

static ssize_t init_verbs_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hda_codec *codec = dev_get_drvdata(dev);
	int err = parse_init_verbs(codec, buf);
	if (err < 0)
		return err;
	return count;
}

static ssize_t hints_show(struct device *dev,
			  struct device_attribute *attr,
			  char *buf)
{
	struct hda_codec *codec = dev_get_drvdata(dev);
	int i, len = 0;
	mutex_lock(&codec->user_mutex);
	for (i = 0; i < codec->hints.used; i++) {
		struct hda_hint *hint = snd_array_elem(&codec->hints, i);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"%s = %s\n", hint->key, hint->val);
	}
	mutex_unlock(&codec->user_mutex);
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

static int parse_hints(struct hda_codec *codec, const char *buf)
{
	char *key, *val;
	struct hda_hint *hint;
	int err = 0;

	buf = skip_spaces(buf);
	if (!*buf || *buf == '#' || *buf == '\n')
		return 0;
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
	val = skip_spaces(val);
	remove_trail_spaces(key);
	remove_trail_spaces(val);
	mutex_lock(&codec->user_mutex);
	hint = get_hint(codec, key);
	if (hint) {
		/* replace */
		kfree(hint->key);
		hint->key = key;
		hint->val = val;
		goto unlock;
	}
	/* allocate a new hint entry */
	if (codec->hints.used >= MAX_HINTS)
		hint = NULL;
	else
		hint = snd_array_new(&codec->hints);
	if (hint) {
		hint->key = key;
		hint->val = val;
	} else {
		err = -ENOMEM;
	}
 unlock:
	mutex_unlock(&codec->user_mutex);
	if (err)
		kfree(key);
	return err;
}

static ssize_t hints_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct hda_codec *codec = dev_get_drvdata(dev);
	int err = parse_hints(codec, buf);
	if (err < 0)
		return err;
	return count;
}

static ssize_t user_pin_configs_show(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct hda_codec *codec = dev_get_drvdata(dev);
	return pin_configs_show(codec, &codec->user_pins, buf);
}

#define MAX_PIN_CONFIGS		32

static int parse_user_pin_configs(struct hda_codec *codec, const char *buf)
{
	int nid, cfg, err;

	if (sscanf(buf, "%i %i", &nid, &cfg) != 2)
		return -EINVAL;
	if (!nid)
		return -EINVAL;
	mutex_lock(&codec->user_mutex);
	err = snd_hda_add_pincfg(codec, &codec->user_pins, nid, cfg);
	mutex_unlock(&codec->user_mutex);
	return err;
}

static ssize_t user_pin_configs_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct hda_codec *codec = dev_get_drvdata(dev);
	int err = parse_user_pin_configs(codec, buf);
	if (err < 0)
		return err;
	return count;
}

/* sysfs attributes exposed only when CONFIG_SND_HDA_RECONFIG=y */
static DEVICE_ATTR_RW(init_verbs);
static DEVICE_ATTR_RW(hints);
static DEVICE_ATTR_RW(user_pin_configs);
static DEVICE_ATTR_WO(reconfig);
static DEVICE_ATTR_WO(clear);

/**
 * snd_hda_get_hint - Look for hint string
 * @codec: the HDA codec
 * @key: the hint key string
 *
 * Look for a hint key/value pair matching with the given key string
 * and returns the value string.  If nothing found, returns NULL.
 */
const char *snd_hda_get_hint(struct hda_codec *codec, const char *key)
{
	struct hda_hint *hint = get_hint(codec, key);
	return hint ? hint->val : NULL;
}
EXPORT_SYMBOL_GPL(snd_hda_get_hint);

/**
 * snd_hda_get_bool_hint - Get a boolean hint value
 * @codec: the HDA codec
 * @key: the hint key string
 *
 * Look for a hint key/value pair matching with the given key string
 * and returns a boolean value parsed from the value.  If no matching
 * key is found, return a negative value.
 */
int snd_hda_get_bool_hint(struct hda_codec *codec, const char *key)
{
	const char *p;
	int ret;

	mutex_lock(&codec->user_mutex);
	p = snd_hda_get_hint(codec, key);
	if (!p || !*p)
		ret = -ENOENT;
	else {
		switch (toupper(*p)) {
		case 'T': /* true */
		case 'Y': /* yes */
		case '1':
			ret = 1;
			break;
		default:
			ret = 0;
			break;
		}
	}
	mutex_unlock(&codec->user_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_hda_get_bool_hint);

/**
 * snd_hda_get_int_hint - Get an integer hint value
 * @codec: the HDA codec
 * @key: the hint key string
 * @valp: pointer to store a value
 *
 * Look for a hint key/value pair matching with the given key string
 * and stores the integer value to @valp.  If no matching key is found,
 * return a negative error code.  Otherwise it returns zero.
 */
int snd_hda_get_int_hint(struct hda_codec *codec, const char *key, int *valp)
{
	const char *p;
	unsigned long val;
	int ret;

	mutex_lock(&codec->user_mutex);
	p = snd_hda_get_hint(codec, key);
	if (!p)
		ret = -ENOENT;
	else if (kstrtoul(p, 0, &val))
		ret = -EINVAL;
	else {
		*valp = val;
		ret = 0;
	}
	mutex_unlock(&codec->user_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(snd_hda_get_int_hint);
#endif /* CONFIG_SND_HDA_RECONFIG */

/*
 * common sysfs attributes
 */
#ifdef CONFIG_SND_HDA_RECONFIG
#define RECONFIG_DEVICE_ATTR(name)	DEVICE_ATTR_RW(name)
#else
#define RECONFIG_DEVICE_ATTR(name)	DEVICE_ATTR_RO(name)
#endif
static RECONFIG_DEVICE_ATTR(vendor_id);
static RECONFIG_DEVICE_ATTR(subsystem_id);
static RECONFIG_DEVICE_ATTR(revision_id);
static DEVICE_ATTR_RO(afg);
static DEVICE_ATTR_RO(mfg);
static RECONFIG_DEVICE_ATTR(vendor_name);
static RECONFIG_DEVICE_ATTR(chip_name);
static RECONFIG_DEVICE_ATTR(modelname);
static DEVICE_ATTR_RO(init_pin_configs);
static DEVICE_ATTR_RO(driver_pin_configs);


#ifdef CONFIG_SND_HDA_PATCH_LOADER

/* parser mode */
enum {
	LINE_MODE_NONE,
	LINE_MODE_CODEC,
	LINE_MODE_MODEL,
	LINE_MODE_PINCFG,
	LINE_MODE_VERB,
	LINE_MODE_HINT,
	LINE_MODE_VENDOR_ID,
	LINE_MODE_SUBSYSTEM_ID,
	LINE_MODE_REVISION_ID,
	LINE_MODE_CHIP_NAME,
	NUM_LINE_MODES,
};

static inline int strmatch(const char *a, const char *b)
{
	return strncasecmp(a, b, strlen(b)) == 0;
}

/* parse the contents after the line "[codec]"
 * accept only the line with three numbers, and assign the current codec
 */
static void parse_codec_mode(char *buf, struct hda_bus *bus,
			     struct hda_codec **codecp)
{
	int vendorid, subid, caddr;
	struct hda_codec *codec;

	*codecp = NULL;
	if (sscanf(buf, "%i %i %i", &vendorid, &subid, &caddr) == 3) {
		list_for_each_entry(codec, &bus->codec_list, list) {
			if ((vendorid <= 0 || codec->vendor_id == vendorid) &&
			    (subid <= 0 || codec->subsystem_id == subid) &&
			    codec->addr == caddr) {
				*codecp = codec;
				break;
			}
		}
	}
}

/* parse the contents after the other command tags, [pincfg], [verb],
 * [vendor_id], [subsystem_id], [revision_id], [chip_name], [hint] and [model]
 * just pass to the sysfs helper (only when any codec was specified)
 */
static void parse_pincfg_mode(char *buf, struct hda_bus *bus,
			      struct hda_codec **codecp)
{
	parse_user_pin_configs(*codecp, buf);
}

static void parse_verb_mode(char *buf, struct hda_bus *bus,
			    struct hda_codec **codecp)
{
	parse_init_verbs(*codecp, buf);
}

static void parse_hint_mode(char *buf, struct hda_bus *bus,
			    struct hda_codec **codecp)
{
	parse_hints(*codecp, buf);
}

static void parse_model_mode(char *buf, struct hda_bus *bus,
			     struct hda_codec **codecp)
{
	kfree((*codecp)->modelname);
	(*codecp)->modelname = kstrdup(buf, GFP_KERNEL);
}

static void parse_chip_name_mode(char *buf, struct hda_bus *bus,
				 struct hda_codec **codecp)
{
	kfree((*codecp)->chip_name);
	(*codecp)->chip_name = kstrdup(buf, GFP_KERNEL);
}

#define DEFINE_PARSE_ID_MODE(name) \
static void parse_##name##_mode(char *buf, struct hda_bus *bus, \
				 struct hda_codec **codecp) \
{ \
	unsigned long val; \
	if (!kstrtoul(buf, 0, &val)) \
		(*codecp)->name = val; \
}

DEFINE_PARSE_ID_MODE(vendor_id);
DEFINE_PARSE_ID_MODE(subsystem_id);
DEFINE_PARSE_ID_MODE(revision_id);


struct hda_patch_item {
	const char *tag;
	const char *alias;
	void (*parser)(char *buf, struct hda_bus *bus, struct hda_codec **retc);
};

static struct hda_patch_item patch_items[NUM_LINE_MODES] = {
	[LINE_MODE_CODEC] = {
		.tag = "[codec]",
		.parser = parse_codec_mode,
	},
	[LINE_MODE_MODEL] = {
		.tag = "[model]",
		.parser = parse_model_mode,
	},
	[LINE_MODE_VERB] = {
		.tag = "[verb]",
		.alias = "[init_verbs]",
		.parser = parse_verb_mode,
	},
	[LINE_MODE_PINCFG] = {
		.tag = "[pincfg]",
		.alias = "[user_pin_configs]",
		.parser = parse_pincfg_mode,
	},
	[LINE_MODE_HINT] = {
		.tag = "[hint]",
		.alias = "[hints]",
		.parser = parse_hint_mode
	},
	[LINE_MODE_VENDOR_ID] = {
		.tag = "[vendor_id]",
		.parser = parse_vendor_id_mode,
	},
	[LINE_MODE_SUBSYSTEM_ID] = {
		.tag = "[subsystem_id]",
		.parser = parse_subsystem_id_mode,
	},
	[LINE_MODE_REVISION_ID] = {
		.tag = "[revision_id]",
		.parser = parse_revision_id_mode,
	},
	[LINE_MODE_CHIP_NAME] = {
		.tag = "[chip_name]",
		.parser = parse_chip_name_mode,
	},
};

/* check the line starting with '[' -- change the parser mode accodingly */
static int parse_line_mode(char *buf, struct hda_bus *bus)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(patch_items); i++) {
		if (!patch_items[i].tag)
			continue;
		if (strmatch(buf, patch_items[i].tag))
			return i;
		if (patch_items[i].alias && strmatch(buf, patch_items[i].alias))
			return i;
	}
	return LINE_MODE_NONE;
}

/* copy one line from the buffer in fw, and update the fields in fw
 * return zero if it reaches to the end of the buffer, or non-zero
 * if successfully copied a line
 *
 * the spaces at the beginning and the end of the line are stripped
 */
static int get_line_from_fw(char *buf, int size, size_t *fw_size_p,
			    const void **fw_data_p)
{
	int len;
	size_t fw_size = *fw_size_p;
	const char *p = *fw_data_p;

	while (isspace(*p) && fw_size) {
		p++;
		fw_size--;
	}
	if (!fw_size)
		return 0;

	for (len = 0; len < fw_size; len++) {
		if (!*p)
			break;
		if (*p == '\n') {
			p++;
			len++;
			break;
		}
		if (len < size)
			*buf++ = *p++;
	}
	*buf = 0;
	*fw_size_p = fw_size - len;
	*fw_data_p = p;
	remove_trail_spaces(buf);
	return 1;
}

/**
 * snd_hda_load_patch - load a "patch" firmware file and parse it
 * @bus: HD-audio bus
 * @fw_size: the firmware byte size
 * @fw_buf: the firmware data
 */
int snd_hda_load_patch(struct hda_bus *bus, size_t fw_size, const void *fw_buf)
{
	char buf[128];
	struct hda_codec *codec;
	int line_mode;

	line_mode = LINE_MODE_NONE;
	codec = NULL;
	while (get_line_from_fw(buf, sizeof(buf) - 1, &fw_size, &fw_buf)) {
		if (!*buf || *buf == '#' || *buf == '\n')
			continue;
		if (*buf == '[')
			line_mode = parse_line_mode(buf, bus);
		else if (patch_items[line_mode].parser &&
			 (codec || line_mode <= LINE_MODE_CODEC))
			patch_items[line_mode].parser(buf, bus, &codec);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_load_patch);
#endif /* CONFIG_SND_HDA_PATCH_LOADER */

/*
 * sysfs entries
 */
static struct attribute *hda_dev_attrs[] = {
	&dev_attr_vendor_id.attr,
	&dev_attr_subsystem_id.attr,
	&dev_attr_revision_id.attr,
	&dev_attr_afg.attr,
	&dev_attr_mfg.attr,
	&dev_attr_vendor_name.attr,
	&dev_attr_chip_name.attr,
	&dev_attr_modelname.attr,
	&dev_attr_init_pin_configs.attr,
	&dev_attr_driver_pin_configs.attr,
#ifdef CONFIG_PM
	&dev_attr_power_on_acct.attr,
	&dev_attr_power_off_acct.attr,
#endif
#ifdef CONFIG_SND_HDA_RECONFIG
	&dev_attr_init_verbs.attr,
	&dev_attr_hints.attr,
	&dev_attr_user_pin_configs.attr,
	&dev_attr_reconfig.attr,
	&dev_attr_clear.attr,
#endif
	NULL
};

static struct attribute_group hda_dev_attr_group = {
	.attrs	= hda_dev_attrs,
};

const struct attribute_group *snd_hda_dev_attr_groups[] = {
	&hda_dev_attr_group,
	NULL
};

void snd_hda_sysfs_init(struct hda_codec *codec)
{
	mutex_init(&codec->user_mutex);
#ifdef CONFIG_SND_HDA_RECONFIG
	snd_array_init(&codec->init_verbs, sizeof(struct hda_verb), 32);
	snd_array_init(&codec->hints, sizeof(struct hda_hint), 32);
	snd_array_init(&codec->user_pins, sizeof(struct hda_pincfg), 16);
#endif
}

void snd_hda_sysfs_clear(struct hda_codec *codec)
{
#ifdef CONFIG_SND_HDA_RECONFIG
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
#endif
}
