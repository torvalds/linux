/*	$OpenBSD: amivar.h,v 1.60 2020/07/22 13:16:04 krw Exp $	*/

/*
 * Copyright (c) 2001 Michael Shalayeff
 * Copyright (c) 2005 Marco Peereboom
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

struct ami_mem {
	bus_dmamap_t		am_map;
	bus_dma_segment_t	am_seg;
	size_t			am_size;
	caddr_t			am_kva;
};

#define AMIMEM_MAP(_am)		((_am)->am_map)
#define AMIMEM_DVA(_am)		((_am)->am_map->dm_segs[0].ds_addr)
#define AMIMEM_KVA(_am)		((void *)(_am)->am_kva)

struct ami_ccbmem {
	struct ami_passthrough	cd_pt;
	struct ami_sgent	cd_sg[AMI_SGEPERCMD];
};

struct ami_softc;

struct ami_ccb {
	struct ami_softc	*ccb_sc;

	struct ami_iocmd	ccb_cmd;
	struct ami_passthrough	*ccb_pt;
	paddr_t			ccb_ptpa;
	struct ami_sgent	*ccb_sglist;
	paddr_t			ccb_sglistpa;
	int			ccb_offset;
	bus_dmamap_t		ccb_dmamap;

	struct scsi_xfer	*ccb_xs;
	void			(*ccb_done)(struct ami_softc *sc,
				    struct ami_ccb *ccb);

	volatile enum {
		AMI_CCB_FREE,
		AMI_CCB_READY,
		AMI_CCB_QUEUED,
		AMI_CCB_PREQUEUED
	}			ccb_state;
	int			ccb_flags;
#define AMI_CCB_F_ERR			(1<<0)
	int			ccb_status;
	TAILQ_ENTRY(ami_ccb)	ccb_link;
};

TAILQ_HEAD(ami_ccb_list, ami_ccb);

struct ami_rawsoftc {
	struct ami_softc	*sc_softc;
	u_int8_t		sc_channel;

	int			sc_proctarget;	/* ses/safte target id */
	char			sc_procdev[16];	/* ses/safte device */
};

struct ami_softc {
	struct device		sc_dev;
	void			*sc_ih;
	struct scsibus_softc	*sc_scsibus;

	int			sc_flags;
#define AMI_CHECK_SIGN	0x0001
#define AMI_BROKEN 	0x0002
#define AMI_QUARTZ	0x0008

	/* low-level interface */
	int			(*sc_init)(struct ami_softc *sc);
	int			(*sc_exec)(struct ami_softc *sc,
				    struct ami_iocmd *);
	int			(*sc_done)(struct ami_softc *sc,
				    struct ami_iocmd *);
	int			(*sc_poll)(struct ami_softc *sc,
				    struct ami_iocmd *);
	int			(*sc_ioctl)(struct device *, u_long, caddr_t);

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_dma_tag_t		sc_dmat;

	struct ami_ccb		*sc_ccbs;
	struct ami_ccb_list	sc_ccb_freeq;
	struct mutex		sc_ccb_freeq_mtx;

	struct ami_mem		*sc_mbox_am;
	volatile struct ami_iocmd *sc_mbox;
	paddr_t			sc_mbox_pa;

	struct ami_ccb_list	sc_ccb_preq, sc_ccb_runq;
	struct mutex		sc_cmd_mtx;

	struct scsi_iopool	sc_iopool;

	struct ami_mem		*sc_ccbmem_am;

	int			sc_timeout;
	struct timeout		sc_run_tmo;
	int			sc_dis_poll;

	struct rwlock		sc_lock;

	char			sc_fwver[16];
	char			sc_biosver[16];
	int			sc_maxcmds;
	int			sc_memory;
	int			sc_targets;
	int			sc_channels;
	int			sc_maxunits;
	int			sc_nunits;
	struct {
		u_int8_t		hd_present;
		u_int8_t		hd_is_logdrv;
		u_int8_t		hd_prop;
		u_int8_t		hd_stat;
		u_int32_t		hd_size;
		char			dev[16];
	}			sc_hdr[AMI_BIG_MAX_LDRIVES];
	struct ami_rawsoftc	*sc_rawsoftcs;

	struct ksensor		*sc_sensors;
	struct ksensordev	sc_sensordev;
	struct ami_big_diskarray *sc_bd;

	/* bio stuff */
	struct bioc_inq		sc_bi;
	char			sc_plist[AMI_BIG_MAX_PDRIVES];

	struct ami_ccb		*sc_mgmtccb;
	int			sc_drainio;
	u_int8_t		sc_drvinscnt;
};

int  ami_attach(struct ami_softc *sc);
int  ami_intr(void *);

int ami_quartz_init(struct ami_softc *sc);
int ami_quartz_exec(struct ami_softc *sc, struct ami_iocmd *);
int ami_quartz_done(struct ami_softc *sc, struct ami_iocmd *);
int ami_quartz_poll(struct ami_softc *sc, struct ami_iocmd *);

int ami_schwartz_init(struct ami_softc *sc);
int ami_schwartz_exec(struct ami_softc *sc, struct ami_iocmd *);
int ami_schwartz_done(struct ami_softc *sc, struct ami_iocmd *);
int ami_schwartz_poll(struct ami_softc *sc, struct ami_iocmd *);

#ifdef AMI_DEBUG
void ami_print_mbox(struct ami_iocmd *);
#endif /* AMI_DEBUG */
