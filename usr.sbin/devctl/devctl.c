/*-
 * Copyright (c) 2014 John Baldwin <jhb@FreeBSD.org>
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

#include <sys/linker_set.h>
#include <devctl.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

struct devctl_command {
	const char *name;
	int (*handler)(int ac, char **av);
};

#define	DEVCTL_DATASET(name)	devctl_ ## name ## _table

#define	DEVCTL_COMMAND(set, name, function)				\
	static struct devctl_command function ## _devctl_command =	\
	{ #name, function };						\
	DATA_SET(DEVCTL_DATASET(set), function ## _devctl_command)

#define	DEVCTL_TABLE(set, name)						\
	SET_DECLARE(DEVCTL_DATASET(name), struct devctl_command);	\
									\
	static int							\
	devctl_ ## name ## _table_handler(int ac, char **av)		\
	{								\
		return (devctl_table_handler(SET_BEGIN(DEVCTL_DATASET(name)), \
		    SET_LIMIT(DEVCTL_DATASET(name)), ac, av));		\
	}								\
	DEVCTL_COMMAND(set, name, devctl_ ## name ## _table_handler)

static int	devctl_table_handler(struct devctl_command **start,
    struct devctl_command **end, int ac, char **av);

SET_DECLARE(DEVCTL_DATASET(top), struct devctl_command);

DEVCTL_TABLE(top, clear);
DEVCTL_TABLE(top, set);

static void
usage(void)
{
	fprintf(stderr,
	    "usage: devctl attach device\n"
	    "       devctl detach [-f] device\n"
	    "       devctl disable [-f] device\n"
	    "       devctl enable device\n"
	    "       devctl suspend device\n"
	    "       devctl resume device\n"
	    "       devctl set driver [-f] device driver\n"
	    "       devctl clear driver [-f] device\n"
	    "       devctl rescan device\n"
	    "       devctl delete [-f] device\n"
	    "       devctl freeze\n"
	    "       devctl thaw\n");
	exit(1);
}

static int
devctl_table_handler(struct devctl_command **start,
    struct devctl_command **end, int ac, char **av)
{
	struct devctl_command **cmd;

	if (ac < 2) {
		warnx("The %s command requires a sub-command.", av[0]);
		return (EINVAL);
	}
	for (cmd = start; cmd < end; cmd++) {
		if (strcmp((*cmd)->name, av[1]) == 0)
			return ((*cmd)->handler(ac - 1, av + 1));
	}

	warnx("%s is not a valid sub-command of %s.", av[1], av[0]);
	return (ENOENT);
}

static int
help(int ac __unused, char **av __unused)
{

	usage();
	return (0);
}
DEVCTL_COMMAND(top, help, help);

static int
attach(int ac, char **av)
{

	if (ac != 2)
		usage();
	if (devctl_attach(av[1]) < 0)
		err(1, "Failed to attach %s", av[1]);
	return (0);
}
DEVCTL_COMMAND(top, attach, attach);

static void
detach_usage(void)
{

	fprintf(stderr, "usage: devctl detach [-f] device\n");
	exit(1);
}

static int
detach(int ac, char **av)
{
	bool force;
	int ch;

	force = false;
	while ((ch = getopt(ac, av, "f")) != -1)
		switch (ch) {
		case 'f':
			force = true;
			break;
		default:
			detach_usage();
		}
	ac -= optind;
	av += optind;

	if (ac != 1)
		detach_usage();
	if (devctl_detach(av[0], force) < 0)
		err(1, "Failed to detach %s", av[0]);
	return (0);
}
DEVCTL_COMMAND(top, detach, detach);

static void
disable_usage(void)
{

	fprintf(stderr, "usage: devctl disable [-f] device\n");
	exit(1);
}

static int
disable(int ac, char **av)
{
	bool force;
	int ch;

	force = false;
	while ((ch = getopt(ac, av, "f")) != -1)
		switch (ch) {
		case 'f':
			force = true;
			break;
		default:
			disable_usage();
		}
	ac -= optind;
	av += optind;

	if (ac != 1)
		disable_usage();
	if (devctl_disable(av[0], force) < 0)
		err(1, "Failed to disable %s", av[0]);
	return (0);
}
DEVCTL_COMMAND(top, disable, disable);

static int
enable(int ac, char **av)
{

	if (ac != 2)
		usage();
	if (devctl_enable(av[1]) < 0)
		err(1, "Failed to enable %s", av[1]);
	return (0);
}
DEVCTL_COMMAND(top, enable, enable);

static int
suspend(int ac, char **av)
{

	if (ac != 2)
		usage();
	if (devctl_suspend(av[1]) < 0)
		err(1, "Failed to suspend %s", av[1]);
	return (0);
}
DEVCTL_COMMAND(top, suspend, suspend);

static int
resume(int ac, char **av)
{

	if (ac != 2)
		usage();
	if (devctl_resume(av[1]) < 0)
		err(1, "Failed to resume %s", av[1]);
	return (0);
}
DEVCTL_COMMAND(top, resume, resume);

static void
set_driver_usage(void)
{

	fprintf(stderr, "usage: devctl set driver [-f] device driver\n");
	exit(1);
}

static int
set_driver(int ac, char **av)
{
	bool force;
	int ch;

	force = false;
	while ((ch = getopt(ac, av, "f")) != -1)
		switch (ch) {
		case 'f':
			force = true;
			break;
		default:
			set_driver_usage();
		}
	ac -= optind;
	av += optind;

	if (ac != 2)
		set_driver_usage();
	if (devctl_set_driver(av[0], av[1], force) < 0)
		err(1, "Failed to set %s driver to %s", av[0], av[1]);
	return (0);
}
DEVCTL_COMMAND(set, driver, set_driver);

static void
clear_driver_usage(void)
{

	fprintf(stderr, "usage: devctl clear driver [-f] device\n");
	exit(1);
}

static int
clear_driver(int ac, char **av)
{
	bool force;
	int ch;

	force = false;
	while ((ch = getopt(ac, av, "f")) != -1)
		switch (ch) {
		case 'f':
			force = true;
			break;
		default:
			clear_driver_usage();
		}
	ac -= optind;
	av += optind;

	if (ac != 1)
		clear_driver_usage();
	if (devctl_clear_driver(av[0], force) < 0)
		err(1, "Failed to clear %s driver", av[0]);
	return (0);
}
DEVCTL_COMMAND(clear, driver, clear_driver);

static int
rescan(int ac, char **av)
{

	if (ac != 2)
		usage();
	if (devctl_rescan(av[1]) < 0)
		err(1, "Failed to rescan %s", av[1]);
	return (0);
}
DEVCTL_COMMAND(top, rescan, rescan);

static void
delete_usage(void)
{

	fprintf(stderr, "usage: devctl delete [-f] device\n");
	exit(1);
}

static int
delete(int ac, char **av)
{
	bool force;
	int ch;

	force = false;
	while ((ch = getopt(ac, av, "f")) != -1)
		switch (ch) {
		case 'f':
			force = true;
			break;
		default:
			delete_usage();
		}
	ac -= optind;
	av += optind;

	if (ac != 1)
		delete_usage();
	if (devctl_delete(av[0], force) < 0)
		err(1, "Failed to delete %s", av[0]);
	return (0);
}
DEVCTL_COMMAND(top, delete, delete);

static void
freeze_usage(void)
{

	fprintf(stderr, "usage: devctl freeze\n");
	exit(1);
}

static int
freeze(int ac, char **av __unused)
{

	if (ac != 1)
		freeze_usage();
	if (devctl_freeze() < 0)
		err(1, "Failed to freeze probe/attach");
	return (0);
}
DEVCTL_COMMAND(top, freeze, freeze);

static void
thaw_usage(void)
{

	fprintf(stderr, "usage: devctl thaw\n");
	exit(1);
}

static int
thaw(int ac, char **av __unused)
{

	if (ac != 1)
		thaw_usage();
	if (devctl_thaw() < 0)
		err(1, "Failed to thaw probe/attach");
	return (0);
}
DEVCTL_COMMAND(top, thaw, thaw);

int
main(int ac, char *av[])
{
	struct devctl_command **cmd;

	if (ac == 1)
		usage();
	ac--;
	av++;

	SET_FOREACH(cmd, DEVCTL_DATASET(top)) {
		if (strcmp((*cmd)->name, av[0]) == 0) {
			if ((*cmd)->handler(ac, av) != 0)
				return (1);
			else
				return (0);
		}
	}
	warnx("Unknown command %s.", av[0]);
	return (1);
}
