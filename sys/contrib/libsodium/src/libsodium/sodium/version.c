
#include "version.h"

const char *
sodium_version_string(void)
{
    return SODIUM_VERSION_STRING;
}

int
sodium_library_version_major(void)
{
    return SODIUM_LIBRARY_VERSION_MAJOR;
}

int
sodium_library_version_minor(void)
{
    return SODIUM_LIBRARY_VERSION_MINOR;
}

int
sodium_library_minimal(void)
{
#ifdef SODIUM_LIBRARY_MINIMAL
    return 1;
#else
    return 0;
#endif
}
