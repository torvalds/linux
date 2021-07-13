/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __SOUND_CORE_H
#define __SOUND_CORE_H

/*
 *  Main header file for the ALSA driver
 *  Copyright (c) 1994-2001 by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/device.h>
#include <linux/sched.h>		/* wake_up() */
#include <linux/mutex.h>		/* struct mutex */
#include <linux/rwsem.h>		/* struct rw_semaphore */
#include <linux/pm.h>			/* pm_message_t */
#include <linux/stringify.h>
#include <linux/printk.h>

/* number of supported soundcards */
#ifdef CONFIG_SND_DYNAMIC_MINORS
#define SNDRV_CARDS CONFIG_SND_MAX_CARDS
#else
#define SNDRV_CARDS 8		/* don't change - minor numbers */
#endif

#define CONFIG_SND_MAJOR	116	/* standard configuration */

/* forward declarations */
struct pci_dev;
struct module;
struct completion;

/* device allocation stuff */

/* type of the object used in snd_device_*()
 * this also defines the calling order
 */
enum snd_device_type {
	SNDRV_DEV_LOWLEVEL,
	SNDRV_DEV_INFO,
	SNDRV_DEV_BUS,
	SNDRV_DEV_CODEC,
	SNDRV_DEV_PCM,
	SNDRV_DEV_COMPRESS,
	SNDRV_DEV_RAWMIDI,
	SNDRV_DEV_TIMER,
	SNDRV_DEV_SEQUENCER,
	SNDRV_DEV_HWDEP,
	SNDRV_DEV_JACK,
	SNDRV_DEV_CONTROL,	/* NOTE: this must be the last one */
};

enum snd_device_state {
	SNDRV_DEV_BUILD,
	SNDRV_DEV_REGISTERED,
	SNDRV_DEV_DISCONNECTED,
};

struct snd_device;

struct snd_device_ops {
	int (*dev_free)(struct snd_device *dev);
	int (*dev_register)(struct snd_device *dev);
	int (*dev_disconnect)(struct snd_device *dev);
};

struct snd_device {
	struct list_head list;		/* list of registered devices */
	struct snd_card *card;		/* card which holds this device */
	enum snd_device_state state;	/* state of the device */
	enum snd_device_type type;	/* device type */
	void *device_data;		/* device structure */
	const struct snd_device_ops *ops;	/* operations */
};

#define snd_device(n) list_entry(n, struct snd_device, list)

/* main structure for soundcard */

struct snd_card {
	int number;			/* number of soundcard (index to
								snd_cards) */

	char id[16];			/* id string of this card */
	char driver[16];		/* driver name */
	char shortname[32];		/* short name of this soundcard */
	char longname[80];		/* name of this soundcard */
	char irq_descr[32];		/* Interrupt description */
	char mixername[80];		/* mixer name */
	char components[128];		/* card components delimited with
								space */
	struct module *module;		/* top-level module */

	void *private_data;		/* private data for soundcard */
	void (*private_free) (struct snd_card *card); /* callback for freeing of
								private data */
	struct list_head devices;	/* devices */

	struct device ctl_dev;		/* control device */
	unsigned int last_numid;	/* last used numeric ID */
	struct rw_semaphore controls_rwsem;	/* controls list lock */
	rwlock_t ctl_files_rwlock;	/* ctl_files list lock */
	int controls_count;		/* count of all controls */
	size_t user_ctl_alloc_size;	// current memory allocation by user controls.
	struct list_head controls;	/* all controls for this card */
	struct list_head ctl_files;	/* active control files */

	struct snd_info_entry *proc_root;	/* root for soundcard specific files */
	struct proc_dir_entry *proc_root_link;	/* number link to real id */

	struct list_head files_list;	/* all files associated to this card */
	struct snd_shutdown_f_ops *s_f_ops; /* file operations in the shutdown
								state */
	spinlock_t files_lock;		/* lock the files for this card */
	int shutdown;			/* this card is going down */
	struct completion *release_completion;
	struct device *dev;		/* device assigned to this card */
	struct device card_dev;		/* cardX object for sysfs */
	const struct attribute_group *dev_groups[4]; /* assigned sysfs attr */
	bool registered;		/* card_dev is registered? */
	int sync_irq;			/* assigned irq, used for PCM sync */
	wait_queue_head_t remove_sleep;

	size_t total_pcm_alloc_bytes;	/* total amount of allocated buffers */
	struct mutex memory_mutex;	/* protection for the above */
#ifdef CONFIG_SND_DEBUG
	struct dentry *debugfs_root;    /* debugfs root for card */
#endif

#ifdef CONFIG_PM
	unsigned int power_state;	/* power state */
	atomic_t power_ref;
	wait_queue_head_t power_sleep;
	wait_queue_head_t power_ref_sleep;
#endif

#if IS_ENABLED(CONFIG_SND_MIXER_OSS)
	struct snd_mixer_oss *mixer_oss;
	int mixer_oss_change_count;
#endif
};

#define dev_to_snd_card(p)	container_of(p, struct snd_card, card_dev)

#ifdef CONFIG_PM
static inline unsigned int snd_power_get_state(struct snd_card *card)
{
	return READ_ONCE(card->power_state);
}

static inline void snd_power_change_state(struct snd_card *card, unsigned int state)
{
	WRITE_ONCE(card->power_state, state);
	wake_up(&card->power_sleep);
}

/**
 * snd_power_ref - Take the reference count for power control
 * @card: sound card object
 *
 * The power_ref reference of the card is used for managing to block
 * the snd_power_sync_ref() operation.  This function increments the reference.
 * The counterpart snd_power_unref() has to be called appropriately later.
 */
static inline void snd_power_ref(struct snd_card *card)
{
	atomic_inc(&card->power_ref);
}

/**
 * snd_power_unref - Release the reference count for power control
 * @card: sound card object
 */
static inline void snd_power_unref(struct snd_card *card)
{
	if (atomic_dec_and_test(&card->power_ref))
		wake_up(&card->power_ref_sleep);
}

/**
 * snd_power_sync_ref - wait until the card power_ref is freed
 * @card: sound card object
 *
 * This function is used to synchronize with the pending power_ref being
 * released.
 */
static inline void snd_power_sync_ref(struct snd_card *card)
{
	wait_event(card->power_ref_sleep, !atomic_read(&card->power_ref));
}

/* init.c */
int snd_power_wait(struct snd_card *card);
int snd_power_ref_and_wait(struct snd_card *card);

#else /* ! CONFIG_PM */

static inline int snd_power_wait(struct snd_card *card) { return 0; }
static inline void snd_power_ref(struct snd_card *card) {}
static inline void snd_power_unref(struct snd_card *card) {}
static inline int snd_power_ref_and_wait(struct snd_card *card) { return 0; }
static inline void snd_power_sync_ref(struct snd_card *card) {}
#define snd_power_get_state(card)	({ (void)(card); SNDRV_CTL_POWER_D0; })
#define snd_power_change_state(card, state)	do { (void)(card); } while (0)

#endif /* CONFIG_PM */

struct snd_minor {
	int type;			/* SNDRV_DEVICE_TYPE_XXX */
	int card;			/* card number */
	int device;			/* device number */
	const struct file_operations *f_ops;	/* file operations */
	void *private_data;		/* private data for f_ops->open */
	struct device *dev;		/* device for sysfs */
	struct snd_card *card_ptr;	/* assigned card instance */
};

/* return a device pointer linked to each sound device as a parent */
static inline struct device *snd_card_get_device_link(struct snd_card *card)
{
	return card ? &card->card_dev : NULL;
}

/* sound.c */

extern int snd_major;
extern int snd_ecards_limit;
extern struct class *sound_class;
#ifdef CONFIG_SND_DEBUG
extern struct dentry *sound_debugfs_root;
#endif

void snd_request_card(int card);

void snd_device_initialize(struct device *dev, struct snd_card *card);

int snd_register_device(int type, struct snd_card *card, int dev,
			const struct file_operations *f_ops,
			void *private_data, struct device *device);
int snd_unregister_device(struct device *dev);
void *snd_lookup_minor_data(unsigned int minor, int type);

#ifdef CONFIG_SND_OSSEMUL
int snd_register_oss_device(int type, struct snd_card *card, int dev,
			    const struct file_operations *f_ops, void *private_data);
int snd_unregister_oss_device(int type, struct snd_card *card, int dev);
void *snd_lookup_oss_minor_data(unsigned int minor, int type);
#endif

int snd_minor_info_init(void);

/* sound_oss.c */

#ifdef CONFIG_SND_OSSEMUL
int snd_minor_info_oss_init(void);
#else
static inline int snd_minor_info_oss_init(void) { return 0; }
#endif

/* memory.c */

int copy_to_user_fromio(void __user *dst, const volatile void __iomem *src, size_t count);
int copy_from_user_toio(volatile void __iomem *dst, const void __user *src, size_t count);

/* init.c */

int snd_card_locked(int card);
#if IS_ENABLED(CONFIG_SND_MIXER_OSS)
#define SND_MIXER_OSS_NOTIFY_REGISTER	0
#define SND_MIXER_OSS_NOTIFY_DISCONNECT	1
#define SND_MIXER_OSS_NOTIFY_FREE	2
extern int (*snd_mixer_oss_notify_callback)(struct snd_card *card, int cmd);
#endif

int snd_card_new(struct device *parent, int idx, const char *xid,
		 struct module *module, int extra_size,
		 struct snd_card **card_ret);

int snd_card_disconnect(struct snd_card *card);
void snd_card_disconnect_sync(struct snd_card *card);
int snd_card_free(struct snd_card *card);
int snd_card_free_when_closed(struct snd_card *card);
void snd_card_set_id(struct snd_card *card, const char *id);
int snd_card_register(struct snd_card *card);
int snd_card_info_init(void);
int snd_card_add_dev_attr(struct snd_card *card,
			  const struct attribute_group *group);
int snd_component_add(struct snd_card *card, const char *component);
int snd_card_file_add(struct snd_card *card, struct file *file);
int snd_card_file_remove(struct snd_card *card, struct file *file);

struct snd_card *snd_card_ref(int card);

/**
 * snd_card_unref - Unreference the card object
 * @card: the card object to unreference
 *
 * Call this function for the card object that was obtained via snd_card_ref()
 * or snd_lookup_minor_data().
 */
static inline void snd_card_unref(struct snd_card *card)
{
	put_device(&card->card_dev);
}

#define snd_card_set_dev(card, devptr) ((card)->dev = (devptr))

/* device.c */

int snd_device_new(struct snd_card *card, enum snd_device_type type,
		   void *device_data, const struct snd_device_ops *ops);
int snd_device_register(struct snd_card *card, void *device_data);
int snd_device_register_all(struct snd_card *card);
void snd_device_disconnect(struct snd_card *card, void *device_data);
void snd_device_disconnect_all(struct snd_card *card);
void snd_device_free(struct snd_card *card, void *device_data);
void snd_device_free_all(struct snd_card *card);
int snd_device_get_state(struct snd_card *card, void *device_data);

/* isadma.c */

#ifdef CONFIG_ISA_DMA_API
#define DMA_MODE_NO_ENABLE	0x0100

void snd_dma_program(unsigned long dma, unsigned long addr, unsigned int size, unsigned short mode);
void snd_dma_disable(unsigned long dma);
unsigned int snd_dma_pointer(unsigned long dma, unsigned int size);
#endif

/* misc.c */
struct resource;
void release_and_free_resource(struct resource *res);

/* --- */

/* sound printk debug levels */
enum {
	SND_PR_ALWAYS,
	SND_PR_DEBUG,
	SND_PR_VERBOSE,
};

#if defined(CONFIG_SND_DEBUG) || defined(CONFIG_SND_VERBOSE_PRINTK)
__printf(4, 5)
void __snd_printk(unsigned int level, const char *file, int line,
		  const char *format, ...);
#else
#define __snd_printk(level, file, line, format, ...) \
	printk(format, ##__VA_ARGS__)
#endif

/**
 * snd_printk - printk wrapper
 * @fmt: format string
 *
 * Works like printk() but prints the file and the line of the caller
 * when configured with CONFIG_SND_VERBOSE_PRINTK.
 */
#define snd_printk(fmt, ...) \
	__snd_printk(0, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#ifdef CONFIG_SND_DEBUG
/**
 * snd_printd - debug printk
 * @fmt: format string
 *
 * Works like snd_printk() for debugging purposes.
 * Ignored when CONFIG_SND_DEBUG is not set.
 */
#define snd_printd(fmt, ...) \
	__snd_printk(1, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define _snd_printd(level, fmt, ...) \
	__snd_printk(level, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/**
 * snd_BUG - give a BUG warning message and stack trace
 *
 * Calls WARN() if CONFIG_SND_DEBUG is set.
 * Ignored when CONFIG_SND_DEBUG is not set.
 */
#define snd_BUG()		WARN(1, "BUG?\n")

/**
 * snd_printd_ratelimit - Suppress high rates of output when
 * 			  CONFIG_SND_DEBUG is enabled.
 */
#define snd_printd_ratelimit() printk_ratelimit()

/**
 * snd_BUG_ON - debugging check macro
 * @cond: condition to evaluate
 *
 * Has the same behavior as WARN_ON when CONFIG_SND_DEBUG is set,
 * otherwise just evaluates the conditional and returns the value.
 */
#define snd_BUG_ON(cond)	WARN_ON((cond))

#else /* !CONFIG_SND_DEBUG */

__printf(1, 2)
static inline void snd_printd(const char *format, ...) {}
__printf(2, 3)
static inline void _snd_printd(int level, const char *format, ...) {}

#define snd_BUG()			do { } while (0)

#define snd_BUG_ON(condition) ({ \
	int __ret_warn_on = !!(condition); \
	unlikely(__ret_warn_on); \
})

static inline bool snd_printd_ratelimit(void) { return false; }

#endif /* CONFIG_SND_DEBUG */

#ifdef CONFIG_SND_DEBUG_VERBOSE
/**
 * snd_printdd - debug printk
 * @format: format string
 *
 * Works like snd_printk() for debugging purposes.
 * Ignored when CONFIG_SND_DEBUG_VERBOSE is not set.
 */
#define snd_printdd(format, ...) \
	__snd_printk(2, __FILE__, __LINE__, format, ##__VA_ARGS__)
#else
__printf(1, 2)
static inline void snd_printdd(const char *format, ...) {}
#endif


#define SNDRV_OSS_VERSION         ((3<<16)|(8<<8)|(1<<4)|(0))	/* 3.8.1a */

/* for easier backward-porting */
#if IS_ENABLED(CONFIG_GAMEPORT)
#define gameport_set_dev_parent(gp,xdev) ((gp)->dev.parent = (xdev))
#define gameport_set_port_data(gp,r) ((gp)->port_data = (r))
#define gameport_get_port_data(gp) (gp)->port_data
#endif

/* PCI quirk list helper */
struct snd_pci_quirk {
	unsigned short subvendor;	/* PCI subvendor ID */
	unsigned short subdevice;	/* PCI subdevice ID */
	unsigned short subdevice_mask;	/* bitmask to match */
	int value;			/* value */
#ifdef CONFIG_SND_DEBUG_VERBOSE
	const char *name;		/* name of the device (optional) */
#endif
};

#define _SND_PCI_QUIRK_ID_MASK(vend, mask, dev)	\
	.subvendor = (vend), .subdevice = (dev), .subdevice_mask = (mask)
#define _SND_PCI_QUIRK_ID(vend, dev) \
	_SND_PCI_QUIRK_ID_MASK(vend, 0xffff, dev)
#define SND_PCI_QUIRK_ID(vend,dev) {_SND_PCI_QUIRK_ID(vend, dev)}
#ifdef CONFIG_SND_DEBUG_VERBOSE
#define SND_PCI_QUIRK(vend,dev,xname,val) \
	{_SND_PCI_QUIRK_ID(vend, dev), .value = (val), .name = (xname)}
#define SND_PCI_QUIRK_VENDOR(vend, xname, val)			\
	{_SND_PCI_QUIRK_ID_MASK(vend, 0, 0), .value = (val), .name = (xname)}
#define SND_PCI_QUIRK_MASK(vend, mask, dev, xname, val)			\
	{_SND_PCI_QUIRK_ID_MASK(vend, mask, dev),			\
			.value = (val), .name = (xname)}
#define snd_pci_quirk_name(q)	((q)->name)
#else
#define SND_PCI_QUIRK(vend,dev,xname,val) \
	{_SND_PCI_QUIRK_ID(vend, dev), .value = (val)}
#define SND_PCI_QUIRK_MASK(vend, mask, dev, xname, val)			\
	{_SND_PCI_QUIRK_ID_MASK(vend, mask, dev), .value = (val)}
#define SND_PCI_QUIRK_VENDOR(vend, xname, val)			\
	{_SND_PCI_QUIRK_ID_MASK(vend, 0, 0), .value = (val)}
#define snd_pci_quirk_name(q)	""
#endif

#ifdef CONFIG_PCI
const struct snd_pci_quirk *
snd_pci_quirk_lookup(struct pci_dev *pci, const struct snd_pci_quirk *list);

const struct snd_pci_quirk *
snd_pci_quirk_lookup_id(u16 vendor, u16 device,
			const struct snd_pci_quirk *list);
#else
static inline const struct snd_pci_quirk *
snd_pci_quirk_lookup(struct pci_dev *pci, const struct snd_pci_quirk *list)
{
	return NULL;
}

static inline const struct snd_pci_quirk *
snd_pci_quirk_lookup_id(u16 vendor, u16 device,
			const struct snd_pci_quirk *list)
{
	return NULL;
}
#endif

#endif /* __SOUND_CORE_H */
