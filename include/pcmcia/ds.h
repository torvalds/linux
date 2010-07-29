/*
 * ds.h -- 16-bit PCMCIA core support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999		David A. Hinds
 * (C) 2003 - 2008	Dominik Brodowski
 */

#ifndef _LINUX_DS_H
#define _LINUX_DS_H

#ifdef __KERNEL__
#include <linux/mod_devicetable.h>
#endif

#include <pcmcia/device_id.h>

#ifdef __KERNEL__
#include <linux/device.h>
#include <pcmcia/ss.h>
#include <asm/atomic.h>

/*
 * PCMCIA device drivers (16-bit cards only; 32-bit cards require CardBus
 * a.k.a. PCI drivers
 */
struct pcmcia_socket;
struct pcmcia_device;
struct config_t;
struct net_device;

/* dynamic device IDs for PCMCIA device drivers. See
 * Documentation/pcmcia/driver.txt for details.
*/
struct pcmcia_dynids {
	struct mutex		lock;
	struct list_head	list;
};

struct pcmcia_driver {
	int (*probe)		(struct pcmcia_device *dev);
	void (*remove)		(struct pcmcia_device *dev);

	int (*suspend)		(struct pcmcia_device *dev);
	int (*resume)		(struct pcmcia_device *dev);

	struct module		*owner;
	struct pcmcia_device_id	*id_table;
	struct device_driver	drv;
	struct pcmcia_dynids	dynids;
};

/* driver registration */
int pcmcia_register_driver(struct pcmcia_driver *driver);
void pcmcia_unregister_driver(struct pcmcia_driver *driver);

/* for struct resource * array embedded in struct pcmcia_device */
enum {
	PCMCIA_IOPORT_0,
	PCMCIA_IOPORT_1,
	PCMCIA_IOMEM_0,
	PCMCIA_IOMEM_1,
	PCMCIA_IOMEM_2,
	PCMCIA_IOMEM_3,
	PCMCIA_NUM_RESOURCES,
};

struct pcmcia_device {
	/* the socket and the device_no [for multifunction devices]
	   uniquely define a pcmcia_device */
	struct pcmcia_socket	*socket;

	char			*devname;

	u8			device_no;

	/* the hardware "function" device; certain subdevices can
	 * share one hardware "function" device. */
	u8			func;
	struct config_t		*function_config;

	struct list_head	socket_device_list;

	/* deprecated, will be cleaned up soon */
	config_req_t		conf;

	/* device setup */
	unsigned int		irq;
	struct resource		*resource[PCMCIA_NUM_RESOURCES];

	unsigned int		io_lines; /* number of I/O lines */

	/* Is the device suspended? */
	u16			suspended:1;

	/* Flags whether io, irq, win configurations were
	 * requested, and whether the configuration is "locked" */
	u16			_irq:1;
	u16			_io:1;
	u16			_win:4;
	u16			_locked:1;

	/* Flag whether a "fuzzy" func_id based match is
	 * allowed. */
	u16			allow_func_id_match:1;

	/* information about this device */
	u16			has_manf_id:1;
	u16			has_card_id:1;
	u16			has_func_id:1;

	u16			reserved:4;

	u8			func_id;
	u16			manf_id;
	u16			card_id;

	char			*prod_id[4];

	u64			dma_mask;
	struct device		dev;

	/* data private to drivers */
	void			*priv;
	unsigned int		open;
};

#define to_pcmcia_dev(n) container_of(n, struct pcmcia_device, dev)
#define to_pcmcia_drv(n) container_of(n, struct pcmcia_driver, drv)


/*
 * CIS access.
 *
 * Please use the following functions to access CIS tuples:
 * - pcmcia_get_tuple()
 * - pcmcia_loop_tuple()
 * - pcmcia_get_mac_from_cis()
 *
 * To parse a tuple_t, pcmcia_parse_tuple() exists. Its interface
 * might change in future.
 */

/* get the very first CIS entry of type @code. Note that buf is pointer
 * to u8 *buf; and that you need to kfree(buf) afterwards. */
size_t pcmcia_get_tuple(struct pcmcia_device *p_dev, cisdata_t code,
			u8 **buf);

/* loop over CIS entries */
int pcmcia_loop_tuple(struct pcmcia_device *p_dev, cisdata_t code,
		      int (*loop_tuple) (struct pcmcia_device *p_dev,
					 tuple_t *tuple,
					 void *priv_data),
		      void *priv_data);

/* get the MAC address from CISTPL_FUNCE */
int pcmcia_get_mac_from_cis(struct pcmcia_device *p_dev,
			    struct net_device *dev);


/* parse a tuple_t */
int pcmcia_parse_tuple(tuple_t *tuple, cisparse_t *parse);

/* loop CIS entries for valid configuration */
int pcmcia_loop_config(struct pcmcia_device *p_dev,
		       int	(*conf_check)	(struct pcmcia_device *p_dev,
						 cistpl_cftable_entry_t *cf,
						 cistpl_cftable_entry_t *dflt,
						 unsigned int vcc,
						 void *priv_data),
		       void *priv_data);

/* is the device still there? */
struct pcmcia_device *pcmcia_dev_present(struct pcmcia_device *p_dev);

/* low-level interface reset */
int pcmcia_reset_card(struct pcmcia_socket *skt);

/* CIS config */
int pcmcia_read_config_byte(struct pcmcia_device *p_dev, off_t where, u8 *val);
int pcmcia_write_config_byte(struct pcmcia_device *p_dev, off_t where, u8 val);

/* device configuration */
int pcmcia_request_io(struct pcmcia_device *p_dev);

int __must_check
__pcmcia_request_exclusive_irq(struct pcmcia_device *p_dev,
				irq_handler_t handler);
static inline __must_check __deprecated int
pcmcia_request_exclusive_irq(struct pcmcia_device *p_dev,
				irq_handler_t handler)
{
	return __pcmcia_request_exclusive_irq(p_dev, handler);
}

int __must_check pcmcia_request_irq(struct pcmcia_device *p_dev,
				irq_handler_t handler);

int pcmcia_request_configuration(struct pcmcia_device *p_dev,
				 config_req_t *req);

int pcmcia_request_window(struct pcmcia_device *p_dev, struct resource *res,
			unsigned int speed);
int pcmcia_release_window(struct pcmcia_device *p_dev, struct resource *res);
int pcmcia_map_mem_page(struct pcmcia_device *p_dev, struct resource *res,
			unsigned int offset);

int pcmcia_fixup_vpp(struct pcmcia_device *p_dev, unsigned char new_vpp);
int pcmcia_fixup_iowidth(struct pcmcia_device *p_dev);

void pcmcia_disable_device(struct pcmcia_device *p_dev);

/* IO ports */
#define IO_DATA_PATH_WIDTH	0x18
#define IO_DATA_PATH_WIDTH_8	0x00
#define IO_DATA_PATH_WIDTH_16	0x08
#define IO_DATA_PATH_WIDTH_AUTO	0x10

/* convert flag found in cfgtable to data path width parameter */
static inline int pcmcia_io_cfg_data_width(unsigned int flags)
{
	if (!(flags & CISTPL_IO_8BIT))
		return IO_DATA_PATH_WIDTH_16;
	if (!(flags & CISTPL_IO_16BIT))
		return IO_DATA_PATH_WIDTH_8;
	return IO_DATA_PATH_WIDTH_AUTO;
}

/* IO memory */
#define WIN_MEMORY_TYPE_CM	0x00 /* default */
#define WIN_MEMORY_TYPE_AM	0x20 /* MAP_ATTRIB */
#define WIN_DATA_WIDTH_8	0x00 /* default */
#define WIN_DATA_WIDTH_16	0x02 /* MAP_16BIT */
#define WIN_ENABLE		0x01 /* MAP_ACTIVE */
#define WIN_USE_WAIT		0x40 /* MAP_USE_WAIT */

#define WIN_FLAGS_MAP		0x63 /* MAP_ATTRIB | MAP_16BIT | MAP_ACTIVE |
					MAP_USE_WAIT */
#define WIN_FLAGS_REQ		0x1c /* mapping to socket->win[i]:
					0x04 -> 0
					0x08 -> 1
					0x0c -> 2
					0x10 -> 3 */


#endif /* __KERNEL__ */

#endif /* _LINUX_DS_H */
