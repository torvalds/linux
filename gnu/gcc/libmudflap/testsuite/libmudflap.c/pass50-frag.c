#include <stdlib.h>

struct a
{
  int a1[5];
  union
  {
    int b1[5];
    struct
    {
      int c1;
      int c2;
    } b2[4];
  } a2[8];
};

int i1 = 5;
int i2 = 2;
int i3 = 6;
int i4 = 0;

int
main ()
{
  volatile struct a *k = calloc (1, sizeof (struct a));
  k->a2[i1].b1[i2] = k->a2[i3].b2[i4].c2;
  free ((void *) k);
  return 0;
}
