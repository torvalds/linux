#include <iostream>


extern char k [];

class foo
{
 public:
  foo (char *m) { m [40] = 20; }
};


foo f1 (k);
foo f2 (k);
foo f3 (k);

int main ()
{
  return 0;
}
