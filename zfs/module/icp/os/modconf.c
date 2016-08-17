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
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/modctl.h>

/*
 * Null operations; used for uninitialized and "misc" modules.
 */
static int mod_null(struct modlmisc *, struct modlinkage *);
static int mod_infonull(void *, struct modlinkage *, int *);

/*
 * Cryptographic Modules
 */
struct mod_ops mod_cryptoops = {
	.modm_install = mod_null,
	.modm_remove = mod_null,
	.modm_info = mod_infonull
};

/*
 * Null operation; return 0.
 */
static int
mod_null(struct modlmisc *modl, struct modlinkage *modlp)
{
	return (0);
}

/*
 * Status for User modules.
 */
static int
mod_infonull(void *modl, struct modlinkage *modlp, int *p0)
{
	*p0 = -1;		/* for modinfo display */
	return (0);
}

/*
 * Install a module.
 * (This routine is in the Solaris SPARC DDI/DKI)
 */
int
mod_install(struct modlinkage *modlp)
{
	int retval = -1;	/* No linkage structures */
	struct modlmisc **linkpp;
	struct modlmisc **linkpp1;

	if (modlp->ml_rev != MODREV_1) {
		cmn_err(CE_WARN, "mod_install: "
		    "modlinkage structure is not MODREV_1\n");
		return (EINVAL);
	}
	linkpp = (struct modlmisc **)&modlp->ml_linkage[0];

	while (*linkpp != NULL) {
		if ((retval = MODL_INSTALL(*linkpp, modlp)) != 0) {
			linkpp1 = (struct modlmisc **)&modlp->ml_linkage[0];

			while (linkpp1 != linkpp) {
				MODL_REMOVE(*linkpp1, modlp); /* clean up */
				linkpp1++;
			}
			break;
		}
		linkpp++;
	}
	return (retval);
}

static char *reins_err =
	"Could not reinstall %s\nReboot to correct the problem";

/*
 * Remove a module.  This is called by the module wrapper routine.
 * (This routine is in the Solaris SPARC DDI/DKI)
 */
int
mod_remove(struct modlinkage *modlp)
{
	int retval = 0;
	struct modlmisc **linkpp, *last_linkp;

	linkpp = (struct modlmisc **)&modlp->ml_linkage[0];

	while (*linkpp != NULL) {
		if ((retval = MODL_REMOVE(*linkpp, modlp)) != 0) {
			last_linkp = *linkpp;
			linkpp = (struct modlmisc **)&modlp->ml_linkage[0];
			while (*linkpp != last_linkp) {
				if (MODL_INSTALL(*linkpp, modlp) != 0) {
					cmn_err(CE_WARN, reins_err,
					    (*linkpp)->misc_linkinfo);
					break;
				}
				linkpp++;
			}
			break;
		}
		linkpp++;
	}
	return (retval);
}

/*
 * Get module status.
 * (This routine is in the Solaris SPARC DDI/DKI)
 */
int
mod_info(struct modlinkage *modlp, struct modinfo *modinfop)
{
	int i;
	int retval = 0;
	struct modspecific_info *msip;
	struct modlmisc **linkpp;

	modinfop->mi_rev = modlp->ml_rev;

	linkpp = (struct modlmisc **)modlp->ml_linkage;
	msip = &modinfop->mi_msinfo[0];

	for (i = 0; i < MODMAXLINK; i++) {
		if (*linkpp == NULL) {
			msip->msi_linkinfo[0] = '\0';
		} else {
			(void) strlcpy(msip->msi_linkinfo,
			    (*linkpp)->misc_linkinfo, MODMAXLINKINFOLEN);
			retval = MODL_INFO(*linkpp, modlp, &msip->msi_p0);
			if (retval != 0)
				break;
			linkpp++;
		}
		msip++;
	}

	if (modinfop->mi_info == MI_INFO_LINKAGE) {
		/*
		 * Slight kludge used to extract the address of the
		 * modlinkage structure from the module (just after
		 * loading a module for the very first time)
		 */
		modinfop->mi_base = (void *)modlp;
	}

	if (retval == 0)
		return (1);
	return (0);
}
