#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main ()
{
struct base {
  int basic;
}; 

struct derived { 
  struct base common;
  char extra;
};

struct base *bp;

bp = (struct base *) malloc (sizeof (struct derived));

bp->basic = 10;
((struct derived *)bp)->extra = 'x';
return 0;
}
