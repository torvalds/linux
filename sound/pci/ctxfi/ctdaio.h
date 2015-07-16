/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 * @File	ctdaio.h
 *
 * @Brief
 * This file contains the definition of Digital Audio Input Output
 * resource management object.
 *
 * @Author	Liu Chun
 * @Date 	May 23 2008
 *
 */

#ifndef CTDAIO_H
#define CTDAIO_H

#include "ctresource.h"
#include "ctimap.h"
#include <linux/spinlock.h>
#include <linux/list.h>
#include <sound/core.h>

/* Define the descriptor of a daio resource */
enum DAIOTYP {
	LINEO1,
	LINEO2,
	LINEO3,
	LINEO4,
	SPDIFOO,	/* S/PDIF Out (Flexijack/Optical) */
	LINEIM,
	SPDIFIO,	/* S/PDIF In (Flexijack/Optical) on the card */
	MIC,		/* Dedicated mic on Titanium HD */
	SPDIFI1,	/* S/PDIF In on internal Drive Bay */
	NUM_DAIOTYP
};

struct dao_rsc_ops;
struct dai_rsc_ops;
struct daio_mgr;

struct daio {
	struct rsc rscl;	/* Basic resource info for left TX/RX */
	struct rsc rscr;	/* Basic resource info for right TX/RX */
	enum DAIOTYP type;
};

struct dao {
	struct daio daio;
	struct dao_rsc_ops *ops;	/* DAO specific operations */
	struct imapper **imappers;
	struct daio_mgr *mgr;
	struct hw *hw;
	void *ctrl_blk;
};

struct dai {
	struct daio daio;
	struct dai_rsc_ops *ops;	/* DAI specific operations */
	struct hw *hw;
	void *ctrl_blk;
};

struct dao_desc {
	unsigned int msr:4;
	unsigned int passthru:1;
};

struct dao_rsc_ops {
	int (*set_spos)(struct dao *dao, unsigned int spos);
	int (*commit_write)(struct dao *dao);
	int (*get_spos)(struct dao *dao, unsigned int *spos);
	int (*reinit)(struct dao *dao, const struct dao_desc *desc);
	int (*set_left_input)(struct dao *dao, struct rsc *input);
	int (*set_right_input)(struct dao *dao, struct rsc *input);
	int (*clear_left_input)(struct dao *dao);
	int (*clear_right_input)(struct dao *dao);
};

struct dai_rsc_ops {
	int (*set_srt_srcl)(struct dai *dai, struct rsc *src);
	int (*set_srt_srcr)(struct dai *dai, struct rsc *src);
	int (*set_srt_msr)(struct dai *dai, unsigned int msr);
	int (*set_enb_src)(struct dai *dai, unsigned int enb);
	int (*set_enb_srt)(struct dai *dai, unsigned int enb);
	int (*commit_write)(struct dai *dai);
};

/* Define daio resource request description info */
struct daio_desc {
	unsigned int type:4;
	unsigned int msr:4;
	unsigned int passthru:1;
};

struct daio_mgr {
	struct rsc_mgr mgr;	/* Basic resource manager info */
	struct snd_card *card;	/* pointer to this card */
	spinlock_t mgr_lock;
	spinlock_t imap_lock;
	struct list_head imappers;
	struct imapper *init_imap;
	unsigned int init_imap_added;

	 /* request one daio resource */
	int (*get_daio)(struct daio_mgr *mgr,
			const struct daio_desc *desc, struct daio **rdaio);
	/* return one daio resource */
	int (*put_daio)(struct daio_mgr *mgr, struct daio *daio);
	int (*daio_enable)(struct daio_mgr *mgr, struct daio *daio);
	int (*daio_disable)(struct daio_mgr *mgr, struct daio *daio);
	int (*imap_add)(struct daio_mgr *mgr, struct imapper *entry);
	int (*imap_delete)(struct daio_mgr *mgr, struct imapper *entry);
	int (*commit_write)(struct daio_mgr *mgr);
};

/* Constructor and destructor of daio resource manager */
int daio_mgr_create(struct hw *hw, struct daio_mgr **rdaio_mgr);
int daio_mgr_destroy(struct daio_mgr *daio_mgr);

#endif /* CTDAIO_H */
