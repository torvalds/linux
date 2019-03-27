/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008, 2009 Yahoo!, Inc.
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
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#ifdef DEBUG
#include <sys/sysctl.h>
#endif
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <libutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "mfiutil.h"

static int	add_spare(int ac, char **av);
static int	remove_spare(int ac, char **av);

static long
dehumanize(const char *value)
{
        char    *vtp;
        long    iv;
 
        if (value == NULL)
                return (0);
        iv = strtoq(value, &vtp, 0);
        if (vtp == value || (vtp[0] != '\0' && vtp[1] != '\0')) {
                return (0);
        }
        switch (vtp[0]) {
        case 't': case 'T':
                iv *= 1024;
        case 'g': case 'G':
                iv *= 1024;
        case 'm': case 'M':
                iv *= 1024;
        case 'k': case 'K':
                iv *= 1024;
        case '\0':
                break;
        default:
                return (0);
        }
        return (iv);
}

int
mfi_config_read(int fd, struct mfi_config_data **configp)
{
	return mfi_config_read_opcode(fd, MFI_DCMD_CFG_READ, configp, NULL, 0);
}

int
mfi_config_read_opcode(int fd, uint32_t opcode, struct mfi_config_data **configp,
	uint8_t *mbox, size_t mboxlen)
{
	struct mfi_config_data *config;
	uint32_t config_size;
	int error;

	/*
	 * Keep fetching the config in a loop until we have a large enough
	 * buffer to hold the entire configuration.
	 */
	config = NULL;
	config_size = 1024;
fetch:
	config = reallocf(config, config_size);
	if (config == NULL)
		return (-1);
	if (mfi_dcmd_command(fd, opcode, config,
	    config_size, mbox, mboxlen, NULL) < 0) {
		error = errno;
		free(config);
		errno = error;
		return (-1);
	}

	if (config->size > config_size) {
		config_size = config->size;
		goto fetch;
	}

	*configp = config;
	return (0);
}

static struct mfi_array *
mfi_config_lookup_array(struct mfi_config_data *config, uint16_t array_ref)
{
	struct mfi_array *ar;
	char *p;
	int i;

	p = (char *)config->array;
	for (i = 0; i < config->array_count; i++) {
		ar = (struct mfi_array *)p;
		if (ar->array_ref == array_ref)
			return (ar);
		p += config->array_size;
	}

	return (NULL);
}

static struct mfi_ld_config *
mfi_config_lookup_volume(struct mfi_config_data *config, uint8_t target_id)
{
	struct mfi_ld_config *ld;
	char *p;
	int i;

	p = (char *)config->array + config->array_count * config->array_size;
	for (i = 0; i < config->log_drv_count; i++) {
		ld = (struct mfi_ld_config *)p;
		if (ld->properties.ld.v.target_id == target_id)
			return (ld);
		p += config->log_drv_size;
	}

	return (NULL);
}

static int
clear_config(int ac __unused, char **av __unused)
{
	struct mfi_ld_list list;
	int ch, error, fd;
	u_int i;

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (!mfi_reconfig_supported()) {
		warnx("The current mfi(4) driver does not support "
		    "configuration changes.");
		close(fd);
		return (EOPNOTSUPP);
	}

	if (mfi_ld_get_list(fd, &list, NULL) < 0) {
		error = errno;
		warn("Failed to get volume list");
		close(fd);
		return (error);
	}

	for (i = 0; i < list.ld_count; i++) {
		if (mfi_volume_busy(fd, list.ld_list[i].ld.v.target_id)) {
			warnx("Volume %s is busy and cannot be deleted",
			    mfi_volume_name(fd, list.ld_list[i].ld.v.target_id));
			close(fd);
			return (EBUSY);
		}
	}

	printf(
	    "Are you sure you wish to clear the configuration on mfi%u? [y/N] ",
	    mfi_unit);
	ch = getchar();
	if (ch != 'y' && ch != 'Y') {
		printf("\nAborting\n");
		close(fd);
		return (0);
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_CLEAR, NULL, 0, NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to clear configuration");
		close(fd);
		return (error);
	}

	printf("mfi%d: Configuration cleared\n", mfi_unit);
	close(fd);

	return (0);
}
MFI_COMMAND(top, clear, clear_config);

#define MAX_DRIVES_PER_ARRAY MFI_MAX_ROW_SIZE
#define MFI_ARRAY_SIZE sizeof(struct mfi_array)

#define	RT_RAID0	0
#define	RT_RAID1	1
#define	RT_RAID5	2
#define	RT_RAID6	3
#define	RT_JBOD		4
#define	RT_CONCAT	5
#define	RT_RAID10	6
#define	RT_RAID50	7
#define	RT_RAID60	8

static int
compare_int(const void *one, const void *two)
{
	int first, second;

	first = *(const int *)one;
	second = *(const int *)two;

	return (first - second);
}

static struct raid_type_entry {
	const char *name;
	int	raid_type;
} raid_type_table[] = {
	{ "raid0",	RT_RAID0 },
	{ "raid-0",	RT_RAID0 },
	{ "raid1",	RT_RAID1 },
	{ "raid-1",	RT_RAID1 },
	{ "mirror",	RT_RAID1 },
	{ "raid5",	RT_RAID5 },
	{ "raid-5",	RT_RAID5 },
	{ "raid6",	RT_RAID6 },
	{ "raid-6",	RT_RAID6 },
	{ "jbod",	RT_JBOD },
	{ "concat",	RT_CONCAT },
	{ "raid10",	RT_RAID10 },
	{ "raid1+0",	RT_RAID10 },
	{ "raid-10",	RT_RAID10 },
	{ "raid-1+0",	RT_RAID10 },
	{ "raid50",	RT_RAID50 },
	{ "raid5+0",	RT_RAID50 },
	{ "raid-50",	RT_RAID50 },
	{ "raid-5+0",	RT_RAID50 },
	{ "raid60",	RT_RAID60 },
	{ "raid6+0",	RT_RAID60 },
	{ "raid-60",	RT_RAID60 },
	{ "raid-6+0",	RT_RAID60 },
	{ NULL,		0 },
};

struct config_id_state {
	int	array_count;
	int	log_drv_count;
	int	*arrays;
	int	*volumes;
	uint16_t array_ref;
	uint8_t	target_id;
};

struct array_info {
	int	drive_count;
	struct mfi_pd_info *drives;
	struct mfi_array *array;
};

/* Parse a comma-separated list of drives for an array. */
static int
parse_array(int fd, int raid_type, char *array_str, struct array_info *info)
{
	struct mfi_pd_info *pinfo;
	uint16_t device_id;
	char *cp;
	u_int count;
	int error;

	cp = array_str;
	for (count = 0; cp != NULL; count++) {
		cp = strchr(cp, ',');
		if (cp != NULL) {
			cp++;
			if (*cp == ',') {
				warnx("Invalid drive list '%s'", array_str);
				return (EINVAL);
			}
		}
	}

	/* Validate the number of drives for this array. */
	if (count >= MAX_DRIVES_PER_ARRAY) {
		warnx("Too many drives for a single array: max is %d",
		    MAX_DRIVES_PER_ARRAY);
		return (EINVAL);
	}
	switch (raid_type) {
	case RT_RAID1:
	case RT_RAID10:
		if (count % 2 != 0) {
			warnx("RAID1 and RAID10 require an even number of "
			    "drives in each array");
			return (EINVAL);
		}
		break;
	case RT_RAID5:
	case RT_RAID50:
		if (count < 3) {
			warnx("RAID5 and RAID50 require at least 3 drives in "
			    "each array");
			return (EINVAL);
		}
		break;
	case RT_RAID6:
	case RT_RAID60:
		if (count < 4) {
			warnx("RAID6 and RAID60 require at least 4 drives in "
			    "each array");
			return (EINVAL);
		}
		break;
	}

	/* Validate each drive. */
	info->drives = calloc(count, sizeof(struct mfi_pd_info));
	if (info->drives == NULL) {
		warnx("malloc failed");
		return (ENOMEM);
	}
	info->drive_count = count;
	for (pinfo = info->drives; (cp = strsep(&array_str, ",")) != NULL;
	     pinfo++) {
		error = mfi_lookup_drive(fd, cp, &device_id);
		if (error) {
			free(info->drives);
			info->drives = NULL;
			return (error);
		}

		if (mfi_pd_get_info(fd, device_id, pinfo, NULL) < 0) {
			error = errno;
			warn("Failed to fetch drive info for drive %s", cp);
			free(info->drives);
			info->drives = NULL;
			return (error);
		}

		if (pinfo->fw_state != MFI_PD_STATE_UNCONFIGURED_GOOD) {
			warnx("Drive %u is not available", device_id);
			free(info->drives);
			info->drives = NULL;
			return (EINVAL);
		}

		if (pinfo->state.ddf.v.pd_type.is_foreign) {
			warnx("Drive %u is foreign", device_id);
			free(info->drives);
			info->drives = NULL;
			return (EINVAL);
		}
	}

	return (0);
}

/*
 * Find the next free array ref assuming that 'array_ref' is the last
 * one used.  'array_ref' should be 0xffff for the initial test.
 */
static uint16_t
find_next_array(struct config_id_state *state)
{
	int i;

	/* Assume the current one is used. */
	state->array_ref++;

	/* Find the next free one. */
	for (i = 0; i < state->array_count; i++)
		if (state->arrays[i] == state->array_ref)
			state->array_ref++;
	return (state->array_ref);
}

/*
 * Find the next free volume ID assuming that 'target_id' is the last
 * one used.  'target_id' should be 0xff for the initial test.
 */
static uint8_t
find_next_volume(struct config_id_state *state)
{
	int i;

	/* Assume the current one is used. */
	state->target_id++;

	/* Find the next free one. */
	for (i = 0; i < state->log_drv_count; i++)
		if (state->volumes[i] == state->target_id)
			state->target_id++;
	return (state->target_id);
}

/* Populate an array with drives. */
static void
build_array(int fd __unused, char *arrayp, struct array_info *array_info,
    struct config_id_state *state, int verbose)
{
	struct mfi_array *ar = (struct mfi_array *)arrayp;
	int i;

	ar->size = array_info->drives[0].coerced_size;
	ar->num_drives = array_info->drive_count;
	ar->array_ref = find_next_array(state);
	for (i = 0; i < array_info->drive_count; i++) {
		if (verbose)
			printf("Adding drive %s to array %u\n",
			    mfi_drive_name(NULL,
			    array_info->drives[i].ref.v.device_id,
			    MFI_DNAME_DEVICE_ID|MFI_DNAME_HONOR_OPTS),
			    ar->array_ref);
		if (ar->size > array_info->drives[i].coerced_size)
			ar->size = array_info->drives[i].coerced_size;
		ar->pd[i].ref = array_info->drives[i].ref;
		ar->pd[i].fw_state = MFI_PD_STATE_ONLINE;
	}
	array_info->array = ar;
}

/*
 * Create a volume that spans one or more arrays.
 */
static void
build_volume(char *volumep, int narrays, struct array_info *arrays,
    int raid_type, long stripe_size, struct config_id_state *state, int verbose)
{
	struct mfi_ld_config *ld = (struct mfi_ld_config *)volumep;
	struct mfi_array *ar;
	int i;

	/* properties */
	ld->properties.ld.v.target_id = find_next_volume(state);
	ld->properties.ld.v.seq = 0;
	ld->properties.default_cache_policy = MR_LD_CACHE_ALLOW_WRITE_CACHE |
	    MR_LD_CACHE_WRITE_BACK;
	ld->properties.access_policy = MFI_LD_ACCESS_RW;
	ld->properties.disk_cache_policy = MR_PD_CACHE_UNCHANGED;
	ld->properties.current_cache_policy = MR_LD_CACHE_ALLOW_WRITE_CACHE |
	    MR_LD_CACHE_WRITE_BACK;
	ld->properties.no_bgi = 0;

	/* params */
	switch (raid_type) {
	case RT_RAID0:
	case RT_JBOD:
		ld->params.primary_raid_level = DDF_RAID0;
		ld->params.raid_level_qualifier = 0;
		ld->params.secondary_raid_level = 0;
		break;
	case RT_RAID1:
		ld->params.primary_raid_level = DDF_RAID1;
		ld->params.raid_level_qualifier = 0;
		ld->params.secondary_raid_level = 0;
		break;
	case RT_RAID5:
		ld->params.primary_raid_level = DDF_RAID5;
		ld->params.raid_level_qualifier = 3;
		ld->params.secondary_raid_level = 0;
		break;
	case RT_RAID6:
		ld->params.primary_raid_level = DDF_RAID6;
		ld->params.raid_level_qualifier = 3;
		ld->params.secondary_raid_level = 0;
		break;
	case RT_CONCAT:
		ld->params.primary_raid_level = DDF_CONCAT;
		ld->params.raid_level_qualifier = 0;
		ld->params.secondary_raid_level = 0;
		break;
	case RT_RAID10:
		ld->params.primary_raid_level = DDF_RAID1;
		ld->params.raid_level_qualifier = 0;
		ld->params.secondary_raid_level = 3; /* XXX? */
		break;
	case RT_RAID50:
		/*
		 * XXX: This appears to work though the card's BIOS
		 * complains that the configuration is foreign.  The
		 * BIOS setup does not allow for creation of RAID-50
		 * or RAID-60 arrays.  The only nested array
		 * configuration it allows for is RAID-10.
		 */
		ld->params.primary_raid_level = DDF_RAID5;
		ld->params.raid_level_qualifier = 3;
		ld->params.secondary_raid_level = 3; /* XXX? */
		break;
	case RT_RAID60:
		ld->params.primary_raid_level = DDF_RAID6;
		ld->params.raid_level_qualifier = 3;
		ld->params.secondary_raid_level = 3; /* XXX? */
		break;
	}

	/*
	 * Stripe size is encoded as (2 ^ N) * 512 = stripe_size.  Use
	 * ffs() to simulate log2(stripe_size).
	 */
	ld->params.stripe_size = ffs(stripe_size) - 1 - 9;
	ld->params.num_drives = arrays[0].array->num_drives;
	ld->params.span_depth = narrays;
	ld->params.state = MFI_LD_STATE_OPTIMAL;
	ld->params.init_state = MFI_LD_PARAMS_INIT_NO;
	ld->params.is_consistent = 0;

	/* spans */
	for (i = 0; i < narrays; i++) {
		ar = arrays[i].array;
		if (verbose)
			printf("Adding array %u to volume %u\n", ar->array_ref,
			    ld->properties.ld.v.target_id);
		ld->span[i].start_block = 0;
		ld->span[i].num_blocks = ar->size;
		ld->span[i].array_ref = ar->array_ref;
	}
}

static int
create_volume(int ac, char **av)
{
	struct mfi_config_data *config;
	struct mfi_array *ar;
	struct mfi_ld_config *ld;
	struct config_id_state state;
	size_t config_size;
	char *p, *cfg_arrays, *cfg_volumes;
	int error, fd, i, raid_type;
	int narrays, nvolumes, arrays_per_volume;
	struct array_info *arrays;
	long stripe_size;
#ifdef DEBUG
	int dump;
#endif
	int ch, verbose;

	/*
	 * Backwards compat.  Map 'create volume' to 'create' and
	 * 'create spare' to 'add'.
	 */
	if (ac > 1) {
		if (strcmp(av[1], "volume") == 0) {
			av++;
			ac--;
		} else if (strcmp(av[1], "spare") == 0) {
			av++;
			ac--;
			return (add_spare(ac, av));
		}
	}

	if (ac < 2) {
		warnx("create volume: volume type required");
		return (EINVAL);
	}

	bzero(&state, sizeof(state));
	config = NULL;
	arrays = NULL;
	narrays = 0;
	error = 0;

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (!mfi_reconfig_supported()) {
		warnx("The current mfi(4) driver does not support "
		    "configuration changes.");
		error = EOPNOTSUPP;
		goto error;
	}

	/* Lookup the RAID type first. */
	raid_type = -1;
	for (i = 0; raid_type_table[i].name != NULL; i++)
		if (strcasecmp(raid_type_table[i].name, av[1]) == 0) {
			raid_type = raid_type_table[i].raid_type;
			break;
		}

	if (raid_type == -1) {
		warnx("Unknown or unsupported volume type %s", av[1]);
		error = EINVAL;
		goto error;
	}

	/* Parse any options. */
	optind = 2;
#ifdef DEBUG
	dump = 0;
#endif
	verbose = 0;
	stripe_size = 64 * 1024;

	while ((ch = getopt(ac, av, "ds:v")) != -1) {
		switch (ch) {
#ifdef DEBUG
		case 'd':
			dump = 1;
			break;
#endif
		case 's':
			stripe_size = dehumanize(optarg);
			if ((stripe_size < 512) || (!powerof2(stripe_size)))
				stripe_size = 64 * 1024;
			break;
		case 'v':
			verbose = 1;
			break;
		case '?':
		default:
			error = EINVAL;
			goto error;
		}
	}
	ac -= optind;
	av += optind;

	/* Parse all the arrays. */
	narrays = ac;
	if (narrays == 0) {
		warnx("At least one drive list is required");
		error = EINVAL;
		goto error;
	}
	switch (raid_type) {
	case RT_RAID0:
	case RT_RAID1:
	case RT_RAID5:
	case RT_RAID6:
	case RT_CONCAT:
		if (narrays != 1) {
			warnx("Only one drive list can be specified");
			error = EINVAL;
			goto error;
		}
		break;
	case RT_RAID10:
	case RT_RAID50:
	case RT_RAID60:
		if (narrays < 1) {
			warnx("RAID10, RAID50, and RAID60 require at least "
			    "two drive lists");
			error = EINVAL;
			goto error;
		}
		if (narrays > MFI_MAX_SPAN_DEPTH) {
			warnx("Volume spans more than %d arrays",
			    MFI_MAX_SPAN_DEPTH);
			error = EINVAL;
			goto error;
		}
		break;
	}
	arrays = calloc(narrays, sizeof(*arrays));
	if (arrays == NULL) {
		warnx("malloc failed");
		error = ENOMEM;
		goto error;
	}
	for (i = 0; i < narrays; i++) {
		error = parse_array(fd, raid_type, av[i], &arrays[i]);
		if (error)
			goto error;
	}

	switch (raid_type) {
	case RT_RAID10:
	case RT_RAID50:
	case RT_RAID60:
		for (i = 1; i < narrays; i++) {
			if (arrays[i].drive_count != arrays[0].drive_count) {
				warnx("All arrays must contain the same "
				    "number of drives");
				error = EINVAL;
				goto error;
			}
		}
		break;
	}

	/*
	 * Fetch the current config and build sorted lists of existing
	 * array and volume identifiers.
	 */
	if (mfi_config_read(fd, &config) < 0) {
		error = errno;
		warn("Failed to read configuration");
		goto error;
	}
	p = (char *)config->array;
	state.array_ref = 0xffff;
	state.target_id = 0xff;
	state.array_count = config->array_count;
	if (config->array_count > 0) {
		state.arrays = calloc(config->array_count, sizeof(int));
		if (state.arrays == NULL) {
			warnx("malloc failed");
			error = ENOMEM;
			goto error;
		}
		for (i = 0; i < config->array_count; i++) {
			ar = (struct mfi_array *)p;
			state.arrays[i] = ar->array_ref;
			p += config->array_size;
		}
		qsort(state.arrays, config->array_count, sizeof(int),
		    compare_int);
	} else
		state.arrays = NULL;
	state.log_drv_count = config->log_drv_count;
	if (config->log_drv_count) {
		state.volumes = calloc(config->log_drv_count, sizeof(int));
		if (state.volumes == NULL) {
			warnx("malloc failed");
			error = ENOMEM;
			goto error;
		}
		for (i = 0; i < config->log_drv_count; i++) {
			ld = (struct mfi_ld_config *)p;
			state.volumes[i] = ld->properties.ld.v.target_id;
			p += config->log_drv_size;
		}
		qsort(state.volumes, config->log_drv_count, sizeof(int),
		    compare_int);
	} else
		state.volumes = NULL;
	free(config);

	/* Determine the size of the configuration we will build. */
	switch (raid_type) {
	case RT_RAID0:
	case RT_RAID1:
	case RT_RAID5:
	case RT_RAID6:
	case RT_CONCAT:
	case RT_JBOD:
		/* Each volume spans a single array. */
		nvolumes = narrays;
		break;
	case RT_RAID10:
	case RT_RAID50:
	case RT_RAID60:
		/* A single volume spans multiple arrays. */
		nvolumes = 1;
		break;
	default:
		/* Pacify gcc. */
		abort();
	}

	config_size = sizeof(struct mfi_config_data) +
	    sizeof(struct mfi_ld_config) * nvolumes + MFI_ARRAY_SIZE * narrays;
	config = calloc(1, config_size);
	if (config == NULL) {
		warnx("malloc failed");
		error = ENOMEM;
		goto error;
	}
	config->size = config_size;
	config->array_count = narrays;
	config->array_size = MFI_ARRAY_SIZE;	/* XXX: Firmware hardcode */
	config->log_drv_count = nvolumes;
	config->log_drv_size = sizeof(struct mfi_ld_config);
	config->spares_count = 0;
	config->spares_size = 40;		/* XXX: Firmware hardcode */
	cfg_arrays = (char *)config->array;
	cfg_volumes = cfg_arrays + config->array_size * narrays;

	/* Build the arrays. */
	for (i = 0; i < narrays; i++) {
		build_array(fd, cfg_arrays, &arrays[i], &state, verbose);
		cfg_arrays += config->array_size;
	}

	/* Now build the volume(s). */
	arrays_per_volume = narrays / nvolumes;
	for (i = 0; i < nvolumes; i++) {
		build_volume(cfg_volumes, arrays_per_volume,
		    &arrays[i * arrays_per_volume], raid_type, stripe_size,
		    &state, verbose);
		cfg_volumes += config->log_drv_size;
	}

#ifdef DEBUG
	if (dump)
		dump_config(fd, config, NULL);
#endif

	/* Send the new config to the controller. */
	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_ADD, config, config_size,
	    NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to add volume");
		/* FALLTHROUGH */
	}

error:
	/* Clean up. */
	free(config);
	free(state.volumes);
	free(state.arrays);
	if (arrays != NULL) {
		for (i = 0; i < narrays; i++)
			free(arrays[i].drives);
		free(arrays);
	}
	close(fd);

	return (error);
}
MFI_COMMAND(top, create, create_volume);

static int
delete_volume(int ac, char **av)
{
	struct mfi_ld_info info;
	int error, fd;
	uint8_t target_id, mbox[4];

	/*
	 * Backwards compat.  Map 'delete volume' to 'delete' and
	 * 'delete spare' to 'remove'.
	 */
	if (ac > 1) {
		if (strcmp(av[1], "volume") == 0) {
			av++;
			ac--;
		} else if (strcmp(av[1], "spare") == 0) {
			av++;
			ac--;
			return (remove_spare(ac, av));
		}
	}

	if (ac != 2) {
		warnx("delete volume: volume required");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	if (!mfi_reconfig_supported()) {
		warnx("The current mfi(4) driver does not support "
		    "configuration changes.");
		close(fd);
		return (EOPNOTSUPP);
	}

	if (mfi_lookup_volume(fd, av[1], &target_id) < 0) {
		error = errno;
		warn("Invalid volume %s", av[1]);
		close(fd);
		return (error);
	}

	if (mfi_ld_get_info(fd, target_id, &info, NULL) < 0) {
		error = errno;
		warn("Failed to get info for volume %d", target_id);
		close(fd);
		return (error);
	}

	if (mfi_volume_busy(fd, target_id)) {
		warnx("Volume %s is busy and cannot be deleted",
		    mfi_volume_name(fd, target_id));
		close(fd);
		return (EBUSY);
	}

	mbox_store_ldref(mbox, &info.ld_config.properties.ld);
	if (mfi_dcmd_command(fd, MFI_DCMD_LD_DELETE, NULL, 0, mbox,
	    sizeof(mbox), NULL) < 0) {
		error = errno;
		warn("Failed to delete volume");
		close(fd);
		return (error);
	}

	close(fd);

	return (0);
}
MFI_COMMAND(top, delete, delete_volume);

static int
add_spare(int ac, char **av)
{
	struct mfi_pd_info info;
	struct mfi_config_data *config;
	struct mfi_array *ar;
	struct mfi_ld_config *ld;
	struct mfi_spare *spare;
	uint16_t device_id;
	uint8_t target_id;
	char *p;
	int error, fd, i;

	if (ac < 2) {
		warnx("add spare: drive required");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	config = NULL;
	spare = NULL;
	error = mfi_lookup_drive(fd, av[1], &device_id);
	if (error)
		goto error;

	if (mfi_pd_get_info(fd, device_id, &info, NULL) < 0) {
		error = errno;
		warn("Failed to fetch drive info");
		goto error;
	}

	if (info.fw_state != MFI_PD_STATE_UNCONFIGURED_GOOD) {
		warnx("Drive %u is not available", device_id);
		error = EINVAL;
		goto error;
	}

	if (ac > 2) {
		if (mfi_lookup_volume(fd, av[2], &target_id) < 0) {
			error = errno;
			warn("Invalid volume %s", av[2]);
			goto error;
		}
	}

	if (mfi_config_read(fd, &config) < 0) {
		error = errno;
		warn("Failed to read configuration");
		goto error;
	}

	spare = malloc(sizeof(struct mfi_spare) + sizeof(uint16_t) *
	    config->array_count);
	if (spare == NULL) {
		warnx("malloc failed");
		error = ENOMEM;
		goto error;
	}
	bzero(spare, sizeof(struct mfi_spare));
	spare->ref = info.ref;

	if (ac == 2) {
		/* Global spare backs all arrays. */
		p = (char *)config->array;
		for (i = 0; i < config->array_count; i++) {
			ar = (struct mfi_array *)p;
			if (ar->size > info.coerced_size) {
				warnx("Spare isn't large enough for array %u",
				    ar->array_ref);
				error = EINVAL;
				goto error;
			}
			p += config->array_size;
		}
		spare->array_count = 0;
	} else  {
		/*
		 * Dedicated spares only back the arrays for a
		 * specific volume.
		 */
		ld = mfi_config_lookup_volume(config, target_id);
		if (ld == NULL) {
			warnx("Did not find volume %d", target_id);
			error = EINVAL;
			goto error;
		}

		spare->spare_type |= MFI_SPARE_DEDICATED;
		spare->array_count = ld->params.span_depth;
		for (i = 0; i < ld->params.span_depth; i++) {
			ar = mfi_config_lookup_array(config,
			    ld->span[i].array_ref);
			if (ar == NULL) {
				warnx("Missing array; inconsistent config?");
				error = ENXIO;
				goto error;
			}
			if (ar->size > info.coerced_size) {
				warnx("Spare isn't large enough for array %u",
				    ar->array_ref);
				error = EINVAL;
				goto error;
			}				
			spare->array_ref[i] = ar->array_ref;
		}
	}

	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_MAKE_SPARE, spare,
	    sizeof(struct mfi_spare) + sizeof(uint16_t) * spare->array_count,
	    NULL, 0, NULL) < 0) {
		error = errno;
		warn("Failed to assign spare");
		/* FALLTHROUGH. */
	}

error:
	free(spare);
	free(config);
	close(fd);

	return (error);
}
MFI_COMMAND(top, add, add_spare);

static int
remove_spare(int ac, char **av)
{
	struct mfi_pd_info info;
	int error, fd;
	uint16_t device_id;
	uint8_t mbox[4];

	if (ac != 2) {
		warnx("remove spare: drive required");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	error = mfi_lookup_drive(fd, av[1], &device_id);
	if (error) {
		close(fd);
		return (error);
	}

	/* Get the info for this drive. */
	if (mfi_pd_get_info(fd, device_id, &info, NULL) < 0) {
		error = errno;
		warn("Failed to fetch info for drive %u", device_id);
		close(fd);
		return (error);
	}

	if (info.fw_state != MFI_PD_STATE_HOT_SPARE) {
		warnx("Drive %u is not a hot spare", device_id);
		close(fd);
		return (EINVAL);
	}

	mbox_store_pdref(mbox, &info.ref);
	if (mfi_dcmd_command(fd, MFI_DCMD_CFG_REMOVE_SPARE, NULL, 0, mbox,
	    sizeof(mbox), NULL) < 0) {
		error = errno;
		warn("Failed to delete spare");
		close(fd);
		return (error);
	}

	close(fd);

	return (0);
}
MFI_COMMAND(top, remove, remove_spare);

/* Display raw data about a config. */
void
dump_config(int fd, struct mfi_config_data *config, const char *msg_prefix)
{
	struct mfi_array *ar;
	struct mfi_ld_config *ld;
	struct mfi_spare *sp;
	struct mfi_pd_info pinfo;
	uint16_t device_id;
	char *p;
	int i, j;

	if (NULL == msg_prefix)
		msg_prefix = "Configuration (Debug)";

	printf(
	    "mfi%d %s: %d arrays, %d volumes, %d spares\n", mfi_unit,
	    msg_prefix, config->array_count, config->log_drv_count,
	    config->spares_count);
	printf("  array size: %u\n", config->array_size);
	printf("  volume size: %u\n", config->log_drv_size);
	printf("  spare size: %u\n", config->spares_size);
	p = (char *)config->array;

	for (i = 0; i < config->array_count; i++) {
		ar = (struct mfi_array *)p;
		printf("    array %u of %u drives:\n", ar->array_ref,
		    ar->num_drives);
		printf("      size = %ju\n", (uintmax_t)ar->size);
		for (j = 0; j < ar->num_drives; j++) {
			device_id = ar->pd[j].ref.v.device_id;
			if (device_id == 0xffff)
				printf("        drive MISSING\n");
			else {
				printf("        drive %u %s\n", device_id,
				    mfi_pdstate(ar->pd[j].fw_state));
				if (mfi_pd_get_info(fd, device_id, &pinfo,
				    NULL) >= 0) {
					printf("          raw size: %ju\n",
					    (uintmax_t)pinfo.raw_size);
					printf("          non-coerced size: %ju\n",
					    (uintmax_t)pinfo.non_coerced_size);
					printf("          coerced size: %ju\n",
					    (uintmax_t)pinfo.coerced_size);
				}
			}
		}
		p += config->array_size;
	}

	for (i = 0; i < config->log_drv_count; i++) {
		ld = (struct mfi_ld_config *)p;
		printf("    volume %s ",
		    mfi_volume_name(fd, ld->properties.ld.v.target_id));
		printf("%s %s",
		    mfi_raid_level(ld->params.primary_raid_level,
			ld->params.secondary_raid_level),
		    mfi_ldstate(ld->params.state));
		if (ld->properties.name[0] != '\0')
			printf(" <%s>", ld->properties.name);
		printf("\n");
		printf("      primary raid level: %u\n",
		    ld->params.primary_raid_level);
		printf("      raid level qualifier: %u\n",
		    ld->params.raid_level_qualifier);
		printf("      secondary raid level: %u\n",
		    ld->params.secondary_raid_level);
		printf("      stripe size: %u\n", ld->params.stripe_size);
		printf("      num drives: %u\n", ld->params.num_drives);
		printf("      init state: %u\n", ld->params.init_state);
		printf("      consistent: %u\n", ld->params.is_consistent);
		printf("      no bgi: %u\n", ld->properties.no_bgi);
		printf("      spans:\n");
		for (j = 0; j < ld->params.span_depth; j++) {
			printf("        array %u @ ", ld->span[j].array_ref);
			printf("%ju : %ju\n",
			    (uintmax_t)ld->span[j].start_block,
			    (uintmax_t)ld->span[j].num_blocks);
		}
		p += config->log_drv_size;
	}

	for (i = 0; i < config->spares_count; i++) {
		sp = (struct mfi_spare *)p;
		printf("    %s spare %u ",
		    sp->spare_type & MFI_SPARE_DEDICATED ? "dedicated" :
		    "global", sp->ref.v.device_id);
		printf("%s", mfi_pdstate(MFI_PD_STATE_HOT_SPARE));
		printf(" backs:\n");
		for (j = 0; j < sp->array_count; j++)
			printf("        array %u\n", sp->array_ref[j]);
		p += config->spares_size;
	}
}

#ifdef DEBUG
static int
debug_config(int ac, char **av)
{
	struct mfi_config_data *config;
	int error, fd;

	if (ac != 1) {
		warnx("debug: extra arguments");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	/* Get the config from the controller. */
	if (mfi_config_read(fd, &config) < 0) {
		error = errno;
		warn("Failed to get config");
		close(fd);
		return (error);
	}

	/* Dump out the configuration. */
	dump_config(fd, config, NULL);
	free(config);
	close(fd);

	return (0);
}
MFI_COMMAND(top, debug, debug_config);

static int
dump(int ac, char **av)
{
	struct mfi_config_data *config;
	char buf[64];
	size_t len;
	int error, fd;

	if (ac != 1) {
		warnx("dump: extra arguments");
		return (EINVAL);
	}

	fd = mfi_open(mfi_unit, O_RDWR);
	if (fd < 0) {
		error = errno;
		warn("mfi_open");
		return (error);
	}

	/* Get the stashed copy of the last dcmd from the driver. */
	snprintf(buf, sizeof(buf), "dev.mfi.%d.debug_command", mfi_unit);
	if (sysctlbyname(buf, NULL, &len, NULL, 0) < 0) {
		error = errno;
		warn("Failed to read debug command");
		if (error == ENOENT)
			error = EOPNOTSUPP;
		close(fd);
		return (error);
	}

	config = malloc(len);
	if (config == NULL) {
		warnx("malloc failed");
		close(fd);
		return (ENOMEM);
	}
	if (sysctlbyname(buf, config, &len, NULL, 0) < 0) {
		error = errno;
		warn("Failed to read debug command");
		free(config);
		close(fd);
		return (error);
	}
	dump_config(fd, config, NULL);
	free(config);
	close(fd);

	return (0);
}
MFI_COMMAND(top, dump, dump);
#endif
