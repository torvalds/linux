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

#define __initmv __attribute__((unused,__section__ (".machvec.init")))
#define ALIAS_MV(system) \
  asm(".global sh_mv\nsh_mv = mv_"#system );

#endif /* __SH_MACHVEC_INIT_H */
