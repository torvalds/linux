/* $OpenBSD: bwfmvar.h,v 1.31 2022/03/06 18:52:47 kettenis Exp $ */
/*
 * Copyright (c) 2010-2016 Broadcom Corporation
 * Copyright (c) 2016,2017 Patrick Wildt <patrick@blueri.se>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Chipcommon Core Chip IDs */
#define BRCM_CC_43143_CHIP_ID		43143
#define BRCM_CC_43235_CHIP_ID		43235
#define BRCM_CC_43236_CHIP_ID		43236
#define BRCM_CC_43238_CHIP_ID		43238
#define BRCM_CC_43241_CHIP_ID		0x4324
#define BRCM_CC_43242_CHIP_ID		43242
#define BRCM_CC_4329_CHIP_ID		0x4329
#define BRCM_CC_4330_CHIP_ID		0x4330
#define BRCM_CC_4334_CHIP_ID		0x4334
#define BRCM_CC_43340_CHIP_ID		43340
#define BRCM_CC_43341_CHIP_ID		43341
#define BRCM_CC_43362_CHIP_ID		43362
#define BRCM_CC_4335_CHIP_ID		0x4335
#define BRCM_CC_4339_CHIP_ID		0x4339
#define BRCM_CC_43430_CHIP_ID		43430
#define BRCM_CC_4345_CHIP_ID		0x4345
#define BRCM_CC_43465_CHIP_ID		43465
#define BRCM_CC_4350_CHIP_ID		0x4350
#define BRCM_CC_43525_CHIP_ID		43525
#define BRCM_CC_4354_CHIP_ID		0x4354
#define BRCM_CC_4355_CHIP_ID		0x4355
#define BRCM_CC_4356_CHIP_ID		0x4356
#define BRCM_CC_43566_CHIP_ID		43566
#define BRCM_CC_43567_CHIP_ID		43567
#define BRCM_CC_43569_CHIP_ID		43569
#define BRCM_CC_43570_CHIP_ID		43570
#define BRCM_CC_4358_CHIP_ID		0x4358
#define BRCM_CC_4359_CHIP_ID		0x4359
#define BRCM_CC_43602_CHIP_ID		43602
#define BRCM_CC_4364_CHIP_ID		0x4364
#define BRCM_CC_4365_CHIP_ID		0x4365
#define BRCM_CC_4366_CHIP_ID		0x4366
#define BRCM_CC_43664_CHIP_ID		43664
#define BRCM_CC_43666_CHIP_ID		43666
#define BRCM_CC_4371_CHIP_ID		0x4371
#define BRCM_CC_4377_CHIP_ID		0x4377
#define BRCM_CC_4378_CHIP_ID		0x4378
#define BRCM_CC_4387_CHIP_ID		0x4387
#define CY_CC_4373_CHIP_ID		0x4373
#define CY_CC_43012_CHIP_ID		43012
#define CY_CC_43752_CHIP_ID		43752

/* Defaults */
#define BWFM_DEFAULT_SCAN_CHANNEL_TIME	40
#define BWFM_DEFAULT_SCAN_UNASSOC_TIME	40
#define BWFM_DEFAULT_SCAN_PASSIVE_TIME	120


struct bwfm_softc;

struct bwfm_core {
	uint16_t	 co_id;
	uint16_t	 co_rev;
	uint32_t	 co_base;
	uint32_t	 co_wrapbase;
	LIST_ENTRY(bwfm_core) co_link;
};

struct bwfm_chip {
	uint32_t	 ch_chip;
	uint32_t	 ch_chiprev;
	uint32_t	 ch_cc_caps;
	uint32_t	 ch_cc_caps_ext;
	uint32_t	 ch_pmucaps;
	uint32_t	 ch_pmurev;
	uint32_t	 ch_rambase;
	uint32_t	 ch_ramsize;
	uint32_t	 ch_srsize;
	char		 ch_name[8];
	LIST_HEAD(,bwfm_core) ch_list;
	int (*ch_core_isup)(struct bwfm_softc *, struct bwfm_core *);
	void (*ch_core_disable)(struct bwfm_softc *, struct bwfm_core *,
	    uint32_t prereset, uint32_t reset);
	void (*ch_core_reset)(struct bwfm_softc *, struct bwfm_core *,
	    uint32_t prereset, uint32_t reset, uint32_t postreset);
};

struct bwfm_bus_ops {
	int (*bs_preinit)(struct bwfm_softc *);
	void (*bs_stop)(struct bwfm_softc *);
	int (*bs_txcheck)(struct bwfm_softc *);
	int (*bs_txdata)(struct bwfm_softc *, struct mbuf *);
	int (*bs_txctl)(struct bwfm_softc *, void *);
};

struct bwfm_buscore_ops {
	uint32_t (*bc_read)(struct bwfm_softc *, uint32_t);
	void (*bc_write)(struct bwfm_softc *, uint32_t, uint32_t);
	int (*bc_prepare)(struct bwfm_softc *);
	int (*bc_reset)(struct bwfm_softc *);
	int (*bc_setup)(struct bwfm_softc *);
	void (*bc_activate)(struct bwfm_softc *, uint32_t);
};

struct bwfm_proto_ops {
	int (*proto_query_dcmd)(struct bwfm_softc *, int, int,
	    char *, size_t *);
	int (*proto_set_dcmd)(struct bwfm_softc *, int, int,
	    char *, size_t);
	void (*proto_rx)(struct bwfm_softc *, struct mbuf *,
	    struct mbuf_list *);
	void (*proto_rxctl)(struct bwfm_softc *, char *, size_t);
};
extern struct bwfm_proto_ops bwfm_proto_bcdc_ops;

struct bwfm_host_cmd {
	void	 (*cb)(struct bwfm_softc *, void *);
	uint8_t	 data[256];
};

struct bwfm_cmd_key {
	struct ieee80211_node	 *ni;
	struct ieee80211_key	 *k;
};

struct bwfm_cmd_flowring_create {
	struct mbuf		*m;
	int			 flowid;
	int			 prio;
};

struct bwfm_cmd_flowring_delete {
	int			 flowid;
};

struct bwfm_host_cmd_ring {
#define BWFM_HOST_CMD_RING_COUNT	32
	struct bwfm_host_cmd	 cmd[BWFM_HOST_CMD_RING_COUNT];
	int			 cur;
	int			 next;
	int			 queued;
};

struct bwfm_proto_bcdc_ctl {
	int				 reqid;
	char				*buf;
	size_t				 len;
	int				 done;
	TAILQ_ENTRY(bwfm_proto_bcdc_ctl) next;
};

struct bwfm_softc {
	struct device		 sc_dev;
	struct ieee80211com	 sc_ic;
	struct ifmedia		 sc_media;
	struct bwfm_bus_ops	*sc_bus_ops;
	struct bwfm_buscore_ops	*sc_buscore_ops;
	struct bwfm_proto_ops	*sc_proto_ops;
	struct bwfm_chip	 sc_chip;
	uint8_t			 sc_io_type;
#define		BWFM_IO_TYPE_D11N		1
#define		BWFM_IO_TYPE_D11AC		2

	int			 sc_node;
	int			 sc_initialized;
	int			 sc_tx_timer;

	int			 sc_scan_ver;

	int			 (*sc_newstate)(struct ieee80211com *,
				     enum ieee80211_state, int);
	struct bwfm_host_cmd_ring sc_cmdq;
	struct taskq		*sc_taskq;
	struct task		 sc_task;
	struct mbuf_list	 sc_evml;

	int			 sc_bcdc_reqid;
	TAILQ_HEAD(, bwfm_proto_bcdc_ctl) sc_bcdc_rxctlq;

	char			 sc_fwdir[16];
	u_char			*sc_clm;
	size_t			 sc_clmsize;
	u_char			*sc_txcap;
	size_t			 sc_txcapsize;
	u_char			*sc_cal;
	size_t			 sc_calsize;
	int			 sc_key_tasks;

	char			 sc_board_type[128];
	char			 sc_module[8];
	char			 sc_vendor[8];
	char			 sc_modrev[8];
};

void bwfm_attach(struct bwfm_softc *);
void bwfm_attachhook(struct device *);
int bwfm_preinit(struct bwfm_softc *);
void bwfm_cleanup(struct bwfm_softc *);
int bwfm_detach(struct bwfm_softc *, int);
int bwfm_activate(struct bwfm_softc *, int);
int bwfm_chip_attach(struct bwfm_softc *);
int bwfm_chip_set_active(struct bwfm_softc *, uint32_t);
void bwfm_chip_set_passive(struct bwfm_softc *);
int bwfm_chip_sr_capable(struct bwfm_softc *);
struct bwfm_core *bwfm_chip_get_core(struct bwfm_softc *, int);
struct bwfm_core *bwfm_chip_get_pmu(struct bwfm_softc *);
void bwfm_rx(struct bwfm_softc *, struct mbuf *, struct mbuf_list *);
void bwfm_do_async(struct bwfm_softc *, void (*)(struct bwfm_softc *, void *),
    void *, int);
int bwfm_nvram_convert(int, u_char **, size_t *, size_t *);
int bwfm_loadfirmware(struct bwfm_softc *, const char *, const char *,
    u_char **, size_t *, u_char **, size_t *, size_t *);
