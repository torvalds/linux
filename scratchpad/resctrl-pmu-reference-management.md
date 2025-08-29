# Resctrl PMU Reference Management

## Overview

This document describes the implementation of reference management for resctrl monitoring files when file descriptors are passed to the perf PMU subsystem. The goal is to ensure safe access to rdtgroup structures even after the corresponding kernfs nodes are removed.

## Problem Statement

### Background

Resctrl provides monitoring capabilities through files in the filesystem (e.g., `/sys/fs/resctrl/<group>/mon_data/<domain>/<event>`). Users can open these files and pass the file descriptors to the perf subsystem via `perf_event_open()` for hardware performance counter monitoring.

### The Issue

Without proper reference management, the following race condition can occur:

1. User opens a monitoring file (e.g., LLC occupancy for an rdtgroup)
2. User passes the file descriptor to perf PMU
3. The rdtgroup is removed (kernfs node deleted)
4. PMU tries to access rdtgroup data through the file descriptor
5. **Potential crash or memory corruption** if rdtgroup memory is freed

### Current Architecture

- Monitoring files use `kf_mondata_ops` kernfs operations
- Each file's `kn->priv` points to `struct mon_data` containing event metadata
- The associated `rdtgroup` is obtained via `rdtgroup_kn_lock_live(of->kn)` which traverses up to the parent directory
    This requires an active reference on the kernfs_node, which is why we're using the open entry in the ops.
- No additional reference counting protects the rdtgroup when file descriptors are held

## Solution Design

### Reference Management Strategy

We implement kernfs `open` and `release` callbacks for monitoring files to:

1. **During file open**: Take an additional reference on the rdtgroup
2. **During file close/release**: Release the rdtgroup reference
3. **Store the reference**: Use `kernfs_open_file->priv` to store the rdtgroup pointer

### Key Design Principles

1. **Minimal disruption**: Leverage existing rdtgroup reference counting mechanisms
2. **Consistent behavior**: All monitoring files get the same reference management
3. **Graceful degradation**: File operations fail cleanly (`-ENODEV`) after node removal
4. **Memory safety**: rdtgroup memory persists until all file references are released

## Implementation Details

### Data Structure Usage

- `kn->priv`: Contains `struct mon_data` (unchanged)
- `of->priv`: Contains `struct rdtgroup *` (new - set during open)

This allows PMU to access both:
- Event metadata via `kn->priv` 
- Valid rdtgroup reference via `of->priv`

### Function Responsibilities

#### `rdtgroup_mondata_open()`
- Called when a monitoring file is opened
- Obtains rdtgroup via `rdtgroup_kn_lock_live(of->kn)`
- Takes additional reference on rdtgroup
- Stores rdtgroup pointer in `of->priv`
- Returns 0 on success, negative errno on failure

#### `rdtgroup_mondata_release()`
- Called when file is closed or during kernfs node draining
- Retrieves rdtgroup from `of->priv`
- Releases the reference taken during open
- Cleans up `of->priv`

### Integration with Kernfs File Lifecycle

This implementation follows the established kernfs file handling patterns documented in `kernfs-file-handling.md`:

1. **Open files don't prevent node removal** - They don't hold active references
2. **Release callbacks are called exactly once** - Either during drain or file close
3. **Memory safety is maintained** - Node structures persist until all references released
4. **Operations fail gracefully** - Return `-ENODEV` when node is deactivated

## Benefits

### For PMU Integration
- **Safe file descriptor passing**: PMU can safely hold monitoring file descriptors
- **Guaranteed data access**: rdtgroup remains valid for the lifetime of the file descriptor
- **Clean error handling**: Operations fail predictably rather than crashing

### For System Stability
- **No memory corruption**: References prevent premature rdtgroup deallocation
- **Consistent behavior**: All monitoring files behave identically
- **Future-proof**: Other subsystems can safely use monitoring file descriptors

### For Maintainability
- **Minimal code changes**: Leverages existing infrastructure
- **Clear ownership**: File reference management is explicit and documented
- **Standard patterns**: Follows established kernfs conventions

## Usage Example

```c
// User space
int fd = open("/sys/fs/resctrl/group1/mon_data/mon_L3_00/llc_occupancy", O_RDONLY);

// Pass to perf
struct perf_event_attr attr = {
    .type = PERF_TYPE_RESCTRL,  // Hypothetical PMU type
    // ... other fields
};
int perf_fd = perf_event_open(&attr, fd, cpu, group_fd, flags);

// Even if the rdtgroup is removed here, the PMU can still safely access
// the rdtgroup via the file descriptor until it's closed
```

## Kernel Implementation Flow

1. **File Open**: `rdtgroup_mondata_open()` takes rdtgroup reference
2. **PMU Access**: PMU uses `of->priv` to access valid rdtgroup
3. **Node Removal**: kernfs calls release callback, but rdtgroup memory persists
4. **File Close**: Final reference released, rdtgroup can be freed

## Future Considerations

This reference management system provides a foundation for:
- Additional PMU integrations with resctrl
- Other subsystems that need persistent access to rdtgroup data
- Enhanced monitoring capabilities that require stable resource group references

## Related Documentation

- `kernfs-file-handling.md`: General kernfs file lifecycle and reference management
- Linux kernel perf subsystem documentation
- Resctrl filesystem documentation in `Documentation/filesystems/resctrl.rst`