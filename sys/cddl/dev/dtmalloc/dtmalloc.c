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

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/ctype.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <sys/dtrace.h>
#include <sys/dtrace_bsd.h>

extern bool dtrace_malloc_enabled;
static uint32_t dtrace_malloc_enabled_count;

static d_open_t	dtmalloc_open;
static int	dtmalloc_unload(void);
static void	dtmalloc_getargdesc(void *, dtrace_id_t, void *, dtrace_argdesc_t *);
static void	dtmalloc_provide(void *, dtrace_probedesc_t *);
static void	dtmalloc_destroy(void *, dtrace_id_t, void *);
static void	dtmalloc_enable(void *, dtrace_id_t, void *);
static void	dtmalloc_disable(void *, dtrace_id_t, void *);
static void	dtmalloc_load(void *);

static struct cdevsw dtmalloc_cdevsw = {
	.d_version	= D_VERSION,
	.d_open		= dtmalloc_open,
	.d_name		= "dtmalloc",
};

static dtrace_pattr_t dtmalloc_attr = {
{ DTRACE_STABILITY_STABLE, DTRACE_STABILITY_STABLE, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_STABLE, DTRACE_STABILITY_STABLE, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_STABLE, DTRACE_STABILITY_STABLE, DTRACE_CLASS_COMMON },
};

static dtrace_pops_t dtmalloc_pops = {
	.dtps_provide =		dtmalloc_provide,
	.dtps_provide_module =	NULL,
	.dtps_enable =		dtmalloc_enable,
	.dtps_disable =		dtmalloc_disable,
	.dtps_suspend =		NULL,
	.dtps_resume =		NULL,
	.dtps_getargdesc =	dtmalloc_getargdesc,
	.dtps_getargval =	NULL,
	.dtps_usermode =	NULL,
	.dtps_destroy =		dtmalloc_destroy
};

static struct cdev		*dtmalloc_cdev;
static dtrace_provider_id_t	dtmalloc_id;

static void
dtmalloc_getargdesc(void *arg, dtrace_id_t id, void *parg, dtrace_argdesc_t *desc)
{
	const char *p = NULL;

	switch (desc->dtargd_ndx) {
	case 0:
		p = "struct malloc_type *";
		break;
	case 1:
		p = "struct malloc_type_internal *";
		break;
	case 2:
		p = "struct malloc_type_stats *";
		break;
	case 3:
		p = "unsigned long";
		break;
	case 4:
		p = "int";
		break;
	default:
		desc->dtargd_ndx = DTRACE_ARGNONE;
		break;
	}

	if (p != NULL)
		strlcpy(desc->dtargd_native, p, sizeof(desc->dtargd_native));

	return;
}

static void
dtmalloc_type_cb(struct malloc_type *mtp, void *arg __unused)
{
	char name[DTRACE_FUNCNAMELEN];
	struct malloc_type_internal *mtip = mtp->ks_handle;
	int i;

	/*
	 * malloc_type descriptions are allowed to contain whitespace, but
	 * DTrace probe identifiers are not, so replace the whitespace with
	 * underscores.
	 */
	strlcpy(name, mtp->ks_shortdesc, sizeof(name));
	for (i = 0; name[i] != 0; i++)
		if (isspace(name[i]))
			name[i] = '_';

	if (dtrace_probe_lookup(dtmalloc_id, NULL, name, "malloc") != 0)
		return;

	(void) dtrace_probe_create(dtmalloc_id, NULL, name, "malloc", 0,
	    &mtip->mti_probes[DTMALLOC_PROBE_MALLOC]);
	(void) dtrace_probe_create(dtmalloc_id, NULL, name, "free", 0,
	    &mtip->mti_probes[DTMALLOC_PROBE_FREE]);
}

static void
dtmalloc_provide(void *arg, dtrace_probedesc_t *desc)
{
	if (desc != NULL)
		return;

	malloc_type_list(dtmalloc_type_cb, desc);
}

static void
dtmalloc_destroy(void *arg, dtrace_id_t id, void *parg)
{
}

static void
dtmalloc_enable(void *arg, dtrace_id_t id, void *parg)
{
	uint32_t *p = parg;
	*p = id;
	dtrace_malloc_enabled_count++;
	if (dtrace_malloc_enabled_count == 1)
		dtrace_malloc_enabled = true;
}

static void
dtmalloc_disable(void *arg, dtrace_id_t id, void *parg)
{
	uint32_t *p = parg;
	*p = 0;
	dtrace_malloc_enabled_count--;
	if (dtrace_malloc_enabled_count == 0)
		dtrace_malloc_enabled = false;
}

static void
dtmalloc_load(void *dummy)
{
	/* Create the /dev/dtrace/dtmalloc entry. */
	dtmalloc_cdev = make_dev(&dtmalloc_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
	    "dtrace/dtmalloc");

	if (dtrace_register("dtmalloc", &dtmalloc_attr, DTRACE_PRIV_USER,
	    NULL, &dtmalloc_pops, NULL, &dtmalloc_id) != 0)
		return;

	dtrace_malloc_probe = dtrace_probe;
}


static int
dtmalloc_unload()
{
	int error = 0;

	dtrace_malloc_probe = NULL;

	if ((error = dtrace_unregister(dtmalloc_id)) != 0)
		return (error);

	destroy_dev(dtmalloc_cdev);

	return (error);
}

static int
dtmalloc_modevent(module_t mod __unused, int type, void *data __unused)
{
	int error = 0;

	switch (type) {
	case MOD_LOAD:
		break;

	case MOD_UNLOAD:
		break;

	case MOD_SHUTDOWN:
		break;

	default:
		error = EOPNOTSUPP;
		break;

	}

	return (error);
}

static int
dtmalloc_open(struct cdev *dev __unused, int oflags __unused, int devtype __unused, struct thread *td __unused)
{
	return (0);
}

SYSINIT(dtmalloc_load, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY, dtmalloc_load, NULL);
SYSUNINIT(dtmalloc_unload, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY, dtmalloc_unload, NULL);

DEV_MODULE(dtmalloc, dtmalloc_modevent, NULL);
MODULE_VERSION(dtmalloc, 1);
MODULE_DEPEND(dtmalloc, dtrace, 1, 1, 1);
MODULE_DEPEND(dtmalloc, opensolaris, 1, 1, 1);
