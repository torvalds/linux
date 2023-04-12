// SPDX-License-Identifier: GPL-2.0-or-later
/* Copyright 2022-2023 NXP
 */

#define CREATE_TRACE_POINTS
#include "trace.h"

void dsa_db_print(const struct dsa_db *db, char buf[DSA_DB_BUFSIZ])
{
	switch (db->type) {
	case DSA_DB_PORT:
		sprintf(buf, "port %s", db->dp->name);
		break;
	case DSA_DB_LAG:
		sprintf(buf, "lag %s id %d", db->lag.dev->name, db->lag.id);
		break;
	case DSA_DB_BRIDGE:
		sprintf(buf, "bridge %s num %d", db->bridge.dev->name,
			db->bridge.num);
		break;
	default:
		sprintf(buf, "unknown");
		break;
	}
}

const char *dsa_port_kind(const struct dsa_port *dp)
{
	switch (dp->type) {
	case DSA_PORT_TYPE_USER:
		return "user";
	case DSA_PORT_TYPE_CPU:
		return "cpu";
	case DSA_PORT_TYPE_DSA:
		return "dsa";
	default:
		return "unused";
	}
}
