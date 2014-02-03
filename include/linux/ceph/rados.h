#ifndef CEPH_RADOS_H
#define CEPH_RADOS_H

/*
 * Data types for the Ceph distributed object storage layer RADOS
 * (Reliable Autonomic Distributed Object Store).
 */

#include <linux/ceph/msgr.h>

/*
 * fs id
 */
struct ceph_fsid {
	unsigned char fsid[16];
};

static inline int ceph_fsid_compare(const struct ceph_fsid *a,
				    const struct ceph_fsid *b)
{
	return memcmp(a, b, sizeof(*a));
}

/*
 * ino, object, etc.
 */
typedef __le64 ceph_snapid_t;
#define CEPH_SNAPDIR ((__u64)(-1))  /* reserved for hidden .snap dir */
#define CEPH_NOSNAP  ((__u64)(-2))  /* "head", "live" revision */
#define CEPH_MAXSNAP ((__u64)(-3))  /* largest valid snapid */

struct ceph_timespec {
	__le32 tv_sec;
	__le32 tv_nsec;
} __attribute__ ((packed));


/*
 * object layout - how objects are mapped into PGs
 */
#define CEPH_OBJECT_LAYOUT_HASH     1
#define CEPH_OBJECT_LAYOUT_LINEAR   2
#define CEPH_OBJECT_LAYOUT_HASHINO  3

/*
 * pg layout -- how PGs are mapped onto (sets of) OSDs
 */
#define CEPH_PG_LAYOUT_CRUSH  0
#define CEPH_PG_LAYOUT_HASH   1
#define CEPH_PG_LAYOUT_LINEAR 2
#define CEPH_PG_LAYOUT_HYBRID 3

#define CEPH_PG_MAX_SIZE      16  /* max # osds in a single pg */

/*
 * placement group.
 * we encode this into one __le64.
 */
struct ceph_pg_v1 {
	__le16 preferred; /* preferred primary osd */
	__le16 ps;        /* placement seed */
	__le32 pool;      /* object pool */
} __attribute__ ((packed));

/*
 * pg_pool is a set of pgs storing a pool of objects
 *
 *  pg_num -- base number of pseudorandomly placed pgs
 *
 *  pgp_num -- effective number when calculating pg placement.  this
 * is used for pg_num increases.  new pgs result in data being "split"
 * into new pgs.  for this to proceed smoothly, new pgs are intiially
 * colocated with their parents; that is, pgp_num doesn't increase
 * until the new pgs have successfully split.  only _then_ are the new
 * pgs placed independently.
 *
 *  lpg_num -- localized pg count (per device).  replicas are randomly
 * selected.
 *
 *  lpgp_num -- as above.
 */
#define CEPH_NOPOOL  ((__u64) (-1))  /* pool id not defined */

#define CEPH_PG_TYPE_REP     1
#define CEPH_PG_TYPE_RAID4   2

/*
 * stable_mod func is used to control number of placement groups.
 * similar to straight-up modulo, but produces a stable mapping as b
 * increases over time.  b is the number of bins, and bmask is the
 * containing power of 2 minus 1.
 *
 * b <= bmask and bmask=(2**n)-1
 * e.g., b=12 -> bmask=15, b=123 -> bmask=127
 */
static inline int ceph_stable_mod(int x, int b, int bmask)
{
	if ((x & bmask) < b)
		return x & bmask;
	else
		return x & (bmask >> 1);
}

/*
 * object layout - how a given object should be stored.
 */
struct ceph_object_layout {
	struct ceph_pg_v1 ol_pgid;   /* raw pg, with _full_ ps precision. */
	__le32 ol_stripe_unit;    /* for per-object parity, if any */
} __attribute__ ((packed));

/*
 * compound epoch+version, used by storage layer to serialize mutations
 */
struct ceph_eversion {
	__le32 epoch;
	__le64 version;
} __attribute__ ((packed));

/*
 * osd map bits
 */

/* status bits */
#define CEPH_OSD_EXISTS  (1<<0)
#define CEPH_OSD_UP      (1<<1)
#define CEPH_OSD_AUTOOUT (1<<2)  /* osd was automatically marked out */
#define CEPH_OSD_NEW     (1<<3)  /* osd is new, never marked in */

extern const char *ceph_osd_state_name(int s);

/* osd weights.  fixed point value: 0x10000 == 1.0 ("in"), 0 == "out" */
#define CEPH_OSD_IN  0x10000
#define CEPH_OSD_OUT 0


/*
 * osd map flag bits
 */
#define CEPH_OSDMAP_NEARFULL (1<<0)  /* sync writes (near ENOSPC) */
#define CEPH_OSDMAP_FULL     (1<<1)  /* no data writes (ENOSPC) */
#define CEPH_OSDMAP_PAUSERD  (1<<2)  /* pause all reads */
#define CEPH_OSDMAP_PAUSEWR  (1<<3)  /* pause all writes */
#define CEPH_OSDMAP_PAUSEREC (1<<4)  /* pause recovery */
#define CEPH_OSDMAP_NOUP     (1<<5)  /* block osd boot */
#define CEPH_OSDMAP_NODOWN   (1<<6)  /* block osd mark-down/failure */
#define CEPH_OSDMAP_NOOUT    (1<<7)  /* block osd auto mark-out */
#define CEPH_OSDMAP_NOIN     (1<<8)  /* block osd auto mark-in */
#define CEPH_OSDMAP_NOBACKFILL (1<<9) /* block osd backfill */
#define CEPH_OSDMAP_NORECOVER (1<<10) /* block osd recovery and backfill */

/*
 * The error code to return when an OSD can't handle a write
 * because it is too large.
 */
#define OSD_WRITETOOBIG EMSGSIZE

/*
 * osd ops
 *
 * WARNING: do not use these op codes directly.  Use the helpers
 * defined below instead.  In certain cases, op code behavior was
 * redefined, resulting in special-cases in the helpers.
 */
#define CEPH_OSD_OP_MODE       0xf000
#define CEPH_OSD_OP_MODE_RD    0x1000
#define CEPH_OSD_OP_MODE_WR    0x2000
#define CEPH_OSD_OP_MODE_RMW   0x3000
#define CEPH_OSD_OP_MODE_SUB   0x4000

#define CEPH_OSD_OP_TYPE       0x0f00
#define CEPH_OSD_OP_TYPE_LOCK  0x0100
#define CEPH_OSD_OP_TYPE_DATA  0x0200
#define CEPH_OSD_OP_TYPE_ATTR  0x0300
#define CEPH_OSD_OP_TYPE_EXEC  0x0400
#define CEPH_OSD_OP_TYPE_PG    0x0500
#define CEPH_OSD_OP_TYPE_MULTI 0x0600 /* multiobject */

enum {
	/** data **/
	/* read */
	CEPH_OSD_OP_READ      = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 1,
	CEPH_OSD_OP_STAT      = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 2,
	CEPH_OSD_OP_MAPEXT    = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 3,

	/* fancy read */
	CEPH_OSD_OP_MASKTRUNC   = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 4,
	CEPH_OSD_OP_SPARSE_READ = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 5,

	CEPH_OSD_OP_NOTIFY    = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 6,
	CEPH_OSD_OP_NOTIFY_ACK = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 7,

	/* versioning */
	CEPH_OSD_OP_ASSERT_VER = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 8,

	/* write */
	CEPH_OSD_OP_WRITE     = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 1,
	CEPH_OSD_OP_WRITEFULL = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 2,
	CEPH_OSD_OP_TRUNCATE  = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 3,
	CEPH_OSD_OP_ZERO      = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 4,
	CEPH_OSD_OP_DELETE    = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 5,

	/* fancy write */
	CEPH_OSD_OP_APPEND    = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 6,
	CEPH_OSD_OP_STARTSYNC = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 7,
	CEPH_OSD_OP_SETTRUNC  = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 8,
	CEPH_OSD_OP_TRIMTRUNC = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 9,

	CEPH_OSD_OP_TMAPUP  = CEPH_OSD_OP_MODE_RMW | CEPH_OSD_OP_TYPE_DATA | 10,
	CEPH_OSD_OP_TMAPPUT = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 11,
	CEPH_OSD_OP_TMAPGET = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 12,

	CEPH_OSD_OP_CREATE  = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 13,
	CEPH_OSD_OP_ROLLBACK= CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 14,

	CEPH_OSD_OP_WATCH   = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 15,

	/* omap */
	CEPH_OSD_OP_OMAPGETKEYS   = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 17,
	CEPH_OSD_OP_OMAPGETVALS   = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 18,
	CEPH_OSD_OP_OMAPGETHEADER = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 19,
	CEPH_OSD_OP_OMAPGETVALSBYKEYS  =
	  CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 20,
	CEPH_OSD_OP_OMAPSETVALS   = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 21,
	CEPH_OSD_OP_OMAPSETHEADER = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 22,
	CEPH_OSD_OP_OMAPCLEAR     = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 23,
	CEPH_OSD_OP_OMAPRMKEYS    = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_DATA | 24,
	CEPH_OSD_OP_OMAP_CMP      = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_DATA | 25,

	/** multi **/
	CEPH_OSD_OP_CLONERANGE = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_MULTI | 1,
	CEPH_OSD_OP_ASSERT_SRC_VERSION = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_MULTI | 2,
	CEPH_OSD_OP_SRC_CMPXATTR = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_MULTI | 3,

	/** attrs **/
	/* read */
	CEPH_OSD_OP_GETXATTR  = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_ATTR | 1,
	CEPH_OSD_OP_GETXATTRS = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_ATTR | 2,
	CEPH_OSD_OP_CMPXATTR  = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_ATTR | 3,

	/* write */
	CEPH_OSD_OP_SETXATTR  = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_ATTR | 1,
	CEPH_OSD_OP_SETXATTRS = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_ATTR | 2,
	CEPH_OSD_OP_RESETXATTRS = CEPH_OSD_OP_MODE_WR|CEPH_OSD_OP_TYPE_ATTR | 3,
	CEPH_OSD_OP_RMXATTR   = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_ATTR | 4,

	/** subop **/
	CEPH_OSD_OP_PULL            = CEPH_OSD_OP_MODE_SUB | 1,
	CEPH_OSD_OP_PUSH            = CEPH_OSD_OP_MODE_SUB | 2,
	CEPH_OSD_OP_BALANCEREADS    = CEPH_OSD_OP_MODE_SUB | 3,
	CEPH_OSD_OP_UNBALANCEREADS  = CEPH_OSD_OP_MODE_SUB | 4,
	CEPH_OSD_OP_SCRUB           = CEPH_OSD_OP_MODE_SUB | 5,
	CEPH_OSD_OP_SCRUB_RESERVE   = CEPH_OSD_OP_MODE_SUB | 6,
	CEPH_OSD_OP_SCRUB_UNRESERVE = CEPH_OSD_OP_MODE_SUB | 7,
	CEPH_OSD_OP_SCRUB_STOP      = CEPH_OSD_OP_MODE_SUB | 8,
	CEPH_OSD_OP_SCRUB_MAP     = CEPH_OSD_OP_MODE_SUB | 9,

	/** lock **/
	CEPH_OSD_OP_WRLOCK    = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_LOCK | 1,
	CEPH_OSD_OP_WRUNLOCK  = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_LOCK | 2,
	CEPH_OSD_OP_RDLOCK    = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_LOCK | 3,
	CEPH_OSD_OP_RDUNLOCK  = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_LOCK | 4,
	CEPH_OSD_OP_UPLOCK    = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_LOCK | 5,
	CEPH_OSD_OP_DNLOCK    = CEPH_OSD_OP_MODE_WR | CEPH_OSD_OP_TYPE_LOCK | 6,

	/** exec **/
	/* note: the RD bit here is wrong; see special-case below in helper */
	CEPH_OSD_OP_CALL    = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_EXEC | 1,

	/** pg **/
	CEPH_OSD_OP_PGLS      = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_PG | 1,
	CEPH_OSD_OP_PGLS_FILTER = CEPH_OSD_OP_MODE_RD | CEPH_OSD_OP_TYPE_PG | 2,
};

static inline int ceph_osd_op_type_lock(int op)
{
	return (op & CEPH_OSD_OP_TYPE) == CEPH_OSD_OP_TYPE_LOCK;
}
static inline int ceph_osd_op_type_data(int op)
{
	return (op & CEPH_OSD_OP_TYPE) == CEPH_OSD_OP_TYPE_DATA;
}
static inline int ceph_osd_op_type_attr(int op)
{
	return (op & CEPH_OSD_OP_TYPE) == CEPH_OSD_OP_TYPE_ATTR;
}
static inline int ceph_osd_op_type_exec(int op)
{
	return (op & CEPH_OSD_OP_TYPE) == CEPH_OSD_OP_TYPE_EXEC;
}
static inline int ceph_osd_op_type_pg(int op)
{
	return (op & CEPH_OSD_OP_TYPE) == CEPH_OSD_OP_TYPE_PG;
}
static inline int ceph_osd_op_type_multi(int op)
{
	return (op & CEPH_OSD_OP_TYPE) == CEPH_OSD_OP_TYPE_MULTI;
}

static inline int ceph_osd_op_mode_subop(int op)
{
	return (op & CEPH_OSD_OP_MODE) == CEPH_OSD_OP_MODE_SUB;
}
static inline int ceph_osd_op_mode_read(int op)
{
	return (op & CEPH_OSD_OP_MODE_RD) &&
		op != CEPH_OSD_OP_CALL;
}
static inline int ceph_osd_op_mode_modify(int op)
{
	return op & CEPH_OSD_OP_MODE_WR;
}

/*
 * note that the following tmap stuff is also defined in the ceph librados.h
 * any modification here needs to be updated there
 */
#define CEPH_OSD_TMAP_HDR 'h'
#define CEPH_OSD_TMAP_SET 's'
#define CEPH_OSD_TMAP_CREATE 'c' /* create key */
#define CEPH_OSD_TMAP_RM  'r'
#define CEPH_OSD_TMAP_RMSLOPPY 'R'

extern const char *ceph_osd_op_name(int op);

/*
 * osd op flags
 *
 * An op may be READ, WRITE, or READ|WRITE.
 */
enum {
	CEPH_OSD_FLAG_ACK =            0x0001,  /* want (or is) "ack" ack */
	CEPH_OSD_FLAG_ONNVRAM =        0x0002,  /* want (or is) "onnvram" ack */
	CEPH_OSD_FLAG_ONDISK =         0x0004,  /* want (or is) "ondisk" ack */
	CEPH_OSD_FLAG_RETRY =          0x0008,  /* resend attempt */
	CEPH_OSD_FLAG_READ =           0x0010,  /* op may read */
	CEPH_OSD_FLAG_WRITE =          0x0020,  /* op may write */
	CEPH_OSD_FLAG_ORDERSNAP =      0x0040,  /* EOLDSNAP if snapc is out of order */
	CEPH_OSD_FLAG_PEERSTAT_OLD =   0x0080,  /* DEPRECATED msg includes osd_peer_stat */
	CEPH_OSD_FLAG_BALANCE_READS =  0x0100,
	CEPH_OSD_FLAG_PARALLELEXEC =   0x0200,  /* execute op in parallel */
	CEPH_OSD_FLAG_PGOP =           0x0400,  /* pg op, no object */
	CEPH_OSD_FLAG_EXEC =           0x0800,  /* op may exec */
	CEPH_OSD_FLAG_EXEC_PUBLIC =    0x1000,  /* DEPRECATED op may exec (public) */
	CEPH_OSD_FLAG_LOCALIZE_READS = 0x2000,  /* read from nearby replica, if any */
	CEPH_OSD_FLAG_RWORDERED =      0x4000,  /* order wrt concurrent reads */
	CEPH_OSD_FLAG_IGNORE_CACHE =   0x8000,  /* ignore cache logic */
	CEPH_OSD_FLAG_SKIPRWLOCKS =   0x10000,  /* skip rw locks */
	CEPH_OSD_FLAG_IGNORE_OVERLAY = 0x20000, /* ignore pool overlay */
	CEPH_OSD_FLAG_FLUSH =         0x40000,  /* this is part of flush */
};

enum {
	CEPH_OSD_OP_FLAG_EXCL = 1,      /* EXCL object create */
	CEPH_OSD_OP_FLAG_FAILOK = 2,    /* continue despite failure */
};

#define EOLDSNAPC    ERESTART  /* ORDERSNAP flag set; writer has old snapc*/
#define EBLACKLISTED ESHUTDOWN /* blacklisted */

/* xattr comparison */
enum {
	CEPH_OSD_CMPXATTR_OP_NOP = 0,
	CEPH_OSD_CMPXATTR_OP_EQ  = 1,
	CEPH_OSD_CMPXATTR_OP_NE  = 2,
	CEPH_OSD_CMPXATTR_OP_GT  = 3,
	CEPH_OSD_CMPXATTR_OP_GTE = 4,
	CEPH_OSD_CMPXATTR_OP_LT  = 5,
	CEPH_OSD_CMPXATTR_OP_LTE = 6
};

enum {
	CEPH_OSD_CMPXATTR_MODE_STRING = 1,
	CEPH_OSD_CMPXATTR_MODE_U64    = 2
};

#define RADOS_NOTIFY_VER	1

/*
 * an individual object operation.  each may be accompanied by some data
 * payload
 */
struct ceph_osd_op {
	__le16 op;           /* CEPH_OSD_OP_* */
	__le32 flags;        /* CEPH_OSD_FLAG_* */
	union {
		struct {
			__le64 offset, length;
			__le64 truncate_size;
			__le32 truncate_seq;
		} __attribute__ ((packed)) extent;
		struct {
			__le32 name_len;
			__le32 value_len;
			__u8 cmp_op;       /* CEPH_OSD_CMPXATTR_OP_* */
			__u8 cmp_mode;     /* CEPH_OSD_CMPXATTR_MODE_* */
		} __attribute__ ((packed)) xattr;
		struct {
			__u8 class_len;
			__u8 method_len;
			__u8 argc;
			__le32 indata_len;
		} __attribute__ ((packed)) cls;
		struct {
			__le64 cookie, count;
		} __attribute__ ((packed)) pgls;
	        struct {
		        __le64 snapid;
	        } __attribute__ ((packed)) snap;
		struct {
			__le64 cookie;
			__le64 ver;
			__u8 flag;	/* 0 = unwatch, 1 = watch */
		} __attribute__ ((packed)) watch;
		struct {
			__le64 offset, length;
			__le64 src_offset;
		} __attribute__ ((packed)) clonerange;
	};
	__le32 payload_len;
} __attribute__ ((packed));


#endif
