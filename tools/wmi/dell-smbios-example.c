// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Sample application for SMBIOS communication over WMI interface
 *  Performs the following:
 *  - Simple cmd_class/cmd_select lookup for TPM information
 *  - Simple query of known tokens and their values
 *  - Simple activation of a token
 *
 *  Copyright (C) 2017 Dell, Inc.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* if uapi header isn't installed, this might not yet exist */
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#include <linux/wmi.h>

/* It would be better to discover these using udev, but for a simple
 * application they're hardcoded
 */
static const char *ioctl_devfs = "/dev/wmi/dell-smbios";
static const char *token_sysfs =
			"/sys/bus/platform/devices/dell-smbios.0/tokens";

static void show_buffer(struct dell_wmi_smbios_buffer *buffer)
{
	printf("Call: %x/%x [%x,%x,%x,%x]\nResults: [%8x,%8x,%8x,%8x]\n",
	buffer->std.cmd_class, buffer->std.cmd_select,
	buffer->std.input[0], buffer->std.input[1],
	buffer->std.input[2], buffer->std.input[3],
	buffer->std.output[0], buffer->std.output[1],
	buffer->std.output[2], buffer->std.output[3]);
}

static int run_wmi_smbios_cmd(struct dell_wmi_smbios_buffer *buffer)
{
	int fd;
	int ret;

	fd = open(ioctl_devfs, O_NONBLOCK);
	ret = ioctl(fd, DELL_WMI_SMBIOS_CMD, buffer);
	close(fd);
	return ret;
}

static int find_token(__u16 token, __u16 *location, __u16 *value)
{
	char location_sysfs[60];
	char value_sysfs[57];
	char buf[4096];
	FILE *f;
	int ret;

	ret = sprintf(value_sysfs, "%s/%04x_value", token_sysfs, token);
	if (ret < 0) {
		printf("sprintf value failed\n");
		return 2;
	}
	f = fopen(value_sysfs, "rb");
	if (!f) {
		printf("failed to open %s\n", value_sysfs);
		return 2;
	}
	fread(buf, 1, 4096, f);
	fclose(f);
	*value = (__u16) strtol(buf, NULL, 16);

	ret = sprintf(location_sysfs, "%s/%04x_location", token_sysfs, token);
	if (ret < 0) {
		printf("sprintf location failed\n");
		return 1;
	}
	f = fopen(location_sysfs, "rb");
	if (!f) {
		printf("failed to open %s\n", location_sysfs);
		return 2;
	}
	fread(buf, 1, 4096, f);
	fclose(f);
	*location = (__u16) strtol(buf, NULL, 16);

	if (*location)
		return 0;
	return 2;
}

static int token_is_active(__u16 *location, __u16 *cmpvalue,
			   struct dell_wmi_smbios_buffer *buffer)
{
	int ret;

	buffer->std.cmd_class = CLASS_TOKEN_READ;
	buffer->std.cmd_select = SELECT_TOKEN_STD;
	buffer->std.input[0] = *location;
	ret = run_wmi_smbios_cmd(buffer);
	if (ret != 0 || buffer->std.output[0] != 0)
		return ret;
	ret = (buffer->std.output[1] == *cmpvalue);
	return ret;
}

static int query_token(__u16 token, struct dell_wmi_smbios_buffer *buffer)
{
	__u16 location;
	__u16 value;
	int ret;

	ret = find_token(token, &location, &value);
	if (ret != 0) {
		printf("unable to find token %04x\n", token);
		return 1;
	}
	return token_is_active(&location, &value, buffer);
}

static int activate_token(struct dell_wmi_smbios_buffer *buffer,
		   __u16 token)
{
	__u16 location;
	__u16 value;
	int ret;

	ret = find_token(token, &location, &value);
	if (ret != 0) {
		printf("unable to find token %04x\n", token);
		return 1;
	}
	buffer->std.cmd_class = CLASS_TOKEN_WRITE;
	buffer->std.cmd_select = SELECT_TOKEN_STD;
	buffer->std.input[0] = location;
	buffer->std.input[1] = 1;
	ret = run_wmi_smbios_cmd(buffer);
	return ret;
}

static int query_buffer_size(__u64 *buffer_size)
{
	FILE *f;

	f = fopen(ioctl_devfs, "rb");
	if (!f)
		return -EINVAL;
	fread(buffer_size, sizeof(__u64), 1, f);
	fclose(f);
	return EXIT_SUCCESS;
}

int main(void)
{
	struct dell_wmi_smbios_buffer *buffer;
	int ret;
	__u64 value = 0;

	ret = query_buffer_size(&value);
	if (ret == EXIT_FAILURE || !value) {
		printf("Unable to read buffer size\n");
		goto out;
	}
	printf("Detected required buffer size %lld\n", value);

	buffer = malloc(value);
	if (buffer == NULL) {
		printf("failed to alloc memory for ioctl\n");
		ret = -ENOMEM;
		goto out;
	}
	buffer->length = value;

	/* simple SMBIOS call for looking up TPM info */
	buffer->std.cmd_class = CLASS_FLASH_INTERFACE;
	buffer->std.cmd_select = SELECT_FLASH_INTERFACE;
	buffer->std.input[0] = 2;
	ret = run_wmi_smbios_cmd(buffer);
	if (ret) {
		printf("smbios ioctl failed: %d\n", ret);
		ret = EXIT_FAILURE;
		goto out;
	}
	show_buffer(buffer);

	/* query some tokens */
	ret = query_token(CAPSULE_EN_TOKEN, buffer);
	printf("UEFI Capsule enabled token is: %d\n", ret);
	ret = query_token(CAPSULE_DIS_TOKEN, buffer);
	printf("UEFI Capsule disabled token is: %d\n", ret);

	/* activate UEFI capsule token if disabled */
	if (ret) {
		printf("Enabling UEFI capsule token");
		if (activate_token(buffer, CAPSULE_EN_TOKEN)) {
			printf("activate failed\n");
			ret = -1;
			goto out;
		}
	}
	ret = EXIT_SUCCESS;
out:
	free(buffer);
	return ret;
}
