/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Alessandro Rubini <rubini@gnudd.com>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 *
 * This work is part of the White Rabbit project, a research effort led
 * by CERN, the European Institute for Nuclear Research.
 */
#ifndef __LINUX_FMC_H__
#define __LINUX_FMC_H__
#include <linux/types.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/io.h>

struct fmc_device;
struct fmc_driver;

/*
 * This bus abstraction is developed separately from drivers, so we need
 * to check the version of the data structures we receive.
 */

#define FMC_MAJOR	3
#define FMC_MINOR	0
#define FMC_VERSION	((FMC_MAJOR << 16) | FMC_MINOR)
#define __FMC_MAJOR(x)	((x) >> 16)
#define __FMC_MINOR(x)	((x) & 0xffff)

/*
 * The device identification, as defined by the IPMI FRU (Field Replaceable
 * Unit) includes four different strings to describe the device. Here we
 * only match the "Board Manufacturer" and the "Board Product Name",
 * ignoring the "Board Serial Number" and "Board Part Number". All 4 are
 * expected to be strings, so they are treated as zero-terminated C strings.
 * Unspecified string (NULL) means "any", so if both are unspecified this
 * is a catch-all driver. So null entries are allowed and we use array
 * and length. This is unlike pci and usb that use null-terminated arrays
 */
struct fmc_fru_id {
	char *manufacturer;
	char *product_name;
};

/*
 * If the FPGA is already programmed (think Etherbone or the second
 * SVEC slot), we can match on SDB devices in the memory image. This
 * match uses an array of devices that must all be present, and the
 * match is based on vendor and device only. Further checks are expected
 * to happen in the probe function. Zero means "any" and catch-all is allowed.
 */
struct fmc_sdb_one_id {
	uint64_t vendor;
	uint32_t device;
};
struct fmc_sdb_id {
	struct fmc_sdb_one_id *cores;
	int cores_nr;
};

struct fmc_device_id {
	struct fmc_fru_id *fru_id;
	int fru_id_nr;
	struct fmc_sdb_id *sdb_id;
	int sdb_id_nr;
};

/* This sizes the module_param_array used by generic module parameters */
#define FMC_MAX_CARDS 32

/* The driver is a pretty simple thing */
struct fmc_driver {
	unsigned long version;
	struct device_driver driver;
	int (*probe)(struct fmc_device *);
	int (*remove)(struct fmc_device *);
	const struct fmc_device_id id_table;
	/* What follows is for generic module parameters */
	int busid_n;
	int busid_val[FMC_MAX_CARDS];
	int gw_n;
	char *gw_val[FMC_MAX_CARDS];
};
#define to_fmc_driver(x) container_of((x), struct fmc_driver, driver)

/* These are the generic parameters, that drivers may instantiate */
#define FMC_PARAM_BUSID(_d) \
    module_param_array_named(busid, _d.busid_val, int, &_d.busid_n, 0444)
#define FMC_PARAM_GATEWARE(_d) \
    module_param_array_named(gateware, _d.gw_val, charp, &_d.gw_n, 0444)

/*
 * Drivers may need to configure gpio pins in the carrier. To read input
 * (a very uncommon operation, and definitely not in the hot paths), just
 * configure one gpio only and get 0 or 1 as retval of the config method
 */
struct fmc_gpio {
	char *carrier_name; /* name or NULL for virtual pins */
	int gpio;
	int _gpio;	/* internal use by the carrier */
	int mode;	/* GPIOF_DIR_OUT etc, from <linux/gpio.h> */
	int irqmode;	/* IRQF_TRIGGER_LOW and so on */
};

/* The numbering of gpio pins allows access to raw pins or virtual roles */
#define FMC_GPIO_RAW(x)		(x)		/* 4096 of them */
#define __FMC_GPIO_IS_RAW(x)	((x) < 0x1000)
#define FMC_GPIO_IRQ(x)		((x) + 0x1000)	/*  256 of them */
#define FMC_GPIO_LED(x)		((x) + 0x1100)	/*  256 of them */
#define FMC_GPIO_KEY(x)		((x) + 0x1200)	/*  256 of them */
#define FMC_GPIO_TP(x)		((x) + 0x1300)	/*  256 of them */
#define FMC_GPIO_USER(x)	((x) + 0x1400)	/*  256 of them */
/* We may add SCL and SDA, or other roles if the need arises */

/* GPIOF_DIR_IN etc are missing before 3.0. copy from <linux/gpio.h> */
#ifndef GPIOF_DIR_IN
#  define GPIOF_DIR_OUT   (0 << 0)
#  define GPIOF_DIR_IN    (1 << 0)
#  define GPIOF_INIT_LOW  (0 << 1)
#  define GPIOF_INIT_HIGH (1 << 1)
#endif

/*
 * The operations are offered by each carrier and should make driver
 * design completely independent of the carrier. Named GPIO pins may be
 * the exception.
 */
struct fmc_operations {
	uint32_t (*read32)(struct fmc_device *fmc, int offset);
	void (*write32)(struct fmc_device *fmc, uint32_t value, int offset);
	int (*validate)(struct fmc_device *fmc, struct fmc_driver *drv);
	int (*reprogram)(struct fmc_device *f, struct fmc_driver *d, char *gw);
	int (*irq_request)(struct fmc_device *fmc, irq_handler_t h,
			   char *name, int flags);
	void (*irq_ack)(struct fmc_device *fmc);
	int (*irq_free)(struct fmc_device *fmc);
	int (*gpio_config)(struct fmc_device *fmc, struct fmc_gpio *gpio,
			   int ngpio);
	int (*read_ee)(struct fmc_device *fmc, int pos, void *d, int l);
	int (*write_ee)(struct fmc_device *fmc, int pos, const void *d, int l);
};

/* Prefer this helper rather than calling of fmc->reprogram directly */
extern int fmc_reprogram(struct fmc_device *f, struct fmc_driver *d, char *gw,
		     int sdb_entry);

/*
 * The device reports all information needed to access hw.
 *
 * If we have eeprom_len and not contents, the core reads it.
 * Then, parsing of identifiers is done by the core which fills fmc_fru_id..
 * Similarly a device that must be matched based on SDB cores must
 * fill the entry point and the core will scan the bus (FIXME: sdb match)
 */
struct fmc_device {
	unsigned long version;
	unsigned long flags;
	struct module *owner;		/* char device must pin it */
	struct fmc_fru_id id;		/* for EEPROM-based match */
	struct fmc_operations *op;	/* carrier-provided */
	int irq;			/* according to host bus. 0 == none */
	int eeprom_len;			/* Usually 8kB, may be less */
	int eeprom_addr;		/* 0x50, 0x52 etc */
	uint8_t *eeprom;		/* Full contents or leading part */
	char *carrier_name;		/* "SPEC" or similar, for special use */
	void *carrier_data;		/* "struct spec *" or equivalent */
	__iomem void *fpga_base;	/* May be NULL (Etherbone) */
	__iomem void *slot_base;	/* Set by the driver */
	struct fmc_device **devarray;	/* Allocated by the bus */
	int slot_id;			/* Index in the slot array */
	int nr_slots;			/* Number of slots in this carrier */
	unsigned long memlen;		/* Used for the char device */
	struct device dev;		/* For Linux use */
	struct device *hwdev;		/* The underlying hardware device */
	unsigned long sdbfs_entry;
	struct sdb_array *sdb;
	uint32_t device_id;		/* Filled by the device */
	char *mezzanine_name;		/* Defaults to ``fmc'' */
	void *mezzanine_data;

	struct dentry *dbg_dir;
	struct dentry *dbg_sdb_dump;
};
#define to_fmc_device(x) container_of((x), struct fmc_device, dev)

#define FMC_DEVICE_HAS_GOLDEN		1
#define FMC_DEVICE_HAS_CUSTOM		2
#define FMC_DEVICE_NO_MEZZANINE		4
#define FMC_DEVICE_MATCH_SDB		8 /* fmc-core must scan sdb in fpga */

/*
 * If fpga_base can be used, the carrier offers no readl/writel methods, and
 * this expands to a single, fast, I/O access.
 */
static inline uint32_t fmc_readl(struct fmc_device *fmc, int offset)
{
	if (unlikely(fmc->op->read32))
		return fmc->op->read32(fmc, offset);
	return readl(fmc->fpga_base + offset);
}
static inline void fmc_writel(struct fmc_device *fmc, uint32_t val, int off)
{
	if (unlikely(fmc->op->write32))
		fmc->op->write32(fmc, val, off);
	else
		writel(val, fmc->fpga_base + off);
}

/* pci-like naming */
static inline void *fmc_get_drvdata(const struct fmc_device *fmc)
{
	return dev_get_drvdata(&fmc->dev);
}

static inline void fmc_set_drvdata(struct fmc_device *fmc, void *data)
{
	dev_set_drvdata(&fmc->dev, data);
}

struct fmc_gateware {
	void *bitstream;
	unsigned long len;
};

/* The 5 access points */
extern int fmc_driver_register(struct fmc_driver *drv);
extern void fmc_driver_unregister(struct fmc_driver *drv);
extern int fmc_device_register(struct fmc_device *tdev);
extern int fmc_device_register_gw(struct fmc_device *tdev,
				  struct fmc_gateware *gw);
extern void fmc_device_unregister(struct fmc_device *tdev);

/* Three more for device sets, all driven by the same FPGA */
extern int fmc_device_register_n(struct fmc_device **devs, int n);
extern int fmc_device_register_n_gw(struct fmc_device **devs, int n,
				    struct fmc_gateware *gw);
extern void fmc_device_unregister_n(struct fmc_device **devs, int n);

/* Internal cross-calls between files; not exported to other modules */
extern int fmc_match(struct device *dev, struct device_driver *drv);
extern int fmc_fill_id_info(struct fmc_device *fmc);
extern void fmc_free_id_info(struct fmc_device *fmc);
extern void fmc_dump_eeprom(const struct fmc_device *fmc);

/* helpers for FMC operations */
extern int fmc_irq_request(struct fmc_device *fmc, irq_handler_t h,
			   char *name, int flags);
extern void fmc_irq_free(struct fmc_device *fmc);
extern void fmc_irq_ack(struct fmc_device *fmc);
extern int fmc_validate(struct fmc_device *fmc, struct fmc_driver *drv);
extern int fmc_gpio_config(struct fmc_device *fmc, struct fmc_gpio *gpio,
			   int ngpio);
extern int fmc_read_ee(struct fmc_device *fmc, int pos, void *d, int l);
extern int fmc_write_ee(struct fmc_device *fmc, int pos, const void *d, int l);

/* helpers for FMC operations */
extern int fmc_irq_request(struct fmc_device *fmc, irq_handler_t h,
			   char *name, int flags);
extern void fmc_irq_free(struct fmc_device *fmc);
extern void fmc_irq_ack(struct fmc_device *fmc);
extern int fmc_validate(struct fmc_device *fmc, struct fmc_driver *drv);

#endif /* __LINUX_FMC_H__ */
