/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1989, 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software developed by the Computer Systems
 * Engineering group at Lawrence Berkeley Laboratory under DARPA contract
 * BG 91-66 and contributed to Berkeley.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
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
__SCCSID("@(#)kvm.c	8.2 (Berkeley) 2/13/94");

#include <sys/param.h>
#include <sys/fnv_hash.h>

#define	_WANT_VNET

#include <sys/user.h>
#include <sys/linker.h>
#include <sys/pcpu.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <net/vnet.h>

#include <fcntl.h>
#include <kvm.h>
#include <limits.h>
#include <paths.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kvm_private.h"

SET_DECLARE(kvm_arch, struct kvm_arch);

static char _kd_is_null[] = "";

char *
kvm_geterr(kvm_t *kd)
{

	if (kd == NULL)
		return (_kd_is_null);
	return (kd->errbuf);
}

static int
_kvm_read_kernel_ehdr(kvm_t *kd)
{
	Elf *elf;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		_kvm_err(kd, kd->program, "Unsupported libelf");
		return (-1);
	}
	elf = elf_begin(kd->nlfd, ELF_C_READ, NULL);
	if (elf == NULL) {
		_kvm_err(kd, kd->program, "%s", elf_errmsg(0));
		return (-1);
	}
	if (elf_kind(elf) != ELF_K_ELF) {
		_kvm_err(kd, kd->program, "kernel is not an ELF file");
		return (-1);
	}
	if (gelf_getehdr(elf, &kd->nlehdr) == NULL) {
		_kvm_err(kd, kd->program, "%s", elf_errmsg(0));
		elf_end(elf);
		return (-1);
	}
	elf_end(elf);

	switch (kd->nlehdr.e_ident[EI_DATA]) {
	case ELFDATA2LSB:
	case ELFDATA2MSB:
		return (0);
	default:
		_kvm_err(kd, kd->program,
		    "unsupported ELF data encoding for kernel");
		return (-1);
	}
}

static kvm_t *
_kvm_open(kvm_t *kd, const char *uf, const char *mf, int flag, char *errout)
{
	struct kvm_arch **parch;
	struct stat st;

	kd->vmfd = -1;
	kd->pmfd = -1;
	kd->nlfd = -1;
	kd->vmst = NULL;
	kd->procbase = NULL;
	kd->argspc = NULL;
	kd->argv = NULL;

	if (uf == NULL)
		uf = getbootfile();
	else if (strlen(uf) >= MAXPATHLEN) {
		_kvm_err(kd, kd->program, "exec file name too long");
		goto failed;
	}
	if (flag & ~O_RDWR) {
		_kvm_err(kd, kd->program, "bad flags arg");
		goto failed;
	}
	if (mf == NULL)
		mf = _PATH_MEM;

	if ((kd->pmfd = open(mf, flag | O_CLOEXEC, 0)) < 0) {
		_kvm_syserr(kd, kd->program, "%s", mf);
		goto failed;
	}
	if (fstat(kd->pmfd, &st) < 0) {
		_kvm_syserr(kd, kd->program, "%s", mf);
		goto failed;
	}
	if (S_ISREG(st.st_mode) && st.st_size <= 0) {
		errno = EINVAL;
		_kvm_syserr(kd, kd->program, "empty file");
		goto failed;
	}
	if (S_ISCHR(st.st_mode)) {
		/*
		 * If this is a character special device, then check that
		 * it's /dev/mem.  If so, open kmem too.  (Maybe we should
		 * make it work for either /dev/mem or /dev/kmem -- in either
		 * case you're working with a live kernel.)
		 */
		if (strcmp(mf, _PATH_DEVNULL) == 0) {
			kd->vmfd = open(_PATH_DEVNULL, O_RDONLY | O_CLOEXEC);
			return (kd);
		} else if (strcmp(mf, _PATH_MEM) == 0) {
			if ((kd->vmfd = open(_PATH_KMEM, flag | O_CLOEXEC)) <
			    0) {
				_kvm_syserr(kd, kd->program, "%s", _PATH_KMEM);
				goto failed;
			}
			return (kd);
		}
	}

	/*
	 * This is either a crash dump or a remote live system with its physical
	 * memory fully accessible via a special device.
	 * Open the namelist fd and determine the architecture.
	 */
	if ((kd->nlfd = open(uf, O_RDONLY | O_CLOEXEC, 0)) < 0) {
		_kvm_syserr(kd, kd->program, "%s", uf);
		goto failed;
	}
	if (_kvm_read_kernel_ehdr(kd) < 0)
		goto failed;
	if (strncmp(mf, _PATH_FWMEM, strlen(_PATH_FWMEM)) == 0 ||
	    strncmp(mf, _PATH_DEVVMM, strlen(_PATH_DEVVMM)) == 0) {
		kd->rawdump = 1;
		kd->writable = 1;
	}
	SET_FOREACH(parch, kvm_arch) {
		if ((*parch)->ka_probe(kd)) {
			kd->arch = *parch;
			break;
		}
	}
	if (kd->arch == NULL) {
		_kvm_err(kd, kd->program, "unsupported architecture");
		goto failed;
	}

	/*
	 * Non-native kernels require a symbol resolver.
	 */
	if (!kd->arch->ka_native(kd) && kd->resolve_symbol == NULL) {
		_kvm_err(kd, kd->program,
		    "non-native kernel requires a symbol resolver");
		goto failed;
	}

	/*
	 * Initialize the virtual address translation machinery.
	 */
	if (kd->arch->ka_initvtop(kd) < 0)
		goto failed;
	return (kd);
failed:
	/*
	 * Copy out the error if doing sane error semantics.
	 */
	if (errout != NULL)
		strlcpy(errout, kd->errbuf, _POSIX2_LINE_MAX);
	(void)kvm_close(kd);
	return (NULL);
}

kvm_t *
kvm_openfiles(const char *uf, const char *mf, const char *sf __unused, int flag,
    char *errout)
{
	kvm_t *kd;

	if ((kd = calloc(1, sizeof(*kd))) == NULL) {
		if (errout != NULL)
			(void)strlcpy(errout, strerror(errno),
			    _POSIX2_LINE_MAX);
		return (NULL);
	}
	return (_kvm_open(kd, uf, mf, flag, errout));
}

kvm_t *
kvm_open(const char *uf, const char *mf, const char *sf __unused, int flag,
    const char *errstr)
{
	kvm_t *kd;

	if ((kd = calloc(1, sizeof(*kd))) == NULL) {
		if (errstr != NULL)
			(void)fprintf(stderr, "%s: %s\n",
				      errstr, strerror(errno));
		return (NULL);
	}
	kd->program = errstr;
	return (_kvm_open(kd, uf, mf, flag, NULL));
}

kvm_t *
kvm_open2(const char *uf, const char *mf, int flag, char *errout,
    int (*resolver)(const char *, kvaddr_t *))
{
	kvm_t *kd;

	if ((kd = calloc(1, sizeof(*kd))) == NULL) {
		if (errout != NULL)
			(void)strlcpy(errout, strerror(errno),
			    _POSIX2_LINE_MAX);
		return (NULL);
	}
	kd->resolve_symbol = resolver;
	return (_kvm_open(kd, uf, mf, flag, errout));
}

int
kvm_close(kvm_t *kd)
{
	int error = 0;

	if (kd == NULL) {
		errno = EINVAL;
		return (-1);
	}
	if (kd->vmst != NULL)
		kd->arch->ka_freevtop(kd);
	if (kd->pmfd >= 0)
		error |= close(kd->pmfd);
	if (kd->vmfd >= 0)
		error |= close(kd->vmfd);
	if (kd->nlfd >= 0)
		error |= close(kd->nlfd);
	if (kd->procbase != 0)
		free((void *)kd->procbase);
	if (kd->argbuf != 0)
		free((void *) kd->argbuf);
	if (kd->argspc != 0)
		free((void *) kd->argspc);
	if (kd->argv != 0)
		free((void *)kd->argv);
	if (kd->pt_map != NULL)
		free(kd->pt_map);
	if (kd->page_map != NULL)
		free(kd->page_map);
	if (kd->sparse_map != MAP_FAILED)
		munmap(kd->sparse_map, kd->pt_sparse_size);
	free((void *)kd);

	return (error);
}

int
kvm_nlist2(kvm_t *kd, struct kvm_nlist *nl)
{

	/*
	 * If called via the public interface, permit initialization of
	 * further virtualized modules on demand.
	 */
	return (_kvm_nlist(kd, nl, 1));
}

int
kvm_nlist(kvm_t *kd, struct nlist *nl)
{
	struct kvm_nlist *kl;
	int count, i, nfail;

	/*
	 * Avoid reporting truncated addresses by failing for non-native
	 * cores.
	 */
	if (!kvm_native(kd)) {
		_kvm_err(kd, kd->program, "kvm_nlist of non-native vmcore");
		return (-1);
	}

	for (count = 0; nl[count].n_name != NULL && nl[count].n_name[0] != '\0';
	     count++)
		;
	if (count == 0)
		return (0);
	kl = calloc(count + 1, sizeof(*kl));
	for (i = 0; i < count; i++)
		kl[i].n_name = nl[i].n_name;
	nfail = kvm_nlist2(kd, kl);
	for (i = 0; i < count; i++) {
		nl[i].n_type = kl[i].n_type;
		nl[i].n_other = 0;
		nl[i].n_desc = 0;
		nl[i].n_value = kl[i].n_value;
	}
	return (nfail);
}

ssize_t
kvm_read(kvm_t *kd, u_long kva, void *buf, size_t len)
{

	return (kvm_read2(kd, kva, buf, len));
}

ssize_t
kvm_read2(kvm_t *kd, kvaddr_t kva, void *buf, size_t len)
{
	int cc;
	ssize_t cr;
	off_t pa;
	char *cp;

	if (ISALIVE(kd)) {
		/*
		 * We're using /dev/kmem.  Just read straight from the
		 * device and let the active kernel do the address translation.
		 */
		errno = 0;
		if (lseek(kd->vmfd, (off_t)kva, 0) == -1 && errno != 0) {
			_kvm_err(kd, 0, "invalid address (0x%jx)",
			    (uintmax_t)kva);
			return (-1);
		}
		cr = read(kd->vmfd, buf, len);
		if (cr < 0) {
			_kvm_syserr(kd, 0, "kvm_read");
			return (-1);
		} else if (cr < (ssize_t)len)
			_kvm_err(kd, kd->program, "short read");
		return (cr);
	}

	cp = buf;
	while (len > 0) {
		cc = kd->arch->ka_kvatop(kd, kva, &pa);
		if (cc == 0)
			return (-1);
		if (cc > (ssize_t)len)
			cc = len;
		errno = 0;
		if (lseek(kd->pmfd, pa, 0) == -1 && errno != 0) {
			_kvm_syserr(kd, 0, _PATH_MEM);
			break;
		}
		cr = read(kd->pmfd, cp, cc);
		if (cr < 0) {
			_kvm_syserr(kd, kd->program, "kvm_read");
			break;
		}
		/*
		 * If ka_kvatop returns a bogus value or our core file is
		 * truncated, we might wind up seeking beyond the end of the
		 * core file in which case the read will return 0 (EOF).
		 */
		if (cr == 0)
			break;
		cp += cr;
		kva += cr;
		len -= cr;
	}

	return (cp - (char *)buf);
}

ssize_t
kvm_write(kvm_t *kd, u_long kva, const void *buf, size_t len)
{
	int cc;
	ssize_t cw;
	off_t pa;
	const char *cp;

	if (!ISALIVE(kd) && !kd->writable) {
		_kvm_err(kd, kd->program,
		    "kvm_write not implemented for dead kernels");
		return (-1);
	}

	if (ISALIVE(kd)) {
		/*
		 * Just like kvm_read, only we write.
		 */
		errno = 0;
		if (lseek(kd->vmfd, (off_t)kva, 0) == -1 && errno != 0) {
			_kvm_err(kd, 0, "invalid address (%lx)", kva);
			return (-1);
		}
		cc = write(kd->vmfd, buf, len);
		if (cc < 0) {
			_kvm_syserr(kd, 0, "kvm_write");
			return (-1);
		} else if ((size_t)cc < len)
			_kvm_err(kd, kd->program, "short write");
		return (cc);
	}

	cp = buf;
	while (len > 0) {
		cc = kd->arch->ka_kvatop(kd, kva, &pa);
		if (cc == 0)
			return (-1);
		if (cc > (ssize_t)len)
			cc = len;
		errno = 0;
		if (lseek(kd->pmfd, pa, 0) == -1 && errno != 0) {
			_kvm_syserr(kd, 0, _PATH_MEM);
			break;
		}
		cw = write(kd->pmfd, cp, cc);
		if (cw < 0) {
			_kvm_syserr(kd, kd->program, "kvm_write");
			break;
		}
		/*
		 * If ka_kvatop returns a bogus value or our core file is
		 * truncated, we might wind up seeking beyond the end of the
		 * core file in which case the read will return 0 (EOF).
		 */
		if (cw == 0)
			break;
		cp += cw;
		kva += cw;
		len -= cw;
	}

	return (cp - (const char *)buf);
}

int
kvm_native(kvm_t *kd)
{

	if (ISALIVE(kd))
		return (1);
	return (kd->arch->ka_native(kd));
}

int
kvm_walk_pages(kvm_t *kd, kvm_walk_pages_cb_t *cb, void *closure)
{

	if (kd->arch->ka_walk_pages == NULL)
		return (0);

	return (kd->arch->ka_walk_pages(kd, cb, closure));
}
