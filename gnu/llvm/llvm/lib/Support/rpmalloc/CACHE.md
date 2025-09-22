# Thread caches
rpmalloc has a thread cache of free memory blocks which can be used in allocations without interfering with other threads or going to system to map more memory, as well as a global cache shared by all threads to let spans of memory pages flow between threads. Configuring the size of these caches can be crucial to obtaining good performance while minimizing memory overhead blowup. Below is a simple case study using the benchmark tool to compare different thread cache configurations for rpmalloc.

The rpmalloc thread cache is configured to be unlimited, performance oriented as meaning default values, size oriented where both thread cache and global cache is reduced significantly, or disabled where both thread and global caches are disabled and completely free pages are directly unmapped.

The benchmark is configured to run threads allocating 150000 blocks distributed in the `[16, 16000]` bytes range with a linear falloff probability. It runs 1000 loops, and every iteration 75000 blocks (50%) are freed and allocated in a scattered pattern. There are no cross thread allocations/deallocations. Parameters: `benchmark n 0 0 0 1000 150000 75000 16 16000`. The benchmarks are run on an Ubuntu 16.10 machine with 8 cores (4 physical, HT) and 12GiB RAM.

The benchmark also includes results for the standard library malloc implementation as a reference for comparison with the nocache setting.

![Ubuntu 16.10 random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=387883204&format=image)
![Ubuntu 16.10 random [16, 16000] bytes, 8 cores](https://docs.google.com/spreadsheets/d/1NWNuar1z0uPCB5iVS_Cs6hSo2xPkTmZf0KsgWS_Fb_4/pubchart?oid=1644710241&format=image)

For single threaded case the unlimited cache and performance oriented cache settings have identical performance and memory overhead, indicating that the memory pages fit in the combined thread and global cache. As number of threads increase to 2-4 threads, the performance settings have slightly higher performance which can seem odd at first, but can be explained by low contention on the global cache where some memory pages can flow between threads without stalling, reducing the overall number of calls to map new memory pages (also indicated by the slightly lower memory overhead). 

As threads increase even more to 5-10 threads, the increased contention and eventual limit of global cache cause the unlimited setting to gain a slight advantage in performance. As expected the memory overhead remains constant for unlimited caches, while going down for performance setting when number of threads increases.

The size oriented setting maintain good performance compared to the standard library while reducing the memory overhead compared to the performance setting with a decent amount.

The nocache setting still outperforms the reference standard library allocator for workloads up to 6 threads while maintaining a near zero memory overhead, which is even slightly lower than the standard library. For use case scenarios where number of allocation of each size class is lower the overhead in rpmalloc from the 64KiB span size will of course increase.
