/*
 * command structure borrowed from udev
 * (git://git.kernel.org/pub/scm/linux/hotplug/udev.git)
 *
 * Copyright (C) 2011 matt mooney <mfm@muteddisk.com>
 *               2005-2007 Takahiro Hirofuchi
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>

#include <getopt.h>
#include <syslog.h>

#include "usbip_common.h"
#include "usbip_network.h"
#include "usbip.h"

static int usbip_help(int argc, char *argv[]);
static int usbip_version(int argc, char *argv[]);

static const char usbip_version_string[] = PACKAGE_STRING;

static const char usbip_usage_string[] =
	"usbip [--debug] [--log] [--tcp-port PORT] [version]\n"
	"             [help] <command> <args>\n";

static void usbip_usage(void)
{
	printf("usage: %s", usbip_usage_string);
}

struct command {
	const char *name;
	int (*fn)(int argc, char *argv[]);
	const char *help;
	void (*usage)(void);
};

static const struct command cmds[] = {
	{
		.name  = "help",
		.fn    = usbip_help,
		.help  = NULL,
		.usage = NULL
	},
	{
		.name  = "version",
		.fn    = usbip_version,
		.help  = NULL,
		.usage = NULL
	},
	{
		.name  = "attach",
		.fn    = usbip_attach,
		.help  = "Attach a remote USB device",
		.usage = usbip_attach_usage
	},
	{
		.name  = "detach",
		.fn    = usbip_detach,
		.help  = "Detach a remote USB device",
		.usage = usbip_detach_usage
	},
	{
		.name  = "list",
		.fn    = usbip_list,
		.help  = "List exportable or local USB devices",
		.usage = usbip_list_usage
	},
	{
		.name  = "bind",
		.fn    = usbip_bind,
		.help  = "Bind device to " USBIP_HOST_DRV_NAME ".ko",
		.usage = usbip_bind_usage
	},
	{
		.name  = "unbind",
		.fn    = usbip_unbind,
		.help  = "Unbind device from " USBIP_HOST_DRV_NAME ".ko",
		.usage = usbip_unbind_usage
	},
	{
		.name  = "port",
		.fn    = usbip_port_show,
		.help  = "Show imported USB devices",
		.usage = NULL
	},
	{ NULL, NULL, NULL, NULL }
};

static int usbip_help(int argc, char *argv[])
{
	const struct command *cmd;
	int i;
	int ret = 0;

	if (argc > 1 && argv++) {
		for (i = 0; cmds[i].name != NULL; i++)
			if (!strcmp(cmds[i].name, argv[0]) && cmds[i].usage) {
				cmds[i].usage();
				goto done;
			}
		ret = -1;
	}

	usbip_usage();
	printf("\n");
	for (cmd = cmds; cmd->name != NULL; cmd++)
		if (cmd->help != NULL)
			printf("  %-10s %s\n", cmd->name, cmd->help);
	printf("\n");
done:
	return ret;
}

static int usbip_version(int argc, char *argv[])
{
	(void) argc;
	(void) argv;

	printf(PROGNAME " (%s)\n", usbip_version_string);
	return 0;
}

static int run_command(const struct command *cmd, int argc, char *argv[])
{
	dbg("running command: `%s'", cmd->name);
	return cmd->fn(argc, argv);
}

int main(int argc, char *argv[])
{
	static const struct option opts[] = {
		{ "debug",    no_argument,       NULL, 'd' },
		{ "log",      no_argument,       NULL, 'l' },
		{ "tcp-port", required_argument, NULL, 't' },
		{ NULL,       0,                 NULL,  0  }
	};

	char *cmd;
	int opt;
	int i, rc = -1;

	usbip_use_stderr = 1;
	opterr = 0;
	for (;;) {
		opt = getopt_long(argc, argv, "+dlt:", opts, NULL);

		if (opt == -1)
			break;

		switch (opt) {
		case 'd':
			usbip_use_debug = 1;
			break;
		case 'l':
			usbip_use_syslog = 1;
			openlog("", LOG_PID, LOG_USER);
			break;
		case 't':
			usbip_setup_port_number(optarg);
			break;
		case '?':
			printf("usbip: invalid option\n");
		default:
			usbip_usage();
			goto out;
		}
	}

	cmd = argv[optind];
	if (cmd) {
		for (i = 0; cmds[i].name != NULL; i++)
			if (!strcmp(cmds[i].name, cmd)) {
				argc -= optind;
				argv += optind;
				optind = 0;
				rc = run_command(&cmds[i], argc, argv);
				goto out;
			}
	}

	/* invalid command */
	usbip_help(0, NULL);
out:
	return (rc > -1 ? EXIT_SUCCESS : EXIT_FAILURE);
}
