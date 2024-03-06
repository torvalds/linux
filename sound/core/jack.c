// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Jack abstraction layer
 *
 *  Copyright 2008 Wolfson Microelectronics
 */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/debugfs.h>
#include <sound/jack.h>
#include <sound/core.h>
#include <sound/control.h>

struct snd_jack_kctl {
	struct snd_kcontrol *kctl;
	struct list_head list;  /* list of controls belong to the same jack */
	unsigned int mask_bits; /* only masked status bits are reported via kctl */
	struct snd_jack *jack;  /* pointer to struct snd_jack */
	bool sw_inject_enable;  /* allow to inject plug event via debugfs */
#ifdef CONFIG_SND_JACK_INJECTION_DEBUG
	struct dentry *jack_debugfs_root; /* jack_kctl debugfs root */
#endif
};

#ifdef CONFIG_SND_JACK_INPUT_DEV
static const int jack_switch_types[SND_JACK_SWITCH_TYPES] = {
	SW_HEADPHONE_INSERT,
	SW_MICROPHONE_INSERT,
	SW_LINEOUT_INSERT,
	SW_JACK_PHYSICAL_INSERT,
	SW_VIDEOOUT_INSERT,
	SW_LINEIN_INSERT,
};
#endif /* CONFIG_SND_JACK_INPUT_DEV */

static int snd_jack_dev_disconnect(struct snd_device *device)
{
#ifdef CONFIG_SND_JACK_INPUT_DEV
	struct snd_jack *jack = device->device_data;

	mutex_lock(&jack->input_dev_lock);
	if (!jack->input_dev) {
		mutex_unlock(&jack->input_dev_lock);
		return 0;
	}

	/* If the input device is registered with the input subsystem
	 * then we need to use a different deallocator. */
	if (jack->registered)
		input_unregister_device(jack->input_dev);
	else
		input_free_device(jack->input_dev);
	jack->input_dev = NULL;
	mutex_unlock(&jack->input_dev_lock);
#endif /* CONFIG_SND_JACK_INPUT_DEV */
	return 0;
}

static int snd_jack_dev_free(struct snd_device *device)
{
	struct snd_jack *jack = device->device_data;
	struct snd_card *card = device->card;
	struct snd_jack_kctl *jack_kctl, *tmp_jack_kctl;

	list_for_each_entry_safe(jack_kctl, tmp_jack_kctl, &jack->kctl_list, list) {
		list_del_init(&jack_kctl->list);
		snd_ctl_remove(card, jack_kctl->kctl);
	}

	if (jack->private_free)
		jack->private_free(jack);

	snd_jack_dev_disconnect(device);

	kfree(jack->id);
	kfree(jack);

	return 0;
}

#ifdef CONFIG_SND_JACK_INPUT_DEV
static int snd_jack_dev_register(struct snd_device *device)
{
	struct snd_jack *jack = device->device_data;
	struct snd_card *card = device->card;
	int err, i;

	snprintf(jack->name, sizeof(jack->name), "%s %s",
		 card->shortname, jack->id);

	mutex_lock(&jack->input_dev_lock);
	if (!jack->input_dev) {
		mutex_unlock(&jack->input_dev_lock);
		return 0;
	}

	jack->input_dev->name = jack->name;

	/* Default to the sound card device. */
	if (!jack->input_dev->dev.parent)
		jack->input_dev->dev.parent = snd_card_get_device_link(card);

	/* Add capabilities for any keys that are enabled */
	for (i = 0; i < ARRAY_SIZE(jack->key); i++) {
		int testbit = SND_JACK_BTN_0 >> i;

		if (!(jack->type & testbit))
			continue;

		if (!jack->key[i])
			jack->key[i] = BTN_0 + i;

		input_set_capability(jack->input_dev, EV_KEY, jack->key[i]);
	}

	err = input_register_device(jack->input_dev);
	if (err == 0)
		jack->registered = 1;

	mutex_unlock(&jack->input_dev_lock);
	return err;
}
#endif /* CONFIG_SND_JACK_INPUT_DEV */

#ifdef CONFIG_SND_JACK_INJECTION_DEBUG
static void snd_jack_inject_report(struct snd_jack_kctl *jack_kctl, int status)
{
	struct snd_jack *jack;
#ifdef CONFIG_SND_JACK_INPUT_DEV
	int i;
#endif
	if (!jack_kctl)
		return;

	jack = jack_kctl->jack;

	if (jack_kctl->sw_inject_enable)
		snd_kctl_jack_report(jack->card, jack_kctl->kctl,
				     status & jack_kctl->mask_bits);

#ifdef CONFIG_SND_JACK_INPUT_DEV
	if (!jack->input_dev)
		return;

	for (i = 0; i < ARRAY_SIZE(jack->key); i++) {
		int testbit = ((SND_JACK_BTN_0 >> i) & jack_kctl->mask_bits);

		if (jack->type & testbit)
			input_report_key(jack->input_dev, jack->key[i],
					 status & testbit);
	}

	for (i = 0; i < ARRAY_SIZE(jack_switch_types); i++) {
		int testbit = ((1 << i) & jack_kctl->mask_bits);

		if (jack->type & testbit)
			input_report_switch(jack->input_dev,
					    jack_switch_types[i],
					    status & testbit);
	}

	input_sync(jack->input_dev);
#endif /* CONFIG_SND_JACK_INPUT_DEV */
}

static ssize_t sw_inject_enable_read(struct file *file,
				     char __user *to, size_t count, loff_t *ppos)
{
	struct snd_jack_kctl *jack_kctl = file->private_data;
	int len, ret;
	char buf[128];

	len = scnprintf(buf, sizeof(buf), "%s: %s\t\t%s: %i\n", "Jack", jack_kctl->kctl->id.name,
			"Inject Enabled", jack_kctl->sw_inject_enable);
	ret = simple_read_from_buffer(to, count, ppos, buf, len);

	return ret;
}

static ssize_t sw_inject_enable_write(struct file *file,
				      const char __user *from, size_t count, loff_t *ppos)
{
	struct snd_jack_kctl *jack_kctl = file->private_data;
	int ret, err;
	unsigned long enable;
	char buf[8] = { 0 };

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, from, count);
	err = kstrtoul(buf, 0, &enable);
	if (err)
		return err;

	if (jack_kctl->sw_inject_enable == (!!enable))
		return ret;

	jack_kctl->sw_inject_enable = !!enable;

	if (!jack_kctl->sw_inject_enable)
		snd_jack_report(jack_kctl->jack, jack_kctl->jack->hw_status_cache);

	return ret;
}

static ssize_t jackin_inject_write(struct file *file,
				   const char __user *from, size_t count, loff_t *ppos)
{
	struct snd_jack_kctl *jack_kctl = file->private_data;
	int ret, err;
	unsigned long enable;
	char buf[8] = { 0 };

	if (!jack_kctl->sw_inject_enable)
		return -EINVAL;

	ret = simple_write_to_buffer(buf, sizeof(buf) - 1, ppos, from, count);
	err = kstrtoul(buf, 0, &enable);
	if (err)
		return err;

	snd_jack_inject_report(jack_kctl, !!enable ? jack_kctl->mask_bits : 0);

	return ret;
}

static ssize_t jack_kctl_id_read(struct file *file,
				 char __user *to, size_t count, loff_t *ppos)
{
	struct snd_jack_kctl *jack_kctl = file->private_data;
	char buf[64];
	int len, ret;

	len = scnprintf(buf, sizeof(buf), "%s\n", jack_kctl->kctl->id.name);
	ret = simple_read_from_buffer(to, count, ppos, buf, len);

	return ret;
}

/* the bit definition is aligned with snd_jack_types in jack.h */
static const char * const jack_events_name[] = {
	"HEADPHONE(0x0001)", "MICROPHONE(0x0002)", "LINEOUT(0x0004)",
	"MECHANICAL(0x0008)", "VIDEOOUT(0x0010)", "LINEIN(0x0020)",
	"", "", "", "BTN_5(0x0200)", "BTN_4(0x0400)", "BTN_3(0x0800)",
	"BTN_2(0x1000)", "BTN_1(0x2000)", "BTN_0(0x4000)", "",
};

/* the recommended buffer size is 256 */
static int parse_mask_bits(unsigned int mask_bits, char *buf, size_t buf_size)
{
	int i;

	scnprintf(buf, buf_size, "0x%04x", mask_bits);

	for (i = 0; i < ARRAY_SIZE(jack_events_name); i++)
		if (mask_bits & (1 << i)) {
			strlcat(buf, " ", buf_size);
			strlcat(buf, jack_events_name[i], buf_size);
		}
	strlcat(buf, "\n", buf_size);

	return strlen(buf);
}

static ssize_t jack_kctl_mask_bits_read(struct file *file,
					char __user *to, size_t count, loff_t *ppos)
{
	struct snd_jack_kctl *jack_kctl = file->private_data;
	char buf[256];
	int len, ret;

	len = parse_mask_bits(jack_kctl->mask_bits, buf, sizeof(buf));
	ret = simple_read_from_buffer(to, count, ppos, buf, len);

	return ret;
}

static ssize_t jack_kctl_status_read(struct file *file,
				     char __user *to, size_t count, loff_t *ppos)
{
	struct snd_jack_kctl *jack_kctl = file->private_data;
	char buf[16];
	int len, ret;

	len = scnprintf(buf, sizeof(buf), "%s\n", jack_kctl->kctl->private_value ?
			"Plugged" : "Unplugged");
	ret = simple_read_from_buffer(to, count, ppos, buf, len);

	return ret;
}

#ifdef CONFIG_SND_JACK_INPUT_DEV
static ssize_t jack_type_read(struct file *file,
			      char __user *to, size_t count, loff_t *ppos)
{
	struct snd_jack_kctl *jack_kctl = file->private_data;
	char buf[256];
	int len, ret;

	len = parse_mask_bits(jack_kctl->jack->type, buf, sizeof(buf));
	ret = simple_read_from_buffer(to, count, ppos, buf, len);

	return ret;
}

static const struct file_operations jack_type_fops = {
	.open = simple_open,
	.read = jack_type_read,
	.llseek = default_llseek,
};
#endif

static const struct file_operations sw_inject_enable_fops = {
	.open = simple_open,
	.read = sw_inject_enable_read,
	.write = sw_inject_enable_write,
	.llseek = default_llseek,
};

static const struct file_operations jackin_inject_fops = {
	.open = simple_open,
	.write = jackin_inject_write,
	.llseek = default_llseek,
};

static const struct file_operations jack_kctl_id_fops = {
	.open = simple_open,
	.read = jack_kctl_id_read,
	.llseek = default_llseek,
};

static const struct file_operations jack_kctl_mask_bits_fops = {
	.open = simple_open,
	.read = jack_kctl_mask_bits_read,
	.llseek = default_llseek,
};

static const struct file_operations jack_kctl_status_fops = {
	.open = simple_open,
	.read = jack_kctl_status_read,
	.llseek = default_llseek,
};

static int snd_jack_debugfs_add_inject_node(struct snd_jack *jack,
					    struct snd_jack_kctl *jack_kctl)
{
	char *tname;
	int i;

	/* Don't create injection interface for Phantom jacks */
	if (strstr(jack_kctl->kctl->id.name, "Phantom"))
		return 0;

	tname = kstrdup(jack_kctl->kctl->id.name, GFP_KERNEL);
	if (!tname)
		return -ENOMEM;

	/* replace the chars which are not suitable for folder's name with _ */
	for (i = 0; tname[i]; i++)
		if (!isalnum(tname[i]))
			tname[i] = '_';

	jack_kctl->jack_debugfs_root = debugfs_create_dir(tname, jack->card->debugfs_root);
	kfree(tname);

	debugfs_create_file("sw_inject_enable", 0644, jack_kctl->jack_debugfs_root, jack_kctl,
			    &sw_inject_enable_fops);

	debugfs_create_file("jackin_inject", 0200, jack_kctl->jack_debugfs_root, jack_kctl,
			    &jackin_inject_fops);

	debugfs_create_file("kctl_id", 0444, jack_kctl->jack_debugfs_root, jack_kctl,
			    &jack_kctl_id_fops);

	debugfs_create_file("mask_bits", 0444, jack_kctl->jack_debugfs_root, jack_kctl,
			    &jack_kctl_mask_bits_fops);

	debugfs_create_file("status", 0444, jack_kctl->jack_debugfs_root, jack_kctl,
			    &jack_kctl_status_fops);

#ifdef CONFIG_SND_JACK_INPUT_DEV
	debugfs_create_file("type", 0444, jack_kctl->jack_debugfs_root, jack_kctl,
			    &jack_type_fops);
#endif
	return 0;
}

static void snd_jack_debugfs_clear_inject_node(struct snd_jack_kctl *jack_kctl)
{
	debugfs_remove(jack_kctl->jack_debugfs_root);
	jack_kctl->jack_debugfs_root = NULL;
}
#else /* CONFIG_SND_JACK_INJECTION_DEBUG */
static int snd_jack_debugfs_add_inject_node(struct snd_jack *jack,
					    struct snd_jack_kctl *jack_kctl)
{
	return 0;
}

static void snd_jack_debugfs_clear_inject_node(struct snd_jack_kctl *jack_kctl)
{
}
#endif /* CONFIG_SND_JACK_INJECTION_DEBUG */

static void snd_jack_kctl_private_free(struct snd_kcontrol *kctl)
{
	struct snd_jack_kctl *jack_kctl;

	jack_kctl = kctl->private_data;
	if (jack_kctl) {
		snd_jack_debugfs_clear_inject_node(jack_kctl);
		list_del(&jack_kctl->list);
		kfree(jack_kctl);
	}
}

static void snd_jack_kctl_add(struct snd_jack *jack, struct snd_jack_kctl *jack_kctl)
{
	jack_kctl->jack = jack;
	list_add_tail(&jack_kctl->list, &jack->kctl_list);
	snd_jack_debugfs_add_inject_node(jack, jack_kctl);
}

static struct snd_jack_kctl * snd_jack_kctl_new(struct snd_card *card, const char *name, unsigned int mask)
{
	struct snd_kcontrol *kctl;
	struct snd_jack_kctl *jack_kctl;
	int err;

	kctl = snd_kctl_jack_new(name, card);
	if (!kctl)
		return NULL;

	err = snd_ctl_add(card, kctl);
	if (err < 0)
		return NULL;

	jack_kctl = kzalloc(sizeof(*jack_kctl), GFP_KERNEL);

	if (!jack_kctl)
		goto error;

	jack_kctl->kctl = kctl;
	jack_kctl->mask_bits = mask;

	kctl->private_data = jack_kctl;
	kctl->private_free = snd_jack_kctl_private_free;

	return jack_kctl;
error:
	snd_ctl_free_one(kctl);
	return NULL;
}

/**
 * snd_jack_add_new_kctl - Create a new snd_jack_kctl and add it to jack
 * @jack:  the jack instance which the kctl will attaching to
 * @name:  the name for the snd_kcontrol object
 * @mask:  a bitmask of enum snd_jack_type values that can be detected
 *         by this snd_jack_kctl object.
 *
 * Creates a new snd_kcontrol object and adds it to the jack kctl_list.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_jack_add_new_kctl(struct snd_jack *jack, const char * name, int mask)
{
	struct snd_jack_kctl *jack_kctl;

	jack_kctl = snd_jack_kctl_new(jack->card, name, mask);
	if (!jack_kctl)
		return -ENOMEM;

	snd_jack_kctl_add(jack, jack_kctl);
	return 0;
}
EXPORT_SYMBOL(snd_jack_add_new_kctl);

/**
 * snd_jack_new - Create a new jack
 * @card:  the card instance
 * @id:    an identifying string for this jack
 * @type:  a bitmask of enum snd_jack_type values that can be detected by
 *         this jack
 * @jjack: Used to provide the allocated jack object to the caller.
 * @initial_kctl: if true, create a kcontrol and add it to the jack list.
 * @phantom_jack: Don't create a input device for phantom jacks.
 *
 * Creates a new jack object.
 *
 * Return: Zero if successful, or a negative error code on failure.
 * On success @jjack will be initialised.
 */
int snd_jack_new(struct snd_card *card, const char *id, int type,
		 struct snd_jack **jjack, bool initial_kctl, bool phantom_jack)
{
	struct snd_jack *jack;
	struct snd_jack_kctl *jack_kctl = NULL;
	int err;
	static const struct snd_device_ops ops = {
		.dev_free = snd_jack_dev_free,
#ifdef CONFIG_SND_JACK_INPUT_DEV
		.dev_register = snd_jack_dev_register,
		.dev_disconnect = snd_jack_dev_disconnect,
#endif /* CONFIG_SND_JACK_INPUT_DEV */
	};

	if (initial_kctl) {
		jack_kctl = snd_jack_kctl_new(card, id, type);
		if (!jack_kctl)
			return -ENOMEM;
	}

	jack = kzalloc(sizeof(struct snd_jack), GFP_KERNEL);
	if (jack == NULL)
		return -ENOMEM;

	jack->id = kstrdup(id, GFP_KERNEL);
	if (jack->id == NULL) {
		kfree(jack);
		return -ENOMEM;
	}

#ifdef CONFIG_SND_JACK_INPUT_DEV
	mutex_init(&jack->input_dev_lock);

	/* don't create input device for phantom jack */
	if (!phantom_jack) {
		int i;

		jack->input_dev = input_allocate_device();
		if (jack->input_dev == NULL) {
			err = -ENOMEM;
			goto fail_input;
		}

		jack->input_dev->phys = "ALSA";

		jack->type = type;

		for (i = 0; i < SND_JACK_SWITCH_TYPES; i++)
			if (type & (1 << i))
				input_set_capability(jack->input_dev, EV_SW,
						     jack_switch_types[i]);

	}
#endif /* CONFIG_SND_JACK_INPUT_DEV */

	err = snd_device_new(card, SNDRV_DEV_JACK, jack, &ops);
	if (err < 0)
		goto fail_input;

	jack->card = card;
	INIT_LIST_HEAD(&jack->kctl_list);

	if (initial_kctl)
		snd_jack_kctl_add(jack, jack_kctl);

	*jjack = jack;

	return 0;

fail_input:
#ifdef CONFIG_SND_JACK_INPUT_DEV
	input_free_device(jack->input_dev);
#endif
	kfree(jack->id);
	kfree(jack);
	return err;
}
EXPORT_SYMBOL(snd_jack_new);

#ifdef CONFIG_SND_JACK_INPUT_DEV
/**
 * snd_jack_set_parent - Set the parent device for a jack
 *
 * @jack:   The jack to configure
 * @parent: The device to set as parent for the jack.
 *
 * Set the parent for the jack devices in the device tree.  This
 * function is only valid prior to registration of the jack.  If no
 * parent is configured then the parent device will be the sound card.
 */
void snd_jack_set_parent(struct snd_jack *jack, struct device *parent)
{
	WARN_ON(jack->registered);
	mutex_lock(&jack->input_dev_lock);
	if (!jack->input_dev) {
		mutex_unlock(&jack->input_dev_lock);
		return;
	}

	jack->input_dev->dev.parent = parent;
	mutex_unlock(&jack->input_dev_lock);
}
EXPORT_SYMBOL(snd_jack_set_parent);

/**
 * snd_jack_set_key - Set a key mapping on a jack
 *
 * @jack:    The jack to configure
 * @type:    Jack report type for this key
 * @keytype: Input layer key type to be reported
 *
 * Map a SND_JACK_BTN_* button type to an input layer key, allowing
 * reporting of keys on accessories via the jack abstraction.  If no
 * mapping is provided but keys are enabled in the jack type then
 * BTN_n numeric buttons will be reported.
 *
 * If jacks are not reporting via the input API this call will have no
 * effect.
 *
 * Note that this is intended to be use by simple devices with small
 * numbers of keys that can be reported.  It is also possible to
 * access the input device directly - devices with complex input
 * capabilities on accessories should consider doing this rather than
 * using this abstraction.
 *
 * This function may only be called prior to registration of the jack.
 *
 * Return: Zero if successful, or a negative error code on failure.
 */
int snd_jack_set_key(struct snd_jack *jack, enum snd_jack_types type,
		     int keytype)
{
	int key = fls(SND_JACK_BTN_0) - fls(type);

	WARN_ON(jack->registered);

	if (!keytype || key >= ARRAY_SIZE(jack->key))
		return -EINVAL;

	jack->type |= type;
	jack->key[key] = keytype;
	return 0;
}
EXPORT_SYMBOL(snd_jack_set_key);
#endif /* CONFIG_SND_JACK_INPUT_DEV */

/**
 * snd_jack_report - Report the current status of a jack
 * Note: This function uses mutexes and should be called from a
 * context which can sleep (such as a workqueue).
 *
 * @jack:   The jack to report status for
 * @status: The current status of the jack
 */
void snd_jack_report(struct snd_jack *jack, int status)
{
	struct snd_jack_kctl *jack_kctl;
	unsigned int mask_bits = 0;
#ifdef CONFIG_SND_JACK_INPUT_DEV
	struct input_dev *idev;
	int i;
#endif

	if (!jack)
		return;

	jack->hw_status_cache = status;

	list_for_each_entry(jack_kctl, &jack->kctl_list, list)
		if (jack_kctl->sw_inject_enable)
			mask_bits |= jack_kctl->mask_bits;
		else
			snd_kctl_jack_report(jack->card, jack_kctl->kctl,
					     status & jack_kctl->mask_bits);

#ifdef CONFIG_SND_JACK_INPUT_DEV
	idev = input_get_device(jack->input_dev);
	if (!idev)
		return;

	for (i = 0; i < ARRAY_SIZE(jack->key); i++) {
		int testbit = ((SND_JACK_BTN_0 >> i) & ~mask_bits);

		if (jack->type & testbit)
			input_report_key(idev, jack->key[i],
					 status & testbit);
	}

	for (i = 0; i < ARRAY_SIZE(jack_switch_types); i++) {
		int testbit = ((1 << i) & ~mask_bits);

		if (jack->type & testbit)
			input_report_switch(idev,
					    jack_switch_types[i],
					    status & testbit);
	}

	input_sync(idev);
	input_put_device(idev);
#endif /* CONFIG_SND_JACK_INPUT_DEV */
}
EXPORT_SYMBOL(snd_jack_report);
