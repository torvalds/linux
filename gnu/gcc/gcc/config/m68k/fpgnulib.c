/* This is a stripped down version of floatlib.c.  It supplies only those
   functions which exist in libgcc, but for which there is not assembly
   language versions in m68k/lb1sf68.asm.

   It also includes simplistic support for extended floats (by working in
   double precision).  You must compile this file again with -DEXTFLOAT
   to get this support.  */

/*
** gnulib support for software floating point.
** Copyright (C) 1991 by Pipeline Associates, Inc.  All rights reserved.
** Permission is granted to do *anything* you want with this file,
** commercial or otherwise, provided this message remains intact.  So there!
** I would appreciate receiving any updates/patches/changes that anyone
** makes, and am willing to be the repository for said changes (am I
** making a big mistake?).
**
** Pat Wood
** Pipeline Associates, Inc.
** pipeline!phw@motown.com or
** sun!pipeline!phw or
** uunet!motown!pipeline!phw
**
** 05/01/91 -- V1.0 -- first release to gcc mailing lists
** 05/04/91 -- V1.1 -- added float and double prototypes and return values
**                  -- fixed problems with adding and subtracting zero
**                  -- fixed rounding in truncdfsf2
**                  -- fixed SWAP define and tested on 386
*/

/*
** The following are routines that replace the gnulib soft floating point
** routines that are called automatically when -msoft-float is selected.
** The support single and double precision IEEE format, with provisions
** for byte-swapped machines (tested on 386).  Some of the double-precision
** routines work at full precision, but most of the hard ones simply punt
** and call the single precision routines, producing a loss of accuracy.
** long long support is not assumed or included.
** Overall accuracy is close to IEEE (actually 68882) for single-precision
** arithmetic.  I think there may still be a 1 in 1000 chance of a bit
** being rounded the wrong way during a multiply.  I'm not fussy enough to
** bother with it, but if anyone is, knock yourself out.
**
** Efficiency has only been addressed where it was obvious that something
** would make a big difference.  Anyone who wants to do this right for
** best speed should go in and rewrite in assembler.
**
** I have tested this only on a 68030 workstation and 386/ix integrated
** in with -msoft-float.
*/

/* the following deal with IEEE single-precision numbers */
#define EXCESS		126L
#define SIGNBIT		0x80000000L
#define HIDDEN		(1L << 23L)
#define SIGN(fp)	((fp) & SIGNBIT)
#define EXP(fp)		(((fp) >> 23L) & 0xFF)
#define MANT(fp)	(((fp) & 0x7FFFFFL) | HIDDEN)
#define PACK(s,e,m)	((s) | ((e) << 23L) | (m))

/* the following deal with IEEE double-precision numbers */
#define EXCESSD		1022L
#define HIDDEND		(1L << 20L)
#define EXPDBITS	11
#define EXPDMASK	0x7FFL
#define EXPD(fp)	(((fp.l.upper) >> 20L) & 0x7FFL)
#define SIGND(fp)	((fp.l.upper) & SIGNBIT)
#define MANTD(fp)	(((((fp.l.upper) & 0xFFFFF) | HIDDEND) << 10) | \
				(fp.l.lower >> 22))
#define MANTDMASK	0xFFFFFL /* mask of upper part */

/* the following deal with IEEE extended-precision numbers */
#define EXCESSX		16382L
#define HIDDENX		(1L << 31L)
#define EXPXBITS	15
#define EXPXMASK	0x7FFF
#define EXPX(fp)	(((fp.l.upper) >> 16) & EXPXMASK)
#define SIGNX(fp)	((fp.l.upper) & SIGNBIT)
#define MANTXMASK	0x7FFFFFFFL /* mask of upper part */

union double_long 
{
  double d;
  struct {
      long upper;
      unsigned long lower;
    } l;
};

union float_long {
  float f;
  long l;
};

union long_double_long
{
  long double ld;
  struct
    {
      long upper;
      unsigned long middle;
      unsigned long lower;
    } l;
};

#ifndef EXTFLOAT

int
__unordsf2(float a, float b)
{
  union float_long fl;

  fl.f = a;
  if (EXP(fl.l) == EXP(~0u) && (MANT(fl.l) & ~HIDDEN) != 0)
    return 1;
  fl.f = b;
  if (EXP(fl.l) == EXP(~0u) && (MANT(fl.l) & ~HIDDEN) != 0)
    return 1;
  return 0;
}

int
__unorddf2(double a, double b)
{
  union double_long dl;

  dl.d = a;
  if (EXPD(dl) == EXPDMASK
      && ((dl.l.upper & MANTDMASK) != 0 || dl.l.lower != 0))
    return 1;
  dl.d = b;
  if (EXPD(dl) == EXPDMASK
      && ((dl.l.upper & MANTDMASK) != 0 || dl.l.lower != 0))
    return 1;
  return 0;
}

/* convert unsigned int to double */
double
__floatunsidf (unsigned long a1)
{
  long exp = 32 + EXCESSD;
  union double_long dl;

  if (!a1)
    {
      dl.l.upper = dl.l.lower = 0;
      return dl.d;
    }

  while (a1 < 0x2000000L)
    {
      a1 <<= 4;
      exp -= 4;
    }

  while (a1 < 0x80000000L)
    {
      a1 <<= 1;
      exp--;
    }

  /* pack up and go home */
  dl.l.upper = exp << 20L;
  dl.l.upper |= (a1 >> 11L) & ~HIDDEND;
  dl.l.lower = a1 << 21L;

  return dl.d;
}

/* convert int to double */
double
__floatsidf (long a1)
{
  long sign = 0, exp = 31 + EXCESSD;
  union double_long dl;

  if (!a1)
    {
      dl.l.upper = dl.l.lower = 0;
      return dl.d;
    }

  if (a1 < 0)
    {
      sign = SIGNBIT;
      a1 = (long)-(unsigned long)a1;
      if (a1 < 0)
	{
	  dl.l.upper = SIGNBIT | ((32 + EXCESSD) << 20L);
	  dl.l.lower = 0;
	  return dl.d;
        }
    }

  while (a1 < 0x1000000L)
    {
      a1 <<= 4;
      exp -= 4;
    }

  while (a1 < 0x40000000L)
    {
      a1 <<= 1;
      exp--;
    }

  /* pack up and go home */
  dl.l.upper = sign;
  dl.l.upper |= exp << 20L;
  dl.l.upper |= (a1 >> 10L) & ~HIDDEND;
  dl.l.lower = a1 << 22L;

  return dl.d;
}

/* convert unsigned int to float */
float
__floatunsisf (unsigned long l)
{
  double foo = __floatunsidf (l);
  return foo;
}

/* convert int to float */
float
__floatsisf (long l)
{
  double foo = __floatsidf (l);
  return foo;
}

/* convert float to double */
double
__extendsfdf2 (float a1)
{
  register union float_long fl1;
  register union double_long dl;
  register long exp;
  register long mant;

  fl1.f = a1;

  dl.l.upper = SIGN (fl1.l);
  if ((fl1.l & ~SIGNBIT) == 0)
    {
      dl.l.lower = 0;
      return dl.d;
    }

  exp = EXP(fl1.l);
  mant = MANT (fl1.l) & ~HIDDEN;
  if (exp == 0)
    {
      /* Denormal.  */
      exp = 1;
      while (!(mant & HIDDEN))
	{
	  mant <<= 1;
	  exp--;
	}
      mant &= ~HIDDEN;
    }
  exp = exp - EXCESS + EXCESSD;
  dl.l.upper |= exp << 20;
  dl.l.upper |= mant >> 3;
  dl.l.lower = mant << 29;
	
  return dl.d;
}

/* convert double to float */
float
__truncdfsf2 (double a1)
{
  register long exp;
  register long mant;
  register union float_long fl;
  register union double_long dl1;

  dl1.d = a1;

  if ((dl1.l.upper & ~SIGNBIT) == 0 && !dl1.l.lower)
    {
      fl.l = SIGND(dl1);
      return fl.f;
    }

  exp = EXPD (dl1) - EXCESSD + EXCESS;

  /* shift double mantissa 6 bits so we can round */
  mant = MANTD (dl1) >> 6;

  /* Check for underflow and denormals.  */
  if (exp <= 0)
    {
      if (exp < -24)
	mant = 0;
      else
	mant >>= 1 - exp;
      exp = 0;
    }
  
  /* now round and shift down */
  mant += 1;
  mant >>= 1;

  /* did the round overflow? */
  if (mant & 0xFF000000L)
    {
      mant >>= 1;
      exp++;
    }

  mant &= ~HIDDEN;

  /* pack up and go home */
  fl.l = PACK (SIGND (dl1), exp, mant);
  return (fl.f);
}

/* convert double to int */
long
__fixdfsi (double a1)
{
  register union double_long dl1;
  register long exp;
  register long l;

  dl1.d = a1;

  if (!dl1.l.upper && !dl1.l.lower) 
    return 0;

  exp = EXPD (dl1) - EXCESSD - 31;
  l = MANTD (dl1);

  if (exp > 0) 
    {
      /* Return largest integer.  */
      return SIGND (dl1) ? 0x80000000L : 0x7fffffffL;
    }

  if (exp <= -32)
    return 0;

  /* shift down until exp = 0 */
  if (exp < 0)
    l >>= -exp;

  return (SIGND (dl1) ? -l : l);
}

/* convert float to int */
long
__fixsfsi (float a1)
{
  double foo = a1;
  return __fixdfsi (foo);
}

#else /* EXTFLOAT */

/* Primitive extended precision floating point support.

   We assume all numbers are normalized, don't do any rounding, etc.  */

/* Prototypes for the above in case we use them.  */
double __floatunsidf (unsigned long);
double __floatsidf (long);
float __floatsisf (long);
double __extendsfdf2 (float);
float __truncdfsf2 (double);
long __fixdfsi (double);
long __fixsfsi (float);

int
__unordxf2(long double a, long double b)
{
  union long_double_long ldl;

  ldl.ld = a;
  if (EXPX(ldl) == EXPXMASK
      && ((ldl.l.middle & MANTXMASK) != 0 || ldl.l.lower != 0))
    return 1;
  ldl.ld = b;
  if (EXPX(ldl) == EXPXMASK
      && ((ldl.l.middle & MANTXMASK) != 0 || ldl.l.lower != 0))
    return 1;
  return 0;
}

/* convert double to long double */
long double
__extenddfxf2 (double d)
{
  register union double_long dl;
  register union long_double_long ldl;
  register long exp;

  dl.d = d;
  /*printf ("dfxf in: %g\n", d);*/

  ldl.l.upper = SIGND (dl);
  if ((dl.l.upper & ~SIGNBIT) == 0 && !dl.l.lower)
    {
      ldl.l.middle = 0;
      ldl.l.lower = 0;
      return ldl.ld;
    }

  exp = EXPD (dl) - EXCESSD + EXCESSX;
  ldl.l.upper |= exp << 16;
  ldl.l.middle = HIDDENX;
  /* 31-20: # mantissa bits in ldl.l.middle - # mantissa bits in dl.l.upper */
  ldl.l.middle |= (dl.l.upper & MANTDMASK) << (31 - 20);
  /* 1+20: explicit-integer-bit + # mantissa bits in dl.l.upper */
  ldl.l.middle |= dl.l.lower >> (1 + 20);
  /* 32 - 21: # bits of dl.l.lower in ldl.l.middle */
  ldl.l.lower = dl.l.lower << (32 - 21);

  /*printf ("dfxf out: %s\n", dumpxf (ldl.ld));*/
  return ldl.ld;
}

/* convert long double to double */
double
__truncxfdf2 (long double ld)
{
  register long exp;
  register union double_long dl;
  register union long_double_long ldl;

  ldl.ld = ld;
  /*printf ("xfdf in: %s\n", dumpxf (ld));*/

  dl.l.upper = SIGNX (ldl);
  if ((ldl.l.upper & ~SIGNBIT) == 0 && !ldl.l.middle && !ldl.l.lower)
    {
      dl.l.lower = 0;
      return dl.d;
    }

  exp = EXPX (ldl) - EXCESSX + EXCESSD;
  /* ??? quick and dirty: keep `exp' sane */
  if (exp >= EXPDMASK)
    exp = EXPDMASK - 1;
  dl.l.upper |= exp << (32 - (EXPDBITS + 1));
  /* +1-1: add one for sign bit, but take one off for explicit-integer-bit */
  dl.l.upper |= (ldl.l.middle & MANTXMASK) >> (EXPDBITS + 1 - 1);
  dl.l.lower = (ldl.l.middle & MANTXMASK) << (32 - (EXPDBITS + 1 - 1));
  dl.l.lower |= ldl.l.lower >> (EXPDBITS + 1 - 1);

  /*printf ("xfdf out: %g\n", dl.d);*/
  return dl.d;
}

/* convert a float to a long double */
long double
__extendsfxf2 (float f)
{
  long double foo = __extenddfxf2 (__extendsfdf2 (f));
  return foo;
}

/* convert a long double to a float */
float
__truncxfsf2 (long double ld)
{
  float foo = __truncdfsf2 (__truncxfdf2 (ld));
  return foo;
}

/* convert an int to a long double */
long double
__floatsixf (long l)
{
  double foo = __floatsidf (l);
  return foo;
}

/* convert an unsigned int to a long double */
long double
__floatunsixf (unsigned long l)
{
  double foo = __floatunsidf (l);
  return foo;
}

/* convert a long double to an int */
long
__fixxfsi (long double ld)
{
  long foo = __fixdfsi ((double) ld);
  return foo;
}

/* The remaining provide crude math support by working in double precision.  */

long double
__addxf3 (long double x1, long double x2)
{
  return (double) x1 + (double) x2;
}

long double
__subxf3 (long double x1, long double x2)
{
  return (double) x1 - (double) x2;
}

long double
__mulxf3 (long double x1, long double x2)
{
  return (double) x1 * (double) x2;
}

long double
__divxf3 (long double x1, long double x2)
{
  return (double) x1 / (double) x2;
}

long double
__negxf2 (long double x1)
{
  return - (double) x1;
}

long
__cmpxf2 (long double x1, long double x2)
{
  return __cmpdf2 ((double) x1, (double) x2);
}

long
__eqxf2 (long double x1, long double x2)
{
  return __cmpdf2 ((double) x1, (double) x2);
}

long
__nexf2 (long double x1, long double x2)
{
  return __cmpdf2 ((double) x1, (double) x2);
}

long
__ltxf2 (long double x1, long double x2)
{
  return __cmpdf2 ((double) x1, (double) x2);
}

long
__lexf2 (long double x1, long double x2)
{
  return __cmpdf2 ((double) x1, (double) x2);
}

long
__gtxf2 (long double x1, long double x2)
{
  return __cmpdf2 ((double) x1, (double) x2);
}

long
__gexf2 (long double x1, long double x2)
{
  return __cmpdf2 ((double) x1, (double) x2);
}

#endif /* EXTFLOAT */
