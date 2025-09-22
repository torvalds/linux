#include <clean-one.h>

int do_nothing_loop()
{
    int i = 32;
    int idx = 0;

    for (idx = i; idx > 0; --idx)
    {
        i += idx;
    }
    return i;
}
