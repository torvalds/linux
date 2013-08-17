/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 * @File	ctsrc.h
 *
 * @Brief
 * This file contains the definition of the Sample Rate Convertor
 * resource management object.
 *
 * @Author	Liu Chun
 * @Date 	May 13 2008
 *
 */

#ifndef CTSRC_H
#define CTSRC_H

#include "ctresource.h"
#include "ctimap.h"
#include <linux/spinlock.h>
#include <linux/list.h>

#define SRC_STATE_OFF	0x0
#define SRC_STATE_INIT	0x4
#define SRC_STATE_RUN	0x5

#define SRC_SF_U8	0x0
#define SRC_SF_S16	0x1
#define SRC_SF_S24	0x2
#define SRC_SF_S32	0x3
#define SRC_SF_F32	0x4

/* Define the descriptor of a src resource */
enum SRCMODE {
	MEMRD,		/* Read data from host memory */
	MEMWR,		/* Write data to host memory */
	ARCRW,		/* Read from and write to audio ring channel */
	NUM_SRCMODES
};

struct src_rsc_ops;

struct src {
	struct rsc rsc; /* Basic resource info */
	struct src *intlv; /* Pointer to next interleaved SRC in a series */
	struct src_rsc_ops *ops; /* SRC specific operations */
	/* Number of contiguous srcs for interleaved usage */
	unsigned char multi;
	unsigned char mode; /* Working mode of this SRC resource */
};

struct src_rsc_ops {
	int (*set_state)(struct src *src, unsigned int state);
	int (*set_bm)(struct src *src, unsigned int bm);
	int (*set_sf)(struct src *src, unsigned int sf);
	int (*set_pm)(struct src *src, unsigned int pm);
	int (*set_rom)(struct src *src, unsigned int rom);
	int (*set_vo)(struct src *src, unsigned int vo);
	int (*set_st)(struct src *src, unsigned int st);
	int (*set_bp)(struct src *src, unsigned int bp);
	int (*set_cisz)(struct src *src, unsigned int cisz);
	int (*set_ca)(struct src *src, unsigned int ca);
	int (*set_sa)(struct src *src, unsigned int sa);
	int (*set_la)(struct src *src, unsigned int la);
	int (*set_pitch)(struct src *src, unsigned int pitch);
	int (*set_clr_zbufs)(struct src *src);
	int (*commit_write)(struct src *src);
	int (*get_ca)(struct src *src);
	int (*init)(struct src *src);
	struct src* (*next_interleave)(struct src *src);
};

/* Define src resource request description info */
struct src_desc {
	/* Number of contiguous master srcs for interleaved usage */
	unsigned char multi;
	unsigned char msr;
	unsigned char mode; /* Working mode of the requested srcs */
};

/* Define src manager object */
struct src_mgr {
	struct rsc_mgr mgr;	/* Basic resource manager info */
	spinlock_t mgr_lock;

	 /* request src resource */
	int (*get_src)(struct src_mgr *mgr,
		       const struct src_desc *desc, struct src **rsrc);
	/* return src resource */
	int (*put_src)(struct src_mgr *mgr, struct src *src);
	int (*src_enable_s)(struct src_mgr *mgr, struct src *src);
	int (*src_enable)(struct src_mgr *mgr, struct src *src);
	int (*src_disable)(struct src_mgr *mgr, struct src *src);
	int (*commit_write)(struct src_mgr *mgr);
};

/* Define the descriptor of a SRC Input Mapper resource */
struct srcimp_mgr;
struct srcimp_rsc_ops;

struct srcimp {
	struct rsc rsc;
	unsigned char idx[8];
	struct imapper *imappers;
	unsigned int mapped; /* A bit-map indicating which conj rsc is mapped */
	struct srcimp_mgr *mgr;
	struct srcimp_rsc_ops *ops;
};

struct srcimp_rsc_ops {
	int (*map)(struct srcimp *srcimp, struct src *user, struct rsc *input);
	int (*unmap)(struct srcimp *srcimp);
};

/* Define SRCIMP resource request description info */
struct srcimp_desc {
	unsigned int msr;
};

struct srcimp_mgr {
	struct rsc_mgr mgr;	/* Basic resource manager info */
	spinlock_t mgr_lock;
	spinlock_t imap_lock;
	struct list_head imappers;
	struct imapper *init_imap;
	unsigned int init_imap_added;

	 /* request srcimp resource */
	int (*get_srcimp)(struct srcimp_mgr *mgr,
			  const struct srcimp_desc *desc,
			  struct srcimp **rsrcimp);
	/* return srcimp resource */
	int (*put_srcimp)(struct srcimp_mgr *mgr, struct srcimp *srcimp);
	int (*imap_add)(struct srcimp_mgr *mgr, struct imapper *entry);
	int (*imap_delete)(struct srcimp_mgr *mgr, struct imapper *entry);
};

/* Constructor and destructor of SRC resource manager */
int src_mgr_create(void *hw, struct src_mgr **rsrc_mgr);
int src_mgr_destroy(struct src_mgr *src_mgr);
/* Constructor and destructor of SRCIMP resource manager */
int srcimp_mgr_create(void *hw, struct srcimp_mgr **rsrc_mgr);
int srcimp_mgr_destroy(struct srcimp_mgr *srcimp_mgr);

#endif /* CTSRC_H */
