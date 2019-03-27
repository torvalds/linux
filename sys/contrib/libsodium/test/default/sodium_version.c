
#define TEST_NAME "sodium_version"
#include "cmptest.h"

int
main(void)
{
    printf("%d\n", sodium_version_string() != NULL);
    printf("%d\n", sodium_library_version_major() > 0);
    printf("%d\n", sodium_library_version_minor() >= 0);
#ifdef SODIUM_LIBRARY_MINIMAL
    assert(sodium_library_minimal() == 1);
#else
    assert(sodium_library_minimal() == 0);
#endif

    return 0;
}
