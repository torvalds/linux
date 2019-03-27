/*-
 * Copyright (c) 2014-2015 Sandvine Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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
#include <sys/iov.h>
#include <sys/dnv.h>
#include <sys/nv.h>

#include <err.h>
#include <regex.h>
#include <stdlib.h>

#include "iovctl.h"

/*
 * Returns a writeable pointer to the configuration for the given device.
 * If no configuration exists, a new nvlist with empty driver and iov
 * sections is allocated and returned.
 *
 * Returning a writeable pointer requires removing the configuration from config
 * using nvlist_take.  It is the responsibility of the caller to re-insert the
 * nvlist in config with nvlist_move_nvlist.
 */
static nvlist_t *
find_config(nvlist_t *config, const char * device)
{
	nvlist_t *subsystem, *empty_driver, *empty_iov;

	subsystem = dnvlist_take_nvlist(config, device, NULL);

	if (subsystem != NULL)
		return (subsystem);

	empty_driver = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (empty_driver == NULL)
		err(1, "Could not allocate config nvlist");

	empty_iov = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (empty_iov == NULL)
		err(1, "Could not allocate config nvlist");

	subsystem = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (subsystem == NULL)
		err(1, "Could not allocate config nvlist");

	nvlist_move_nvlist(subsystem, DRIVER_CONFIG_NAME, empty_driver);
	nvlist_move_nvlist(subsystem, IOV_CONFIG_NAME, empty_iov);

	return (subsystem);
}

static uint16_t
parse_vf_num(const char *key, regmatch_t *matches)
{
	u_long vf_num;

	vf_num = strtoul(key + matches[1].rm_so, NULL, 10);

	if (vf_num > UINT16_MAX)
		errx(1, "VF number %lu is too large to be valid",
		    vf_num);

	return (vf_num);
}

/*
 * Apply the default values specified in device_defaults to the specified
 * subsystem in the given device_config.
 *
 * This function assumes that the values specified in device_defaults have
 * already been validated.
 */
static void
apply_subsystem_defaults(nvlist_t *device_config, const char *subsystem,
    const nvlist_t *device_defaults)
{
	nvlist_t *config;
	const nvlist_t *defaults;
	const char *name;
	void *cookie;
	size_t len;
	const void *bin;
	int type;

	config = nvlist_take_nvlist(device_config, subsystem);
	defaults = nvlist_get_nvlist(device_defaults, subsystem);

	cookie = NULL;
	while ((name = nvlist_next(defaults, &type, &cookie)) != NULL) {
		if (nvlist_exists(config, name))
			continue;

		switch (type) {
		case NV_TYPE_BOOL:
			nvlist_add_bool(config, name,
			    nvlist_get_bool(defaults, name));
			break;
		case NV_TYPE_NUMBER:
			nvlist_add_number(config, name,
			    nvlist_get_number(defaults, name));
			break;
		case NV_TYPE_STRING:
			nvlist_add_string(config, name,
			    nvlist_get_string(defaults, name));
			break;
		case NV_TYPE_NVLIST:
			nvlist_add_nvlist(config, name,
			    nvlist_get_nvlist(defaults, name));
			break;
		case NV_TYPE_BINARY:
			bin = nvlist_get_binary(defaults, name, &len);
			nvlist_add_binary(config, name, bin, len);
			break;
		default:
			errx(1, "Unexpected type '%d'", type);
		}
	}
	nvlist_move_nvlist(device_config, subsystem, config);
}

/*
 * Iterate over every subsystem in the given VF device and apply default values
 * for parameters that were not configured with a value.
 *
 * This function assumes that the values specified in defaults have already been
 * validated.
 */
static void
apply_defaults(nvlist_t *vf, const nvlist_t *defaults)
{

	apply_subsystem_defaults(vf, DRIVER_CONFIG_NAME, defaults);
	apply_subsystem_defaults(vf, IOV_CONFIG_NAME, defaults);
}

/*
 * Validate that all required parameters have been configured in the specified
 * subsystem.
 */
static void
validate_subsystem(const nvlist_t *device, const nvlist_t *device_schema,
    const char *subsystem_name, const char *config_name)
{
	const nvlist_t *subsystem, *schema, *config;
	const char *name;
	void *cookie;
	int type;

	subsystem = nvlist_get_nvlist(device, subsystem_name);
	schema = nvlist_get_nvlist(device_schema, subsystem_name);

	cookie = NULL;
	while ((name = nvlist_next(schema, &type, &cookie)) != NULL) {
		config = nvlist_get_nvlist(schema, name);

		if (dnvlist_get_bool(config, REQUIRED_SCHEMA_NAME, false)) {
			if (!nvlist_exists(subsystem, name))
				errx(1,
				    "Required parameter '%s' not found in '%s'",
				    name, config_name);
		}
	}
}

/*
 * Validate that all required parameters have been configured in all subsystems
 * in the device.
 */
static void
validate_device(const nvlist_t *device, const nvlist_t *schema,
    const char *config_name)
{

	validate_subsystem(device, schema, DRIVER_CONFIG_NAME, config_name);
	validate_subsystem(device, schema, IOV_CONFIG_NAME, config_name);
}

static uint16_t
get_num_vfs(const nvlist_t *pf)
{
	const nvlist_t *iov;

	iov = nvlist_get_nvlist(pf, IOV_CONFIG_NAME);
	return (nvlist_get_number(iov, "num_vfs"));
}

/*
 * Validates the configuration that has been parsed into config using the given
 * config schema.  Note that the parser is required to not insert configuration
 * keys that are not valid in the schema, and to not insert configuration values
 * that are of the incorrect type.  Therefore this function will not validate
 * either condition.  This function is only responsible for inserting config
 * file defaults in individual VF sections and removing the DEFAULT_SCHEMA_NAME
 * subsystem from config, validating that all required parameters in the schema
 * are present in each PF and VF subsystem, and that there is no VF subsystem
 * section whose number exceeds num_vfs.
 */
void
validate_config(nvlist_t *config, const nvlist_t *schema, const regex_t *vf_pat)
{
	char device_name[VF_MAX_NAME];
	regmatch_t matches[2];
	nvlist_t *defaults, *pf, *vf;
	const nvlist_t *vf_schema;
	const char *key;
	void *cookie;
	int i, type;
	uint16_t vf_num, num_vfs;

	pf = find_config(config, PF_CONFIG_NAME);
	validate_device(pf, nvlist_get_nvlist(schema, PF_CONFIG_NAME),
	    PF_CONFIG_NAME);
	nvlist_move_nvlist(config, PF_CONFIG_NAME, pf);

	num_vfs = get_num_vfs(pf);
	vf_schema = nvlist_get_nvlist(schema, VF_SCHEMA_NAME);

	if (num_vfs == 0)
		errx(1, "PF.num_vfs must be at least 1");

	defaults = dnvlist_take_nvlist(config, DEFAULT_SCHEMA_NAME, NULL);

	for (i = 0; i < num_vfs; i++) {
		snprintf(device_name, sizeof(device_name), VF_PREFIX"%d",
		    i);

		vf = find_config(config, device_name);

		if (defaults != NULL)
			apply_defaults(vf, defaults);

		validate_device(vf, vf_schema, device_name);
		nvlist_move_nvlist(config, device_name, vf);
	}
	nvlist_destroy(defaults);

	cookie = NULL;
	while ((key = nvlist_next(config, &type, &cookie)) != NULL) {
		if (regexec(vf_pat, key, nitems(matches), matches, 0) == 0) {
			vf_num = parse_vf_num(key, matches);
			if (vf_num >= num_vfs)
				errx(1,
				   "VF number %d is out of bounds (num_vfs=%d)",
				    vf_num, num_vfs);
		}
	}
}

