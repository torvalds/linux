/* Public domain.  */
typedef int SItype __attribute__ ((mode (SI)));
typedef unsigned int USItype __attribute__ ((mode (SI)));
typedef float DFtype __attribute__ ((mode (DF)));

DFtype
__floatunsidf (USItype u)
{
  SItype s = (SItype) u;
  DFtype r = (DFtype) s;
  if (s < 0)
    r += (DFtype)2.0 * (DFtype) ((USItype) 1
				 << (sizeof (USItype) * __CHAR_BIT__ - 1));
  return r;
}

#ifdef __ARM_EABI__
__asm__ (".globl\t__aeabi_ui2d\n.set\t__aeabi_ui2d, __floatunsidf\n");
#endif
