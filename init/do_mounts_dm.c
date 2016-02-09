/* do_mounts_dm.c
 * Copyright (C) 2010 The Chromium OS Authors <chromium-os-dev@chromium.org>
 *                    All Rights Reserved.
 * Based on do_mounts_md.c
 *
 * This file is released under the GPL.
 */
#include <linux/device-mapper.h>
#include <linux/fs.h>
#include <linux/string.h>

#include "do_mounts.h"
#include "../drivers/md/dm.h"

#define DM_MAX_NAME 32
#define DM_MAX_UUID 129
#define DM_NO_UUID "none"

#define DM_MSG_PREFIX "init"

/* Separators used for parsing the dm= argument. */
#define DM_FIELD_SEP ' '
#define DM_LINE_SEP ','

/*
 * When the device-mapper and any targets are compiled into the kernel
 * (not a module), one target may be created and used as the root device at
 * boot time with the parameters given with the boot line dm=...
 * The code for that is here.
 */

struct dm_setup_target {
	sector_t begin;
	sector_t length;
	char *type;
	char *params;
	/* simple singly linked list */
	struct dm_setup_target *next;
};

static struct {
	int minor;
	int ro;
	char name[DM_MAX_NAME];
	char uuid[DM_MAX_UUID];
	char *targets;
	struct dm_setup_target *target;
	int target_count;
} dm_setup_args __initdata;

static __initdata int dm_early_setup;

static size_t __init get_dm_option(char *str, char **next, char sep)
{
	size_t len = 0;
	char *endp = NULL;

	if (!str)
		return 0;

	endp = strchr(str, sep);
	if (!endp) {  /* act like strchrnul */
		len = strlen(str);
		endp = str + len;
	} else {
		len = endp - str;
	}

	if (endp == str)
		return 0;

	if (!next)
		return len;

	if (*endp == 0) {
		/* Don't advance past the nul. */
		*next = endp;
	} else {
		*next = endp + 1;
	}
	return len;
}

static int __init dm_setup_args_init(void)
{
	dm_setup_args.minor = 0;
	dm_setup_args.ro = 0;
	dm_setup_args.target = NULL;
	dm_setup_args.target_count = 0;
	return 0;
}

static int __init dm_setup_cleanup(void)
{
	struct dm_setup_target *target = dm_setup_args.target;
	struct dm_setup_target *old_target = NULL;
	while (target) {
		kfree(target->type);
		kfree(target->params);
		old_target = target;
		target = target->next;
		kfree(old_target);
		dm_setup_args.target_count--;
	}
	BUG_ON(dm_setup_args.target_count);
	return 0;
}

static char * __init dm_setup_parse_device_args(char *str)
{
	char *next = NULL;
	size_t len = 0;

	/* Grab the logical name of the device to be exported to udev */
	len = get_dm_option(str, &next, DM_FIELD_SEP);
	if (!len) {
		DMERR("failed to parse device name");
		goto parse_fail;
	}
	len = min(len + 1, sizeof(dm_setup_args.name));
	strlcpy(dm_setup_args.name, str, len);  /* includes nul */
	str = skip_spaces(next);

	/* Grab the UUID value or "none" */
	len = get_dm_option(str, &next, DM_FIELD_SEP);
	if (!len) {
		DMERR("failed to parse device uuid");
		goto parse_fail;
	}
	len = min(len + 1, sizeof(dm_setup_args.uuid));
	strlcpy(dm_setup_args.uuid, str, len);
	str = skip_spaces(next);

	/* Determine if the table/device will be read only or read-write */
	if (!strncmp("ro,", str, 3)) {
		dm_setup_args.ro = 1;
	} else if (!strncmp("rw,", str, 3)) {
		dm_setup_args.ro = 0;
	} else {
		DMERR("failed to parse table mode");
		goto parse_fail;
	}
	str = skip_spaces(str + 3);

	return str;

parse_fail:
	return NULL;
}

static void __init dm_substitute_devices(char *str, size_t str_len)
{
	char *candidate = str;
	char *candidate_end = str;
	char old_char;
	size_t len = 0;
	dev_t dev;

	if (str_len < 3)
		return;

	while (str && *str) {
		candidate = strchr(str, '/');
		if (!candidate)
			break;

		/* Avoid embedded slashes */
		if (candidate != str && *(candidate - 1) != DM_FIELD_SEP) {
			str = strchr(candidate, DM_FIELD_SEP);
			continue;
		}

		len = get_dm_option(candidate, &candidate_end, DM_FIELD_SEP);
		str = skip_spaces(candidate_end);
		if (len < 3 || len > 37)  /* name_to_dev_t max; maj:mix min */
			continue;

		/* Temporarily terminate with a nul */
		candidate_end--;
		old_char = *candidate_end;
		*candidate_end = '\0';

		DMDEBUG("converting candidate device '%s' to dev_t", candidate);
		/* Use the boot-time specific device naming */
		dev = name_to_dev_t(candidate);
		*candidate_end = old_char;

		DMDEBUG(" -> %u", dev);
		/* No suitable replacement found */
		if (!dev)
			continue;

		/* Rewrite the /dev/path as a major:minor */
		len = snprintf(candidate, len, "%u:%u", MAJOR(dev), MINOR(dev));
		if (!len) {
			DMERR("error substituting device major/minor.");
			break;
		}
		candidate += len;
		/* Pad out with spaces (fixing our nul) */
		while (candidate < candidate_end)
			*(candidate++) = DM_FIELD_SEP;
	}
}

static int __init dm_setup_parse_targets(char *str)
{
	char *next = NULL;
	size_t len = 0;
	struct dm_setup_target **target = NULL;

	/* Targets are defined as per the table format but with a
	 * comma as a newline separator. */
	target = &dm_setup_args.target;
	while (str && *str) {
		*target = kzalloc(sizeof(struct dm_setup_target), GFP_KERNEL);
		if (!*target) {
			DMERR("failed to allocate memory for target %d",
			      dm_setup_args.target_count);
			goto parse_fail;
		}
		dm_setup_args.target_count++;

		(*target)->begin = simple_strtoull(str, &next, 10);
		if (!next || *next != DM_FIELD_SEP) {
			DMERR("failed to parse starting sector for target %d",
			      dm_setup_args.target_count - 1);
			goto parse_fail;
		}
		str = skip_spaces(next + 1);

		(*target)->length = simple_strtoull(str, &next, 10);
		if (!next || *next != DM_FIELD_SEP) {
			DMERR("failed to parse length for target %d",
			      dm_setup_args.target_count - 1);
			goto parse_fail;
		}
		str = skip_spaces(next + 1);

		len = get_dm_option(str, &next, DM_FIELD_SEP);
		if (!len ||
		    !((*target)->type = kstrndup(str, len, GFP_KERNEL))) {
			DMERR("failed to parse type for target %d",
			      dm_setup_args.target_count - 1);
			goto parse_fail;
		}
		str = skip_spaces(next);

		len = get_dm_option(str, &next, DM_LINE_SEP);
		if (!len ||
		    !((*target)->params = kstrndup(str, len, GFP_KERNEL))) {
			DMERR("failed to parse params for target %d",
			      dm_setup_args.target_count - 1);
			goto parse_fail;
		}
		str = skip_spaces(next);

		/* Before moving on, walk through the copied target and
		 * attempt to replace all /dev/xxx with the major:minor number.
		 * It may not be possible to resolve them traditionally at
		 * boot-time. */
		dm_substitute_devices((*target)->params, len);

		target = &((*target)->next);
	}
	DMDEBUG("parsed %d targets", dm_setup_args.target_count);

	return 0;

parse_fail:
	return 1;
}

/*
 * Parse the command-line parameters given our kernel, but do not
 * actually try to invoke the DM device now; that is handled by
 * dm_setup_drive after the low-level disk drivers have initialised.
 * dm format is as follows:
 *  dm="name uuid fmode,[table line 1],[table line 2],..."
 * May be used with root=/dev/dm-0 as it always uses the first dm minor.
 */

static int __init dm_setup(char *str)
{
	dm_setup_args_init();

	str = dm_setup_parse_device_args(str);
	if (!str) {
		DMDEBUG("str is NULL");
		goto parse_fail;
	}

	/* Target parsing is delayed until we have dynamic memory */
	dm_setup_args.targets = str;

	printk(KERN_INFO "dm: will configure '%s' on dm-%d\n",
	       dm_setup_args.name, dm_setup_args.minor);

	dm_early_setup = 1;
	return 1;

parse_fail:
	printk(KERN_WARNING "dm: Invalid arguments supplied to dm=.\n");
	return 0;
}


static void __init dm_setup_drive(void)
{
	struct mapped_device *md = NULL;
	struct dm_table *table = NULL;
	struct dm_setup_target *target;
	char *uuid = dm_setup_args.uuid;
	fmode_t fmode = FMODE_READ;

	/* Finish parsing the targets. */
	if (dm_setup_parse_targets(dm_setup_args.targets))
		goto parse_fail;

	if (dm_create(dm_setup_args.minor, &md)) {
		DMDEBUG("failed to create the device");
		goto dm_create_fail;
	}
	DMDEBUG("created device '%s'", dm_device_name(md));

	/* In addition to flagging the table below, the disk must be
	 * set explicitly ro/rw. */
	set_disk_ro(dm_disk(md), dm_setup_args.ro);

	if (!dm_setup_args.ro)
		fmode |= FMODE_WRITE;
	if (dm_table_create(&table, fmode, dm_setup_args.target_count, md)) {
		DMDEBUG("failed to create the table");
		goto dm_table_create_fail;
	}

	dm_lock_md_type(md);
	target = dm_setup_args.target;
	while (target) {
		DMINFO("adding target '%llu %llu %s %s'",
		       (unsigned long long) target->begin,
		       (unsigned long long) target->length, target->type,
		       target->params);
		if (dm_table_add_target(table, target->type, target->begin,
					target->length, target->params)) {
			DMDEBUG("failed to add the target to the table");
			goto add_target_fail;
		}
		target = target->next;
	}

	if (dm_table_complete(table)) {
		DMDEBUG("failed to complete the table");
		goto table_complete_fail;
	}

	if (dm_get_md_type(md) == DM_TYPE_NONE) {
		dm_set_md_type(md, dm_table_get_type(table));
		if (dm_setup_md_queue(md)) {
			DMWARN("unable to set up device queue for new table.");
			goto setup_md_queue_fail;
		}
	} else if (dm_get_md_type(md) != dm_table_get_type(table)) {
		DMWARN("can't change device type after initial table load.");
		goto setup_md_queue_fail;
        }

	/* Suspend the device so that we can bind it to the table. */
	if (dm_suspend(md, 0)) {
		DMDEBUG("failed to suspend the device pre-bind");
		goto suspend_fail;
	}

	/* Bind the table to the device. This is the only way to associate
	 * md->map with the table and set the disk capacity directly. */
	if (dm_swap_table(md, table)) {  /* should return NULL. */
		DMDEBUG("failed to bind the device to the table");
		goto table_bind_fail;
	}

	/* Finally, resume and the device should be ready. */
	if (dm_resume(md)) {
		DMDEBUG("failed to resume the device");
		goto resume_fail;
	}

	/* Export the dm device via the ioctl interface */
	if (!strcmp(DM_NO_UUID, dm_setup_args.uuid))
		uuid = NULL;
	if (dm_ioctl_export(md, dm_setup_args.name, uuid)) {
		DMDEBUG("failed to export device with given name and uuid");
		goto export_fail;
	}
	printk(KERN_INFO "dm: dm-%d is ready\n", dm_setup_args.minor);

	dm_unlock_md_type(md);
	dm_setup_cleanup();
	return;

export_fail:
resume_fail:
table_bind_fail:
suspend_fail:
setup_md_queue_fail:
table_complete_fail:
add_target_fail:
	dm_unlock_md_type(md);
dm_table_create_fail:
	dm_put(md);
dm_create_fail:
	dm_setup_cleanup();
parse_fail:
	printk(KERN_WARNING "dm: starting dm-%d (%s) failed\n",
	       dm_setup_args.minor, dm_setup_args.name);
}

__setup("dm=", dm_setup);

void __init dm_run_setup(void)
{
	if (!dm_early_setup)
		return;
	printk(KERN_INFO "dm: attempting early device configuration.\n");
	dm_setup_drive();
}
