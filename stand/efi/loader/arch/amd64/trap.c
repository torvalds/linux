/*-
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Konstantin Belousov under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stand.h>
#include <string.h>
#include <sys/param.h>
#include <machine/cpufunc.h>
#include <machine/psl.h>
#include <machine/segments.h>
#include <machine/frame.h>
#include <machine/tss.h>

#include <efi.h>
#include <efilib.h>

#include "bootstrap.h"
#include "loader_efi.h"

#define	NUM_IST	8
#define	NUM_EXC	32

/*
 * This code catches exceptions but forwards hardware interrupts to
 * handlers installed by firmware.  It differentiates exceptions
 * vs. interrupts by presence of the error code on the stack, which
 * causes different stack pointer value on trap handler entry.
 *
 * Use kernel layout for the trapframe just to not be original.
 *
 * Use free IST slot in existing TSS, or create our own TSS if
 * firmware did not configured any, to have stack switched to
 * IST-specified one, e.g. to handle #SS.  If hand-off cannot find
 * unused IST slot, or create a new descriptor in GDT, we bail out.
 */

static struct region_descriptor fw_idt;	/* Descriptor for pristine fw IDT */
static struct region_descriptor loader_idt;/* Descriptor for loader
					   shadow IDT */
static EFI_PHYSICAL_ADDRESS lidt_pa;	/* Address of loader shadow IDT */
static EFI_PHYSICAL_ADDRESS tss_pa;	/* Address of TSS */
static EFI_PHYSICAL_ADDRESS exc_stack_pa;/* Address of IST stack for loader */
EFI_PHYSICAL_ADDRESS exc_rsp;	/* %rsp value on our IST stack when
				   exception happens */
EFI_PHYSICAL_ADDRESS fw_intr_handlers[NUM_EXC]; /* fw handlers for < 32 IDT
						   vectors */
static int intercepted[NUM_EXC];
static int ist;				/* IST for exception handlers */
static uint32_t tss_fw_seg;		/* Fw TSS segment */
static uint32_t loader_tss;		/* Loader TSS segment */
static struct region_descriptor fw_gdt;	/* Descriptor of pristine GDT */
static EFI_PHYSICAL_ADDRESS loader_gdt_pa; /* Address of loader shadow GDT */

void report_exc(struct trapframe *tf);
void
report_exc(struct trapframe *tf)
{

	/*
	 * printf() depends on loader runtime and UEFI firmware health
	 * to produce the console output, in case of exception, the
	 * loader or firmware runtime may fail to support the printf().
	 */
	printf("===================================================="
	    "============================\n");
	printf("Exception %u\n", tf->tf_trapno);
	printf("ss 0x%04hx cs 0x%04hx ds 0x%04hx es 0x%04hx fs 0x%04hx "
	    "gs 0x%04hx\n",
	    (uint16_t)tf->tf_ss, (uint16_t)tf->tf_cs, (uint16_t)tf->tf_ds,
	    (uint16_t)tf->tf_es, (uint16_t)tf->tf_fs, (uint16_t)tf->tf_gs);
	printf("err 0x%08x rfl 0x%08x addr 0x%016lx\n"
	    "rsp 0x%016lx rip 0x%016lx\n",
	    (uint32_t)tf->tf_err, (uint32_t)tf->tf_rflags, tf->tf_addr,
	    tf->tf_rsp, tf->tf_rip);
	printf(
	    "rdi 0x%016lx rsi 0x%016lx rdx 0x%016lx\n"
	    "rcx 0x%016lx r8  0x%016lx r9  0x%016lx\n"
	    "rax 0x%016lx rbx 0x%016lx rbp 0x%016lx\n"
	    "r10 0x%016lx r11 0x%016lx r12 0x%016lx\n"
	    "r13 0x%016lx r14 0x%016lx r15 0x%016lx\n",
	    tf->tf_rdi, tf->tf_rsi, tf->tf_rdx, tf->tf_rcx, tf->tf_r8,
	    tf->tf_r9, tf->tf_rax, tf->tf_rbx, tf->tf_rbp, tf->tf_r10,
	    tf->tf_r11, tf->tf_r12, tf->tf_r13, tf->tf_r14, tf->tf_r15);
	printf("Machine stopped.\n");
}

static void
prepare_exception(unsigned idx, uint64_t my_handler,
    int ist_use_table[static NUM_IST])
{
	struct gate_descriptor *fw_idt_e, *loader_idt_e;

	fw_idt_e = &((struct gate_descriptor *)fw_idt.rd_base)[idx];
	loader_idt_e = &((struct gate_descriptor *)loader_idt.rd_base)[idx];
	fw_intr_handlers[idx] = fw_idt_e->gd_looffset +
	    (fw_idt_e->gd_hioffset << 16);
	intercepted[idx] = 1;
	ist_use_table[fw_idt_e->gd_ist]++;
	loader_idt_e->gd_looffset = my_handler;
	loader_idt_e->gd_hioffset = my_handler >> 16;
	/*
	 * We reuse uefi selector for the code segment for the exception
	 * handler code, while the reason for the fault might be the
	 * corruption of that gdt entry. On the other hand, allocating
	 * our own descriptor might be not much better, if gdt is corrupted.
	 */
	loader_idt_e->gd_selector = fw_idt_e->gd_selector;
	loader_idt_e->gd_ist = 0;
	loader_idt_e->gd_type = SDT_SYSIGT;
	loader_idt_e->gd_dpl = 0;
	loader_idt_e->gd_p = 1;
	loader_idt_e->gd_xx = 0;
	loader_idt_e->sd_xx1 = 0;
}
#define	PREPARE_EXCEPTION(N)						\
    extern char EXC##N##_handler[];					\
    prepare_exception(N, (uintptr_t)EXC##N##_handler, ist_use_table);

static void
free_tables(void)
{

	if (lidt_pa != 0) {
		BS->FreePages(lidt_pa, EFI_SIZE_TO_PAGES(fw_idt.rd_limit));
		lidt_pa = 0;
	}
	if (exc_stack_pa != 0) {
		BS->FreePages(exc_stack_pa, 1);
		exc_stack_pa = 0;
	}
	if (tss_pa != 0 && tss_fw_seg == 0) {
		BS->FreePages(tss_pa, EFI_SIZE_TO_PAGES(sizeof(struct
		    amd64tss)));
		tss_pa = 0;
	}
	if (loader_gdt_pa != 0) {
		BS->FreePages(tss_pa, 2);
		loader_gdt_pa = 0;
	}
	ist = 0;
	loader_tss = 0;
}

static int
efi_setup_tss(struct region_descriptor *gdt, uint32_t loader_tss_idx,
    struct amd64tss **tss)
{
	EFI_STATUS status;
	struct system_segment_descriptor *tss_desc;

	tss_desc = (struct system_segment_descriptor *)(gdt->rd_base +
	    (loader_tss_idx << 3));
	status = BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
	    EFI_SIZE_TO_PAGES(sizeof(struct amd64tss)), &tss_pa);
	if (EFI_ERROR(status)) {
		printf("efi_setup_tss: AllocatePages tss error %lu\n",
		    EFI_ERROR_CODE(status));
		return (0);
	}
	*tss = (struct amd64tss *)tss_pa;
	bzero(*tss, sizeof(**tss));
	tss_desc->sd_lolimit = sizeof(struct amd64tss);
	tss_desc->sd_lobase = tss_pa;
	tss_desc->sd_type = SDT_SYSTSS;
	tss_desc->sd_dpl = 0;
	tss_desc->sd_p = 1;
	tss_desc->sd_hilimit = sizeof(struct amd64tss) >> 16;
	tss_desc->sd_gran = 0;
	tss_desc->sd_hibase = tss_pa >> 24;
	tss_desc->sd_xx0 = 0;
	tss_desc->sd_xx1 = 0;
	tss_desc->sd_mbz = 0;
	tss_desc->sd_xx2 = 0;
	return (1);
}

static int
efi_redirect_exceptions(void)
{
	int ist_use_table[NUM_IST];
	struct gate_descriptor *loader_idt_e;
	struct system_segment_descriptor *tss_desc, *gdt_desc;
	struct amd64tss *tss;
	struct region_descriptor *gdt_rd, loader_gdt;
	uint32_t i;
	EFI_STATUS status;
	register_t rfl;

	sidt(&fw_idt);
	status = BS->AllocatePages(AllocateAnyPages, EfiLoaderData,
	    EFI_SIZE_TO_PAGES(fw_idt.rd_limit), &lidt_pa);
	if (EFI_ERROR(status)) {
		printf("efi_redirect_exceptions: AllocatePages IDT error %lu\n",
		    EFI_ERROR_CODE(status));
		lidt_pa = 0;
		return (0);
	}
	status = BS->AllocatePages(AllocateAnyPages, EfiLoaderData, 1,
	    &exc_stack_pa);
	if (EFI_ERROR(status)) {
		printf("efi_redirect_exceptions: AllocatePages stk error %lu\n",
		    EFI_ERROR_CODE(status));
		exc_stack_pa = 0;
		free_tables();
		return (0);
	}
	loader_idt.rd_limit = fw_idt.rd_limit;
	bcopy((void *)fw_idt.rd_base, (void *)loader_idt.rd_base,
	    loader_idt.rd_limit);
	bzero(ist_use_table, sizeof(ist_use_table));
	bzero(fw_intr_handlers, sizeof(fw_intr_handlers));
	bzero(intercepted, sizeof(intercepted));

	sgdt(&fw_gdt);
	tss_fw_seg = read_tr();
	gdt_rd = NULL;
	if (tss_fw_seg == 0) {
		for (i = 2; (i << 3) + sizeof(*gdt_desc) <= fw_gdt.rd_limit;
		    i += 2) {
			gdt_desc = (struct system_segment_descriptor *)(
			    fw_gdt.rd_base + (i << 3));
			if (gdt_desc->sd_type == 0 && gdt_desc->sd_mbz == 0) {
				gdt_rd = &fw_gdt;
				break;
			}
		}
		if (gdt_rd == NULL) {
			if (i >= 8190) {
				printf("efi_redirect_exceptions: all slots "
				    "in gdt are used\n");
				free_tables();
				return (0);
			}
			loader_gdt.rd_limit = roundup2(fw_gdt.rd_limit +
			    sizeof(struct system_segment_descriptor),
			    sizeof(struct system_segment_descriptor)) - 1;
			i = (loader_gdt.rd_limit + 1 -
			    sizeof(struct system_segment_descriptor)) /
			    sizeof(struct system_segment_descriptor) * 2;
			status = BS->AllocatePages(AllocateAnyPages,
			    EfiLoaderData,
			    EFI_SIZE_TO_PAGES(loader_gdt.rd_limit),
			    &loader_gdt_pa);
			if (EFI_ERROR(status)) {
				printf("efi_setup_tss: AllocatePages gdt error "
				    "%lu\n",  EFI_ERROR_CODE(status));
				loader_gdt_pa = 0;
				free_tables();
				return (0);
			}
			loader_gdt.rd_base = loader_gdt_pa;
			bzero((void *)loader_gdt.rd_base, loader_gdt.rd_limit);
			bcopy((void *)fw_gdt.rd_base,
			    (void *)loader_gdt.rd_base, fw_gdt.rd_limit);
			gdt_rd = &loader_gdt;
		}
		loader_tss = i << 3;
		if (!efi_setup_tss(gdt_rd, i, &tss)) {
			tss_pa = 0;
			free_tables();
			return (0);
		}
	} else {
		tss_desc = (struct system_segment_descriptor *)((char *)
		    fw_gdt.rd_base + tss_fw_seg);
		if (tss_desc->sd_type != SDT_SYSTSS &&
		    tss_desc->sd_type != SDT_SYSBSY) {
			printf("LTR points to non-TSS descriptor\n");
			free_tables();
			return (0);
		}
		tss_pa = tss_desc->sd_lobase + (tss_desc->sd_hibase << 16);
		tss = (struct amd64tss *)tss_pa;
		tss_desc->sd_type = SDT_SYSTSS; /* unbusy */
	}

	PREPARE_EXCEPTION(0);
	PREPARE_EXCEPTION(1);
	PREPARE_EXCEPTION(2);
	PREPARE_EXCEPTION(3);
	PREPARE_EXCEPTION(4);
	PREPARE_EXCEPTION(5);
	PREPARE_EXCEPTION(6);
	PREPARE_EXCEPTION(7);
	PREPARE_EXCEPTION(8);
	PREPARE_EXCEPTION(9);
	PREPARE_EXCEPTION(10);
	PREPARE_EXCEPTION(11);
	PREPARE_EXCEPTION(12);
	PREPARE_EXCEPTION(13);
	PREPARE_EXCEPTION(14);
	PREPARE_EXCEPTION(16);
	PREPARE_EXCEPTION(17);
	PREPARE_EXCEPTION(18);
	PREPARE_EXCEPTION(19);
	PREPARE_EXCEPTION(20);

	exc_rsp = exc_stack_pa + PAGE_SIZE -
	    (6 /* hw exception frame */ + 3 /* scratch regs */) * 8;

	/* Find free IST and use it */
	for (ist = 1; ist < NUM_IST; ist++) {
		if (ist_use_table[ist] == 0)
			break;
	}
	if (ist == NUM_IST) {
		printf("efi_redirect_exceptions: all ISTs used\n");
		free_tables();
		lidt_pa = 0;
		return (0);
	}
	for (i = 0; i < NUM_EXC; i++) {
		loader_idt_e = &((struct gate_descriptor *)loader_idt.
		    rd_base)[i];
		if (intercepted[i])
			loader_idt_e->gd_ist = ist;
	}
	(&(tss->tss_ist1))[ist - 1] = exc_stack_pa + PAGE_SIZE;

	/* Switch to new IDT */
	rfl = intr_disable();
	if (loader_gdt_pa != 0)
		bare_lgdt(&loader_gdt);
	if (loader_tss != 0)
		ltr(loader_tss);
	lidt(&loader_idt);
	intr_restore(rfl);
	return (1);
}

static void
efi_unredirect_exceptions(void)
{
	register_t rfl;

	if (lidt_pa == 0)
		return;

	rfl = intr_disable();
	if (ist != 0)
		(&(((struct amd64tss *)tss_pa)->tss_ist1))[ist - 1] = 0;
	if (loader_gdt_pa != 0)
		bare_lgdt(&fw_gdt);
	if (loader_tss != 0)
		ltr(tss_fw_seg);
	lidt(&fw_idt);
	intr_restore(rfl);
	free_tables();
}

static int
command_grab_faults(int argc, char *argv[])
{
	int res;

	res = efi_redirect_exceptions();
	if (!res)
		printf("failed\n");
	return (CMD_OK);
}
COMMAND_SET(grap_faults, "grab_faults", "grab faults", command_grab_faults);

static int
command_ungrab_faults(int argc, char *argv[])
{

	efi_unredirect_exceptions();
	return (CMD_OK);
}
COMMAND_SET(ungrab_faults, "ungrab_faults", "ungrab faults",
    command_ungrab_faults);

static int
command_fault(int argc, char *argv[])
{

	__asm("ud2");
	return (CMD_OK);
}
COMMAND_SET(fault, "fault", "generate fault", command_fault);
