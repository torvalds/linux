#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int foo (int *u, int i)
{
   return u[i];  /* this dereference should not be instrumented */
}

int main ()
{
  int *k = malloc (6);
 int l = foo (k, 8);
 int boo [8];
 int m = boo [l % 2 + 12]; /* should not be instrumented */
 return m & strlen (""); /* a fancy way of saying "0" */
}
/* { dg-options "-fmudflap -fmudflapir -lmudflap -Wall" } */
