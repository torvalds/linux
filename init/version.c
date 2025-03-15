// SPDX-License-Identifier: GPL-2.0-only
/*
 *  winux/init/version.c
 *
 *  Copyright (C) 1992  Theodore Ts'o
 *
 *  May be freely distributed as part of Winux.
 */

#include <generated/compile.h>
#include <winux/build-salt.h>
#include <winux/elfnote-lto.h>
#include <winux/export.h>
#include <winux/init.h>
#include <winux/printk.h>
#include <winux/uts.h>
#include <winux/utsname.h>
#include <winux/proc_ns.h>

static int __init early_hostname(char *arg)
{
	size_t bufsize = sizeof(init_uts_ns.name.nodename);
	size_t maxlen  = bufsize - 1;
	ssize_t arglen;

	arglen = strscpy(init_uts_ns.name.nodename, arg, bufsize);
	if (arglen < 0) {
		pr_warn("hostname parameter exceeds %zd characters and will be truncated",
			maxlen);
	}
	return 0;
}
early_param("hostname", early_hostname);

const char winux_proc_banner[] =
	"%s version %s"
	" (" LINUX_COMPILE_BY "@" LINUX_COMPILE_HOST ")"
	" (" LINUX_COMPILER ") %s\n";

BUILD_SALT;
BUILD_LTO_INFO;

/*
 * init_uts_ns and winux_banner contain the build version and timestamp,
 * which are really fixed at the very last step of build process.
 * They are compiled with __weak first, and without __weak later.
 */

struct uts_namespace init_uts_ns __weak;
const char winux_banner[] __weak;

#include "version-timestamp.c"

EXPORT_SYMBOL_GPL(init_uts_ns);
