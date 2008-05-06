/*
 * Linux Plug and Play Support
 * Copyright by Adam Belay <ambx1@neo.rr.com>
 */

#ifndef _LINUX_PNP_H
#define _LINUX_PNP_H

#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/mod_devicetable.h>

#define PNP_NAME_LEN		50

struct pnp_protocol;
struct pnp_dev;
struct pnp_resource_table;

/*
 * Resource Management
 */
struct resource *pnp_get_resource(struct pnp_dev *, unsigned int, unsigned int);

static inline int pnp_resource_valid(struct resource *res)
{
	if (res && !(res->flags & IORESOURCE_UNSET))
		return 1;
	return 0;
}

static inline resource_size_t pnp_resource_len(struct resource *res)
{
	if (res->start == 0 && res->end == 0)
		return 0;
	return res->end - res->start + 1;
}


static inline resource_size_t pnp_port_start(struct pnp_dev *dev,
					     unsigned int bar)
{
	return pnp_get_resource(dev, IORESOURCE_IO, bar)->start;
}

static inline resource_size_t pnp_port_end(struct pnp_dev *dev,
					   unsigned int bar)
{
	return pnp_get_resource(dev, IORESOURCE_IO, bar)->end;
}

static inline unsigned long pnp_port_flags(struct pnp_dev *dev,
					   unsigned int bar)
{
	return pnp_get_resource(dev, IORESOURCE_IO, bar)->flags;
}

static inline int pnp_port_valid(struct pnp_dev *dev, unsigned int bar)
{
	return pnp_resource_valid(pnp_get_resource(dev, IORESOURCE_IO, bar));
}

static inline resource_size_t pnp_port_len(struct pnp_dev *dev,
					   unsigned int bar)
{
	return pnp_resource_len(pnp_get_resource(dev, IORESOURCE_IO, bar));
}


static inline resource_size_t pnp_mem_start(struct pnp_dev *dev,
					    unsigned int bar)
{
	return pnp_get_resource(dev, IORESOURCE_MEM, bar)->start;
}

static inline resource_size_t pnp_mem_end(struct pnp_dev *dev,
					  unsigned int bar)
{
	return pnp_get_resource(dev, IORESOURCE_MEM, bar)->end;
}

static inline unsigned long pnp_mem_flags(struct pnp_dev *dev, unsigned int bar)
{
	return pnp_get_resource(dev, IORESOURCE_MEM, bar)->flags;
}

static inline int pnp_mem_valid(struct pnp_dev *dev, unsigned int bar)
{
	return pnp_resource_valid(pnp_get_resource(dev, IORESOURCE_MEM, bar));
}

static inline resource_size_t pnp_mem_len(struct pnp_dev *dev,
					  unsigned int bar)
{
	return pnp_resource_len(pnp_get_resource(dev, IORESOURCE_MEM, bar));
}


static inline resource_size_t pnp_irq(struct pnp_dev *dev, unsigned int bar)
{
	return pnp_get_resource(dev, IORESOURCE_IRQ, bar)->start;
}

static inline unsigned long pnp_irq_flags(struct pnp_dev *dev, unsigned int bar)
{
	return pnp_get_resource(dev, IORESOURCE_IRQ, bar)->flags;
}

static inline int pnp_irq_valid(struct pnp_dev *dev, unsigned int bar)
{
	return pnp_resource_valid(pnp_get_resource(dev, IORESOURCE_IRQ, bar));
}


static inline resource_size_t pnp_dma(struct pnp_dev *dev, unsigned int bar)
{
	return pnp_get_resource(dev, IORESOURCE_DMA, bar)->start;
}

static inline unsigned long pnp_dma_flags(struct pnp_dev *dev, unsigned int bar)
{
	return pnp_get_resource(dev, IORESOURCE_DMA, bar)->flags;
}

static inline int pnp_dma_valid(struct pnp_dev *dev, unsigned int bar)
{
	return pnp_resource_valid(pnp_get_resource(dev, IORESOURCE_DMA, bar));
}


#define PNP_PORT_FLAG_16BITADDR	(1<<0)
#define PNP_PORT_FLAG_FIXED	(1<<1)

struct pnp_port {
	unsigned short min;	/* min base number */
	unsigned short max;	/* max base number */
	unsigned char align;	/* align boundary */
	unsigned char size;	/* size of range */
	unsigned char flags;	/* port flags */
	unsigned char pad;	/* pad */
	struct pnp_port *next;	/* next port */
};

#define PNP_IRQ_NR 256
struct pnp_irq {
	DECLARE_BITMAP(map, PNP_IRQ_NR);	/* bitmask for IRQ lines */
	unsigned char flags;	/* IRQ flags */
	unsigned char pad;	/* pad */
	struct pnp_irq *next;	/* next IRQ */
};

struct pnp_dma {
	unsigned char map;	/* bitmask for DMA channels */
	unsigned char flags;	/* DMA flags */
	struct pnp_dma *next;	/* next port */
};

struct pnp_mem {
	unsigned int min;	/* min base number */
	unsigned int max;	/* max base number */
	unsigned int align;	/* align boundary */
	unsigned int size;	/* size of range */
	unsigned char flags;	/* memory flags */
	unsigned char pad;	/* pad */
	struct pnp_mem *next;	/* next memory resource */
};

#define PNP_RES_PRIORITY_PREFERRED	0
#define PNP_RES_PRIORITY_ACCEPTABLE	1
#define PNP_RES_PRIORITY_FUNCTIONAL	2
#define PNP_RES_PRIORITY_INVALID	65535

struct pnp_option {
	unsigned short priority;	/* priority */
	struct pnp_port *port;		/* first port */
	struct pnp_irq *irq;		/* first IRQ */
	struct pnp_dma *dma;		/* first DMA */
	struct pnp_mem *mem;		/* first memory resource */
	struct pnp_option *next;	/* used to chain dependent resources */
};

/*
 * Device Management
 */

struct pnp_card {
	struct device dev;		/* Driver Model device interface */
	unsigned char number;		/* used as an index, must be unique */
	struct list_head global_list;	/* node in global list of cards */
	struct list_head protocol_list;	/* node in protocol's list of cards */
	struct list_head devices;	/* devices attached to the card */

	struct pnp_protocol *protocol;
	struct pnp_id *id;		/* contains supported EISA IDs */

	char name[PNP_NAME_LEN];	/* contains a human-readable name */
	unsigned char pnpver;		/* Plug & Play version */
	unsigned char productver;	/* product version */
	unsigned int serial;		/* serial number */
	unsigned char checksum;		/* if zero - checksum passed */
	struct proc_dir_entry *procdir;	/* directory entry in /proc/bus/isapnp */
};

#define global_to_pnp_card(n) list_entry(n, struct pnp_card, global_list)
#define protocol_to_pnp_card(n) list_entry(n, struct pnp_card, protocol_list)
#define to_pnp_card(n) container_of(n, struct pnp_card, dev)
#define pnp_for_each_card(card) \
	for((card) = global_to_pnp_card(pnp_cards.next); \
	(card) != global_to_pnp_card(&pnp_cards); \
	(card) = global_to_pnp_card((card)->global_list.next))

struct pnp_card_link {
	struct pnp_card *card;
	struct pnp_card_driver *driver;
	void *driver_data;
	pm_message_t pm_state;
};

static inline void *pnp_get_card_drvdata(struct pnp_card_link *pcard)
{
	return pcard->driver_data;
}

static inline void pnp_set_card_drvdata(struct pnp_card_link *pcard, void *data)
{
	pcard->driver_data = data;
}

struct pnp_dev {
	struct device dev;		/* Driver Model device interface */
	u64 dma_mask;
	unsigned int number;		/* used as an index, must be unique */
	int status;

	struct list_head global_list;	/* node in global list of devices */
	struct list_head protocol_list;	/* node in list of device's protocol */
	struct list_head card_list;	/* node in card's list of devices */
	struct list_head rdev_list;	/* node in cards list of requested devices */

	struct pnp_protocol *protocol;
	struct pnp_card *card;	/* card the device is attached to, none if NULL */
	struct pnp_driver *driver;
	struct pnp_card_link *card_link;

	struct pnp_id *id;		/* supported EISA IDs */

	int active;
	int capabilities;
	struct pnp_option *independent;
	struct pnp_option *dependent;
	struct pnp_resource_table *res;

	char name[PNP_NAME_LEN];	/* contains a human-readable name */
	int flags;			/* used by protocols */
	struct proc_dir_entry *procent;	/* device entry in /proc/bus/isapnp */
	void *data;
};

#define global_to_pnp_dev(n) list_entry(n, struct pnp_dev, global_list)
#define card_to_pnp_dev(n) list_entry(n, struct pnp_dev, card_list)
#define protocol_to_pnp_dev(n) list_entry(n, struct pnp_dev, protocol_list)
#define	to_pnp_dev(n) container_of(n, struct pnp_dev, dev)
#define pnp_for_each_dev(dev) \
	for((dev) = global_to_pnp_dev(pnp_global.next); \
	(dev) != global_to_pnp_dev(&pnp_global); \
	(dev) = global_to_pnp_dev((dev)->global_list.next))
#define card_for_each_dev(card,dev) \
	for((dev) = card_to_pnp_dev((card)->devices.next); \
	(dev) != card_to_pnp_dev(&(card)->devices); \
	(dev) = card_to_pnp_dev((dev)->card_list.next))
#define pnp_dev_name(dev) (dev)->name

static inline void *pnp_get_drvdata(struct pnp_dev *pdev)
{
	return dev_get_drvdata(&pdev->dev);
}

static inline void pnp_set_drvdata(struct pnp_dev *pdev, void *data)
{
	dev_set_drvdata(&pdev->dev, data);
}

struct pnp_fixup {
	char id[7];
	void (*quirk_function) (struct pnp_dev * dev);	/* fixup function */
};

/* config parameters */
#define PNP_CONFIG_NORMAL	0x0001
#define PNP_CONFIG_FORCE	0x0002	/* disables validity checking */

/* capabilities */
#define PNP_READ		0x0001
#define PNP_WRITE		0x0002
#define PNP_DISABLE		0x0004
#define PNP_CONFIGURABLE	0x0008
#define PNP_REMOVABLE		0x0010

#define pnp_can_read(dev)	(((dev)->protocol->get) && \
				 ((dev)->capabilities & PNP_READ))
#define pnp_can_write(dev)	(((dev)->protocol->set) && \
				 ((dev)->capabilities & PNP_WRITE))
#define pnp_can_disable(dev)	(((dev)->protocol->disable) && \
				 ((dev)->capabilities & PNP_DISABLE))
#define pnp_can_configure(dev)	((!(dev)->active) && \
				 ((dev)->capabilities & PNP_CONFIGURABLE))

#ifdef CONFIG_ISAPNP
extern struct pnp_protocol isapnp_protocol;
#define pnp_device_is_isapnp(dev) ((dev)->protocol == (&isapnp_protocol))
#else
#define pnp_device_is_isapnp(dev) 0
#endif
extern struct mutex pnp_res_mutex;

#ifdef CONFIG_PNPBIOS
extern struct pnp_protocol pnpbios_protocol;
#define pnp_device_is_pnpbios(dev) ((dev)->protocol == (&pnpbios_protocol))
#else
#define pnp_device_is_pnpbios(dev) 0
#endif

/* status */
#define PNP_READY		0x0000
#define PNP_ATTACHED		0x0001
#define PNP_BUSY		0x0002
#define PNP_FAULTY		0x0004

/* isapnp specific macros */

#define isapnp_card_number(dev)	((dev)->card ? (dev)->card->number : -1)
#define isapnp_csn_number(dev)  ((dev)->number)

/*
 * Driver Management
 */

struct pnp_id {
	char id[PNP_ID_LEN];
	struct pnp_id *next;
};

struct pnp_driver {
	char *name;
	const struct pnp_device_id *id_table;
	unsigned int flags;
	int (*probe) (struct pnp_dev *dev, const struct pnp_device_id *dev_id);
	void (*remove) (struct pnp_dev *dev);
	int (*suspend) (struct pnp_dev *dev, pm_message_t state);
	int (*resume) (struct pnp_dev *dev);
	struct device_driver driver;
};

#define	to_pnp_driver(drv) container_of(drv, struct pnp_driver, driver)

struct pnp_card_driver {
	struct list_head global_list;
	char *name;
	const struct pnp_card_device_id *id_table;
	unsigned int flags;
	int (*probe) (struct pnp_card_link *card,
		      const struct pnp_card_device_id *card_id);
	void (*remove) (struct pnp_card_link *card);
	int (*suspend) (struct pnp_card_link *card, pm_message_t state);
	int (*resume) (struct pnp_card_link *card);
	struct pnp_driver link;
};

#define	to_pnp_card_driver(drv) container_of(drv, struct pnp_card_driver, link)

/* pnp driver flags */
#define PNP_DRIVER_RES_DO_NOT_CHANGE	0x0001	/* do not change the state of the device */
#define PNP_DRIVER_RES_DISABLE		0x0003	/* ensure the device is disabled */

/*
 * Protocol Management
 */

struct pnp_protocol {
	struct list_head protocol_list;
	char *name;

	/* resource control functions */
	int (*get) (struct pnp_dev *dev);
	int (*set) (struct pnp_dev *dev);
	int (*disable) (struct pnp_dev *dev);

	/* protocol specific suspend/resume */
	int (*suspend) (struct pnp_dev * dev, pm_message_t state);
	int (*resume) (struct pnp_dev * dev);

	/* used by pnp layer only (look but don't touch) */
	unsigned char number;	/* protocol number */
	struct device dev;	/* link to driver model */
	struct list_head cards;
	struct list_head devices;
};

#define to_pnp_protocol(n) list_entry(n, struct pnp_protocol, protocol_list)
#define protocol_for_each_card(protocol,card) \
	for((card) = protocol_to_pnp_card((protocol)->cards.next); \
	(card) != protocol_to_pnp_card(&(protocol)->cards); \
	(card) = protocol_to_pnp_card((card)->protocol_list.next))
#define protocol_for_each_dev(protocol,dev) \
	for((dev) = protocol_to_pnp_dev((protocol)->devices.next); \
	(dev) != protocol_to_pnp_dev(&(protocol)->devices); \
	(dev) = protocol_to_pnp_dev((dev)->protocol_list.next))

extern struct bus_type pnp_bus_type;

#if defined(CONFIG_PNP)

/* device management */
int pnp_device_attach(struct pnp_dev *pnp_dev);
void pnp_device_detach(struct pnp_dev *pnp_dev);
extern struct list_head pnp_global;
extern int pnp_platform_devices;

/* multidevice card support */
struct pnp_dev *pnp_request_card_device(struct pnp_card_link *clink,
					const char *id, struct pnp_dev *from);
void pnp_release_card_device(struct pnp_dev *dev);
int pnp_register_card_driver(struct pnp_card_driver *drv);
void pnp_unregister_card_driver(struct pnp_card_driver *drv);
extern struct list_head pnp_cards;

/* resource management */
int pnp_auto_config_dev(struct pnp_dev *dev);
int pnp_start_dev(struct pnp_dev *dev);
int pnp_stop_dev(struct pnp_dev *dev);
int pnp_activate_dev(struct pnp_dev *dev);
int pnp_disable_dev(struct pnp_dev *dev);

/* protocol helpers */
int pnp_is_active(struct pnp_dev *dev);
int compare_pnp_id(struct pnp_id *pos, const char *id);
int pnp_register_driver(struct pnp_driver *drv);
void pnp_unregister_driver(struct pnp_driver *drv);

#else

/* device management */
static inline int pnp_device_attach(struct pnp_dev *pnp_dev) { return -ENODEV; }
static inline void pnp_device_detach(struct pnp_dev *pnp_dev) { }

#define pnp_platform_devices 0

/* multidevice card support */
static inline struct pnp_dev *pnp_request_card_device(struct pnp_card_link *clink, const char *id, struct pnp_dev *from) { return NULL; }
static inline void pnp_release_card_device(struct pnp_dev *dev) { }
static inline int pnp_register_card_driver(struct pnp_card_driver *drv) { return -ENODEV; }
static inline void pnp_unregister_card_driver(struct pnp_card_driver *drv) { }

/* resource management */
static inline int pnp_auto_config_dev(struct pnp_dev *dev) { return -ENODEV; }
static inline int pnp_start_dev(struct pnp_dev *dev) { return -ENODEV; }
static inline int pnp_stop_dev(struct pnp_dev *dev) { return -ENODEV; }
static inline int pnp_activate_dev(struct pnp_dev *dev) { return -ENODEV; }
static inline int pnp_disable_dev(struct pnp_dev *dev) { return -ENODEV; }

/* protocol helpers */
static inline int pnp_is_active(struct pnp_dev *dev) { return 0; }
static inline int compare_pnp_id(struct pnp_id *pos, const char *id) { return -ENODEV; }
static inline int pnp_register_driver(struct pnp_driver *drv) { return -ENODEV; }
static inline void pnp_unregister_driver(struct pnp_driver *drv) { }

#endif /* CONFIG_PNP */

#define pnp_err(format, arg...) printk(KERN_ERR "pnp: " format "\n" , ## arg)
#define pnp_info(format, arg...) printk(KERN_INFO "pnp: " format "\n" , ## arg)
#define pnp_warn(format, arg...) printk(KERN_WARNING "pnp: " format "\n" , ## arg)

#ifdef CONFIG_PNP_DEBUG
#define pnp_dbg(format, arg...) printk(KERN_DEBUG "pnp: " format "\n" , ## arg)
#else
#define pnp_dbg(format, arg...) do {} while (0)
#endif

#endif /* _LINUX_PNP_H */
