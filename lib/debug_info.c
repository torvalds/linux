/*
 * This file exists solely to ensure debug information for some core
 * data structures is included in the final image even for
 * CONFIG_DEBUG_INFO_REDUCED. Please do not add actual code. However,
 * adding appropriate #includes is fine.
 */
#include <stdarg.h>

#include <linux/cred.h>
#include <linux/crypto.h>
#include <linux/dcache.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/fscache-cache.h>
#include <linux/io.h>
#include <linux/kallsyms.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <net/addrconf.h>
#include <net/sock.h>
#include <net/tcp.h>
