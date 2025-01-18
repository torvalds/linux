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

    if (!arg || strlen(arg) >= bufsize) {
        if (!arg) {
            pr_err("Invalid hostname argument: null pointer.\n");
        } else {
            pr_err("Hostname is too long, it must be less than %zu characters.\n", bufsize);
        }
        return -EINVAL;
    }

    if (strscpy(init_uts_ns.name.nodename, arg, bufsize) >= bufsize) {
        WARN_ONCE(1, "Hostname '%s' exceeds maximum length (%zu), it will be truncated.\n", arg, maxlen);
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

struct uts_namespace init_uts_ns __weak;
const char linux_banner[] __weak;

#include "version-timestamp.c"

EXPORT_SYMBOL_GPL(init_uts_ns);
