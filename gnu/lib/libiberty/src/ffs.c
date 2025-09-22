/* ffs -- Find the first bit set in the parameter

@deftypefn Supplemental int ffs (int @var{valu})

Find the first (least significant) bit set in @var{valu}.  Bits are
numbered from right to left, starting with bit 1 (corresponding to the
value 1).  If @var{valu} is zero, zero is returned.

@end deftypefn

*/

int
ffs (register int valu)
{
  register int bit;

  if (valu == 0)
    return 0;

  for (bit = 1; !(valu & 1); bit++)
  	valu >>= 1;

  return bit;
}

