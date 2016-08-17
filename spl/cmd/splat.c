/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting LAyer Tests (SPLAT) User Space Interface.
\*****************************************************************************/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "splat.h"

#undef ioctl

static const char shortOpts[] = "hvlat:xc";
static const struct option longOpts[] = {
	{ "help",            no_argument,       0, 'h' },
	{ "verbose",         no_argument,       0, 'v' },
	{ "list",            no_argument,       0, 'l' },
	{ "all",             no_argument,       0, 'a' },
	{ "test",            required_argument, 0, 't' },
	{ "exit",            no_argument,       0, 'x' },
	{ "nocolor",         no_argument,       0, 'c' },
	{ 0,                 0,                 0, 0   }
};

#define VERSION_SIZE	64

static List subsystems;				/* Subsystem/tests */
static int splatctl_fd;				/* Control file descriptor */
static char splat_version[VERSION_SIZE];	/* Kernel version string */
static char *splat_buffer = NULL;		/* Scratch space area */
static int splat_buffer_size = 0;		/* Scratch space size */


static void test_list(List, int);
static int dev_clear(void);
static void subsystem_fini(subsystem_t *);
static void test_fini(test_t *);


static int usage(void) {
	fprintf(stderr, "usage: splat [hvla] [-t <subsystem:<tests>>]\n");
	fprintf(stderr,
	"  --help      -h               This help\n"
	"  --verbose   -v               Increase verbosity\n"
	"  --list      -l               List all tests in all subsystems\n"
	"  --all       -a               Run all tests in all subsystems\n"
	"  --test      -t <sub:test>    Run 'test' in subsystem 'sub'\n"
	"  --exit      -x               Exit on first test error\n"
	"  --nocolor   -c               Do not colorize output\n");
	fprintf(stderr, "\n"
	"Examples:\n"
	"  splat -t kmem:all     # Runs all kmem tests\n"
	"  splat -t taskq:0x201  # Run taskq test 0x201\n");

	return 0;
}

static subsystem_t *subsystem_init(splat_user_t *desc)
{
	subsystem_t *sub;

	sub = (subsystem_t *)malloc(sizeof(*sub));
	if (sub == NULL)
		return NULL;

	memcpy(&sub->sub_desc, desc, sizeof(*desc));

	sub->sub_tests = list_create((ListDelF)test_fini);
	if (sub->sub_tests == NULL) {
		free(sub);
		return NULL;
	}

	return sub;
}

static void subsystem_fini(subsystem_t *sub)
{
	assert(sub != NULL);
	free(sub);
}

static int subsystem_setup(void)
{
	splat_cfg_t *cfg;
	int i, rc, size, cfg_size;
	subsystem_t *sub;
	splat_user_t *desc;

	/* Aquire the number of registered subsystems */
	cfg_size = sizeof(*cfg);
	cfg = (splat_cfg_t *)malloc(cfg_size);
	if (cfg == NULL)
		return -ENOMEM;

	memset(cfg, 0, cfg_size);
	cfg->cfg_magic = SPLAT_CFG_MAGIC;
        cfg->cfg_cmd   = SPLAT_CFG_SUBSYSTEM_COUNT;

	rc = ioctl(splatctl_fd, SPLAT_CFG, cfg);
	if (rc) {
		fprintf(stderr, "Ioctl() error 0x%lx / %d: %d\n",
		        (unsigned long)SPLAT_CFG, cfg->cfg_cmd, errno);
		free(cfg);
		return rc;
	}

	size = cfg->cfg_rc1;
	free(cfg);

	/* Based on the newly acquired number of subsystems allocate
	 * memory to get the descriptive information for them all. */
	cfg_size = sizeof(*cfg) + size * sizeof(splat_user_t);
	cfg = (splat_cfg_t *)malloc(cfg_size);
	if (cfg == NULL)
		return -ENOMEM;

	memset(cfg, 0, cfg_size);
	cfg->cfg_magic = SPLAT_CFG_MAGIC;
	cfg->cfg_cmd   = SPLAT_CFG_SUBSYSTEM_LIST;
	cfg->cfg_data.splat_subsystems.size = size;

	rc = ioctl(splatctl_fd, SPLAT_CFG, cfg);
	if (rc) {
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		        (unsigned long) SPLAT_CFG, cfg->cfg_cmd, errno);
		free(cfg);
		return rc;
	}

	/* Add the new subsystems in to the global list */
	size = cfg->cfg_rc1;
	for (i = 0; i < size; i++) {
		desc = &(cfg->cfg_data.splat_subsystems.descs[i]);

		sub = subsystem_init(desc);
		if (sub == NULL) {
			fprintf(stderr, "Error initializing subsystem: %s\n",
			        desc->name);
			free(cfg);
			return -ENOMEM;
		}

		list_append(subsystems, sub);
	}

	free(cfg);
	return 0;
}

static void subsystem_list(List l, int indent)
{
	ListIterator i;
	subsystem_t *sub;

	fprintf(stdout,
	        "------------------------------ "
	        "Available SPLAT Tests "
	        "------------------------------\n");

	i = list_iterator_create(l);

	while ((sub = list_next(i))) {
		fprintf(stdout, "%*s0x%0*x %-*s ---- %s ----\n",
		        indent, "",
		        4, sub->sub_desc.id,
		        SPLAT_NAME_SIZE + 7, sub->sub_desc.name,
		        sub->sub_desc.desc);
		test_list(sub->sub_tests, indent + 7);
	}

	list_iterator_destroy(i);
}

static test_t *test_init(subsystem_t *sub, splat_user_t *desc)
{
	test_t *test;

	test = (test_t *)malloc(sizeof(*test));
	if (test == NULL)
		return NULL;

	test->test_sub = sub;
	memcpy(&test->test_desc, desc, sizeof(*desc));

	return test;
}

static void test_fini(test_t *test)
{
	assert(test != NULL);
	free(test);
}

static int test_setup(subsystem_t *sub)
{
	splat_cfg_t *cfg;
	int i, rc, size;
	test_t *test;
	splat_user_t *desc;

	/* Aquire the number of registered tests for the give subsystem */
	cfg = (splat_cfg_t *)malloc(sizeof(*cfg));
	if (cfg == NULL)
		return -ENOMEM;

	memset(cfg, 0, sizeof(*cfg));
	cfg->cfg_magic = SPLAT_CFG_MAGIC;
        cfg->cfg_cmd   = SPLAT_CFG_TEST_COUNT;
	cfg->cfg_arg1  = sub->sub_desc.id; /* Subsystem of interest */

	rc = ioctl(splatctl_fd, SPLAT_CFG, cfg);
	if (rc) {
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		        (unsigned long) SPLAT_CFG, cfg->cfg_cmd, errno);
		free(cfg);
		return rc;
	}

	size = cfg->cfg_rc1;
	free(cfg);

	/* Based on the newly aquired number of tests allocate enough
	 * memory to get the descriptive information for them all. */
	cfg = (splat_cfg_t *)malloc(sizeof(*cfg) + size*sizeof(splat_user_t));
	if (cfg == NULL)
		return -ENOMEM;

	memset(cfg, 0, sizeof(*cfg) + size * sizeof(splat_user_t));
	cfg->cfg_magic = SPLAT_CFG_MAGIC;
	cfg->cfg_cmd   = SPLAT_CFG_TEST_LIST;
	cfg->cfg_arg1  = sub->sub_desc.id; /* Subsystem of interest */
	cfg->cfg_data.splat_tests.size = size;

	rc = ioctl(splatctl_fd, SPLAT_CFG, cfg);
	if (rc) {
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		        (unsigned long) SPLAT_CFG, cfg->cfg_cmd, errno);
		free(cfg);
		return rc;
	}

	/* Add the new tests in to the relevant subsystems */
	size = cfg->cfg_rc1;
	for (i = 0; i < size; i++) {
		desc = &(cfg->cfg_data.splat_tests.descs[i]);

		test = test_init(sub, desc);
		if (test == NULL) {
			fprintf(stderr, "Error initializing test: %s\n",
			        desc->name);
			free(cfg);
			return -ENOMEM;
		}

		list_append(sub->sub_tests, test);
	}

	free(cfg);
	return 0;
}

static test_t *test_copy(test_t *test)
{
	return test_init(test->test_sub, &test->test_desc);
}

static void test_list(List l, int indent)
{
	ListIterator i;
	test_t *test;

	i = list_iterator_create(l);

	while ((test = list_next(i)))
		fprintf(stdout, "%*s0x%0*x %-*s %s\n",
		        indent, "", 04, test->test_desc.id,
		        SPLAT_NAME_SIZE, test->test_desc.name,
		        test->test_desc.desc);

	list_iterator_destroy(i);
}

static test_t *test_find(char *sub_str, char *test_str)
{
	ListIterator si, ti;
	subsystem_t *sub;
	test_t *test;
	__u32 sub_num, test_num;

	/*
	 * No error checking here because it may not be a number, it's
	 * perfectly OK for it to be a string.  Since we're just using
	 * it for comparison purposes this is all very safe.
	 */
	sub_num = strtoul(sub_str, NULL, 0);
	test_num = strtoul(test_str, NULL, 0);

        si = list_iterator_create(subsystems);

        while ((sub = list_next(si))) {

		if (strncmp(sub->sub_desc.name, sub_str, SPLAT_NAME_SIZE) &&
		    sub->sub_desc.id != sub_num)
			continue;

		ti = list_iterator_create(sub->sub_tests);

		while ((test = list_next(ti))) {

			if (!strncmp(test->test_desc.name, test_str,
		            SPLAT_NAME_SIZE) || test->test_desc.id==test_num) {
				list_iterator_destroy(ti);
			        list_iterator_destroy(si);
				return test;
			}
		}

	        list_iterator_destroy(ti);
        }

        list_iterator_destroy(si);

	return NULL;
}

static int test_add(cmd_args_t *args, test_t *test)
{
	test_t *tmp;

	tmp = test_copy(test);
	if (tmp == NULL)
		return -ENOMEM;

	list_append(args->args_tests, tmp);
	return 0;
}

static int test_add_all(cmd_args_t *args)
{
	ListIterator si, ti;
	subsystem_t *sub;
	test_t *test;
	int rc;

        si = list_iterator_create(subsystems);

        while ((sub = list_next(si))) {
		ti = list_iterator_create(sub->sub_tests);

		while ((test = list_next(ti))) {
			if ((rc = test_add(args, test))) {
			        list_iterator_destroy(ti);
			        list_iterator_destroy(si);
				return rc;
			}
		}

	        list_iterator_destroy(ti);
        }

        list_iterator_destroy(si);

	return 0;
}

static int test_run(cmd_args_t *args, test_t *test)
{
	subsystem_t *sub = test->test_sub;
	splat_cmd_t *cmd;
	int rc, cmd_size;

	dev_clear();

	cmd_size = sizeof(*cmd);
	cmd = (splat_cmd_t *)malloc(cmd_size);
	if (cmd == NULL)
		return -ENOMEM;

	memset(cmd, 0, cmd_size);
	cmd->cmd_magic = SPLAT_CMD_MAGIC;
        cmd->cmd_subsystem = sub->sub_desc.id;
	cmd->cmd_test = test->test_desc.id;
	cmd->cmd_data_size = 0; /* Unused feature */

	fprintf(stdout, "%*s:%-*s ",
	        SPLAT_NAME_SIZE, sub->sub_desc.name,
	        SPLAT_NAME_SIZE, test->test_desc.name);
	fflush(stdout);
	rc = ioctl(splatctl_fd, SPLAT_CMD, cmd);
	if (args->args_do_color) {
		fprintf(stdout, "%s  %s\n", rc ?
		        COLOR_RED "Fail" COLOR_RESET :
		        COLOR_GREEN "Pass" COLOR_RESET,
			rc ? strerror(errno) : "");
	} else {
		fprintf(stdout, "%s  %s\n", rc ?
		        "Fail" : "Pass",
			rc ? strerror(errno) : "");
	}
	fflush(stdout);
	free(cmd);

	if ((args->args_verbose == 1 && rc) ||
	    (args->args_verbose >= 2)) {
		if ((rc = read(splatctl_fd, splat_buffer,
			       splat_buffer_size - 1)) < 0) {
			fprintf(stdout, "Error reading results: %d\n", rc);
		} else {
			fprintf(stdout, "\n%s\n", splat_buffer);
			fflush(stdout);
		}
	}

	return rc;
}

static int tests_run(cmd_args_t *args)
{
        ListIterator i;
	test_t *test;
	int rc;

	fprintf(stdout,
	        "------------------------------ "
	        "Running SPLAT Tests "
	        "------------------------------\n");

	i = list_iterator_create(args->args_tests);

	while ((test = list_next(i))) {
		rc = test_run(args, test);
		if (rc && args->args_exit_on_error) {
			list_iterator_destroy(i);
			return rc;
		}
	}

	list_iterator_destroy(i);
	return 0;
}

static int args_parse_test(cmd_args_t *args, char *str)
{
        ListIterator si, ti;
	subsystem_t *s;
	test_t *t;
	char *sub_str, *test_str;
	int sub_num, test_num;
	int sub_all = 0, test_all = 0;
	int rc, flag = 0;

	test_str = strchr(str, ':');
	if (test_str == NULL) {
		fprintf(stderr, "Test must be of the "
		        "form <subsystem:test>\n");
		return -EINVAL;
	}

	sub_str = str;
	test_str[0] = '\0';
	test_str = test_str + 1;

	sub_num = strtol(sub_str, NULL, 0);
	test_num = strtol(test_str, NULL, 0);

	if (!strncasecmp(sub_str, "all", strlen(sub_str)) || (sub_num == -1))
		sub_all = 1;

	if (!strncasecmp(test_str,"all",strlen(test_str)) || (test_num == -1))
		test_all = 1;

	si = list_iterator_create(subsystems);

	if (sub_all) {
		if (test_all) {
			/* Add all tests from all subsystems */
			while ((s = list_next(si))) {
				ti = list_iterator_create(s->sub_tests);
				while ((t = list_next(ti))) {
					if ((rc = test_add(args, t))) {
						list_iterator_destroy(ti);
						goto error_run;
					}
				}
				list_iterator_destroy(ti);
			}
		} else {
			/* Add a specific test from all subsystems */
			while ((s = list_next(si))) {
				if ((t=test_find(s->sub_desc.name,test_str))) {
					if ((rc = test_add(args, t)))
						goto error_run;

					flag = 1;
				}
			}

			if (!flag)
				fprintf(stderr, "No tests '%s:%s' could be "
				        "found\n", sub_str, test_str);
		}
	} else {
		if (test_all) {
			/* Add all tests from a specific subsystem */
			while ((s = list_next(si))) {
				if (strncasecmp(sub_str, s->sub_desc.name,
				    strlen(sub_str)))
					continue;

				ti = list_iterator_create(s->sub_tests);
				while ((t = list_next(ti))) {
					if ((rc = test_add(args, t))) {
						list_iterator_destroy(ti);
						goto error_run;
					}
				}
				list_iterator_destroy(ti);
			}
		} else {
			/* Add a specific test from a specific subsystem */
			if ((t = test_find(sub_str, test_str))) {
				if ((rc = test_add(args, t)))
					goto error_run;
			} else {
				fprintf(stderr, "Test '%s:%s' could not be "
				        "found\n", sub_str, test_str);
				return -EINVAL;
			}
		}
	}

	list_iterator_destroy(si);

	return 0;

error_run:
	list_iterator_destroy(si);

	fprintf(stderr, "Test '%s:%s' not added to run list: %d\n",
	        sub_str, test_str, rc);

	return rc;
}

static void args_fini(cmd_args_t *args)
{
	assert(args != NULL);

	if (args->args_tests != NULL)
		list_destroy(args->args_tests);

	free(args);
}

static cmd_args_t *
args_init(int argc, char **argv)
{
	cmd_args_t *args;
	int c, rc;

	if (argc == 1) {
		usage();
		return (cmd_args_t *) NULL;
	}

	/* Configure and populate the args structures */
	args = malloc(sizeof(*args));
	if (args == NULL)
		return NULL;

	memset(args, 0, sizeof(*args));
	args->args_verbose = 0;
	args->args_do_list = 0;
	args->args_do_all  = 0;
	args->args_do_color = 1;
	args->args_exit_on_error = 0;
	args->args_tests = list_create((ListDelF)test_fini);
	if (args->args_tests == NULL) {
		args_fini(args);
		return NULL;
	}

	while ((c = getopt_long(argc, argv, shortOpts, longOpts, NULL)) != -1){
		switch (c) {
		case 'v':  args->args_verbose++;			break;
		case 'l':  args->args_do_list = 1;			break;
		case 'a':  args->args_do_all = 1;			break;
		case 'c':  args->args_do_color = 0;			break;
		case 'x':  args->args_exit_on_error = 1;		break;
		case 't':
			if (args->args_do_all) {
				fprintf(stderr, "Option -t <subsystem:test> is "
				        "useless when used with -a\n");
				args_fini(args);
				return NULL;
			}

			rc = args_parse_test(args, argv[optind - 1]);
			if (rc) {
				args_fini(args);
				return NULL;
			}
			break;
		case 'h':
		case '?':
			usage();
			args_fini(args);
			return NULL;
		default:
			fprintf(stderr, "Unknown option '%s'\n",
			        argv[optind - 1]);
			break;
		}
	}

	return args;
}

static int
dev_clear(void)
{
	splat_cfg_t cfg;
	int rc;

	memset(&cfg, 0, sizeof(cfg));
	cfg.cfg_magic = SPLAT_CFG_MAGIC;
        cfg.cfg_cmd   = SPLAT_CFG_BUFFER_CLEAR;
	cfg.cfg_arg1  = 0;

	rc = ioctl(splatctl_fd, SPLAT_CFG, &cfg);
	if (rc)
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		        (unsigned long) SPLAT_CFG, cfg.cfg_cmd, errno);

	lseek(splatctl_fd, 0, SEEK_SET);

	return rc;
}

static int
dev_size(int size)
{
	splat_cfg_t cfg;
	int rc;

	memset(&cfg, 0, sizeof(cfg));
	cfg.cfg_magic = SPLAT_CFG_MAGIC;
        cfg.cfg_cmd   = SPLAT_CFG_BUFFER_SIZE;
	cfg.cfg_arg1  = size;

	rc = ioctl(splatctl_fd, SPLAT_CFG, &cfg);
	if (rc) {
		fprintf(stderr, "Ioctl() error %lu / %d: %d\n",
		        (unsigned long) SPLAT_CFG, cfg.cfg_cmd, errno);
		return rc;
	}

	return cfg.cfg_rc1;
}

static void
dev_fini(void)
{
	if (splat_buffer)
		free(splat_buffer);

	if (splatctl_fd != -1) {
		if (close(splatctl_fd) == -1) {
			fprintf(stderr, "Unable to close %s: %d\n",
		                SPLAT_DEV, errno);
		}
	}
}

static int
dev_init(void)
{
	ListIterator i;
	subsystem_t *sub;
	int rc;

	splatctl_fd = open(SPLAT_DEV, O_RDONLY);
	if (splatctl_fd == -1) {
		fprintf(stderr, "Unable to open %s: %d\n"
		        "Is the splat module loaded?\n", SPLAT_DEV, errno);
		rc = errno;
		goto error;
	}

	/* Determine kernel module version string */
	memset(splat_version, 0, VERSION_SIZE);
	if ((rc = read(splatctl_fd, splat_version, VERSION_SIZE - 1)) == -1)
		goto error;

	if ((rc = dev_clear()))
		goto error;

	if ((rc = dev_size(0)) < 0)
		goto error;

	splat_buffer_size = rc;
	splat_buffer = (char *)malloc(splat_buffer_size);
	if (splat_buffer == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	memset(splat_buffer, 0, splat_buffer_size);

	/* Determine available subsystems */
	if ((rc = subsystem_setup()) != 0)
		goto error;

	/* Determine available tests for all subsystems */
	i = list_iterator_create(subsystems);

	while ((sub = list_next(i))) {
		if ((rc = test_setup(sub)) != 0) {
			list_iterator_destroy(i);
			goto error;
		}
	}

	list_iterator_destroy(i);
	return 0;

error:
	if (splatctl_fd != -1) {
		if (close(splatctl_fd) == -1) {
			fprintf(stderr, "Unable to close %s: %d\n",
		                SPLAT_DEV, errno);
		}
	}

	return rc;
}

int
init(void)
{
	int rc = 0;

	/* Allocate the subsystem list */
	subsystems = list_create((ListDelF)subsystem_fini);
	if (subsystems == NULL)
		rc = ENOMEM;

	return rc;
}

void
fini(void)
{
	list_destroy(subsystems);
}


int
main(int argc, char **argv)
{
	cmd_args_t *args = NULL;
	int rc = 0;

	/* General init */
	if ((rc = init()))
		return rc;

	/* Device specific init */
	if ((rc = dev_init()))
		goto out;

	/* Argument init and parsing */
	if ((args = args_init(argc, argv)) == NULL) {
		rc = -1;
		goto out;
	}

	/* Generic kernel version string */
	if (args->args_verbose)
		fprintf(stdout, "%s", splat_version);

	/* Print the available test list and exit */
	if (args->args_do_list) {
		subsystem_list(subsystems, 0);
		goto out;
	}

	/* Add all available test to the list of tests to run */
	if (args->args_do_all) {
		if ((rc = test_add_all(args)))
			goto out;
	}

	/* Run all the requested tests */
	if ((rc = tests_run(args)))
		goto out;

out:
	if (args != NULL)
		args_fini(args);

	dev_fini();
	fini();
	return rc;
}
