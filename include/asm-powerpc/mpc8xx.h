/* This is the single file included by all MPC8xx build options.
 * Since there are many different boards and no standard configuration,
 * we have a unique include file for each.  Rather than change every
 * file that has to include MPC8xx configuration, they all include
 * this one and the configuration switching is done here.
 */
#ifdef __KERNEL__
#ifndef __CONFIG_8xx_DEFS
#define __CONFIG_8xx_DEFS


#ifdef CONFIG_8xx

#ifdef CONFIG_FADS
#include <platforms/fads.h>
#endif

#if defined(CONFIG_MPC86XADS)
#include <platforms/8xx/mpc86xads.h>
#endif

#if defined(CONFIG_MPC885ADS)
#include <platforms/8xx/mpc885ads.h>
#endif

#ifdef CONFIG_PCMCIA_M8XX
extern struct mpc8xx_pcmcia_ops m8xx_pcmcia_ops;
#endif

#endif /* CONFIG_8xx */
#endif /* __CONFIG_8xx_DEFS */
#endif /* __KERNEL__ */
