/*-
 * Copyright (c) 2014 Luiz Otavio O Souza <loos@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/sysctl.h>

#include <bsnmp/snmpmod.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "lm75_oid.h"
#include "lm75_tree.h"

#ifndef	LM75BUF
#define	LM75BUF		64
#endif
#define	TZ_ZEROC	2732
#define	UPDATE_INTERVAL	500	/* update interval in ticks */

static struct lmodule *module;

static const struct asn_oid oid_lm75 = OIDX_begemotLm75;

/* the Object Resource registration index */
static u_int lm75_index = 0;

/* Number of available sensors in the system. */
static int lm75_sensors;

/*
 * Structure that describes single sensor.
 */
struct lm75_snmp_sensor {
	TAILQ_ENTRY(lm75_snmp_sensor) link;
	int32_t		index;
	int32_t		sysctlidx;
	int32_t		temp;
	char		desc[LM75BUF];
	char		location[LM75BUF];
	char		parent[LM75BUF];
	char		pnpinfo[LM75BUF];
};

static TAILQ_HEAD(, lm75_snmp_sensor) sensors =
    TAILQ_HEAD_INITIALIZER(sensors);

/* Ticks of the last sensors reading. */
static uint64_t last_sensors_update;

static void free_sensors(void);
static int lm75_fini(void);
static int lm75_init(struct lmodule *mod, int argc, char *argv[]);
static void lm75_start(void);
static int update_sensors(void);

const struct snmp_module config = {
    .comment   =
	"This module implements the BEGEMOT MIB for reading LM75 sensors data.",
    .init      = lm75_init,
    .start     = lm75_start,
    .fini      = lm75_fini,
    .tree      = lm75_ctree,
    .tree_size = lm75_CTREE_SIZE,
};

static int
lm75_init(struct lmodule *mod, int argc __unused, char *argv[] __unused)
{

	module = mod;

	lm75_sensors = 0;
	openlog("snmp_lm75", LOG_NDELAY | LOG_PID, LOG_DAEMON);

	return(0);
}

static void
lm75_start(void)
{

	lm75_index = or_register(&oid_lm75,
	    "The MIB module for reading lm75 sensors data.", module);
}

static int
lm75_fini(void)
{

	or_unregister(lm75_index);
	free_sensors();
	closelog();

	return (0);
}

static void
free_sensors(void)
{
	struct lm75_snmp_sensor *sensor;

	while ((sensor = TAILQ_FIRST(&sensors)) != NULL) {
		TAILQ_REMOVE(&sensors, sensor, link);
		free(sensor);
	}
}

static int
sysctlname(int *oid, int nlen, char *name, size_t len)
{
	int mib[12];

	if (nlen > (int)(sizeof(mib) / sizeof(int) - 2))
		return (-1);

	mib[0] = 0;
	mib[1] = 1;
	memcpy(mib + 2, oid, nlen * sizeof(int));

	if (sysctl(mib, nlen + 2, name, &len, 0, 0) == -1)
		return (-1);

	return (0);
}

static int
sysctlgetnext(int *oid, int nlen, int *next, size_t *nextlen)
{
	int mib[12];

	if (nlen > (int)(sizeof(mib) / sizeof(int) - 2))
		return (-1);

	mib[0] = 0;
	mib[1] = 2;
	memcpy(mib + 2, oid, nlen * sizeof(int));

	if (sysctl(mib, nlen + 2, next, nextlen, 0, 0) == -1)
		return (-1);

	return (0);
}

static int
update_sensor_sysctl(void *obuf, size_t *obuflen, int idx, const char *name)
{
	char buf[LM75BUF];
	int mib[5];
	size_t len;

	/* Fill out the mib information. */
	snprintf(buf, sizeof(buf) - 1, "dev.lm75.%d.%s", idx, name);
	len = sizeof(mib) / sizeof(int);
	if (sysctlnametomib(buf, mib, &len) == -1)
		return (-1);

	if (len != 4)
		return (-1);

	/* Read the sysctl data. */
	if (sysctl(mib, len, obuf, obuflen, NULL, 0) == -1)
		return (-1);

	return (0);
}

static void
update_sensor(struct lm75_snmp_sensor *sensor, int idx)
{
	size_t len;

	len = sizeof(sensor->desc);
	update_sensor_sysctl(sensor->desc, &len, idx, "%desc");

	len = sizeof(sensor->location);
	update_sensor_sysctl(sensor->location, &len, idx, "%location");

	len = sizeof(sensor->pnpinfo);
	update_sensor_sysctl(sensor->pnpinfo, &len, idx, "%pnpinfo");

	len = sizeof(sensor->parent);
	update_sensor_sysctl(sensor->parent, &len, idx, "%parent");
}

static int
add_sensor(char *buf)
{
	int idx, temp;
	size_t len;
	struct lm75_snmp_sensor *sensor;

	if (sscanf(buf, "dev.lm75.%d.temperature", &idx) != 1)
		return (-1);

	/* Read the sensor temperature. */
	len = sizeof(temp);
	if (update_sensor_sysctl(&temp, &len, idx, "temperature") != 0)
		return (-1);

	/* Add the sensor data to the table. */
	sensor = calloc(1, sizeof(*sensor));
	if (sensor == NULL) {
		syslog(LOG_ERR, "Unable to allocate %zu bytes for resource",
		    sizeof(*sensor));
		return (-1);
	}
	sensor->index = ++lm75_sensors;
	sensor->sysctlidx = idx;
	sensor->temp = (temp - TZ_ZEROC) / 10;
	TAILQ_INSERT_TAIL(&sensors, sensor, link);

	update_sensor(sensor, idx);

	return (0);
}

static int
update_sensors(void)
{
	char buf[LM75BUF];
	int i, root[5], *next, *oid;
	size_t len, nextlen, rootlen;
	static uint64_t now;

	now = get_ticks();
	if (now - last_sensors_update < UPDATE_INTERVAL)
		return (0);

	last_sensors_update = now;

	/* Reset the sensor data. */
	free_sensors();
	lm75_sensors = 0;

	/* Start from the lm75 default root node. */
	rootlen = 2;
	if (sysctlnametomib("dev.lm75", root, &rootlen) == -1)
		return (0);

	oid = (int *)malloc(sizeof(int) * rootlen);
	if (oid == NULL) {
		perror("malloc");
		return (-1);
	}
	memcpy(oid, root, rootlen * sizeof(int));
	len = rootlen;

	/* Traverse the sysctl(3) interface and find the active sensors. */
	for (;;) {

		/* Find the size of the next mib. */
		nextlen = 0;
		if (sysctlgetnext(oid, len, NULL, &nextlen) == -1) {
			free(oid);
			return (0);
		}
		/* Alocate and read the next mib. */
		next = (int *)malloc(nextlen);
		if (next == NULL) {
			syslog(LOG_ERR,
			    "Unable to allocate %zu bytes for resource",
			    nextlen);
			free(oid);
			return (-1);
		}
		if (sysctlgetnext(oid, len, next, &nextlen) == -1) {
			free(oid);
			free(next);
			return (0);
		}
		free(oid);
		/* Check if we care about the next mib. */
		for (i = 0; i < (int)rootlen; i++)
			if (next[i] != root[i]) {
				free(next);
				return (0);
			}
		oid = (int *)malloc(nextlen);
		if (oid == NULL) {
			syslog(LOG_ERR,
			    "Unable to allocate %zu bytes for resource",
			    nextlen);
			free(next);
			return (-1);
		}
		memcpy(oid, next, nextlen);
		free(next);
		len = nextlen / sizeof(int);

		/* Find the mib name. */
		if (sysctlname(oid, len, buf, sizeof(buf)) != 0)
			continue;

		if (strstr(buf, "temperature"))
			if (add_sensor(buf) != 0) {
				free(oid);
				return (-1);
			}
	}

	return (0);
}

int
op_lm75Sensors(struct snmp_context *context __unused, struct snmp_value *value,
    u_int sub, u_int iidx __unused, enum snmp_op op)
{
	asn_subid_t which;

	if (update_sensors() == -1)
		return (SNMP_ERR_RES_UNAVAIL);

	which = value->var.subs[sub - 1];

	switch (op) {
	case SNMP_OP_GET:
		switch (which) {
		case LEAF_lm75Sensors:
			value->v.integer = lm75_sensors;
			break;
		default:
			return (SNMP_ERR_RES_UNAVAIL);
		}
		break;
	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);
	case SNMP_OP_GETNEXT:
	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	default:
		return (SNMP_ERR_RES_UNAVAIL);
	}

	return (SNMP_ERR_NOERROR);
}

int
op_lm75SensorTable(struct snmp_context *context __unused,
    struct snmp_value *value, u_int sub, u_int iidx __unused, enum snmp_op op)
{
	struct lm75_snmp_sensor *sensor;
	asn_subid_t which;
	int ret;

	if (update_sensors() == -1)
		return (SNMP_ERR_RES_UNAVAIL);

	which = value->var.subs[sub - 1];

	switch (op) {
	case SNMP_OP_GETNEXT:
		sensor = NEXT_OBJECT_INT(&sensors, &value->var, sub);
		if (sensor == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		value->var.len = sub + 1;
		value->var.subs[sub] = sensor->index;
		break;
	case SNMP_OP_GET:
		if (value->var.len - sub != 1)
			return (SNMP_ERR_NOSUCHNAME);
		sensor = FIND_OBJECT_INT(&sensors, &value->var, sub);
		if (sensor == NULL)
			return (SNMP_ERR_NOSUCHNAME);
		break;
	case SNMP_OP_SET:
		return (SNMP_ERR_NOT_WRITEABLE);
	case SNMP_OP_ROLLBACK:
	case SNMP_OP_COMMIT:
		return (SNMP_ERR_NOERROR);
	default:
		return (SNMP_ERR_RES_UNAVAIL);
	}

	ret = SNMP_ERR_NOERROR;

	switch (which) {
	case LEAF_lm75SensorIndex:
		value->v.integer = sensor->index;
		break;
	case LEAF_lm75SensorSysctlIndex:
		value->v.integer = sensor->sysctlidx;
		break;
	case LEAF_lm75SensorDesc:
		ret = string_get(value, sensor->desc, -1);
		break;
	case LEAF_lm75SensorLocation:
		ret = string_get(value, sensor->location, -1);
		break;
	case LEAF_lm75SensorPnpInfo:
		ret = string_get(value, sensor->pnpinfo, -1);
		break;
	case LEAF_lm75SensorParent:
		ret = string_get(value, sensor->parent, -1);
		break;
	case LEAF_lm75SensorTemperature:
		value->v.integer = sensor->temp;
		break;
	default:
		ret = SNMP_ERR_RES_UNAVAIL;
		break;
	}

	return (ret);
}
