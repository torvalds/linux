/*-
 * Copyright (c) 2013-2015 Sandvine Inc.
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
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "iovctl.h"

static void	config_action(const char *filename, int dryrun);
static void	delete_action(const char *device, int dryrun);
static void	print_schema(const char *device);

/*
 * Fetch the config schema from the kernel via ioctl.  This function has to
 * call the ioctl twice: the first returns the amount of memory that we need
 * to allocate for the schema, and the second actually fetches the schema.
 */
static nvlist_t *
get_schema(int fd)
{
	struct pci_iov_schema arg;
	nvlist_t *schema;
	int error;

	/* Do the ioctl() once to fetch the size of the schema. */
	arg.schema = NULL;
	arg.len = 0;
	arg.error = 0;
	error = ioctl(fd, IOV_GET_SCHEMA, &arg);
	if (error != 0)
		err(1, "Could not fetch size of config schema");

	arg.schema = malloc(arg.len);
	if (arg.schema == NULL)
		err(1, "Could not allocate %zu bytes for schema",
		    arg.len);

	/* Now do the ioctl() for real to get the schema. */
	error = ioctl(fd, IOV_GET_SCHEMA, &arg);
	if (error != 0 || arg.error != 0) {
		if (arg.error != 0)
			errno = arg.error;
		err(1, "Could not fetch config schema");
	}

	schema = nvlist_unpack(arg.schema, arg.len, NV_FLAG_IGNORE_CASE);
	if (schema == NULL)
		err(1, "Could not unpack schema");

	free(arg.schema);
	return (schema);
}

/*
 * Call the ioctl that activates SR-IOV and creates the VFs.
 */
static void
config_iov(int fd, const char *dev_name, const nvlist_t *config, int dryrun)
{
	struct pci_iov_arg arg;
	int error;

	arg.config = nvlist_pack(config, &arg.len);
	if (arg.config == NULL)
		err(1, "Could not pack configuration");

	if (dryrun) {
		printf("Would enable SR-IOV on device '%s'.\n", dev_name);
		printf(
		    "The following configuration parameters would be used:\n");
		nvlist_fdump(config, stdout);
		printf(
		"The configuration parameters consume %zu bytes when packed.\n",
		    arg.len);
	} else {
		error = ioctl(fd, IOV_CONFIG, &arg);
		if (error != 0)
			err(1, "Failed to configure SR-IOV");
	}

	free(arg.config);
}

static int
open_device(const char *dev_name)
{
	char *dev;
	int fd;
	size_t copied, size;
	long path_max;

	path_max = pathconf("/dev", _PC_PATH_MAX);
	if (path_max < 0)
		err(1, "Could not get maximum path length");

	size = path_max;
	dev = malloc(size);
	if (dev == NULL)
		err(1, "Could not allocate memory for device path");

	if (dev_name[0] == '/')
		copied = strlcpy(dev, dev_name, size);
	else
		copied = snprintf(dev, size, "/dev/iov/%s", dev_name);

	/* >= to account for null terminator. */
	if (copied >= size)
		errx(1, "Provided file name too long");

	fd = open(dev, O_RDWR);
	if (fd < 0)
		err(1, "Could not open device '%s'", dev);

	free(dev);
	return (fd);
}

static void
usage(void)
{

	warnx("Usage: iovctl -C -f <config file> [-n]");
	warnx("       iovctl -D [-d <PF device> | -f <config file>] [-n]");
	warnx("       iovctl -S [-d <PF device> | -f <config file>]");
	exit(1);

}

enum main_action {
	NONE,
	CONFIG,
	DELETE,
	PRINT_SCHEMA,
};

int
main(int argc, char **argv)
{
	char *device;
	const char *filename;
	int ch, dryrun;
	enum main_action action;

	device = NULL;
	filename = NULL;
	dryrun = 0;
	action = NONE;

	while ((ch = getopt(argc, argv, "Cd:Df:nS")) != -1) {
		switch (ch) {
		case 'C':
			if (action != NONE) {
				warnx(
				   "Only one of -C, -D or -S may be specified");
				usage();
			}
			action = CONFIG;
			break;
		case 'd':
			device = strdup(optarg);
			break;
		case 'D':
			if (action != NONE) {
				warnx(
				   "Only one of -C, -D or -S may be specified");
				usage();
			}
			action = DELETE;
			break;
		case 'f':
			filename = optarg;
			break;
		case 'n':
			dryrun = 1;
			break;
		case 'S':
			if (action != NONE) {
				warnx(
				   "Only one of -C, -D or -S may be specified");
				usage();
			}
			action = PRINT_SCHEMA;
			break;
		case '?':
			warnx("Unrecognized argument '-%c'\n", optopt);
			usage();
			break;
		}
	}

	if (device != NULL && filename != NULL) {
		warnx("Only one of the -d and -f flags may be specified");
		usage();
	}

	if (device == NULL && filename == NULL  && action != CONFIG) {
		warnx("Either the -d or -f flag must be specified");
		usage();
	}

	switch (action) {
	case CONFIG:
		if (device != NULL) {
			warnx("-d flag cannot be used with the -C flag");
			usage();
		}
		if (filename == NULL) {
			warnx("The -f flag must be specified");
			usage();
		}
		config_action(filename, dryrun);
		break;
	case DELETE:
		if (device == NULL)
			device = find_device(filename);
		delete_action(device, dryrun);
		free(device);
		break;
	case PRINT_SCHEMA:
		if (dryrun) {
			warnx("-n flag cannot be used with the -S flag");
			usage();
		}
		if (device == NULL)
			device = find_device(filename);
		print_schema(device);
		free(device);
		break;
	default:
		usage();
		break;
	}

	exit(0);
}

static void
config_action(const char *filename, int dryrun)
{
	char *dev;
	nvlist_t *schema, *config;
	int fd;

	dev = find_device(filename);
	fd = open(dev, O_RDWR);
	if (fd < 0)
		err(1, "Could not open device '%s'", dev);

	schema = get_schema(fd);
	config = parse_config_file(filename, schema);
	if (config == NULL)
		errx(1, "Could not parse config");

	config_iov(fd, dev, config, dryrun);

	nvlist_destroy(config);
	nvlist_destroy(schema);
	free(dev);
	close(fd);
}

static void
delete_action(const char *dev_name, int dryrun)
{
	int fd, error;

	fd = open_device(dev_name);

	if (dryrun)
		printf("Would attempt to delete all VF children of '%s'\n",
		    dev_name);
	else {
		error = ioctl(fd, IOV_DELETE);
		if (error != 0)
			err(1, "Failed to delete VFs");
	}

	close(fd);
}

static void
print_default_value(const nvlist_t *parameter, const char *type)
{
	const uint8_t *mac;
	size_t size;

	if (strcasecmp(type, "bool") == 0)
		printf(" (default = %s)",
		    nvlist_get_bool(parameter, DEFAULT_SCHEMA_NAME) ? "true" :
		    "false");
	else if (strcasecmp(type, "string") == 0)
		printf(" (default = %s)",
		    nvlist_get_string(parameter, DEFAULT_SCHEMA_NAME));
	else if (strcasecmp(type, "uint8_t") == 0)
		printf(" (default = %ju)",
		    (uintmax_t)nvlist_get_number(parameter,
		    DEFAULT_SCHEMA_NAME));
	else if (strcasecmp(type, "uint16_t") == 0)
		printf(" (default = %ju)",
		    (uintmax_t)nvlist_get_number(parameter,
		    DEFAULT_SCHEMA_NAME));
	else if (strcasecmp(type, "uint32_t") == 0)
		printf(" (default = %ju)",
		    (uintmax_t)nvlist_get_number(parameter,
		    DEFAULT_SCHEMA_NAME));
	else if (strcasecmp(type, "uint64_t") == 0)
		printf(" (default = %ju)",
		    (uintmax_t)nvlist_get_number(parameter,
		    DEFAULT_SCHEMA_NAME));
	else if (strcasecmp(type, "unicast-mac") == 0) {
		mac = nvlist_get_binary(parameter, DEFAULT_SCHEMA_NAME, &size);
		printf(" (default = %02x:%02x:%02x:%02x:%02x:%02x)", mac[0],
		    mac[1], mac[2], mac[3], mac[4], mac[5]);
	} else
		errx(1, "Unexpected type in schema: '%s'", type);
}

static void
print_subsystem_schema(const nvlist_t * subsystem_schema)
{
	const char *name, *type;
	const nvlist_t *parameter;
	void *it;
	int nvtype;

	it = NULL;
	while ((name = nvlist_next(subsystem_schema, &nvtype, &it)) != NULL) {
		parameter = nvlist_get_nvlist(subsystem_schema, name);
		type = nvlist_get_string(parameter, TYPE_SCHEMA_NAME);

		printf("\t%s : %s", name, type);
		if (dnvlist_get_bool(parameter, REQUIRED_SCHEMA_NAME, false))
			printf(" (required)");
		else if (nvlist_exists(parameter, DEFAULT_SCHEMA_NAME))
			print_default_value(parameter, type);
		else
			printf(" (optional)");
		printf("\n");
	}
}

static void
print_schema(const char *dev_name)
{
	nvlist_t *schema;
	const nvlist_t *iov_schema, *driver_schema, *pf_schema, *vf_schema;
	int fd;

	fd = open_device(dev_name);
	schema = get_schema(fd);

	pf_schema = nvlist_get_nvlist(schema, PF_CONFIG_NAME);
	iov_schema = nvlist_get_nvlist(pf_schema, IOV_CONFIG_NAME);
	driver_schema = nvlist_get_nvlist(pf_schema, DRIVER_CONFIG_NAME);
	printf(
"The following configuration parameters may be configured on the PF:\n");
	print_subsystem_schema(iov_schema);
	print_subsystem_schema(driver_schema);

	vf_schema = nvlist_get_nvlist(schema, VF_SCHEMA_NAME);
	iov_schema = nvlist_get_nvlist(vf_schema, IOV_CONFIG_NAME);
	driver_schema = nvlist_get_nvlist(vf_schema, DRIVER_CONFIG_NAME);
	printf(
"\nThe following configuration parameters may be configured on a VF:\n");
	print_subsystem_schema(iov_schema);
	print_subsystem_schema(driver_schema);

	nvlist_destroy(schema);
	close(fd);
}
