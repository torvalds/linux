int foo1 ()
{
  union { int l; char c[sizeof (int)]; } k1;
  char *m;
  k1.l = 0;
  /* This test variant triggers ADDR_EXPR of k explicitly in order to
     ensure it's registered with the runtime.  */
  m = k1.c;
  k1.c [sizeof (int)-1] = m[sizeof (int)-2];
}

int foo2 ()
{
  union { int l; char c[sizeof (int)]; } k2;
  k2.l = 0;
  /* Since this access is known-in-range, k need not be registered
     with the runtime, but then this access better not be instrumented
     either.  */
  k2.c [sizeof (int)-1] ++;
  return k2.l;
}

int foo3idx = sizeof (int)-1;

int foo3 ()
{
  union { int l; char c[sizeof (int)]; } k3;
  k3.l = 0;
  /* NB this test uses foo3idx, an extern variable, to defeat mudflap
     known-in-range-index optimizations.  */
  k3.c [foo3idx] ++;
  return k3.l;
}

int main ()
{
  foo1 ();
  foo2 ();
  foo3 ();
  return 0;
}
