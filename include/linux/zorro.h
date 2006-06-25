/*
 *  linux/zorro.h -- Amiga AutoConfig (Zorro) Bus Definitions
 *
 *  Copyright (C) 1995--2003 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 */

#ifndef _LINUX_ZORRO_H
#define _LINUX_ZORRO_H

#include <linux/device.h>


    /*
     *  Each Zorro board has a 32-bit ID of the form
     *
     *      mmmmmmmmmmmmmmmmppppppppeeeeeeee
     *
     *  with
     *
     *      mmmmmmmmmmmmmmmm	16-bit Manufacturer ID (assigned by CBM (sigh))
     *      pppppppp		8-bit Product ID (assigned by manufacturer)
     *      eeeeeeee		8-bit Extended Product ID (currently only used
     *				for some GVP boards)
     */


#define ZORRO_MANUF(id)		((id) >> 16)
#define ZORRO_PROD(id)		(((id) >> 8) & 0xff)
#define ZORRO_EPC(id)		((id) & 0xff)

#define ZORRO_ID(manuf, prod, epc) \
    ((ZORRO_MANUF_##manuf << 16) | ((prod) << 8) | (epc))

typedef __u32 zorro_id;


#define ZORRO_WILDCARD		(0xffffffff)	/* not official */

/* Include the ID list */
#include <linux/zorro_ids.h>


    /*
     *  GVP identifies most of its products through the 'extended product code'
     *  (epc). The epc has to be ANDed with the GVP_PRODMASK before the
     *  identification.
     */

#define GVP_PRODMASK			(0xf8)
#define GVP_SCSICLKMASK			(0x01)

enum GVP_flags {
    GVP_IO		= 0x01,
    GVP_ACCEL		= 0x02,
    GVP_SCSI		= 0x04,
    GVP_24BITDMA	= 0x08,
    GVP_25BITDMA	= 0x10,
    GVP_NOBANK		= 0x20,
    GVP_14MHZ		= 0x40,
};


struct Node {
    struct  Node *ln_Succ;	/* Pointer to next (successor) */
    struct  Node *ln_Pred;	/* Pointer to previous (predecessor) */
    __u8    ln_Type;
    __s8    ln_Pri;		/* Priority, for sorting */
    __s8    *ln_Name;		/* ID string, null terminated */
} __attribute__ ((packed));

struct ExpansionRom {
    /* -First 16 bytes of the expansion ROM */
    __u8  er_Type;		/* Board type, size and flags */
    __u8  er_Product;		/* Product number, assigned by manufacturer */
    __u8  er_Flags;		/* Flags */
    __u8  er_Reserved03;	/* Must be zero ($ff inverted) */
    __u16 er_Manufacturer;	/* Unique ID, ASSIGNED BY COMMODORE-AMIGA! */
    __u32 er_SerialNumber;	/* Available for use by manufacturer */
    __u16 er_InitDiagVec;	/* Offset to optional "DiagArea" structure */
    __u8  er_Reserved0c;
    __u8  er_Reserved0d;
    __u8  er_Reserved0e;
    __u8  er_Reserved0f;
} __attribute__ ((packed));

/* er_Type board type bits */
#define ERT_TYPEMASK	0xc0
#define ERT_ZORROII	0xc0
#define ERT_ZORROIII	0x80

/* other bits defined in er_Type */
#define ERTB_MEMLIST	5		/* Link RAM into free memory list */
#define ERTF_MEMLIST	(1<<5)

struct ConfigDev {
    struct Node		cd_Node;
    __u8		cd_Flags;	/* (read/write) */
    __u8		cd_Pad;		/* reserved */
    struct ExpansionRom cd_Rom;		/* copy of board's expansion ROM */
    void		*cd_BoardAddr;	/* where in memory the board was placed */
    __u32		cd_BoardSize;	/* size of board in bytes */
    __u16		cd_SlotAddr;	/* which slot number (PRIVATE) */
    __u16		cd_SlotSize;	/* number of slots (PRIVATE) */
    void		*cd_Driver;	/* pointer to node of driver */
    struct ConfigDev	*cd_NextCD;	/* linked list of drivers to config */
    __u32		cd_Unused[4];	/* for whatever the driver wants */
} __attribute__ ((packed));

#define ZORRO_NUM_AUTO		16

#ifdef __KERNEL__

#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/zorro.h>


    /*
     *  Zorro devices
     */

struct zorro_dev {
    struct ExpansionRom rom;
    zorro_id id;
    struct zorro_driver *driver;	/* which driver has allocated this device */
    struct device dev;			/* Generic device interface */
    u16 slotaddr;
    u16 slotsize;
    char name[64];
    struct resource resource;
};

#define	to_zorro_dev(n)	container_of(n, struct zorro_dev, dev)


    /*
     *  Zorro bus
     */

struct zorro_bus {
    struct list_head devices;		/* list of devices on this bus */
    unsigned int num_resources;		/* number of resources */
    struct resource resources[4];	/* address space routed to this bus */
    struct device dev;
    char name[10];
};

extern struct zorro_bus zorro_bus;	/* single Zorro bus */
extern struct bus_type zorro_bus_type;


    /*
     *  Zorro device IDs
     */

struct zorro_device_id {
	zorro_id id;			/* Device ID or ZORRO_WILDCARD */
	unsigned long driver_data;	/* Data private to the driver */
};


    /*
     *  Zorro device drivers
     */

struct zorro_driver {
    struct list_head node;
    char *name;
    const struct zorro_device_id *id_table;	/* NULL if wants all devices */
    int (*probe)(struct zorro_dev *z, const struct zorro_device_id *id);	/* New device inserted */
    void (*remove)(struct zorro_dev *z);	/* Device removed (NULL if not a hot-plug capable driver) */
    struct device_driver driver;
};

#define	to_zorro_driver(drv)	container_of(drv, struct zorro_driver, driver)


#define zorro_for_each_dev(dev)	\
	for (dev = &zorro_autocon[0]; dev < zorro_autocon+zorro_num_autocon; dev++)


/* New-style probing */
extern int zorro_register_driver(struct zorro_driver *);
extern void zorro_unregister_driver(struct zorro_driver *);
extern const struct zorro_device_id *zorro_match_device(const struct zorro_device_id *ids, const struct zorro_dev *z);
static inline struct zorro_driver *zorro_dev_driver(const struct zorro_dev *z)
{
    return z->driver;
}


extern unsigned int zorro_num_autocon;	/* # of autoconfig devices found */
extern struct zorro_dev zorro_autocon[ZORRO_NUM_AUTO];


    /*
     *  Zorro Functions
     */

extern struct zorro_dev *zorro_find_device(zorro_id id,
					   struct zorro_dev *from);

#define zorro_resource_start(z)	((z)->resource.start)
#define zorro_resource_end(z)	((z)->resource.end)
#define zorro_resource_len(z)	((z)->resource.end-(z)->resource.start+1)
#define zorro_resource_flags(z)	((z)->resource.flags)

#define zorro_request_device(z, name) \
    request_mem_region(zorro_resource_start(z), zorro_resource_len(z), name)
#define zorro_release_device(z) \
    release_mem_region(zorro_resource_start(z), zorro_resource_len(z))

/* Similar to the helpers above, these manipulate per-zorro_dev
 * driver-specific data.  They are really just a wrapper around
 * the generic device structure functions of these calls.
 */
static inline void *zorro_get_drvdata (struct zorro_dev *z)
{
	return dev_get_drvdata(&z->dev);
}

static inline void zorro_set_drvdata (struct zorro_dev *z, void *data)
{
	dev_set_drvdata(&z->dev, data);
}


    /*
     *  Bitmask indicating portions of available Zorro II RAM that are unused
     *  by the system. Every bit represents a 64K chunk, for a maximum of 8MB
     *  (128 chunks, physical 0x00200000-0x009fffff).
     *
     *  If you want to use (= allocate) portions of this RAM, you should clear
     *  the corresponding bits.
     */

extern DECLARE_BITMAP(zorro_unused_z2ram, 128);

#define Z2RAM_START		(0x00200000)
#define Z2RAM_END		(0x00a00000)
#define Z2RAM_SIZE		(0x00800000)
#define Z2RAM_CHUNKSIZE		(0x00010000)
#define Z2RAM_CHUNKMASK		(0x0000ffff)
#define Z2RAM_CHUNKSHIFT	(16)


#endif /* __KERNEL__ */

#endif /* _LINUX_ZORRO_H */
