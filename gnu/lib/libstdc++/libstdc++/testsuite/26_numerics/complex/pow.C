// PR libbstdc++/10689
// Origin: Daniel.Levine@jhuaph.edu

#include <complex>
#include <testsuite_hooks.h>

int main()
{
   std::complex<double> z(0, 1) ;

   VERIFY(pow(z, 1.0/3.0) == 0.0);

   return 0;
}
