# A Guide to Linux `resctrl` Subsystem Internals

The Resource Control (`resctrl`) subsystem is a Linux kernel feature, primarily for x86 platforms, that provides a user-space interface for managing and monitoring CPU resources. It exposes Intel's Resource Director Technology (RDT) and AMD's Platform Quality of Service (PQOS) features.

These technologies allow for:
*   **Allocation**: Partitioning resources like Last Level Cache (LLC) and memory bandwidth among groups of tasks. This is known as Cache Allocation Technology (CAT) and Memory Bandwidth Allocation (MBA).
*   **Monitoring**: Observing the usage of these resources by groups of tasks. This is known as Cache Monitoring Technology (CMT) and Memory Bandwidth Monitoring (MBM).

The user interface is a dedicated filesystem, typically mounted at `/sys/fs/resctrl`. This guide explains the key components and workflows within the subsystem.

## 1. Responsibilities of `fs/resctrl`

This directory contains the Virtual File System (VFS) layer that connects the user-space view (directories and files) to the underlying `resctrl` kernel logic. It is responsible for handling all file operations on the `resctrl` mount point.

*   **`fs/resctrl/fs.c`**: This is the entry point for the filesystem itself.
    *   It defines the `resctrl_fs_type` which is registered with the kernel using `register_filesystem()`.
    *   It implements the mount (`resctrl_mount`) and unmount (`resctrl_kill_sb`) operations. When you run `mount -t resctrl resctrl /sys/fs/resctrl`, the `resctrl_mount` function is called.
    *   It's responsible for setting up the root directory of the filesystem, including the default resource group and the `info` subdirectory.

*   **`fs/resctrl/rdtgroup.c`**: This is the heart of the user-space interface. It manages the resource group directories and the files within them.
    *   **Directory Operations**: It implements the `mkdir` and `rmdir` syscall handlers for the `resctrl` fs. When a user runs `mkdir /sys/fs/resctrl/my_group`, the **`rdtgroup_mkdir()`** function is called. This is the crucial function that creates a new resource control group.
    *   **File Operations**: It defines the file operations (`read`, `write`, `open`, etc.) for the control files within each group directory like `tasks`, `cpus`/`cpu_mask`, `schemata`, `mon_data`, and `mon_groups`.

*   **`fs/resctrl/internal.h`**: A private header file containing function prototypes and structure definitions used only within the `fs/resctrl` directory.

## 2. What each file in `arch/x86/kernel/cpu/resctrl` does

This directory contains the architecture-specific (x86) implementation that interacts directly with the hardware (via Model-Specific Registers or MSRs) to apply the policies and perform monitoring configured through the filesystem interface.

*   **`arch/x86/kernel/cpu/resctrl/core.c`**: This is the core logic hub.
    *   **Initialization**: **`rdt_init()`** is the main entry point, called during kernel boot to detect RDT features and initialize the subsystem.
    *   **Group Management**: It manages the lifecycle of `struct rdt_group`, the kernel's representation of a resource group. It allocates and frees CLOSIDs (Class of Service ID) and RMIDs (Resource Monitoring ID), which are the hardware identifiers for groups.
    *   **Task Association**: It contains the scheduler hooks that apply resource controls to a task. The key function is **`__resctrl_sched_in()`**, which is called on a context switch to program the CPU with the task's group IDs.

*   **`arch/x86/kernel/cpu/resctrl/rdt.c`**: This file handles resource and feature detection.
    *   It reads CPUID leaves to discover which RDT/PQOS features (CAT, MBA, CMT, MBM, etc.) are available on the CPU.
    *   It populates the `struct rdt_resource` array, which describes each available resource (e.g., L3 cache). This information is exposed to user space in the `/sys/fs/resctrl/info` directory.

*   **`arch/x86/kernel/cpu/resctrl/ctrl.c`**: Manages the "control" or "allocation" features (CAT and MBA).
    *   It contains the logic for parsing the `schemata` file content written by the user.
    *   It validates user-provided cache masks (for CAT) or bandwidth values (for MBA).
    *   It programs the hardware MSRs (e.g., `IA32_L3_CBM_BASE`) with these values, associating a `CLOSID` with a specific resource policy.

*   **`arch/x86/kernel/cpu/resctrl/mon.c`**: Manages the "monitoring" features (CMT and MBM).
    *   This is a key file for your goal of adding perf-based monitoring. It handles reading the hardware monitoring counters.
    *   When user space reads from a `mon_data` file, the VFS layer calls into functions in this file.
    *   It reads the `IA32_QM_CTR` MSR, which contains the occupancy or bandwidth usage data associated with a specific `RMID`.

*   **`arch/x86/kernel/cpu/resctrl/pseudo_lock.c`**: Implements the Pseudo-Locking feature, a special allocation feature where a region of the cache is "locked" for exclusive use by a resource group.

*   **`arch/x86/kernel/cpu/resctrl/internal.h`**: A private header file for the architecture-specific implementation.

## 3. What Happens When We `mkdir` a New Resctrl Group?

Creating a new directory in the `resctrl` filesystem is the fundamental way to create a new resource control group. Here is the step-by-step flow:

1.  **User-space Action**: A user or script executes `mkdir /sys/fs/resctrl/my_group`.
2.  **VFS Layer**: The kernel's VFS receives the `mkdir` syscall and routes it to the `mkdir` inode operation defined in `resctrl_fs_type`.
3.  **Filesystem Handler**: This call is routed to **`rdtgroup_mkdir()`** in `fs/resctrl/rdtgroup.c`.
4.  **Group Allocation**: Inside `rdtgroup_mkdir()`, a new instance of `struct rdt_group` is allocated. This structure holds all the information for the new group.
    ```c
    // In fs/resctrl/rdtgroup.c
    static int rdtgroup_mkdir(struct kernfs_node *parent, const char *name, umode_t mode)
    {
        struct rdt_group *rdt_parent_group, *rdtgrp;
        // ...
        rdtgrp = rdtgroup_kn_alloc_init(parent, name); // Allocate and init rdt_group
        // ...
    }
    ```
5.  **CLOSID/RMID Assignment**: The core logic in `arch/x86/kernel/cpu/resctrl/core.c` is called to find a free `CLOSID` and `RMID` to assign to this new group.
6.  **File Creation**: `rdtgroup_mkdir()` then creates the standard set of control files (`tasks`, `schemata`, etc.) within the new directory using the `kernfs` API.
7.  **Group Registration**: The new `rdt_group` is added to a global list (`rdt_all_groups`), making it visible to the rest of the `resctrl` subsystem.
8.  **Hardware Update**: The `schemata` file is pre-populated with the default (full) cache mask, and the core logic programs the corresponding `IA32_L3_CBM_BASE + closid` MSR with this default mask.

At this point, the group exists but contains no tasks.

## 4. How the Subsystem Tracks Tasks and Enforces Policies

Associating a task with a group and enforcing its policy is a two-step process: assignment and enforcement.

### Assignment

1.  **User-space Action**: A user writes a task ID (TID) to the `tasks` file: `echo <TID> > /sys/fs/resctrl/my_group/tasks`.
2.  **Filesystem Handler**: The `write()` syscall is handled by **`rdtgroup_tasks_write()`** in `fs/resctrl/rdtgroup.c`.
3.  **Task Lookup**: This function parses the TID and finds the corresponding `struct task_struct`.
4.  **Update `task_struct`**: The key step happens here. The `closid` and `rmid` from the target `rdt_group` are written directly into the task's `task_struct`. This is handled by **`resctrl_move_task()`** in `arch/x86/kernel/cpu/resctrl/core.c`.
    ```c
    // In include/linux/sched.h
    struct task_struct {
        // ...
    #ifdef CONFIG_RESCTRL
        u32				closid;
        u32				rmid;
    #endif
        // ...
    };
    ```
    ```c
    // In arch/x86/kernel/cpu/resctrl/core.c
    int resctrl_move_task(int pid, struct rdt_group *rdtgrp)
    {
        // ... find task_struct *p from pid ...
        WRITE_ONCE(p->closid, rdtgrp->closid);
        WRITE_ONCE(p->rmid, rdtgrp->mon.rmid);
        // ...
    }
    ```

### Enforcement (The Scheduler Hook)

The assignment only flags the task. The policy is enforced every time the task gets to run on a CPU core.

1.  **Context Switch**: When the Linux scheduler decides to run our task, it performs a context switch.
2.  **Resctrl Hook**: As part of the context switch path, the scheduler calls **`__resctrl_sched_in()`**, defined in `arch/x86/kernel/cpu/resctrl/core.c`.
3.  **Program the MSR**: `__resctrl_sched_in()` reads the `closid` and `rmid` from the `task_struct` of the task that is about to run. It then writes these values to the per-CPU `IA32_PQR_ASSOC` (PQR) MSR.
    ```c
    // In arch/x86/kernel/cpu/resctrl/core.c
    void __resctrl_sched_in(struct task_struct *tsk)
    {
        u64 pqr_val;
    
        // If the task's closid is the same as what's already in the MSR,
        // we can skip the MSR write for performance.
        if (tsk->closid == this_cpu_read(pqr_state.cur_closid))
            return;
    
        pqr_val = resctrl_to_pqr(tsk->closid, tsk->rmid);
        wrmsrl(MSR_IA32_PQR_ASSOC, pqr_val);
    
        // Cache the current value to avoid redundant MSR writes.
        this_cpu_write(pqr_state.cur_closid, tsk->closid);
        this_cpu_write(pqr_state.cur_rmid, tsk->rmid);
    }
    ```
This MSR write is the final step. It tells the CPU hardware, "The code that is about to execute belongs to this Class of Service and should be monitored with this Resource Monitoring ID." The CPU then automatically enforces the associated cache partitions and updates the correct monitoring counters.