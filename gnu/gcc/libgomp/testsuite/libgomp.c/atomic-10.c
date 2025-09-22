/* { dg-do run } */
/* { dg-options "-O2 -fopenmp" } */

extern void abort (void);
int x1, x2, x3, x4, x5;
volatile int y6 = 9, y2, y3, y4, y5;
volatile unsigned char z1, z2, z3, z4, z5;
float a1, a2, a3, a4;

void
f1 (void)
{
  #pragma omp atomic
    x1++;
  #pragma omp atomic
    x2--;
  #pragma omp atomic
    ++x3;
  #pragma omp atomic
    --x4;
  #pragma omp atomic
    x5 += 1;
  #pragma omp atomic
    x1 -= y6;
  #pragma omp atomic
    x2 |= 1;
  #pragma omp atomic
    x3 &= 1;
  #pragma omp atomic
    x4 ^= 1;
  #pragma omp atomic
    x5 *= 3;
  #pragma omp atomic
    x1 /= 3;
  #pragma omp atomic
    x2 /= 3;
  #pragma omp atomic
    x3 <<= 3;
  #pragma omp atomic
    x4 >>= 3;
}

void
f2 (void)
{
  #pragma omp atomic
    y6++;
  #pragma omp atomic
    y2--;
  #pragma omp atomic
    ++y3;
  #pragma omp atomic
    --y4;
  #pragma omp atomic
    y5 += 1;
  #pragma omp atomic
    y6 -= x1;
  #pragma omp atomic
    y2 |= 1;
  #pragma omp atomic
    y3 &= 1;
  #pragma omp atomic
    y4 ^= 1;
  #pragma omp atomic
    y5 *= 3;
  #pragma omp atomic
    y6 /= 3;
  #pragma omp atomic
    y2 /= 3;
  #pragma omp atomic
    y3 <<= 3;
  #pragma omp atomic
    y4 >>= 3;
}

void
f3 (void)
{
  #pragma omp atomic
    z1++;
  #pragma omp atomic
    z2--;
  #pragma omp atomic
    ++z3;
  #pragma omp atomic
    --z4;
  #pragma omp atomic
    z5 += 1;
  #pragma omp atomic
    z1 |= 1;
  #pragma omp atomic
    z2 &= 1;
  #pragma omp atomic
    z3 ^= 1;
  #pragma omp atomic
    z4 *= 3;
  #pragma omp atomic
    z5 /= 3;
  #pragma omp atomic
    z1 /= 3;
  #pragma omp atomic
    z2 <<= 3;
  #pragma omp atomic
    z3 >>= 3;
}

void
f4 (void)
{
  #pragma omp atomic
    a1 += 8.0;
  #pragma omp atomic
    a2 *= 3.5;
  #pragma omp atomic
    a3 -= a1 + a2;
  #pragma omp atomic
    a4 /= 2.0;
}

int
main (void)
{
  f1 ();
  if (x1 != -2 || x2 != 0 || x3 != 8 || x4 != -1 || x5 != 3)
    abort ();
  f2 ();
  if (y6 != 4 || y2 != 0 || y3 != 8 || y4 != -1 || y5 != 3)
    abort ();
  f3 ();
  if (z1 != 0 || z2 != 8 || z3 != 0 || z4 != 253 || z5 != 0)
    abort ();
  a1 = 7;
  a2 = 10;
  a3 = 11;
  a4 = 13;
  f4 ();
  if (a1 != 15.0 || a2 != 35.0 || a3 != -39.0 || a4 != 6.5)
    abort ();
  return 0;
}
