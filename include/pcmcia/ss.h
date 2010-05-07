/*
 * ss.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * (C) 1999             David A. Hinds
 */

#ifndef _LINUX_SS_H
#define _LINUX_SS_H

#include <linux/device.h>
#include <linux/sched.h>	/* task_struct, completion */
#include <linux/mutex.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#ifdef CONFIG_CARDBUS
#include <linux/pci.h>
#endif

/* Definitions for card status flags for GetStatus */
#define SS_WRPROT	0x0001
#define SS_CARDLOCK	0x0002
#define SS_EJECTION	0x0004
#define SS_INSERTION	0x0008
#define SS_BATDEAD	0x0010
#define SS_BATWARN	0x0020
#define SS_READY	0x0040
#define SS_DETECT	0x0080
#define SS_POWERON	0x0100
#define SS_GPI		0x0200
#define SS_STSCHG	0x0400
#define SS_CARDBUS	0x0800
#define SS_3VCARD	0x1000
#define SS_XVCARD	0x2000
#define SS_PENDING	0x4000
#define SS_ZVCARD	0x8000

/* InquireSocket capabilities */
#define SS_CAP_PAGE_REGS	0x0001
#define SS_CAP_VIRTUAL_BUS	0x0002
#define SS_CAP_MEM_ALIGN	0x0004
#define SS_CAP_STATIC_MAP	0x0008
#define SS_CAP_PCCARD		0x4000
#define SS_CAP_CARDBUS		0x8000

/* for GetSocket, SetSocket */
typedef struct socket_state_t {
	u_int	flags;
	u_int	csc_mask;
	u_char	Vcc, Vpp;
	u_char	io_irq;
} socket_state_t;

extern socket_state_t dead_socket;

/* Socket configuration flags */
#define SS_PWR_AUTO	0x0010
#define SS_IOCARD	0x0020
#define SS_RESET	0x0040
#define SS_DMA_MODE	0x0080
#define SS_SPKR_ENA	0x0100
#define SS_OUTPUT_ENA	0x0200

/* Flags for I/O port and memory windows */
#define MAP_ACTIVE	0x01
#define MAP_16BIT	0x02
#define MAP_AUTOSZ	0x04
#define MAP_0WS		0x08
#define MAP_WRPROT	0x10
#define MAP_ATTRIB	0x20
#define MAP_USE_WAIT	0x40
#define MAP_PREFETCH	0x80

/* Use this just for bridge windows */
#define MAP_IOSPACE	0x20

/* power hook operations */
#define HOOK_POWER_PRE	0x01
#define HOOK_POWER_POST	0x02

typedef struct pccard_io_map {
	u_char	map;
	u_char	flags;
	u_short	speed;
	phys_addr_t start, stop;
} pccard_io_map;

typedef struct pccard_mem_map {
	u_char		map;
	u_char		flags;
	u_short		speed;
	phys_addr_t	static_start;
	u_int		card_start;
	struct resource	*res;
} pccard_mem_map;

typedef struct io_window_t {
	u_int			InUse, Config;
	struct resource		*res;
} io_window_t;

/* Maximum number of IO windows per socket */
#define MAX_IO_WIN 2

/* Maximum number of memory windows per socket */
#define MAX_WIN 4


/*
 * Socket operations.
 */
struct pcmcia_socket;
struct pccard_resource_ops;
struct config_t;
struct pcmcia_callback;
struct user_info_t;

struct pccard_operations {
	int (*init)(struct pcmcia_socket *s);
	int (*suspend)(struct pcmcia_socket *s);
	int (*get_status)(struct pcmcia_socket *s, u_int *value);
	int (*set_socket)(struct pcmcia_socket *s, socket_state_t *state);
	int (*set_io_map)(struct pcmcia_socket *s, struct pccard_io_map *io);
	int (*set_mem_map)(struct pcmcia_socket *s, struct pccard_mem_map *mem);
};

struct pcmcia_socket {
	struct module			*owner;
	socket_state_t			socket;
	u_int				state;
	u_int				suspended_state;	/* state before suspend */
	u_short				functions;
	u_short				lock_count;
	pccard_mem_map			cis_mem;
	void __iomem 			*cis_virt;
	struct {
		u_int			AssignedIRQ;
		u_int			Config;
	} irq;
	io_window_t			io[MAX_IO_WIN];
	pccard_mem_map			win[MAX_WIN];
	struct list_head		cis_cache;
	size_t				fake_cis_len;
	u8				*fake_cis;

	struct list_head		socket_list;
	struct completion		socket_released;

	/* deprecated */
	unsigned int			sock;		/* socket number */


	/* socket capabilities */
	u_int				features;
	u_int				irq_mask;
	u_int				map_size;
	u_int				io_offset;
	u_int				pci_irq;
	struct pci_dev			*cb_dev;


	/* socket setup is done so resources should be able to be allocated.
	 * Only if set to 1, calls to find_{io,mem}_region are handled, and
	 * insertio events are actually managed by the PCMCIA layer.*/
	u8				resource_setup_done:1;

	/* It's old if resource setup is done using adjust_resource_info() */
	u8				resource_setup_old:1;
	u8				resource_setup_new:1;

	u8				reserved:5;

	/* socket operations */
	struct pccard_operations	*ops;
	struct pccard_resource_ops	*resource_ops;
	void				*resource_data;

	/* Zoom video behaviour is so chip specific its not worth adding
	   this to _ops */
	void 				(*zoom_video)(struct pcmcia_socket *,
						      int);

	/* so is power hook */
	int (*power_hook)(struct pcmcia_socket *sock, int operation);

	/* allows tuning the CB bridge before loading driver for the CB card */
#ifdef CONFIG_CARDBUS
	void (*tune_bridge)(struct pcmcia_socket *sock, struct pci_bus *bus);
#endif

	/* state thread */
	struct task_struct		*thread;
	struct completion		thread_done;
	unsigned int			thread_events;
	unsigned int			sysfs_events;

	/* For the non-trivial interaction between these locks,
	 * see Documentation/pcmcia/locking.txt */
	struct mutex			skt_mutex;
	struct mutex			ops_mutex;

	/* protects thread_events and sysfs_events */
	spinlock_t			thread_lock;

	/* pcmcia (16-bit) */
	struct pcmcia_callback		*callback;

#if defined(CONFIG_PCMCIA) || defined(CONFIG_PCMCIA_MODULE)
	/* The following elements refer to 16-bit PCMCIA devices inserted
	 * into the socket */
	struct list_head		devices_list;

	/* the number of devices, used only internally and subject to
	 * incorrectness and change */
	u8				device_count;

	/* 16-bit state: */
	struct {
		/* "master" ioctl is used */
		u8			busy:1;
		/* the PCMCIA card consists of two pseudo devices */
		u8			has_pfc:1;

		u8			reserved:6;
	} pcmcia_state;

	/* non-zero if PCMCIA card is present */
	atomic_t			present;

#ifdef CONFIG_PCMCIA_IOCTL
	struct user_info_t		*user;
	wait_queue_head_t		queue;
#endif /* CONFIG_PCMCIA_IOCTL */
#endif /* CONFIG_PCMCIA */

	/* socket device */
	struct device			dev;
	/* data internal to the socket driver */
	void				*driver_data;
	/* status of the card during resume from a system sleep state */
	int				resume_status;
};


/* socket drivers must define the resource operations type they use. There
 * are three options:
 * - pccard_static_ops		iomem and ioport areas are assigned statically
 * - pccard_iodyn_ops		iomem areas is assigned statically, ioport
 *				areas dynamically
 *				If this option is selected, use
 *				"select PCCARD_IODYN" in Kconfig.
 * - pccard_nonstatic_ops	iomem and ioport areas are assigned dynamically.
 *				If this option is selected, use
 *				"select PCCARD_NONSTATIC" in Kconfig.
 *
 */
extern struct pccard_resource_ops pccard_static_ops;
#if defined(CONFIG_PCMCIA) || defined(CONFIG_PCMCIA_MODULE)
extern struct pccard_resource_ops pccard_iodyn_ops;
extern struct pccard_resource_ops pccard_nonstatic_ops;
#else
/* If PCMCIA is not used, but only CARDBUS, these functions are not used
 * at all. Therefore, do not use the large (240K!) rsrc_nonstatic module
 */
#define pccard_iodyn_ops pccard_static_ops
#define pccard_nonstatic_ops pccard_static_ops
#endif


/* socket drivers use this callback in their IRQ handler */
extern void pcmcia_parse_events(struct pcmcia_socket *socket,
				unsigned int events);

/* to register and unregister a socket */
extern int pcmcia_register_socket(struct pcmcia_socket *socket);
extern void pcmcia_unregister_socket(struct pcmcia_socket *socket);


#endif /* _LINUX_SS_H */
