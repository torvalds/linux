/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2002 Dima Dorfman.
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

/*
 * DEVFS ruleset implementation.
 *
 * A note on terminology: To "run" a rule on a dirent is to take the
 * prescribed action; to "apply" a rule is to check whether it matches
 * a dirent and run if if it does.
 *
 * A note on locking: Only foreign entry points (non-static functions)
 * should deal with locking.  Everything else assumes we already hold
 * the required kind of lock.
 *
 * A note on namespace: devfs_rules_* are the non-static functions for
 * the entire "ruleset" subsystem, devfs_rule_* are the static
 * functions that operate on rules, and devfs_ruleset_* are the static
 * functions that operate on rulesets.  The line between the last two
 * isn't always clear, but the guideline is still useful.
 *
 * A note on "special" identifiers: Ruleset 0 is the NULL, or empty,
 * ruleset; it cannot be deleted or changed in any way.  This may be
 * assumed inside the code; e.g., a ruleset of 0 may be interpeted to
 * mean "no ruleset".  The interpretation of rule 0 is
 * command-dependent, but in no case is there a real rule with number
 * 0.
 *
 * A note on errno codes: To make it easier for the userland to tell
 * what went wrong, we sometimes use errno codes that are not entirely
 * appropriate for the error but that would be less ambiguous than the
 * appropriate "generic" code.  For example, when we can't find a
 * ruleset, we return ESRCH instead of ENOENT (except in
 * DEVFSIO_{R,S}GETNEXT, where a nonexistent ruleset means "end of
 * list", and the userland expects ENOENT to be this indicator); this
 * way, when an operation fails, it's clear that what couldn't be
 * found is a ruleset and not a rule (well, it's clear to those who
 * know the convention).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/priv.h>
#include <sys/dirent.h>
#include <sys/ioccom.h>
#include <sys/lock.h>
#include <sys/sx.h>

#include <fs/devfs/devfs.h>
#include <fs/devfs/devfs_int.h>

/*
 * Kernel version of devfs_rule.
 */
struct devfs_krule {
	TAILQ_ENTRY(devfs_krule)	dk_list;
	struct devfs_ruleset		*dk_ruleset;
	struct devfs_rule		dk_rule;
};

TAILQ_HEAD(rulehead, devfs_krule);
static MALLOC_DEFINE(M_DEVFSRULE, "DEVFS_RULE", "DEVFS rule storage");

/*
 * Structure to describe a ruleset.
 */
struct devfs_ruleset {
	TAILQ_ENTRY(devfs_ruleset)	ds_list;
	struct rulehead			ds_rules;
	devfs_rsnum			ds_number;
	int				ds_refcount;
};

static devfs_rid devfs_rid_input(devfs_rid rid, struct devfs_mount *dm);

static void devfs_rule_applyde_recursive(struct devfs_krule *dk,
		struct devfs_mount *dm, struct devfs_dirent *de);
static void devfs_rule_applydm(struct devfs_krule *dk, struct devfs_mount *dm);
static int  devfs_rule_autonumber(struct devfs_ruleset *ds, devfs_rnum *rnp);
static struct devfs_krule *devfs_rule_byid(devfs_rid rid);
static int  devfs_rule_delete(struct devfs_krule *dkp);
static struct cdev *devfs_rule_getdev(struct devfs_dirent *de);
static int  devfs_rule_input(struct devfs_rule *dr, struct devfs_mount *dm);
static int  devfs_rule_insert(struct devfs_rule *dr);
static int  devfs_rule_match(struct devfs_krule *dk, struct devfs_mount *dm,
		struct devfs_dirent *de);
static int  devfs_rule_matchpath(struct devfs_krule *dk, struct devfs_mount *dm,
		struct devfs_dirent *de);
static void devfs_rule_run(struct devfs_krule *dk, struct devfs_mount *dm,
		struct devfs_dirent *de, unsigned depth);

static void devfs_ruleset_applyde(struct devfs_ruleset *ds,
		struct devfs_mount *dm, struct devfs_dirent *de,
		unsigned depth);
static void devfs_ruleset_applydm(struct devfs_ruleset *ds,
		struct devfs_mount *dm);
static struct devfs_ruleset *devfs_ruleset_bynum(devfs_rsnum rsnum);
static struct devfs_ruleset *devfs_ruleset_create(devfs_rsnum rsnum);
static void devfs_ruleset_reap(struct devfs_ruleset *dsp);
static int  devfs_ruleset_use(devfs_rsnum rsnum, struct devfs_mount *dm);

static struct sx sx_rules;
SX_SYSINIT(sx_rules, &sx_rules, "DEVFS ruleset lock");

static TAILQ_HEAD(, devfs_ruleset) devfs_rulesets =
    TAILQ_HEAD_INITIALIZER(devfs_rulesets);

/*
 * Called to apply the proper rules for 'de' before it can be
 * exposed to the userland.  This should be called with an exclusive
 * lock on dm in case we need to run anything.
 */
void
devfs_rules_apply(struct devfs_mount *dm, struct devfs_dirent *de)
{
	struct devfs_ruleset *ds;

	sx_assert(&dm->dm_lock, SX_XLOCKED);

	if (dm->dm_ruleset == 0)
		return;
	sx_slock(&sx_rules);
	ds = devfs_ruleset_bynum(dm->dm_ruleset);
	KASSERT(ds != NULL, ("mount-point has NULL ruleset"));
	devfs_ruleset_applyde(ds, dm, de, devfs_rule_depth);
	sx_sunlock(&sx_rules);
}

/*
 * Rule subsystem ioctl hook.
 */
int
devfs_rules_ioctl(struct devfs_mount *dm, u_long cmd, caddr_t data, struct thread *td)
{
	struct devfs_ruleset *ds;
	struct devfs_krule *dk;
	struct devfs_rule *dr;
	devfs_rsnum rsnum;
	devfs_rnum rnum;
	devfs_rid rid;
	int error;

	sx_assert(&dm->dm_lock, SX_XLOCKED);

	/*
	 * XXX: This returns an error regardless of whether we actually
	 * support the cmd or not.
	 *
	 * We could make this privileges finer grained if desired.
	 */
	error = priv_check(td, PRIV_DEVFS_RULE);
	if (error)
		return (error);

	sx_xlock(&sx_rules);

	switch (cmd) {
	case DEVFSIO_RADD:
		dr = (struct devfs_rule *)data;
		error = devfs_rule_input(dr, dm);
		if (error != 0)
			break;
		dk = devfs_rule_byid(dr->dr_id);
		if (dk != NULL) {
			error = EEXIST;
			break;
		}
		if (rid2rsn(dr->dr_id) == 0) {
			error = EIO;
			break;
		}
		error = devfs_rule_insert(dr);
		break;
	case DEVFSIO_RAPPLY:
		dr = (struct devfs_rule *)data;
		error = devfs_rule_input(dr, dm);
		if (error != 0)
			break;

		/*
		 * This is one of many possible hackish
		 * implementations.  The primary contender is an
		 * implementation where the rule we read in is
		 * temporarily inserted into some ruleset, perhaps
		 * with a hypothetical DRO_NOAUTO flag so that it
		 * doesn't get used where it isn't intended, and
		 * applied in the normal way.  This can be done in the
		 * userland (DEVFSIO_ADD, DEVFSIO_APPLYID,
		 * DEVFSIO_DEL) or in the kernel; either way it breaks
		 * some corner case assumptions in other parts of the
		 * code (not that this implementation doesn't do
		 * that).
		 */
		if (dr->dr_iacts & DRA_INCSET &&
		    devfs_ruleset_bynum(dr->dr_incset) == NULL) {
			error = ESRCH;
			break;
		}
		dk = malloc(sizeof(*dk), M_TEMP, M_WAITOK | M_ZERO);
		memcpy(&dk->dk_rule, dr, sizeof(*dr));
		devfs_rule_applydm(dk, dm);
		free(dk, M_TEMP);
		break;
	case DEVFSIO_RAPPLYID:
		rid = *(devfs_rid *)data;
		rid = devfs_rid_input(rid, dm);
		dk = devfs_rule_byid(rid);
		if (dk == NULL) {
			error = ENOENT;
			break;
		}
		devfs_rule_applydm(dk, dm);
		break;
	case DEVFSIO_RDEL:
		rid = *(devfs_rid *)data;
		rid = devfs_rid_input(rid, dm);
		dk = devfs_rule_byid(rid);
		if (dk == NULL) {
			error = ENOENT;
			break;
		}
		ds = dk->dk_ruleset;
		error = devfs_rule_delete(dk);
		break;
	case DEVFSIO_RGETNEXT:
		dr = (struct devfs_rule *)data;
		error = devfs_rule_input(dr, dm);
		if (error != 0)
			break;
		/*
		 * We can't use devfs_rule_byid() here since that
		 * requires the rule specified to exist, but we want
		 * getnext(N) to work whether there is a rule N or not
		 * (specifically, getnext(0) must work, but we should
		 * never have a rule 0 since the add command
		 * interprets 0 to mean "auto-number").
		 */
		ds = devfs_ruleset_bynum(rid2rsn(dr->dr_id));
		if (ds == NULL) {
			error = ENOENT;
			break;
		}
		rnum = rid2rn(dr->dr_id);
		TAILQ_FOREACH(dk, &ds->ds_rules, dk_list) {
			if (rid2rn(dk->dk_rule.dr_id) > rnum)
				break;
		}
		if (dk == NULL) {
			error = ENOENT;
			break;
		}
		memcpy(dr, &dk->dk_rule, sizeof(*dr));
		break;
	case DEVFSIO_SUSE:
		rsnum = *(devfs_rsnum *)data;
		error = devfs_ruleset_use(rsnum, dm);
		break;
	case DEVFSIO_SAPPLY:
		rsnum = *(devfs_rsnum *)data;
		rsnum = rid2rsn(devfs_rid_input(mkrid(rsnum, 0), dm));
		ds = devfs_ruleset_bynum(rsnum);
		if (ds == NULL) {
			error = ESRCH;
			break;
		}
		devfs_ruleset_applydm(ds, dm);
		break;
	case DEVFSIO_SGETNEXT:
		rsnum = *(devfs_rsnum *)data;
		TAILQ_FOREACH(ds, &devfs_rulesets, ds_list) {
			if (ds->ds_number > rsnum)
				break;
		}
		if (ds == NULL) {
			error = ENOENT;
			break;
		}
		*(devfs_rsnum *)data = ds->ds_number;
		break;
	default:
		error = ENOIOCTL;
		break;
	}

	sx_xunlock(&sx_rules);
	return (error);
}

/*
 * Adjust the rule identifier to use the ruleset of dm if one isn't
 * explicitly specified.
 *
 * Note that after this operation, rid2rsn(rid) might still be 0, and
 * that's okay; ruleset 0 is a valid ruleset, but when it's read in
 * from the userland, it means "current ruleset for this mount-point".
 */
static devfs_rid
devfs_rid_input(devfs_rid rid, struct devfs_mount *dm)
{

	if (rid2rsn(rid) == 0)
		return (mkrid(dm->dm_ruleset, rid2rn(rid)));
	else
		return (rid);
}

/*
 * Apply dk to de and everything under de.
 *
 * XXX: This method needs a function call for every nested
 * subdirectory in a devfs mount.  If we plan to have many of these,
 * we might eventually run out of kernel stack space.
 * XXX: a linear search could be done through the cdev list instead.
 */
static void
devfs_rule_applyde_recursive(struct devfs_krule *dk, struct devfs_mount *dm,
    struct devfs_dirent *de)
{
	struct devfs_dirent *de2;

	TAILQ_FOREACH(de2, &de->de_dlist, de_list)
		devfs_rule_applyde_recursive(dk, dm, de2);
	devfs_rule_run(dk, dm, de, devfs_rule_depth);
}

/*
 * Apply dk to all entires in dm.
 */
static void
devfs_rule_applydm(struct devfs_krule *dk, struct devfs_mount *dm)
{

	devfs_rule_applyde_recursive(dk, dm, dm->dm_rootdir);
}

/*
 * Automatically select a number for a new rule in ds, and write the
 * result into rnump.
 */
static int
devfs_rule_autonumber(struct devfs_ruleset *ds, devfs_rnum *rnump)
{
	struct devfs_krule *dk;

	/* Find the last rule. */
	dk = TAILQ_LAST(&ds->ds_rules, rulehead);
	if (dk == NULL)
		*rnump = 100;
	else {
		*rnump = rid2rn(dk->dk_rule.dr_id) + 100;
		/* Detect overflow. */
		if (*rnump < rid2rn(dk->dk_rule.dr_id))
			return (ERANGE);
	}
	KASSERT(devfs_rule_byid(mkrid(ds->ds_number, *rnump)) == NULL,
	    ("autonumbering resulted in an already existing rule"));
	return (0);
}

/*
 * Find a krule by id.
 */
static struct devfs_krule *
devfs_rule_byid(devfs_rid rid)
{
	struct devfs_ruleset *ds;
	struct devfs_krule *dk;
	devfs_rnum rn;

	rn = rid2rn(rid);
	ds = devfs_ruleset_bynum(rid2rsn(rid));
	if (ds == NULL)
		return (NULL);
	TAILQ_FOREACH(dk, &ds->ds_rules, dk_list) {
		if (rid2rn(dk->dk_rule.dr_id) == rn)
			return (dk);
		else if (rid2rn(dk->dk_rule.dr_id) > rn)
			break;
	}
	return (NULL);
}

/*
 * Remove dkp from any lists it may be on and remove memory associated
 * with it.
 */
static int
devfs_rule_delete(struct devfs_krule *dk)
{
	struct devfs_ruleset *ds;

	if (dk->dk_rule.dr_iacts & DRA_INCSET) {
		ds = devfs_ruleset_bynum(dk->dk_rule.dr_incset);
		KASSERT(ds != NULL, ("DRA_INCSET but bad dr_incset"));
		--ds->ds_refcount;
		devfs_ruleset_reap(ds);
	}
	ds = dk->dk_ruleset;
	TAILQ_REMOVE(&ds->ds_rules, dk, dk_list);
	devfs_ruleset_reap(ds);
	free(dk, M_DEVFSRULE);
	return (0);
}

/*
 * Get a struct cdev *corresponding to de so we can try to match rules based
 * on it.  If this routine returns NULL, there is no struct cdev *associated
 * with the dirent (symlinks and directories don't have dev_ts), and
 * the caller should assume that any critera dependent on a dev_t
 * don't match.
 */
static struct cdev *
devfs_rule_getdev(struct devfs_dirent *de)
{

	if (de->de_cdp == NULL)
		return (NULL);
	if (de->de_cdp->cdp_flags & CDP_ACTIVE)
		return (&de->de_cdp->cdp_c);
	else
		return (NULL);
}

/*
 * Do what we need to do to a rule that we just loaded from the
 * userland.  In particular, we need to check the magic, and adjust
 * the ruleset appropriate if desired.
 */
static int
devfs_rule_input(struct devfs_rule *dr, struct devfs_mount *dm)
{

	if (dr->dr_magic != DEVFS_MAGIC)
		return (ERPCMISMATCH);
	dr->dr_id = devfs_rid_input(dr->dr_id, dm);
	return (0);
}

/*
 * Import dr into the appropriate place in the kernel (i.e., make a
 * krule).  The value of dr is copied, so the pointer may be destroyed
 * after this call completes.
 */
static int
devfs_rule_insert(struct devfs_rule *dr)
{
	struct devfs_ruleset *ds, *dsi;
	struct devfs_krule *k1;
	struct devfs_krule *dk;
	devfs_rsnum rsnum;
	devfs_rnum dkrn;
	int error;

	/*
	 * This stuff seems out of place here, but we want to do it as
	 * soon as possible so that if it fails, we don't have to roll
	 * back any changes we already made (e.g., ruleset creation).
	 */
	if (dr->dr_iacts & DRA_INCSET) {
		dsi = devfs_ruleset_bynum(dr->dr_incset);
		if (dsi == NULL)
			return (ESRCH);
	} else
		dsi = NULL;

	rsnum = rid2rsn(dr->dr_id);
	KASSERT(rsnum != 0, ("Inserting into ruleset zero"));

	ds = devfs_ruleset_bynum(rsnum);
	if (ds == NULL)
		ds = devfs_ruleset_create(rsnum);
	dkrn = rid2rn(dr->dr_id);
	if (dkrn == 0) {
		error = devfs_rule_autonumber(ds, &dkrn);
		if (error != 0) {
			devfs_ruleset_reap(ds);
			return (error);
		}
	}

	dk = malloc(sizeof(*dk), M_DEVFSRULE, M_WAITOK | M_ZERO);
	dk->dk_ruleset = ds;
	if (dsi != NULL)
		++dsi->ds_refcount;
	/* XXX: Inspect dr? */
	memcpy(&dk->dk_rule, dr, sizeof(*dr));
	dk->dk_rule.dr_id = mkrid(rid2rsn(dk->dk_rule.dr_id), dkrn);

	TAILQ_FOREACH(k1, &ds->ds_rules, dk_list) {
		if (rid2rn(k1->dk_rule.dr_id) > dkrn) {
			TAILQ_INSERT_BEFORE(k1, dk, dk_list);
			break;
		}
	}
	if (k1 == NULL)
		TAILQ_INSERT_TAIL(&ds->ds_rules, dk, dk_list);
	return (0);
}

/*
 * Determine whether dk matches de.  Returns 1 if dk should be run on
 * de; 0, otherwise.
 */
static int
devfs_rule_match(struct devfs_krule *dk, struct devfs_mount *dm,
    struct devfs_dirent *de)
{
	struct devfs_rule *dr = &dk->dk_rule;
	struct cdev *dev;
	struct cdevsw *dsw;
	int ref;

	dev = devfs_rule_getdev(de);
	/*
	 * At this point, if dev is NULL, we should assume that any
	 * criteria that depend on it don't match.  We should *not*
	 * just ignore them (i.e., act like they weren't specified),
	 * since that makes a rule that only has criteria dependent on
	 * the struct cdev *match all symlinks and directories.
	 *
	 * Note also that the following tests are somewhat reversed:
	 * They're actually testing to see whether the condition does
	 * *not* match, since the default is to assume the rule should
	 * be run (such as if there are no conditions).
	 */
	if (dr->dr_icond & DRC_DSWFLAGS) {
		if (dev == NULL)
			return (0);
		dsw = dev_refthread(dev, &ref);
		if (dsw == NULL)
			return (0);
		if ((dsw->d_flags & dr->dr_dswflags) == 0) {
			dev_relthread(dev, ref);
			return (0);
		}
		dev_relthread(dev, ref);
	}
	if (dr->dr_icond & DRC_PATHPTRN)
		if (!devfs_rule_matchpath(dk, dm, de))
			return (0);

	return (1);
}

/*
 * Determine whether dk matches de on account of dr_pathptrn.
 */
static int
devfs_rule_matchpath(struct devfs_krule *dk, struct devfs_mount *dm,
    struct devfs_dirent *de)
{
	struct devfs_rule *dr = &dk->dk_rule;
	struct cdev *dev;
	int match;
	char *pname, *specname;

	specname = NULL;
	dev = devfs_rule_getdev(de);
	if (dev != NULL)
		pname = dev->si_name;
	else if (de->de_dirent->d_type == DT_LNK ||
	    (de->de_dirent->d_type == DT_DIR && de != dm->dm_rootdir &&
	    (de->de_flags & (DE_DOT | DE_DOTDOT)) == 0)) {
		specname = malloc(SPECNAMELEN + 1, M_TEMP, M_WAITOK);
		pname = devfs_fqpn(specname, dm, de, NULL);
	} else
		return (0);

	KASSERT(pname != NULL, ("devfs_rule_matchpath: NULL pname"));
	match = fnmatch(dr->dr_pathptrn, pname, FNM_PATHNAME) == 0;
	free(specname, M_TEMP);
	return (match);
}

/*
 * Run dk on de.
 */
static void
devfs_rule_run(struct devfs_krule *dk,  struct devfs_mount *dm,
    struct devfs_dirent *de, unsigned depth)
{
	struct devfs_rule *dr = &dk->dk_rule;
	struct devfs_ruleset *ds;

	if (!devfs_rule_match(dk, dm, de))
		return;
	if (dr->dr_iacts & DRA_BACTS) {
		if (dr->dr_bacts & DRB_HIDE)
			de->de_flags |= DE_WHITEOUT;
		if (dr->dr_bacts & DRB_UNHIDE)
			de->de_flags &= ~DE_WHITEOUT;
	}
	if (dr->dr_iacts & DRA_UID)
		de->de_uid = dr->dr_uid;
	if (dr->dr_iacts & DRA_GID)
		de->de_gid = dr->dr_gid;
	if (dr->dr_iacts & DRA_MODE)
		de->de_mode = dr->dr_mode;
	if (dr->dr_iacts & DRA_INCSET) {
		/*
		 * XXX: we should tell the user if the depth is exceeded here
		 * XXX: but it is not obvious how to.  A return value will
		 * XXX: not work as this is called when devices are created
		 * XXX: long time after the rules were instantiated.
		 * XXX: a printf() would probably give too much noise, or
		 * XXX: DoS the machine.  I guess a rate-limited message
		 * XXX: might work.
		 */
		if (depth > 0) {
			ds = devfs_ruleset_bynum(dk->dk_rule.dr_incset);
			KASSERT(ds != NULL, ("DRA_INCSET but bad dr_incset"));
			devfs_ruleset_applyde(ds, dm, de, depth - 1);
		}
	}
}

/*
 * Apply all the rules in ds to de.
 */
static void
devfs_ruleset_applyde(struct devfs_ruleset *ds, struct devfs_mount *dm,
    struct devfs_dirent *de, unsigned depth)
{
	struct devfs_krule *dk;

	TAILQ_FOREACH(dk, &ds->ds_rules, dk_list)
		devfs_rule_run(dk, dm, de, depth);
}

/*
 * Apply all the rules in ds to all the entires in dm.
 */
static void
devfs_ruleset_applydm(struct devfs_ruleset *ds, struct devfs_mount *dm)
{
	struct devfs_krule *dk;

	/*
	 * XXX: Does it matter whether we do
	 *
	 *	foreach(dk in ds)
	 *		foreach(de in dm)
	 *			apply(dk to de)
	 *
	 * as opposed to
	 *
	 *	foreach(de in dm)
	 *		foreach(dk in ds)
	 *			apply(dk to de)
	 *
	 * The end result is obviously the same, but does the order
	 * matter?
	 */
	TAILQ_FOREACH(dk, &ds->ds_rules, dk_list)
		devfs_rule_applydm(dk, dm);
}

/*
 * Find a ruleset by number.
 */
static struct devfs_ruleset *
devfs_ruleset_bynum(devfs_rsnum rsnum)
{
	struct devfs_ruleset *ds;

	TAILQ_FOREACH(ds, &devfs_rulesets, ds_list) {
		if (ds->ds_number == rsnum)
			return (ds);
	}
	return (NULL);
}

/*
 * Create a new ruleset.
 */
static struct devfs_ruleset *
devfs_ruleset_create(devfs_rsnum rsnum)
{
	struct devfs_ruleset *s1;
	struct devfs_ruleset *ds;

	KASSERT(rsnum != 0, ("creating ruleset zero"));

	KASSERT(devfs_ruleset_bynum(rsnum) == NULL,
	    ("creating already existent ruleset %d", rsnum));

	ds = malloc(sizeof(*ds), M_DEVFSRULE, M_WAITOK | M_ZERO);
	ds->ds_number = rsnum;
	TAILQ_INIT(&ds->ds_rules);

	TAILQ_FOREACH(s1, &devfs_rulesets, ds_list) {
		if (s1->ds_number > rsnum) {
			TAILQ_INSERT_BEFORE(s1, ds, ds_list);
			break;
		}
	}
	if (s1 == NULL)
		TAILQ_INSERT_TAIL(&devfs_rulesets, ds, ds_list);
	return (ds);
}

/*
 * Remove a ruleset from the system if it's empty and not used
 * anywhere.  This should be called after every time a rule is deleted
 * from this ruleset or the reference count is decremented.
 */
static void
devfs_ruleset_reap(struct devfs_ruleset *ds)
{

	KASSERT(ds->ds_number != 0, ("reaping ruleset zero "));

	if (!TAILQ_EMPTY(&ds->ds_rules) || ds->ds_refcount != 0) 
		return;

	TAILQ_REMOVE(&devfs_rulesets, ds, ds_list);
	free(ds, M_DEVFSRULE);
}

/*
 * Make rsnum the active ruleset for dm.
 */
static int
devfs_ruleset_use(devfs_rsnum rsnum, struct devfs_mount *dm)
{
	struct devfs_ruleset *cds, *ds;

	if (dm->dm_ruleset != 0) {
		cds = devfs_ruleset_bynum(dm->dm_ruleset);
		--cds->ds_refcount;
		devfs_ruleset_reap(cds);
	}

	if (rsnum == 0) {
		dm->dm_ruleset = 0;
		return (0);
	}

	ds = devfs_ruleset_bynum(rsnum);
	if (ds == NULL)
		ds = devfs_ruleset_create(rsnum);
	/* These should probably be made atomic somehow. */
	++ds->ds_refcount;
	dm->dm_ruleset = rsnum;

	return (0);
}

void
devfs_rules_cleanup(struct devfs_mount *dm)
{
	struct devfs_ruleset *ds;

	sx_assert(&dm->dm_lock, SX_XLOCKED);
	if (dm->dm_ruleset != 0) {
		ds = devfs_ruleset_bynum(dm->dm_ruleset);
		--ds->ds_refcount;
		devfs_ruleset_reap(ds);
	}
}

/*
 * Make rsnum the active ruleset for dm (locked)
 */
void
devfs_ruleset_set(devfs_rsnum rsnum, struct devfs_mount *dm)
{

	sx_assert(&dm->dm_lock, SX_XLOCKED);

	sx_xlock(&sx_rules);
	devfs_ruleset_use(rsnum, dm);
	sx_xunlock(&sx_rules);
}

/*
 * Apply the current active ruleset on a mount
 */
void
devfs_ruleset_apply(struct devfs_mount *dm)
{
	struct devfs_ruleset *ds;

	sx_assert(&dm->dm_lock, SX_XLOCKED);

	sx_xlock(&sx_rules);
	if (dm->dm_ruleset == 0) {
		sx_xunlock(&sx_rules);
		return;
	}
	ds = devfs_ruleset_bynum(dm->dm_ruleset);
	if (ds != NULL)
		devfs_ruleset_applydm(ds, dm);
	sx_xunlock(&sx_rules);
}
