/*
 * Ceph string constants
 */
#include <linux/module.h>
#include <linux/ceph/types.h>

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
	case CEPH_OSD_OP_MAPEXT: return "mapext";
	case CEPH_OSD_OP_SPARSE_READ: return "sparse-read";
	case CEPH_OSD_OP_NOTIFY: return "notify";
	case CEPH_OSD_OP_NOTIFY_ACK: return "notify-ack";
	case CEPH_OSD_OP_ASSERT_VER: return "assert-version";

	case CEPH_OSD_OP_MASKTRUNC: return "masktrunc";

	case CEPH_OSD_OP_CREATE: return "create";
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
	case CEPH_OSD_OP_WATCH: return "watch";

	case CEPH_OSD_OP_CLONERANGE: return "clonerange";
	case CEPH_OSD_OP_ASSERT_SRC_VERSION: return "assert-src-version";
	case CEPH_OSD_OP_SRC_CMPXATTR: return "src-cmpxattr";

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
	case CEPH_OSD_OP_SCRUB_RESERVE: return "scrub-reserve";
	case CEPH_OSD_OP_SCRUB_UNRESERVE: return "scrub-unreserve";
	case CEPH_OSD_OP_SCRUB_STOP: return "scrub-stop";
	case CEPH_OSD_OP_SCRUB_MAP: return "scrub-map";

	case CEPH_OSD_OP_WRLOCK: return "wrlock";
	case CEPH_OSD_OP_WRUNLOCK: return "wrunlock";
	case CEPH_OSD_OP_RDLOCK: return "rdlock";
	case CEPH_OSD_OP_RDUNLOCK: return "rdunlock";
	case CEPH_OSD_OP_UPLOCK: return "uplock";
	case CEPH_OSD_OP_DNLOCK: return "dnlock";

	case CEPH_OSD_OP_CALL: return "call";

	case CEPH_OSD_OP_PGLS: return "pgls";
	case CEPH_OSD_OP_PGLS_FILTER: return "pgls-filter";
	case CEPH_OSD_OP_OMAPGETKEYS: return "omap-get-keys";
	case CEPH_OSD_OP_OMAPGETVALS: return "omap-get-vals";
	case CEPH_OSD_OP_OMAPGETHEADER: return "omap-get-header";
	case CEPH_OSD_OP_OMAPGETVALSBYKEYS: return "omap-get-vals-by-keys";
	case CEPH_OSD_OP_OMAPSETVALS: return "omap-set-vals";
	case CEPH_OSD_OP_OMAPSETHEADER: return "omap-set-header";
	case CEPH_OSD_OP_OMAPCLEAR: return "omap-clear";
	case CEPH_OSD_OP_OMAPRMKEYS: return "omap-rm-keys";
	}
	return "???";
}

const char *ceph_osd_state_name(int s)
{
	switch (s) {
	case CEPH_OSD_EXISTS:
		return "exists";
	case CEPH_OSD_UP:
		return "up";
	case CEPH_OSD_AUTOOUT:
		return "autoout";
	case CEPH_OSD_NEW:
		return "new";
	default:
		return "???";
	}
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
