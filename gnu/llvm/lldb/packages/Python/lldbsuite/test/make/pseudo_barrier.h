#include <atomic>

// Note that although hogging the CPU while waiting for a variable to change
// would be terrible in production code, it's great for testing since it avoids
// a lot of messy context switching to get multiple threads synchronized.

typedef std::atomic<int> pseudo_barrier_t;

#define pseudo_barrier_wait(barrier)        \
    do                                      \
    {                                       \
        --(barrier);                        \
        while ((barrier).load() > 0)        \
            ;                               \
    } while (0)

#define pseudo_barrier_init(barrier, count) \
    do                                      \
    {                                       \
        (barrier) = (count);                \
    } while (0)
