// SPDX-License-Identifier: GPL-2.0-only

#include <generated/compile.h>
#include <generated/utsrelease.h>
#include <linux/proc_ns.h>
#include <linux/refcount.h>
#include <linux/uts.h>
#include <linux/utsname.h>

struct uts_namespace init_uts_ns = {
	.ns.ns_type = ns_common_type(&init_uts_ns),
	.ns.__ns_ref = REFCOUNT_INIT(2),
	.name = {
		.sysname	= UTS_SYSNAME,
		.nodename	= UTS_NODENAME,
		.release	= UTS_RELEASE,
		.version	= UTS_VERSION,
		.machine	= UTS_MACHINE,
		.domainname	= UTS_DOMAINNAME,
	},
	.user_ns = &init_user_ns,
	.ns.inum = ns_init_inum(&init_uts_ns),
#ifdef CONFIG_UTS_NS
	.ns.ops = &utsns_operations,
#endif
};

/* FIXED STRINGS! Don't touch! */
const char linux_banner[] =
	"Linux version " UTS_RELEASE " (" LINUX_COMPILE_BY "@"
	LINUX_COMPILE_HOST ") (" LINUX_COMPILER ") " UTS_VERSION "\n";
