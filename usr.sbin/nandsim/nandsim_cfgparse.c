/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2009-2012 Semihalf
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
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <dev/nand/nandsim.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include "nandsim_cfgparse.h"

#define warn(fmt, args...) do { \
    printf("WARNING: " fmt "\n", ##args); } while (0)

#define error(fmt, args...) do { \
    printf("ERROR: " fmt "\n", ##args); } while (0)

#define MSG_MANDATORYKEYMISSING "mandatory key \"%s\" value belonging to " \
    "section \"%s\" is missing!\n"

#define DEBUG
#undef DEBUG

#ifdef DEBUG
#define debug(fmt, args...) do { \
    printf("NANDSIM_CONF:" fmt "\n", ##args); } while (0)
#else
#define debug(fmt, args...) do {} while(0)
#endif

#define STRBUFSIZ 2000

/* Macros extracts type and type size */
#define TYPE(x) ((x) & 0xf8)
#define SIZE(x) (((x) & 0x07))

/* Erase/Prog/Read time max and min values */
#define DELAYTIME_MIN	10000
#define DELAYTIME_MAX	10000000

/* Structure holding configuration for controller. */
static struct sim_ctrl ctrl_conf;
/* Structure holding configuration for chip. */
static struct sim_chip chip_conf;

static struct nandsim_key nandsim_ctrl_keys[] = {
	{"num_cs", 1, VALUE_UINT | SIZE_8, (void *)&ctrl_conf.num_cs, 0},
	{"ctrl_num", 1, VALUE_UINT | SIZE_8, (void *)&ctrl_conf.num, 0},

	{"ecc_layout", 1, VALUE_UINTARRAY | SIZE_16,
	    (void *)&ctrl_conf.ecc_layout, MAX_ECC_BYTES},

	{"filename", 0, VALUE_STRING,
	    (void *)&ctrl_conf.filename, FILENAME_SIZE},

	{"ecc", 0, VALUE_BOOL, (void *)&ctrl_conf.ecc, 0},
	{NULL, 0, 0, NULL, 0},
};

static struct nandsim_key nandsim_chip_keys[] = {
	{"chip_cs", 1, VALUE_UINT | SIZE_8, (void *)&chip_conf.num, 0},
	{"chip_ctrl", 1, VALUE_UINT | SIZE_8, (void *)&chip_conf.ctrl_num,
	    0},
	{"device_id", 1, VALUE_UINT | SIZE_8, (void *)&chip_conf.device_id,
	    0},
	{"manufacturer_id", 1, VALUE_UINT | SIZE_8,
	    (void *)&chip_conf.manufact_id, 0},
	{"model", 0, VALUE_STRING, (void *)&chip_conf.device_model,
	    DEV_MODEL_STR_SIZE},
	{"manufacturer", 0, VALUE_STRING, (void *)&chip_conf.manufacturer,
	    MAN_STR_SIZE},
	{"page_size", 1, VALUE_UINT | SIZE_32, (void *)&chip_conf.page_size,
	    0},
	{"oob_size", 1, VALUE_UINT | SIZE_32, (void *)&chip_conf.oob_size,
	    0},
	{"pages_per_block", 1, VALUE_UINT | SIZE_32,
	    (void *)&chip_conf.pgs_per_blk, 0},
	{"blocks_per_lun", 1, VALUE_UINT | SIZE_32,
	    (void *)&chip_conf.blks_per_lun, 0},
	{"luns", 1, VALUE_UINT | SIZE_32, (void *)&chip_conf.luns, 0},
	{"column_addr_cycle", 1,VALUE_UINT | SIZE_8,
	    (void *)&chip_conf.col_addr_cycles, 0},
	{"row_addr_cycle", 1, VALUE_UINT | SIZE_8,
	    (void *)&chip_conf.row_addr_cycles, 0},
	{"program_time", 0, VALUE_UINT | SIZE_32,
	    (void *)&chip_conf.prog_time, 0},
	{"erase_time", 0, VALUE_UINT | SIZE_32,
	    (void *)&chip_conf.erase_time, 0},
	{"read_time", 0, VALUE_UINT | SIZE_32,
	    (void *)&chip_conf.read_time, 0},
	{"width", 1, VALUE_UINT | SIZE_8, (void *)&chip_conf.width, 0},
	{"wear_out", 1, VALUE_UINT | SIZE_32, (void *)&chip_conf.wear_level,
	    0},
	{"bad_block_map", 0, VALUE_UINTARRAY | SIZE_32,
	    (void *)&chip_conf.bad_block_map, MAX_BAD_BLOCKS},
	{NULL, 0, 0, NULL, 0},
};

static struct nandsim_section sections[] = {
	{"ctrl", (struct nandsim_key *)&nandsim_ctrl_keys},
	{"chip", (struct nandsim_key *)&nandsim_chip_keys},
	{NULL, NULL},
};

static uint8_t logoutputtoint(char *, int *);
static uint8_t validate_chips(struct sim_chip *, int, struct sim_ctrl *, int);
static uint8_t validate_ctrls(struct sim_ctrl *, int);
static int configure_sim(const char *, struct rcfile *);
static int create_ctrls(struct rcfile *, struct sim_ctrl **, int *);
static int create_chips(struct rcfile *, struct sim_chip **, int *);
static void destroy_ctrls(struct sim_ctrl *);
static void destroy_chips(struct sim_chip *);
static int validate_section_config(struct rcfile *, const char *, int);

int
convert_argint(char *arg, int *value)
{

	if (arg == NULL || value == NULL)
		return (EINVAL);

	errno = 0;
	*value = (int)strtol(arg, NULL, 0);
	if (*value == 0 && errno != 0) {
		error("Cannot convert to number argument \'%s\'", arg);
		return (EINVAL);
	}
	return (0);
}

int
convert_arguint(char *arg, unsigned int *value)
{

	if (arg == NULL || value == NULL)
		return (EINVAL);

	errno = 0;
	*value = (unsigned int)strtol(arg, NULL, 0);
	if (*value == 0 && errno != 0) {
		error("Cannot convert to number argument \'%s\'", arg);
		return (EINVAL);
	}
	return (0);
}

/* Parse given ',' separated list of bytes into buffer. */
int
parse_intarray(char *array, int **buffer)
{
	char *tmp, *tmpstr, *origstr;
	unsigned int currbufp = 0, i;
	unsigned int count = 0, from  = 0, to = 0;

	/* Remove square braces */
	if (array[0] == '[')
		array ++;
	if (array[strlen(array)-1] == ']')
		array[strlen(array)-1] = ',';

	from = strlen(array);
	origstr = (char *)malloc(sizeof(char) * from);
	strcpy(origstr, array);

	tmpstr = (char *)strtok(array, ",");
	/* First loop checks for how big int array we need to allocate */
	while (tmpstr != NULL) {
		errno = 0;
		if ((tmp = strchr(tmpstr, '-')) != NULL) {
			*tmp = ' ';
			if (convert_arguint(tmpstr, &from) ||
			    convert_arguint(tmp, &to)) {
				free(origstr);
				return (EINVAL);
			}

			count += to - from + 1;
		} else {
			if (convert_arguint(tmpstr, &from)) {
				free(origstr);
				return (EINVAL);
			}
			count++;
		}
		tmpstr = (char *)strtok(NULL, ",");
	}

	if (count == 0)
		goto out;

	/* Allocate buffer of ints */
	tmpstr = (char *)strtok(origstr, ",");
	*buffer = malloc(count * sizeof(int));

	/* Second loop is just inserting converted values into int array */
	while (tmpstr != NULL) {
		errno = 0;
		if ((tmp = strchr(tmpstr, '-')) != NULL) {
			*tmp = ' ';
			from = strtol(tmpstr, NULL, 0);
			to = strtol(tmp, NULL, 0);
			tmpstr = strtok(NULL, ",");
			for (i = from; i <= to; i ++)
				(*buffer)[currbufp++] = i;
			continue;
		}
		errno = 0;
		from = (int)strtol(tmpstr, NULL, 0);
		(*buffer)[currbufp++] = from;
		tmpstr = (char *)strtok(NULL, ",");
	}
out:
	free(origstr);
	return (count);
}

/* Convert logoutput strings literals into appropriate ints. */
static uint8_t
logoutputtoint(char *logoutput, int *output)
{
	int out;

	if (strcmp(logoutput, "file") == 0)
		out = NANDSIM_OUTPUT_FILE;

	else if (strcmp(logoutput, "console") == 0)
		out = NANDSIM_OUTPUT_CONSOLE;

	else if (strcmp(logoutput, "ram") == 0)
		out = NANDSIM_OUTPUT_RAM;

	else if (strcmp(logoutput, "none") == 0)
		out = NANDSIM_OUTPUT_NONE;
	else
		out = -1;

	*output = out;

	if (out == -1)
		return (EINVAL);
	else
		return (0);
}

static int
configure_sim(const char *devfname, struct rcfile *f)
{
	struct sim_param sim_conf;
	char buf[255];
	int err, tmpv, fd;

	err = rc_getint(f, "sim", 0, "log_level", &tmpv);

	if (tmpv < 0 || tmpv > 255 || err) {
		error("Bad log level specified (%d)\n", tmpv);
		return (ENOTSUP);
	} else
		sim_conf.log_level = tmpv;

	rc_getstring(f, "sim", 0, "log_output", 255, (char *)&buf);

	tmpv = -1;
	err = logoutputtoint((char *)&buf, &tmpv);
	if (err) {
		error("Log output specified in config file does not seem to "
		    "be valid (%s)!", (char *)&buf);
		return (ENOTSUP);
	}

	sim_conf.log_output = tmpv;

	fd = open(devfname, O_RDWR);
	if (fd == -1) {
		error("could not open simulator device file (%s)!",
		    devfname);
		return (EX_OSFILE);
	}

	err = ioctl(fd, NANDSIM_SIM_PARAM, &sim_conf);
	if (err) {
		error("simulator parameters could not be modified: %s",
		    strerror(errno));
		close(fd);
		return (ENXIO);
	}

	close(fd);
	return (EX_OK);
}

static int
create_ctrls(struct rcfile *f, struct sim_ctrl **ctrls, int *cnt)
{
	int count, i;
	struct sim_ctrl *ctrlsptr;

	count = rc_getsectionscount(f, "ctrl");
	if (count > MAX_SIM_DEV) {
		error("Too many CTRL sections specified(%d)", count);
		return (ENOTSUP);
	} else if (count == 0) {
		error("No ctrl sections specified");
		return (ENOENT);
	}

	ctrlsptr = (struct sim_ctrl *)malloc(sizeof(struct sim_ctrl) * count);
	if (ctrlsptr == NULL) {
		error("Could not allocate memory for ctrl configuration");
		return (ENOMEM);
	}

	for (i = 0; i < count; i++) {
		bzero((void *)&ctrl_conf, sizeof(ctrl_conf));

		/*
		 * ECC layout have to end up with 0xffff, so
		 * we're filling buffer with 0xff. If ecc_layout is
		 * defined in config file, values will be overridden.
		 */
		memset((void *)&ctrl_conf.ecc_layout, 0xff,
		    sizeof(ctrl_conf.ecc_layout));

		if (validate_section_config(f, "ctrl", i) != 0) {
			free(ctrlsptr);
			return (EINVAL);
		}

		if (parse_section(f, "ctrl", i) != 0) {
			free(ctrlsptr);
			return (EINVAL);
		}

		memcpy(&ctrlsptr[i], &ctrl_conf, sizeof(ctrl_conf));
		/* Try to create ctrl with config parsed */
		debug("NUM=%d\nNUM_CS=%d\nECC=%d\nFILENAME=%s\nECC_LAYOUT[0]"
		    "=%d\nECC_LAYOUT[1]=%d\n\n",
		    ctrlsptr[i].num, ctrlsptr[i].num_cs, ctrlsptr[i].ecc,
		    ctrlsptr[i].filename, ctrlsptr[i].ecc_layout[0],
		    ctrlsptr[i].ecc_layout[1]);
	}
	*cnt = count;
	*ctrls = ctrlsptr;
	return (0);
}

static void
destroy_ctrls(struct sim_ctrl *ctrls)
{

	free(ctrls);
}

static int
create_chips(struct rcfile *f, struct sim_chip **chips, int *cnt)
{
	struct sim_chip *chipsptr;
	int count, i;

	count = rc_getsectionscount(f, "chip");
	if (count > (MAX_CTRL_CS * MAX_SIM_DEV)) {
		error("Too many chip sections specified(%d)", count);
		return (ENOTSUP);
	} else if (count == 0) {
		error("No chip sections specified");
		return (ENOENT);
	}

	chipsptr = (struct sim_chip *)malloc(sizeof(struct sim_chip) * count);
	if (chipsptr == NULL) {
		error("Could not allocate memory for chip configuration");
		return (ENOMEM);
	}

	for (i = 0; i < count; i++) {
		bzero((void *)&chip_conf, sizeof(chip_conf));

		/*
		 * Bad block map have to end up with 0xffff, so
		 * we're filling array with 0xff. If bad block map is
		 * defined in config file, values will be overridden.
		 */
		memset((void *)&chip_conf.bad_block_map, 0xff,
		    sizeof(chip_conf.bad_block_map));

		if (validate_section_config(f, "chip", i) != 0) {
			free(chipsptr);
			return (EINVAL);
		}

		if (parse_section(f, "chip", i) != 0) {
			free(chipsptr);
			return (EINVAL);
		}

		memcpy(&chipsptr[i], &chip_conf, sizeof(chip_conf));

		/* Try to create chip with config parsed */
		debug("CHIP:\nNUM=%d\nCTRL_NUM=%d\nDEVID=%d\nMANID=%d\n"
		    "PAGE_SZ=%d\nOOBSZ=%d\nREAD_T=%d\nDEVMODEL=%s\n"
		    "MAN=%s\nCOLADDRCYCLES=%d\nROWADDRCYCLES=%d\nCHWIDTH=%d\n"
		    "PGS/BLK=%d\nBLK/LUN=%d\nLUNS=%d\nERR_RATIO=%d\n"
		    "WEARLEVEL=%d\nISWP=%d\n\n\n\n",
		    chipsptr[i].num, chipsptr[i].ctrl_num,
		    chipsptr[i].device_id, chipsptr[i].manufact_id,
		    chipsptr[i].page_size, chipsptr[i].oob_size,
		    chipsptr[i].read_time, chipsptr[i].device_model,
		    chipsptr[i].manufacturer, chipsptr[i].col_addr_cycles,
		    chipsptr[i].row_addr_cycles, chipsptr[i].width,
		    chipsptr[i].pgs_per_blk, chipsptr[i].blks_per_lun,
		    chipsptr[i].luns, chipsptr[i].error_ratio,
		    chipsptr[i].wear_level, chipsptr[i].is_wp);
	}
	*cnt = count;
	*chips = chipsptr;
	return (0);
}

static void
destroy_chips(struct sim_chip *chips)
{

	free(chips);
}

int
parse_config(char *cfgfname, const char *devfname)
{
	int err = 0, fd;
	unsigned int chipsectionscnt, ctrlsectionscnt, i;
	struct rcfile *f;
	struct sim_chip *chips;
	struct sim_ctrl *ctrls;

	err = rc_open(cfgfname, "r", &f);
	if (err) {
		error("could not open configuration file (%s)", cfgfname);
		return (EX_NOINPUT);
	}

	/* First, try to configure simulator itself. */
	if (configure_sim(devfname, f) != EX_OK) {
		rc_close(f);
		return (EINVAL);
	}

	debug("SIM CONFIGURED!\n");
	/* Then create controllers' configs */
	if (create_ctrls(f, &ctrls, &ctrlsectionscnt) != 0) {
		rc_close(f);
		return (ENXIO);
	}
	debug("CTRLS CONFIG READ!\n");

	/* Then create chips' configs */
	if (create_chips(f, &chips, &chipsectionscnt) != 0) {
		destroy_ctrls(ctrls);
		rc_close(f);
		return (ENXIO);
	}
	debug("CHIPS CONFIG READ!\n");

	if (validate_ctrls(ctrls, ctrlsectionscnt) != 0) {
		destroy_ctrls(ctrls);
		destroy_chips(chips);
		rc_close(f);
		return (EX_SOFTWARE);
	}
	if (validate_chips(chips, chipsectionscnt, ctrls,
	    ctrlsectionscnt) != 0) {
		destroy_ctrls(ctrls);
		destroy_chips(chips);
		rc_close(f);
		return (EX_SOFTWARE);
	}

	/* Open device */
	fd = open(devfname, O_RDWR);
	if (fd == -1) {
		error("could not open simulator device file (%s)!",
		    devfname);
		rc_close(f);
		destroy_chips(chips);
		destroy_ctrls(ctrls);
		return (EX_OSFILE);
	}

	debug("SIM CONFIG STARTED!\n");

	/* At this stage, both ctrls' and chips' configs should be valid */
	for (i = 0; i < ctrlsectionscnt; i++) {
		err = ioctl(fd, NANDSIM_CREATE_CTRL, &ctrls[i]);
		if (err) {
			if (err == EEXIST)
				error("Controller#%d already created\n",
				    ctrls[i].num);
			else if (err == EINVAL)
				error("Incorrect controller number (%d)\n",
				    ctrls[i].num);
			else
				error("Could not created controller#%d\n",
				    ctrls[i].num);
			/* Errors during controller creation stops parsing */
			close(fd);
			rc_close(f);
			destroy_ctrls(ctrls);
			destroy_chips(chips);
			return (ENXIO);
		}
		debug("CTRL#%d CONFIG STARTED!\n", i);
	}

	for (i = 0; i < chipsectionscnt; i++) {
		err = ioctl(fd, NANDSIM_CREATE_CHIP, &chips[i]);
		if (err) {
			if (err == EEXIST)
				error("Chip#%d for controller#%d already "
				    "created\n", chips[i].num,
				    chips[i].ctrl_num);
			else if (err == EINVAL)
				error("Incorrect chip number (%d:%d)\n",
				    chips[i].num, chips[i].ctrl_num);
			else
				error("Could not create chip (%d:%d)\n",
				    chips[i].num, chips[i].ctrl_num);
			error("Could not start chip#%d\n", i);
			destroy_chips(chips);
			destroy_ctrls(ctrls);
			close(fd);
			rc_close(f);
			return (ENXIO);
		}
	}
	debug("CHIPS CONFIG STARTED!\n");

	close(fd);
	rc_close(f);
	destroy_chips(chips);
	destroy_ctrls(ctrls);
	return (0);
}

/*
 * Function tries to get appropriate value for given key, convert it to
 * array of ints (of given size), and perform all the necessary checks and
 * conversions.
 */
static int
get_argument_intarray(const char *sect_name, int sectno,
    struct nandsim_key *key, struct rcfile *f)
{
	char strbuf[STRBUFSIZ];
	int *intbuf;
	int getres;
	uint32_t cnt, i = 0;

	getres = rc_getstring(f, sect_name, sectno, key->keyname, STRBUFSIZ,
	    (char *)&strbuf);

	if (getres != 0) {
		if (key->mandatory != 0) {
			error(MSG_MANDATORYKEYMISSING, key->keyname,
			    sect_name);
			return (EINVAL);
		} else
			/* Non-mandatory key, not present -- skip */
			return (0);
	}
	cnt = parse_intarray((char *)&strbuf, &intbuf);
	cnt = (cnt <= key->maxlength) ? cnt : key->maxlength;

	for (i = 0; i < cnt; i++) {
		if (SIZE(key->valuetype) == SIZE_8)
			*((uint8_t *)(key->field) + i) =
			    (uint8_t)intbuf[i];
		else if (SIZE(key->valuetype) == SIZE_16)
			*((uint16_t *)(key->field) + i) =
			    (uint16_t)intbuf[i];
		else
			*((uint32_t *)(key->field) + i) =
			    (uint32_t)intbuf[i];
	}
	free(intbuf);
	return (0);
}

/*
 *  Function tries to get appropriate value for given key, convert it to
 *  int of certain length.
 */
static int
get_argument_int(const char *sect_name, int sectno, struct nandsim_key *key,
    struct rcfile *f)
{
	int getres;
	uint32_t val;

	getres = rc_getint(f, sect_name, sectno, key->keyname, &val);
	if (getres != 0) {

		if (key->mandatory != 0) {
			error(MSG_MANDATORYKEYMISSING, key->keyname,
			    sect_name);

			return (EINVAL);
		} else
			/* Non-mandatory key, not present -- skip */
			return (0);
	}
	if (SIZE(key->valuetype) == SIZE_8)
		*(uint8_t *)(key->field) = (uint8_t)val;
	else if (SIZE(key->valuetype) == SIZE_16)
		*(uint16_t *)(key->field) = (uint16_t)val;
	else
		*(uint32_t *)(key->field) = (uint32_t)val;
	return (0);
}

/* Function tries to get string value for given key */
static int
get_argument_string(const char *sect_name, int sectno,
    struct nandsim_key *key, struct rcfile *f)
{
	char strbuf[STRBUFSIZ];
	int getres;

	getres = rc_getstring(f, sect_name, sectno, key->keyname, STRBUFSIZ,
	    strbuf);

	if (getres != 0) {
		if (key->mandatory != 0) {
			error(MSG_MANDATORYKEYMISSING, key->keyname,
			    sect_name);
			return (1);
		} else
			/* Non-mandatory key, not present -- skip */
			return (0);
	}
	strncpy(key->field, (char *)&strbuf, (size_t)(key->maxlength - 1));
	return (0);
}

/* Function tries to get on/off value for given key */
static int
get_argument_bool(const char *sect_name, int sectno, struct nandsim_key *key,
    struct rcfile *f)
{
	int getres, val;

	getres = rc_getbool(f, sect_name, sectno, key->keyname, &val);
	if (getres != 0) {
		if (key->mandatory != 0) {
			error(MSG_MANDATORYKEYMISSING, key->keyname,
			    sect_name);
			return (1);
		} else
			/* Non-mandatory key, not present -- skip */
			return (0);
	}
	*(uint8_t *)key->field = (uint8_t)val;
	return (0);
}

int
parse_section(struct rcfile *f, const char *sect_name, int sectno)
{
	struct nandsim_key *key;
	struct nandsim_section *sect = (struct nandsim_section *)&sections;
	int getres = 0;

	while (1) {
		if (sect == NULL)
			return (EINVAL);

		if (strcmp(sect->name, sect_name) == 0)
			break;
		else
			sect++;
	}
	key = sect->keys;
	do {
		debug("->Section: %s, Key: %s, type: %d, size: %d",
		    sect_name, key->keyname, TYPE(key->valuetype),
		    SIZE(key->valuetype)/2);

		switch (TYPE(key->valuetype)) {
		case VALUE_UINT:
			/* Single int value */
			getres = get_argument_int(sect_name, sectno, key, f);

			if (getres != 0)
				return (getres);

			break;
		case VALUE_UINTARRAY:
			/* Array of ints */
			getres = get_argument_intarray(sect_name,
			    sectno, key, f);

			if (getres != 0)
				return (getres);

			break;
		case VALUE_STRING:
			/* Array of chars */
			getres = get_argument_string(sect_name, sectno, key,
			    f);

			if (getres != 0)
				return (getres);

			break;
		case VALUE_BOOL:
			/* Boolean value (true/false/on/off/yes/no) */
			getres = get_argument_bool(sect_name, sectno, key,
			    f);

			if (getres != 0)
				return (getres);

			break;
		}
	} while ((++key)->keyname != NULL);

	return (0);
}

static uint8_t
validate_chips(struct sim_chip *chips, int chipcnt,
    struct sim_ctrl *ctrls, int ctrlcnt)
{
	int cchipcnt, i, width, j, id, max;

	cchipcnt = chipcnt;
	for (chipcnt -= 1; chipcnt >= 0; chipcnt--) {
		if (chips[chipcnt].num >= MAX_CTRL_CS) {
			error("chip no. too high (%d)!!\n",
			    chips[chipcnt].num);
			return (EINVAL);
		}

		if (chips[chipcnt].ctrl_num >= MAX_SIM_DEV) {
			error("controller no. too high (%d)!!\n",
			    chips[chipcnt].ctrl_num);
			return (EINVAL);
		}

		if (chips[chipcnt].width != 8 &&
		    chips[chipcnt].width != 16) {
			error("invalid width:%d for chip#%d",
			    chips[chipcnt].width, chips[chipcnt].num);
			return (EINVAL);
		}

		/* Check if page size is > 512 and if its power of 2 */
		if (chips[chipcnt].page_size < 512 ||
		    (chips[chipcnt].page_size &
		    (chips[chipcnt].page_size - 1)) != 0) {
			error("invalid page size:%d for chip#%d at ctrl#%d!!"
			    "\n", chips[chipcnt].page_size,
			    chips[chipcnt].num,
			    chips[chipcnt].ctrl_num);
			return (EINVAL);
		}

		/* Check if controller no. ctrl_num is configured */
		for (i = 0, id = -1; i < ctrlcnt && id == -1; i++)
			if (ctrls[i].num == chips[chipcnt].ctrl_num)
				id = i;

		if (i == ctrlcnt && id == -1) {
			error("Missing configuration for controller %d"
			    " (at least one chip is connected to it)",
			    chips[chipcnt].ctrl_num);
			return (EINVAL);
		} else {
			/*
			 * Controller is configured -> check oob_size
			 * validity
			 */
			i = 0;
			max = ctrls[id].ecc_layout[0];
			while (i < MAX_ECC_BYTES &&
			    ctrls[id].ecc_layout[i] != 0xffff) {

				if (ctrls[id].ecc_layout[i] > max)
					max = ctrls[id].ecc_layout[i];
				i++;
			}

			if (chips[chipcnt].oob_size < (unsigned)i) {
				error("OOB size for chip#%d at ctrl#%d is "
				    "smaller than ecc layout length!",
				    chips[chipcnt].num,
				    chips[chipcnt].ctrl_num);
				exit(EINVAL);
			}

			if (chips[chipcnt].oob_size < (unsigned)max) {
				error("OOB size for chip#%d at ctrl#%d is "
				    "smaller than maximal ecc position in "
				    "defined layout!", chips[chipcnt].num,
				    chips[chipcnt].ctrl_num);
				exit(EINVAL);
			}


		}

		if ((chips[chipcnt].erase_time < DELAYTIME_MIN ||
		    chips[chipcnt].erase_time > DELAYTIME_MAX) &&
		    chips[chipcnt].erase_time != 0) {
			error("Invalid erase time value for chip#%d at "
			    "ctrl#%d",
			    chips[chipcnt].num,
			    chips[chipcnt].ctrl_num);
			return (EINVAL);
		}

		if ((chips[chipcnt].prog_time < DELAYTIME_MIN ||
		    chips[chipcnt].prog_time > DELAYTIME_MAX) &&
		    chips[chipcnt].prog_time != 0) {
			error("Invalid prog time value for chip#%d at "
			    "ctr#%d!",
			    chips[chipcnt].num,
			    chips[chipcnt].ctrl_num);
			return (EINVAL);
		}

		if ((chips[chipcnt].read_time < DELAYTIME_MIN ||
		    chips[chipcnt].read_time > DELAYTIME_MAX) &&
		    chips[chipcnt].read_time != 0) {
			error("Invalid read time value for chip#%d at "
			    "ctrl#%d!",
			    chips[chipcnt].num,
			    chips[chipcnt].ctrl_num);
			return (EINVAL);
		}
	}
	/* Check if chips attached to the same controller, have same width */
	for (i = 0; i < ctrlcnt; i++) {
		width = -1;
		for (j = 0; j < cchipcnt; j++) {
			if (chips[j].ctrl_num == i) {
				if (width == -1) {
					width = chips[j].width;
				} else {
					if (width != chips[j].width) {
						error("Chips attached to "
						    "ctrl#%d have different "
						    "widths!\n", i);
						return (EINVAL);
					}
				}
			}
		}
	}

	return (0);
}

static uint8_t
validate_ctrls(struct sim_ctrl *ctrl, int ctrlcnt)
{
	for (ctrlcnt -= 1; ctrlcnt >= 0; ctrlcnt--) {
		if (ctrl[ctrlcnt].num > MAX_SIM_DEV) {
			error("Controller no. too high (%d)!!\n",
			    ctrl[ctrlcnt].num);
			return (EINVAL);
		}
		if (ctrl[ctrlcnt].num_cs > MAX_CTRL_CS) {
			error("Too many CS (%d)!!\n", ctrl[ctrlcnt].num_cs);
			return (EINVAL);
		}
		if (ctrl[ctrlcnt].ecc != 0 && ctrl[ctrlcnt].ecc != 1) {
			error("ECC is set to neither 0 nor 1 !\n");
			return (EINVAL);
		}
	}

	return (0);
}

static int validate_section_config(struct rcfile *f, const char *sect_name,
    int sectno)
{
	struct nandsim_key *key;
	struct nandsim_section *sect;
	char **keys_tbl;
	int i, match;

	for (match = 0, sect = (struct nandsim_section *)&sections;
	    sect != NULL; sect++) {
		if (strcmp(sect->name, sect_name) == 0) {
			match = 1;
			break;
		}
	}

	if (match == 0)
		return (EINVAL);

	keys_tbl = rc_getkeys(f, sect_name, sectno);
	if (keys_tbl == NULL)
		return (ENOMEM);

	for (i = 0; keys_tbl[i] != NULL; i++) {
		key = sect->keys;
		match = 0;
		do {
			if (strcmp(keys_tbl[i], key->keyname) == 0) {
				match = 1;
				break;
			}
		} while ((++key)->keyname != NULL);

		if (match == 0) {
			error("Invalid key in config file: %s\n", keys_tbl[i]);
			free(keys_tbl);
			return (EINVAL);
		}
	}

	free(keys_tbl);
	return (0);
}
