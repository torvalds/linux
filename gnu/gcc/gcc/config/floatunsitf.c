/* Public domain.  */
typedef int SItype __attribute__ ((mode (SI)));
typedef unsigned int USItype __attribute__ ((mode (SI)));
typedef float TFtype __attribute__ ((mode (TF)));

TFtype
__floatunsitf (USItype u)
{
  SItype s = (SItype) u;
  TFtype r = (TFtype) s;
  if (s < 0)
    r += (TFtype)2.0 * (TFtype) ((USItype) 1
				 << (sizeof (USItype) * __CHAR_BIT__ - 1));
  return r;
}
