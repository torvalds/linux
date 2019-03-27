/*
 * This file is freeware. You are free to use it and add your own
 * license.
 *
 * $FreeBSD$
 *
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <sys/dtrace.h>

static d_open_t	prototype_open;
static int	prototype_unload(void);
static void	prototype_getargdesc(void *, dtrace_id_t, void *, dtrace_argdesc_t *);
static void	prototype_provide(void *, dtrace_probedesc_t *);
static void	prototype_destroy(void *, dtrace_id_t, void *);
static void	prototype_enable(void *, dtrace_id_t, void *);
static void	prototype_disable(void *, dtrace_id_t, void *);
static void	prototype_load(void *);

static struct cdevsw prototype_cdevsw = {
	.d_version	= D_VERSION,
	.d_open		= prototype_open,
	.d_name		= "prototype",
};

static dtrace_pattr_t prototype_attr = {
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_UNKNOWN },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
{ DTRACE_STABILITY_EVOLVING, DTRACE_STABILITY_EVOLVING, DTRACE_CLASS_COMMON },
{ DTRACE_STABILITY_PRIVATE, DTRACE_STABILITY_PRIVATE, DTRACE_CLASS_ISA },
};

static dtrace_pops_t prototype_pops = {
	.dtps_provide =		prototype_provide,
	.dtps_provide_module =	NULL,
	.dtps_enable =		prototype_enable,
	.dtps_disable =		prototype_disable,
	.dtps_suspend =		NULL,
	.dtps_resume =		NULL,
	.dtps_getargdesc =	prototype_getargdesc,
	.dtps_getargval =	NULL,
	.dtps_usermode =	NULL,
	.dtps_destroy =		prototype_destroy
};

static struct cdev		*prototype_cdev;
static dtrace_provider_id_t	prototype_id;

static void
prototype_getargdesc(void *arg, dtrace_id_t id, void *parg, dtrace_argdesc_t *desc)
{
}

static void
prototype_provide(void *arg, dtrace_probedesc_t *desc)
{
}

static void
prototype_destroy(void *arg, dtrace_id_t id, void *parg)
{
}

static void
prototype_enable(void *arg, dtrace_id_t id, void *parg)
{
}

static void
prototype_disable(void *arg, dtrace_id_t id, void *parg)
{
}

static void
prototype_load(void *dummy)
{
	/* Create the /dev/dtrace/prototype entry. */
	prototype_cdev = make_dev(&prototype_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600,
	    "dtrace/prototype");

	if (dtrace_register("prototype", &prototype_attr, DTRACE_PRIV_USER,
	    NULL, &prototype_pops, NULL, &prototype_id) != 0)
		return;
}


static int
prototype_unload()
{
	int error = 0;

	if ((error = dtrace_unregister(prototype_id)) != 0)
		return (error);

	destroy_dev(prototype_cdev);

	return (error);
}

static int
prototype_modevent(module_t mod __unused, int type, void *data __unused)
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
prototype_open(struct cdev *dev __unused, int oflags __unused, int devtype __unused, struct thread *td __unused)
{
	return (0);
}

SYSINIT(prototype_load, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY, prototype_load, NULL);
SYSUNINIT(prototype_unload, SI_SUB_DTRACE_PROVIDER, SI_ORDER_ANY, prototype_unload, NULL);

DEV_MODULE(prototype, prototype_modevent, NULL);
MODULE_VERSION(prototype, 1);
MODULE_DEPEND(prototype, dtrace, 1, 1, 1);
MODULE_DEPEND(prototype, opensolaris, 1, 1, 1);
