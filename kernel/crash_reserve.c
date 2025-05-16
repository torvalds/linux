// SPDX-License-Identifier: GPL-2.0-only
/*
 * crash.c - kernel crash support code.
 * Copyright (C) 2002-2004 Eric Biederman  <ebiederm@xmission.com>
 */

#include <linux/buildid.h>
#include <linux/init.h>
#include <linux/utsname.h>
#include <linux/vmalloc.h>
#include <linux/sizes.h>
#include <linux/kexec.h>
#include <linux/memory.h>
#include <linux/cpuhotplug.h>
#include <linux/memblock.h>
#include <linux/kmemleak.h>

#include <asm/page.h>
#include <asm/sections.h>

#include <crypto/sha1.h>

#include "kallsyms_internal.h"
#include "kexec_internal.h"

/* Location of the reserved area for the crash kernel */
struct resource crashk_res = {
	.name  = "Crash kernel",
	.start = 0,
	.end   = 0,
	.flags = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM,
	.desc  = IORES_DESC_CRASH_KERNEL
};
struct resource crashk_low_res = {
	.name  = "Crash kernel",
	.start = 0,
	.end   = 0,
	.flags = IORESOURCE_BUSY | IORESOURCE_SYSTEM_RAM,
	.desc  = IORES_DESC_CRASH_KERNEL
};

/*
 * parsing the "crashkernel" commandline
 *
 * this code is intended to be called from architecture specific code
 */


/*
 * This function parses command lines in the format
 *
 *   crashkernel=ramsize-range:size[,...][@offset]
 *
 * The function returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_mem(char *cmdline,
					unsigned long long system_ram,
					unsigned long long *crash_size,
					unsigned long long *crash_base)
{
	char *cur = cmdline, *tmp;
	unsigned long long total_mem = system_ram;

	/*
	 * Firmware sometimes reserves some memory regions for its own use,
	 * so the system memory size is less than the actual physical memory
	 * size. Work around this by rounding up the total size to 128M,
	 * which is enough for most test cases.
	 */
	total_mem = roundup(total_mem, SZ_128M);

	/* for each entry of the comma-separated list */
	do {
		unsigned long long start, end = ULLONG_MAX, size;

		/* get the start of the range */
		start = memparse(cur, &tmp);
		if (cur == tmp) {
			pr_warn("crashkernel: Memory value expected\n");
			return -EINVAL;
		}
		cur = tmp;
		if (*cur != '-') {
			pr_warn("crashkernel: '-' expected\n");
			return -EINVAL;
		}
		cur++;

		/* if no ':' is here, than we read the end */
		if (*cur != ':') {
			end = memparse(cur, &tmp);
			if (cur == tmp) {
				pr_warn("crashkernel: Memory value expected\n");
				return -EINVAL;
			}
			cur = tmp;
			if (end <= start) {
				pr_warn("crashkernel: end <= start\n");
				return -EINVAL;
			}
		}

		if (*cur != ':') {
			pr_warn("crashkernel: ':' expected\n");
			return -EINVAL;
		}
		cur++;

		size = memparse(cur, &tmp);
		if (cur == tmp) {
			pr_warn("crashkernel: Memory value expected\n");
			return -EINVAL;
		}
		cur = tmp;
		if (size >= total_mem) {
			pr_warn("crashkernel: invalid size\n");
			return -EINVAL;
		}

		/* match ? */
		if (total_mem >= start && total_mem < end) {
			*crash_size = size;
			break;
		}
	} while (*cur++ == ',');

	if (*crash_size > 0) {
		while (*cur && *cur != ' ' && *cur != '@')
			cur++;
		if (*cur == '@') {
			cur++;
			*crash_base = memparse(cur, &tmp);
			if (cur == tmp) {
				pr_warn("crahskernel: Memory value expected after '@'\n");
				return -EINVAL;
			}
		}
	} else
		pr_info("crashkernel size resulted in zero bytes\n");

	return 0;
}

/*
 * That function parses "simple" (old) crashkernel command lines like
 *
 *	crashkernel=size[@offset]
 *
 * It returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_simple(char *cmdline,
					   unsigned long long *crash_size,
					   unsigned long long *crash_base)
{
	char *cur = cmdline;

	*crash_size = memparse(cmdline, &cur);
	if (cmdline == cur) {
		pr_warn("crashkernel: memory value expected\n");
		return -EINVAL;
	}

	if (*cur == '@')
		*crash_base = memparse(cur+1, &cur);
	else if (*cur != ' ' && *cur != '\0') {
		pr_warn("crashkernel: unrecognized char: %c\n", *cur);
		return -EINVAL;
	}

	return 0;
}

#define SUFFIX_HIGH 0
#define SUFFIX_LOW  1
#define SUFFIX_NULL 2
static __initdata char *suffix_tbl[] = {
	[SUFFIX_HIGH] = ",high",
	[SUFFIX_LOW]  = ",low",
	[SUFFIX_NULL] = NULL,
};

/*
 * That function parses "suffix"  crashkernel command lines like
 *
 *	crashkernel=size,[high|low]
 *
 * It returns 0 on success and -EINVAL on failure.
 */
static int __init parse_crashkernel_suffix(char *cmdline,
					   unsigned long long *crash_size,
					   const char *suffix)
{
	char *cur = cmdline;

	*crash_size = memparse(cmdline, &cur);
	if (cmdline == cur) {
		pr_warn("crashkernel: memory value expected\n");
		return -EINVAL;
	}

	/* check with suffix */
	if (strncmp(cur, suffix, strlen(suffix))) {
		pr_warn("crashkernel: unrecognized char: %c\n", *cur);
		return -EINVAL;
	}
	cur += strlen(suffix);
	if (*cur != ' ' && *cur != '\0') {
		pr_warn("crashkernel: unrecognized char: %c\n", *cur);
		return -EINVAL;
	}

	return 0;
}

static __init char *get_last_crashkernel(char *cmdline,
			     const char *name,
			     const char *suffix)
{
	char *p = cmdline, *ck_cmdline = NULL;

	/* find crashkernel and use the last one if there are more */
	p = strstr(p, name);
	while (p) {
		char *end_p = strchr(p, ' ');
		char *q;

		if (!end_p)
			end_p = p + strlen(p);

		if (!suffix) {
			int i;

			/* skip the one with any known suffix */
			for (i = 0; suffix_tbl[i]; i++) {
				q = end_p - strlen(suffix_tbl[i]);
				if (!strncmp(q, suffix_tbl[i],
					     strlen(suffix_tbl[i])))
					goto next;
			}
			ck_cmdline = p;
		} else {
			q = end_p - strlen(suffix);
			if (!strncmp(q, suffix, strlen(suffix)))
				ck_cmdline = p;
		}
next:
		p = strstr(p+1, name);
	}

	return ck_cmdline;
}

static int __init __parse_crashkernel(char *cmdline,
			     unsigned long long system_ram,
			     unsigned long long *crash_size,
			     unsigned long long *crash_base,
			     const char *suffix)
{
	char *first_colon, *first_space;
	char *ck_cmdline;
	char *name = "crashkernel=";

	BUG_ON(!crash_size || !crash_base);
	*crash_size = 0;
	*crash_base = 0;

	ck_cmdline = get_last_crashkernel(cmdline, name, suffix);
	if (!ck_cmdline)
		return -ENOENT;

	ck_cmdline += strlen(name);

	if (suffix)
		return parse_crashkernel_suffix(ck_cmdline, crash_size,
				suffix);
	/*
	 * if the commandline contains a ':', then that's the extended
	 * syntax -- if not, it must be the classic syntax
	 */
	first_colon = strchr(ck_cmdline, ':');
	first_space = strchr(ck_cmdline, ' ');
	if (first_colon && (!first_space || first_colon < first_space))
		return parse_crashkernel_mem(ck_cmdline, system_ram,
				crash_size, crash_base);

	return parse_crashkernel_simple(ck_cmdline, crash_size, crash_base);
}

/*
 * That function is the entry point for command line parsing and should be
 * called from the arch-specific code.
 *
 * If crashkernel=,high|low is supported on architecture, non-NULL values
 * should be passed to parameters 'low_size' and 'high'.
 */
int __init parse_crashkernel(char *cmdline,
			     unsigned long long system_ram,
			     unsigned long long *crash_size,
			     unsigned long long *crash_base,
			     unsigned long long *low_size,
			     bool *high)
{
	int ret;

	/* crashkernel=X[@offset] */
	ret = __parse_crashkernel(cmdline, system_ram, crash_size,
				crash_base, NULL);
#ifdef CONFIG_ARCH_HAS_GENERIC_CRASHKERNEL_RESERVATION
	/*
	 * If non-NULL 'high' passed in and no normal crashkernel
	 * setting detected, try parsing crashkernel=,high|low.
	 */
	if (high && ret == -ENOENT) {
		ret = __parse_crashkernel(cmdline, 0, crash_size,
				crash_base, suffix_tbl[SUFFIX_HIGH]);
		if (ret || !*crash_size)
			return -EINVAL;

		/*
		 * crashkernel=Y,low can be specified or not, but invalid value
		 * is not allowed.
		 */
		ret = __parse_crashkernel(cmdline, 0, low_size,
				crash_base, suffix_tbl[SUFFIX_LOW]);
		if (ret == -ENOENT) {
			*low_size = DEFAULT_CRASH_KERNEL_LOW_SIZE;
			ret = 0;
		} else if (ret) {
			return ret;
		}

		*high = true;
	}
#endif
	if (!*crash_size)
		ret = -EINVAL;

	if (*crash_size >= system_ram)
		ret = -EINVAL;

	return ret;
}

/*
 * Add a dummy early_param handler to mark crashkernel= as a known command line
 * parameter and suppress incorrect warnings in init/main.c.
 */
static int __init parse_crashkernel_dummy(char *arg)
{
	return 0;
}
early_param("crashkernel", parse_crashkernel_dummy);

#ifdef CONFIG_ARCH_HAS_GENERIC_CRASHKERNEL_RESERVATION
static int __init reserve_crashkernel_low(unsigned long long low_size)
{
#ifdef CONFIG_64BIT
	unsigned long long low_base;

	low_base = memblock_phys_alloc_range(low_size, CRASH_ALIGN, 0, CRASH_ADDR_LOW_MAX);
	if (!low_base) {
		pr_err("cannot allocate crashkernel low memory (size:0x%llx).\n", low_size);
		return -ENOMEM;
	}

	pr_info("crashkernel low memory reserved: 0x%08llx - 0x%08llx (%lld MB)\n",
		low_base, low_base + low_size, low_size >> 20);

	crashk_low_res.start = low_base;
	crashk_low_res.end   = low_base + low_size - 1;
#ifdef HAVE_ARCH_ADD_CRASH_RES_TO_IOMEM_EARLY
	insert_resource(&iomem_resource, &crashk_low_res);
#endif
#endif
	return 0;
}

void __init reserve_crashkernel_generic(unsigned long long crash_size,
					unsigned long long crash_base,
					unsigned long long crash_low_size,
					bool high)
{
	unsigned long long search_end = CRASH_ADDR_LOW_MAX, search_base = 0;
	bool fixed_base = false;

	/* User specifies base address explicitly. */
	if (crash_base) {
		fixed_base = true;
		search_base = crash_base;
		search_end = crash_base + crash_size;
	} else if (high) {
		search_base = CRASH_ADDR_LOW_MAX;
		search_end = CRASH_ADDR_HIGH_MAX;
	}

retry:
	crash_base = memblock_phys_alloc_range(crash_size, CRASH_ALIGN,
					       search_base, search_end);
	if (!crash_base) {
		/*
		 * For crashkernel=size[KMG]@offset[KMG], print out failure
		 * message if can't reserve the specified region.
		 */
		if (fixed_base) {
			pr_warn("crashkernel reservation failed - memory is in use.\n");
			return;
		}

		/*
		 * For crashkernel=size[KMG], if the first attempt was for
		 * low memory, fall back to high memory, the minimum required
		 * low memory will be reserved later.
		 */
		if (!high && search_end == CRASH_ADDR_LOW_MAX) {
			search_end = CRASH_ADDR_HIGH_MAX;
			search_base = CRASH_ADDR_LOW_MAX;
			crash_low_size = DEFAULT_CRASH_KERNEL_LOW_SIZE;
			goto retry;
		}

		/*
		 * For crashkernel=size[KMG],high, if the first attempt was
		 * for high memory, fall back to low memory.
		 */
		if (high && search_end == CRASH_ADDR_HIGH_MAX) {
			search_end = CRASH_ADDR_LOW_MAX;
			search_base = 0;
			if (search_end != CRASH_ADDR_HIGH_MAX)
				goto retry;
		}
		pr_warn("cannot allocate crashkernel (size:0x%llx)\n",
			crash_size);
		return;
	}

	if ((crash_base >= CRASH_ADDR_LOW_MAX) &&
	     crash_low_size && reserve_crashkernel_low(crash_low_size)) {
		memblock_phys_free(crash_base, crash_size);
		return;
	}

	pr_info("crashkernel reserved: 0x%016llx - 0x%016llx (%lld MB)\n",
		crash_base, crash_base + crash_size, crash_size >> 20);

	/*
	 * The crashkernel memory will be removed from the kernel linear
	 * map. Inform kmemleak so that it won't try to access it.
	 */
	kmemleak_ignore_phys(crash_base);
	if (crashk_low_res.end)
		kmemleak_ignore_phys(crashk_low_res.start);

	crashk_res.start = crash_base;
	crashk_res.end = crash_base + crash_size - 1;
#ifdef HAVE_ARCH_ADD_CRASH_RES_TO_IOMEM_EARLY
	insert_resource(&iomem_resource, &crashk_res);
#endif
}

#ifndef HAVE_ARCH_ADD_CRASH_RES_TO_IOMEM_EARLY
static __init int insert_crashkernel_resources(void)
{
	if (crashk_res.start < crashk_res.end)
		insert_resource(&iomem_resource, &crashk_res);

	if (crashk_low_res.start < crashk_low_res.end)
		insert_resource(&iomem_resource, &crashk_low_res);

	return 0;
}
early_initcall(insert_crashkernel_resources);
#endif
#endif
