#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <unistd.h>

void print_separator() {
    printf("------------------------------------------------\n");
}

void print_os_info() {
    struct utsname uname_data;
    if (uname(&uname_data) != -1) {
        printf("OS: %s %s %s\n", uname_data.sysname, uname_data.release, uname_data.machine);
    } else {
        printf("OS: Unknown\n");
    }
}

void print_uptime() {
    FILE *uptime_file = fopen("/proc/uptime", "r");
    if (uptime_file != NULL) {
        double uptime_seconds;
        fscanf(uptime_file, "%lf", &uptime_seconds);
        fclose(uptime_file);

        int hours = (int)(uptime_seconds / 3600);
        int minutes = (int)((uptime_seconds - hours * 3600) / 60);

        printf("Uptime: %d hours %d minutes\n", hours, minutes);
    } else {
        printf("Uptime: Unknown\n");
    }
}

void print_cpu_info() {
    FILE *cpu_info = fopen("/proc/cpuinfo", "r");
    if (cpu_info != NULL) {
        char line[256];
        int core_count = 0;
        while (fgets(line, sizeof(line), cpu_info)) {
            if (strstr(line, "model name")) {
                char *token = strtok(line, ":");
                token = strtok(NULL, ":");
                printf("CPU: %s", token);
            }
            if (strstr(line, "cpu cores")) {
                char *token = strtok(line, ":");
                token = strtok(NULL, ":");
                core_count = atoi(token);
            }
        }
        fclose(cpu_info);
        printf("Cores: %d\n", core_count);
    } else {
        printf("CPU: Unknown\n");
    }
}

void print_memory_info() {
    long pages = sysconf(_SC_PHYS_PAGES);
    long page_size = sysconf(_SC_PAGE_SIZE);
    long total_memory = pages * page_size;
    printf("Memory: %ld MB\n", total_memory / (1024 * 1024));
}

void print_gpu_info() {
    FILE *gpu_info = popen("lspci | grep -i 'vga\\|3d\\|2d'", "r");
    if (gpu_info != NULL) {
        char line[256];
        if (fgets(line, sizeof(line), gpu_info)) {
            printf("GPU: %s", line);
        } else {
            printf("GPU: Unknown\n");
        }
        pclose(gpu_info);
    } else {
        printf("GPU: Unknown\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "--shell") == 0) {
        printf("#!/usr/bin/env sh\n\n");
        printf("set -e\n\n");
        printf("mkdir -p build/\n");
        printf("cd build/\n\n");
        
        // Add the rest of the shell script here
        printf("if [ -z \"${CMAKE_BUILD_TYPE}\" ]; then\n");
        printf("    CMAKE_BUILD_TYPE=Release\n");
        printf("fi\n");
        printf("cmake \"-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}\" ..\n\n");
        
        // ... More shell script
        
        printf("./gromfetch \"$@\"\n");
        return 0;
    }
    
    print_separator();
    print_os_info();
    print_uptime();
    print_cpu_info();
    print_memory_info();
    print_gpu_info();
    print_separator();

    return 0;
}
