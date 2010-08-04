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

#include <pcmcia/cs_types.h>
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
	u_int			open;
	io_req_t		io;
	config_req_t		conf;
	window_handle_t		win;

	/* device setup */
	unsigned int		irq;

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

#ifdef CONFIG_PCMCIA_IOCTL
	/* device driver wanted by cardmgr */
	struct pcmcia_driver	*cardmgr;
#endif

	/* data private to drivers */
	void			*priv;
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
int pcmcia_access_configuration_register(struct pcmcia_device *p_dev,
					 conf_reg_t *reg);

/* device configuration */
int pcmcia_request_io(struct pcmcia_device *p_dev, io_req_t *req);

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

int pcmcia_request_window(struct pcmcia_device *p_dev, win_req_t *req,
			  window_handle_t *wh);
int pcmcia_release_window(struct pcmcia_device *p_dev, window_handle_t win);
int pcmcia_map_mem_page(struct pcmcia_device *p_dev, window_handle_t win,
			memreq_t *req);

int pcmcia_modify_configuration(struct pcmcia_device *p_dev, modconf_t *mod);
void pcmcia_disable_device(struct pcmcia_device *p_dev);

#endif /* __KERNEL__ */



/* Below, there are only definitions which are used by
 * - the PCMCIA ioctl
 * - deprecated PCMCIA userspace tools only
 *
 * here be dragons ... here be dragons ... here be dragons ... here be drag
 */

#if defined(CONFIG_PCMCIA_IOCTL) || !defined(__KERNEL__)

#if defined(__arm__) || defined(__mips__) || defined(__avr32__) || \
	defined(__bfin__)
/* This (ioaddr_t) is exposed to userspace & hence cannot be changed. */
typedef u_int   ioaddr_t;
#else
typedef u_short	ioaddr_t;
#endif

/* for AdjustResourceInfo */
typedef struct adjust_t {
	u_int			Action;
	u_int			Resource;
	u_int			Attributes;
	union {
		struct memory {
			u_long		Base;
			u_long		Size;
		} memory;
		struct io {
			ioaddr_t	BasePort;
			ioaddr_t	NumPorts;
			u_int		IOAddrLines;
		} io;
		struct irq {
			u_int		IRQ;
		} irq;
	} resource;
} adjust_t;

/* Action field */
#define REMOVE_MANAGED_RESOURCE		1
#define ADD_MANAGED_RESOURCE		2
#define GET_FIRST_MANAGED_RESOURCE	3
#define GET_NEXT_MANAGED_RESOURCE	4
/* Resource field */
#define RES_MEMORY_RANGE		1
#define RES_IO_RANGE			2
#define RES_IRQ				3
/* Attribute field */
#define RES_IRQ_TYPE			0x03
#define RES_IRQ_TYPE_EXCLUSIVE		0
#define RES_IRQ_TYPE_TIME		1
#define RES_IRQ_TYPE_DYNAMIC		2
#define RES_IRQ_CSC			0x04
#define RES_SHARED			0x08
#define RES_RESERVED			0x10
#define RES_ALLOCATED			0x20
#define RES_REMOVED			0x40


typedef struct tuple_parse_t {
	tuple_t			tuple;
	cisdata_t		data[255];
	cisparse_t		parse;
} tuple_parse_t;

typedef struct win_info_t {
	window_handle_t		handle;
	win_req_t		window;
	memreq_t		map;
} win_info_t;

typedef struct bind_info_t {
	dev_info_t		dev_info;
	u_char			function;
	struct pcmcia_device	*instance;
	char			name[DEV_NAME_LEN];
	u_short			major, minor;
	void			*next;
} bind_info_t;

typedef struct mtd_info_t {
	dev_info_t     		dev_info;
	u_int			Attributes;
	u_int			CardOffset;
} mtd_info_t;

typedef struct region_info_t {
	u_int			Attributes;
	u_int			CardOffset;
	u_int			RegionSize;
	u_int			AccessSpeed;
	u_int			BlockSize;
	u_int			PartMultiple;
	u_char			JedecMfr, JedecInfo;
	memory_handle_t		next;
} region_info_t;

#define REGION_TYPE		0x0001
#define REGION_TYPE_CM		0x0000
#define REGION_TYPE_AM		0x0001
#define REGION_PREFETCH		0x0008
#define REGION_CACHEABLE	0x0010
#define REGION_BAR_MASK		0xe000
#define REGION_BAR_SHIFT	13

/* For ReplaceCIS */
typedef struct cisdump_t {
	u_int			Length;
	cisdata_t		Data[CISTPL_MAX_CIS_SIZE];
} cisdump_t;

/* for GetConfigurationInfo */
typedef struct config_info_t {
	u_char			Function;
	u_int			Attributes;
	u_int			Vcc, Vpp1, Vpp2;
	u_int			IntType;
	u_int			ConfigBase;
	u_char			Status, Pin, Copy, Option, ExtStatus;
	u_int			Present;
	u_int			CardValues;
	u_int			AssignedIRQ;
	u_int			IRQAttributes;
	ioaddr_t		BasePort1;
	ioaddr_t		NumPorts1;
	u_int			Attributes1;
	ioaddr_t		BasePort2;
	ioaddr_t		NumPorts2;
	u_int			Attributes2;
	u_int			IOAddrLines;
} config_info_t;

/* For ValidateCIS */
typedef struct cisinfo_t {
	u_int			Chains;
} cisinfo_t;

typedef struct cs_status_t {
	u_char			Function;
	event_t 		CardState;
	event_t			SocketState;
} cs_status_t;

typedef union ds_ioctl_arg_t {
	adjust_t		adjust;
	config_info_t		config;
	tuple_t			tuple;
	tuple_parse_t		tuple_parse;
	client_req_t		client_req;
	cs_status_t		status;
	conf_reg_t		conf_reg;
	cisinfo_t		cisinfo;
	region_info_t		region;
	bind_info_t		bind_info;
	mtd_info_t		mtd_info;
	win_info_t		win_info;
	cisdump_t		cisdump;
} ds_ioctl_arg_t;

#define DS_ADJUST_RESOURCE_INFO			_IOWR('d',  2, adjust_t)
#define DS_GET_CONFIGURATION_INFO		_IOWR('d',  3, config_info_t)
#define DS_GET_FIRST_TUPLE			_IOWR('d',  4, tuple_t)
#define DS_GET_NEXT_TUPLE			_IOWR('d',  5, tuple_t)
#define DS_GET_TUPLE_DATA			_IOWR('d',  6, tuple_parse_t)
#define DS_PARSE_TUPLE				_IOWR('d',  7, tuple_parse_t)
#define DS_RESET_CARD				_IO  ('d',  8)
#define DS_GET_STATUS				_IOWR('d',  9, cs_status_t)
#define DS_ACCESS_CONFIGURATION_REGISTER	_IOWR('d', 10, conf_reg_t)
#define DS_VALIDATE_CIS				_IOR ('d', 11, cisinfo_t)
#define DS_SUSPEND_CARD				_IO  ('d', 12)
#define DS_RESUME_CARD				_IO  ('d', 13)
#define DS_EJECT_CARD				_IO  ('d', 14)
#define DS_INSERT_CARD				_IO  ('d', 15)
#define DS_GET_FIRST_REGION			_IOWR('d', 16, region_info_t)
#define DS_GET_NEXT_REGION			_IOWR('d', 17, region_info_t)
#define DS_REPLACE_CIS				_IOWR('d', 18, cisdump_t)
#define DS_GET_FIRST_WINDOW			_IOR ('d', 19, win_info_t)
#define DS_GET_NEXT_WINDOW			_IOWR('d', 20, win_info_t)
#define DS_GET_MEM_PAGE				_IOWR('d', 21, win_info_t)

#define DS_BIND_REQUEST				_IOWR('d', 60, bind_info_t)
#define DS_GET_DEVICE_INFO			_IOWR('d', 61, bind_info_t)
#define DS_GET_NEXT_DEVICE			_IOWR('d', 62, bind_info_t)
#define DS_UNBIND_REQUEST			_IOW ('d', 63, bind_info_t)
#define DS_BIND_MTD				_IOWR('d', 64, mtd_info_t)


/* used in userspace only */
#define CS_IN_USE			0x1e

#define INFO_MASTER_CLIENT	0x01
#define INFO_IO_CLIENT		0x02
#define INFO_MTD_CLIENT		0x04
#define INFO_MEM_CLIENT		0x08
#define MAX_NUM_CLIENTS		3

#define INFO_CARD_SHARE		0x10
#define INFO_CARD_EXCL		0x20


#endif /* !defined(__KERNEL__) || defined(CONFIG_PCMCIA_IOCTL) */

#endif /* _LINUX_DS_H */
