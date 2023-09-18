// SPDX-License-Identifier: GPL-2.0
/*
 * Check for KVM_GET_REG_LIST regressions.
 *
 * Copyright (C) 2020, Red Hat, Inc.
 *
 * When attempting to migrate from a host with an older kernel to a host
 * with a newer kernel we allow the newer kernel on the destination to
 * list new registers with get-reg-list. We assume they'll be unused, at
 * least until the guest reboots, and so they're relatively harmless.
 * However, if the destination host with the newer kernel is missing
 * registers which the source host with the older kernel has, then that's
 * a regression in get-reg-list. This test checks for that regression by
 * checking the current list against a blessed list. We should never have
 * missing registers, but if new ones appear then they can probably be
 * added to the blessed list. A completely new blessed list can be created
 * by running the test with the --list command line argument.
 *
 * The blessed list should be created from the oldest possible kernel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "kvm_util.h"
#include "test_util.h"
#include "processor.h"

static struct kvm_reg_list *reg_list;
static __u64 *blessed_reg, blessed_n;

extern struct vcpu_reg_list *vcpu_configs[];
extern int vcpu_configs_n;

#define for_each_reg(i)								\
	for ((i) = 0; (i) < reg_list->n; ++(i))

#define for_each_reg_filtered(i)						\
	for_each_reg(i)								\
		if (!filter_reg(reg_list->reg[i]))

#define for_each_missing_reg(i)							\
	for ((i) = 0; (i) < blessed_n; ++(i))					\
		if (!find_reg(reg_list->reg, reg_list->n, blessed_reg[i]))	\
			if (check_supported_reg(vcpu, blessed_reg[i]))

#define for_each_new_reg(i)							\
	for_each_reg_filtered(i)						\
		if (!find_reg(blessed_reg, blessed_n, reg_list->reg[i]))

#define for_each_present_blessed_reg(i)						\
	for_each_reg(i)								\
		if (find_reg(blessed_reg, blessed_n, reg_list->reg[i]))

static const char *config_name(struct vcpu_reg_list *c)
{
	struct vcpu_reg_sublist *s;
	int len = 0;

	if (c->name)
		return c->name;

	for_each_sublist(c, s)
		len += strlen(s->name) + 1;

	c->name = malloc(len);

	len = 0;
	for_each_sublist(c, s) {
		if (!strcmp(s->name, "base"))
			continue;
		strcat(c->name + len, s->name);
		len += strlen(s->name) + 1;
		c->name[len - 1] = '+';
	}
	c->name[len - 1] = '\0';

	return c->name;
}

bool __weak check_supported_reg(struct kvm_vcpu *vcpu, __u64 reg)
{
	return true;
}

bool __weak filter_reg(__u64 reg)
{
	return false;
}

static bool find_reg(__u64 regs[], __u64 nr_regs, __u64 reg)
{
	int i;

	for (i = 0; i < nr_regs; ++i)
		if (reg == regs[i])
			return true;
	return false;
}

void __weak print_reg(const char *prefix, __u64 id)
{
	printf("\t0x%llx,\n", id);
}

bool __weak check_reject_set(int err)
{
	return true;
}

void __weak finalize_vcpu(struct kvm_vcpu *vcpu, struct vcpu_reg_list *c)
{
}

#ifdef __aarch64__
static void prepare_vcpu_init(struct vcpu_reg_list *c, struct kvm_vcpu_init *init)
{
	struct vcpu_reg_sublist *s;

	for_each_sublist(c, s)
		if (s->capability)
			init->features[s->feature / 32] |= 1 << (s->feature % 32);
}

static struct kvm_vcpu *vcpu_config_get_vcpu(struct vcpu_reg_list *c, struct kvm_vm *vm)
{
	struct kvm_vcpu_init init = { .target = -1, };
	struct kvm_vcpu *vcpu;

	prepare_vcpu_init(c, &init);
	vcpu = __vm_vcpu_add(vm, 0);
	aarch64_vcpu_setup(vcpu, &init);

	return vcpu;
}
#else
static struct kvm_vcpu *vcpu_config_get_vcpu(struct vcpu_reg_list *c, struct kvm_vm *vm)
{
	return __vm_vcpu_add(vm, 0);
}
#endif

static void check_supported(struct vcpu_reg_list *c)
{
	struct vcpu_reg_sublist *s;

	for_each_sublist(c, s) {
		if (!s->capability)
			continue;

		__TEST_REQUIRE(kvm_has_cap(s->capability),
			       "%s: %s not available, skipping tests\n",
			       config_name(c), s->name);
	}
}

static bool print_list;
static bool print_filtered;

static void run_test(struct vcpu_reg_list *c)
{
	int new_regs = 0, missing_regs = 0, i, n;
	int failed_get = 0, failed_set = 0, failed_reject = 0;
	int skipped_set = 0;
	struct kvm_vcpu *vcpu;
	struct kvm_vm *vm;
	struct vcpu_reg_sublist *s;

	check_supported(c);

	vm = vm_create_barebones();
	vcpu = vcpu_config_get_vcpu(c, vm);
	finalize_vcpu(vcpu, c);

	reg_list = vcpu_get_reg_list(vcpu);

	if (print_list || print_filtered) {
		putchar('\n');
		for_each_reg(i) {
			__u64 id = reg_list->reg[i];
			if ((print_list && !filter_reg(id)) ||
			    (print_filtered && filter_reg(id)))
				print_reg(config_name(c), id);
		}
		putchar('\n');
		return;
	}

	for_each_sublist(c, s)
		blessed_n += s->regs_n;
	blessed_reg = calloc(blessed_n, sizeof(__u64));

	n = 0;
	for_each_sublist(c, s) {
		for (i = 0; i < s->regs_n; ++i)
			blessed_reg[n++] = s->regs[i];
	}

	/*
	 * We only test that we can get the register and then write back the
	 * same value. Some registers may allow other values to be written
	 * back, but others only allow some bits to be changed, and at least
	 * for ID registers set will fail if the value does not exactly match
	 * what was returned by get. If registers that allow other values to
	 * be written need to have the other values tested, then we should
	 * create a new set of tests for those in a new independent test
	 * executable.
	 *
	 * Only do the get/set tests on present, blessed list registers,
	 * since we don't know the capabilities of any new registers.
	 */
	for_each_present_blessed_reg(i) {
		uint8_t addr[2048 / 8];
		struct kvm_one_reg reg = {
			.id = reg_list->reg[i],
			.addr = (__u64)&addr,
		};
		bool reject_reg = false, skip_reg = false;
		int ret;

		ret = __vcpu_get_reg(vcpu, reg_list->reg[i], &addr);
		if (ret) {
			printf("%s: Failed to get ", config_name(c));
			print_reg(config_name(c), reg.id);
			putchar('\n');
			++failed_get;
		}

		for_each_sublist(c, s) {
			/* rejects_set registers are rejected for set operation */
			if (s->rejects_set && find_reg(s->rejects_set, s->rejects_set_n, reg.id)) {
				reject_reg = true;
				ret = __vcpu_ioctl(vcpu, KVM_SET_ONE_REG, &reg);
				if (ret != -1 || !check_reject_set(errno)) {
					printf("%s: Failed to reject (ret=%d, errno=%d) ", config_name(c), ret, errno);
					print_reg(config_name(c), reg.id);
					putchar('\n');
					++failed_reject;
				}
				break;
			}

			/* skips_set registers are skipped for set operation */
			if (s->skips_set && find_reg(s->skips_set, s->skips_set_n, reg.id)) {
				skip_reg = true;
				++skipped_set;
				break;
			}
		}

		if (!reject_reg && !skip_reg) {
			ret = __vcpu_ioctl(vcpu, KVM_SET_ONE_REG, &reg);
			if (ret) {
				printf("%s: Failed to set ", config_name(c));
				print_reg(config_name(c), reg.id);
				putchar('\n');
				++failed_set;
			}
		}
	}

	for_each_new_reg(i)
		++new_regs;

	for_each_missing_reg(i)
		++missing_regs;

	if (new_regs || missing_regs) {
		n = 0;
		for_each_reg_filtered(i)
			++n;

		printf("%s: Number blessed registers: %5lld\n", config_name(c), blessed_n);
		printf("%s: Number registers:         %5lld (includes %lld filtered registers)\n",
		       config_name(c), reg_list->n, reg_list->n - n);
	}

	if (new_regs) {
		printf("\n%s: There are %d new registers.\n"
		       "Consider adding them to the blessed reg "
		       "list with the following lines:\n\n", config_name(c), new_regs);
		for_each_new_reg(i)
			print_reg(config_name(c), reg_list->reg[i]);
		putchar('\n');
	}

	if (missing_regs) {
		printf("\n%s: There are %d missing registers.\n"
		       "The following lines are missing registers:\n\n", config_name(c), missing_regs);
		for_each_missing_reg(i)
			print_reg(config_name(c), blessed_reg[i]);
		putchar('\n');
	}

	TEST_ASSERT(!missing_regs && !failed_get && !failed_set && !failed_reject,
		    "%s: There are %d missing registers; %d registers failed get; "
		    "%d registers failed set; %d registers failed reject; %d registers skipped set",
		    config_name(c), missing_regs, failed_get, failed_set, failed_reject, skipped_set);

	pr_info("%s: PASS\n", config_name(c));
	blessed_n = 0;
	free(blessed_reg);
	free(reg_list);
	kvm_vm_free(vm);
}

static void help(void)
{
	struct vcpu_reg_list *c;
	int i;

	printf(
	"\n"
	"usage: get-reg-list [--config=<selection>] [--list] [--list-filtered]\n\n"
	" --config=<selection>        Used to select a specific vcpu configuration for the test/listing\n"
	"                             '<selection>' may be\n");

	for (i = 0; i < vcpu_configs_n; ++i) {
		c = vcpu_configs[i];
		printf(
	"                               '%s'\n", config_name(c));
	}

	printf(
	"\n"
	" --list                      Print the register list rather than test it (requires --config)\n"
	" --list-filtered             Print registers that would normally be filtered out (requires --config)\n"
	"\n"
	);
}

static struct vcpu_reg_list *parse_config(const char *config)
{
	struct vcpu_reg_list *c = NULL;
	int i;

	if (config[8] != '=')
		help(), exit(1);

	for (i = 0; i < vcpu_configs_n; ++i) {
		c = vcpu_configs[i];
		if (strcmp(config_name(c), &config[9]) == 0)
			break;
	}

	if (i == vcpu_configs_n)
		help(), exit(1);

	return c;
}

int main(int ac, char **av)
{
	struct vcpu_reg_list *c, *sel = NULL;
	int i, ret = 0;
	pid_t pid;

	for (i = 1; i < ac; ++i) {
		if (strncmp(av[i], "--config", 8) == 0)
			sel = parse_config(av[i]);
		else if (strcmp(av[i], "--list") == 0)
			print_list = true;
		else if (strcmp(av[i], "--list-filtered") == 0)
			print_filtered = true;
		else if (strcmp(av[i], "--help") == 0 || strcmp(av[1], "-h") == 0)
			help(), exit(0);
		else
			help(), exit(1);
	}

	if (print_list || print_filtered) {
		/*
		 * We only want to print the register list of a single config.
		 */
		if (!sel)
			help(), exit(1);
	}

	for (i = 0; i < vcpu_configs_n; ++i) {
		c = vcpu_configs[i];
		if (sel && c != sel)
			continue;

		pid = fork();

		if (!pid) {
			run_test(c);
			exit(0);
		} else {
			int wstatus;
			pid_t wpid = wait(&wstatus);
			TEST_ASSERT(wpid == pid && WIFEXITED(wstatus), "wait: Unexpected return");
			if (WEXITSTATUS(wstatus) && WEXITSTATUS(wstatus) != KSFT_SKIP)
				ret = KSFT_FAIL;
		}
	}

	return ret;
}
