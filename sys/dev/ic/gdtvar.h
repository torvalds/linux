/*	$OpenBSD: gdtvar.h,v 1.27 2024/09/01 03:08:56 jsg Exp $	*/

/*
 * Copyright (c) 1999, 2000 Niklas Hallqvist.  All rights reserved.
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
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This driver would not have written if it was not for the hardware donations
 * from both ICP-Vortex and Öko.neT.  I want to thank them for their support.
 */

#define DEVNAME(s)  ((s)->sc_dev.dv_xname)
#define GDT_CMD_RESERVE	4	/* Internal driver cmd reserve. */

#define GDT_SCRATCH_SZ 4096

#ifdef _KERNEL

/* Debugging */
/* #define GDT_DEBUG	GDT_D_IOCTL | GDT_D_INFO */
#ifdef GDT_DEBUG
#define GDT_DPRINTF(mask, args) if (gdt_debug & (mask)) printf args
#define GDT_D_INTR	0x01
#define GDT_D_MISC	0x02
#define GDT_D_CMD	0x04
#define GDT_D_QUEUE	0x08
#define GDT_D_IO	0x10
#define GDT_D_IOCTL	0x20
#define GDT_D_INFO	0x40
extern int gdt_debug;
#else
#define GDT_DPRINTF(mask, args)
#endif

/* Miscellaneous constants */
#define GDT_RETRIES		100000000	/* 100000 * 1us = 100s */
#define GDT_TIMEOUT		100000000	/* 100000 * 1us = 100s */
#define GDT_POLL_TIMEOUT	10000		/* 10000 * 1ms = 10s */
#define GDT_WATCH_TIMEOUT	10000		/* 10000 * 1ms = 10s */

/* Context structure for interrupt services */
struct gdt_intr_ctx {
	u_int32_t info, info2;
	u_int16_t cmd_status, service;
	u_int8_t istatus;
};

/*
 * A command control block, one for each corresponding command index of the
 * controller.
 */
struct gdt_ccb {
	TAILQ_ENTRY(gdt_ccb) gc_chain;
	struct scsi_xfer *gc_xs;
	bus_dmamap_t gc_dmamap_xfer;
	int gc_timeout;
	u_int32_t gc_info;
	u_int32_t gc_blockno;
	u_int32_t gc_blockcnt;
	u_int16_t gc_status;
	u_int8_t gc_service;
	u_int8_t gc_cmd_index;
	u_int8_t gc_flags;
#define GDT_GCF_CMD_MASK	0x3
#define GDT_GCF_UNUSED		0	
#define GDT_GCF_INTERNAL	1
#define GDT_GCF_SCREEN 		2
#define GDT_GCF_SCSI 		3
#define GDT_GCF_WATCHDOG 	0x4
};

static inline int gdt_ccb_set_cmd(struct gdt_ccb *, int);
static inline int
gdt_ccb_set_cmd(struct gdt_ccb *ccb, int flag)
{
	int rv = ccb->gc_flags & GDT_GCF_CMD_MASK;

	ccb->gc_flags &= ~GDT_GCF_CMD_MASK;
	ccb->gc_flags |= flag;
	return (rv);
}

struct gdt_softc {
	struct	device sc_dev;
	void   *sc_ih;

	int	sc_class;		/* Controller class */
#define GDT_ISA		0x01
#define GDT_EISA	0x02
#define GDT_PCI		0x03
#define GDT_PCINEW	0x04
#define GDT_MPR		0x05
#define GDT_CLASS_MASK	0x07
#define GDT_FC		0x10
#define GDT_CLASS(gdt)	((gdt)->sc_class & GDT_CLASS_MASK)

	bus_space_tag_t sc_dpmemt;
	bus_space_handle_t sc_dpmemh;
	bus_addr_t sc_dpmembase;
	bus_dma_tag_t sc_dmat;

	/* XXX These could go into a class-dependent opaque struct instead */
	bus_space_tag_t sc_iot;
	bus_space_handle_t sc_ioh;
	bus_addr_t sc_iobase;

	u_int16_t sc_ic_all_size;

	struct gdt_ccb sc_ccbs[GDT_MAXCMDS];
	TAILQ_HEAD(, gdt_ccb) sc_free_ccb, sc_ccbq;
	struct scsi_xfer_list sc_queue;

	struct mutex		sc_ccb_mtx;
	struct scsi_iopool	sc_iopool;

	int	sc_ndevs;

	u_int16_t sc_cmd_len;
	u_int16_t sc_cmd_off;
	u_int16_t sc_cmd_cnt;
	u_int8_t sc_cmd[GDT_CMD_SZ];

	u_int32_t sc_info;
	u_int32_t sc_info2;
	bus_dma_segment_t sc_scratch_seg;
	caddr_t sc_scratch;
	u_int16_t sc_status;

	u_int8_t sc_bus_cnt;
	u_int8_t sc_bus_id[GDT_MAXBUS];
	u_int8_t sc_more_proc;

	u_int32_t sc_total_disks;

	struct {
		u_int8_t hd_present;
		u_int8_t hd_is_logdrv;
		u_int8_t hd_is_arraydrv;
		u_int8_t hd_is_master;
		u_int8_t hd_is_parity;
		u_int8_t hd_is_hotfix;
		u_int8_t hd_master_no;
		u_int8_t hd_lock;
		u_int8_t hd_heads;
		u_int8_t hd_secs;
		u_int16_t hd_devtype;
		u_int32_t hd_size;
		u_int8_t hd_ldr_no;
		u_int8_t hd_rw_attribs;
		u_int32_t hd_start_sec;
	} sc_hdr[GDT_MAX_HDRIVES];

	struct {
		u_int8_t	ra_lock;	/* chan locked? (hot plug */
		u_int8_t	ra_phys_cnt;	/* physical disk count */
		u_int8_t	ra_local_no;	/* local channel number */
		u_int8_t	ra_io_cnt[GDT_MAXID];	/* current IO count */
		u_int32_t	ra_address;	/* channel address */
		u_int32_t	ra_id_list[GDT_MAXID];	/* IDs of phys disks */
	} sc_raw[GDT_MAXBUS];			/* SCSI channels */

	struct {
		u_int32_t cp_version;
		u_int16_t cp_state;
		u_int16_t cp_strategy;
		u_int16_t cp_write_back;
		u_int16_t cp_block_size;
	} sc_cpar;

	struct {
		u_int32_t bi_ser_no;		/* serial number */
		u_int8_t bi_oem_id[2];		/* u_int8_t OEM ID */
		u_int16_t bi_ep_flags;		/* eprom flags */
		u_int32_t bi_proc_id;		/* processor ID */
		u_int32_t bi_memsize;		/* memory size (bytes) */
		u_int8_t bi_mem_banks;		/* memory banks */
		u_int8_t bi_chan_type;		/* channel type */
		u_int8_t bi_chan_count;		/* channel count */
		u_int8_t bi_rdongle_pres;	/* dongle present */
		u_int32_t bi_epr_fw_ver;	/* (eprom) firmware ver */
		u_int32_t bi_upd_fw_ver;	/* (update) firmware ver */
		u_int32_t bi_upd_revision;	/* update revision */
		char bi_type_string[16];	/* char controller name */
		char bi_raid_string[16];	/* char RAID firmware name */
		u_int8_t bi_update_pres;	/* update present? */
		u_int8_t bi_xor_pres;		/* XOR engine present */
		u_int8_t bi_prom_type;		/* ROM type (eprom/flash) */
		u_int8_t bi_prom_count;		/* number of ROM devices */
		u_int32_t bi_dup_pres;		/* duplexing module pres? */
		u_int32_t bi_chan_pres;		/* # of exp. channels */
		u_int32_t bi_mem_pres;		/* memory expansion inst? */
		u_int8_t bi_ft_bus_system;	/* fault bus supported? */
		u_int8_t bi_subtype_valid;	/* board_subtype valid */
		u_int8_t bi_board_subtype;	/* subtype/hardware level */
		u_int8_t bi_rampar_pres;	/* RAM parity check hw? */
	} sc_binfo;

	struct {
		u_int8_t bf_chaining;	/* chaining supported */
		u_int8_t bf_striping;	/* striping (RAID-0) supported */
		u_int8_t bf_mirroring;	/* mirroring (RAID-1) supported */
		u_int8_t bf_raid;	/* RAID-4/5/10 supported */
	} sc_bfeat;

	u_int16_t sc_raw_feat;
	u_int16_t sc_cache_feat;

	void (*sc_copy_cmd)(struct gdt_softc *, struct gdt_ccb *);
	u_int8_t (*sc_get_status)(struct gdt_softc *);
	void (*sc_intr)(struct gdt_softc *, struct gdt_intr_ctx *);
	void (*sc_release_event)(struct gdt_softc *, struct gdt_ccb *);
	void (*sc_set_sema0)(struct gdt_softc *);
	int (*sc_test_busy)(struct gdt_softc *);
};

int	gdt_attach(struct gdt_softc *);
int	gdt_intr(void *);

/* These all require correctly aligned buffers */
static inline void gdt_enc16(u_int8_t *, u_int16_t);
static inline void gdt_enc32(u_int8_t *, u_int32_t);
static inline u_int8_t gdt_dec8(u_int8_t *);
static inline u_int16_t gdt_dec16(u_int8_t *);
static inline u_int32_t gdt_dec32(u_int8_t *);

static inline void
gdt_enc16(u_int8_t *addr, u_int16_t value)
{
	*(u_int16_t *)addr = htole16(value);
}

static inline void
gdt_enc32(u_int8_t *addr, u_int32_t value)
{
	*(u_int32_t *)addr = htole32(value);
}

static inline u_int8_t
gdt_dec8(u_int8_t *addr)
{
	return *(u_int8_t *)addr;
}

static inline u_int16_t
gdt_dec16(u_int8_t *addr)
{
	return letoh16(*(u_int16_t *)addr);
}

static inline u_int32_t
gdt_dec32(u_int8_t *addr)
{
	return letoh32(*(u_int32_t *)addr);
}

extern u_int8_t gdt_polling;

#endif
