/* $Id: capilli.h,v 1.1.2.2 2004/01/16 21:09:27 keil Exp $
 * 
 * Kernel CAPI 2.0 Driver Interface for Linux
 * 
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef __CAPILLI_H__
#define __CAPILLI_H__

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/capi.h>
#include <linux/kernelcapi.h>

typedef struct capiloaddatapart {
	int user;		/* data in userspace ? */
	int len;
	unsigned char *data;
} capiloaddatapart;

typedef struct capiloaddata {
	capiloaddatapart firmware;
	capiloaddatapart configuration;
} capiloaddata;

typedef struct capicardparams {
	unsigned int port;
	unsigned irq;
	int cardtype;
	int cardnr;
	unsigned int membase;
} capicardparams;

struct capi_ctr {
	/* filled in before calling attach_capi_ctr */
	struct module *owner;
	void *driverdata;			/* driver specific */
	char name[32];				/* name of controller */
	char *driver_name;			/* name of driver */
	int (*load_firmware)(struct capi_ctr *, capiloaddata *);
	void (*reset_ctr)(struct capi_ctr *);
	void (*register_appl)(struct capi_ctr *, u16 appl,
			      capi_register_params *);
	void (*release_appl)(struct capi_ctr *, u16 appl);
	u16  (*send_message)(struct capi_ctr *, struct sk_buff *skb);
	
	char *(*procinfo)(struct capi_ctr *);
	int (*proc_show)(struct seq_file *, void *);

	/* filled in before calling ready callback */
	u8 manu[CAPI_MANUFACTURER_LEN];		/* CAPI_GET_MANUFACTURER */
	capi_version version;			/* CAPI_GET_VERSION */
	capi_profile profile;			/* CAPI_GET_PROFILE */
	u8 serial[CAPI_SERIAL_LEN];		/* CAPI_GET_SERIAL */

	/* management information for kcapi */

	unsigned long nrecvctlpkt;
	unsigned long nrecvdatapkt;
	unsigned long nsentctlpkt;
	unsigned long nsentdatapkt;

	int cnr;				/* controller number */
	unsigned short state;			/* controller state */
	int blocked;				/* output blocked */
	int traceflag;				/* capi trace */

	struct proc_dir_entry *procent;
        char procfn[128];
};

int attach_capi_ctr(struct capi_ctr *);
int detach_capi_ctr(struct capi_ctr *);

void capi_ctr_ready(struct capi_ctr * card);
void capi_ctr_down(struct capi_ctr * card);
void capi_ctr_handle_message(struct capi_ctr * card, u16 appl, struct sk_buff *skb);

// ---------------------------------------------------------------------------
// needed for AVM capi drivers

struct capi_driver {
	char name[32];				/* driver name */
	char revision[32];

	/* management information for kcapi */
	struct list_head list; 
};

#endif				/* __CAPILLI_H__ */
