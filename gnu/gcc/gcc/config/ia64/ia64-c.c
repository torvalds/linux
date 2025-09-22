/* Definitions of C specific functions for GNU compiler.
   Copyright (C) 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Steve Ellcey <sje@cup.hp.com>

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "tree.h"
#include "cpplib.h"
#include "c-common.h"
#include "c-pragma.h"
#include "toplev.h"
#include "tm_p.h"

static void ia64_hpux_add_pragma_builtin (tree func);

void
ia64_hpux_handle_builtin_pragma (cpp_reader *pfile ATTRIBUTE_UNUSED)
{
  /* #pragma builtin name, name, name */

  enum cpp_ttype type;
  tree x;

  type = pragma_lex (&x);
  while (type == CPP_NAME)
    {
      ia64_hpux_add_pragma_builtin (x);
      type = pragma_lex (&x);
      if (type == CPP_COMMA)
	type = pragma_lex (&x);
    }
  if (type != CPP_EOF)
    warning (OPT_Wpragmas, "malformed #pragma builtin");
}

/* List of standard math functions which do not set matherr by default
   and which have a different version which does set errno and which we
   want to call *if* we have seen an extern for the routine and we have
   asked for strict C89 compatibility.  */

typedef struct c89_mathlib_names
{
        const char *realname; /* User visible function name.  */
        const char *c89name;  /* libm special name needed to set errno.  */
} c89_mathlib_names;

static const c89_mathlib_names c89_mathlib_name_list [] =
{
	{"acos", "_Acos_e#"},
	{"acosd", "_Acosd_e#"},
	{"acosdf", "_Acosdf_e#"},
	{"acosdl", "_Acosdl_e#"},
	{"acosdw", "_Acosdw_e#"},
	{"acosf", "_Acosf_e#"},
	{"acosh", "_Acosh_e#"},
	{"acoshf", "_Acoshf_e#"},
	{"acoshl", "_Acoshl_e#"},
	{"acoshw", "_Acoshw_e#"},
	{"acosl", "_Acosl_e#"},
	{"acosw", "_Acosw_e#"},
	{"asin", "_Asin_e#"},
	{"asind", "_Asind_e#"},
	{"asindf", "_Asindf_e#"},
	{"asindl", "_Asindl_e#"},
	{"asindw", "_Asindw_e#"},
	{"asinf", "_Asinf_e#"},
	{"asinl", "_Asinl_e#"},
	{"asinw", "_Asinw_e#"},
	{"atanh", "_Atanh_e#"},
	{"atanhf", "_Atanhf_e#"},
	{"atanhl", "_Atanhl_e#"},
	{"atanhw", "_Atanhw_e#"},
	{"cosh", "_Cosh_e#"},
	{"coshf", "_Coshf_e#"},
	{"coshl", "_Coshl_e#"},
	{"coshw", "_Coshw_e#"},
	{"exp2", "_Exp2_e#"},
	{"exp2f", "_Exp2f_e#"},
	{"exp2l", "_Exp2l_e#"},
	{"exp2w", "_Exp2w_e#"},
	{"exp", "_Exp_e#"},
	{"expf", "_Expf_e#"},
	{"expl", "_Expl_e#"},
	{"expm1", "_Expm1_e#"},
	{"expm1f", "_Expm1f_e#"},
	{"expm1l", "_Expm1l_e#"},
	{"expm1w", "_Expm1w_e#"},
	{"expw", "_Expw_e#"},
	{"fmod", "_Fmod_e#"},
	{"fmodf", "_Fmodf_e#"},
	{"fmodl", "_Fmodl_e#"},
	{"fmodw", "_Fmodw_e#"},
	{"gamma", "_Gamma_e#"},
	{"gammaf", "_Gammaf_e#"},
	{"gammal", "_Gammal_e#"},
	{"gammaw", "_Gammaw_e#"},
	{"ldexp", "_Ldexp_e#"},
	{"ldexpf", "_Ldexpf_e#"},
	{"ldexpl", "_Ldexpl_e#"},
	{"ldexpw", "_Ldexpw_e#"},
	{"lgamma", "_Lgamma_e#"},
	{"lgammaf", "_Lgammaf_e#"},
	{"lgammal", "_Lgammal_e#"},
	{"lgammaw", "_Lgammaw_e#"},
	{"log10", "_Log10_e#"},
	{"log10f", "_Log10f_e#"},
	{"log10l", "_Log10l_e#"},
	{"log10w", "_Log10w_e#"},
	{"log1p", "_Log1p_e#"},
	{"log1pf", "_Log1pf_e#"},
	{"log1pl", "_Log1pl_e#"},
	{"log1pw", "_Log1pw_e#"},
	{"log2", "_Log2_e#"},
	{"log2f", "_Log2f_e#"},
	{"log2l", "_Log2l_e#"},
	{"log2w", "_Log2w_e#"},
	{"log", "_Log_e#"},
	{"logb", "_Logb_e#"},
	{"logbf", "_Logbf_e#"},
	{"logbl", "_Logbl_e#"},
	{"logbw", "_Logbw_e#"},
	{"logf", "_Logf_e#"},
	{"logl", "_Logl_e#"},
	{"logw", "_Logw_e#"},
	{"nextafter", "_Nextafter_e#"},
	{"nextafterf", "_Nextafterf_e#"},
	{"nextafterl", "_Nextafterl_e#"},
	{"nextafterw", "_Nextafterw_e#"},
	{"pow", "_Pow_e#"},
	{"powf", "_Powf_e#"},
	{"powl", "_Powl_e#"},
	{"poww", "_Poww_e#"},
	{"remainder", "_Remainder_e#"},
	{"remainderf", "_Remainderf_e#"},
	{"remainderl", "_Remainderl_e#"},
	{"remainderw", "_Remainderw_e#"},
	{"scalb", "_Scalb_e#"},
	{"scalbf", "_Scalbf_e#"},
	{"scalbl", "_Scalbl_e#"},
	{"scalbw", "_Scalbw_e#"},
	{"sinh", "_Sinh_e#"},
	{"sinhf", "_Sinhf_e#"},
	{"sinhl", "_Sinhl_e#"},
	{"sinhw", "_Sinhw_e#"},
	{"sqrt", "_Sqrt_e#"},
	{"sqrtf", "_Sqrtf_e#"},
	{"sqrtl", "_Sqrtl_e#"},
	{"sqrtw", "_Sqrtw_e#"},
	{"tgamma", "_Tgamma_e#"},
	{"tgammaf", "_Tgammaf_e#"},
	{"tgammal", "_Tgammal_e#"},
	{"tgammaw", "_Tgammaw_e#"}
};

static void
ia64_hpux_add_pragma_builtin (tree func)
{
  size_t i;

  if (!flag_isoc94 && flag_iso)
    {
	for (i = 0; i < ARRAY_SIZE (c89_mathlib_name_list); i++)
	  {
	    if (!strcmp(c89_mathlib_name_list[i].realname,
			IDENTIFIER_POINTER (func)))
	      {
		add_to_renaming_pragma_list(func,
			get_identifier(c89_mathlib_name_list[i].c89name));
	      }
	  }
    }
}
