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
#include <sys/conf.h>
#include <sys/ctype.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/iov.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/queue.h>

#include <machine/stdarg.h>

#include <sys/dnv.h>
#include <sys/nv.h>
#include <sys/iov_schema.h>

#include <net/ethernet.h>

#include <dev/pci/schema_private.h>

struct config_type_validator;
typedef int (validate_func)(const struct config_type_validator *,
   const nvlist_t *, const char *name);
typedef int (default_validate_t)(const struct config_type_validator *,
   const nvlist_t *);

static validate_func pci_iov_schema_validate_bool;
static validate_func pci_iov_schema_validate_string;
static validate_func pci_iov_schema_validate_uint;
static validate_func pci_iov_schema_validate_unicast_mac;

static default_validate_t pci_iov_validate_bool_default;
static default_validate_t pci_iov_validate_string_default;
static default_validate_t pci_iov_validate_uint_default;
static default_validate_t pci_iov_validate_unicast_mac_default;

struct config_type_validator {
	const char *type_name;
	validate_func *validate;
	default_validate_t *default_validate;
	uintmax_t limit;
};

static struct config_type_validator pci_iov_schema_validators[] = {
	{
		.type_name = "bool",
		.validate = pci_iov_schema_validate_bool,
		.default_validate = pci_iov_validate_bool_default
	},
	{
		.type_name = "string",
		.validate = pci_iov_schema_validate_string,
		.default_validate = pci_iov_validate_string_default
	},
	{
		.type_name = "uint8_t",
		.validate = pci_iov_schema_validate_uint,
		.default_validate = pci_iov_validate_uint_default,
		.limit = UINT8_MAX
	},
	{
		.type_name = "uint16_t",
		.validate = pci_iov_schema_validate_uint,
		.default_validate = pci_iov_validate_uint_default,
		.limit = UINT16_MAX
	},
	{
		.type_name = "uint32_t",
		.validate = pci_iov_schema_validate_uint,
		.default_validate = pci_iov_validate_uint_default,
		.limit = UINT32_MAX
	},
	{
		.type_name = "uint64_t",
		.validate = pci_iov_schema_validate_uint,
		.default_validate = pci_iov_validate_uint_default,
		.limit = UINT64_MAX
	},
	{
		.type_name = "unicast-mac",
		.validate = pci_iov_schema_validate_unicast_mac,
		.default_validate = pci_iov_validate_unicast_mac_default,
	},
};

static const struct config_type_validator *
pci_iov_schema_find_validator(const char *type)
{
	struct config_type_validator *validator;
	int i;

	for (i = 0; i < nitems(pci_iov_schema_validators); i++) {
		validator = &pci_iov_schema_validators[i];
		if (strcmp(type, validator->type_name) == 0)
			return (validator);
	}

	return (NULL);
}

static void
pci_iov_schema_add_type(nvlist_t *entry, const char *type)
{

	if (pci_iov_schema_find_validator(type) == NULL) {
		nvlist_set_error(entry, EINVAL);
		return;
	}
	nvlist_add_string(entry, "type", type);
}

static void
pci_iov_schema_add_required(nvlist_t *entry, uint32_t flags)
{

	if (flags & IOV_SCHEMA_REQUIRED) {
		if (flags & IOV_SCHEMA_HASDEFAULT) {
			nvlist_set_error(entry, EINVAL);
			return;
		}

		nvlist_add_bool(entry, "required", 1);
	}
}

void
pci_iov_schema_add_bool(nvlist_t *schema, const char *name, uint32_t flags,
    int defaultVal)
{
	nvlist_t *entry;

	entry = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (entry == NULL) {
		nvlist_set_error(schema, ENOMEM);
		return;
	}

	pci_iov_schema_add_type(entry, "bool");
	if (flags & IOV_SCHEMA_HASDEFAULT)
		nvlist_add_bool(entry, "default", defaultVal);
	pci_iov_schema_add_required(entry, flags);

	nvlist_move_nvlist(schema, name, entry);
}

void
pci_iov_schema_add_string(nvlist_t *schema, const char *name, uint32_t flags,
    const char *defaultVal)
{
	nvlist_t *entry;

	entry = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (entry == NULL) {
		nvlist_set_error(schema, ENOMEM);
		return;
	}

	pci_iov_schema_add_type(entry, "string");
	if (flags & IOV_SCHEMA_HASDEFAULT)
		nvlist_add_string(entry, "default", defaultVal);
	pci_iov_schema_add_required(entry, flags);

	nvlist_move_nvlist(schema, name, entry);
}

static void
pci_iov_schema_int(nvlist_t *schema, const char *name, const char *type,
    uint32_t flags, uint64_t defaultVal)
{
	nvlist_t *entry;

	entry = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (entry == NULL) {
		nvlist_set_error(schema, ENOMEM);
		return;
	}

	pci_iov_schema_add_type(entry, type);
	if (flags & IOV_SCHEMA_HASDEFAULT)
		nvlist_add_number(entry, "default", defaultVal);
	pci_iov_schema_add_required(entry, flags);

	nvlist_move_nvlist(schema, name, entry);
}

void
pci_iov_schema_add_uint8(nvlist_t *schema, const char *name, uint32_t flags,
    uint8_t defaultVal)
{

	pci_iov_schema_int(schema, name, "uint8_t", flags, defaultVal);
}

void
pci_iov_schema_add_uint16(nvlist_t *schema, const char *name, uint32_t flags,
    uint16_t defaultVal)
{

	pci_iov_schema_int(schema, name, "uint16_t", flags, defaultVal);
}

void
pci_iov_schema_add_uint32(nvlist_t *schema, const char *name, uint32_t flags,
    uint32_t defaultVal)
{

	pci_iov_schema_int(schema, name, "uint32_t", flags, defaultVal);
}

void
pci_iov_schema_add_uint64(nvlist_t *schema, const char *name, uint32_t flags,
    uint64_t defaultVal)
{

	pci_iov_schema_int(schema, name, "uint64_t", flags, defaultVal);
}

void
pci_iov_schema_add_unicast_mac(nvlist_t *schema, const char *name,
    uint32_t flags, const uint8_t * defaultVal)
{
	nvlist_t *entry;

	entry = nvlist_create(NV_FLAG_IGNORE_CASE);
	if (entry == NULL) {
		nvlist_set_error(schema, ENOMEM);
		return;
	}

	pci_iov_schema_add_type(entry, "unicast-mac");
	if (flags & IOV_SCHEMA_HASDEFAULT)
		nvlist_add_binary(entry, "default", defaultVal, ETHER_ADDR_LEN);
	pci_iov_schema_add_required(entry, flags);

	nvlist_move_nvlist(schema, name, entry);
}

static int
pci_iov_schema_validate_bool(const struct config_type_validator * validator,
   const nvlist_t *config, const char *name)
{

	if (!nvlist_exists_bool(config, name))
		return (EINVAL);
	return (0);
}

static int
pci_iov_schema_validate_string(const struct config_type_validator * validator,
   const nvlist_t *config, const char *name)
{

	if (!nvlist_exists_string(config, name))
		return (EINVAL);
	return (0);
}

static int
pci_iov_schema_validate_uint(const struct config_type_validator * validator,
   const nvlist_t *config, const char *name)
{
	uint64_t value;

	if (!nvlist_exists_number(config, name))
		return (EINVAL);

	value = nvlist_get_number(config, name);

	if (value > validator->limit)
		return (EINVAL);

	return (0);
}

static int
pci_iov_schema_validate_unicast_mac(
   const struct config_type_validator * validator,
   const nvlist_t *config, const char *name)
{
	const uint8_t *mac;
	size_t size;

	if (!nvlist_exists_binary(config, name))
		return (EINVAL);

	mac = nvlist_get_binary(config, name, &size);

	if (size != ETHER_ADDR_LEN)
		return (EINVAL);

	if (ETHER_IS_MULTICAST(mac))
		return (EINVAL);

	return (0);
}

static void
pci_iov_config_add_default(const nvlist_t *param_schema, const char *name,
    nvlist_t *config)
{
	const void *binary;
	size_t len;

	if (nvlist_exists_binary(param_schema, "default")) {
		binary = nvlist_get_binary(param_schema, "default", &len);
		nvlist_add_binary(config, name, binary, len);
	} else if (nvlist_exists_bool(param_schema, "default"))
		nvlist_add_bool(config, name,
		    nvlist_get_bool(param_schema, "default"));
	else if (nvlist_exists_number(param_schema, "default"))
		nvlist_add_number(config, name,
		    nvlist_get_number(param_schema, "default"));
	else if (nvlist_exists_nvlist(param_schema, "default"))
		nvlist_add_nvlist(config, name,
		    nvlist_get_nvlist(param_schema, "default"));
	else if (nvlist_exists_string(param_schema, "default"))
		nvlist_add_string(config, name,
		    nvlist_get_string(param_schema, "default"));
	else
		panic("Unexpected nvlist type");
}

static int
pci_iov_validate_bool_default(const struct config_type_validator * validator,
   const nvlist_t *param)
{

	if (!nvlist_exists_bool(param, DEFAULT_SCHEMA_NAME))
		return (EINVAL);
	return (0);
}

static int
pci_iov_validate_string_default(const struct config_type_validator * validator,
   const nvlist_t *param)
{

	if (!nvlist_exists_string(param, DEFAULT_SCHEMA_NAME))
		return (EINVAL);
	return (0);
}

static int
pci_iov_validate_uint_default(const struct config_type_validator * validator,
   const nvlist_t *param)
{
	uint64_t defaultVal;

	if (!nvlist_exists_number(param, DEFAULT_SCHEMA_NAME))
		return (EINVAL);

	defaultVal = nvlist_get_number(param, DEFAULT_SCHEMA_NAME);
	if (defaultVal > validator->limit)
		return (EINVAL);
	return (0);
}

static int
pci_iov_validate_unicast_mac_default(
   const struct config_type_validator * validator, const nvlist_t *param)
{
	const uint8_t *mac;
	size_t size;

	if (!nvlist_exists_binary(param, DEFAULT_SCHEMA_NAME))
		return (EINVAL);

	mac = nvlist_get_binary(param, DEFAULT_SCHEMA_NAME, &size);
	if (size != ETHER_ADDR_LEN)
		return (EINVAL);

	if (ETHER_IS_MULTICAST(mac))
		return (EINVAL);
	return (0);
}

static int
pci_iov_validate_param_schema(const nvlist_t *schema)
{
	const struct config_type_validator *validator;
	const char *type;
	int error;

	/* All parameters must define a type. */
	if (!nvlist_exists_string(schema, TYPE_SCHEMA_NAME))
		return (EINVAL);
	type = nvlist_get_string(schema, TYPE_SCHEMA_NAME);

	validator = pci_iov_schema_find_validator(type);
	if (validator == NULL)
		return (EINVAL);

	/* Validate that the default value conforms to the type. */
	if (nvlist_exists(schema, DEFAULT_SCHEMA_NAME)) {
		error = validator->default_validate(validator, schema);
		if (error != 0)
			return (error);

		/* Required and Default are mutually exclusive. */
		if (nvlist_exists(schema, REQUIRED_SCHEMA_NAME))
			return (EINVAL);
	}

	/* The "Required" field must be a bool. */
	if (nvlist_exists(schema, REQUIRED_SCHEMA_NAME)) {
		if (!nvlist_exists_bool(schema, REQUIRED_SCHEMA_NAME))
			return (EINVAL);
	}

	return (0);
}

static int
pci_iov_validate_subsystem_schema(const nvlist_t *dev_schema, const char *name)
{
	const nvlist_t *sub_schema, *param_schema;
	const char *param_name;
	void *it;
	int type, error;

	if (!nvlist_exists_nvlist(dev_schema, name))
		return (EINVAL);
	sub_schema = nvlist_get_nvlist(dev_schema, name);

	it = NULL;
	while ((param_name = nvlist_next(sub_schema, &type, &it)) != NULL) {
		if (type != NV_TYPE_NVLIST)
			return (EINVAL);
		param_schema = nvlist_get_nvlist(sub_schema, param_name);

		error = pci_iov_validate_param_schema(param_schema);
		if (error != 0)
			return (error);
	}

	return (0);
}

/*
 * Validate that the driver schema does not define any configuration parameters
 * whose names collide with configuration parameters defined in the iov schema.
 */
static int
pci_iov_validate_param_collisions(const nvlist_t *dev_schema)
{
	const nvlist_t *iov_schema, *driver_schema;
	const char *name;
	void *it;
	int type;

	driver_schema = nvlist_get_nvlist(dev_schema, DRIVER_CONFIG_NAME);
	iov_schema = nvlist_get_nvlist(dev_schema, IOV_CONFIG_NAME);

	it = NULL;
	while ((name = nvlist_next(driver_schema, &type, &it)) != NULL) {
		if (nvlist_exists(iov_schema, name))
			return (EINVAL);
	}

	return (0);
}

/*
 * Validate that we only have IOV and DRIVER subsystems beneath the given
 * device schema node.
 */
static int
pci_iov_validate_schema_subsystems(const nvlist_t *dev_schema)
{
	const char *name;
	void *it;
	int type;

	it = NULL;
	while ((name = nvlist_next(dev_schema, &type, &it)) != NULL) {
		if (strcmp(name, IOV_CONFIG_NAME) != 0 &&
		    strcmp(name, DRIVER_CONFIG_NAME) != 0)
			return (EINVAL);
	}

	return (0);
}

static int
pci_iov_validate_device_schema(const nvlist_t *schema, const char *name)
{
	const nvlist_t *dev_schema;
	int error;

	if (!nvlist_exists_nvlist(schema, name))
		return (EINVAL);
	dev_schema = nvlist_get_nvlist(schema, name);

	error = pci_iov_validate_subsystem_schema(dev_schema, IOV_CONFIG_NAME);
	if (error != 0)
		return (error);

	error = pci_iov_validate_subsystem_schema(dev_schema,
	    DRIVER_CONFIG_NAME);
	if (error != 0)
		return (error);

	error = pci_iov_validate_param_collisions(dev_schema);
	if (error != 0)
		return (error);

	return (pci_iov_validate_schema_subsystems(dev_schema));
}

/* Validate that we only have PF and VF devices beneath the top-level schema. */
static int
pci_iov_validate_schema_devices(const nvlist_t *dev_schema)
{
	const char *name;
	void *it;
	int type;

	it = NULL;
	while ((name = nvlist_next(dev_schema, &type, &it)) != NULL) {
		if (strcmp(name, PF_CONFIG_NAME) != 0 &&
		    strcmp(name, VF_SCHEMA_NAME) != 0)
			return (EINVAL);
	}

	return (0);
}

int
pci_iov_validate_schema(const nvlist_t *schema)
{
	int error;

	error = pci_iov_validate_device_schema(schema, PF_CONFIG_NAME);
	if (error != 0)
		return (error);

	error = pci_iov_validate_device_schema(schema, VF_SCHEMA_NAME);
	if (error != 0)
		return (error);

	return (pci_iov_validate_schema_devices(schema));
}

/*
 * Validate that all required parameters from the schema are specified in the
 * config.  If any parameter with a default value is not specified in the
 * config, add it to config.
 */
static int
pci_iov_schema_validate_required(const nvlist_t *schema, nvlist_t *config)
{
	const nvlist_t *param_schema;
	const char *name;
	void *cookie;
	int type;

	cookie = NULL;
	while ((name = nvlist_next(schema, &type, &cookie)) != NULL) {
		param_schema = nvlist_get_nvlist(schema, name);

		if (dnvlist_get_bool(param_schema, "required", 0)) {
			if (!nvlist_exists(config, name))
				return (EINVAL);
		}

		if (nvlist_exists(param_schema, "default") &&
		    !nvlist_exists(config, name))
			pci_iov_config_add_default(param_schema, name, config);
	}

	return (nvlist_error(config));
}

static int
pci_iov_schema_validate_param(const nvlist_t *schema_param, const char *name,
    const nvlist_t *config)
{
	const struct config_type_validator *validator;
	const char *type;

	type = nvlist_get_string(schema_param, "type");
	validator = pci_iov_schema_find_validator(type);

	KASSERT(validator != NULL,
	    ("Schema was not validated: Unknown type %s", type));

	return (validator->validate(validator, config, name));
}

/*
 * Validate that all parameters in config are defined in the schema.  Also
 * validate that the type of the parameter matches the type in the schema.
 */
static int
pci_iov_schema_validate_types(const nvlist_t *schema, const nvlist_t *config)
{
	const nvlist_t *schema_param;
	void *cookie;
	const char *name;
	int type, error;

	cookie = NULL;
	while ((name = nvlist_next(config, &type, &cookie)) != NULL) {
		if (!nvlist_exists_nvlist(schema, name))
			return (EINVAL);

		schema_param = nvlist_get_nvlist(schema, name);

		error = pci_iov_schema_validate_param(schema_param, name,
		    config);

		if (error != 0)
			return (error);
	}

	return (0);
}

static int
pci_iov_schema_validate_device(const nvlist_t *schema, nvlist_t *config,
    const char *schema_device, const char *config_device)
{
	const nvlist_t *device_schema, *iov_schema, *driver_schema;
	nvlist_t *device_config, *iov_config, *driver_config;
	int error;

	device_config = NULL;
	iov_config = NULL;
	driver_config = NULL;

	device_schema = nvlist_get_nvlist(schema, schema_device);
	iov_schema = nvlist_get_nvlist(device_schema, IOV_CONFIG_NAME);
	driver_schema = nvlist_get_nvlist(device_schema, DRIVER_CONFIG_NAME);

	device_config = dnvlist_take_nvlist(config, config_device, NULL);
	if (device_config == NULL) {
		error = EINVAL;
		goto out;
	}

	iov_config = dnvlist_take_nvlist(device_config, IOV_CONFIG_NAME, NULL);
	if (iov_config == NULL) {
		error = EINVAL;
		goto out;
	}

	driver_config = dnvlist_take_nvlist(device_config, DRIVER_CONFIG_NAME,
	    NULL);
	if (driver_config == NULL) {
		error = EINVAL;
		goto out;
	}

	error = pci_iov_schema_validate_required(iov_schema, iov_config);
	if (error != 0)
		goto out;

	error = pci_iov_schema_validate_required(driver_schema, driver_config);
	if (error != 0)
		goto out;

	error = pci_iov_schema_validate_types(iov_schema, iov_config);
	if (error != 0)
		goto out;

	error = pci_iov_schema_validate_types(driver_schema, driver_config);
	if (error != 0)
		goto out;

out:
	/* Note that these functions handle NULL pointers safely. */
	nvlist_move_nvlist(device_config, IOV_CONFIG_NAME, iov_config);
	nvlist_move_nvlist(device_config, DRIVER_CONFIG_NAME, driver_config);
	nvlist_move_nvlist(config, config_device, device_config);

	return (error);
}

static int
pci_iov_schema_validate_vfs(const nvlist_t *schema, nvlist_t *config,
    uint16_t num_vfs)
{
	char device[VF_MAX_NAME];
	int i, error;

	for (i = 0; i < num_vfs; i++) {
		snprintf(device, sizeof(device), VF_PREFIX"%d", i);

		error = pci_iov_schema_validate_device(schema, config,
		    VF_SCHEMA_NAME, device);
		if (error != 0)
			return (error);
	}

	return (0);
}

/*
 * Validate that the device node only has IOV and DRIVER subnodes.
 */
static int
pci_iov_schema_validate_device_subsystems(const nvlist_t *config)
{
	void *cookie;
	const char *name;
	int type;

	cookie = NULL;
	while ((name = nvlist_next(config, &type, &cookie)) != NULL) {
		if (strcasecmp(name, IOV_CONFIG_NAME) == 0)
			continue;
		else if (strcasecmp(name, DRIVER_CONFIG_NAME) == 0)
			continue;

		return (EINVAL);
	}

	return (0);
}

/*
 * Validate that the string is a valid device node name.  It must either be "PF"
 * or "VF-n", where n is an integer in the range [0, num_vfs).
 */
static int
pci_iov_schema_validate_dev_name(const char *name, uint16_t num_vfs)
{
	const char *number_start;
	char *endp;
	u_long vf_num;

	if (strcasecmp(PF_CONFIG_NAME, name) == 0)
		return (0);

	/* Ensure that we start with "VF-" */
	if (strncasecmp(name, VF_PREFIX, VF_PREFIX_LEN) != 0)
		return (EINVAL);

	number_start = name + VF_PREFIX_LEN;

	/* Filter out name == "VF-" (no number) */
	if (number_start[0] == '\0')
		return (EINVAL);

	/* Disallow leading whitespace or +/- */
	if (!isdigit(number_start[0]))
		return (EINVAL);

	vf_num = strtoul(number_start, &endp, 10);
	if (*endp != '\0')
		return (EINVAL);

	/* Disallow leading zeros on VF-[1-9][0-9]* */
	if (vf_num != 0 && number_start[0] == '0')
		return (EINVAL);

	/* Disallow leading zeros on VF-0 */
	if (vf_num == 0 && number_start[1] != '\0')
		return (EINVAL);

	if (vf_num >= num_vfs)
		return (EINVAL);

	return (0);
}

/*
 * Validate that there are no device nodes in config other than the ones for
 * the PF and the VFs.  This includes validating that all config nodes of the
 * form VF-n specify a VF number that is < num_vfs.
 */
static int
pci_iov_schema_validate_device_names(const nvlist_t *config, uint16_t num_vfs)
{
	const nvlist_t *device;
	void *cookie;
	const char *name;
	int type, error;

	cookie = NULL;
	while ((name = nvlist_next(config, &type, &cookie)) != NULL) {
		error = pci_iov_schema_validate_dev_name(name, num_vfs);
		if (error != 0)
			return (error);

		/*
		 * Note that as this is a valid PF/VF node, we know that
		 * pci_iov_schema_validate_device() has already checked that
		 * the PF/VF node is an nvlist.
		 */
		device = nvlist_get_nvlist(config, name);
		error = pci_iov_schema_validate_device_subsystems(device);
		if (error != 0)
			return (error);
	}

	return (0);
}

int
pci_iov_schema_validate_config(const nvlist_t *schema, nvlist_t *config)
{
	int error;
	uint16_t num_vfs;

	error = pci_iov_schema_validate_device(schema, config, PF_CONFIG_NAME,
	    PF_CONFIG_NAME);
	if (error != 0)
		return (error);

	num_vfs = pci_iov_config_get_num_vfs(config);

	error = pci_iov_schema_validate_vfs(schema, config, num_vfs);
	if (error != 0)
		return (error);

	return (pci_iov_schema_validate_device_names(config, num_vfs));
}

/*
 * Return value of the num_vfs parameter.  config must have already been
 * validated, which guarantees that the parameter exists.
 */
uint16_t
pci_iov_config_get_num_vfs(const nvlist_t *config)
{
	const nvlist_t *pf, *iov;

	pf = nvlist_get_nvlist(config, PF_CONFIG_NAME);
	iov = nvlist_get_nvlist(pf, IOV_CONFIG_NAME);
	return (nvlist_get_number(iov, "num_vfs"));
}

/* Allocate a new empty schema node. */
nvlist_t *
pci_iov_schema_alloc_node(void)
{

	return (nvlist_create(NV_FLAG_IGNORE_CASE));
}
