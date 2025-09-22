/*	$OpenBSD: pmon.c,v 1.8 2017/05/21 14:22:36 visa Exp $	*/

/*
 * Copyright (c) 2009, 2012 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>

#include <machine/cpu.h>
#include <machine/pmon.h>

int	pmon_argc;
int32_t	*pmon_argv;
int32_t	*pmon_envp;
vaddr_t pmon_envbase;
int	pmon_envtype;

void
pmon_init(int32_t argc, int32_t argv, int32_t envp, int32_t callvec,
    uint32_t prid)
{
	pmon_callvec = callvec;

	pmon_argc = argc;
	/* sign extend pointers */
	pmon_argv = (int32_t *)(vaddr_t)argv;
	pmon_envp = (int32_t *)(vaddr_t)envp;

	/*
	 * Those ``smart'' brains at Lemote have decided to change the way
	 * system information is passed from PMON to the kernel: earlier
	 * PMON versions pass the usual environment array, while newer
	 * versions pass a struct, without any form of magic number in it
	 * (beginner's mistake #1).
	 *
	 * Of course, to make things worse, quoting
	 * http://www.linux-mips.org/archives/linux-mips/2012-08/msg00233.html :
	 * ``All Loongson-based machines support this new interface except
	 * 2E/2F series.''
	 *
	 * This is obviously not true, there are 3A systems which pass the
	 * good old environment variables around... I have such a system here.
	 */

	switch (prid & 0xff) {
	case 0x02:	/* 2E */
	case 0x03:	/* 2F */
		pmon_envtype = PMON_ENVTYPE_ENVP;
		pmon_envbase = CKSEG1_BASE;
		break;
	default:
	    {
		struct pmon_env *env = (struct pmon_env *)pmon_envp;

		/*
		 * `pmon_envp' either points to an EFI struct or to a
		 * NULL-terminated array of 32-bit pointers.
		 *
		 * Since the EFI struct starts with a few 64 bit pointers,
		 * it should be easy to figure out which flavour we are
		 * facing by checking the top 32 bits of these 64 bit
		 * pointers.
		 *
		 * However, not all of these pointers are actually initialized
		 * by the firmware (beginner's mistake #2).  Therefore, we can
		 * only safely check the first two fields of the `smbios'
		 * struct:
		 * - the version number must be small
		 * - the `vga bios' pointer must point to the kseg0 segment.
		 *
		 * Of course, I can reasonably expect these assumptions to
		 * be broken in future systems.  Where would be the fun if
		 * not?
		 */
		if (env->efi.bios.version < 0x2000 &&
		    env->efi.bios.vga_bios >= CKSEG0_BASE &&
		    env->efi.bios.vga_bios < CKSEG0_BASE + CKSEG_SIZE) {
			pmon_envtype = PMON_ENVTYPE_EFI;
		} else {
			pmon_envtype = PMON_ENVTYPE_ENVP;
			pmon_envbase = CKSEG0_BASE;
		}
	    }
		break;
	}

	/*
	 * Figure out where the environment pointers are supposed to live.
	 * Loongson 2[EF] systems use uncached memory, while 2G onwards
	 * apparently use cached memory.
	 */
	if (pmon_envtype == PMON_ENVTYPE_ENVP) {
		pmon_envbase = (uint64_t)*pmon_envp < CKSEG1_BASE ?
		    CKSEG0_BASE : CKSEG1_BASE;
	}
}

const char *
pmon_getarg(const int argno)
{
	if (argno < 0 || argno >= pmon_argc)
		return NULL;

	return (const char *)(vaddr_t)pmon_argv[argno];
}

int
pmon_getenvtype()
{
	return pmon_envtype;
}

const char *
pmon_getenv(const char *var)
{
	int32_t *envptr = pmon_envp;
	const char *envstr;
	size_t varlen;

	if (envptr == NULL || pmon_envtype != PMON_ENVTYPE_ENVP)
		return NULL;

	varlen = strlen(var);
	while (*envptr != 0) {
		envstr = (const char *)(vaddr_t)*envptr;
		/*
		 * There is a PMON2000 bug, at least on Lemote Yeeloong,
		 * which causes it to override part of the environment
		 * pointers array with the environment data itself.
		 *
		 * This only happens on cold boot, and if the BSD kernel
		 * is loaded without symbols (i.e. no option -k passed
		 * to the boot command).
		 *
		 * Until a suitable workaround is found or the bug is
		 * fixed, ignore broken environment information and
		 * tell the user (in case this prevents us from finding
		 * important information).
		 */
		if ((vaddr_t)envstr - pmon_envbase >= CKSEG_SIZE) {
			printf("WARNING! CORRUPTED ENVIRONMENT!\n");
			printf("Unable to search for \"%s\".\n", var);
#ifdef _STANDALONE
			printf("If boot fails, power-cycle the machine.\n");
#else
			printf("If the kernel fails to identify the system"
			    " type, please boot it again with `-k' option.\n");
#endif

			/* terminate environment for further calls */
			*envptr = 0;
			break;
		}
		if (strncmp(envstr, var, varlen) == 0 &&
		    envstr[varlen] == '=')
			return envstr + varlen + 1;
		envptr++;
	}

	return NULL;
}

const struct pmon_env_reset *
pmon_get_env_reset(void)
{
	struct pmon_env *env = (struct pmon_env *)pmon_envp;

	if (pmon_envtype != PMON_ENVTYPE_EFI)
		return NULL;

	return &env->reset;
}

const struct pmon_env_smbios *
pmon_get_env_smbios(void)
{
	struct pmon_env *env = (struct pmon_env *)pmon_envp;

	if (pmon_envtype != PMON_ENVTYPE_EFI)
		return NULL;

	return &env->efi.bios;
}

const struct pmon_env_mem *
pmon_get_env_mem(void)
{
	struct pmon_env *env = (struct pmon_env *)pmon_envp;
	uint64_t va = (uint64_t)&env->efi.bios.ptrs;

	if (pmon_envtype != PMON_ENVTYPE_EFI)
		return NULL;

	return (const struct pmon_env_mem *)
	    (va + env->efi.bios.ptrs.offs_mem);
}

const struct pmon_env_cpu *
pmon_get_env_cpu(void)
{
	struct pmon_env *env = (struct pmon_env *)pmon_envp;
	uint64_t va = (uint64_t)&env->efi.bios.ptrs;

	if (pmon_envtype != PMON_ENVTYPE_EFI)
		return NULL;

	return (const struct pmon_env_cpu *)
	    (va + env->efi.bios.ptrs.offs_cpu);
}

const struct pmon_env_sys *
pmon_get_env_sys(void)
{
	struct pmon_env *env = (struct pmon_env *)pmon_envp;
	uint64_t va = (uint64_t)&env->efi.bios.ptrs;

	if (pmon_envtype != PMON_ENVTYPE_EFI)
		return NULL;

	return (const struct pmon_env_sys *)
	    (va + env->efi.bios.ptrs.offs_sys);
}

const struct pmon_env_irq *
pmon_get_env_irq(void)
{
	struct pmon_env *env = (struct pmon_env *)pmon_envp;
	uint64_t va = (uint64_t)&env->efi.bios.ptrs;

	if (pmon_envtype != PMON_ENVTYPE_EFI)
		return NULL;

	return (const struct pmon_env_irq *)
	    (va + env->efi.bios.ptrs.offs_irq);
}

const struct pmon_env_iface *
pmon_get_env_iface(void)
{
	struct pmon_env *env = (struct pmon_env *)pmon_envp;
	uint64_t va = (uint64_t)&env->efi.bios.ptrs;

	if (pmon_envtype != PMON_ENVTYPE_EFI)
		return NULL;

	return (const struct pmon_env_iface *)
	    (va + env->efi.bios.ptrs.offs_iface);
}

const struct pmon_env_special *
pmon_get_env_special(void)
{
	struct pmon_env *env = (struct pmon_env *)pmon_envp;
	uint64_t va = (uint64_t)&env->efi.bios.ptrs;

	if (pmon_envtype != PMON_ENVTYPE_EFI)
		return NULL;

	return (const struct pmon_env_special *)
	    (va + env->efi.bios.ptrs.offs_special);
}

const struct pmon_env_device *
pmon_get_env_device(void)
{
	struct pmon_env *env = (struct pmon_env *)pmon_envp;
	uint64_t va = (uint64_t)&env->efi.bios.ptrs;

	if (pmon_envtype != PMON_ENVTYPE_EFI)
		return NULL;

	return (const struct pmon_env_device *)
	    (va + env->efi.bios.ptrs.offs_device);
}
