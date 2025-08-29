# Resctrl PMU Integration: Reference Management and Race Prevention

## Overview

This document describes the implementation of safe reference management when integrating the resctrl monitoring subsystem with the perf PMU. The solution addresses race conditions that can occur when file descriptors for resctrl monitoring files are passed to the PMU while the underlying kernfs nodes are being removed.

## The Race Condition Problem

### Scenario
1. User opens a resctrl monitoring file (e.g., `/sys/fs/resctrl/group1/mon_data/mon_L3_00/llc_occupancy`)
2. User passes the file descriptor to perf via `perf_event_open()`
3. **Race window**: Administrator removes the rdtgroup (e.g., `rmdir /sys/fs/resctrl/group1`)
4. PMU attempts to access rdtgroup data through the file descriptor
5. **Potential issues**: Access to freed memory, NULL pointer dereference, inconsistent state

### Root Cause
- PMU needs access to `struct rdtgroup` for monitoring operations
- Rdtgroup can be deleted while file descriptors remain open
- Without proper synchronization, PMU may access invalid rdtgroup data

## Solution: rdtgroup_mutex Protection

### Key Discovery
Resctrl already uses a global mutex (`rdtgroup_mutex`) that provides perfect protection for this use case:

```c
// From fs/resctrl/rdtgroup.c
static int rdtgroup_rmdir(struct kernfs_node *kn)
{
    rdtgrp = rdtgroup_kn_lock_live(kn);  // Takes rdtgroup_mutex
    // ... removal operations under mutex protection
}

struct rdtgroup *rdtgroup_kn_lock_live(struct kernfs_node *kn)
{
    // ...
    mutex_lock(&rdtgroup_mutex);  // CRITICAL PROTECTION
    // ...
}
```

### Protection Mechanism
1. **Node removal holds rdtgroup_mutex**: When an rdtgroup is being removed, the entire operation (including kernfs drain and release callbacks) happens under `rdtgroup_mutex`
2. **PMU access under same mutex**: PMU code holds the same mutex when accessing `of->priv` to get the rdtgroup reference
3. **Mutual exclusion guaranteed**: These operations cannot happen concurrently

## Implementation Details

### Data Structures

#### Updated PMU Event Structure
```c
struct resctrl_pmu_event {
    char *mon_path;                    // Keep for logging context
    struct rdtgroup *rdtgrp;           // Protected rdtgroup reference
};
```

#### Reference Counting Flow
1. **File Open**: `rdtgroup_mondata_open()` stores rdtgroup in `of->priv`
2. **PMU Init**: `get_rdtgroup_from_fd()` takes additional reference under mutex protection
3. **File Release**: `rdtgroup_mondata_release()` clears `of->priv` under mutex protection
4. **PMU Cleanup**: Releases the additional reference taken during init

### Core Functions

#### Protected rdtgroup Access
```c
static struct rdtgroup *get_rdtgroup_from_fd(int fd)
{
    struct file *file;
    struct kernfs_open_file *of;
    struct rdtgroup *rdtgrp;
    
    file = fget(fd);
    // ... validation ...
    
    of = kernfs_of(file);
    
    // CRITICAL: Same mutex that protects node removal
    mutex_lock(&rdtgroup_mutex);
    
    rdtgrp = of->priv;
    if (!rdtgrp || (rdtgrp->flags & RDT_DELETED)) {
        mutex_unlock(&rdtgroup_mutex);
        fput(file);
        return ERR_PTR(-ENOENT);
    }
    
    // Take reference while protected
    rdtgroup_kn_get(rdtgrp, of->kn);
    
    mutex_unlock(&rdtgroup_mutex);
    fput(file);
    return rdtgrp;
}
```

#### Reference Management in PMU
```c
// PMU event initialization
static int resctrl_event_init(struct perf_event *event)
{
    // ... validation ...
    
    rdtgrp = get_rdtgroup_from_fd(fd);  // Takes protected reference
    if (IS_ERR(rdtgrp))
        return PTR_ERR(rdtgrp);
    
    // Store reference in PMU event data
    resctrl_event->rdtgrp = rdtgrp;
    
    // ... logging ...
    return 0;
}

// PMU event cleanup
static void resctrl_event_cleanup(struct perf_event *event)
{
    if (resctrl_event->rdtgrp) {
        // ... logging ...
        rdtgroup_kn_put(rdtgrp, rdtgrp->kn);  // Release reference
    }
    // ... cleanup ...
}
```

## Race Condition Analysis

### Timeline with Protection
1. **T1**: User opens monitoring file → `rdtgroup_mondata_open()` sets `of->priv = rdtgrp`
2. **T2**: User calls `perf_event_open()` → PMU calls `get_rdtgroup_from_fd()`
3. **T3**: Admin calls `rmdir` → `rdtgroup_rmdir()` attempts to acquire `rdtgroup_mutex`
4. **T4**: PMU acquires `rdtgroup_mutex` first → safely accesses `of->priv` → takes reference
5. **T5**: PMU releases `rdtgroup_mutex`
6. **T6**: `rmdir` acquires `rdtgroup_mutex` → drains files → calls release callback
7. **T7**: Release callback sets `of->priv = NULL` (safe - PMU already has its reference)

### Key Protection Points
- **Mutual exclusion**: Steps T4-T5 and T6-T7 cannot overlap
- **Reference safety**: PMU gets its own reference before release callback runs
- **Memory safety**: rdtgroup memory persists until PMU releases its reference
- **State consistency**: No access to invalid or partially-removed rdtgroup data

## Error Handling

### Validation Checks
1. **File descriptor validation**: Ensure fd refers to valid kernfs file
2. **NULL pointer check**: Handle case where `of->priv` was cleared by release
3. **Deletion flag check**: Reject rdtgroups marked with `RDT_DELETED`
4. **Reference acquisition**: Ensure rdtgroup reference is successfully taken

### Error Scenarios
- **File drained before PMU access**: `of->priv == NULL` → return `-ENOENT`
- **rdtgroup marked for deletion**: `rdtgrp->flags & RDT_DELETED` → return `-ENOENT`
- **Invalid file descriptor**: Not a kernfs monitoring file → return `-EINVAL`

## Debugging and Monitoring

### Comprehensive Logging
PMU operations log detailed rdtgroup state for debugging:

```
resctrl_pmu: PMU event initialized: fd=5, path=/sys/fs/resctrl/group1/mon_data/mon_L3_00/llc_occupancy
resctrl_pmu:   rdtgroup: closid=1, rmid=2, waitcount=2
resctrl_pmu:   type=CTRL, mode=0, flags=0x0
resctrl_pmu:   cpu_mask=0-3

resctrl_pmu: PMU event cleanup: path=/sys/fs/resctrl/group1/mon_data/mon_L3_00/llc_occupancy
resctrl_pmu:   rdtgroup: closid=1, rmid=2, waitcount=1
resctrl_pmu:   type=CTRL, mode=0, flags=0x1
resctrl_pmu:   cpu_mask=0-3
```

### Monitored Fields
- **closid**: Class of Service ID for cache allocation
- **rmid**: Resource Monitoring ID for performance monitoring
- **waitcount**: Reference count (should decrease after PMU cleanup)
- **type**: RDTCTRL_GROUP (control) vs RDTMON_GROUP (monitor-only)
- **mode**: Resource sharing mode (shareable, exclusive, pseudo-lock)
- **flags**: Status flags including RDT_DELETED
- **cpu_mask**: CPUs assigned to this rdtgroup

## Benefits of This Approach

### Simplicity
- **No new locking primitives**: Reuses existing resctrl infrastructure
- **No complex reference counting**: Leverages existing rdtgroup reference mechanisms
- **Minimal code changes**: Focused on the specific race condition

### Robustness
- **Proven locking pattern**: Uses the same mutex resctrl uses internally
- **Comprehensive protection**: Covers all rdtgroup access scenarios
- **Graceful degradation**: Operations fail cleanly rather than crashing

### Maintainability
- **Clear ownership**: Well-defined reference lifetimes
- **Extensive logging**: Rich debugging information for troubleshooting
- **Standard patterns**: Follows established kernfs and resctrl conventions

## Future Considerations

### Scalability
- Current implementation uses global mutex - acceptable for current use cases
- Future optimizations could use per-rdtgroup locking if needed

### Extended Integration
- Foundation supports additional PMU event types
- Pattern can be applied to other subsystems needing resctrl integration
- Monitoring data structures remain accessible for advanced PMU operations

## Related Documentation

- `resctrl-pmu-reference-management.md`: Original reference management design
- `kernfs-file-handling.md`: Kernfs file lifecycle and reference management
- Linux kernel resctrl documentation: `Documentation/filesystems/resctrl.rst`
- Perf subsystem documentation: `tools/perf/Documentation/`

## Summary

The rdtgroup_mutex protection approach provides a robust, simple solution to the race condition between PMU access and resctrl node removal. By leveraging existing resctrl locking infrastructure, the implementation ensures:

1. **Memory safety**: No access to freed rdtgroup structures
2. **State consistency**: No access to partially-removed rdtgroups  
3. **Reference correctness**: Proper reference counting prevents premature cleanup
4. **Error resilience**: Graceful handling of all race condition scenarios

This design provides a solid foundation for safe resctrl-PMU integration while maintaining the simplicity and reliability expected in kernel subsystems.