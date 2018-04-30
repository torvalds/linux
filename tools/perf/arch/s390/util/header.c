/*
 * Implementation of get_cpuid().
 *
 * Copyright IBM Corp. 2014, 2018
 * Author(s): Alexander Yarygin <yarygin@linux.vnet.ibm.com>
 *	      Thomas Richter <tmricht@linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 */

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "../../util/header.h"
#include "../../util/util.h"

#define SYSINFO_MANU	"Manufacturer:"
#define SYSINFO_TYPE	"Type:"
#define SYSINFO_MODEL	"Model:"
#define SRVLVL_CPUMF	"CPU-MF:"
#define SRVLVL_VERSION	"version="
#define SRVLVL_AUTHORIZATION	"authorization="
#define SYSINFO		"/proc/sysinfo"
#define SRVLVL		"/proc/service_levels"

int get_cpuid(char *buffer, size_t sz)
{
	char *cp, *line = NULL, *line2;
	char type[8], model[33], version[8], manufacturer[32], authorization[8];
	int tpsize = 0, mdsize = 0, vssize = 0, mfsize = 0, atsize = 0;
	int read;
	unsigned long line_sz;
	size_t nbytes;
	FILE *sysinfo;

	/*
	 * Scan /proc/sysinfo line by line and read out values for
	 * Manufacturer:, Type: and Model:, for example:
	 * Manufacturer:    IBM
	 * Type:            2964
	 * Model:           702              N96
	 * The first word is the Model Capacity and the second word is
	 * Model (can be omitted). Both words have a maximum size of 16
	 * bytes.
	 */
	memset(manufacturer, 0, sizeof(manufacturer));
	memset(type, 0, sizeof(type));
	memset(model, 0, sizeof(model));
	memset(version, 0, sizeof(version));
	memset(authorization, 0, sizeof(authorization));

	sysinfo = fopen(SYSINFO, "r");
	if (sysinfo == NULL)
		return -1;

	while ((read = getline(&line, &line_sz, sysinfo)) != -1) {
		if (!strncmp(line, SYSINFO_MANU, strlen(SYSINFO_MANU))) {
			line2 = line + strlen(SYSINFO_MANU);

			while ((cp = strtok_r(line2, "\n ", &line2))) {
				mfsize += scnprintf(manufacturer + mfsize,
						    sizeof(manufacturer) - mfsize, "%s", cp);
			}
		}

		if (!strncmp(line, SYSINFO_TYPE, strlen(SYSINFO_TYPE))) {
			line2 = line + strlen(SYSINFO_TYPE);

			while ((cp = strtok_r(line2, "\n ", &line2))) {
				tpsize += scnprintf(type + tpsize,
						    sizeof(type) - tpsize, "%s", cp);
			}
		}

		if (!strncmp(line, SYSINFO_MODEL, strlen(SYSINFO_MODEL))) {
			line2 = line + strlen(SYSINFO_MODEL);

			while ((cp = strtok_r(line2, "\n ", &line2))) {
				mdsize += scnprintf(model + mdsize, sizeof(model) - mdsize,
						    "%s%s", model[0] ? "," : "", cp);
			}
			break;
		}
	}
	fclose(sysinfo);

	/* Missing manufacturer, type or model information should not happen */
	if (!manufacturer[0] || !type[0] || !model[0])
		return -1;

	/*
	 * Scan /proc/service_levels and return the CPU-MF counter facility
	 * version number and authorization level.
	 * Optional, does not exist on z/VM guests.
	 */
	sysinfo = fopen(SRVLVL, "r");
	if (sysinfo == NULL)
		goto skip_sysinfo;
	while ((read = getline(&line, &line_sz, sysinfo)) != -1) {
		if (strncmp(line, SRVLVL_CPUMF, strlen(SRVLVL_CPUMF)))
			continue;

		line2 = line + strlen(SRVLVL_CPUMF);
		while ((cp = strtok_r(line2, "\n ", &line2))) {
			if (!strncmp(cp, SRVLVL_VERSION,
				     strlen(SRVLVL_VERSION))) {
				char *sep = strchr(cp, '=');

				vssize += scnprintf(version + vssize,
						    sizeof(version) - vssize, "%s", sep + 1);
			}
			if (!strncmp(cp, SRVLVL_AUTHORIZATION,
				     strlen(SRVLVL_AUTHORIZATION))) {
				char *sep = strchr(cp, '=');

				atsize += scnprintf(authorization + atsize,
						    sizeof(authorization) - atsize, "%s", sep + 1);
			}
		}
	}
	fclose(sysinfo);

skip_sysinfo:
	free(line);

	if (version[0] && authorization[0] )
		nbytes = snprintf(buffer, sz, "%s,%s,%s,%s,%s",
				  manufacturer, type, model, version,
				  authorization);
	else
		nbytes = snprintf(buffer, sz, "%s,%s,%s", manufacturer, type,
				  model);
	return (nbytes >= sz) ? -1 : 0;
}

char *get_cpuid_str(struct perf_pmu *pmu __maybe_unused)
{
	char *buf = malloc(128);

	if (buf && get_cpuid(buf, 128) < 0)
		zfree(&buf);
	return buf;
}
