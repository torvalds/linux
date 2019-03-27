/*-
 * Copyright (c) 2015 Nuxi, https://nuxi.nl/
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

#include <sys/param.h>
#include <sys/imgact.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/sysent.h>
#include <sys/systm.h>

#include <contrib/cloudabi/cloudabi64_types.h>

#include <compat/cloudabi/cloudabi_util.h>

#include <compat/cloudabi64/cloudabi64_util.h>

extern char _binary_cloudabi64_vdso_o_start[];
extern char _binary_cloudabi64_vdso_o_end[];

register_t *
cloudabi64_copyout_strings(struct image_params *imgp)
{
	struct image_args *args;
	uintptr_t begin;
	size_t len;

	/* Copy out program arguments. */
	args = imgp->args;
	len = exec_args_get_begin_envv(args) - args->begin_argv;
	begin = rounddown2(imgp->sysent->sv_usrstack - len, sizeof(register_t));
	copyout(args->begin_argv, (void *)begin, len);
	return ((register_t *)begin);
}

int
cloudabi64_fixup(register_t **stack_base, struct image_params *imgp)
{
	char canarybuf[64], pidbuf[16];
	Elf64_Auxargs *args;
	struct thread *td;
	void *argdata, *canary, *pid;
	size_t argdatalen;
	int error;

	/*
	 * CloudABI executables do not store the FreeBSD OS release
	 * number in their header. Set the OS release number to the
	 * latest version of FreeBSD, so that system calls behave as if
	 * called natively.
	 */
	td = curthread;
	td->td_proc->p_osrel = __FreeBSD_version;

	argdata = *stack_base;

	/* Store canary for stack smashing protection. */
	arc4rand(canarybuf, sizeof(canarybuf), 0);
	*stack_base -= howmany(sizeof(canarybuf), sizeof(register_t));
	canary = *stack_base;
	error = copyout(canarybuf, canary, sizeof(canarybuf));
	if (error != 0)
		return (error);

	/*
	 * Generate a random UUID that identifies the process. Right now
	 * we don't store this UUID in the kernel. Ideally, it should be
	 * exposed through ps(1).
	 */
	arc4rand(pidbuf, sizeof(pidbuf), 0);
	pidbuf[6] = (pidbuf[6] & 0x0f) | 0x40;
	pidbuf[8] = (pidbuf[8] & 0x3f) | 0x80;
	*stack_base -= howmany(sizeof(pidbuf), sizeof(register_t));
	pid = *stack_base;
	error = copyout(pidbuf, pid, sizeof(pidbuf));
	if (error != 0)
		return (error);

	/*
	 * Compute length of program arguments. As the argument data is
	 * binary safe, we had to add a trailing null byte in
	 * exec_copyin_data_fds(). Undo this by reducing the length.
	 */
	args = (Elf64_Auxargs *)imgp->auxargs;
	argdatalen = exec_args_get_begin_envv(imgp->args) -
	    imgp->args->begin_argv;
	if (argdatalen > 0)
		--argdatalen;

	/* Write out an auxiliary vector. */
	cloudabi64_auxv_t auxv[] = {
#define	VAL(type, val)	{ .a_type = (type), .a_val = (val) }
#define	PTR(type, ptr)	{ .a_type = (type), .a_ptr = (uintptr_t)(ptr) }
		PTR(CLOUDABI_AT_ARGDATA, argdata),
		VAL(CLOUDABI_AT_ARGDATALEN, argdatalen),
		VAL(CLOUDABI_AT_BASE, args->base),
		PTR(CLOUDABI_AT_CANARY, canary),
		VAL(CLOUDABI_AT_CANARYLEN, sizeof(canarybuf)),
		VAL(CLOUDABI_AT_NCPUS, mp_ncpus),
		VAL(CLOUDABI_AT_PAGESZ, args->pagesz),
		PTR(CLOUDABI_AT_PHDR, args->phdr),
		VAL(CLOUDABI_AT_PHNUM, args->phnum),
		PTR(CLOUDABI_AT_PID, pid),
		PTR(CLOUDABI_AT_SYSINFO_EHDR,
		    imgp->proc->p_sysent->sv_shared_page_base),
		VAL(CLOUDABI_AT_TID, td->td_tid),
#undef VAL
#undef PTR
		{ .a_type = CLOUDABI_AT_NULL },
	};
	*stack_base -= howmany(sizeof(auxv), sizeof(register_t));
	error = copyout(auxv, *stack_base, sizeof(auxv));
	if (error != 0)
		return (error);

	/* Reserve space for storing the TCB. */
	*stack_base -= howmany(sizeof(cloudabi64_tcb_t), sizeof(register_t));
	return (0);
}

static int
cloudabi64_modevent(module_t mod, int type, void *data)
{

	switch (type) {
	case MOD_LOAD:
		cloudabi_vdso_init(cloudabi64_brand.sysvec,
		    _binary_cloudabi64_vdso_o_start,
		    _binary_cloudabi64_vdso_o_end);
		if (elf64_insert_brand_entry(&cloudabi64_brand) < 0) {
			printf("Failed to add CloudABI ELF brand handler\n");
			return (EINVAL);
		}
		return (0);
	case MOD_UNLOAD:
		if (elf64_brand_inuse(&cloudabi64_brand))
			return (EBUSY);
		if (elf64_remove_brand_entry(&cloudabi64_brand) < 0) {
			printf("Failed to remove CloudABI ELF brand handler\n");
			return (EINVAL);
		}
		cloudabi_vdso_destroy(cloudabi64_brand.sysvec);
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

static moduledata_t cloudabi64_module = {
	"cloudabi64",
	cloudabi64_modevent,
	NULL
};

DECLARE_MODULE_TIED(cloudabi64, cloudabi64_module, SI_SUB_EXEC, SI_ORDER_ANY);
MODULE_DEPEND(cloudabi64, cloudabi, 1, 1, 1);
FEATURE(cloudabi64, "CloudABI 64bit support");
