typedef unsigned int USItype		__attribute__ ((mode (SI)));

USItype
__mulsi3 (USItype a, USItype b)
{
  USItype c = 0;

  while (a != 0)
    {
      if (a & 1)
	c += b;
      a >>= 1;
      b <<= 1;
    }

  return c;
}
