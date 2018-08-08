/*
 * Copyright (C) 2009 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License (not later!)
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not,  see <http://www.gnu.org/licenses>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "event-parse.h"

#ifdef HAVE_UDIS86

#include <udis86.h>

static ud_t ud;

static void init_disassembler(void)
{
	ud_init(&ud);
	ud_set_syntax(&ud, UD_SYN_ATT);
}

static const char *disassemble(unsigned char *insn, int len, uint64_t rip,
			       int cr0_pe, int eflags_vm,
			       int cs_d, int cs_l)
{
	int mode;

	if (!cr0_pe)
		mode = 16;
	else if (eflags_vm)
		mode = 16;
	else if (cs_l)
		mode = 64;
	else if (cs_d)
		mode = 32;
	else
		mode = 16;

	ud_set_pc(&ud, rip);
	ud_set_mode(&ud, mode);
	ud_set_input_buffer(&ud, insn, len);
	ud_disassemble(&ud);
	return ud_insn_asm(&ud);
}

#else

static void init_disassembler(void)
{
}

static const char *disassemble(unsigned char *insn, int len, uint64_t rip,
			       int cr0_pe, int eflags_vm,
			       int cs_d, int cs_l)
{
	static char out[15*3+1];
	int i;

	for (i = 0; i < len; ++i)
		sprintf(out + i * 3, "%02x ", insn[i]);
	out[len*3-1] = '\0';
	return out;
}

#endif


#define VMX_EXIT_REASONS			\
	_ER(EXCEPTION_NMI,	 0)		\
	_ER(EXTERNAL_INTERRUPT,	 1)		\
	_ER(TRIPLE_FAULT,	 2)		\
	_ER(PENDING_INTERRUPT,	 7)		\
	_ER(NMI_WINDOW,		 8)		\
	_ER(TASK_SWITCH,	 9)		\
	_ER(CPUID,		 10)		\
	_ER(HLT,		 12)		\
	_ER(INVD,		 13)		\
	_ER(INVLPG,		 14)		\
	_ER(RDPMC,		 15)		\
	_ER(RDTSC,		 16)		\
	_ER(VMCALL,		 18)		\
	_ER(VMCLEAR,		 19)		\
	_ER(VMLAUNCH,		 20)		\
	_ER(VMPTRLD,		 21)		\
	_ER(VMPTRST,		 22)		\
	_ER(VMREAD,		 23)		\
	_ER(VMRESUME,		 24)		\
	_ER(VMWRITE,		 25)		\
	_ER(VMOFF,		 26)		\
	_ER(VMON,		 27)		\
	_ER(CR_ACCESS,		 28)		\
	_ER(DR_ACCESS,		 29)		\
	_ER(IO_INSTRUCTION,	 30)		\
	_ER(MSR_READ,		 31)		\
	_ER(MSR_WRITE,		 32)		\
	_ER(MWAIT_INSTRUCTION,	 36)		\
	_ER(MONITOR_INSTRUCTION, 39)		\
	_ER(PAUSE_INSTRUCTION,	 40)		\
	_ER(MCE_DURING_VMENTRY,	 41)		\
	_ER(TPR_BELOW_THRESHOLD, 43)		\
	_ER(APIC_ACCESS,	 44)		\
	_ER(EOI_INDUCED,	 45)		\
	_ER(EPT_VIOLATION,	 48)		\
	_ER(EPT_MISCONFIG,	 49)		\
	_ER(INVEPT,		 50)		\
	_ER(PREEMPTION_TIMER,	 52)		\
	_ER(WBINVD,		 54)		\
	_ER(XSETBV,		 55)		\
	_ER(APIC_WRITE,		 56)		\
	_ER(INVPCID,		 58)		\
	_ER(PML_FULL,		 62)		\
	_ER(XSAVES,		 63)		\
	_ER(XRSTORS,		 64)

#define SVM_EXIT_REASONS \
	_ER(EXIT_READ_CR0,	0x000)		\
	_ER(EXIT_READ_CR3,	0x003)		\
	_ER(EXIT_READ_CR4,	0x004)		\
	_ER(EXIT_READ_CR8,	0x008)		\
	_ER(EXIT_WRITE_CR0,	0x010)		\
	_ER(EXIT_WRITE_CR3,	0x013)		\
	_ER(EXIT_WRITE_CR4,	0x014)		\
	_ER(EXIT_WRITE_CR8,	0x018)		\
	_ER(EXIT_READ_DR0,	0x020)		\
	_ER(EXIT_READ_DR1,	0x021)		\
	_ER(EXIT_READ_DR2,	0x022)		\
	_ER(EXIT_READ_DR3,	0x023)		\
	_ER(EXIT_READ_DR4,	0x024)		\
	_ER(EXIT_READ_DR5,	0x025)		\
	_ER(EXIT_READ_DR6,	0x026)		\
	_ER(EXIT_READ_DR7,	0x027)		\
	_ER(EXIT_WRITE_DR0,	0x030)		\
	_ER(EXIT_WRITE_DR1,	0x031)		\
	_ER(EXIT_WRITE_DR2,	0x032)		\
	_ER(EXIT_WRITE_DR3,	0x033)		\
	_ER(EXIT_WRITE_DR4,	0x034)		\
	_ER(EXIT_WRITE_DR5,	0x035)		\
	_ER(EXIT_WRITE_DR6,	0x036)		\
	_ER(EXIT_WRITE_DR7,	0x037)		\
	_ER(EXIT_EXCP_BASE,     0x040)		\
	_ER(EXIT_INTR,		0x060)		\
	_ER(EXIT_NMI,		0x061)		\
	_ER(EXIT_SMI,		0x062)		\
	_ER(EXIT_INIT,		0x063)		\
	_ER(EXIT_VINTR,		0x064)		\
	_ER(EXIT_CR0_SEL_WRITE,	0x065)		\
	_ER(EXIT_IDTR_READ,	0x066)		\
	_ER(EXIT_GDTR_READ,	0x067)		\
	_ER(EXIT_LDTR_READ,	0x068)		\
	_ER(EXIT_TR_READ,	0x069)		\
	_ER(EXIT_IDTR_WRITE,	0x06a)		\
	_ER(EXIT_GDTR_WRITE,	0x06b)		\
	_ER(EXIT_LDTR_WRITE,	0x06c)		\
	_ER(EXIT_TR_WRITE,	0x06d)		\
	_ER(EXIT_RDTSC,		0x06e)		\
	_ER(EXIT_RDPMC,		0x06f)		\
	_ER(EXIT_PUSHF,		0x070)		\
	_ER(EXIT_POPF,		0x071)		\
	_ER(EXIT_CPUID,		0x072)		\
	_ER(EXIT_RSM,		0x073)		\
	_ER(EXIT_IRET,		0x074)		\
	_ER(EXIT_SWINT,		0x075)		\
	_ER(EXIT_INVD,		0x076)		\
	_ER(EXIT_PAUSE,		0x077)		\
	_ER(EXIT_HLT,		0x078)		\
	_ER(EXIT_INVLPG,	0x079)		\
	_ER(EXIT_INVLPGA,	0x07a)		\
	_ER(EXIT_IOIO,		0x07b)		\
	_ER(EXIT_MSR,		0x07c)		\
	_ER(EXIT_TASK_SWITCH,	0x07d)		\
	_ER(EXIT_FERR_FREEZE,	0x07e)		\
	_ER(EXIT_SHUTDOWN,	0x07f)		\
	_ER(EXIT_VMRUN,		0x080)		\
	_ER(EXIT_VMMCALL,	0x081)		\
	_ER(EXIT_VMLOAD,	0x082)		\
	_ER(EXIT_VMSAVE,	0x083)		\
	_ER(EXIT_STGI,		0x084)		\
	_ER(EXIT_CLGI,		0x085)		\
	_ER(EXIT_SKINIT,	0x086)		\
	_ER(EXIT_RDTSCP,	0x087)		\
	_ER(EXIT_ICEBP,		0x088)		\
	_ER(EXIT_WBINVD,	0x089)		\
	_ER(EXIT_MONITOR,	0x08a)		\
	_ER(EXIT_MWAIT,		0x08b)		\
	_ER(EXIT_MWAIT_COND,	0x08c)		\
	_ER(EXIT_NPF,		0x400)		\
	_ER(EXIT_ERR,		-1)

#define _ER(reason, val)	{ #reason, val },
struct str_values {
	const char	*str;
	int		val;
};

static struct str_values vmx_exit_reasons[] = {
	VMX_EXIT_REASONS
	{ NULL, -1}
};

static struct str_values svm_exit_reasons[] = {
	SVM_EXIT_REASONS
	{ NULL, -1}
};

static struct isa_exit_reasons {
	unsigned isa;
	struct str_values *strings;
} isa_exit_reasons[] = {
	{ .isa = 1, .strings = vmx_exit_reasons },
	{ .isa = 2, .strings = svm_exit_reasons },
	{ }
};

static const char *find_exit_reason(unsigned isa, int val)
{
	struct str_values *strings = NULL;
	int i;

	for (i = 0; isa_exit_reasons[i].strings; ++i)
		if (isa_exit_reasons[i].isa == isa) {
			strings = isa_exit_reasons[i].strings;
			break;
		}
	if (!strings)
		return "UNKNOWN-ISA";
	for (i = 0; strings[i].val >= 0; i++)
		if (strings[i].val == val)
			break;

	return strings[i].str;
}

static int print_exit_reason(struct trace_seq *s, struct tep_record *record,
			     struct event_format *event, const char *field)
{
	unsigned long long isa;
	unsigned long long val;
	const char *reason;

	if (tep_get_field_val(s, event, field, record, &val, 1) < 0)
		return -1;

	if (tep_get_field_val(s, event, "isa", record, &isa, 0) < 0)
		isa = 1;

	reason = find_exit_reason(isa, val);
	if (reason)
		trace_seq_printf(s, "reason %s", reason);
	else
		trace_seq_printf(s, "reason UNKNOWN (%llu)", val);
	return 0;
}

static int kvm_exit_handler(struct trace_seq *s, struct tep_record *record,
			    struct event_format *event, void *context)
{
	unsigned long long info1 = 0, info2 = 0;

	if (print_exit_reason(s, record, event, "exit_reason") < 0)
		return -1;

	tep_print_num_field(s, " rip 0x%lx", event, "guest_rip", record, 1);

	if (tep_get_field_val(s, event, "info1", record, &info1, 0) >= 0
	    && tep_get_field_val(s, event, "info2", record, &info2, 0) >= 0)
		trace_seq_printf(s, " info %llx %llx", info1, info2);

	return 0;
}

#define KVM_EMUL_INSN_F_CR0_PE (1 << 0)
#define KVM_EMUL_INSN_F_EFL_VM (1 << 1)
#define KVM_EMUL_INSN_F_CS_D   (1 << 2)
#define KVM_EMUL_INSN_F_CS_L   (1 << 3)

static int kvm_emulate_insn_handler(struct trace_seq *s,
				    struct tep_record *record,
				    struct event_format *event, void *context)
{
	unsigned long long rip, csbase, len, flags, failed;
	int llen;
	uint8_t *insn;
	const char *disasm;

	if (tep_get_field_val(s, event, "rip", record, &rip, 1) < 0)
		return -1;

	if (tep_get_field_val(s, event, "csbase", record, &csbase, 1) < 0)
		return -1;

	if (tep_get_field_val(s, event, "len", record, &len, 1) < 0)
		return -1;

	if (tep_get_field_val(s, event, "flags", record, &flags, 1) < 0)
		return -1;

	if (tep_get_field_val(s, event, "failed", record, &failed, 1) < 0)
		return -1;

	insn = tep_get_field_raw(s, event, "insn", record, &llen, 1);
	if (!insn)
		return -1;

	disasm = disassemble(insn, len, rip,
			     flags & KVM_EMUL_INSN_F_CR0_PE,
			     flags & KVM_EMUL_INSN_F_EFL_VM,
			     flags & KVM_EMUL_INSN_F_CS_D,
			     flags & KVM_EMUL_INSN_F_CS_L);

	trace_seq_printf(s, "%llx:%llx: %s%s", csbase, rip, disasm,
			 failed ? " FAIL" : "");
	return 0;
}


static int kvm_nested_vmexit_inject_handler(struct trace_seq *s, struct tep_record *record,
					    struct event_format *event, void *context)
{
	if (print_exit_reason(s, record, event, "exit_code") < 0)
		return -1;

	tep_print_num_field(s, " info1 %llx", event, "exit_info1", record, 1);
	tep_print_num_field(s, " info2 %llx", event, "exit_info2", record, 1);
	tep_print_num_field(s, " int_info %llx", event, "exit_int_info", record, 1);
	tep_print_num_field(s, " int_info_err %llx", event, "exit_int_info_err", record, 1);

	return 0;
}

static int kvm_nested_vmexit_handler(struct trace_seq *s, struct tep_record *record,
				     struct event_format *event, void *context)
{
	tep_print_num_field(s, "rip %llx ", event, "rip", record, 1);

	return kvm_nested_vmexit_inject_handler(s, record, event, context);
}

union kvm_mmu_page_role {
	unsigned word;
	struct {
		unsigned level:4;
		unsigned cr4_pae:1;
		unsigned quadrant:2;
		unsigned direct:1;
		unsigned access:3;
		unsigned invalid:1;
		unsigned nxe:1;
		unsigned cr0_wp:1;
		unsigned smep_and_not_wp:1;
		unsigned smap_and_not_wp:1;
		unsigned pad_for_nice_hex_output:8;
		unsigned smm:8;
	};
};

static int kvm_mmu_print_role(struct trace_seq *s, struct tep_record *record,
			      struct event_format *event, void *context)
{
	unsigned long long val;
	static const char *access_str[] = {
		"---", "--x", "w--", "w-x", "-u-", "-ux", "wu-", "wux"
	};
	union kvm_mmu_page_role role;

	if (tep_get_field_val(s, event, "role", record, &val, 1) < 0)
		return -1;

	role.word = (int)val;

	/*
	 * We can only use the structure if file is of the same
	 * endianess.
	 */
	if (tep_is_file_bigendian(event->pevent) ==
	    tep_is_host_bigendian(event->pevent)) {

		trace_seq_printf(s, "%u q%u%s %s%s %spae %snxe %swp%s%s%s",
				 role.level,
				 role.quadrant,
				 role.direct ? " direct" : "",
				 access_str[role.access],
				 role.invalid ? " invalid" : "",
				 role.cr4_pae ? "" : "!",
				 role.nxe ? "" : "!",
				 role.cr0_wp ? "" : "!",
				 role.smep_and_not_wp ? " smep" : "",
				 role.smap_and_not_wp ? " smap" : "",
				 role.smm ? " smm" : "");
	} else
		trace_seq_printf(s, "WORD: %08x", role.word);

	tep_print_num_field(s, " root %u ",  event,
			    "root_count", record, 1);

	if (tep_get_field_val(s, event, "unsync", record, &val, 1) < 0)
		return -1;

	trace_seq_printf(s, "%s%c",  val ? "unsync" : "sync", 0);
	return 0;
}

static int kvm_mmu_get_page_handler(struct trace_seq *s,
				    struct tep_record *record,
				    struct event_format *event, void *context)
{
	unsigned long long val;

	if (tep_get_field_val(s, event, "created", record, &val, 1) < 0)
		return -1;

	trace_seq_printf(s, "%s ", val ? "new" : "existing");

	if (tep_get_field_val(s, event, "gfn", record, &val, 1) < 0)
		return -1;

	trace_seq_printf(s, "sp gfn %llx ", val);
	return kvm_mmu_print_role(s, record, event, context);
}

#define PT_WRITABLE_SHIFT 1
#define PT_WRITABLE_MASK (1ULL << PT_WRITABLE_SHIFT)

static unsigned long long
process_is_writable_pte(struct trace_seq *s, unsigned long long *args)
{
	unsigned long pte = args[0];
	return pte & PT_WRITABLE_MASK;
}

int TEP_PLUGIN_LOADER(struct tep_handle *pevent)
{
	init_disassembler();

	tep_register_event_handler(pevent, -1, "kvm", "kvm_exit",
				   kvm_exit_handler, NULL);

	tep_register_event_handler(pevent, -1, "kvm", "kvm_emulate_insn",
				   kvm_emulate_insn_handler, NULL);

	tep_register_event_handler(pevent, -1, "kvm", "kvm_nested_vmexit",
				   kvm_nested_vmexit_handler, NULL);

	tep_register_event_handler(pevent, -1, "kvm", "kvm_nested_vmexit_inject",
				   kvm_nested_vmexit_inject_handler, NULL);

	tep_register_event_handler(pevent, -1, "kvmmmu", "kvm_mmu_get_page",
				   kvm_mmu_get_page_handler, NULL);

	tep_register_event_handler(pevent, -1, "kvmmmu", "kvm_mmu_sync_page",
				   kvm_mmu_print_role, NULL);

	tep_register_event_handler(pevent, -1,
				   "kvmmmu", "kvm_mmu_unsync_page",
				   kvm_mmu_print_role, NULL);

	tep_register_event_handler(pevent, -1, "kvmmmu", "kvm_mmu_zap_page",
				   kvm_mmu_print_role, NULL);

	tep_register_event_handler(pevent, -1, "kvmmmu",
			"kvm_mmu_prepare_zap_page", kvm_mmu_print_role,
			NULL);

	tep_register_print_function(pevent,
				    process_is_writable_pte,
				    TEP_FUNC_ARG_INT,
				    "is_writable_pte",
				    TEP_FUNC_ARG_LONG,
				    TEP_FUNC_ARG_VOID);
	return 0;
}

void TEP_PLUGIN_UNLOADER(struct tep_handle *pevent)
{
	tep_unregister_event_handler(pevent, -1, "kvm", "kvm_exit",
				     kvm_exit_handler, NULL);

	tep_unregister_event_handler(pevent, -1, "kvm", "kvm_emulate_insn",
				     kvm_emulate_insn_handler, NULL);

	tep_unregister_event_handler(pevent, -1, "kvm", "kvm_nested_vmexit",
				     kvm_nested_vmexit_handler, NULL);

	tep_unregister_event_handler(pevent, -1, "kvm", "kvm_nested_vmexit_inject",
				     kvm_nested_vmexit_inject_handler, NULL);

	tep_unregister_event_handler(pevent, -1, "kvmmmu", "kvm_mmu_get_page",
				     kvm_mmu_get_page_handler, NULL);

	tep_unregister_event_handler(pevent, -1, "kvmmmu", "kvm_mmu_sync_page",
				     kvm_mmu_print_role, NULL);

	tep_unregister_event_handler(pevent, -1,
				     "kvmmmu", "kvm_mmu_unsync_page",
				     kvm_mmu_print_role, NULL);

	tep_unregister_event_handler(pevent, -1, "kvmmmu", "kvm_mmu_zap_page",
				     kvm_mmu_print_role, NULL);

	tep_unregister_event_handler(pevent, -1, "kvmmmu",
			"kvm_mmu_prepare_zap_page", kvm_mmu_print_role,
			NULL);

	tep_unregister_print_function(pevent, process_is_writable_pte,
				      "is_writable_pte");
}
