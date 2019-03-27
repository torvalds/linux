/*-
 * Copyright (c) 2017 Andriy Gapon
 * All rights reserved.
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
 *
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/types.h>

#include <dev/pci/pcivar.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>

#include <machine/cputypes.h>
#include <machine/md_var.h>


/*
 * See BKDG for AMD Family 15h Models 00h-0Fh Processors
 * (publication 42301 Rev 3.08 - March 12, 2012):
 * - 2.13.3.1 DRAM Error Injection
 * - D18F3xB8 NB Array Address
 * - D18F3xBC NB Array Data Port
 * - D18F3xBC_x8 DRAM ECC
 */
#define	NB_MCA_CFG		0x44
#define		DRAM_ECC_EN	(1 << 22)
#define	NB_MCA_EXTCFG		0x180
#define		ECC_SYMB_SZ	(1 << 25)
#define	NB_ARRAY_ADDR		0xb8
#define		DRAM_ECC_SEL	(0x8 << 28)
#define		QUADRANT_SHIFT	1
#define		QUADRANT_MASK	0x3
#define	NB_ARRAY_PORT		0xbc
#define		INJ_WORD_SHIFT	20
#define		INJ_WORD_MASK	0x1ff
#define		DRAM_ERR_EN	(1 << 18)
#define		DRAM_WR_REQ	(1 << 17)
#define		DRAM_RD_REQ	(1 << 16)
#define		INJ_VECTOR_MASK	0xffff

static void ecc_ei_inject(int);

static device_t nbdev;
static int delay_ms = 0;
static int quadrant = 0;	/* 0 - 3 */
static int word_mask = 0x001;	/* 9 bits: 8 + 1 for ECC */
static int bit_mask = 0x0001;	/* 16 bits */

static int
sysctl_int_with_max(SYSCTL_HANDLER_ARGS)
{
	u_int value;
	int error;

	value = *(u_int *)arg1;
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error || req->newptr == NULL)
		return (error);
	if (value > arg2)
		return (EINVAL);
	*(u_int *)arg1 = value;
	return (0);
}

static int
sysctl_nonzero_int_with_max(SYSCTL_HANDLER_ARGS)
{
	u_int value;
	int error;

	value = *(u_int *)arg1;
	error = sysctl_int_with_max(oidp, &value, arg2, req);
	if (error || req->newptr == NULL)
		return (error);
	if (value == 0)
		return (EINVAL);
	*(u_int *)arg1 = value;
	return (0);
}

static int
sysctl_proc_inject(SYSCTL_HANDLER_ARGS)
{
	int error;
	int i;

	i = 0;
	error = sysctl_handle_int(oidp, &i, 0, req);
	if (error)
		return (error);
	if (i != 0)
		ecc_ei_inject(i);
	return (0);
}

static SYSCTL_NODE(_hw, OID_AUTO, error_injection, CTLFLAG_RD, NULL,
    "Hardware error injection");
static SYSCTL_NODE(_hw_error_injection, OID_AUTO, dram_ecc, CTLFLAG_RD, NULL,
    "DRAM ECC error injection");
SYSCTL_UINT(_hw_error_injection_dram_ecc, OID_AUTO, delay,
    CTLTYPE_UINT | CTLFLAG_RW, &delay_ms, 0,
    "Delay in milliseconds between error injections");
SYSCTL_PROC(_hw_error_injection_dram_ecc, OID_AUTO, quadrant,
    CTLTYPE_UINT | CTLFLAG_RW, &quadrant, QUADRANT_MASK,
    sysctl_int_with_max, "IU",
    "Index of 16-byte quadrant within 64-byte line where errors "
    "should be injected");
SYSCTL_PROC(_hw_error_injection_dram_ecc, OID_AUTO, word_mask,
    CTLTYPE_UINT | CTLFLAG_RW, &word_mask, INJ_WORD_MASK,
    sysctl_nonzero_int_with_max, "IU",
    "9-bit mask of words where errors should be injected (8 data + 1 ECC)");
SYSCTL_PROC(_hw_error_injection_dram_ecc, OID_AUTO, bit_mask,
    CTLTYPE_UINT | CTLFLAG_RW, &bit_mask, INJ_VECTOR_MASK,
    sysctl_nonzero_int_with_max, "IU",
    "16-bit mask of bits within each selected word where errors "
    "should be injected");
SYSCTL_PROC(_hw_error_injection_dram_ecc, OID_AUTO, inject,
    CTLTYPE_INT | CTLFLAG_RW | CTLFLAG_MPSAFE, NULL, 0, sysctl_proc_inject, "I",
    "Inject a number of errors according to configured parameters");

static void
ecc_ei_inject_one(void *arg, size_t size)
{
	volatile uint64_t *memory = arg;
	uint32_t val;
	int i;

	val = DRAM_ECC_SEL | (quadrant << QUADRANT_SHIFT);
	pci_write_config(nbdev, NB_ARRAY_ADDR, val, 4);

	val = (word_mask << INJ_WORD_SHIFT) | DRAM_WR_REQ | bit_mask;
	pci_write_config(nbdev, NB_ARRAY_PORT, val, 4);

	for (i = 0; i < size / sizeof(uint64_t); i++) {
		memory[i] = 0;
		val = pci_read_config(nbdev, NB_ARRAY_PORT, 4);
		if ((val & DRAM_WR_REQ) == 0)
			break;
	}
	for (i = 0; i < size / sizeof(uint64_t); i++)
		memory[0] = memory[i];
}

static void
ecc_ei_inject(int count)
{
	vm_offset_t memory;
	int injected;

	KASSERT((quadrant & ~QUADRANT_MASK) == 0,
	    ("quadrant value is outside of range: %u", quadrant));
	KASSERT(word_mask != 0 && (word_mask & ~INJ_WORD_MASK) == 0,
	    ("word mask value is outside of range: 0x%x", word_mask));
	KASSERT(bit_mask != 0 && (bit_mask & ~INJ_VECTOR_MASK) == 0,
	    ("bit mask value is outside of range: 0x%x", bit_mask));

	memory = kmem_alloc_attr(PAGE_SIZE, M_WAITOK, 0, ~0,
	    VM_MEMATTR_UNCACHEABLE);

	for (injected = 0; injected < count; injected++) {
		ecc_ei_inject_one((void*)memory, PAGE_SIZE);
		if (delay_ms != 0 && injected != count - 1)
			pause_sbt("ecc_ei_inject", delay_ms * SBT_1MS, 0, 0);
	}

	kmem_free(memory, PAGE_SIZE);
}

static int
ecc_ei_load(void)
{
	uint32_t val;

	if (cpu_vendor_id != CPU_VENDOR_AMD || CPUID_TO_FAMILY(cpu_id) < 0x10) {
		printf("DRAM ECC error injection is not supported\n");
		return (ENXIO);
	}
	nbdev = pci_find_bsf(0, 24, 3);
	if (nbdev == NULL) {
		printf("Couldn't find NB PCI device\n");
		return (ENXIO);
	}
	val = pci_read_config(nbdev, NB_MCA_CFG, 4);
	if ((val & DRAM_ECC_EN) == 0) {
		printf("DRAM ECC is not supported or disabled\n");
		return (ENXIO);
	}
	printf("DRAM ECC error injection support loaded\n");
	return (0);
}

static int
tsc_modevent(module_t mod __unused, int type, void *data __unused)
{
	int error;

	error = 0;
	switch (type) {
	case MOD_LOAD:
		error = ecc_ei_load();
		break;
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (0);
}

DEV_MODULE(tsc, tsc_modevent, NULL);
