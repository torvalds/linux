/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NVM_H
#define NVM_H

#include <linux/blkdev.h>
#include <linux/types.h>
#include <uapi/linux/lightnvm.h>

enum {
	NVM_IO_OK = 0,
	NVM_IO_REQUEUE = 1,
	NVM_IO_DONE = 2,
	NVM_IO_ERR = 3,

	NVM_IOTYPE_NONE = 0,
	NVM_IOTYPE_GC = 1,
};

#define NVM_BLK_BITS (16)
#define NVM_PG_BITS  (16)
#define NVM_SEC_BITS (8)
#define NVM_PL_BITS  (8)
#define NVM_LUN_BITS (8)
#define NVM_CH_BITS  (7)

enum {
	NVM_OCSSD_SPEC_12 = 12,
	NVM_OCSSD_SPEC_20 = 20,
};

struct ppa_addr {
	/* Generic structure for all addresses */
	union {
		struct {
			u64 blk		: NVM_BLK_BITS;
			u64 pg		: NVM_PG_BITS;
			u64 sec		: NVM_SEC_BITS;
			u64 pl		: NVM_PL_BITS;
			u64 lun		: NVM_LUN_BITS;
			u64 ch		: NVM_CH_BITS;
			u64 reserved	: 1;
		} g;

		struct {
			u64 line	: 63;
			u64 is_cached	: 1;
		} c;

		u64 ppa;
	};
};

struct nvm_rq;
struct nvm_id;
struct nvm_dev;
struct nvm_tgt_dev;

typedef int (nvm_id_fn)(struct nvm_dev *);
typedef int (nvm_op_bb_tbl_fn)(struct nvm_dev *, struct ppa_addr, u8 *);
typedef int (nvm_op_set_bb_fn)(struct nvm_dev *, struct ppa_addr *, int, int);
typedef int (nvm_submit_io_fn)(struct nvm_dev *, struct nvm_rq *);
typedef int (nvm_submit_io_sync_fn)(struct nvm_dev *, struct nvm_rq *);
typedef void *(nvm_create_dma_pool_fn)(struct nvm_dev *, char *);
typedef void (nvm_destroy_dma_pool_fn)(void *);
typedef void *(nvm_dev_dma_alloc_fn)(struct nvm_dev *, void *, gfp_t,
								dma_addr_t *);
typedef void (nvm_dev_dma_free_fn)(void *, void*, dma_addr_t);

struct nvm_dev_ops {
	nvm_id_fn		*identity;
	nvm_op_bb_tbl_fn	*get_bb_tbl;
	nvm_op_set_bb_fn	*set_bb_tbl;

	nvm_submit_io_fn	*submit_io;
	nvm_submit_io_sync_fn	*submit_io_sync;

	nvm_create_dma_pool_fn	*create_dma_pool;
	nvm_destroy_dma_pool_fn	*destroy_dma_pool;
	nvm_dev_dma_alloc_fn	*dev_dma_alloc;
	nvm_dev_dma_free_fn	*dev_dma_free;
};

#ifdef CONFIG_NVM

#include <linux/blkdev.h>
#include <linux/file.h>
#include <linux/dmapool.h>
#include <uapi/linux/lightnvm.h>

enum {
	/* HW Responsibilities */
	NVM_RSP_L2P	= 1 << 0,
	NVM_RSP_ECC	= 1 << 1,

	/* Physical Adressing Mode */
	NVM_ADDRMODE_LINEAR	= 0,
	NVM_ADDRMODE_CHANNEL	= 1,

	/* Plane programming mode for LUN */
	NVM_PLANE_SINGLE	= 1,
	NVM_PLANE_DOUBLE	= 2,
	NVM_PLANE_QUAD		= 4,

	/* Status codes */
	NVM_RSP_SUCCESS		= 0x0,
	NVM_RSP_NOT_CHANGEABLE	= 0x1,
	NVM_RSP_ERR_FAILWRITE	= 0x40ff,
	NVM_RSP_ERR_EMPTYPAGE	= 0x42ff,
	NVM_RSP_ERR_FAILECC	= 0x4281,
	NVM_RSP_ERR_FAILCRC	= 0x4004,
	NVM_RSP_WARN_HIGHECC	= 0x4700,

	/* Device opcodes */
	NVM_OP_PWRITE		= 0x91,
	NVM_OP_PREAD		= 0x92,
	NVM_OP_ERASE		= 0x90,

	/* PPA Command Flags */
	NVM_IO_SNGL_ACCESS	= 0x0,
	NVM_IO_DUAL_ACCESS	= 0x1,
	NVM_IO_QUAD_ACCESS	= 0x2,

	/* NAND Access Modes */
	NVM_IO_SUSPEND		= 0x80,
	NVM_IO_SLC_MODE		= 0x100,
	NVM_IO_SCRAMBLE_ENABLE	= 0x200,

	/* Block Types */
	NVM_BLK_T_FREE		= 0x0,
	NVM_BLK_T_BAD		= 0x1,
	NVM_BLK_T_GRWN_BAD	= 0x2,
	NVM_BLK_T_DEV		= 0x4,
	NVM_BLK_T_HOST		= 0x8,

	/* Memory capabilities */
	NVM_ID_CAP_SLC		= 0x1,
	NVM_ID_CAP_CMD_SUSPEND	= 0x2,
	NVM_ID_CAP_SCRAMBLE	= 0x4,
	NVM_ID_CAP_ENCRYPT	= 0x8,

	/* Memory types */
	NVM_ID_FMTYPE_SLC	= 0,
	NVM_ID_FMTYPE_MLC	= 1,

	/* Device capabilities */
	NVM_ID_DCAP_BBLKMGMT	= 0x1,
	NVM_UD_DCAP_ECC		= 0x2,
};

struct nvm_id_lp_mlc {
	u16	num_pairs;
	u8	pairs[886];
};

struct nvm_id_lp_tbl {
	__u8	id[8];
	struct nvm_id_lp_mlc mlc;
};

struct nvm_addrf_12 {
	u8	ch_len;
	u8	lun_len;
	u8	blk_len;
	u8	pg_len;
	u8	pln_len;
	u8	sect_len;

	u8	ch_offset;
	u8	lun_offset;
	u8	blk_offset;
	u8	pg_offset;
	u8	pln_offset;
	u8	sect_offset;

	u64	ch_mask;
	u64	lun_mask;
	u64	blk_mask;
	u64	pg_mask;
	u64	pln_mask;
	u64	sec_mask;
};

struct nvm_addrf {
	u8	ch_len;
	u8	lun_len;
	u8	chk_len;
	u8	sec_len;
	u8	rsv_len[2];

	u8	ch_offset;
	u8	lun_offset;
	u8	chk_offset;
	u8	sec_offset;
	u8	rsv_off[2];

	u64	ch_mask;
	u64	lun_mask;
	u64	chk_mask;
	u64	sec_mask;
	u64	rsv_mask[2];
};

struct nvm_target {
	struct list_head list;
	struct nvm_tgt_dev *dev;
	struct nvm_tgt_type *type;
	struct gendisk *disk;
};

#define ADDR_EMPTY (~0ULL)

#define NVM_TARGET_DEFAULT_OP (101)
#define NVM_TARGET_MIN_OP (3)
#define NVM_TARGET_MAX_OP (80)

#define NVM_VERSION_MAJOR 1
#define NVM_VERSION_MINOR 0
#define NVM_VERSION_PATCH 0

#define NVM_MAX_VLBA (64) /* max logical blocks in a vector command */

struct nvm_rq;
typedef void (nvm_end_io_fn)(struct nvm_rq *);

struct nvm_rq {
	struct nvm_tgt_dev *dev;

	struct bio *bio;

	union {
		struct ppa_addr ppa_addr;
		dma_addr_t dma_ppa_list;
	};

	struct ppa_addr *ppa_list;

	void *meta_list;
	dma_addr_t dma_meta_list;

	nvm_end_io_fn *end_io;

	uint8_t opcode;
	uint16_t nr_ppas;
	uint16_t flags;

	u64 ppa_status; /* ppa media status */
	int error;

	void *private;
};

static inline struct nvm_rq *nvm_rq_from_pdu(void *pdu)
{
	return pdu - sizeof(struct nvm_rq);
}

static inline void *nvm_rq_to_pdu(struct nvm_rq *rqdata)
{
	return rqdata + 1;
}

enum {
	NVM_BLK_ST_FREE =	0x1,	/* Free block */
	NVM_BLK_ST_TGT =	0x2,	/* Block in use by target */
	NVM_BLK_ST_BAD =	0x8,	/* Bad block */
};

/* Instance geometry */
struct nvm_geo {
	/* device reported version */
	u8	major_ver_id;
	u8	minor_ver_id;

	/* kernel short version */
	u8	version;

	/* instance specific geometry */
	int nr_chnls;
	int nr_luns;		/* per channel */

	/* calculated values */
	int all_luns;		/* across channels */
	int all_chunks;		/* across channels */

	int op;			/* over-provision in instance */

	sector_t total_secs;	/* across channels */

	/* chunk geometry */
	u32	nr_chks;	/* chunks per lun */
	u32	clba;		/* sectors per chunk */
	u16	csecs;		/* sector size */
	u16	sos;		/* out-of-band area size */

	/* device write constrains */
	u32	ws_min;		/* minimum write size */
	u32	ws_opt;		/* optimal write size */
	u32	mw_cunits;	/* distance required for successful read */
	u32	maxoc;		/* maximum open chunks */
	u32	maxocpu;	/* maximum open chunks per parallel unit */

	/* device capabilities */
	u32	mccap;

	/* device timings */
	u32	trdt;		/* Avg. Tread (ns) */
	u32	trdm;		/* Max Tread (ns) */
	u32	tprt;		/* Avg. Tprog (ns) */
	u32	tprm;		/* Max Tprog (ns) */
	u32	tbet;		/* Avg. Terase (ns) */
	u32	tbem;		/* Max Terase (ns) */

	/* generic address format */
	struct nvm_addrf addrf;

	/* 1.2 compatibility */
	u8	vmnt;
	u32	cap;
	u32	dom;

	u8	mtype;
	u8	fmtype;

	u16	cpar;
	u32	mpos;

	u8	num_pln;
	u8	plane_mode;
	u16	num_pg;
	u16	fpg_sz;
};

/* sub-device structure */
struct nvm_tgt_dev {
	/* Device information */
	struct nvm_geo geo;

	/* Base ppas for target LUNs */
	struct ppa_addr *luns;

	struct request_queue *q;

	struct nvm_dev *parent;
	void *map;
};

struct nvm_dev {
	struct nvm_dev_ops *ops;

	struct list_head devices;

	/* Device information */
	struct nvm_geo geo;

	unsigned long *lun_map;
	void *dma_pool;

	/* Backend device */
	struct request_queue *q;
	char name[DISK_NAME_LEN];
	void *private_data;

	void *rmap;

	struct mutex mlock;
	spinlock_t lock;

	/* target management */
	struct list_head area_list;
	struct list_head targets;
};

static inline struct ppa_addr generic_to_dev_addr(struct nvm_tgt_dev *tgt_dev,
						  struct ppa_addr r)
{
	struct nvm_geo *geo = &tgt_dev->geo;
	struct nvm_addrf_12 *ppaf = (struct nvm_addrf_12 *)&geo->addrf;
	struct ppa_addr l;

	l.ppa = ((u64)r.g.ch) << ppaf->ch_offset;
	l.ppa |= ((u64)r.g.lun) << ppaf->lun_offset;
	l.ppa |= ((u64)r.g.blk) << ppaf->blk_offset;
	l.ppa |= ((u64)r.g.pg) << ppaf->pg_offset;
	l.ppa |= ((u64)r.g.pl) << ppaf->pln_offset;
	l.ppa |= ((u64)r.g.sec) << ppaf->sect_offset;

	return l;
}

static inline struct ppa_addr dev_to_generic_addr(struct nvm_tgt_dev *tgt_dev,
						  struct ppa_addr r)
{
	struct nvm_geo *geo = &tgt_dev->geo;
	struct nvm_addrf_12 *ppaf = (struct nvm_addrf_12 *)&geo->addrf;
	struct ppa_addr l;

	l.ppa = 0;

	l.g.ch = (r.ppa & ppaf->ch_mask) >> ppaf->ch_offset;
	l.g.lun = (r.ppa & ppaf->lun_mask) >> ppaf->lun_offset;
	l.g.blk = (r.ppa & ppaf->blk_mask) >> ppaf->blk_offset;
	l.g.pg = (r.ppa & ppaf->pg_mask) >> ppaf->pg_offset;
	l.g.pl = (r.ppa & ppaf->pln_mask) >> ppaf->pln_offset;
	l.g.sec = (r.ppa & ppaf->sec_mask) >> ppaf->sect_offset;

	return l;
}

typedef blk_qc_t (nvm_tgt_make_rq_fn)(struct request_queue *, struct bio *);
typedef sector_t (nvm_tgt_capacity_fn)(void *);
typedef void *(nvm_tgt_init_fn)(struct nvm_tgt_dev *, struct gendisk *,
				int flags);
typedef void (nvm_tgt_exit_fn)(void *);
typedef int (nvm_tgt_sysfs_init_fn)(struct gendisk *);
typedef void (nvm_tgt_sysfs_exit_fn)(struct gendisk *);

struct nvm_tgt_type {
	const char *name;
	unsigned int version[3];

	/* target entry points */
	nvm_tgt_make_rq_fn *make_rq;
	nvm_tgt_capacity_fn *capacity;

	/* module-specific init/teardown */
	nvm_tgt_init_fn *init;
	nvm_tgt_exit_fn *exit;

	/* sysfs */
	nvm_tgt_sysfs_init_fn *sysfs_init;
	nvm_tgt_sysfs_exit_fn *sysfs_exit;

	/* For internal use */
	struct list_head list;
	struct module *owner;
};

extern int nvm_register_tgt_type(struct nvm_tgt_type *);
extern void nvm_unregister_tgt_type(struct nvm_tgt_type *);

extern void *nvm_dev_dma_alloc(struct nvm_dev *, gfp_t, dma_addr_t *);
extern void nvm_dev_dma_free(struct nvm_dev *, void *, dma_addr_t);

extern struct nvm_dev *nvm_alloc_dev(int);
extern int nvm_register(struct nvm_dev *);
extern void nvm_unregister(struct nvm_dev *);

extern int nvm_set_tgt_bb_tbl(struct nvm_tgt_dev *, struct ppa_addr *,
			      int, int);
extern int nvm_submit_io(struct nvm_tgt_dev *, struct nvm_rq *);
extern int nvm_submit_io_sync(struct nvm_tgt_dev *, struct nvm_rq *);
extern void nvm_end_io(struct nvm_rq *);
extern int nvm_bb_tbl_fold(struct nvm_dev *, u8 *, int);
extern int nvm_get_tgt_bb_tbl(struct nvm_tgt_dev *, struct ppa_addr, u8 *);

#else /* CONFIG_NVM */
struct nvm_dev_ops;

static inline struct nvm_dev *nvm_alloc_dev(int node)
{
	return ERR_PTR(-EINVAL);
}
static inline int nvm_register(struct nvm_dev *dev)
{
	return -EINVAL;
}
static inline void nvm_unregister(struct nvm_dev *dev) {}
#endif /* CONFIG_NVM */
#endif /* LIGHTNVM.H */
