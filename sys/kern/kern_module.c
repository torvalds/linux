/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997 Doug Rabson
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/eventhandler.h>
#include <sys/malloc.h>
#include <sys/sysproto.h>
#include <sys/sysent.h>
#include <sys/proc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/reboot.h>
#include <sys/sx.h>
#include <sys/module.h>
#include <sys/linker.h>

static MALLOC_DEFINE(M_MODULE, "module", "module data structures");

struct module {
	TAILQ_ENTRY(module)	link;	/* chain together all modules */
	TAILQ_ENTRY(module)	flink;	/* all modules in a file */
	struct linker_file	*file;	/* file which contains this module */
	int			refs;	/* reference count */
	int 			id;	/* unique id number */
	char 			*name;	/* module name */
	modeventhand_t 		handler;	/* event handler */
	void 			*arg;	/* argument for handler */
	modspecific_t 		data;	/* module specific data */
};

#define MOD_EVENT(mod, type)	(mod)->handler((mod), (type), (mod)->arg)

static TAILQ_HEAD(modulelist, module) modules;
struct sx modules_sx;
static int nextid = 1;
static void module_shutdown(void *, int);

static int
modevent_nop(module_t mod, int what, void *arg)
{

	switch(what) {
	case MOD_LOAD:
		return (0);
	case MOD_UNLOAD:
		return (EBUSY);
	default:
		return (EOPNOTSUPP);
	}
}

static void
module_init(void *arg)
{

	sx_init(&modules_sx, "module subsystem sx lock");
	TAILQ_INIT(&modules);
	EVENTHANDLER_REGISTER(shutdown_final, module_shutdown, NULL,
	    SHUTDOWN_PRI_DEFAULT);
}

SYSINIT(module, SI_SUB_KLD, SI_ORDER_FIRST, module_init, NULL);

static void
module_shutdown(void *arg1, int arg2)
{
	module_t mod;

	if (arg2 & RB_NOSYNC)
		return;
	mtx_lock(&Giant);
	MOD_SLOCK;
	TAILQ_FOREACH_REVERSE(mod, &modules, modulelist, link)
		MOD_EVENT(mod, MOD_SHUTDOWN);
	MOD_SUNLOCK;
	mtx_unlock(&Giant);
}

void
module_register_init(const void *arg)
{
	const moduledata_t *data = (const moduledata_t *)arg;
	int error;
	module_t mod;

	mtx_lock(&Giant);
	MOD_SLOCK;
	mod = module_lookupbyname(data->name);
	if (mod == NULL)
		panic("module_register_init: module named %s not found\n",
		    data->name);
	MOD_SUNLOCK;
	error = MOD_EVENT(mod, MOD_LOAD);
	if (error) {
		MOD_EVENT(mod, MOD_UNLOAD);
		MOD_XLOCK;
		module_release(mod);
		MOD_XUNLOCK;
		printf("module_register_init: MOD_LOAD (%s, %p, %p) error"
		    " %d\n", data->name, (void *)data->evhand, data->priv,
		    error); 
	} else {
		MOD_XLOCK;
		if (mod->file) {
			/*
			 * Once a module is successfully loaded, move
			 * it to the head of the module list for this
			 * linker file.  This resorts the list so that
			 * when the kernel linker iterates over the
			 * modules to unload them, it will unload them
			 * in the reverse order they were loaded.
			 */
			TAILQ_REMOVE(&mod->file->modules, mod, flink);
			TAILQ_INSERT_HEAD(&mod->file->modules, mod, flink);
		}
		MOD_XUNLOCK;
	}
	mtx_unlock(&Giant);
}

int
module_register(const moduledata_t *data, linker_file_t container)
{
	size_t namelen;
	module_t newmod;

	MOD_XLOCK;
	newmod = module_lookupbyname(data->name);
	if (newmod != NULL) {
		MOD_XUNLOCK;
		printf("%s: cannot register %s from %s; already loaded from %s\n",
		    __func__, data->name, container->filename, newmod->file->filename);
		return (EEXIST);
	}
	namelen = strlen(data->name) + 1;
	newmod = malloc(sizeof(struct module) + namelen, M_MODULE, M_WAITOK);
	newmod->refs = 1;
	newmod->id = nextid++;
	newmod->name = (char *)(newmod + 1);
	strcpy(newmod->name, data->name);
	newmod->handler = data->evhand ? data->evhand : modevent_nop;
	newmod->arg = data->priv;
	bzero(&newmod->data, sizeof(newmod->data));
	TAILQ_INSERT_TAIL(&modules, newmod, link);

	if (container)
		TAILQ_INSERT_TAIL(&container->modules, newmod, flink);
	newmod->file = container;
	MOD_XUNLOCK;
	return (0);
}

void
module_reference(module_t mod)
{

	MOD_XLOCK_ASSERT;

	MOD_DPF(REFS, ("module_reference: before, refs=%d\n", mod->refs));
	mod->refs++;
}

void
module_release(module_t mod)
{

	MOD_XLOCK_ASSERT;

	if (mod->refs <= 0)
		panic("module_release: bad reference count");

	MOD_DPF(REFS, ("module_release: before, refs=%d\n", mod->refs));
	
	mod->refs--;
	if (mod->refs == 0) {
		TAILQ_REMOVE(&modules, mod, link);
		if (mod->file)
			TAILQ_REMOVE(&mod->file->modules, mod, flink);
		free(mod, M_MODULE);
	}
}

module_t
module_lookupbyname(const char *name)
{
	module_t mod;
	int err;

	MOD_LOCK_ASSERT;

	TAILQ_FOREACH(mod, &modules, link) {
		err = strcmp(mod->name, name);
		if (err == 0)
			return (mod);
	}
	return (NULL);
}

module_t
module_lookupbyid(int modid)
{
        module_t mod;

        MOD_LOCK_ASSERT;

        TAILQ_FOREACH(mod, &modules, link)
                if (mod->id == modid)
                        return(mod);
        return (NULL);
}

int
module_quiesce(module_t mod)
{
	int error;

	mtx_lock(&Giant);
	error = MOD_EVENT(mod, MOD_QUIESCE);
	mtx_unlock(&Giant);
	if (error == EOPNOTSUPP || error == EINVAL)
		error = 0;
	return (error);
}

int
module_unload(module_t mod)
{
	int error;

	mtx_lock(&Giant);
	error = MOD_EVENT(mod, MOD_UNLOAD);
	mtx_unlock(&Giant);
	return (error);
}

int
module_getid(module_t mod)
{

	MOD_LOCK_ASSERT;
	return (mod->id);
}

module_t
module_getfnext(module_t mod)
{

	MOD_LOCK_ASSERT;
	return (TAILQ_NEXT(mod, flink));
}

const char *
module_getname(module_t mod)
{

	MOD_LOCK_ASSERT;
	return (mod->name);
}

void
module_setspecific(module_t mod, modspecific_t *datap)
{

	MOD_XLOCK_ASSERT;
	mod->data = *datap;
}

linker_file_t
module_file(module_t mod)
{

	return (mod->file);
}

/*
 * Syscalls.
 */
int
sys_modnext(struct thread *td, struct modnext_args *uap)
{
	module_t mod;
	int error = 0;

	td->td_retval[0] = -1;

	MOD_SLOCK;
	if (uap->modid == 0) {
		mod = TAILQ_FIRST(&modules);
		if (mod)
			td->td_retval[0] = mod->id;
		else
			error = ENOENT;
		goto done2;
	}
	mod = module_lookupbyid(uap->modid);
	if (mod == NULL) {
		error = ENOENT;
		goto done2;
	}
	if (TAILQ_NEXT(mod, link))
		td->td_retval[0] = TAILQ_NEXT(mod, link)->id;
	else
		td->td_retval[0] = 0;
done2:
	MOD_SUNLOCK;
	return (error);
}

int
sys_modfnext(struct thread *td, struct modfnext_args *uap)
{
	module_t mod;
	int error;

	td->td_retval[0] = -1;

	MOD_SLOCK;
	mod = module_lookupbyid(uap->modid);
	if (mod == NULL) {
		error = ENOENT;
	} else {
		error = 0;
		if (TAILQ_NEXT(mod, flink))
			td->td_retval[0] = TAILQ_NEXT(mod, flink)->id;
		else
			td->td_retval[0] = 0;
	}
	MOD_SUNLOCK;
	return (error);
}

struct module_stat_v1 {
	int	version;		/* set to sizeof(struct module_stat) */
	char	name[MAXMODNAME];
	int	refs;
	int	id;
};

int
sys_modstat(struct thread *td, struct modstat_args *uap)
{
	module_t mod;
	modspecific_t data;
	int error = 0;
	int id, namelen, refs, version;
	struct module_stat *stat;
	char *name;

	MOD_SLOCK;
	mod = module_lookupbyid(uap->modid);
	if (mod == NULL) {
		MOD_SUNLOCK;
		return (ENOENT);
	}
	id = mod->id;
	refs = mod->refs;
	name = mod->name;
	data = mod->data;
	MOD_SUNLOCK;
	stat = uap->stat;

	/*
	 * Check the version of the user's structure.
	 */
	if ((error = copyin(&stat->version, &version, sizeof(version))) != 0)
		return (error);
	if (version != sizeof(struct module_stat_v1)
	    && version != sizeof(struct module_stat))
		return (EINVAL);
	namelen = strlen(mod->name) + 1;
	if (namelen > MAXMODNAME)
		namelen = MAXMODNAME;
	if ((error = copyout(name, &stat->name[0], namelen)) != 0)
		return (error);

	if ((error = copyout(&refs, &stat->refs, sizeof(int))) != 0)
		return (error);
	if ((error = copyout(&id, &stat->id, sizeof(int))) != 0)
		return (error);

	/*
	 * >v1 stat includes module data.
	 */
	if (version == sizeof(struct module_stat))
		if ((error = copyout(&data, &stat->data, 
		    sizeof(data))) != 0)
			return (error);
	td->td_retval[0] = 0;
	return (error);
}

int
sys_modfind(struct thread *td, struct modfind_args *uap)
{
	int error = 0;
	char name[MAXMODNAME];
	module_t mod;

	if ((error = copyinstr(uap->name, name, sizeof name, 0)) != 0)
		return (error);

	MOD_SLOCK;
	mod = module_lookupbyname(name);
	if (mod == NULL)
		error = ENOENT;
	else
		td->td_retval[0] = module_getid(mod);
	MOD_SUNLOCK;
	return (error);
}

MODULE_VERSION(kernel, __FreeBSD_version);

#ifdef COMPAT_FREEBSD32
#include <sys/mount.h>
#include <sys/socket.h>
#include <compat/freebsd32/freebsd32_util.h>
#include <compat/freebsd32/freebsd32.h>
#include <compat/freebsd32/freebsd32_proto.h>

typedef union modspecific32 {
	int		intval;
	uint32_t	uintval;
	int		longval;
	uint32_t	ulongval;
} modspecific32_t;

struct module_stat32 {
	int		version;
	char		name[MAXMODNAME];
	int		refs;
	int		id;
	modspecific32_t	data;
};

int
freebsd32_modstat(struct thread *td, struct freebsd32_modstat_args *uap)
{
	module_t mod;
	modspecific32_t data32;
	int error = 0;
	int id, namelen, refs, version;
	struct module_stat32 *stat32;
	char *name;

	MOD_SLOCK;
	mod = module_lookupbyid(uap->modid);
	if (mod == NULL) {
		MOD_SUNLOCK;
		return (ENOENT);
	}

	id = mod->id;
	refs = mod->refs;
	name = mod->name;
	CP(mod->data, data32, intval);
	CP(mod->data, data32, uintval);
	CP(mod->data, data32, longval);
	CP(mod->data, data32, ulongval);
	MOD_SUNLOCK;
	stat32 = uap->stat;

	if ((error = copyin(&stat32->version, &version, sizeof(version))) != 0)
		return (error);
	if (version != sizeof(struct module_stat_v1)
	    && version != sizeof(struct module_stat32))
		return (EINVAL);
	namelen = strlen(mod->name) + 1;
	if (namelen > MAXMODNAME)
		namelen = MAXMODNAME;
	if ((error = copyout(name, &stat32->name[0], namelen)) != 0)
		return (error);

	if ((error = copyout(&refs, &stat32->refs, sizeof(int))) != 0)
		return (error);
	if ((error = copyout(&id, &stat32->id, sizeof(int))) != 0)
		return (error);

	/*
	 * >v1 stat includes module data.
	 */
	if (version == sizeof(struct module_stat32))
		if ((error = copyout(&data32, &stat32->data,
		    sizeof(data32))) != 0)
			return (error);
	td->td_retval[0] = 0;
	return (error);
}
#endif
