#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#define PAGE_SIZE 4096
#define NUM_PAGES 25000   /* Adjust this to increase the mapping size, e.g. 25000 pages ~ 100MB */

int main(void)
{
    size_t size = NUM_PAGES * PAGE_SIZE;
    char *addr;
    unsigned long iteration = 0;

    /* Allocate a large anonymous mapping */
    addr = mmap(NULL, size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (addr == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Initialize the memory so that pages are faulted in */
    for (size_t i = 0; i < size; i += PAGE_SIZE)
        addr[i] = 'x';

    /* Print the PID so you can manually check /proc/<pid>/... */
    printf("Test program PID: %d\n", getpid());
    printf("Mapping size: %zu bytes (%zu pages)\n", size, NUM_PAGES);
    fflush(stdout);

    /* Infinite loop: repeatedly drop pages and access them */
    while (1) {
        /* Drop the mapping's pages so that next access will trigger swap-in faults */
        if (madvise(addr, size, MADV_DONTNEED) < 0) {
            fprintf(stderr, "madvise failed: %s\n", strerror(errno));
            break;
        }
        /* Access each page to trigger faults */
        volatile uint8_t sum = 0;
        for (size_t i = 0; i < size; i += PAGE_SIZE) {
            sum += addr[i];
        }
        /* Optionally print progress every 100 iterations */
        if (++iteration % 100 == 0) {
            printf("Iteration: %lu\n", iteration);
            fflush(stdout);
        }
        /* Small sleep to prevent hogging the CPU too much */
        usleep(10000);  /* 10 ms */
    }

    if (munmap(addr, size) < 0) {
        perror("munmap");
        exit(EXIT_FAILURE);
    }
    return 0;
}
