#ifndef _ASM_IA64_SN_FEATURE_SETS_H
#define _ASM_IA64_SN_FEATURE_SETS_H

/*
 * SN PROM Features
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2005-2006 Silicon Graphics, Inc.  All rights reserved.
 */


/* --------------------- PROM Features -----------------------------*/
extern int sn_prom_feature_available(int id);

#define MAX_PROM_FEATURE_SETS			2

/*
 * The following defines features that may or may not be supported by the
 * current PROM. The OS uses sn_prom_feature_available(feature) to test for
 * the presence of a PROM feature. Down rev (old) PROMs will always test
 * "false" for new features.
 *
 * Use:
 * 		if (sn_prom_feature_available(PRF_XXX))
 * 			...
 */

#define PRF_PAL_CACHE_FLUSH_SAFE	0
#define PRF_DEVICE_FLUSH_LIST		1



/* --------------------- OS Features -------------------------------*/

/*
 * The following defines OS features that are optionally present in
 * the operating system.
 * During boot, PROM is notified of these features via a series of calls:
 *
 * 		ia64_sn_set_os_feature(feature1);
 *
 * Once enabled, a feature cannot be disabled.
 *
 * By default, features are disabled unless explicitly enabled.
 */
#define  OSF_MCA_SLV_TO_OS_INIT_SLV	0
#define  OSF_FEAT_LOG_SBES		1

#endif /* _ASM_IA64_SN_FEATURE_SETS_H */
