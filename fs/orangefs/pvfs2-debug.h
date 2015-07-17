/*
 * (C) 2001 Clemson University and The University of Chicago
 *
 * See COPYING in top-level directory.
 */

/* This file just defines debugging masks to be used with the gossip
 * logging utility.  All debugging masks for PVFS2 are kept here to make
 * sure we don't have collisions.
 */

#ifndef __PVFS2_DEBUG_H
#define __PVFS2_DEBUG_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

#define GOSSIP_NO_DEBUG                (__u64)0
#define GOSSIP_BMI_DEBUG_TCP           ((__u64)1 << 0)
#define GOSSIP_BMI_DEBUG_CONTROL       ((__u64)1 << 1)
#define GOSSIP_BMI_DEBUG_OFFSETS       ((__u64)1 << 2)
#define GOSSIP_BMI_DEBUG_GM            ((__u64)1 << 3)
#define GOSSIP_JOB_DEBUG               ((__u64)1 << 4)
#define GOSSIP_SERVER_DEBUG            ((__u64)1 << 5)
#define GOSSIP_STO_DEBUG_CTRL          ((__u64)1 << 6)
#define GOSSIP_STO_DEBUG_DEFAULT       ((__u64)1 << 7)
#define GOSSIP_FLOW_DEBUG              ((__u64)1 << 8)
#define GOSSIP_BMI_DEBUG_GM_MEM        ((__u64)1 << 9)
#define GOSSIP_REQUEST_DEBUG           ((__u64)1 << 10)
#define GOSSIP_FLOW_PROTO_DEBUG        ((__u64)1 << 11)
#define GOSSIP_NCACHE_DEBUG            ((__u64)1 << 12)
#define GOSSIP_CLIENT_DEBUG            ((__u64)1 << 13)
#define GOSSIP_REQ_SCHED_DEBUG         ((__u64)1 << 14)
#define GOSSIP_ACACHE_DEBUG            ((__u64)1 << 15)
#define GOSSIP_TROVE_DEBUG             ((__u64)1 << 16)
#define GOSSIP_TROVE_OP_DEBUG          ((__u64)1 << 17)
#define GOSSIP_DIST_DEBUG              ((__u64)1 << 18)
#define GOSSIP_BMI_DEBUG_IB            ((__u64)1 << 19)
#define GOSSIP_DBPF_ATTRCACHE_DEBUG    ((__u64)1 << 20)
#define GOSSIP_MMAP_RCACHE_DEBUG       ((__u64)1 << 21)
#define GOSSIP_LOOKUP_DEBUG            ((__u64)1 << 22)
#define GOSSIP_REMOVE_DEBUG            ((__u64)1 << 23)
#define GOSSIP_GETATTR_DEBUG           ((__u64)1 << 24)
#define GOSSIP_READDIR_DEBUG           ((__u64)1 << 25)
#define GOSSIP_IO_DEBUG                ((__u64)1 << 26)
#define GOSSIP_DBPF_OPEN_CACHE_DEBUG   ((__u64)1 << 27)
#define GOSSIP_PERMISSIONS_DEBUG       ((__u64)1 << 28)
#define GOSSIP_CANCEL_DEBUG            ((__u64)1 << 29)
#define GOSSIP_MSGPAIR_DEBUG           ((__u64)1 << 30)
#define GOSSIP_CLIENTCORE_DEBUG        ((__u64)1 << 31)
#define GOSSIP_CLIENTCORE_TIMING_DEBUG ((__u64)1 << 32)
#define GOSSIP_SETATTR_DEBUG           ((__u64)1 << 33)
#define GOSSIP_MKDIR_DEBUG             ((__u64)1 << 34)
#define GOSSIP_VARSTRIP_DEBUG          ((__u64)1 << 35)
#define GOSSIP_GETEATTR_DEBUG          ((__u64)1 << 36)
#define GOSSIP_SETEATTR_DEBUG          ((__u64)1 << 37)
#define GOSSIP_ENDECODE_DEBUG          ((__u64)1 << 38)
#define GOSSIP_DELEATTR_DEBUG          ((__u64)1 << 39)
#define GOSSIP_ACCESS_DEBUG            ((__u64)1 << 40)
#define GOSSIP_ACCESS_DETAIL_DEBUG     ((__u64)1 << 41)
#define GOSSIP_LISTEATTR_DEBUG         ((__u64)1 << 42)
#define GOSSIP_PERFCOUNTER_DEBUG       ((__u64)1 << 43)
#define GOSSIP_STATE_MACHINE_DEBUG     ((__u64)1 << 44)
#define GOSSIP_DBPF_KEYVAL_DEBUG       ((__u64)1 << 45)
#define GOSSIP_LISTATTR_DEBUG          ((__u64)1 << 46)
#define GOSSIP_DBPF_COALESCE_DEBUG     ((__u64)1 << 47)
#define GOSSIP_ACCESS_HOSTNAMES        ((__u64)1 << 48)
#define GOSSIP_FSCK_DEBUG              ((__u64)1 << 49)
#define GOSSIP_BMI_DEBUG_MX            ((__u64)1 << 50)
#define GOSSIP_BSTREAM_DEBUG           ((__u64)1 << 51)
#define GOSSIP_BMI_DEBUG_PORTALS       ((__u64)1 << 52)
#define GOSSIP_USER_DEV_DEBUG          ((__u64)1 << 53)
#define GOSSIP_DIRECTIO_DEBUG          ((__u64)1 << 54)
#define GOSSIP_MGMT_DEBUG              ((__u64)1 << 55)
#define GOSSIP_MIRROR_DEBUG            ((__u64)1 << 56)
#define GOSSIP_WIN_CLIENT_DEBUG        ((__u64)1 << 57)
#define GOSSIP_SECURITY_DEBUG          ((__u64)1 << 58)
#define GOSSIP_USRINT_DEBUG            ((__u64)1 << 59)
#define GOSSIP_RCACHE_DEBUG            ((__u64)1 << 60)
#define GOSSIP_SECCACHE_DEBUG          ((__u64)1 << 61)

#define GOSSIP_BMI_DEBUG_ALL ((__u64) (GOSSIP_BMI_DEBUG_TCP +	\
					 GOSSIP_BMI_DEBUG_CONTROL +	\
					 GOSSIP_BMI_DEBUG_GM +		\
					 GOSSIP_BMI_DEBUG_OFFSETS +	\
					 GOSSIP_BMI_DEBUG_IB +		\
					 GOSSIP_BMI_DEBUG_MX +		\
					 GOSSIP_BMI_DEBUG_PORTALS))

const char *PVFS_debug_get_next_debug_keyword(int position);

#define GOSSIP_SUPER_DEBUG		((__u64)1 << 0)
#define GOSSIP_INODE_DEBUG		((__u64)1 << 1)
#define GOSSIP_FILE_DEBUG		((__u64)1 << 2)
#define GOSSIP_DIR_DEBUG		((__u64)1 << 3)
#define GOSSIP_UTILS_DEBUG		((__u64)1 << 4)
#define GOSSIP_WAIT_DEBUG		((__u64)1 << 5)
#define GOSSIP_ACL_DEBUG		((__u64)1 << 6)
#define GOSSIP_DCACHE_DEBUG		((__u64)1 << 7)
#define GOSSIP_DEV_DEBUG		((__u64)1 << 8)
#define GOSSIP_NAME_DEBUG		((__u64)1 << 9)
#define GOSSIP_BUFMAP_DEBUG		((__u64)1 << 10)
#define GOSSIP_CACHE_DEBUG		((__u64)1 << 11)
#define GOSSIP_DEBUGFS_DEBUG		((__u64)1 << 12)
#define GOSSIP_XATTR_DEBUG		((__u64)1 << 13)
#define GOSSIP_INIT_DEBUG		((__u64)1 << 14)
#define GOSSIP_SYSFS_DEBUG		((__u64)1 << 15)

#define GOSSIP_MAX_NR                 16
#define GOSSIP_MAX_DEBUG              (((__u64)1 << GOSSIP_MAX_NR) - 1)

/*function prototypes*/
__u64 PVFS_kmod_eventlog_to_mask(const char *event_logging);
__u64 PVFS_debug_eventlog_to_mask(const char *event_logging);
char *PVFS_debug_mask_to_eventlog(__u64 mask);
char *PVFS_kmod_mask_to_eventlog(__u64 mask);

/* a private internal type */
struct __keyword_mask_s {
	const char *keyword;
	__u64 mask_val;
};

#define __DEBUG_ALL ((__u64) -1)

/* map all config keywords to pvfs2 debug masks here */
static struct __keyword_mask_s s_keyword_mask_map[] = {
	/* Log trove debugging info.  Same as 'trove'. */
	{"storage", GOSSIP_TROVE_DEBUG},
	/* Log trove debugging info.  Same as 'storage'. */
	{"trove", GOSSIP_TROVE_DEBUG},
	/* Log trove operations. */
	{"trove_op", GOSSIP_TROVE_OP_DEBUG},
	/* Log network debug info. */
	{"network", GOSSIP_BMI_DEBUG_ALL},
	/* Log server info, including new operations. */
	{"server", GOSSIP_SERVER_DEBUG},
	/* Log client sysint info.  This is only useful for the client. */
	{"client", GOSSIP_CLIENT_DEBUG},
	/* Debug the varstrip distribution */
	{"varstrip", GOSSIP_VARSTRIP_DEBUG},
	/* Log job info */
	{"job", GOSSIP_JOB_DEBUG},
	/* Debug PINT_process_request calls.  EXTREMELY verbose! */
	{"request", GOSSIP_REQUEST_DEBUG},
	/* Log request scheduler events */
	{"reqsched", GOSSIP_REQ_SCHED_DEBUG},
	/* Log the flow protocol events, including flowproto_multiqueue */
	{"flowproto", GOSSIP_FLOW_PROTO_DEBUG},
	/* Log flow calls */
	{"flow", GOSSIP_FLOW_DEBUG},
	/* Debug the client name cache.  Only useful on the client. */
	{"ncache", GOSSIP_NCACHE_DEBUG},
	/* Debug read-ahead cache events.  Only useful on the client. */
	{"mmaprcache", GOSSIP_MMAP_RCACHE_DEBUG},
	/* Debug the attribute cache.  Only useful on the client. */
	{"acache", GOSSIP_ACACHE_DEBUG},
	/* Log/Debug distribution calls */
	{"distribution", GOSSIP_DIST_DEBUG},
	/* Debug the server-side dbpf attribute cache */
	{"dbpfattrcache", GOSSIP_DBPF_ATTRCACHE_DEBUG},
	/* Debug the client lookup state machine. */
	{"lookup", GOSSIP_LOOKUP_DEBUG},
	/* Debug the client remove state macine. */
	{"remove", GOSSIP_REMOVE_DEBUG},
	/* Debug the server getattr state machine. */
	{"getattr", GOSSIP_GETATTR_DEBUG},
	/* Debug the server setattr state machine. */
	{"setattr", GOSSIP_SETATTR_DEBUG},
	/* vectored getattr server state machine */
	{"listattr", GOSSIP_LISTATTR_DEBUG},
	/* Debug the client and server get ext attributes SM. */
	{"geteattr", GOSSIP_GETEATTR_DEBUG},
	/* Debug the client and server set ext attributes SM. */
	{"seteattr", GOSSIP_SETEATTR_DEBUG},
	/* Debug the readdir operation (client and server) */
	{"readdir", GOSSIP_READDIR_DEBUG},
	/* Debug the mkdir operation (server only) */
	{"mkdir", GOSSIP_MKDIR_DEBUG},
	/* Debug the io operation (reads and writes)
	 * for both the client and server */
	{"io", GOSSIP_IO_DEBUG},
	/* Debug the server's open file descriptor cache */
	{"open_cache", GOSSIP_DBPF_OPEN_CACHE_DEBUG},
	/* Debug permissions checking on the server */
	{"permissions", GOSSIP_PERMISSIONS_DEBUG},
	/* Debug the cancel operation */
	{"cancel", GOSSIP_CANCEL_DEBUG},
	/* Debug the msgpair state machine */
	{"msgpair", GOSSIP_MSGPAIR_DEBUG},
	/* Debug the client core app */
	{"clientcore", GOSSIP_CLIENTCORE_DEBUG},
	/* Debug the client timing state machines (job timeout, etc.) */
	{"clientcore_timing", GOSSIP_CLIENTCORE_TIMING_DEBUG},
	/* network encoding */
	{"endecode", GOSSIP_ENDECODE_DEBUG},
	/* Show server file (metadata) accesses (both modify and read-only). */
	{"access", GOSSIP_ACCESS_DEBUG},
	/* Show more detailed server file accesses */
	{"access_detail", GOSSIP_ACCESS_DETAIL_DEBUG},
	/* Debug the listeattr operation */
	{"listeattr", GOSSIP_LISTEATTR_DEBUG},
	/* Debug the state machine management code */
	{"sm", GOSSIP_STATE_MACHINE_DEBUG},
	/* Debug the metadata dbpf keyval functions */
	{"keyval", GOSSIP_DBPF_KEYVAL_DEBUG},
	/* Debug the metadata sync coalescing code */
	{"coalesce", GOSSIP_DBPF_COALESCE_DEBUG},
	/* Display the hostnames instead of IP addrs in debug output */
	{"access_hostnames", GOSSIP_ACCESS_HOSTNAMES},
	/* Show the client device events */
	{"user_dev", GOSSIP_USER_DEV_DEBUG},
	/* Debug the fsck tool */
	{"fsck", GOSSIP_FSCK_DEBUG},
	/* Debug the bstream code */
	{"bstream", GOSSIP_BSTREAM_DEBUG},
	/* Debug trove in direct io mode */
	{"directio", GOSSIP_DIRECTIO_DEBUG},
	/* Debug direct io thread management */
	{"mgmt", GOSSIP_MGMT_DEBUG},
	/* Debug mirroring process */
	{"mirror", GOSSIP_MIRROR_DEBUG},
	/* Windows client */
	{"win_client", GOSSIP_WIN_CLIENT_DEBUG},
	/* Debug robust security code */
	{"security", GOSSIP_SECURITY_DEBUG},
	/* Capability Cache */
	{"seccache", GOSSIP_SECCACHE_DEBUG},
	/* Client User Interface */
	{"usrint", GOSSIP_USRINT_DEBUG},
	/* rcache */
	{"rcache", GOSSIP_RCACHE_DEBUG},
	/* Everything except the periodic events.  Useful for debugging */
	{"verbose",
	 (__DEBUG_ALL &
	  ~(GOSSIP_PERFCOUNTER_DEBUG | GOSSIP_STATE_MACHINE_DEBUG |
	    GOSSIP_ENDECODE_DEBUG | GOSSIP_USER_DEV_DEBUG))
	 },
	/* No debug output */
	{"none", GOSSIP_NO_DEBUG},
	/* Everything */
	{"all", __DEBUG_ALL}
};

#undef __DEBUG_ALL

/*
 * Map all kmod keywords to kmod debug masks here. Keep this
 * structure "packed":
 *
 *   "all" is always last...
 *
 *   keyword     mask_val     index
 *     foo          1           0
 *     bar          2           1
 *     baz          4           2
 *     qux          8           3
 *      .           .           .
 */
static struct __keyword_mask_s s_kmod_keyword_mask_map[] = {
	{"super", GOSSIP_SUPER_DEBUG},
	{"inode", GOSSIP_INODE_DEBUG},
	{"file", GOSSIP_FILE_DEBUG},
	{"dir", GOSSIP_DIR_DEBUG},
	{"utils", GOSSIP_UTILS_DEBUG},
	{"wait", GOSSIP_WAIT_DEBUG},
	{"acl", GOSSIP_ACL_DEBUG},
	{"dcache", GOSSIP_DCACHE_DEBUG},
	{"dev", GOSSIP_DEV_DEBUG},
	{"name", GOSSIP_NAME_DEBUG},
	{"bufmap", GOSSIP_BUFMAP_DEBUG},
	{"cache", GOSSIP_CACHE_DEBUG},
	{"debugfs", GOSSIP_DEBUGFS_DEBUG},
	{"xattr", GOSSIP_XATTR_DEBUG},
	{"init", GOSSIP_INIT_DEBUG},
	{"sysfs", GOSSIP_SYSFS_DEBUG},
	{"none", GOSSIP_NO_DEBUG},
	{"all", GOSSIP_MAX_DEBUG}
};

static const int num_kmod_keyword_mask_map = (int)
	(sizeof(s_kmod_keyword_mask_map) / sizeof(struct __keyword_mask_s));

static const int num_keyword_mask_map = (int)
	(sizeof(s_keyword_mask_map) / sizeof(struct __keyword_mask_s));

#endif /* __PVFS2_DEBUG_H */
