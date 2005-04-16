/*
 * include/asm-sh/machvec_init.h
 *
 * Copyright 2000 Stuart Menefy (stuart.menefy@st.com)
 *
 * May be copied or modified under the terms of the GNU General Public
 * License.  See linux/COPYING for more information.
 *
 * This file has goodies to help simplify instantiation of machine vectors.
 */

#ifndef __SH_MACHVEC_INIT_H
#define __SH_MACHVEC_INIT_H

#include <linux/config.h>

/*
 * In a GENERIC kernel, we have lots of these vectors floating about,
 * all but one of which we want to go away.  In a non-GENERIC kernel,
 * we want only one, ever.
 *
 * Accomplish this in the GENERIC kernel by puting all of the vectors
 * in the .init.data section where they'll go away.  We'll copy the
 * one we want to the real alpha_mv vector in setup_arch.
 *
 * Accomplish this in a non-GENERIC kernel by ifdef'ing out all but
 * one of the vectors, which will not reside in .init.data.  We then
 * alias this one vector to alpha_mv, so no copy is needed.
 *
 * Upshot: set __initdata to nothing for non-GENERIC kernels.
 *
 * Note we do the same thing for the UNKNOWN kernel, as we need to write
 * to the machine vector while setting it up.
 */

#if defined(CONFIG_SH_GENERIC) || defined(CONFIG_SH_UNKNOWN)
#define __initmv __attribute__((unused,__section__ (".machvec.init")))
#define ALIAS_MV(x)
#else
#define __initmv

/* GCC actually has a syntax for defining aliases, but is under some
   delusion that you shouldn't be able to declare it extern somewhere
   else beforehand.  Fine.  We'll do it ourselves.  */
#if 0
#define ALIAS_MV(system) \
  struct sh_machine_vector sh_mv __attribute__((alias("mv_"#system)));
#else
#define ALIAS_MV(system) \
  asm(".global sh_mv\nsh_mv = mv_"#system );
#endif
#endif /* GENERIC */

#endif /* __SH_MACHVEC_INIT_H */
