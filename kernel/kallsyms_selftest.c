// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Test the function and performance of kallsyms
 *
 * Copyright (C) Huawei Technologies Co., Ltd., 2022
 *
 * Authors: Zhen Lei <thunder.leizhen@huawei.com> Huawei
 */

#define pr_fmt(fmt) "kallsyms_selftest: " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/random.h>
#include <linux/sched/clock.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>

#include "kallsyms_internal.h"
#include "kallsyms_selftest.h"


#define MAX_NUM_OF_RECORDS		64

struct test_stat {
	int min;
	int max;
	int save_cnt;
	int real_cnt;
	int perf;
	u64 sum;
	char *name;
	unsigned long addr;
	unsigned long addrs[MAX_NUM_OF_RECORDS];
};

struct test_item {
	char *name;
	unsigned long addr;
};

#define ITEM_FUNC(s)				\
	{					\
		.name = #s,			\
		.addr = (unsigned long)s,	\
	}

#define ITEM_DATA(s)				\
	{					\
		.name = #s,			\
		.addr = (unsigned long)&s,	\
	}


static int kallsyms_test_var_bss_static;
static int kallsyms_test_var_data_static = 1;
int kallsyms_test_var_bss;
int kallsyms_test_var_data = 1;

static int kallsyms_test_func_static(void)
{
	kallsyms_test_var_bss_static++;
	kallsyms_test_var_data_static++;

	return 0;
}

int kallsyms_test_func(void)
{
	return kallsyms_test_func_static();
}

__weak int kallsyms_test_func_weak(void)
{
	kallsyms_test_var_bss++;
	kallsyms_test_var_data++;
	return 0;
}

static struct test_item test_items[] = {
	ITEM_FUNC(kallsyms_test_func_static),
	ITEM_FUNC(kallsyms_test_func),
	ITEM_FUNC(kallsyms_test_func_weak),
	ITEM_FUNC(vmalloc_noprof),
	ITEM_FUNC(vfree),
#ifdef CONFIG_KALLSYMS_ALL
	ITEM_DATA(kallsyms_test_var_bss_static),
	ITEM_DATA(kallsyms_test_var_data_static),
	ITEM_DATA(kallsyms_test_var_bss),
	ITEM_DATA(kallsyms_test_var_data),
#endif
};

static char stub_name[KSYM_NAME_LEN];

static int stat_symbol_len(void *data, const char *name, unsigned long addr)
{
	*(u32 *)data += strlen(name);

	return 0;
}

static void test_kallsyms_compression_ratio(void)
{
	u32 pos, off, len, num;
	u32 ratio, total_size, total_len = 0;

	kallsyms_on_each_symbol(stat_symbol_len, &total_len);

	/*
	 * A symbol name cannot start with a number. This stub name helps us
	 * traverse the entire symbol table without finding a match. It's used
	 * for subsequent performance tests, and its length is the average
	 * length of all symbol names.
	 */
	memset(stub_name, '4', sizeof(stub_name));
	pos = total_len / kallsyms_num_syms;
	stub_name[pos] = 0;

	pos = 0;
	num = 0;
	off = 0;
	while (pos < kallsyms_num_syms) {
		len = kallsyms_names[off];
		num++;
		off++;
		pos++;
		if ((len & 0x80) != 0) {
			len = (len & 0x7f) | (kallsyms_names[off] << 7);
			num++;
			off++;
		}
		off += len;
	}

	/*
	 * 1. The length fields is not counted
	 * 2. The memory occupied by array kallsyms_token_table[] and
	 *    kallsyms_token_index[] needs to be counted.
	 */
	total_size = off - num;
	pos = kallsyms_token_index[0xff];
	total_size += pos + strlen(&kallsyms_token_table[pos]) + 1;
	total_size += 0x100 * sizeof(u16);

	pr_info(" ---------------------------------------------------------\n");
	pr_info("| nr_symbols | compressed size | original size | ratio(%%) |\n");
	pr_info("|---------------------------------------------------------|\n");
	ratio = (u32)div_u64(10000ULL * total_size, total_len);
	pr_info("| %10d |    %10d   |   %10d  |  %2d.%-2d   |\n",
		kallsyms_num_syms, total_size, total_len, ratio / 100, ratio % 100);
	pr_info(" ---------------------------------------------------------\n");
}

static int lookup_name(void *data, const char *name, unsigned long addr)
{
	u64 t0, t1, t;
	struct test_stat *stat = (struct test_stat *)data;

	t0 = ktime_get_ns();
	(void)kallsyms_lookup_name(name);
	t1 = ktime_get_ns();

	t = t1 - t0;
	if (t < stat->min)
		stat->min = t;

	if (t > stat->max)
		stat->max = t;

	stat->real_cnt++;
	stat->sum += t;

	return 0;
}

static void test_perf_kallsyms_lookup_name(void)
{
	struct test_stat stat;

	memset(&stat, 0, sizeof(stat));
	stat.min = INT_MAX;
	kallsyms_on_each_symbol(lookup_name, &stat);
	pr_info("kallsyms_lookup_name() looked up %d symbols\n", stat.real_cnt);
	pr_info("The time spent on each symbol is (ns): min=%d, max=%d, avg=%lld\n",
		stat.min, stat.max, div_u64(stat.sum, stat.real_cnt));
}

static int find_symbol(void *data, const char *name, unsigned long addr)
{
	struct test_stat *stat = (struct test_stat *)data;

	if (!strcmp(name, stat->name)) {
		stat->real_cnt++;
		stat->addr = addr;

		if (stat->save_cnt < MAX_NUM_OF_RECORDS) {
			stat->addrs[stat->save_cnt] = addr;
			stat->save_cnt++;
		}

		if (stat->real_cnt == stat->max)
			return 1;
	}

	return 0;
}

static void test_perf_kallsyms_on_each_symbol(void)
{
	u64 t0, t1;
	struct test_stat stat;

	memset(&stat, 0, sizeof(stat));
	stat.max = INT_MAX;
	stat.name = stub_name;
	stat.perf = 1;
	t0 = ktime_get_ns();
	kallsyms_on_each_symbol(find_symbol, &stat);
	t1 = ktime_get_ns();
	pr_info("kallsyms_on_each_symbol() traverse all: %lld ns\n", t1 - t0);
}

static int match_symbol(void *data, unsigned long addr)
{
	struct test_stat *stat = (struct test_stat *)data;

	stat->real_cnt++;
	stat->addr = addr;

	if (stat->save_cnt < MAX_NUM_OF_RECORDS) {
		stat->addrs[stat->save_cnt] = addr;
		stat->save_cnt++;
	}

	if (stat->real_cnt == stat->max)
		return 1;

	return 0;
}

static void test_perf_kallsyms_on_each_match_symbol(void)
{
	u64 t0, t1;
	struct test_stat stat;

	memset(&stat, 0, sizeof(stat));
	stat.max = INT_MAX;
	stat.name = stub_name;
	t0 = ktime_get_ns();
	kallsyms_on_each_match_symbol(match_symbol, stat.name, &stat);
	t1 = ktime_get_ns();
	pr_info("kallsyms_on_each_match_symbol() traverse all: %lld ns\n", t1 - t0);
}

static int test_kallsyms_basic_function(void)
{
	int i, j, ret;
	int next = 0, nr_failed = 0;
	char *prefix;
	unsigned short rand;
	unsigned long addr, lookup_addr;
	char namebuf[KSYM_NAME_LEN];
	struct test_stat *stat, *stat2;

	stat = kmalloc(sizeof(*stat) * 2, GFP_KERNEL);
	if (!stat)
		return -ENOMEM;
	stat2 = stat + 1;

	prefix = "kallsyms_lookup_name() for";
	for (i = 0; i < ARRAY_SIZE(test_items); i++) {
		addr = kallsyms_lookup_name(test_items[i].name);
		if (addr != test_items[i].addr) {
			nr_failed++;
			pr_info("%s %s failed: addr=%lx, expect %lx\n",
				prefix, test_items[i].name, addr, test_items[i].addr);
		}
	}

	prefix = "kallsyms_on_each_symbol() for";
	for (i = 0; i < ARRAY_SIZE(test_items); i++) {
		memset(stat, 0, sizeof(*stat));
		stat->max = INT_MAX;
		stat->name = test_items[i].name;
		kallsyms_on_each_symbol(find_symbol, stat);
		if (stat->addr != test_items[i].addr || stat->real_cnt != 1) {
			nr_failed++;
			pr_info("%s %s failed: count=%d, addr=%lx, expect %lx\n",
				prefix, test_items[i].name,
				stat->real_cnt, stat->addr, test_items[i].addr);
		}
	}

	prefix = "kallsyms_on_each_match_symbol() for";
	for (i = 0; i < ARRAY_SIZE(test_items); i++) {
		memset(stat, 0, sizeof(*stat));
		stat->max = INT_MAX;
		stat->name = test_items[i].name;
		kallsyms_on_each_match_symbol(match_symbol, test_items[i].name, stat);
		if (stat->addr != test_items[i].addr || stat->real_cnt != 1) {
			nr_failed++;
			pr_info("%s %s failed: count=%d, addr=%lx, expect %lx\n",
				prefix, test_items[i].name,
				stat->real_cnt, stat->addr, test_items[i].addr);
		}
	}

	if (nr_failed) {
		kfree(stat);
		return -ESRCH;
	}

	for (i = 0; i < kallsyms_num_syms; i++) {
		addr = kallsyms_sym_address(i);
		if (!is_ksym_addr(addr))
			continue;

		ret = lookup_symbol_name(addr, namebuf);
		if (unlikely(ret)) {
			namebuf[0] = 0;
			pr_info("%d: lookup_symbol_name(%lx) failed\n", i, addr);
			goto failed;
		}

		lookup_addr = kallsyms_lookup_name(namebuf);

		memset(stat, 0, sizeof(*stat));
		stat->max = INT_MAX;
		kallsyms_on_each_match_symbol(match_symbol, namebuf, stat);

		/*
		 * kallsyms_on_each_symbol() is too slow, randomly select some
		 * symbols for test.
		 */
		if (i >= next) {
			memset(stat2, 0, sizeof(*stat2));
			stat2->max = INT_MAX;
			stat2->name = namebuf;
			kallsyms_on_each_symbol(find_symbol, stat2);

			/*
			 * kallsyms_on_each_symbol() and kallsyms_on_each_match_symbol()
			 * need to get the same traversal result.
			 */
			if (stat->addr != stat2->addr ||
			    stat->real_cnt != stat2->real_cnt ||
			    memcmp(stat->addrs, stat2->addrs,
				   stat->save_cnt * sizeof(stat->addrs[0]))) {
				pr_info("%s: mismatch between kallsyms_on_each_symbol() and kallsyms_on_each_match_symbol()\n",
					namebuf);
				goto failed;
			}

			/*
			 * The average of random increments is 128, that is, one of
			 * them is tested every 128 symbols.
			 */
			get_random_bytes(&rand, sizeof(rand));
			next = i + (rand & 0xff) + 1;
		}

		/* Need to be found at least once */
		if (!stat->real_cnt) {
			pr_info("%s: Never found\n", namebuf);
			goto failed;
		}

		/*
		 * kallsyms_lookup_name() returns the address of the first
		 * symbol found and cannot be NULL.
		 */
		if (!lookup_addr) {
			pr_info("%s: NULL lookup_addr?!\n", namebuf);
			goto failed;
		}
		if (lookup_addr != stat->addrs[0]) {
			pr_info("%s: lookup_addr != stat->addrs[0]\n", namebuf);
			goto failed;
		}

		/*
		 * If the addresses of all matching symbols are recorded, the
		 * target address needs to be exist.
		 */
		if (stat->real_cnt <= MAX_NUM_OF_RECORDS) {
			for (j = 0; j < stat->save_cnt; j++) {
				if (stat->addrs[j] == addr)
					break;
			}

			if (j == stat->save_cnt) {
				pr_info("%s: j == save_cnt?!\n", namebuf);
				goto failed;
			}
		}
	}

	kfree(stat);

	return 0;

failed:
	pr_info("Test for %dth symbol failed: (%s) addr=%lx", i, namebuf, addr);
	kfree(stat);
	return -ESRCH;
}

static int test_entry(void *p)
{
	int ret;

	do {
		schedule_timeout(5 * HZ);
	} while (system_state != SYSTEM_RUNNING);

	pr_info("start\n");
	ret = test_kallsyms_basic_function();
	if (ret) {
		pr_info("abort\n");
		return 0;
	}

	test_kallsyms_compression_ratio();
	test_perf_kallsyms_lookup_name();
	test_perf_kallsyms_on_each_symbol();
	test_perf_kallsyms_on_each_match_symbol();
	pr_info("finish\n");

	return 0;
}

static int __init kallsyms_test_init(void)
{
	struct task_struct *t;

	t = kthread_create(test_entry, NULL, "kallsyms_test");
	if (IS_ERR(t)) {
		pr_info("Create kallsyms selftest task failed\n");
		return PTR_ERR(t);
	}
	kthread_bind(t, 0);
	wake_up_process(t);

	return 0;
}
late_initcall(kallsyms_test_init);
