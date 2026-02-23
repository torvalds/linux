// SPDX-License-Identifier: GPL-2.0
#include "debug.h"
#include "evsel.h"
#include "kvm-stat.h"
#include <dwarf-regs.h>

bool kvm_exit_event(struct evsel *evsel)
{
	uint16_t e_machine = evsel__e_machine(evsel, /*e_flags=*/NULL);

	return evsel__name_is(evsel, kvm_exit_trace(e_machine));
}

void exit_event_get_key(struct evsel *evsel,
			struct perf_sample *sample,
			struct event_key *key)
{
	uint16_t e_machine = evsel__e_machine(evsel, /*e_flags=*/NULL);

	key->info = 0;
	key->key  = evsel__intval(evsel, sample, kvm_exit_reason(e_machine));
}


bool exit_event_begin(struct evsel *evsel,
		      struct perf_sample *sample, struct event_key *key)
{
	if (kvm_exit_event(evsel)) {
		exit_event_get_key(evsel, sample, key);
		return true;
	}

	return false;
}

bool kvm_entry_event(struct evsel *evsel)
{
	uint16_t e_machine = evsel__e_machine(evsel, /*e_flags=*/NULL);

	return evsel__name_is(evsel, kvm_entry_trace(e_machine));
}

bool exit_event_end(struct evsel *evsel,
		    struct perf_sample *sample __maybe_unused,
		    struct event_key *key __maybe_unused)
{
	return kvm_entry_event(evsel);
}

static const char *get_exit_reason(struct perf_kvm_stat *kvm,
				   struct exit_reasons_table *tbl,
				   u64 exit_code)
{
	while (tbl->reason != NULL) {
		if (tbl->exit_code == exit_code)
			return tbl->reason;
		tbl++;
	}

	pr_err("unknown kvm exit code:%lld on %s\n",
		(unsigned long long)exit_code, kvm->exit_reasons_isa);
	return "UNKNOWN";
}

void exit_event_decode_key(struct perf_kvm_stat *kvm,
			   struct event_key *key,
			   char *decode)
{
	const char *exit_reason = get_exit_reason(kvm, key->exit_reasons,
						  key->key);

	scnprintf(decode, KVM_EVENT_NAME_LEN, "%s", exit_reason);
}

int setup_kvm_events_tp(struct perf_kvm_stat *kvm, uint16_t e_machine)
{
	switch (e_machine) {
	case EM_PPC:
	case EM_PPC64:
		return __setup_kvm_events_tp_powerpc(kvm);
	default:
		return 0;
	}
}

int cpu_isa_init(struct perf_kvm_stat *kvm, uint16_t e_machine, const char *cpuid)
{
	switch (e_machine) {
	case EM_AARCH64:
		return __cpu_isa_init_arm64(kvm);
	case EM_LOONGARCH:
		return __cpu_isa_init_loongarch(kvm);
	case EM_PPC:
	case EM_PPC64:
		return __cpu_isa_init_powerpc(kvm);
	case EM_RISCV:
		return __cpu_isa_init_riscv(kvm);
	case EM_S390:
		return __cpu_isa_init_s390(kvm, cpuid);
	case EM_X86_64:
	case EM_386:
		return __cpu_isa_init_x86(kvm, cpuid);
	default:
		pr_err("Unsupported kvm-stat host %d\n", e_machine);
		return -1;
	}
}

const char *vcpu_id_str(uint16_t e_machine)
{
	switch (e_machine) {
	case EM_AARCH64:
	case EM_RISCV:
	case EM_S390:
		return "id";
	case EM_LOONGARCH:
	case EM_PPC:
	case EM_PPC64:
	case EM_X86_64:
	case EM_386:
		return "vcpu_id";
	default:
		pr_err("Unsupported kvm-stat host %d\n", e_machine);
		return NULL;
	}
}

const char *kvm_exit_reason(uint16_t e_machine)
{
	switch (e_machine) {
	case EM_AARCH64:
		return "ret";
	case EM_LOONGARCH:
		return "reason";
	case EM_PPC:
	case EM_PPC64:
		return "trap";
	case EM_RISCV:
		return "scause";
	case EM_S390:
		return "icptcode";
	case EM_X86_64:
	case EM_386:
		return "exit_reason";
	default:
		pr_err("Unsupported kvm-stat host %d\n", e_machine);
		return NULL;
	}
}

const char *kvm_entry_trace(uint16_t e_machine)
{
	switch (e_machine) {
	case EM_AARCH64:
	case EM_RISCV:
	case EM_X86_64:
	case EM_386:
		return "kvm:kvm_entry";
	case EM_LOONGARCH:
		return "kvm:kvm_enter";
	case EM_PPC:
	case EM_PPC64:
		return "kvm_hv:kvm_guest_enter";
	case EM_S390:
		return "kvm:kvm_s390_sie_enter";
	default:
		pr_err("Unsupported kvm-stat host %d\n", e_machine);
		return NULL;
	}
}

const char *kvm_exit_trace(uint16_t e_machine)
{
	switch (e_machine) {
	case EM_AARCH64:
	case EM_LOONGARCH:
	case EM_RISCV:
	case EM_X86_64:
	case EM_386:
		return "kvm:kvm_exit";
	case EM_PPC:
	case EM_PPC64:
		return "kvm_hv:kvm_guest_exit";
	case EM_S390:
		return "kvm:kvm_s390_sie_exit";
	default:
		pr_err("Unsupported kvm-stat host %d\n", e_machine);
		return NULL;
	}
}

const char * const *kvm_events_tp(uint16_t e_machine)
{
	switch (e_machine) {
	case EM_AARCH64:
		return __kvm_events_tp_arm64();
	case EM_LOONGARCH:
		return __kvm_events_tp_loongarch();
	case EM_PPC:
	case EM_PPC64:
		return __kvm_events_tp_powerpc();
	case EM_RISCV:
		return __kvm_events_tp_riscv();
	case EM_S390:
		return __kvm_events_tp_s390();
	case EM_X86_64:
	case EM_386:
		return __kvm_events_tp_x86();
	default:
		pr_err("Unsupported kvm-stat host %d\n", e_machine);
		return NULL;
	}
}

const struct kvm_reg_events_ops *kvm_reg_events_ops(uint16_t e_machine)
{
	switch (e_machine) {
	case EM_AARCH64:
		return __kvm_reg_events_ops_arm64();
	case EM_LOONGARCH:
		return __kvm_reg_events_ops_loongarch();
	case EM_PPC:
	case EM_PPC64:
		return __kvm_reg_events_ops_powerpc();
	case EM_RISCV:
		return __kvm_reg_events_ops_riscv();
	case EM_S390:
		return __kvm_reg_events_ops_s390();
	case EM_X86_64:
	case EM_386:
		return __kvm_reg_events_ops_x86();
	default:
		pr_err("Unsupported kvm-stat host %d\n", e_machine);
		return NULL;
	}
}

const char * const *kvm_skip_events(uint16_t e_machine)
{
	switch (e_machine) {
	case EM_AARCH64:
		return __kvm_skip_events_arm64();
	case EM_LOONGARCH:
		return __kvm_skip_events_loongarch();
	case EM_PPC:
	case EM_PPC64:
		return __kvm_skip_events_powerpc();
	case EM_RISCV:
		return __kvm_skip_events_riscv();
	case EM_S390:
		return __kvm_skip_events_s390();
	case EM_X86_64:
	case EM_386:
		return __kvm_skip_events_x86();
	default:
		pr_err("Unsupported kvm-stat host %d\n", e_machine);
		return NULL;
	}
}

int kvm_add_default_arch_event(uint16_t e_machine, int *argc, const char **argv)
{
	switch (e_machine) {
	case EM_PPC:
	case EM_PPC64:
		return __kvm_add_default_arch_event_powerpc(argc, argv);
	case EM_X86_64:
	case EM_386:
		return __kvm_add_default_arch_event_x86(argc, argv);
	default:
		return 0;
	}
}
