/*
 * Dongle BUS interface Abstraction layer
 *   target buses like USB, SDIO, SPI, etc.
 *
 * Copyright (C) 1999-2011, Broadcom Corporation
 * 
 *         Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 * $Id: dbus.h 275703 2011-08-04 20:20:27Z $
 */

#ifndef __DBUS_H__
#define __DBUS_H__

#include "typedefs.h"

#define DBUSTRACE(args)
#define DBUSERR(args)
#define DBUSDBGLOCK(args)

enum {
	DBUS_OK = 0,
	DBUS_ERR = -200,
	DBUS_ERR_TIMEOUT,
	DBUS_ERR_DISCONNECT,
	DBUS_ERR_NODEVICE,
	DBUS_ERR_UNSUPPORTED,
	DBUS_ERR_PENDING,
	DBUS_ERR_NOMEM,
	DBUS_ERR_TXFAIL,
	DBUS_ERR_TXTIMEOUT,
	DBUS_ERR_TXDROP,
	DBUS_ERR_RXFAIL,
	DBUS_ERR_RXDROP,
	DBUS_ERR_TXCTLFAIL,
	DBUS_ERR_RXCTLFAIL,
	DBUS_ERR_REG_PARAM,
	DBUS_STATUS_CANCELLED,
	DBUS_ERR_NVRAM
};

#define ERR_CBMASK_TXFAIL		0x00000001
#define ERR_CBMASK_RXFAIL		0x00000002
#define ERR_CBMASK_ALL			0xFFFFFFFF

#define DBUS_CBCTL_WRITE		0
#define DBUS_CBCTL_READ			1

#define DBUS_TX_RETRY_LIMIT		3		/* retries for failed txirb */
#define DBUS_TX_TIMEOUT_INTERVAL	250		/* timeout for txirb complete, in ms */

#define DBUS_BUFFER_SIZE_TX	16000
#define DBUS_BUFFER_SIZE_RX	5000

#define DBUS_BUFFER_SIZE_TX_NOAGG	2048
#define DBUS_BUFFER_SIZE_RX_NOAGG	2048

/* DBUS types */
enum {
	DBUS_USB,
	DBUS_SDIO,
	DBUS_SPI,
	DBUS_UNKNOWN
};

enum dbus_state {
	DBUS_STATE_DL_PENDING,
	DBUS_STATE_DL_DONE,
	DBUS_STATE_UP,
	DBUS_STATE_DOWN,
	DBUS_STATE_PNP_FWDL,
	DBUS_STATE_DISCONNECT,
	DBUS_STATE_SLEEP
};

enum dbus_pnp_state {
	DBUS_PNP_DISCONNECT,
	DBUS_PNP_SLEEP,
	DBUS_PNP_RESUME,
	DBUS_PNP_HSIC_SATE,
	DBUS_PNP_HSIC_AUTOSLEEP_ENABLE,
	DBUS_PNP_HSIC_AUTOSLEEP_DISABLE,
	DBUS_PNP_HSIC_AUTOSLEEP_STATE
};

typedef enum _DEVICE_SPEED {
	INVALID_SPEED = -1,
	LOW_SPEED     =  1,	/* USB 1.1: 1.5 Mbps */
	FULL_SPEED,     	/* USB 1.1: 12  Mbps */
	HIGH_SPEED,		/* USB 2.0: 480 Mbps */
	SUPER_SPEED,		/* USB 3.0: 4.8 Gbps */
} DEVICE_SPEED;

typedef struct {
	int bustype;
	int vid;
	int pid;
	int devid;
	int chiprev; /* chip revsion number */
	int mtu;
	int nchan; /* Data Channels */
	int has_2nd_bulk_in_ep;
} dbus_attrib_t;

typedef struct {
	uint32 rx_errors;
	uint32 tx_errors;
	uint32 rx_dropped;
	uint32 tx_dropped;
} dbus_stats_t;

/*
 * Configurable BUS parameters
 */
typedef struct {
	bool rxctl_deferrespok;
} dbus_config_t;

struct dbus_callbacks;
struct exec_parms;

typedef void *(*probe_cb_t)(void *arg, const char *desc, uint32 bustype, uint32 hdrlen);
typedef void (*disconnect_cb_t)(void *arg);
typedef void *(*exec_cb_t)(struct exec_parms *args);

/* Client callbacks registered during dbus_attach() */
typedef struct dbus_callbacks {
	void (*send_complete)(void *cbarg, void *info, int status);
	void (*recv_buf)(void *cbarg, uint8 *buf, int len);
	void (*recv_pkt)(void *cbarg, void *pkt);
	void (*txflowcontrol)(void *cbarg, bool onoff);
	void (*errhandler)(void *cbarg, int err);
	void (*ctl_complete)(void *cbarg, int type, int status);
	void (*state_change)(void *cbarg, int state);
	void *(*pktget)(void *cbarg, uint len, bool send);
	void (*pktfree)(void *cbarg, void *p, bool send);
} dbus_callbacks_t;

struct dbus_pub;
struct bcmstrbuf;
struct dbus_irb;
struct dbus_irb_rx;
struct dbus_irb_tx;
struct dbus_intf_callbacks;

typedef struct {
	void* (*attach)(struct dbus_pub *pub, void *cbarg, struct dbus_intf_callbacks *cbs);
	void (*detach)(struct dbus_pub *pub, void *bus);

	int (*up)(void *bus);
	int (*down)(void *bus);
	int (*send_irb)(void *bus, struct dbus_irb_tx *txirb);
	int (*recv_irb)(void *bus, struct dbus_irb_rx *rxirb);
	int (*cancel_irb)(void *bus, struct dbus_irb_tx *txirb);
	int (*send_ctl)(void *bus, uint8 *buf, int len);
	int (*recv_ctl)(void *bus, uint8 *buf, int len);
	int (*get_stats)(void *bus, dbus_stats_t *stats);
	int (*get_attrib)(void *bus, dbus_attrib_t *attrib);

	int (*pnp)(void *bus, int event);
	int (*remove)(void *bus);
	int (*resume)(void *bus);
	int (*suspend)(void *bus);
	int (*stop)(void *bus);
	int (*reset)(void *bus);

	/* Access to bus buffers directly */
	void *(*pktget)(void *bus, int len);
	void (*pktfree)(void *bus, void *pkt);

	int  (*iovar_op)(void *bus, const char *name, void *params, int plen, void *arg, int len,
		bool set);
	void (*dump)(void *bus, struct bcmstrbuf *strbuf);
	int  (*set_config)(void *bus, dbus_config_t *config);
	int  (*get_config)(void *bus, dbus_config_t *config);

	bool (*device_exists)(void *bus);
	bool (*dlneeded)(void *bus);
	int  (*dlstart)(void *bus, uint8 *fw, int len);
	int  (*dlrun)(void *bus);
	bool (*recv_needed)(void *bus);

	void *(*exec_rxlock)(void *bus, exec_cb_t func, struct exec_parms *args);
	void *(*exec_txlock)(void *bus, exec_cb_t func, struct exec_parms *args);
	void (*set_revinfo)(void *bus, uint32 chipid, uint32 chiprev);
	void (*get_revinfo)(void *bus, uint32 *chipid, uint32 *chiprev);

	int (*tx_timer_init)(void *bus);
	int (*tx_timer_start)(void *bus, uint timeout);
	int (*tx_timer_stop)(void *bus);

	int (*sched_dpc)(void *bus);
	int (*lock)(void *bus);
	int (*unlock)(void *bus);
	int (*sched_probe_cb)(void *bus);

	int (*shutdown)(void *bus);

	int (*recv_stop)(void *bus);
	int (*recv_resume)(void *bus);

	int (*recv_irb_from_ep)(void *bus, struct dbus_irb_rx *rxirb, uint ep_idx);

	/* Add from the bottom */
} dbus_intf_t;

typedef struct dbus_pub {
	struct osl_info *osh;
	dbus_stats_t stats;
	dbus_attrib_t attrib;
	enum dbus_state busstate;
	DEVICE_SPEED device_speed;
	int ntxq, nrxq, rxsize;
	void *bus;
	struct shared_info *sh;
} dbus_pub_t;

#define BUS_INFO(bus, type) (((type *) bus)->pub->bus)

/*
 * Public Bus Function Interface
 */

extern int dbus_register(int vid, int pid, probe_cb_t prcb, disconnect_cb_t discb, void *prarg,
	void *param1, void *param2);
extern int dbus_deregister(void);

extern const dbus_pub_t *dbus_attach(struct osl_info *osh, int rxsize, int nrxq, int ntxq,
	void *cbarg, dbus_callbacks_t *cbs, struct shared_info *sh);
extern void dbus_detach(const dbus_pub_t *pub);

extern int dbus_up(const dbus_pub_t *pub);
extern int dbus_down(const dbus_pub_t *pub);
extern int dbus_stop(const dbus_pub_t *pub);
extern int dbus_shutdown(const dbus_pub_t *pub);
extern void dbus_flowctrl_rx(const dbus_pub_t *pub, bool on);

extern int dbus_send_buf(const dbus_pub_t *pub, uint8 *buf, int len, void *info);
extern int dbus_send_pkt(const dbus_pub_t *pub, void *pkt, void *info);
extern int dbus_send_ctl(const dbus_pub_t *pub, uint8 *buf, int len);
extern int dbus_recv_ctl(const dbus_pub_t *pub, uint8 *buf, int len);
extern int dbus_recv_bulk(const dbus_pub_t *pub, uint32 ep_idx);

extern int dbus_get_stats(const dbus_pub_t *pub, dbus_stats_t *stats);
extern int dbus_get_attrib(const dbus_pub_t *pub, dbus_attrib_t *attrib);
extern int dbus_get_device_speed(const dbus_pub_t *pub);
extern int dbus_set_config(const dbus_pub_t *pub, dbus_config_t *config);
extern int dbus_get_config(const dbus_pub_t *pub, dbus_config_t *config);

extern void *dbus_pktget(const dbus_pub_t *pub, int len);
extern void dbus_pktfree(const dbus_pub_t *pub, void* pkt);

extern int dbus_set_errmask(const dbus_pub_t *pub, uint32 mask);
extern int dbus_pnp_sleep(const dbus_pub_t *pub);
extern int dbus_pnp_resume(const dbus_pub_t *pub, int *fw_reload);
extern int dbus_pnp_disconnect(const dbus_pub_t *pub);
extern void dbus_set_revinfo(const dbus_pub_t *pub, uint32 chipid, uint32 chiprev);
extern void dbus_get_revinfo(const dbus_pub_t *pub, uint32 *chipid, uint32 *chiprev);

extern int dbus_iovar_op(const dbus_pub_t *pub, const char *name,
	void *params, int plen, void *arg, int len, bool set);
/*
 * Private Common Bus Interface
 */

/* IO Request Block (IRB) */
typedef struct dbus_irb {
	struct dbus_irb *next;	/* it's casted from dbus_irb_tx or dbus_irb_rx struct */
} dbus_irb_t;

typedef struct dbus_irb_rx {
	struct dbus_irb irb; /* Must be first */
	uint8 *buf;
	int buf_len;
	int actual_len;
	void *pkt;
	void *info;
	void *arg;
} dbus_irb_rx_t;

typedef struct dbus_irb_tx {
	struct dbus_irb irb; /* Must be first */
	uint8 *buf;
	int len;
	void *pkt;
	int retry_count;
	void *info;
	void *arg;
	void *send_buf; /* linear  bufffer for LINUX when aggreagtion is enabled */
} dbus_irb_tx_t;

/* DBUS interface callbacks are different from user callbacks
 * so, internally, different info can be passed to upper layer
 */
typedef struct dbus_intf_callbacks {
	void (*send_irb_timeout)(void *cbarg, dbus_irb_tx_t *txirb);
	void (*send_irb_complete)(void *cbarg, dbus_irb_tx_t *txirb, int status);
	void (*recv_irb_complete)(void *cbarg, dbus_irb_rx_t *rxirb, int status);
	void (*errhandler)(void *cbarg, int err);
	void (*ctl_complete)(void *cbarg, int type, int status);
	void (*state_change)(void *cbarg, int state);
	bool (*isr)(void *cbarg, bool *wantdpc);
	bool (*dpc)(void *cbarg, bool bounded);
	void (*watchdog)(void *cbarg);
	void *(*pktget)(void *cbarg, uint len, bool send);
	void (*pktfree)(void *cbarg, void *p, bool send);
	struct dbus_irb* (*getirb)(void *cbarg, bool send);
	void (*rxerr_indicate)(void *cbarg, bool on);
} dbus_intf_callbacks_t;

/*
 * Porting: To support new bus, port these functions below
 */

/*
 * Bus specific Interface
 * Implemented by dbus_usb.c/dbus_sdio.c
 */
extern int dbus_bus_register(int vid, int pid, probe_cb_t prcb, disconnect_cb_t discb, void *prarg,
	dbus_intf_t **intf, void *param1, void *param2);
extern int dbus_bus_deregister(void);

/*
 * Bus-specific and OS-specific Interface
 * Implemented by dbus_usb_[linux/ndis].c/dbus_sdio_[linux/ndis].c
 */
extern int dbus_bus_osl_register(int vid, int pid, probe_cb_t prcb, disconnect_cb_t discb,
	void *prarg, dbus_intf_t **intf, void *param1, void *param2);
extern int dbus_bus_osl_deregister(void);

/*
 * Bus-specific, OS-specific, HW-specific Interface
 * Mainly for SDIO Host HW controller
 */
extern int dbus_bus_osl_hw_register(int vid, int pid, probe_cb_t prcb, disconnect_cb_t discb,
	void *prarg, dbus_intf_t **intf);
extern int dbus_bus_osl_hw_deregister(void);

extern uint usbdev_bulkin_eps(void);
#endif /* __DBUS_H__ */
