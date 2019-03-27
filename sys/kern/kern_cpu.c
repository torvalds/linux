/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004-2007 Nate Lawson (SDG)
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
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/proc.h>
#include <sys/queue.h>
#include <sys/sbuf.h>
#include <sys/sched.h>
#include <sys/smp.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/sx.h>
#include <sys/timetc.h>
#include <sys/taskqueue.h>

#include "cpufreq_if.h"

/*
 * Common CPU frequency glue code.  Drivers for specific hardware can
 * attach this interface to allow users to get/set the CPU frequency.
 */

/*
 * Number of levels we can handle.  Levels are synthesized from settings
 * so for M settings and N drivers, there may be M*N levels.
 */
#define CF_MAX_LEVELS	256

struct cf_saved_freq {
	struct cf_level			level;
	int				priority;
	SLIST_ENTRY(cf_saved_freq)	link;
};

struct cpufreq_softc {
	struct sx			lock;
	struct cf_level			curr_level;
	int				curr_priority;
	SLIST_HEAD(, cf_saved_freq)	saved_freq;
	struct cf_level_lst		all_levels;
	int				all_count;
	int				max_mhz;
	device_t			dev;
	struct sysctl_ctx_list		sysctl_ctx;
	struct task			startup_task;
	struct cf_level			*levels_buf;
};

struct cf_setting_array {
	struct cf_setting		sets[MAX_SETTINGS];
	int				count;
	TAILQ_ENTRY(cf_setting_array)	link;
};

TAILQ_HEAD(cf_setting_lst, cf_setting_array);

#define CF_MTX_INIT(x)		sx_init((x), "cpufreq lock")
#define CF_MTX_LOCK(x)		sx_xlock((x))
#define CF_MTX_UNLOCK(x)	sx_xunlock((x))
#define CF_MTX_ASSERT(x)	sx_assert((x), SX_XLOCKED)

#define CF_DEBUG(msg...)	do {		\
	if (cf_verbose)				\
		printf("cpufreq: " msg);	\
	} while (0)

static int	cpufreq_attach(device_t dev);
static void	cpufreq_startup_task(void *ctx, int pending);
static int	cpufreq_detach(device_t dev);
static int	cf_set_method(device_t dev, const struct cf_level *level,
		    int priority);
static int	cf_get_method(device_t dev, struct cf_level *level);
static int	cf_levels_method(device_t dev, struct cf_level *levels,
		    int *count);
static int	cpufreq_insert_abs(struct cpufreq_softc *sc,
		    struct cf_setting *sets, int count);
static int	cpufreq_expand_set(struct cpufreq_softc *sc,
		    struct cf_setting_array *set_arr);
static struct cf_level *cpufreq_dup_set(struct cpufreq_softc *sc,
		    struct cf_level *dup, struct cf_setting *set);
static int	cpufreq_curr_sysctl(SYSCTL_HANDLER_ARGS);
static int	cpufreq_levels_sysctl(SYSCTL_HANDLER_ARGS);
static int	cpufreq_settings_sysctl(SYSCTL_HANDLER_ARGS);

static device_method_t cpufreq_methods[] = {
	DEVMETHOD(device_probe,		bus_generic_probe),
	DEVMETHOD(device_attach,	cpufreq_attach),
	DEVMETHOD(device_detach,	cpufreq_detach),

        DEVMETHOD(cpufreq_set,		cf_set_method),
        DEVMETHOD(cpufreq_get,		cf_get_method),
        DEVMETHOD(cpufreq_levels,	cf_levels_method),
	{0, 0}
};
static driver_t cpufreq_driver = {
	"cpufreq", cpufreq_methods, sizeof(struct cpufreq_softc)
};
static devclass_t cpufreq_dc;
DRIVER_MODULE(cpufreq, cpu, cpufreq_driver, cpufreq_dc, 0, 0);

static int		cf_lowest_freq;
static int		cf_verbose;
static SYSCTL_NODE(_debug, OID_AUTO, cpufreq, CTLFLAG_RD, NULL,
    "cpufreq debugging");
SYSCTL_INT(_debug_cpufreq, OID_AUTO, lowest, CTLFLAG_RWTUN, &cf_lowest_freq, 1,
    "Don't provide levels below this frequency.");
SYSCTL_INT(_debug_cpufreq, OID_AUTO, verbose, CTLFLAG_RWTUN, &cf_verbose, 1,
    "Print verbose debugging messages");

static int
cpufreq_attach(device_t dev)
{
	struct cpufreq_softc *sc;
	struct pcpu *pc;
	device_t parent;
	uint64_t rate;
	int numdevs;

	CF_DEBUG("initializing %s\n", device_get_nameunit(dev));
	sc = device_get_softc(dev);
	parent = device_get_parent(dev);
	sc->dev = dev;
	sysctl_ctx_init(&sc->sysctl_ctx);
	TAILQ_INIT(&sc->all_levels);
	CF_MTX_INIT(&sc->lock);
	sc->curr_level.total_set.freq = CPUFREQ_VAL_UNKNOWN;
	SLIST_INIT(&sc->saved_freq);
	/* Try to get nominal CPU freq to use it as maximum later if needed */
	sc->max_mhz = cpu_get_nominal_mhz(dev);
	/* If that fails, try to measure the current rate */
	if (sc->max_mhz <= 0) {
		pc = cpu_get_pcpu(dev);
		if (cpu_est_clockrate(pc->pc_cpuid, &rate) == 0)
			sc->max_mhz = rate / 1000000;
		else
			sc->max_mhz = CPUFREQ_VAL_UNKNOWN;
	}

	/*
	 * Only initialize one set of sysctls for all CPUs.  In the future,
	 * if multiple CPUs can have different settings, we can move these
	 * sysctls to be under every CPU instead of just the first one.
	 */
	numdevs = devclass_get_count(cpufreq_dc);
	if (numdevs > 1)
		return (0);

	CF_DEBUG("initializing one-time data for %s\n",
	    device_get_nameunit(dev));
	sc->levels_buf = malloc(CF_MAX_LEVELS * sizeof(*sc->levels_buf),
	    M_DEVBUF, M_WAITOK);
	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(parent)),
	    OID_AUTO, "freq", CTLTYPE_INT | CTLFLAG_RW, sc, 0,
	    cpufreq_curr_sysctl, "I", "Current CPU frequency");
	SYSCTL_ADD_PROC(&sc->sysctl_ctx,
	    SYSCTL_CHILDREN(device_get_sysctl_tree(parent)),
	    OID_AUTO, "freq_levels", CTLTYPE_STRING | CTLFLAG_RD, sc, 0,
	    cpufreq_levels_sysctl, "A", "CPU frequency levels");

	/*
	 * Queue a one-shot broadcast that levels have changed.
	 * It will run once the system has completed booting.
	 */
	TASK_INIT(&sc->startup_task, 0, cpufreq_startup_task, dev);
	taskqueue_enqueue(taskqueue_thread, &sc->startup_task);

	return (0);
}

/* Handle any work to be done for all drivers that attached during boot. */
static void 
cpufreq_startup_task(void *ctx, int pending)
{

	cpufreq_settings_changed((device_t)ctx);
}

static int
cpufreq_detach(device_t dev)
{
	struct cpufreq_softc *sc;
	struct cf_saved_freq *saved_freq;
	int numdevs;

	CF_DEBUG("shutdown %s\n", device_get_nameunit(dev));
	sc = device_get_softc(dev);
	sysctl_ctx_free(&sc->sysctl_ctx);

	while ((saved_freq = SLIST_FIRST(&sc->saved_freq)) != NULL) {
		SLIST_REMOVE_HEAD(&sc->saved_freq, link);
		free(saved_freq, M_TEMP);
	}

	/* Only clean up these resources when the last device is detaching. */
	numdevs = devclass_get_count(cpufreq_dc);
	if (numdevs == 1) {
		CF_DEBUG("final shutdown for %s\n", device_get_nameunit(dev));
		free(sc->levels_buf, M_DEVBUF);
	}

	return (0);
}

static int
cf_set_method(device_t dev, const struct cf_level *level, int priority)
{
	struct cpufreq_softc *sc;
	const struct cf_setting *set;
	struct cf_saved_freq *saved_freq, *curr_freq;
	struct pcpu *pc;
	int error, i;
	u_char pri;

	sc = device_get_softc(dev);
	error = 0;
	set = NULL;
	saved_freq = NULL;

	/* We are going to change levels so notify the pre-change handler. */
	EVENTHANDLER_INVOKE(cpufreq_pre_change, level, &error);
	if (error != 0) {
		EVENTHANDLER_INVOKE(cpufreq_post_change, level, error);
		return (error);
	}

	CF_MTX_LOCK(&sc->lock);

#ifdef SMP
#ifdef EARLY_AP_STARTUP
	MPASS(mp_ncpus == 1 || smp_started);
#else
	/*
	 * If still booting and secondary CPUs not started yet, don't allow
	 * changing the frequency until they're online.  This is because we
	 * can't switch to them using sched_bind() and thus we'd only be
	 * switching the main CPU.  XXXTODO: Need to think more about how to
	 * handle having different CPUs at different frequencies.  
	 */
	if (mp_ncpus > 1 && !smp_started) {
		device_printf(dev, "rejecting change, SMP not started yet\n");
		error = ENXIO;
		goto out;
	}
#endif
#endif /* SMP */

	/*
	 * If the requested level has a lower priority, don't allow
	 * the new level right now.
	 */
	if (priority < sc->curr_priority) {
		CF_DEBUG("ignoring, curr prio %d less than %d\n", priority,
		    sc->curr_priority);
		error = EPERM;
		goto out;
	}

	/*
	 * If the caller didn't specify a level and one is saved, prepare to
	 * restore the saved level.  If none has been saved, return an error.
	 */
	if (level == NULL) {
		saved_freq = SLIST_FIRST(&sc->saved_freq);
		if (saved_freq == NULL) {
			CF_DEBUG("NULL level, no saved level\n");
			error = ENXIO;
			goto out;
		}
		level = &saved_freq->level;
		priority = saved_freq->priority;
		CF_DEBUG("restoring saved level, freq %d prio %d\n",
		    level->total_set.freq, priority);
	}

	/* Reject levels that are below our specified threshold. */
	if (level->total_set.freq < cf_lowest_freq) {
		CF_DEBUG("rejecting freq %d, less than %d limit\n",
		    level->total_set.freq, cf_lowest_freq);
		error = EINVAL;
		goto out;
	}

	/* If already at this level, just return. */
	if (sc->curr_level.total_set.freq == level->total_set.freq) {
		CF_DEBUG("skipping freq %d, same as current level %d\n",
		    level->total_set.freq, sc->curr_level.total_set.freq);
		goto skip;
	}

	/* First, set the absolute frequency via its driver. */
	set = &level->abs_set;
	if (set->dev) {
		if (!device_is_attached(set->dev)) {
			error = ENXIO;
			goto out;
		}

		/* Bind to the target CPU before switching. */
		pc = cpu_get_pcpu(set->dev);
		thread_lock(curthread);
		pri = curthread->td_priority;
		sched_prio(curthread, PRI_MIN);
		sched_bind(curthread, pc->pc_cpuid);
		thread_unlock(curthread);
		CF_DEBUG("setting abs freq %d on %s (cpu %d)\n", set->freq,
		    device_get_nameunit(set->dev), PCPU_GET(cpuid));
		error = CPUFREQ_DRV_SET(set->dev, set);
		thread_lock(curthread);
		sched_unbind(curthread);
		sched_prio(curthread, pri);
		thread_unlock(curthread);
		if (error) {
			goto out;
		}
	}

	/* Next, set any/all relative frequencies via their drivers. */
	for (i = 0; i < level->rel_count; i++) {
		set = &level->rel_set[i];
		if (!device_is_attached(set->dev)) {
			error = ENXIO;
			goto out;
		}

		/* Bind to the target CPU before switching. */
		pc = cpu_get_pcpu(set->dev);
		thread_lock(curthread);
		pri = curthread->td_priority;
		sched_prio(curthread, PRI_MIN);
		sched_bind(curthread, pc->pc_cpuid);
		thread_unlock(curthread);
		CF_DEBUG("setting rel freq %d on %s (cpu %d)\n", set->freq,
		    device_get_nameunit(set->dev), PCPU_GET(cpuid));
		error = CPUFREQ_DRV_SET(set->dev, set);
		thread_lock(curthread);
		sched_unbind(curthread);
		sched_prio(curthread, pri);
		thread_unlock(curthread);
		if (error) {
			/* XXX Back out any successful setting? */
			goto out;
		}
	}

skip:
	/*
	 * Before recording the current level, check if we're going to a
	 * higher priority.  If so, save the previous level and priority.
	 */
	if (sc->curr_level.total_set.freq != CPUFREQ_VAL_UNKNOWN &&
	    priority > sc->curr_priority) {
		CF_DEBUG("saving level, freq %d prio %d\n",
		    sc->curr_level.total_set.freq, sc->curr_priority);
		curr_freq = malloc(sizeof(*curr_freq), M_TEMP, M_NOWAIT);
		if (curr_freq == NULL) {
			error = ENOMEM;
			goto out;
		}
		curr_freq->level = sc->curr_level;
		curr_freq->priority = sc->curr_priority;
		SLIST_INSERT_HEAD(&sc->saved_freq, curr_freq, link);
	}
	sc->curr_level = *level;
	sc->curr_priority = priority;

	/* If we were restoring a saved state, reset it to "unused". */
	if (saved_freq != NULL) {
		CF_DEBUG("resetting saved level\n");
		sc->curr_level.total_set.freq = CPUFREQ_VAL_UNKNOWN;
		SLIST_REMOVE_HEAD(&sc->saved_freq, link);
		free(saved_freq, M_TEMP);
	}

out:
	CF_MTX_UNLOCK(&sc->lock);

	/*
	 * We changed levels (or attempted to) so notify the post-change
	 * handler of new frequency or error.
	 */
	EVENTHANDLER_INVOKE(cpufreq_post_change, level, error);
	if (error && set)
		device_printf(set->dev, "set freq failed, err %d\n", error);

	return (error);
}

static int
cf_get_method(device_t dev, struct cf_level *level)
{
	struct cpufreq_softc *sc;
	struct cf_level *levels;
	struct cf_setting *curr_set, set;
	struct pcpu *pc;
	device_t *devs;
	int bdiff, count, diff, error, i, n, numdevs;
	uint64_t rate;

	sc = device_get_softc(dev);
	error = 0;
	levels = NULL;

	/* If we already know the current frequency, we're done. */
	CF_MTX_LOCK(&sc->lock);
	curr_set = &sc->curr_level.total_set;
	if (curr_set->freq != CPUFREQ_VAL_UNKNOWN) {
		CF_DEBUG("get returning known freq %d\n", curr_set->freq);
		goto out;
	}
	CF_MTX_UNLOCK(&sc->lock);

	/*
	 * We need to figure out the current level.  Loop through every
	 * driver, getting the current setting.  Then, attempt to get a best
	 * match of settings against each level.
	 */
	count = CF_MAX_LEVELS;
	levels = malloc(count * sizeof(*levels), M_TEMP, M_NOWAIT);
	if (levels == NULL)
		return (ENOMEM);
	error = CPUFREQ_LEVELS(sc->dev, levels, &count);
	if (error) {
		if (error == E2BIG)
			printf("cpufreq: need to increase CF_MAX_LEVELS\n");
		free(levels, M_TEMP);
		return (error);
	}
	error = device_get_children(device_get_parent(dev), &devs, &numdevs);
	if (error) {
		free(levels, M_TEMP);
		return (error);
	}

	/*
	 * Reacquire the lock and search for the given level.
	 *
	 * XXX Note: this is not quite right since we really need to go
	 * through each level and compare both absolute and relative
	 * settings for each driver in the system before making a match.
	 * The estimation code below catches this case though.
	 */
	CF_MTX_LOCK(&sc->lock);
	for (n = 0; n < numdevs && curr_set->freq == CPUFREQ_VAL_UNKNOWN; n++) {
		if (!device_is_attached(devs[n]))
			continue;
		if (CPUFREQ_DRV_GET(devs[n], &set) != 0)
			continue;
		for (i = 0; i < count; i++) {
			if (set.freq == levels[i].total_set.freq) {
				sc->curr_level = levels[i];
				break;
			}
		}
	}
	free(devs, M_TEMP);
	if (curr_set->freq != CPUFREQ_VAL_UNKNOWN) {
		CF_DEBUG("get matched freq %d from drivers\n", curr_set->freq);
		goto out;
	}

	/*
	 * We couldn't find an exact match, so attempt to estimate and then
	 * match against a level.
	 */
	pc = cpu_get_pcpu(dev);
	if (pc == NULL) {
		error = ENXIO;
		goto out;
	}
	cpu_est_clockrate(pc->pc_cpuid, &rate);
	rate /= 1000000;
	bdiff = 1 << 30;
	for (i = 0; i < count; i++) {
		diff = abs(levels[i].total_set.freq - rate);
		if (diff < bdiff) {
			bdiff = diff;
			sc->curr_level = levels[i];
		}
	}
	CF_DEBUG("get estimated freq %d\n", curr_set->freq);

out:
	if (error == 0)
		*level = sc->curr_level;

	CF_MTX_UNLOCK(&sc->lock);
	if (levels)
		free(levels, M_TEMP);
	return (error);
}

static int
cf_levels_method(device_t dev, struct cf_level *levels, int *count)
{
	struct cf_setting_array *set_arr;
	struct cf_setting_lst rel_sets;
	struct cpufreq_softc *sc;
	struct cf_level *lev;
	struct cf_setting *sets;
	struct pcpu *pc;
	device_t *devs;
	int error, i, numdevs, set_count, type;
	uint64_t rate;

	if (levels == NULL || count == NULL)
		return (EINVAL);

	TAILQ_INIT(&rel_sets);
	sc = device_get_softc(dev);
	error = device_get_children(device_get_parent(dev), &devs, &numdevs);
	if (error)
		return (error);
	sets = malloc(MAX_SETTINGS * sizeof(*sets), M_TEMP, M_NOWAIT);
	if (sets == NULL) {
		free(devs, M_TEMP);
		return (ENOMEM);
	}

	/* Get settings from all cpufreq drivers. */
	CF_MTX_LOCK(&sc->lock);
	for (i = 0; i < numdevs; i++) {
		/* Skip devices that aren't ready. */
		if (!device_is_attached(devs[i]))
			continue;

		/*
		 * Get settings, skipping drivers that offer no settings or
		 * provide settings for informational purposes only.
		 */
		error = CPUFREQ_DRV_TYPE(devs[i], &type);
		if (error || (type & CPUFREQ_FLAG_INFO_ONLY)) {
			if (error == 0) {
				CF_DEBUG("skipping info-only driver %s\n",
				    device_get_nameunit(devs[i]));
			}
			continue;
		}
		set_count = MAX_SETTINGS;
		error = CPUFREQ_DRV_SETTINGS(devs[i], sets, &set_count);
		if (error || set_count == 0)
			continue;

		/* Add the settings to our absolute/relative lists. */
		switch (type & CPUFREQ_TYPE_MASK) {
		case CPUFREQ_TYPE_ABSOLUTE:
			error = cpufreq_insert_abs(sc, sets, set_count);
			break;
		case CPUFREQ_TYPE_RELATIVE:
			CF_DEBUG("adding %d relative settings\n", set_count);
			set_arr = malloc(sizeof(*set_arr), M_TEMP, M_NOWAIT);
			if (set_arr == NULL) {
				error = ENOMEM;
				goto out;
			}
			bcopy(sets, set_arr->sets, set_count * sizeof(*sets));
			set_arr->count = set_count;
			TAILQ_INSERT_TAIL(&rel_sets, set_arr, link);
			break;
		default:
			error = EINVAL;
		}
		if (error)
			goto out;
	}

	/*
	 * If there are no absolute levels, create a fake one at 100%.  We
	 * then cache the clockrate for later use as our base frequency.
	 */
	if (TAILQ_EMPTY(&sc->all_levels)) {
		if (sc->max_mhz == CPUFREQ_VAL_UNKNOWN) {
			sc->max_mhz = cpu_get_nominal_mhz(dev);
			/*
			 * If the CPU can't report a rate for 100%, hope
			 * the CPU is running at its nominal rate right now,
			 * and use that instead.
			 */
			if (sc->max_mhz <= 0) {
				pc = cpu_get_pcpu(dev);
				cpu_est_clockrate(pc->pc_cpuid, &rate);
				sc->max_mhz = rate / 1000000;
			}
		}
		memset(&sets[0], CPUFREQ_VAL_UNKNOWN, sizeof(*sets));
		sets[0].freq = sc->max_mhz;
		sets[0].dev = NULL;
		error = cpufreq_insert_abs(sc, sets, 1);
		if (error)
			goto out;
	}

	/* Create a combined list of absolute + relative levels. */
	TAILQ_FOREACH(set_arr, &rel_sets, link)
		cpufreq_expand_set(sc, set_arr);

	/* If the caller doesn't have enough space, return the actual count. */
	if (sc->all_count > *count) {
		*count = sc->all_count;
		error = E2BIG;
		goto out;
	}

	/* Finally, output the list of levels. */
	i = 0;
	TAILQ_FOREACH(lev, &sc->all_levels, link) {

		/* Skip levels that have a frequency that is too low. */
		if (lev->total_set.freq < cf_lowest_freq) {
			sc->all_count--;
			continue;
		}

		levels[i] = *lev;
		i++;
	}
	*count = sc->all_count;
	error = 0;

out:
	/* Clear all levels since we regenerate them each time. */
	while ((lev = TAILQ_FIRST(&sc->all_levels)) != NULL) {
		TAILQ_REMOVE(&sc->all_levels, lev, link);
		free(lev, M_TEMP);
	}
	sc->all_count = 0;

	CF_MTX_UNLOCK(&sc->lock);
	while ((set_arr = TAILQ_FIRST(&rel_sets)) != NULL) {
		TAILQ_REMOVE(&rel_sets, set_arr, link);
		free(set_arr, M_TEMP);
	}
	free(devs, M_TEMP);
	free(sets, M_TEMP);
	return (error);
}

/*
 * Create levels for an array of absolute settings and insert them in
 * sorted order in the specified list.
 */
static int
cpufreq_insert_abs(struct cpufreq_softc *sc, struct cf_setting *sets,
    int count)
{
	struct cf_level_lst *list;
	struct cf_level *level, *search;
	int i, inserted;

	CF_MTX_ASSERT(&sc->lock);

	list = &sc->all_levels;
	for (i = 0; i < count; i++) {
		level = malloc(sizeof(*level), M_TEMP, M_NOWAIT | M_ZERO);
		if (level == NULL)
			return (ENOMEM);
		level->abs_set = sets[i];
		level->total_set = sets[i];
		level->total_set.dev = NULL;
		sc->all_count++;
		inserted = 0;

		if (TAILQ_EMPTY(list)) {
			CF_DEBUG("adding abs setting %d at head\n",
			    sets[i].freq);
			TAILQ_INSERT_HEAD(list, level, link);
			continue;
		}

		TAILQ_FOREACH_REVERSE(search, list, cf_level_lst, link)
			if (sets[i].freq <= search->total_set.freq) {
				CF_DEBUG("adding abs setting %d after %d\n",
				    sets[i].freq, search->total_set.freq);
				TAILQ_INSERT_AFTER(list, search, level, link);
				inserted = 1;
				break;
			}

		if (inserted == 0) {
			TAILQ_FOREACH(search, list, link)
				if (sets[i].freq >= search->total_set.freq) {
					CF_DEBUG("adding abs setting %d before %d\n",
					    sets[i].freq, search->total_set.freq);
					TAILQ_INSERT_BEFORE(search, level, link);
					break;
				}
		}
	}

	return (0);
}

/*
 * Expand a group of relative settings, creating derived levels from them.
 */
static int
cpufreq_expand_set(struct cpufreq_softc *sc, struct cf_setting_array *set_arr)
{
	struct cf_level *fill, *search;
	struct cf_setting *set;
	int i;

	CF_MTX_ASSERT(&sc->lock);

	/*
	 * Walk the set of all existing levels in reverse.  This is so we
	 * create derived states from the lowest absolute settings first
	 * and discard duplicates created from higher absolute settings.
	 * For instance, a level of 50 Mhz derived from 100 Mhz + 50% is
	 * preferable to 200 Mhz + 25% because absolute settings are more
	 * efficient since they often change the voltage as well.
	 */
	TAILQ_FOREACH_REVERSE(search, &sc->all_levels, cf_level_lst, link) {
		/* Add each setting to the level, duplicating if necessary. */
		for (i = 0; i < set_arr->count; i++) {
			set = &set_arr->sets[i];

			/*
			 * If this setting is less than 100%, split the level
			 * into two and add this setting to the new level.
			 */
			fill = search;
			if (set->freq < 10000) {
				fill = cpufreq_dup_set(sc, search, set);

				/*
				 * The new level was a duplicate of an existing
				 * level or its absolute setting is too high
				 * so we freed it.  For example, we discard a
				 * derived level of 1000 MHz/25% if a level
				 * of 500 MHz/100% already exists.
				 */
				if (fill == NULL)
					break;
			}

			/* Add this setting to the existing or new level. */
			KASSERT(fill->rel_count < MAX_SETTINGS,
			    ("cpufreq: too many relative drivers (%d)",
			    MAX_SETTINGS));
			fill->rel_set[fill->rel_count] = *set;
			fill->rel_count++;
			CF_DEBUG(
			"expand set added rel setting %d%% to %d level\n",
			    set->freq / 100, fill->total_set.freq);
		}
	}

	return (0);
}

static struct cf_level *
cpufreq_dup_set(struct cpufreq_softc *sc, struct cf_level *dup,
    struct cf_setting *set)
{
	struct cf_level_lst *list;
	struct cf_level *fill, *itr;
	struct cf_setting *fill_set, *itr_set;
	int i;

	CF_MTX_ASSERT(&sc->lock);

	/*
	 * Create a new level, copy it from the old one, and update the
	 * total frequency and power by the percentage specified in the
	 * relative setting.
	 */
	fill = malloc(sizeof(*fill), M_TEMP, M_NOWAIT);
	if (fill == NULL)
		return (NULL);
	*fill = *dup;
	fill_set = &fill->total_set;
	fill_set->freq =
	    ((uint64_t)fill_set->freq * set->freq) / 10000;
	if (fill_set->power != CPUFREQ_VAL_UNKNOWN) {
		fill_set->power = ((uint64_t)fill_set->power * set->freq)
		    / 10000;
	}
	if (set->lat != CPUFREQ_VAL_UNKNOWN) {
		if (fill_set->lat != CPUFREQ_VAL_UNKNOWN)
			fill_set->lat += set->lat;
		else
			fill_set->lat = set->lat;
	}
	CF_DEBUG("dup set considering derived setting %d\n", fill_set->freq);

	/*
	 * If we copied an old level that we already modified (say, at 100%),
	 * we need to remove that setting before adding this one.  Since we
	 * process each setting array in order, we know any settings for this
	 * driver will be found at the end.
	 */
	for (i = fill->rel_count; i != 0; i--) {
		if (fill->rel_set[i - 1].dev != set->dev)
			break;
		CF_DEBUG("removed last relative driver: %s\n",
		    device_get_nameunit(set->dev));
		fill->rel_count--;
	}

	/*
	 * Insert the new level in sorted order.  If it is a duplicate of an
	 * existing level (1) or has an absolute setting higher than the
	 * existing level (2), do not add it.  We can do this since any such
	 * level is guaranteed use less power.  For example (1), a level with
	 * one absolute setting of 800 Mhz uses less power than one composed
	 * of an absolute setting of 1600 Mhz and a relative setting at 50%.
	 * Also for example (2), a level of 800 Mhz/75% is preferable to
	 * 1600 Mhz/25% even though the latter has a lower total frequency.
	 */
	list = &sc->all_levels;
	KASSERT(!TAILQ_EMPTY(list), ("all levels list empty in dup set"));
	TAILQ_FOREACH_REVERSE(itr, list, cf_level_lst, link) {
		itr_set = &itr->total_set;
		if (CPUFREQ_CMP(fill_set->freq, itr_set->freq)) {
			CF_DEBUG("dup set rejecting %d (dupe)\n",
			    fill_set->freq);
			itr = NULL;
			break;
		} else if (fill_set->freq < itr_set->freq) {
			if (fill->abs_set.freq <= itr->abs_set.freq) {
				CF_DEBUG(
			"dup done, inserting new level %d after %d\n",
				    fill_set->freq, itr_set->freq);
				TAILQ_INSERT_AFTER(list, itr, fill, link);
				sc->all_count++;
			} else {
				CF_DEBUG("dup set rejecting %d (abs too big)\n",
				    fill_set->freq);
				itr = NULL;
			}
			break;
		}
	}

	/* We didn't find a good place for this new level so free it. */
	if (itr == NULL) {
		CF_DEBUG("dup set freeing new level %d (not optimal)\n",
		    fill_set->freq);
		free(fill, M_TEMP);
		fill = NULL;
	}

	return (fill);
}

static int
cpufreq_curr_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct cpufreq_softc *sc;
	struct cf_level *levels;
	int best, count, diff, bdiff, devcount, error, freq, i, n;
	device_t *devs;

	devs = NULL;
	sc = oidp->oid_arg1;
	levels = sc->levels_buf;

	error = CPUFREQ_GET(sc->dev, &levels[0]);
	if (error)
		goto out;
	freq = levels[0].total_set.freq;
	error = sysctl_handle_int(oidp, &freq, 0, req);
	if (error != 0 || req->newptr == NULL)
		goto out;

	/*
	 * While we only call cpufreq_get() on one device (assuming all
	 * CPUs have equal levels), we call cpufreq_set() on all CPUs.
	 * This is needed for some MP systems.
	 */
	error = devclass_get_devices(cpufreq_dc, &devs, &devcount);
	if (error)
		goto out;
	for (n = 0; n < devcount; n++) {
		count = CF_MAX_LEVELS;
		error = CPUFREQ_LEVELS(devs[n], levels, &count);
		if (error) {
			if (error == E2BIG)
				printf(
			"cpufreq: need to increase CF_MAX_LEVELS\n");
			break;
		}
		best = 0;
		bdiff = 1 << 30;
		for (i = 0; i < count; i++) {
			diff = abs(levels[i].total_set.freq - freq);
			if (diff < bdiff) {
				bdiff = diff;
				best = i;
			}
		}
		error = CPUFREQ_SET(devs[n], &levels[best], CPUFREQ_PRIO_USER);
	}

out:
	if (devs)
		free(devs, M_TEMP);
	return (error);
}

static int
cpufreq_levels_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct cpufreq_softc *sc;
	struct cf_level *levels;
	struct cf_setting *set;
	struct sbuf sb;
	int count, error, i;

	sc = oidp->oid_arg1;
	sbuf_new(&sb, NULL, 128, SBUF_AUTOEXTEND);

	/* Get settings from the device and generate the output string. */
	count = CF_MAX_LEVELS;
	levels = sc->levels_buf;
	if (levels == NULL) {
		sbuf_delete(&sb);
		return (ENOMEM);
	}
	error = CPUFREQ_LEVELS(sc->dev, levels, &count);
	if (error) {
		if (error == E2BIG)
			printf("cpufreq: need to increase CF_MAX_LEVELS\n");
		goto out;
	}
	if (count) {
		for (i = 0; i < count; i++) {
			set = &levels[i].total_set;
			sbuf_printf(&sb, "%d/%d ", set->freq, set->power);
		}
	} else
		sbuf_cpy(&sb, "0");
	sbuf_trim(&sb);
	sbuf_finish(&sb);
	error = sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);

out:
	sbuf_delete(&sb);
	return (error);
}

static int
cpufreq_settings_sysctl(SYSCTL_HANDLER_ARGS)
{
	device_t dev;
	struct cf_setting *sets;
	struct sbuf sb;
	int error, i, set_count;

	dev = oidp->oid_arg1;
	sbuf_new(&sb, NULL, 128, SBUF_AUTOEXTEND);

	/* Get settings from the device and generate the output string. */
	set_count = MAX_SETTINGS;
	sets = malloc(set_count * sizeof(*sets), M_TEMP, M_NOWAIT);
	if (sets == NULL) {
		sbuf_delete(&sb);
		return (ENOMEM);
	}
	error = CPUFREQ_DRV_SETTINGS(dev, sets, &set_count);
	if (error)
		goto out;
	if (set_count) {
		for (i = 0; i < set_count; i++)
			sbuf_printf(&sb, "%d/%d ", sets[i].freq, sets[i].power);
	} else
		sbuf_cpy(&sb, "0");
	sbuf_trim(&sb);
	sbuf_finish(&sb);
	error = sysctl_handle_string(oidp, sbuf_data(&sb), sbuf_len(&sb), req);

out:
	free(sets, M_TEMP);
	sbuf_delete(&sb);
	return (error);
}

int
cpufreq_register(device_t dev)
{
	struct cpufreq_softc *sc;
	device_t cf_dev, cpu_dev;

	/* Add a sysctl to get each driver's settings separately. */
	SYSCTL_ADD_PROC(device_get_sysctl_ctx(dev),
	    SYSCTL_CHILDREN(device_get_sysctl_tree(dev)),
	    OID_AUTO, "freq_settings", CTLTYPE_STRING | CTLFLAG_RD, dev, 0,
	    cpufreq_settings_sysctl, "A", "CPU frequency driver settings");

	/*
	 * Add only one cpufreq device to each CPU.  Currently, all CPUs
	 * must offer the same levels and be switched at the same time.
	 */
	cpu_dev = device_get_parent(dev);
	if ((cf_dev = device_find_child(cpu_dev, "cpufreq", -1))) {
		sc = device_get_softc(cf_dev);
		sc->max_mhz = CPUFREQ_VAL_UNKNOWN;
		return (0);
	}

	/* Add the child device and possibly sysctls. */
	cf_dev = BUS_ADD_CHILD(cpu_dev, 0, "cpufreq", -1);
	if (cf_dev == NULL)
		return (ENOMEM);
	device_quiet(cf_dev);

	return (device_probe_and_attach(cf_dev));
}

int
cpufreq_unregister(device_t dev)
{
	device_t cf_dev, *devs;
	int cfcount, devcount, error, i, type;

	/*
	 * If this is the last cpufreq child device, remove the control
	 * device as well.  We identify cpufreq children by calling a method
	 * they support.
	 */
	error = device_get_children(device_get_parent(dev), &devs, &devcount);
	if (error)
		return (error);
	cf_dev = device_find_child(device_get_parent(dev), "cpufreq", -1);
	if (cf_dev == NULL) {
		device_printf(dev,
	"warning: cpufreq_unregister called with no cpufreq device active\n");
		free(devs, M_TEMP);
		return (0);
	}
	cfcount = 0;
	for (i = 0; i < devcount; i++) {
		if (!device_is_attached(devs[i]))
			continue;
		if (CPUFREQ_DRV_TYPE(devs[i], &type) == 0)
			cfcount++;
	}
	if (cfcount <= 1)
		device_delete_child(device_get_parent(cf_dev), cf_dev);
	free(devs, M_TEMP);

	return (0);
}

int
cpufreq_settings_changed(device_t dev)
{

	EVENTHANDLER_INVOKE(cpufreq_levels_changed,
	    device_get_unit(device_get_parent(dev)));
	return (0);
}
