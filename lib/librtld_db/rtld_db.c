/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Rui Paulo under sponsorship from the
 * FreeBSD Foundation.
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
#include <sys/sysctl.h>
#include <sys/user.h>

#include <assert.h>
#include <err.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <machine/elf.h>

#include <libelf.h>
#include <libproc.h>
#include <libprocstat.h>
#include <libutil.h>

#include "rtld_db.h"

static int _librtld_db_debug = 0;
#define DPRINTF(...) do {				\
	if (_librtld_db_debug) {			\
		fprintf(stderr, "librtld_db: DEBUG: ");	\
		fprintf(stderr, __VA_ARGS__);		\
	}						\
} while (0)

void
rd_delete(rd_agent_t *rdap)
{

	if (rdap->rda_procstat != NULL)
		procstat_close(rdap->rda_procstat);
	free(rdap);
}

const char *
rd_errstr(rd_err_e rderr)
{

	switch (rderr) {
	case RD_ERR:
		return "generic error";
	case RD_OK:
		return "no error";
	case RD_NOCAPAB:
		return "capability not supported";
	case RD_DBERR:
		return "database error";
	case RD_NOBASE:
		return "NOBASE";
	case RD_NOMAPS:
		return "NOMAPS";
	default:
		return "unknown error";
	}
}

rd_err_e
rd_event_addr(rd_agent_t *rdap, rd_event_e event, rd_notify_t *notify)
{
	rd_err_e ret;

	DPRINTF("%s rdap %p event %d notify %p\n", __func__, rdap, event,
	    notify);

	ret = RD_OK;
	switch (event) {
	case RD_NONE:
		break;
	case RD_PREINIT:
		notify->type = RD_NOTIFY_BPT;
		notify->u.bptaddr = rdap->rda_preinit_addr;
		break;
	case RD_POSTINIT:
		notify->type = RD_NOTIFY_BPT;
		notify->u.bptaddr = rdap->rda_postinit_addr;
		break;
	case RD_DLACTIVITY:
		notify->type = RD_NOTIFY_BPT;
		notify->u.bptaddr = rdap->rda_dlactivity_addr;
		break;
	default:
		ret = RD_ERR;
		break;
	}
	return (ret);
}

rd_err_e
rd_event_enable(rd_agent_t *rdap __unused, int onoff)
{
	DPRINTF("%s onoff %d\n", __func__, onoff);

	return (RD_OK);
}

rd_err_e
rd_event_getmsg(rd_agent_t *rdap __unused, rd_event_msg_t *msg)
{
	DPRINTF("%s\n", __func__);

	msg->type = RD_POSTINIT;
	msg->u.state = RD_CONSISTENT;

	return (RD_OK);
}

rd_err_e
rd_init(int version)
{
	char *debug = NULL;

	if (version == RD_VERSION) {
		debug = getenv("LIBRTLD_DB_DEBUG");
		_librtld_db_debug = debug ? atoi(debug) : 0;
		return (RD_OK);
	} else
		return (RD_NOCAPAB);
}

rd_err_e
rd_loadobj_iter(rd_agent_t *rdap, rl_iter_f *cb, void *clnt_data)
{
	struct kinfo_vmentry *kves, *kve;
	rd_loadobj_t rdl;
	rd_err_e ret;
	int cnt, i, lastvn;

	DPRINTF("%s\n", __func__);

	if ((kves = kinfo_getvmmap(proc_getpid(rdap->rda_php), &cnt)) == NULL) {
		warn("ERROR: kinfo_getvmmap() failed");
		return (RD_ERR);
	}

	ret = RD_OK;
	lastvn = 0;
	for (i = 0; i < cnt; i++) {
		kve = kves + i;
		if (kve->kve_type == KVME_TYPE_VNODE)
			lastvn = i;
		memset(&rdl, 0, sizeof(rdl));
		/*
		 * Map the kinfo_vmentry struct to the rd_loadobj structure.
		 */
		rdl.rdl_saddr = kve->kve_start;
		rdl.rdl_eaddr = kve->kve_end;
		rdl.rdl_offset = kve->kve_offset;
		if (kve->kve_protection & KVME_PROT_READ)
			rdl.rdl_prot |= RD_RDL_R;
		if (kve->kve_protection & KVME_PROT_WRITE)
			rdl.rdl_prot |= RD_RDL_W;
		if (kve->kve_protection & KVME_PROT_EXEC)
			rdl.rdl_prot |= RD_RDL_X;
		strlcpy(rdl.rdl_path, kves[lastvn].kve_path,
		    sizeof(rdl.rdl_path));
		if ((*cb)(&rdl, clnt_data) != 0) {
			ret = RD_ERR;
			break;
		}
	}
	free(kves);
	return (ret);
}

void
rd_log(const int onoff)
{
	DPRINTF("%s\n", __func__);

	(void)onoff;
}

rd_agent_t *
rd_new(struct proc_handle *php)
{
	rd_agent_t *rdap;

	rdap = malloc(sizeof(*rdap));
	if (rdap == NULL)
		return (NULL);

	memset(rdap, 0, sizeof(rd_agent_t));
	rdap->rda_php = php;
	rdap->rda_procstat = procstat_open_sysctl();

	if (rd_reset(rdap) != RD_OK) {
		rd_delete(rdap);
		rdap = NULL;
	}
	return (rdap);
}

rd_err_e
rd_objpad_enable(rd_agent_t *rdap, size_t padsize)
{
	DPRINTF("%s\n", __func__);

	(void)rdap;
	(void)padsize;

	return (RD_ERR);
}

rd_err_e
rd_plt_resolution(rd_agent_t *rdap, uintptr_t pc, struct proc *proc,
    uintptr_t plt_base, rd_plt_info_t *rpi)
{
	DPRINTF("%s\n", __func__);

	(void)rdap;
	(void)pc;
	(void)proc;
	(void)plt_base;
	(void)rpi;

	return (RD_ERR);
}

static int
rtld_syms(rd_agent_t *rdap, const char *rtldpath, u_long base)
{
	GElf_Shdr shdr;
	GElf_Sym sym;
	Elf *e;
	Elf_Data *data;
	Elf_Scn *scn;
	const char *symname;
	Elf64_Word strscnidx;
	int fd, i, ret;

	ret = 1;
	e = NULL;

	fd = open(rtldpath, O_RDONLY);
	if (fd < 0)
		goto err;

	if (elf_version(EV_CURRENT) == EV_NONE)
		goto err;
	e = elf_begin(fd, ELF_C_READ, NULL);
	if (e == NULL)
		goto err;

	scn = NULL;
	while ((scn = elf_nextscn(e, scn)) != NULL) {
		gelf_getshdr(scn, &shdr);
		if (shdr.sh_type == SHT_DYNSYM)
			break;
	}
	if (scn == NULL)
		goto err;

	strscnidx = shdr.sh_link;
	data = elf_getdata(scn, NULL);
	if (data == NULL)
		goto err;

	for (i = 0; gelf_getsym(data, i, &sym) != NULL; i++) {
		if (GELF_ST_TYPE(sym.st_info) != STT_FUNC ||
		    GELF_ST_BIND(sym.st_info) != STB_GLOBAL)
			continue;
		symname = elf_strptr(e, strscnidx, sym.st_name);
		if (symname == NULL)
			continue;

		if (strcmp(symname, "r_debug_state") == 0) {
			rdap->rda_preinit_addr = sym.st_value + base;
			rdap->rda_dlactivity_addr = sym.st_value + base;
		} else if (strcmp(symname, "_r_debug_postinit") == 0) {
			rdap->rda_postinit_addr = sym.st_value + base;
		}
	}

	if (rdap->rda_preinit_addr != 0 &&
	    rdap->rda_postinit_addr != 0 &&
	    rdap->rda_dlactivity_addr != 0)
		ret = 0;

err:
	if (e != NULL)
		(void)elf_end(e);
	if (fd >= 0)
		(void)close(fd);
	return (ret);
}

rd_err_e
rd_reset(rd_agent_t *rdap)
{
	struct kinfo_proc *kp;
	struct kinfo_vmentry *kve;
	Elf_Auxinfo *auxv;
	const char *rtldpath;
	u_long base;
	rd_err_e rderr;
	int count, i;

	kp = NULL;
	auxv = NULL;
	kve = NULL;
	rderr = RD_ERR;

	kp = procstat_getprocs(rdap->rda_procstat, KERN_PROC_PID,
	    proc_getpid(rdap->rda_php), &count);
	if (kp == NULL)
		return (RD_ERR);
	assert(count == 1);

	auxv = procstat_getauxv(rdap->rda_procstat, kp, &count);
	if (auxv == NULL)
		goto err;

	base = 0;
	for (i = 0; i < count; i++) {
		if (auxv[i].a_type == AT_BASE) {
			base = auxv[i].a_un.a_val;
			break;
		}
	}
	if (i == count)
		goto err;

	rtldpath = NULL;
	kve = procstat_getvmmap(rdap->rda_procstat, kp, &count);
	if (kve == NULL)
		goto err;
	for (i = 0; i < count; i++) {
		if (kve[i].kve_start == base) {
			rtldpath = kve[i].kve_path;
			break;
		}
	}
	if (i == count)
		goto err;

	if (rtld_syms(rdap, rtldpath, base) != 0)
		goto err;

	rderr = RD_OK;

err:
	if (kve != NULL)
		procstat_freevmmap(rdap->rda_procstat, kve);
	if (auxv != NULL)
		procstat_freeauxv(rdap->rda_procstat, auxv);
	if (kp != NULL)
		procstat_freeprocs(rdap->rda_procstat, kp);
	return (rderr);
}
