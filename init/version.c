// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/init/version.c
 *
 *  Copyright (C) 1992  Theodore Ts'o
 *
 *  May be freely distributed as part of Linux.
 */

#include <generated/compile.h>
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/uts.h>
#include <linux/utsname.h>
#include <linux/proc_ns.h>

/**
 * Sets the hostname based on a command line argument.
 *
 * @param arg Pointer to the string containing the hostname.
 * @return 0 on success, negative value on error.
 */
static int __init early_hostname(char *arg)
{
    size_t bufsize = sizeof(init_uts_ns.name.nodename);
    size_t maxlen  = bufsize - 1;
    ssize_t arglen;

    if (strlen(arg) >= bufsize) {
        pr_err("Hostname is too long, it must be less than %zu characters.\n", bufsize);
        return -EINVAL;
    }

    arglen = strscpy(init_uts_ns.name.nodename, arg, bufsize);
    if (arglen < 0 || (size_t)arglen >= bufsize) {
        pr_warn("Hostname '%s' exceeds maximum length (%zu), it will be truncated.\n", arg, maxlen);
    }

    return 0;
}
early_param("hostname", early_hostname);

const char linux_proc_banner[] =
    "%s version %s (%s)\n"
    "Compiled by %s on %s\n"
    "Compiler: %s\n";

BUILD_SALT;

#ifdef CONFIG_LTO_CLANG
BUILD_LTO_INFO;
#endif

/*
 * init_uts_ns and linux_banner contain the build version and timestamp,
 * which are really fixed at the very last step of build process.
 * They are compiled with __weak first, and without __weak later.
 */

struct uts_namespace init_uts_ns __weak;
const char linux_banner[] __weak;

#include "version-timestamp.c"

EXPORT_SYMBOL_GPL(init_uts_ns);
