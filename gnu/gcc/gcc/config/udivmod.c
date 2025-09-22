long udivmodsi4 ();

long
__udivsi3 (long a, long b)
{
  return udivmodsi4 (a, b, 0);
}

long
__umodsi3 (long a, long b)
{
  return udivmodsi4 (a, b, 1);
}

