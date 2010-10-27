/*
 * Ceph string constants
 */
#include "types.h"

const char *ceph_entity_type_name(int type)
{
	switch (type) {
	case CEPH_ENTITY_TYPE_MDS: return "mds";
	case CEPH_ENTITY_TYPE_OSD: return "osd";
	case CEPH_ENTITY_TYPE_MON: return "mon";
	case CEPH_ENTITY_TYPE_CLIENT: return "client";
	case CEPH_ENTITY_TYPE_AUTH: return "auth";
	default: return "unknown";
	}
}

const char *ceph_osd_op_name(int op)
{
	switch (op) {
	case CEPH_OSD_OP_READ: return "read";
	case CEPH_OSD_OP_STAT: return "stat";

	case CEPH_OSD_OP_MASKTRUNC: return "masktrunc";

	case CEPH_OSD_OP_WRITE: return "write";
	case CEPH_OSD_OP_DELETE: return "delete";
	case CEPH_OSD_OP_TRUNCATE: return "truncate";
	case CEPH_OSD_OP_ZERO: return "zero";
	case CEPH_OSD_OP_WRITEFULL: return "writefull";
	case CEPH_OSD_OP_ROLLBACK: return "rollback";

	case CEPH_OSD_OP_APPEND: return "append";
	case CEPH_OSD_OP_STARTSYNC: return "startsync";
	case CEPH_OSD_OP_SETTRUNC: return "settrunc";
	case CEPH_OSD_OP_TRIMTRUNC: return "trimtrunc";

	case CEPH_OSD_OP_TMAPUP: return "tmapup";
	case CEPH_OSD_OP_TMAPGET: return "tmapget";
	case CEPH_OSD_OP_TMAPPUT: return "tmapput";

	case CEPH_OSD_OP_GETXATTR: return "getxattr";
	case CEPH_OSD_OP_GETXATTRS: return "getxattrs";
	case CEPH_OSD_OP_SETXATTR: return "setxattr";
	case CEPH_OSD_OP_SETXATTRS: return "setxattrs";
	case CEPH_OSD_OP_RESETXATTRS: return "resetxattrs";
	case CEPH_OSD_OP_RMXATTR: return "rmxattr";
	case CEPH_OSD_OP_CMPXATTR: return "cmpxattr";

	case CEPH_OSD_OP_PULL: return "pull";
	case CEPH_OSD_OP_PUSH: return "push";
	case CEPH_OSD_OP_BALANCEREADS: return "balance-reads";
	case CEPH_OSD_OP_UNBALANCEREADS: return "unbalance-reads";
	case CEPH_OSD_OP_SCRUB: return "scrub";

	case CEPH_OSD_OP_WRLOCK: return "wrlock";
	case CEPH_OSD_OP_WRUNLOCK: return "wrunlock";
	case CEPH_OSD_OP_RDLOCK: return "rdlock";
	case CEPH_OSD_OP_RDUNLOCK: return "rdunlock";
	case CEPH_OSD_OP_UPLOCK: return "uplock";
	case CEPH_OSD_OP_DNLOCK: return "dnlock";

	case CEPH_OSD_OP_CALL: return "call";

	case CEPH_OSD_OP_PGLS: return "pgls";
	}
	return "???";
}

const char *ceph_mds_state_name(int s)
{
	switch (s) {
		/* down and out */
	case CEPH_MDS_STATE_DNE:        return "down:dne";
	case CEPH_MDS_STATE_STOPPED:    return "down:stopped";
		/* up and out */
	case CEPH_MDS_STATE_BOOT:       return "up:boot";
	case CEPH_MDS_STATE_STANDBY:    return "up:standby";
	case CEPH_MDS_STATE_STANDBY_REPLAY:    return "up:standby-replay";
	case CEPH_MDS_STATE_CREATING:   return "up:creating";
	case CEPH_MDS_STATE_STARTING:   return "up:starting";
		/* up and in */
	case CEPH_MDS_STATE_REPLAY:     return "up:replay";
	case CEPH_MDS_STATE_RESOLVE:    return "up:resolve";
	case CEPH_MDS_STATE_RECONNECT:  return "up:reconnect";
	case CEPH_MDS_STATE_REJOIN:     return "up:rejoin";
	case CEPH_MDS_STATE_CLIENTREPLAY: return "up:clientreplay";
	case CEPH_MDS_STATE_ACTIVE:     return "up:active";
	case CEPH_MDS_STATE_STOPPING:   return "up:stopping";
	}
	return "???";
}

const char *ceph_session_op_name(int op)
{
	switch (op) {
	case CEPH_SESSION_REQUEST_OPEN: return "request_open";
	case CEPH_SESSION_OPEN: return "open";
	case CEPH_SESSION_REQUEST_CLOSE: return "request_close";
	case CEPH_SESSION_CLOSE: return "close";
	case CEPH_SESSION_REQUEST_RENEWCAPS: return "request_renewcaps";
	case CEPH_SESSION_RENEWCAPS: return "renewcaps";
	case CEPH_SESSION_STALE: return "stale";
	case CEPH_SESSION_RECALL_STATE: return "recall_state";
	}
	return "???";
}

const char *ceph_mds_op_name(int op)
{
	switch (op) {
	case CEPH_MDS_OP_LOOKUP:  return "lookup";
	case CEPH_MDS_OP_LOOKUPHASH:  return "lookuphash";
	case CEPH_MDS_OP_LOOKUPPARENT:  return "lookupparent";
	case CEPH_MDS_OP_GETATTR:  return "getattr";
	case CEPH_MDS_OP_SETXATTR: return "setxattr";
	case CEPH_MDS_OP_SETATTR: return "setattr";
	case CEPH_MDS_OP_RMXATTR: return "rmxattr";
	case CEPH_MDS_OP_READDIR: return "readdir";
	case CEPH_MDS_OP_MKNOD: return "mknod";
	case CEPH_MDS_OP_LINK: return "link";
	case CEPH_MDS_OP_UNLINK: return "unlink";
	case CEPH_MDS_OP_RENAME: return "rename";
	case CEPH_MDS_OP_MKDIR: return "mkdir";
	case CEPH_MDS_OP_RMDIR: return "rmdir";
	case CEPH_MDS_OP_SYMLINK: return "symlink";
	case CEPH_MDS_OP_CREATE: return "create";
	case CEPH_MDS_OP_OPEN: return "open";
	case CEPH_MDS_OP_LOOKUPSNAP: return "lookupsnap";
	case CEPH_MDS_OP_LSSNAP: return "lssnap";
	case CEPH_MDS_OP_MKSNAP: return "mksnap";
	case CEPH_MDS_OP_RMSNAP: return "rmsnap";
	case CEPH_MDS_OP_SETFILELOCK: return "setfilelock";
	case CEPH_MDS_OP_GETFILELOCK: return "getfilelock";
	}
	return "???";
}

const char *ceph_cap_op_name(int op)
{
	switch (op) {
	case CEPH_CAP_OP_GRANT: return "grant";
	case CEPH_CAP_OP_REVOKE: return "revoke";
	case CEPH_CAP_OP_TRUNC: return "trunc";
	case CEPH_CAP_OP_EXPORT: return "export";
	case CEPH_CAP_OP_IMPORT: return "import";
	case CEPH_CAP_OP_UPDATE: return "update";
	case CEPH_CAP_OP_DROP: return "drop";
	case CEPH_CAP_OP_FLUSH: return "flush";
	case CEPH_CAP_OP_FLUSH_ACK: return "flush_ack";
	case CEPH_CAP_OP_FLUSHSNAP: return "flushsnap";
	case CEPH_CAP_OP_FLUSHSNAP_ACK: return "flushsnap_ack";
	case CEPH_CAP_OP_RELEASE: return "release";
	case CEPH_CAP_OP_RENEW: return "renew";
	}
	return "???";
}

const char *ceph_lease_op_name(int o)
{
	switch (o) {
	case CEPH_MDS_LEASE_REVOKE: return "revoke";
	case CEPH_MDS_LEASE_RELEASE: return "release";
	case CEPH_MDS_LEASE_RENEW: return "renew";
	case CEPH_MDS_LEASE_REVOKE_ACK: return "revoke_ack";
	}
	return "???";
}

const char *ceph_snap_op_name(int o)
{
	switch (o) {
	case CEPH_SNAP_OP_UPDATE: return "update";
	case CEPH_SNAP_OP_CREATE: return "create";
	case CEPH_SNAP_OP_DESTROY: return "destroy";
	case CEPH_SNAP_OP_SPLIT: return "split";
	}
	return "???";
}

const char *ceph_pool_op_name(int op)
{
	switch (op) {
	case POOL_OP_CREATE: return "create";
	case POOL_OP_DELETE: return "delete";
	case POOL_OP_AUID_CHANGE: return "auid change";
	case POOL_OP_CREATE_SNAP: return "create snap";
	case POOL_OP_DELETE_SNAP: return "delete snap";
	case POOL_OP_CREATE_UNMANAGED_SNAP: return "create unmanaged snap";
	case POOL_OP_DELETE_UNMANAGED_SNAP: return "delete unmanaged snap";
	}
	return "???";
}
