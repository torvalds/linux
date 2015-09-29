/*
 * sysfs support for HD-audio core device
 */

#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <sound/core.h>
#include <sound/hdaudio.h>
#include "local.h"

struct hdac_widget_tree {
	struct kobject *root;
	struct kobject *afg;
	struct kobject **nodes;
};

#define CODEC_ATTR(type)					\
static ssize_t type##_show(struct device *dev,			\
			   struct device_attribute *attr,	\
			   char *buf)				\
{								\
	struct hdac_device *codec = dev_to_hdac_dev(dev);	\
	return sprintf(buf, "0x%x\n", codec->type);		\
} \
static DEVICE_ATTR_RO(type)

#define CODEC_ATTR_STR(type)					\
static ssize_t type##_show(struct device *dev,			\
			     struct device_attribute *attr,	\
					char *buf)		\
{								\
	struct hdac_device *codec = dev_to_hdac_dev(dev);	\
	return sprintf(buf, "%s\n",				\
		       codec->type ? codec->type : "");		\
} \
static DEVICE_ATTR_RO(type)

CODEC_ATTR(type);
CODEC_ATTR(vendor_id);
CODEC_ATTR(subsystem_id);
CODEC_ATTR(revision_id);
CODEC_ATTR(afg);
CODEC_ATTR(mfg);
CODEC_ATTR_STR(vendor_name);
CODEC_ATTR_STR(chip_name);

static ssize_t modalias_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	return snd_hdac_codec_modalias(dev_to_hdac_dev(dev), buf, 256);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *hdac_dev_attrs[] = {
	&dev_attr_type.attr,
	&dev_attr_vendor_id.attr,
	&dev_attr_subsystem_id.attr,
	&dev_attr_revision_id.attr,
	&dev_attr_afg.attr,
	&dev_attr_mfg.attr,
	&dev_attr_vendor_name.attr,
	&dev_attr_chip_name.attr,
	&dev_attr_modalias.attr,
	NULL
};

static struct attribute_group hdac_dev_attr_group = {
	.attrs	= hdac_dev_attrs,
};

const struct attribute_group *hdac_dev_attr_groups[] = {
	&hdac_dev_attr_group,
	NULL
};

/*
 * Widget tree sysfs
 *
 * This is a tree showing the attributes of each widget.  It appears like
 * /sys/bus/hdaudioC0D0/widgets/04/caps
 */

struct widget_attribute;

struct widget_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct hdac_device *codec, hda_nid_t nid,
			struct widget_attribute *attr, char *buf);
	ssize_t (*store)(struct hdac_device *codec, hda_nid_t nid,
			 struct widget_attribute *attr,
			 const char *buf, size_t count);
};

static int get_codec_nid(struct kobject *kobj, struct hdac_device **codecp)
{
	struct device *dev = kobj_to_dev(kobj->parent->parent);
	int nid;
	ssize_t ret;

	ret = kstrtoint(kobj->name, 16, &nid);
	if (ret < 0)
		return ret;
	*codecp = dev_to_hdac_dev(dev);
	return nid;
}

static ssize_t widget_attr_show(struct kobject *kobj, struct attribute *attr,
				char *buf)
{
	struct widget_attribute *wid_attr =
		container_of(attr, struct widget_attribute, attr);
	struct hdac_device *codec;
	int nid;

	if (!wid_attr->show)
		return -EIO;
	nid = get_codec_nid(kobj, &codec);
	if (nid < 0)
		return nid;
	return wid_attr->show(codec, nid, wid_attr, buf);
}

static ssize_t widget_attr_store(struct kobject *kobj, struct attribute *attr,
				 const char *buf, size_t count)
{
	struct widget_attribute *wid_attr =
		container_of(attr, struct widget_attribute, attr);
	struct hdac_device *codec;
	int nid;

	if (!wid_attr->store)
		return -EIO;
	nid = get_codec_nid(kobj, &codec);
	if (nid < 0)
		return nid;
	return wid_attr->store(codec, nid, wid_attr, buf, count);
}

static const struct sysfs_ops widget_sysfs_ops = {
	.show	= widget_attr_show,
	.store	= widget_attr_store,
};

static void widget_release(struct kobject *kobj)
{
	kfree(kobj);
}

static struct kobj_type widget_ktype = {
	.release	= widget_release,
	.sysfs_ops	= &widget_sysfs_ops,
};

#define WIDGET_ATTR_RO(_name) \
	struct widget_attribute wid_attr_##_name = __ATTR_RO(_name)
#define WIDGET_ATTR_RW(_name) \
	struct widget_attribute wid_attr_##_name = __ATTR_RW(_name)

static ssize_t caps_show(struct hdac_device *codec, hda_nid_t nid,
			struct widget_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%08x\n", get_wcaps(codec, nid));
}

static ssize_t pin_caps_show(struct hdac_device *codec, hda_nid_t nid,
			     struct widget_attribute *attr, char *buf)
{
	if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_PIN)
		return 0;
	return sprintf(buf, "0x%08x\n",
		       snd_hdac_read_parm(codec, nid, AC_PAR_PIN_CAP));
}

static ssize_t pin_cfg_show(struct hdac_device *codec, hda_nid_t nid,
			    struct widget_attribute *attr, char *buf)
{
	unsigned int val;

	if (get_wcaps_type(get_wcaps(codec, nid)) != AC_WID_PIN)
		return 0;
	if (snd_hdac_read(codec, nid, AC_VERB_GET_CONFIG_DEFAULT, 0, &val))
		return 0;
	return sprintf(buf, "0x%08x\n", val);
}

static bool has_pcm_cap(struct hdac_device *codec, hda_nid_t nid)
{
	if (nid == codec->afg || nid == codec->mfg)
		return true;
	switch (get_wcaps_type(get_wcaps(codec, nid))) {
	case AC_WID_AUD_OUT:
	case AC_WID_AUD_IN:
		return true;
	default:
		return false;
	}
}

static ssize_t pcm_caps_show(struct hdac_device *codec, hda_nid_t nid,
			     struct widget_attribute *attr, char *buf)
{
	if (!has_pcm_cap(codec, nid))
		return 0;
	return sprintf(buf, "0x%08x\n",
		       snd_hdac_read_parm(codec, nid, AC_PAR_PCM));
}

static ssize_t pcm_formats_show(struct hdac_device *codec, hda_nid_t nid,
				struct widget_attribute *attr, char *buf)
{
	if (!has_pcm_cap(codec, nid))
		return 0;
	return sprintf(buf, "0x%08x\n",
		       snd_hdac_read_parm(codec, nid, AC_PAR_STREAM));
}

static ssize_t amp_in_caps_show(struct hdac_device *codec, hda_nid_t nid,
				struct widget_attribute *attr, char *buf)
{
	if (nid != codec->afg && !(get_wcaps(codec, nid) & AC_WCAP_IN_AMP))
		return 0;
	return sprintf(buf, "0x%08x\n",
		       snd_hdac_read_parm(codec, nid, AC_PAR_AMP_IN_CAP));
}

static ssize_t amp_out_caps_show(struct hdac_device *codec, hda_nid_t nid,
				 struct widget_attribute *attr, char *buf)
{
	if (nid != codec->afg && !(get_wcaps(codec, nid) & AC_WCAP_OUT_AMP))
		return 0;
	return sprintf(buf, "0x%08x\n",
		       snd_hdac_read_parm(codec, nid, AC_PAR_AMP_OUT_CAP));
}

static ssize_t power_caps_show(struct hdac_device *codec, hda_nid_t nid,
			       struct widget_attribute *attr, char *buf)
{
	if (nid != codec->afg && !(get_wcaps(codec, nid) & AC_WCAP_POWER))
		return 0;
	return sprintf(buf, "0x%08x\n",
		       snd_hdac_read_parm(codec, nid, AC_PAR_POWER_STATE));
}

static ssize_t gpio_caps_show(struct hdac_device *codec, hda_nid_t nid,
			      struct widget_attribute *attr, char *buf)
{
	return sprintf(buf, "0x%08x\n",
		       snd_hdac_read_parm(codec, nid, AC_PAR_GPIO_CAP));
}

static ssize_t connections_show(struct hdac_device *codec, hda_nid_t nid,
				struct widget_attribute *attr, char *buf)
{
	hda_nid_t list[32];
	int i, nconns;
	ssize_t ret = 0;

	nconns = snd_hdac_get_connections(codec, nid, list, ARRAY_SIZE(list));
	if (nconns <= 0)
		return nconns;
	for (i = 0; i < nconns; i++)
		ret += sprintf(buf + ret, "%s0x%02x", i ? " " : "", list[i]);
	ret += sprintf(buf + ret, "\n");
	return ret;
}

static WIDGET_ATTR_RO(caps);
static WIDGET_ATTR_RO(pin_caps);
static WIDGET_ATTR_RO(pin_cfg);
static WIDGET_ATTR_RO(pcm_caps);
static WIDGET_ATTR_RO(pcm_formats);
static WIDGET_ATTR_RO(amp_in_caps);
static WIDGET_ATTR_RO(amp_out_caps);
static WIDGET_ATTR_RO(power_caps);
static WIDGET_ATTR_RO(gpio_caps);
static WIDGET_ATTR_RO(connections);

static struct attribute *widget_node_attrs[] = {
	&wid_attr_caps.attr,
	&wid_attr_pin_caps.attr,
	&wid_attr_pin_cfg.attr,
	&wid_attr_pcm_caps.attr,
	&wid_attr_pcm_formats.attr,
	&wid_attr_amp_in_caps.attr,
	&wid_attr_amp_out_caps.attr,
	&wid_attr_power_caps.attr,
	&wid_attr_connections.attr,
	NULL,
};

static struct attribute *widget_afg_attrs[] = {
	&wid_attr_pcm_caps.attr,
	&wid_attr_pcm_formats.attr,
	&wid_attr_amp_in_caps.attr,
	&wid_attr_amp_out_caps.attr,
	&wid_attr_power_caps.attr,
	&wid_attr_gpio_caps.attr,
	NULL,
};

static const struct attribute_group widget_node_group = {
	.attrs = widget_node_attrs,
};

static const struct attribute_group widget_afg_group = {
	.attrs = widget_afg_attrs,
};

static void free_widget_node(struct kobject *kobj,
			     const struct attribute_group *group)
{
	if (kobj) {
		sysfs_remove_group(kobj, group);
		kobject_put(kobj);
	}
}

static void widget_tree_free(struct hdac_device *codec)
{
	struct hdac_widget_tree *tree = codec->widgets;
	struct kobject **p;

	if (!tree)
		return;
	free_widget_node(tree->afg, &widget_afg_group);
	if (tree->nodes) {
		for (p = tree->nodes; *p; p++)
			free_widget_node(*p, &widget_node_group);
		kfree(tree->nodes);
	}
	kobject_put(tree->root);
	kfree(tree);
	codec->widgets = NULL;
}

static int add_widget_node(struct kobject *parent, hda_nid_t nid,
			   const struct attribute_group *group,
			   struct kobject **res)
{
	struct kobject *kobj = kzalloc(sizeof(*kobj), GFP_KERNEL);
	int err;

	if (!kobj)
		return -ENOMEM;
	kobject_init(kobj, &widget_ktype);
	err = kobject_add(kobj, parent, "%02x", nid);
	if (err < 0)
		return err;
	err = sysfs_create_group(kobj, group);
	if (err < 0) {
		kobject_put(kobj);
		return err;
	}

	*res = kobj;
	return 0;
}

static int widget_tree_create(struct hdac_device *codec)
{
	struct hdac_widget_tree *tree;
	int i, err;
	hda_nid_t nid;

	tree = codec->widgets = kzalloc(sizeof(*tree), GFP_KERNEL);
	if (!tree)
		return -ENOMEM;

	tree->root = kobject_create_and_add("widgets", &codec->dev.kobj);
	if (!tree->root)
		return -ENOMEM;

	tree->nodes = kcalloc(codec->num_nodes + 1, sizeof(*tree->nodes),
			      GFP_KERNEL);
	if (!tree->nodes)
		return -ENOMEM;

	for (i = 0, nid = codec->start_nid; i < codec->num_nodes; i++, nid++) {
		err = add_widget_node(tree->root, nid, &widget_node_group,
				      &tree->nodes[i]);
		if (err < 0)
			return err;
	}

	if (codec->afg) {
		err = add_widget_node(tree->root, codec->afg,
				      &widget_afg_group, &tree->afg);
		if (err < 0)
			return err;
	}

	kobject_uevent(tree->root, KOBJ_CHANGE);
	return 0;
}

int hda_widget_sysfs_init(struct hdac_device *codec)
{
	int err;

	if (codec->widgets)
		return 0; /* already created */

	err = widget_tree_create(codec);
	if (err < 0) {
		widget_tree_free(codec);
		return err;
	}

	return 0;
}

void hda_widget_sysfs_exit(struct hdac_device *codec)
{
	widget_tree_free(codec);
}
