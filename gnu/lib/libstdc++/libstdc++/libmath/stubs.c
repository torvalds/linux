/* Stub definitions for libmath subpart of libstdc++. */

/* Copyright (C) 2001, 2002, 2003 Free Software Foundation, Inc.

   This file is part of the GNU ISO C++ Library.  This library is free
   software; you can redistribute it and/or modify it under the
   terms of the GNU General Public License as published by the
   Free Software Foundation; either version 2, or (at your option)
   any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this library; see the file COPYING.  If not, write to the Free
   Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
   USA.

   As a special exception, you may use this file as part of a free software
   library without restriction.  Specifically, if other files instantiate
   templates or use macros or inline functions from this file, or you compile
   this file and link it with other files to produce an executable, this
   file does not by itself cause the resulting executable to be covered by
   the GNU General Public License.  This exception does not however
   invalidate any other reasons why the executable file might be covered by
   the GNU General Public License.  */

#include <math.h>
#include "config.h"

/* For targets which do not have support for long double versions,
   we use the crude approximation.  We'll do better later.  */


#ifndef HAVE_ATAN2F
float
atan2f(float x, float y)
{
  return (float) atan2(x, y);
}
#endif

#ifndef HAVE_ATAN2L
long double
atan2l(long double x, long double y)
{
  return atan2((double) x, (double) y);
}
#endif


#ifndef HAVE_COSF
float
cosf(float x)
{
  return (float) cos(x);
}
#endif

#ifndef HAVE_COSL
long double
cosl(long double x)
{
  return cos((double) x);
}
#endif


#ifndef HAVE_COSHF
float
coshf(float x)
{
  return (float) cosh(x);
}
#endif

#ifndef HAVE_COSHL
long double
coshl(long double x)
{
  return cosh((double) x);
}
#endif


#ifndef HAVE_EXPF
float
expf(float x)
{
  return (float) exp(x);
}
#endif

#ifndef HAVE_EXPL
long double
expl(long double x)
{
  return exp((double) x);
}
#endif


/* Compute the hypothenuse of a right triangle with side x and y.  */
#ifndef HAVE_HYPOTF
float
hypotf(float x, float y)
{
  float s = fabsf(x) + fabsf(y);
  if (s == 0.0F)
    return s;
  x /= s; y /= s;
  return s * sqrtf(x * x + y * y);
}
#endif

#ifndef HAVE_HYPOT
double
hypot(double x, double y)
{
  double s = fabs(x) + fabs(y);
  if (s == 0.0)
    return s;
  x /= s; y /= s;
  return s * sqrt(x * x + y * y);
}
#endif

#ifndef HAVE_HYPOTL
long double
hypotl(long double x, long double y)
{
  long double s = fabsl(x) + fabsl(y);
  if (s == 0.0L)
    return s;
  x /= s; y /= s;
  return s * sqrtl(x * x + y * y);
}
#endif



#ifndef HAVE_LOGF
float
logf(float x)
{
  return (float) log(x);
}
#endif

#ifndef HAVE_LOGL
long double
logl(long double x)
{
  return log((double) x);
}
#endif


#ifndef HAVE_LOG10F
float
log10f(float x)
{
  return (float) log10(x);
}
#endif

#ifndef HAVE_LOG10L
long double
log10l(long double x)
{
  return log10((double) x);
}
#endif


#ifndef HAVE_POWF
float
powf(float x, float y)
{
  return (float) pow(x, y);
}
#endif

#ifndef HAVE_POWL
long double
powl(long double x, long double y)
{
  return pow((double) x, (double) y);
}
#endif


#ifndef HAVE_SINF
float
sinf(float x)
{
  return (float) sin(x);
}
#endif

#ifndef HAVE_SINL
long double
sinl(long double x)
{
  return sin((double) x);
}
#endif


#ifndef HAVE_SINHF
float
sinhf(float x)
{
  return (float) sinh(x);
}
#endif

#ifndef HAVE_SINHL
long double
sinhl(long double x)
{
  return sinh((double) x);
}
#endif


#ifndef HAVE_SQRTF
float
sqrtf(float x)
{
  return (float) sqrt(x);
}
#endif

#ifndef HAVE_SQRTL
long double
sqrtl(long double x)
{
  return  sqrt((double) x);
}
#endif


#ifndef HAVE_TANF
float
tanf(float x)
{
  return (float) tan(x);
}
#endif

#ifndef HAVE_TANL
long double
tanl(long double x)
{
  return tan((double) x);
}
#endif


#ifndef HAVE_TANHF
float
tanhf(float x)
{
  return (float) tanh(x);
}
#endif

#ifndef HAVE_TANHL
long double
tanhl(long double x)
{
  return tanh((double) x);
}
#endif
