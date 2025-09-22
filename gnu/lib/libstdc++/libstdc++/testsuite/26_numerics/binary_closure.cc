// 19990805 gdr
//
// XXX: to impove later.
// Origin: Andreas Amann <amann@physik.tu-berlin.de>
// CXXFLAGS: -g

#include <iostream>
#include <valarray>


int main()
{
    std::valarray<double> a(10), b(10), c(10), d(10);

    a = 1.2;
    b = 3.1;

    c = 4.0;

    d = ( 2.0 * b + a );  // works
    std::cout << "d[4] = " << d[4] << std::endl;

    d = (a * 2.0 + b ); // works
    std::cout << "d[4] = " << d[4] << std::endl;

    d = (a + b * 2.0 ); // segfaults!
    std::cout << "d[4] = " << d[4] << std::endl;
    d = (a + 2.0* b );

    std::cout << "d[4] = " << d[4] << std::endl;
    d = (a + 2.0* b );
    std::cout << "d[4] = " << d[4] << std::endl;
    d = (a + 2.0* b );

    std::cout << "d[4] = " << d[4] << std::endl;
    return 0;
}
