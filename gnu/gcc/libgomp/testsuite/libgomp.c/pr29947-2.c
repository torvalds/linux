/* PR libgomp/29947 */
/* { dg-options "-O2 -fopenmp" } */
/* { dg-do run } */

extern void abort (void);

int cnt;

void
test1 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel reduction (+:e,c)
  {
#pragma omp for schedule (static)
    for (i = j1; i <= k1; ++i)
      {
	if (i < j2 || i > k2)
	  ++e;
	++c;
      }
#pragma omp atomic
    ++cnt;
  }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test2 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel reduction (+:e,c)
  {
#pragma omp for schedule (static)
    for (i = k1; i >= j1; --i)
      {
	if (i < j2 || i > k2)
	  ++e;
	++c;
      }
#pragma omp atomic
    ++cnt;
  }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test3 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel reduction (+:e,c)
  {
#pragma omp for schedule (static, 1)
    for (i = j1; i <= k1; ++i)
      {
	if (i < j2 || i > k2)
	  ++e;
	++c;
      }
#pragma omp atomic
    ++cnt;
  }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test4 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel reduction (+:e,c)
  {
#pragma omp for schedule (static, 1)
    for (i = k1; i >= j1; --i)
      {
	if (i < j2 || i > k2)
	  ++e;
	++c;
      }
#pragma omp atomic
    ++cnt;
  }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test5 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel reduction (+:e,c)
  {
#pragma omp for schedule (static) ordered
    for (i = j1; i <= k1; ++i)
      {
	if (i < j2 || i > k2)
	  ++e;
#pragma omp ordered
	++c;
      }
#pragma omp atomic
    ++cnt;
  }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test6 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel reduction (+:e,c)
  {
#pragma omp for schedule (static) ordered
    for (i = k1; i >= j1; --i)
      {
	if (i < j2 || i > k2)
	  ++e;
#pragma omp ordered
	++c;
      }
#pragma omp atomic
    ++cnt;
  }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test7 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel reduction (+:e,c)
  {
#pragma omp for schedule (static, 1) ordered
    for (i = j1; i <= k1; ++i)
      {
	if (i < j2 || i > k2)
	  ++e;
#pragma omp ordered
	++c;
      }
#pragma omp atomic
    ++cnt;
  }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test8 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel reduction (+:e,c)
  {
#pragma omp for schedule (static, 1) ordered
    for (i = k1; i >= j1; --i)
      {
	if (i < j2 || i > k2)
	  ++e;
#pragma omp ordered
	++c;
      }
#pragma omp atomic
    ++cnt;
  }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test9 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel for reduction (+:e,c) schedule (static)
  for (i = j1; i <= k1; ++i)
    {
      if (i < j2 || i > k2)
	++e;
      ++c;
    }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test10 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel for reduction (+:e,c) schedule (static)
  for (i = k1; i >= j1; --i)
    {
      if (i < j2 || i > k2)
	++e;
      ++c;
    }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test11 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel for reduction (+:e,c) schedule (static, 1)
  for (i = j1; i <= k1; ++i)
    {
      if (i < j2 || i > k2)
	++e;
      ++c;
    }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test12 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel for reduction (+:e,c) schedule (static, 1)
  for (i = k1; i >= j1; --i)
    {
      if (i < j2 || i > k2)
	++e;
      ++c;
    }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test13 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel for reduction (+:e,c) schedule (static) ordered
  for (i = j1; i <= k1; ++i)
    {
      if (i < j2 || i > k2)
	++e;
#pragma omp ordered
      ++c;
    }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test14 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel for reduction (+:e,c) schedule (static) ordered
  for (i = k1; i >= j1; --i)
    {
      if (i < j2 || i > k2)
	++e;
#pragma omp ordered
      ++c;
    }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test15 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel for reduction (+:e,c) schedule (static, 1) ordered
  for (i = j1; i <= k1; ++i)
    {
      if (i < j2 || i > k2)
	++e;
#pragma omp ordered
      ++c;
    }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

void
test16 (long j1, long k1, long j2, long k2)
{
  long i, e = 0, c = 0;
#pragma omp parallel for reduction (+:e,c) schedule (static, 1) ordered
  for (i = k1; i >= j1; --i)
    {
      if (i < j2 || i > k2)
	++e;
#pragma omp ordered
      ++c;
    }
  if (e || (c != j2 > k2 ? 0 : k2 - j2 + 1))
    abort ();
}

int
__attribute__((noinline))
test (long j1, long k1, long j2, long k2)
{
  test1 (j1, k1, j2, k2);
  test2 (j1, k1, j2, k2);
  test3 (j1, k1, j2, k2);
  test4 (j1, k1, j2, k2);
  test5 (j1, k1, j2, k2);
  test6 (j1, k1, j2, k2);
  test7 (j1, k1, j2, k2);
  test8 (j1, k1, j2, k2);
  test9 (j1, k1, j2, k2);
  test10 (j1, k1, j2, k2);
  test11 (j1, k1, j2, k2);
  test12 (j1, k1, j2, k2);
  test13 (j1, k1, j2, k2);
  test14 (j1, k1, j2, k2);
  test15 (j1, k1, j2, k2);
  test16 (j1, k1, j2, k2);
  return cnt;
}

int
main (void)
{
  test (1, 5, 1, 5);
  test (5, 5, 5, 5);
  test (5, 4, 5, 4);
  test (5, 1, 5, 1);
  return 0;
}
