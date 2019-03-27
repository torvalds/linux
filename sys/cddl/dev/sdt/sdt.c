/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 *
 * Portions Copyright 2006-2008 John Birrell jb@freebsd.org
 *
 * $FreeBSD$
 *
 */

/*
 * This file contains a reimplementation of the statically-defined tracing (SDT)
 * framework for DTrace. Probes and SDT providers are defined using the macros
 * in sys/sdt.h, which append all the needed structures to linker sets. When
 * this module is loaded, it iterates over all of the loaded modules and
 * registers probes and providers with the DTrace framework based on the
 * contents of these linker sets.
 *
 * A list of SDT providers is maintained here since a provider may span multiple
 * modules. When a kernel module is unloaded, a provider defined in that module
 * is unregistered only if no other modules refer to it. The DTrace framework is
 * responsible for destroying individual probes when a kernel module is
 * unloaded; in particular, probes may not span multiple kernel modules.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <sys/conf.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/linker.h>
#include <sys/linker_set.h>
#include <sys/lock.h>
#include <sys/lockstat.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/sdt.h>

#include <sys/dtrace.h>
#include <sys/dtrace_bsd.h>

/* DTrace methods. */
static void	sdt_getargdesc(void *, dtrace_id_t, void *, dtrace_argdesc_t *);
static void	sdt_provide_probes(void *, dtrace_probedesc_t *);
static void	sdt_destroy(void *, dtrace_id_t, void *);
static void	sdt_enable(void *, dtrace_id_t, void *);
static void	sdt_disable(void *, dtrace_id_t, void *);

static void	sdt_load(void);
static int	sdt_unload(void);
static void	sdt_create_provider(struct sdt_provider *);
static void	sdt_create_probe(struct sdt_probe *);
static void	sdt_kld_load(void *, struct linker_file *);
static void	sdt_kld_unload_try(void *, struct linker_file *, int *);

static MALLOC_DEFINE(M_SDT, "SDT", "DTrace SDT providers");

static int sdt_probes_enabled_count;
static int lockstat_enabled_count;

static dtrace_pattr_t sdt_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
};

static dtrace_pops_t sdt_pops = {
	.dtps_provide =		sdt_provide_probes,
	.dtps_provide_module =	NULL,
	.dtps_enable =		sdt_enable,
	.dtps_disable =		sdt_disable,
	.dtps_suspend =		NULL,
	.dtps_resume =		NULL,
	.dtps_getargdesc =	sdt_getargdesc,
	.dtps_getargval =	NULL,
	.dtps_usermode =	NULL,
	.dtps_destroy =		sdt_destroy,
};

static TAILQ_HEAD(, sdt_provider) sdt_prov_list;

static eventhandler_tag	sdt_kld_load_tag;
static eventhandler_tag	sdt_kld_unload_try_tag;

static void
sdt_create_provider(struct sdt_provider *prov)
{
	struct sdt_provider *curr, *newprov;

	TAILQ_FOREACH(curr, &sdt_prov_list, prov_entry)
		if (strcmp(prov->name, curr->name) == 0) {
			/* The provider has already been defined. */
			curr->sdt_refs++;
			return;
		}

	/*
	 * Make a copy of prov so that we don't lose fields if its module is
	 * unloaded but the provider isn't destroyed. This could happen with
	 * a provider that spans multiple modules.
	 */
	newprov = malloc(sizeof(*newprov), M_SDT, M_WAITOK | M_ZERO);
	newprov->name = strdup(prov->name, M_SDT);
	prov->sdt_refs = newprov->sdt_refs = 1;

	TAILQ_INSERT_TAIL(&sdt_prov_list, newprov, prov_entry);

	(void)dtrace_register(newprov->name, &sdt_attr, DTRACE_PRIV_USER, NULL,
	    &sdt_pops, NULL, (dtrace_provider_id_t *)&newprov->id);
	prov->id = newprov->id;
}

static void
sdt_create_probe(struct sdt_probe *probe)
{
	struct sdt_provider *prov;
	char mod[DTRACE_MODNAMELEN];
	char func[DTRACE_FUNCNAMELEN];
	char name[DTRACE_NAMELEN];
	const char *from;
	char *to;
	size_t len;

	if (probe->version != (int)sizeof(*probe)) {
		printf("ignoring probe %p, version %u expected %u\n",
		    probe, probe->version, (int)sizeof(*probe));
		return;
	}

	TAILQ_FOREACH(prov, &sdt_prov_list, prov_entry)
		if (strcmp(prov->name, probe->prov->name) == 0)
			break;

	KASSERT(prov != NULL, ("probe defined without a provider"));

	/* If no module name was specified, use the module filename. */
	if (*probe->mod == 0) {
		len = strlcpy(mod, probe->sdtp_lf->filename, sizeof(mod));
		if (len > 3 && strcmp(mod + len - 3, ".ko") == 0)
			mod[len - 3] = '\0';
	} else
		strlcpy(mod, probe->mod, sizeof(mod));

	/*
	 * Unfortunately this is necessary because the Solaris DTrace
	 * code mixes consts and non-consts with casts to override
	 * the incompatibilies. On FreeBSD, we use strict warnings
	 * in the C compiler, so we have to respect const vs non-const.
	 */
	strlcpy(func, probe->func, sizeof(func));
	if (func[0] == '\0')
		strcpy(func, "none");

	from = probe->name;
	to = name;
	for (len = 0; len < (sizeof(name) - 1) && *from != '\0';
	    len++, from++, to++) {
		if (from[0] == '_' && from[1] == '_') {
			*to = '-';
			from++;
		} else
			*to = *from;
	}
	*to = '\0';

	if (dtrace_probe_lookup(prov->id, mod, func, name) != DTRACE_IDNONE)
		return;

	(void)dtrace_probe_create(prov->id, mod, func, name, 1, probe);
}

/*
 * Probes are created through the SDT module load/unload hook, so this function
 * has nothing to do. It only exists because the DTrace provider framework
 * requires one of provide_probes and provide_module to be defined.
 */
static void
sdt_provide_probes(void *arg, dtrace_probedesc_t *desc)
{
}

static void
sdt_enable(void *arg __unused, dtrace_id_t id, void *parg)
{
	struct sdt_probe *probe = parg;

	probe->id = id;
	probe->sdtp_lf->nenabled++;
	if (strcmp(probe->prov->name, "lockstat") == 0) {
		lockstat_enabled_count++;
		if (lockstat_enabled_count == 1)
			lockstat_enabled = true;
	}
	sdt_probes_enabled_count++;
	if (sdt_probes_enabled_count == 1)
		sdt_probes_enabled = true;
}

static void
sdt_disable(void *arg __unused, dtrace_id_t id, void *parg)
{
	struct sdt_probe *probe = parg;

	KASSERT(probe->sdtp_lf->nenabled > 0, ("no probes enabled"));

	sdt_probes_enabled_count--;
	if (sdt_probes_enabled_count == 0)
		sdt_probes_enabled = false;
	if (strcmp(probe->prov->name, "lockstat") == 0) {
		lockstat_enabled_count--;
		if (lockstat_enabled_count == 0)
			lockstat_enabled = false;
	}
	probe->id = 0;
	probe->sdtp_lf->nenabled--;
}

static void
sdt_getargdesc(void *arg, dtrace_id_t id, void *parg, dtrace_argdesc_t *desc)
{
	struct sdt_argtype *argtype;
	struct sdt_probe *probe = parg;

	if (desc->dtargd_ndx >= probe->n_args) {
		desc->dtargd_ndx = DTRACE_ARGNONE;
		return;
	}

	TAILQ_FOREACH(argtype, &probe->argtype_list, argtype_entry) {
		if (desc->dtargd_ndx == argtype->ndx) {
			desc->dtargd_mapping = desc->dtargd_ndx;
			if (argtype->type == NULL) {
				desc->dtargd_native[0] = '\0';
				desc->dtargd_xlate[0] = '\0';
				continue;
			}
			strlcpy(desc->dtargd_native, argtype->type,
			    sizeof(desc->dtargd_native));
			if (argtype->xtype != NULL)
				strlcpy(desc->dtargd_xlate, argtype->xtype,
				    sizeof(desc->dtargd_xlate));
		}
	}
}

static void
sdt_destroy(void *arg, dtrace_id_t id, void *parg)
{
}

/*
 * Called from the kernel linker when a module is loaded, before
 * dtrace_module_loaded() is called. This is done so that it's possible to
 * register new providers when modules are loaded. The DTrace framework
 * explicitly disallows calling into the framework from the provide_module
 * provider method, so we cannot do this there.
 */
static void
sdt_kld_load(void *arg __unused, struct linker_file *lf)
{
	struct sdt_provider **prov, **begin, **end;
	struct sdt_probe **probe, **p_begin, **p_end;
	struct sdt_argtype **argtype, **a_begin, **a_end;

	if (linker_file_lookup_set(lf, "sdt_providers_set", &begin, &end,
	    NULL) == 0) {
		for (prov = begin; prov < end; prov++)
			sdt_create_provider(*prov);
	}

	if (linker_file_lookup_set(lf, "sdt_probes_set", &p_begin, &p_end,
	    NULL) == 0) {
		for (probe = p_begin; probe < p_end; probe++) {
			(*probe)->sdtp_lf = lf;
			sdt_create_probe(*probe);
			TAILQ_INIT(&(*probe)->argtype_list);
		}
	}

	if (linker_file_lookup_set(lf, "sdt_argtypes_set", &a_begin, &a_end,
	    NULL) == 0) {
		for (argtype = a_begin; argtype < a_end; argtype++) {
			(*argtype)->probe->n_args++;
			TAILQ_INSERT_TAIL(&(*argtype)->probe->argtype_list,
			    *argtype, argtype_entry);
		}
	}
}

static void
sdt_kld_unload_try(void *arg __unused, struct linker_file *lf, int *error)
{
	struct sdt_provider *prov, **curr, **begin, **end, *tmp;

	if (*error != 0)
		/* We already have an error, so don't do anything. */
		return;
	else if (linker_file_lookup_set(lf, "sdt_providers_set", &begin, &end,
	    NULL))
		/* No DTrace providers are declared in this file. */
		return;

	/*
	 * Go through all the providers declared in this linker file and
	 * unregister any that aren't declared in another loaded file.
	 */
	for (curr = begin; curr < end; curr++) {
		TAILQ_FOREACH_SAFE(prov, &sdt_prov_list, prov_entry, tmp) {
			if (strcmp(prov->name, (*curr)->name) != 0)
				continue;

			if (prov->sdt_refs == 1) {
				if (dtrace_unregister(prov->id) != 0) {
					*error = 1;
					return;
				}
				TAILQ_REMOVE(&sdt_prov_list, prov, prov_entry);
				free(prov->name, M_SDT);
				free(prov, M_SDT);
			} else
				prov->sdt_refs--;
			break;
		}
	}
}

static int
sdt_linker_file_cb(linker_file_t lf, void *arg __unused)
{

	sdt_kld_load(NULL, lf);

	return (0);
}

static void
sdt_load()
{

	TAILQ_INIT(&sdt_prov_list);

	sdt_probe_func = dtrace_probe;

	sdt_kld_load_tag = EVENTHANDLER_REGISTER(kld_load, sdt_kld_load, NULL,
	    EVENTHANDLER_PRI_ANY);
	sdt_kld_unload_try_tag = EVENTHANDLER_REGISTER(kld_unload_try,
	    sdt_kld_unload_try, NULL, EVENTHANDLER_PRI_ANY);

	/* Pick up probes from the kernel and already-loaded linker files. */
	linker_file_foreach(sdt_linker_file_cb, NULL);
}

static int
sdt_unload()
{
	struct sdt_provider *prov, *tmp;
	int ret;

	EVENTHANDLER_DEREGISTER(kld_load, sdt_kld_load_tag);
	EVENTHANDLER_DEREGISTER(kld_unload_try, sdt_kld_unload_try_tag);

	sdt_probe_func = sdt_probe_stub;

	TAILQ_FOREACH_SAFE(prov, &sdt_prov_list, prov_entry, tmp) {
		ret = dtrace_unregister(prov->id);
		if (ret != 0)
			return (ret);
		TAILQ_REMOVE(&sdt_prov_list, prov, prov_entry);
		free(prov->name, M_SDT);
		free(prov, M_SDT);
	}

	return (0);
}

static int
sdt_modevent(module_t mod __unused, int type, void *data __unused)
{

	switch (type) {
	case MOD_LOAD:
	case MOD_UNLOAD:
	case MOD_SHUTDOWN:
		return (0);
	default:
		return (EOPNOTSUPP);
	}
}

SYSINIT(sdt_load, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY, sdt_load, NULL);
SYSUNINIT(sdt_unload, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY, sdt_unload, NULL);

DEV_MODULE(sdt, sdt_modevent, NULL);
MODULE_VERSION(sdt, 1);
MODULE_DEPEND(sdt, dtrace, 1, 1, 1);
