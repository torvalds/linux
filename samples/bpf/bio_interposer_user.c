// SPDX-License-Identifier: GPL-2.0
/* User-space loader for bio_interposer BPF program
 *
 * Copyright (c) 2023 Your Name <your.email@example.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>
#include <linux/bpf.h>
#include <sys/syscall.h>

#define bpf_map_lookup_elem(fd, key, value) \
        syscall(__NR_bpf, BPF_MAP_LOOKUP_ELEM, fd, key, value, 0)

#define bpf_map_get_next_key(fd, key, next_key) \
        syscall(__NR_bpf, BPF_MAP_GET_NEXT_KEY, fd, key, next_key, 0)

static volatile bool exiting = false;

/* Handle termination signals for graceful shutdown */
static void sig_handler(int sig)
{
    exiting = true;
}

/* Print usage information */
static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [OPTIONS]\n", prog);
    fprintf(stderr, "  -h         Show this help\n");
    fprintf(stderr, "  -t SECONDS Run for SECONDS seconds (default: until Ctrl-C)\n");
    fprintf(stderr, "  -d DEV_ID  Target device ID (default: value in BPF code)\n");
}

/* Print I/O statistics from the BPF maps */
static void print_stats(int map_fd)
{
    unsigned int key, next_key;
    unsigned long long value;
    const char *op_names[] = {
        [0] = "READ",
        [1] = "WRITE",
        [2] = "FLUSH",
        [3] = "DISCARD",
        [4] = "SECURE_ERASE",
        [5] = "ZONE_RESET",
        [6] = "ZONE_OPEN",
        [7] = "ZONE_CLOSE",
        [8] = "ZONE_FINISH",
        [9] = "ZONE_APPEND",
        [10] = "WRITE_SAME",
        [11] = "WRITE_ZEROES",
        [12] = "ZONE_RESET_ALL",
    };
    
    printf("\nI/O Statistics:\n");
    printf("%-15s %s\n", "Operation", "Count");
    printf("---------------------------\n");
    
    key = 0;
    while (bpf_map_get_next_key(map_fd, &key, &next_key) == 0) {
        if (bpf_map_lookup_elem(map_fd, &next_key, &value) == 0) {
            const char *op_name = "UNKNOWN";
            if (next_key < sizeof(op_names)/sizeof(op_names[0]) && op_names[next_key])
                op_name = op_names[next_key];
            
            printf("%-15s %llu\n", op_name, value);
        }
        key = next_key;
    }
}

int main(int argc, char **argv)
{
    int opt, run_time = 0;
    int device_id = 0;
    
    /* Parse command line arguments */
    while ((opt = getopt(argc, argv, "ht:d:")) != -1) {
        switch (opt) {
        case 'h':
            usage(argv[0]);
            return 0;
        case 't':
            run_time = atoi(optarg);
            if (run_time <= 0) {
                fprintf(stderr, "Invalid run time\n");
                return 1;
            }
            break;
        case 'd':
            device_id = atoi(optarg);
            break;
        default:
            usage(argv[0]);
            return 1;
        }
    }
    
    /* Set up signal handling */
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);
    
    printf("BPF program demonstration - kernel side not yet implemented\n");
    printf("This is a sample to show the user space component\n");
    
    /* Run for specified time or until interrupted */
    int elapsed = 0;
    while (!exiting && (run_time == 0 || elapsed < run_time)) {
        sleep(1);
        elapsed++;
        
        /* Print a status indicator */
        printf(".");
        fflush(stdout);
        
        /* Print stats every 10 seconds */
        if (elapsed % 10 == 0) {
            printf("\nSimulated stats at %d seconds\n", elapsed);
        }
    }
    
    printf("\nSample program completed\n");
    
    return 0;
} 