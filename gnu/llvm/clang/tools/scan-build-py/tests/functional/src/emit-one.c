#include <assert.h>

int div(int numerator, int denominator)
{
    return numerator / denominator;
}

void div_test()
{
    int i = 0;
    for (i = 0; i < 2; ++i)
        assert(div(2 * i, i) == 2);
}

int do_nothing()
{
    unsigned int i = 0;

    int k = 100;
    int j = k + 1;

    return j;
}
