// SPDX-License-Identifier: GPL-2.0
/*
 * Kernel command line console options for hardware based addressing
 *
 * Copyright (C) 2023 Texas Instruments Incorporated - https://www.ti.com/
 * Author: Tony Lindgren <tony@atomide.com>
 */

#include <linux/console.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/types.h>

#include <asm/errno.h>

#include "console_cmdline.h"

/*
 * Allow longer DEVNAME:0.0 style console naming such as abcd0000.serial:0.0
 * in addition to the legacy ttyS0 style naming.
 */
#define CONSOLE_NAME_MAX	32

#define CONSOLE_OPT_MAX		16
#define CONSOLE_BRL_OPT_MAX	16

struct console_option {
	char name[CONSOLE_NAME_MAX];
	char opt[CONSOLE_OPT_MAX];
	char brl_opt[CONSOLE_BRL_OPT_MAX];
	u8 has_brl_opt:1;
};

/* Updated only at console_setup() time, no locking needed */
static struct console_option conopt[MAX_CMDLINECONSOLES];

/**
 * console_opt_save - Saves kernel command line console option for driver use
 * @str: Kernel command line console name and option
 * @brl_opt: Braille console options
 *
 * Saves a kernel command line console option for driver subsystems to use for
 * adding a preferred console during init. Called from console_setup() only.
 *
 * Return: 0 on success, negative error code on failure.
 */
int __init console_opt_save(const char *str, const char *brl_opt)
{
	struct console_option *con;
	size_t namelen, optlen;
	const char *opt;
	int i;

	namelen = strcspn(str, ",");
	if (namelen == 0 || namelen >= CONSOLE_NAME_MAX)
		return -EINVAL;

	opt = str + namelen;
	if (*opt == ',')
		opt++;

	optlen = strlen(opt);
	if (optlen >= CONSOLE_OPT_MAX)
		return -EINVAL;

	for (i = 0; i < MAX_CMDLINECONSOLES; i++) {
		con = &conopt[i];

		if (con->name[0]) {
			if (!strncmp(str, con->name, namelen))
				return 0;
			continue;
		}

		/*
		 * The name isn't terminated, only opt is. Empty opt is fine,
		 * but brl_opt can be either empty or NULL. For more info, see
		 * _braille_console_setup().
		 */
		strscpy(con->name, str, namelen + 1);
		strscpy(con->opt, opt, CONSOLE_OPT_MAX);
		if (brl_opt) {
			strscpy(con->brl_opt, brl_opt, CONSOLE_BRL_OPT_MAX);
			con->has_brl_opt = 1;
		}

		return 0;
	}

	return -ENOMEM;
}

static struct console_option *console_opt_find(const char *name)
{
	struct console_option *con;
	int i;

	for (i = 0; i < MAX_CMDLINECONSOLES; i++) {
		con = &conopt[i];
		if (!strcmp(name, con->name))
			return con;
	}

	return NULL;
}

/**
 * add_preferred_console_match - Adds a preferred console if a match is found
 * @match: Expected console on kernel command line, such as console=DEVNAME:0.0
 * @name: Name of the console character device to add such as ttyS
 * @idx: Index for the console
 *
 * Allows driver subsystems to add a console after translating the command
 * line name to the character device name used for the console. Options are
 * added automatically based on the kernel command line. Duplicate preferred
 * consoles are ignored by __add_preferred_console().
 *
 * Return: 0 on success, negative error code on failure.
 */
int add_preferred_console_match(const char *match, const char *name,
				const short idx)
{
	struct console_option *con;
	char *brl_opt = NULL;

	if (!match || !strlen(match) || !name || !strlen(name) ||
	    idx < 0)
		return -EINVAL;

	con = console_opt_find(match);
	if (!con)
		return -ENOENT;

	/*
	 * See __add_preferred_console(). It checks for NULL brl_options to set
	 * the preferred_console flag. Empty brl_opt instead of NULL leads into
	 * the preferred_console flag not set, and CON_CONSDEV not being set,
	 * and the boot console won't get disabled at the end of console_setup().
	 */
	if (con->has_brl_opt)
		brl_opt = con->brl_opt;

	console_opt_add_preferred_console(name, idx, con->opt, brl_opt);

	return 0;
}
