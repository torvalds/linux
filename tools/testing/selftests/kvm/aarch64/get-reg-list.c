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
 * Note, the blessed list should be created from the oldest possible
 * kernel. We can't go older than v4.15, though, because that's the first
 * release to expose the ID system registers in KVM_GET_REG_LIST, see
 * commit 93390c0a1b20 ("arm64: KVM: Hide unsupported AArch64 CPU features
 * from guests"). Also, one must use the --core-reg-fixup command line
 * option when running on an older kernel that doesn't include df205b5c6328
 * ("KVM: arm64: Filter out invalid core register IDs in KVM_GET_REG_LIST")
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

struct reg_sublist {
	const char *name;
	long capability;
	int feature;
	bool finalize;
	__u64 *regs;
	__u64 regs_n;
	__u64 *rejects_set;
	__u64 rejects_set_n;
};

struct vcpu_config {
	char *name;
	struct reg_sublist sublists[];
};

static struct vcpu_config *vcpu_configs[];
static int vcpu_configs_n;

#define for_each_sublist(c, s)							\
	for ((s) = &(c)->sublists[0]; (s)->regs; ++(s))

#define for_each_reg(i)								\
	for ((i) = 0; (i) < reg_list->n; ++(i))

#define for_each_reg_filtered(i)						\
	for_each_reg(i)								\
		if (!filter_reg(reg_list->reg[i]))

#define for_each_missing_reg(i)							\
	for ((i) = 0; (i) < blessed_n; ++(i))					\
		if (!find_reg(reg_list->reg, reg_list->n, blessed_reg[i]))

#define for_each_new_reg(i)							\
	for_each_reg_filtered(i)						\
		if (!find_reg(blessed_reg, blessed_n, reg_list->reg[i]))

static const char *config_name(struct vcpu_config *c)
{
	struct reg_sublist *s;
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

static bool has_cap(struct vcpu_config *c, long capability)
{
	struct reg_sublist *s;

	for_each_sublist(c, s)
		if (s->capability == capability)
			return true;
	return false;
}

static bool filter_reg(__u64 reg)
{
	/*
	 * DEMUX register presence depends on the host's CLIDR_EL1.
	 * This means there's no set of them that we can bless.
	 */
	if ((reg & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return true;

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

static const char *str_with_index(const char *template, __u64 index)
{
	char *str, *p;
	int n;

	str = strdup(template);
	p = strstr(str, "##");
	n = sprintf(p, "%lld", index);
	strcat(p + n, strstr(template, "##") + 2);

	return (const char *)str;
}

#define REG_MASK (KVM_REG_ARCH_MASK | KVM_REG_SIZE_MASK | KVM_REG_ARM_COPROC_MASK)

#define CORE_REGS_XX_NR_WORDS	2
#define CORE_SPSR_XX_NR_WORDS	2
#define CORE_FPREGS_XX_NR_WORDS	4

static const char *core_id_to_str(struct vcpu_config *c, __u64 id)
{
	__u64 core_off = id & ~REG_MASK, idx;

	/*
	 * core_off is the offset into struct kvm_regs
	 */
	switch (core_off) {
	case KVM_REG_ARM_CORE_REG(regs.regs[0]) ...
	     KVM_REG_ARM_CORE_REG(regs.regs[30]):
		idx = (core_off - KVM_REG_ARM_CORE_REG(regs.regs[0])) / CORE_REGS_XX_NR_WORDS;
		TEST_ASSERT(idx < 31, "%s: Unexpected regs.regs index: %lld", config_name(c), idx);
		return str_with_index("KVM_REG_ARM_CORE_REG(regs.regs[##])", idx);
	case KVM_REG_ARM_CORE_REG(regs.sp):
		return "KVM_REG_ARM_CORE_REG(regs.sp)";
	case KVM_REG_ARM_CORE_REG(regs.pc):
		return "KVM_REG_ARM_CORE_REG(regs.pc)";
	case KVM_REG_ARM_CORE_REG(regs.pstate):
		return "KVM_REG_ARM_CORE_REG(regs.pstate)";
	case KVM_REG_ARM_CORE_REG(sp_el1):
		return "KVM_REG_ARM_CORE_REG(sp_el1)";
	case KVM_REG_ARM_CORE_REG(elr_el1):
		return "KVM_REG_ARM_CORE_REG(elr_el1)";
	case KVM_REG_ARM_CORE_REG(spsr[0]) ...
	     KVM_REG_ARM_CORE_REG(spsr[KVM_NR_SPSR - 1]):
		idx = (core_off - KVM_REG_ARM_CORE_REG(spsr[0])) / CORE_SPSR_XX_NR_WORDS;
		TEST_ASSERT(idx < KVM_NR_SPSR, "%s: Unexpected spsr index: %lld", config_name(c), idx);
		return str_with_index("KVM_REG_ARM_CORE_REG(spsr[##])", idx);
	case KVM_REG_ARM_CORE_REG(fp_regs.vregs[0]) ...
	     KVM_REG_ARM_CORE_REG(fp_regs.vregs[31]):
		idx = (core_off - KVM_REG_ARM_CORE_REG(fp_regs.vregs[0])) / CORE_FPREGS_XX_NR_WORDS;
		TEST_ASSERT(idx < 32, "%s: Unexpected fp_regs.vregs index: %lld", config_name(c), idx);
		return str_with_index("KVM_REG_ARM_CORE_REG(fp_regs.vregs[##])", idx);
	case KVM_REG_ARM_CORE_REG(fp_regs.fpsr):
		return "KVM_REG_ARM_CORE_REG(fp_regs.fpsr)";
	case KVM_REG_ARM_CORE_REG(fp_regs.fpcr):
		return "KVM_REG_ARM_CORE_REG(fp_regs.fpcr)";
	}

	TEST_FAIL("%s: Unknown core reg id: 0x%llx", config_name(c), id);
	return NULL;
}

static const char *sve_id_to_str(struct vcpu_config *c, __u64 id)
{
	__u64 sve_off, n, i;

	if (id == KVM_REG_ARM64_SVE_VLS)
		return "KVM_REG_ARM64_SVE_VLS";

	sve_off = id & ~(REG_MASK | ((1ULL << 5) - 1));
	i = id & (KVM_ARM64_SVE_MAX_SLICES - 1);

	TEST_ASSERT(i == 0, "%s: Currently we don't expect slice > 0, reg id 0x%llx", config_name(c), id);

	switch (sve_off) {
	case KVM_REG_ARM64_SVE_ZREG_BASE ...
	     KVM_REG_ARM64_SVE_ZREG_BASE + (1ULL << 5) * KVM_ARM64_SVE_NUM_ZREGS - 1:
		n = (id >> 5) & (KVM_ARM64_SVE_NUM_ZREGS - 1);
		TEST_ASSERT(id == KVM_REG_ARM64_SVE_ZREG(n, 0),
			    "%s: Unexpected bits set in SVE ZREG id: 0x%llx", config_name(c), id);
		return str_with_index("KVM_REG_ARM64_SVE_ZREG(##, 0)", n);
	case KVM_REG_ARM64_SVE_PREG_BASE ...
	     KVM_REG_ARM64_SVE_PREG_BASE + (1ULL << 5) * KVM_ARM64_SVE_NUM_PREGS - 1:
		n = (id >> 5) & (KVM_ARM64_SVE_NUM_PREGS - 1);
		TEST_ASSERT(id == KVM_REG_ARM64_SVE_PREG(n, 0),
			    "%s: Unexpected bits set in SVE PREG id: 0x%llx", config_name(c), id);
		return str_with_index("KVM_REG_ARM64_SVE_PREG(##, 0)", n);
	case KVM_REG_ARM64_SVE_FFR_BASE:
		TEST_ASSERT(id == KVM_REG_ARM64_SVE_FFR(0),
			    "%s: Unexpected bits set in SVE FFR id: 0x%llx", config_name(c), id);
		return "KVM_REG_ARM64_SVE_FFR(0)";
	}

	return NULL;
}

static void print_reg(struct vcpu_config *c, __u64 id)
{
	unsigned op0, op1, crn, crm, op2;
	const char *reg_size = NULL;

	TEST_ASSERT((id & KVM_REG_ARCH_MASK) == KVM_REG_ARM64,
		    "%s: KVM_REG_ARM64 missing in reg id: 0x%llx", config_name(c), id);

	switch (id & KVM_REG_SIZE_MASK) {
	case KVM_REG_SIZE_U8:
		reg_size = "KVM_REG_SIZE_U8";
		break;
	case KVM_REG_SIZE_U16:
		reg_size = "KVM_REG_SIZE_U16";
		break;
	case KVM_REG_SIZE_U32:
		reg_size = "KVM_REG_SIZE_U32";
		break;
	case KVM_REG_SIZE_U64:
		reg_size = "KVM_REG_SIZE_U64";
		break;
	case KVM_REG_SIZE_U128:
		reg_size = "KVM_REG_SIZE_U128";
		break;
	case KVM_REG_SIZE_U256:
		reg_size = "KVM_REG_SIZE_U256";
		break;
	case KVM_REG_SIZE_U512:
		reg_size = "KVM_REG_SIZE_U512";
		break;
	case KVM_REG_SIZE_U1024:
		reg_size = "KVM_REG_SIZE_U1024";
		break;
	case KVM_REG_SIZE_U2048:
		reg_size = "KVM_REG_SIZE_U2048";
		break;
	default:
		TEST_FAIL("%s: Unexpected reg size: 0x%llx in reg id: 0x%llx",
			  config_name(c), (id & KVM_REG_SIZE_MASK) >> KVM_REG_SIZE_SHIFT, id);
	}

	switch (id & KVM_REG_ARM_COPROC_MASK) {
	case KVM_REG_ARM_CORE:
		printf("\tKVM_REG_ARM64 | %s | KVM_REG_ARM_CORE | %s,\n", reg_size, core_id_to_str(c, id));
		break;
	case KVM_REG_ARM_DEMUX:
		TEST_ASSERT(!(id & ~(REG_MASK | KVM_REG_ARM_DEMUX_ID_MASK | KVM_REG_ARM_DEMUX_VAL_MASK)),
			    "%s: Unexpected bits set in DEMUX reg id: 0x%llx", config_name(c), id);
		printf("\tKVM_REG_ARM64 | %s | KVM_REG_ARM_DEMUX | KVM_REG_ARM_DEMUX_ID_CCSIDR | %lld,\n",
		       reg_size, id & KVM_REG_ARM_DEMUX_VAL_MASK);
		break;
	case KVM_REG_ARM64_SYSREG:
		op0 = (id & KVM_REG_ARM64_SYSREG_OP0_MASK) >> KVM_REG_ARM64_SYSREG_OP0_SHIFT;
		op1 = (id & KVM_REG_ARM64_SYSREG_OP1_MASK) >> KVM_REG_ARM64_SYSREG_OP1_SHIFT;
		crn = (id & KVM_REG_ARM64_SYSREG_CRN_MASK) >> KVM_REG_ARM64_SYSREG_CRN_SHIFT;
		crm = (id & KVM_REG_ARM64_SYSREG_CRM_MASK) >> KVM_REG_ARM64_SYSREG_CRM_SHIFT;
		op2 = (id & KVM_REG_ARM64_SYSREG_OP2_MASK) >> KVM_REG_ARM64_SYSREG_OP2_SHIFT;
		TEST_ASSERT(id == ARM64_SYS_REG(op0, op1, crn, crm, op2),
			    "%s: Unexpected bits set in SYSREG reg id: 0x%llx", config_name(c), id);
		printf("\tARM64_SYS_REG(%d, %d, %d, %d, %d),\n", op0, op1, crn, crm, op2);
		break;
	case KVM_REG_ARM_FW:
		TEST_ASSERT(id == KVM_REG_ARM_FW_REG(id & 0xffff),
			    "%s: Unexpected bits set in FW reg id: 0x%llx", config_name(c), id);
		printf("\tKVM_REG_ARM_FW_REG(%lld),\n", id & 0xffff);
		break;
	case KVM_REG_ARM64_SVE:
		if (has_cap(c, KVM_CAP_ARM_SVE))
			printf("\t%s,\n", sve_id_to_str(c, id));
		else
			TEST_FAIL("%s: KVM_REG_ARM64_SVE is an unexpected coproc type in reg id: 0x%llx", config_name(c), id);
		break;
	default:
		TEST_FAIL("%s: Unexpected coproc type: 0x%llx in reg id: 0x%llx",
			  config_name(c), (id & KVM_REG_ARM_COPROC_MASK) >> KVM_REG_ARM_COPROC_SHIFT, id);
	}
}

/*
 * Older kernels listed each 32-bit word of CORE registers separately.
 * For 64 and 128-bit registers we need to ignore the extra words. We
 * also need to fixup the sizes, because the older kernels stated all
 * registers were 64-bit, even when they weren't.
 */
static void core_reg_fixup(void)
{
	struct kvm_reg_list *tmp;
	__u64 id, core_off;
	int i;

	tmp = calloc(1, sizeof(*tmp) + reg_list->n * sizeof(__u64));

	for (i = 0; i < reg_list->n; ++i) {
		id = reg_list->reg[i];

		if ((id & KVM_REG_ARM_COPROC_MASK) != KVM_REG_ARM_CORE) {
			tmp->reg[tmp->n++] = id;
			continue;
		}

		core_off = id & ~REG_MASK;

		switch (core_off) {
		case 0x52: case 0xd2: case 0xd6:
			/*
			 * These offsets are pointing at padding.
			 * We need to ignore them too.
			 */
			continue;
		case KVM_REG_ARM_CORE_REG(fp_regs.vregs[0]) ...
		     KVM_REG_ARM_CORE_REG(fp_regs.vregs[31]):
			if (core_off & 3)
				continue;
			id &= ~KVM_REG_SIZE_MASK;
			id |= KVM_REG_SIZE_U128;
			tmp->reg[tmp->n++] = id;
			continue;
		case KVM_REG_ARM_CORE_REG(fp_regs.fpsr):
		case KVM_REG_ARM_CORE_REG(fp_regs.fpcr):
			id &= ~KVM_REG_SIZE_MASK;
			id |= KVM_REG_SIZE_U32;
			tmp->reg[tmp->n++] = id;
			continue;
		default:
			if (core_off & 1)
				continue;
			tmp->reg[tmp->n++] = id;
			break;
		}
	}

	free(reg_list);
	reg_list = tmp;
}

static void prepare_vcpu_init(struct vcpu_config *c, struct kvm_vcpu_init *init)
{
	struct reg_sublist *s;

	for_each_sublist(c, s)
		if (s->capability)
			init->features[s->feature / 32] |= 1 << (s->feature % 32);
}

static void finalize_vcpu(struct kvm_vm *vm, uint32_t vcpuid, struct vcpu_config *c)
{
	struct reg_sublist *s;
	int feature;

	for_each_sublist(c, s) {
		if (s->finalize) {
			feature = s->feature;
			vcpu_ioctl(vm, vcpuid, KVM_ARM_VCPU_FINALIZE, &feature);
		}
	}
}

static void check_supported(struct vcpu_config *c)
{
	struct reg_sublist *s;

	for_each_sublist(c, s) {
		if (s->capability && !kvm_check_cap(s->capability)) {
			fprintf(stderr, "%s: %s not available, skipping tests\n", config_name(c), s->name);
			exit(KSFT_SKIP);
		}
	}
}

static bool print_list;
static bool print_filtered;
static bool fixup_core_regs;

static void run_test(struct vcpu_config *c)
{
	struct kvm_vcpu_init init = { .target = -1, };
	int new_regs = 0, missing_regs = 0, i, n;
	int failed_get = 0, failed_set = 0, failed_reject = 0;
	struct kvm_vm *vm;
	struct reg_sublist *s;

	check_supported(c);

	vm = vm_create(VM_MODE_DEFAULT, DEFAULT_GUEST_PHY_PAGES, O_RDWR);
	prepare_vcpu_init(c, &init);
	aarch64_vcpu_add_default(vm, 0, &init, NULL);
	finalize_vcpu(vm, 0, c);

	reg_list = vcpu_get_reg_list(vm, 0);

	if (fixup_core_regs)
		core_reg_fixup();

	if (print_list || print_filtered) {
		putchar('\n');
		for_each_reg(i) {
			__u64 id = reg_list->reg[i];
			if ((print_list && !filter_reg(id)) ||
			    (print_filtered && filter_reg(id)))
				print_reg(c, id);
		}
		putchar('\n');
		return;
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
	 */
	for_each_reg(i) {
		uint8_t addr[2048 / 8];
		struct kvm_one_reg reg = {
			.id = reg_list->reg[i],
			.addr = (__u64)&addr,
		};
		bool reject_reg = false;
		int ret;

		ret = _vcpu_ioctl(vm, 0, KVM_GET_ONE_REG, &reg);
		if (ret) {
			printf("%s: Failed to get ", config_name(c));
			print_reg(c, reg.id);
			putchar('\n');
			++failed_get;
		}

		/* rejects_set registers are rejected after KVM_ARM_VCPU_FINALIZE */
		for_each_sublist(c, s) {
			if (s->rejects_set && find_reg(s->rejects_set, s->rejects_set_n, reg.id)) {
				reject_reg = true;
				ret = _vcpu_ioctl(vm, 0, KVM_SET_ONE_REG, &reg);
				if (ret != -1 || errno != EPERM) {
					printf("%s: Failed to reject (ret=%d, errno=%d) ", config_name(c), ret, errno);
					print_reg(c, reg.id);
					putchar('\n');
					++failed_reject;
				}
				break;
			}
		}

		if (!reject_reg) {
			ret = _vcpu_ioctl(vm, 0, KVM_SET_ONE_REG, &reg);
			if (ret) {
				printf("%s: Failed to set ", config_name(c));
				print_reg(c, reg.id);
				putchar('\n');
				++failed_set;
			}
		}
	}

	for_each_sublist(c, s)
		blessed_n += s->regs_n;
	blessed_reg = calloc(blessed_n, sizeof(__u64));

	n = 0;
	for_each_sublist(c, s) {
		for (i = 0; i < s->regs_n; ++i)
			blessed_reg[n++] = s->regs[i];
	}

	for_each_new_reg(i)
		++new_regs;

	for_each_missing_reg(i)
		++missing_regs;

	if (new_regs || missing_regs) {
		printf("%s: Number blessed registers: %5lld\n", config_name(c), blessed_n);
		printf("%s: Number registers:         %5lld\n", config_name(c), reg_list->n);
	}

	if (new_regs) {
		printf("\n%s: There are %d new registers.\n"
		       "Consider adding them to the blessed reg "
		       "list with the following lines:\n\n", config_name(c), new_regs);
		for_each_new_reg(i)
			print_reg(c, reg_list->reg[i]);
		putchar('\n');
	}

	if (missing_regs) {
		printf("\n%s: There are %d missing registers.\n"
		       "The following lines are missing registers:\n\n", config_name(c), missing_regs);
		for_each_missing_reg(i)
			print_reg(c, blessed_reg[i]);
		putchar('\n');
	}

	TEST_ASSERT(!missing_regs && !failed_get && !failed_set && !failed_reject,
		    "%s: There are %d missing registers; "
		    "%d registers failed get; %d registers failed set; %d registers failed reject",
		    config_name(c), missing_regs, failed_get, failed_set, failed_reject);

	pr_info("%s: PASS\n", config_name(c));
	blessed_n = 0;
	free(blessed_reg);
	free(reg_list);
	kvm_vm_free(vm);
}

static void help(void)
{
	struct vcpu_config *c;
	int i;

	printf(
	"\n"
	"usage: get-reg-list [--config=<selection>] [--list] [--list-filtered] [--core-reg-fixup]\n\n"
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
	" --core-reg-fixup            Needed when running on old kernels with broken core reg listings\n"
	"\n"
	);
}

static struct vcpu_config *parse_config(const char *config)
{
	struct vcpu_config *c;
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
	struct vcpu_config *c, *sel = NULL;
	int i, ret = 0;
	pid_t pid;

	for (i = 1; i < ac; ++i) {
		if (strcmp(av[i], "--core-reg-fixup") == 0)
			fixup_core_regs = true;
		else if (strncmp(av[i], "--config", 8) == 0)
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

/*
 * The current blessed list was primed with the output of kernel version
 * v4.15 with --core-reg-fixup and then later updated with new registers.
 *
 * The blessed list is up to date with kernel version v5.13-rc3
 */
static __u64 base_regs[] = {
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[0]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[1]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[2]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[3]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[4]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[5]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[6]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[7]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[8]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[9]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[10]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[11]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[12]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[13]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[14]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[15]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[16]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[17]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[18]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[19]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[20]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[21]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[22]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[23]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[24]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[25]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[26]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[27]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[28]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[29]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.regs[30]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.sp),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.pc),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(regs.pstate),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(sp_el1),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(elr_el1),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(spsr[0]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(spsr[1]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(spsr[2]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(spsr[3]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(spsr[4]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U32 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.fpsr),
	KVM_REG_ARM64 | KVM_REG_SIZE_U32 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.fpcr),
	KVM_REG_ARM_FW_REG(0),
	KVM_REG_ARM_FW_REG(1),
	KVM_REG_ARM_FW_REG(2),
	ARM64_SYS_REG(3, 3, 14, 3, 1),	/* CNTV_CTL_EL0 */
	ARM64_SYS_REG(3, 3, 14, 3, 2),	/* CNTV_CVAL_EL0 */
	ARM64_SYS_REG(3, 3, 14, 0, 2),
	ARM64_SYS_REG(3, 0, 0, 0, 0),	/* MIDR_EL1 */
	ARM64_SYS_REG(3, 0, 0, 0, 6),	/* REVIDR_EL1 */
	ARM64_SYS_REG(3, 1, 0, 0, 1),	/* CLIDR_EL1 */
	ARM64_SYS_REG(3, 1, 0, 0, 7),	/* AIDR_EL1 */
	ARM64_SYS_REG(3, 3, 0, 0, 1),	/* CTR_EL0 */
	ARM64_SYS_REG(2, 0, 0, 0, 4),
	ARM64_SYS_REG(2, 0, 0, 0, 5),
	ARM64_SYS_REG(2, 0, 0, 0, 6),
	ARM64_SYS_REG(2, 0, 0, 0, 7),
	ARM64_SYS_REG(2, 0, 0, 1, 4),
	ARM64_SYS_REG(2, 0, 0, 1, 5),
	ARM64_SYS_REG(2, 0, 0, 1, 6),
	ARM64_SYS_REG(2, 0, 0, 1, 7),
	ARM64_SYS_REG(2, 0, 0, 2, 0),	/* MDCCINT_EL1 */
	ARM64_SYS_REG(2, 0, 0, 2, 2),	/* MDSCR_EL1 */
	ARM64_SYS_REG(2, 0, 0, 2, 4),
	ARM64_SYS_REG(2, 0, 0, 2, 5),
	ARM64_SYS_REG(2, 0, 0, 2, 6),
	ARM64_SYS_REG(2, 0, 0, 2, 7),
	ARM64_SYS_REG(2, 0, 0, 3, 4),
	ARM64_SYS_REG(2, 0, 0, 3, 5),
	ARM64_SYS_REG(2, 0, 0, 3, 6),
	ARM64_SYS_REG(2, 0, 0, 3, 7),
	ARM64_SYS_REG(2, 0, 0, 4, 4),
	ARM64_SYS_REG(2, 0, 0, 4, 5),
	ARM64_SYS_REG(2, 0, 0, 4, 6),
	ARM64_SYS_REG(2, 0, 0, 4, 7),
	ARM64_SYS_REG(2, 0, 0, 5, 4),
	ARM64_SYS_REG(2, 0, 0, 5, 5),
	ARM64_SYS_REG(2, 0, 0, 5, 6),
	ARM64_SYS_REG(2, 0, 0, 5, 7),
	ARM64_SYS_REG(2, 0, 0, 6, 4),
	ARM64_SYS_REG(2, 0, 0, 6, 5),
	ARM64_SYS_REG(2, 0, 0, 6, 6),
	ARM64_SYS_REG(2, 0, 0, 6, 7),
	ARM64_SYS_REG(2, 0, 0, 7, 4),
	ARM64_SYS_REG(2, 0, 0, 7, 5),
	ARM64_SYS_REG(2, 0, 0, 7, 6),
	ARM64_SYS_REG(2, 0, 0, 7, 7),
	ARM64_SYS_REG(2, 0, 0, 8, 4),
	ARM64_SYS_REG(2, 0, 0, 8, 5),
	ARM64_SYS_REG(2, 0, 0, 8, 6),
	ARM64_SYS_REG(2, 0, 0, 8, 7),
	ARM64_SYS_REG(2, 0, 0, 9, 4),
	ARM64_SYS_REG(2, 0, 0, 9, 5),
	ARM64_SYS_REG(2, 0, 0, 9, 6),
	ARM64_SYS_REG(2, 0, 0, 9, 7),
	ARM64_SYS_REG(2, 0, 0, 10, 4),
	ARM64_SYS_REG(2, 0, 0, 10, 5),
	ARM64_SYS_REG(2, 0, 0, 10, 6),
	ARM64_SYS_REG(2, 0, 0, 10, 7),
	ARM64_SYS_REG(2, 0, 0, 11, 4),
	ARM64_SYS_REG(2, 0, 0, 11, 5),
	ARM64_SYS_REG(2, 0, 0, 11, 6),
	ARM64_SYS_REG(2, 0, 0, 11, 7),
	ARM64_SYS_REG(2, 0, 0, 12, 4),
	ARM64_SYS_REG(2, 0, 0, 12, 5),
	ARM64_SYS_REG(2, 0, 0, 12, 6),
	ARM64_SYS_REG(2, 0, 0, 12, 7),
	ARM64_SYS_REG(2, 0, 0, 13, 4),
	ARM64_SYS_REG(2, 0, 0, 13, 5),
	ARM64_SYS_REG(2, 0, 0, 13, 6),
	ARM64_SYS_REG(2, 0, 0, 13, 7),
	ARM64_SYS_REG(2, 0, 0, 14, 4),
	ARM64_SYS_REG(2, 0, 0, 14, 5),
	ARM64_SYS_REG(2, 0, 0, 14, 6),
	ARM64_SYS_REG(2, 0, 0, 14, 7),
	ARM64_SYS_REG(2, 0, 0, 15, 4),
	ARM64_SYS_REG(2, 0, 0, 15, 5),
	ARM64_SYS_REG(2, 0, 0, 15, 6),
	ARM64_SYS_REG(2, 0, 0, 15, 7),
	ARM64_SYS_REG(2, 4, 0, 7, 0),	/* DBGVCR32_EL2 */
	ARM64_SYS_REG(3, 0, 0, 0, 5),	/* MPIDR_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 0),	/* ID_PFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 1),	/* ID_PFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 2),	/* ID_DFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 3),	/* ID_AFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 4),	/* ID_MMFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 5),	/* ID_MMFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 6),	/* ID_MMFR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 1, 7),	/* ID_MMFR3_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 0),	/* ID_ISAR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 1),	/* ID_ISAR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 2),	/* ID_ISAR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 3),	/* ID_ISAR3_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 4),	/* ID_ISAR4_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 5),	/* ID_ISAR5_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 6),	/* ID_MMFR4_EL1 */
	ARM64_SYS_REG(3, 0, 0, 2, 7),	/* ID_ISAR6_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 0),	/* MVFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 1),	/* MVFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 2),	/* MVFR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 3),
	ARM64_SYS_REG(3, 0, 0, 3, 4),	/* ID_PFR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 5),	/* ID_DFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 6),	/* ID_MMFR5_EL1 */
	ARM64_SYS_REG(3, 0, 0, 3, 7),
	ARM64_SYS_REG(3, 0, 0, 4, 0),	/* ID_AA64PFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 4, 1),	/* ID_AA64PFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 4, 2),
	ARM64_SYS_REG(3, 0, 0, 4, 3),
	ARM64_SYS_REG(3, 0, 0, 4, 4),	/* ID_AA64ZFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 4, 5),
	ARM64_SYS_REG(3, 0, 0, 4, 6),
	ARM64_SYS_REG(3, 0, 0, 4, 7),
	ARM64_SYS_REG(3, 0, 0, 5, 0),	/* ID_AA64DFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 5, 1),	/* ID_AA64DFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 5, 2),
	ARM64_SYS_REG(3, 0, 0, 5, 3),
	ARM64_SYS_REG(3, 0, 0, 5, 4),	/* ID_AA64AFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 5, 5),	/* ID_AA64AFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 5, 6),
	ARM64_SYS_REG(3, 0, 0, 5, 7),
	ARM64_SYS_REG(3, 0, 0, 6, 0),	/* ID_AA64ISAR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 6, 1),	/* ID_AA64ISAR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 6, 2),
	ARM64_SYS_REG(3, 0, 0, 6, 3),
	ARM64_SYS_REG(3, 0, 0, 6, 4),
	ARM64_SYS_REG(3, 0, 0, 6, 5),
	ARM64_SYS_REG(3, 0, 0, 6, 6),
	ARM64_SYS_REG(3, 0, 0, 6, 7),
	ARM64_SYS_REG(3, 0, 0, 7, 0),	/* ID_AA64MMFR0_EL1 */
	ARM64_SYS_REG(3, 0, 0, 7, 1),	/* ID_AA64MMFR1_EL1 */
	ARM64_SYS_REG(3, 0, 0, 7, 2),	/* ID_AA64MMFR2_EL1 */
	ARM64_SYS_REG(3, 0, 0, 7, 3),
	ARM64_SYS_REG(3, 0, 0, 7, 4),
	ARM64_SYS_REG(3, 0, 0, 7, 5),
	ARM64_SYS_REG(3, 0, 0, 7, 6),
	ARM64_SYS_REG(3, 0, 0, 7, 7),
	ARM64_SYS_REG(3, 0, 1, 0, 0),	/* SCTLR_EL1 */
	ARM64_SYS_REG(3, 0, 1, 0, 1),	/* ACTLR_EL1 */
	ARM64_SYS_REG(3, 0, 1, 0, 2),	/* CPACR_EL1 */
	ARM64_SYS_REG(3, 0, 2, 0, 0),	/* TTBR0_EL1 */
	ARM64_SYS_REG(3, 0, 2, 0, 1),	/* TTBR1_EL1 */
	ARM64_SYS_REG(3, 0, 2, 0, 2),	/* TCR_EL1 */
	ARM64_SYS_REG(3, 0, 5, 1, 0),	/* AFSR0_EL1 */
	ARM64_SYS_REG(3, 0, 5, 1, 1),	/* AFSR1_EL1 */
	ARM64_SYS_REG(3, 0, 5, 2, 0),	/* ESR_EL1 */
	ARM64_SYS_REG(3, 0, 6, 0, 0),	/* FAR_EL1 */
	ARM64_SYS_REG(3, 0, 7, 4, 0),	/* PAR_EL1 */
	ARM64_SYS_REG(3, 0, 10, 2, 0),	/* MAIR_EL1 */
	ARM64_SYS_REG(3, 0, 10, 3, 0),	/* AMAIR_EL1 */
	ARM64_SYS_REG(3, 0, 12, 0, 0),	/* VBAR_EL1 */
	ARM64_SYS_REG(3, 0, 12, 1, 1),	/* DISR_EL1 */
	ARM64_SYS_REG(3, 0, 13, 0, 1),	/* CONTEXTIDR_EL1 */
	ARM64_SYS_REG(3, 0, 13, 0, 4),	/* TPIDR_EL1 */
	ARM64_SYS_REG(3, 0, 14, 1, 0),	/* CNTKCTL_EL1 */
	ARM64_SYS_REG(3, 2, 0, 0, 0),	/* CSSELR_EL1 */
	ARM64_SYS_REG(3, 3, 13, 0, 2),	/* TPIDR_EL0 */
	ARM64_SYS_REG(3, 3, 13, 0, 3),	/* TPIDRRO_EL0 */
	ARM64_SYS_REG(3, 4, 3, 0, 0),	/* DACR32_EL2 */
	ARM64_SYS_REG(3, 4, 5, 0, 1),	/* IFSR32_EL2 */
	ARM64_SYS_REG(3, 4, 5, 3, 0),	/* FPEXC32_EL2 */
};

static __u64 pmu_regs[] = {
	ARM64_SYS_REG(3, 0, 9, 14, 1),	/* PMINTENSET_EL1 */
	ARM64_SYS_REG(3, 0, 9, 14, 2),	/* PMINTENCLR_EL1 */
	ARM64_SYS_REG(3, 3, 9, 12, 0),	/* PMCR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 12, 1),	/* PMCNTENSET_EL0 */
	ARM64_SYS_REG(3, 3, 9, 12, 2),	/* PMCNTENCLR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 12, 3),	/* PMOVSCLR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 12, 4),	/* PMSWINC_EL0 */
	ARM64_SYS_REG(3, 3, 9, 12, 5),	/* PMSELR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 13, 0),	/* PMCCNTR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 14, 0),	/* PMUSERENR_EL0 */
	ARM64_SYS_REG(3, 3, 9, 14, 3),	/* PMOVSSET_EL0 */
	ARM64_SYS_REG(3, 3, 14, 8, 0),
	ARM64_SYS_REG(3, 3, 14, 8, 1),
	ARM64_SYS_REG(3, 3, 14, 8, 2),
	ARM64_SYS_REG(3, 3, 14, 8, 3),
	ARM64_SYS_REG(3, 3, 14, 8, 4),
	ARM64_SYS_REG(3, 3, 14, 8, 5),
	ARM64_SYS_REG(3, 3, 14, 8, 6),
	ARM64_SYS_REG(3, 3, 14, 8, 7),
	ARM64_SYS_REG(3, 3, 14, 9, 0),
	ARM64_SYS_REG(3, 3, 14, 9, 1),
	ARM64_SYS_REG(3, 3, 14, 9, 2),
	ARM64_SYS_REG(3, 3, 14, 9, 3),
	ARM64_SYS_REG(3, 3, 14, 9, 4),
	ARM64_SYS_REG(3, 3, 14, 9, 5),
	ARM64_SYS_REG(3, 3, 14, 9, 6),
	ARM64_SYS_REG(3, 3, 14, 9, 7),
	ARM64_SYS_REG(3, 3, 14, 10, 0),
	ARM64_SYS_REG(3, 3, 14, 10, 1),
	ARM64_SYS_REG(3, 3, 14, 10, 2),
	ARM64_SYS_REG(3, 3, 14, 10, 3),
	ARM64_SYS_REG(3, 3, 14, 10, 4),
	ARM64_SYS_REG(3, 3, 14, 10, 5),
	ARM64_SYS_REG(3, 3, 14, 10, 6),
	ARM64_SYS_REG(3, 3, 14, 10, 7),
	ARM64_SYS_REG(3, 3, 14, 11, 0),
	ARM64_SYS_REG(3, 3, 14, 11, 1),
	ARM64_SYS_REG(3, 3, 14, 11, 2),
	ARM64_SYS_REG(3, 3, 14, 11, 3),
	ARM64_SYS_REG(3, 3, 14, 11, 4),
	ARM64_SYS_REG(3, 3, 14, 11, 5),
	ARM64_SYS_REG(3, 3, 14, 11, 6),
	ARM64_SYS_REG(3, 3, 14, 12, 0),
	ARM64_SYS_REG(3, 3, 14, 12, 1),
	ARM64_SYS_REG(3, 3, 14, 12, 2),
	ARM64_SYS_REG(3, 3, 14, 12, 3),
	ARM64_SYS_REG(3, 3, 14, 12, 4),
	ARM64_SYS_REG(3, 3, 14, 12, 5),
	ARM64_SYS_REG(3, 3, 14, 12, 6),
	ARM64_SYS_REG(3, 3, 14, 12, 7),
	ARM64_SYS_REG(3, 3, 14, 13, 0),
	ARM64_SYS_REG(3, 3, 14, 13, 1),
	ARM64_SYS_REG(3, 3, 14, 13, 2),
	ARM64_SYS_REG(3, 3, 14, 13, 3),
	ARM64_SYS_REG(3, 3, 14, 13, 4),
	ARM64_SYS_REG(3, 3, 14, 13, 5),
	ARM64_SYS_REG(3, 3, 14, 13, 6),
	ARM64_SYS_REG(3, 3, 14, 13, 7),
	ARM64_SYS_REG(3, 3, 14, 14, 0),
	ARM64_SYS_REG(3, 3, 14, 14, 1),
	ARM64_SYS_REG(3, 3, 14, 14, 2),
	ARM64_SYS_REG(3, 3, 14, 14, 3),
	ARM64_SYS_REG(3, 3, 14, 14, 4),
	ARM64_SYS_REG(3, 3, 14, 14, 5),
	ARM64_SYS_REG(3, 3, 14, 14, 6),
	ARM64_SYS_REG(3, 3, 14, 14, 7),
	ARM64_SYS_REG(3, 3, 14, 15, 0),
	ARM64_SYS_REG(3, 3, 14, 15, 1),
	ARM64_SYS_REG(3, 3, 14, 15, 2),
	ARM64_SYS_REG(3, 3, 14, 15, 3),
	ARM64_SYS_REG(3, 3, 14, 15, 4),
	ARM64_SYS_REG(3, 3, 14, 15, 5),
	ARM64_SYS_REG(3, 3, 14, 15, 6),
	ARM64_SYS_REG(3, 3, 14, 15, 7),	/* PMCCFILTR_EL0 */
};

static __u64 vregs[] = {
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[0]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[1]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[2]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[3]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[4]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[5]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[6]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[7]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[8]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[9]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[10]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[11]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[12]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[13]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[14]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[15]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[16]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[17]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[18]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[19]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[20]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[21]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[22]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[23]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[24]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[25]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[26]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[27]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[28]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[29]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[30]),
	KVM_REG_ARM64 | KVM_REG_SIZE_U128 | KVM_REG_ARM_CORE | KVM_REG_ARM_CORE_REG(fp_regs.vregs[31]),
};

static __u64 sve_regs[] = {
	KVM_REG_ARM64_SVE_VLS,
	KVM_REG_ARM64_SVE_ZREG(0, 0),
	KVM_REG_ARM64_SVE_ZREG(1, 0),
	KVM_REG_ARM64_SVE_ZREG(2, 0),
	KVM_REG_ARM64_SVE_ZREG(3, 0),
	KVM_REG_ARM64_SVE_ZREG(4, 0),
	KVM_REG_ARM64_SVE_ZREG(5, 0),
	KVM_REG_ARM64_SVE_ZREG(6, 0),
	KVM_REG_ARM64_SVE_ZREG(7, 0),
	KVM_REG_ARM64_SVE_ZREG(8, 0),
	KVM_REG_ARM64_SVE_ZREG(9, 0),
	KVM_REG_ARM64_SVE_ZREG(10, 0),
	KVM_REG_ARM64_SVE_ZREG(11, 0),
	KVM_REG_ARM64_SVE_ZREG(12, 0),
	KVM_REG_ARM64_SVE_ZREG(13, 0),
	KVM_REG_ARM64_SVE_ZREG(14, 0),
	KVM_REG_ARM64_SVE_ZREG(15, 0),
	KVM_REG_ARM64_SVE_ZREG(16, 0),
	KVM_REG_ARM64_SVE_ZREG(17, 0),
	KVM_REG_ARM64_SVE_ZREG(18, 0),
	KVM_REG_ARM64_SVE_ZREG(19, 0),
	KVM_REG_ARM64_SVE_ZREG(20, 0),
	KVM_REG_ARM64_SVE_ZREG(21, 0),
	KVM_REG_ARM64_SVE_ZREG(22, 0),
	KVM_REG_ARM64_SVE_ZREG(23, 0),
	KVM_REG_ARM64_SVE_ZREG(24, 0),
	KVM_REG_ARM64_SVE_ZREG(25, 0),
	KVM_REG_ARM64_SVE_ZREG(26, 0),
	KVM_REG_ARM64_SVE_ZREG(27, 0),
	KVM_REG_ARM64_SVE_ZREG(28, 0),
	KVM_REG_ARM64_SVE_ZREG(29, 0),
	KVM_REG_ARM64_SVE_ZREG(30, 0),
	KVM_REG_ARM64_SVE_ZREG(31, 0),
	KVM_REG_ARM64_SVE_PREG(0, 0),
	KVM_REG_ARM64_SVE_PREG(1, 0),
	KVM_REG_ARM64_SVE_PREG(2, 0),
	KVM_REG_ARM64_SVE_PREG(3, 0),
	KVM_REG_ARM64_SVE_PREG(4, 0),
	KVM_REG_ARM64_SVE_PREG(5, 0),
	KVM_REG_ARM64_SVE_PREG(6, 0),
	KVM_REG_ARM64_SVE_PREG(7, 0),
	KVM_REG_ARM64_SVE_PREG(8, 0),
	KVM_REG_ARM64_SVE_PREG(9, 0),
	KVM_REG_ARM64_SVE_PREG(10, 0),
	KVM_REG_ARM64_SVE_PREG(11, 0),
	KVM_REG_ARM64_SVE_PREG(12, 0),
	KVM_REG_ARM64_SVE_PREG(13, 0),
	KVM_REG_ARM64_SVE_PREG(14, 0),
	KVM_REG_ARM64_SVE_PREG(15, 0),
	KVM_REG_ARM64_SVE_FFR(0),
	ARM64_SYS_REG(3, 0, 1, 2, 0),   /* ZCR_EL1 */
};

static __u64 sve_rejects_set[] = {
	KVM_REG_ARM64_SVE_VLS,
};

#define BASE_SUBLIST \
	{ "base", .regs = base_regs, .regs_n = ARRAY_SIZE(base_regs), }
#define VREGS_SUBLIST \
	{ "vregs", .regs = vregs, .regs_n = ARRAY_SIZE(vregs), }
#define PMU_SUBLIST \
	{ "pmu", .regs = pmu_regs, .regs_n = ARRAY_SIZE(pmu_regs), }
#define SVE_SUBLIST \
	{ "sve", .capability = KVM_CAP_ARM_SVE, .feature = KVM_ARM_VCPU_SVE, .finalize = true, \
	  .regs = sve_regs, .regs_n = ARRAY_SIZE(sve_regs), \
	  .rejects_set = sve_rejects_set, .rejects_set_n = ARRAY_SIZE(sve_rejects_set), }

static struct vcpu_config vregs_config = {
	.sublists = {
	BASE_SUBLIST,
	VREGS_SUBLIST,
	{0},
	},
};
static struct vcpu_config vregs_pmu_config = {
	.sublists = {
	BASE_SUBLIST,
	VREGS_SUBLIST,
	PMU_SUBLIST,
	{0},
	},
};
static struct vcpu_config sve_config = {
	.sublists = {
	BASE_SUBLIST,
	SVE_SUBLIST,
	{0},
	},
};
static struct vcpu_config sve_pmu_config = {
	.sublists = {
	BASE_SUBLIST,
	SVE_SUBLIST,
	PMU_SUBLIST,
	{0},
	},
};

static struct vcpu_config *vcpu_configs[] = {
	&vregs_config,
	&vregs_pmu_config,
	&sve_config,
	&sve_pmu_config,
};
static int vcpu_configs_n = ARRAY_SIZE(vcpu_configs);
