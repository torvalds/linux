
#include <stdlib.h>

/* C++Builder defines a "random" macro */
#undef random

#ifdef __native_client__
# define memset(dst, c, n) xmemset(dst, c, n)

static void *
xmemset(void *dst, int c, size_t n)
{
    unsigned char *     dst_ = (unsigned char *) dst;
    const unsigned char c_   = (unsigned char) c;
    size_t              i;

    for (i = 0; i < n; i++) {
        dst_[i] = c_;
    }
    return dst;
}
#endif

#ifdef __EMSCRIPTEN__
# define strcmp(s1, s2) xstrcmp(s1, s2)

static int
strcmp(const char *s1, const char *s2)
{
    while (*s1 == *s2++) {
        if (*s1++ == 0) {
            return 0;
        }
    }
    return *(unsigned char *) s1 - *(unsigned char *) --s2;
}
#endif

#ifdef _WIN32
static void
srandom(unsigned seed)
{
    srand(seed);
}

static long
random(void)
{
    return (long) rand();
}
#endif
