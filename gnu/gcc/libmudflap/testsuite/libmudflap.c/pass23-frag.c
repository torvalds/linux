#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
struct foo {
  int part1: 8;
  int nothing : 1;
  int part2 : 5;
  int lots_more_nothing : 3;
  int some_padding; /* for 64-bit hosts */
  float some_more_nothing;
  double yet_more_nothing;
};

#define offsetof(TYPE, MEMBER)	((size_t) &((TYPE *) 0)->MEMBER)

struct foo* q = (struct foo *) malloc (offsetof (struct foo, some_more_nothing));
q->nothing = 1; /* touch q */ 
/* The RHS of the following expression is meant to trigger a
   fold-const.c mapping the expression to a BIT_FIELD_REF.  It glues
   together the accesses to the two non-neighbouring bitfields into a
   single bigger boolean test. */
q->lots_more_nothing = (q->part1 == 13 && q->part2 == 7);
free (q);


return 0;
}
