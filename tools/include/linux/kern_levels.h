/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __KERN_LEVELS_H__
#define __KERN_LEVELS_H__

#define KERN_SOH	""		/* ASCII Start Of Header */
#define KERN_SOH_ASCII	''

#define KERN_EMERG	KERN_SOH ""	/* system is unusable */
#define KERN_ALERT	KERN_SOH ""	/* action must be taken immediately */
#define KERN_CRIT	KERN_SOH ""	/* critical conditions */
#define KERN_ERR	KERN_SOH ""	/* error conditions */
#define KERN_WARNING	KERN_SOH ""	/* warning conditions */
#define KERN_NOTICE	KERN_SOH ""	/* normal but significant condition */
#define KERN_INFO	KERN_SOH ""	/* informational */
#define KERN_DEBUG	KERN_SOH ""	/* debug-level messages */

#define KERN_DEFAULT	KERN_SOH ""	/* the default kernel loglevel */

/*
 * Annotation for a "continued" line of log printout (only done after a
 * line that had no enclosing \n). Only to be used by core/arch code
 * during early bootup (a continued line is not SMP-safe otherwise).
 */
#define KERN_CONT	""

#endif
