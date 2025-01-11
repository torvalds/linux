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
 * Function to set the hostname during early boot.
 *
 * @param arg Pointer to a string containing the hostname.
 * @return Always returns 0.
 */
static int __init early_hostname(char *arg)
{
    const size_t bufsize = sizeof(init_uts_ns.name.nodename);
    const size_t maxlen  = bufsize - 1;
    ssize_t arglen;

    arglen = strlcpy(init_uts_ns.name.nodename, arg, bufsize);
    
    if (arglen > maxlen) {
        init_uts_ns.name.nodename[maxlen] = '\0';
        pr_warn("Hostname parameter was truncated to %zu characters.", maxlen);
    }

    return 0;
}
early_param("hostname", early_hostname);

/* Banner format for displaying kernel version information. */
const char linux_proc_banner[] =
    "%s version %s (%s)"
    " (" LINUX_COMPILE_BY "@" LINUX_COMPILE_HOST ")"
    " (" LINUX_COMPILER ") %s\n"
    "Kernel command line: %s\n";

BUILD_SALT;
BUILD_LTO_INFO;

/*
 * init_uts_ns and linux_banner contain the build version and timestamp,
 * which are really fixed at the very last step of build process.
 * They are compiled with __weak first, and without __weak later.
 */

struct uts_namespace init_uts_ns __weak;
const char linux_banner[] __weak;

#include "version-timestamp.c"

EXPORT_SYMBOL_GPL(init_uts_ns);
