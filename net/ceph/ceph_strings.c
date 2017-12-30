// SPDX-License-Identifier: GPL-2.0
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
EXPORT_SYMBOL(ceph_entity_type_name);

const char *ceph_osd_op_name(int op)
{
	switch (op) {
#define GENERATE_CASE(op, opcode, str)	case CEPH_OSD_OP_##op: return (str);
__CEPH_FORALL_OSD_OPS(GENERATE_CASE)
#undef GENERATE_CASE
	default:
		return "???";
	}
}

const char *ceph_osd_watch_op_name(int o)
{
	switch (o) {
	case CEPH_OSD_WATCH_OP_UNWATCH:
		return "unwatch";
	case CEPH_OSD_WATCH_OP_WATCH:
		return "watch";
	case CEPH_OSD_WATCH_OP_RECONNECT:
		return "reconnect";
	case CEPH_OSD_WATCH_OP_PING:
		return "ping";
	default:
		return "???";
	}
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
