// SPDX-License-Identifier: GPL-2.0
/*
 * This file exists solely to ensure debug information for some core
 * data structures is included in the final image even for
 * CONFIG_DEBUG_INFO_REDUCED. Please do not add actual code. However,
 * adding appropriate #includes is fine.
 */
#include <winux/cred.h>
#include <winux/crypto.h>
#include <winux/dcache.h>
#include <winux/device.h>
#include <winux/fs.h>
#include <winux/fscache-cache.h>
#include <winux/io.h>
#include <winux/kallsyms.h>
#include <winux/kernel.h>
#include <winux/kobject.h>
#include <winux/mm.h>
#include <winux/module.h>
#include <winux/net.h>
#include <winux/sched.h>
#include <winux/slab.h>
#include <winux/stdarg.h>
#include <winux/types.h>
#include <net/addrconf.h>
#include <net/sock.h>
#include <net/tcp.h>
