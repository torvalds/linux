# Getting struct rdtgroup from File Descriptor: A Guide for Perf Support

## Overview

To add perf support to resctrl, we need a mechanism similar to `cgroup_bpf_prog_attach()` that can convert a file descriptor into a `struct rdtgroup`. This guide explains how to implement this conversion safely, following the same patterns used by the cgroup subsystem.

## Background: How cgroup_bpf_prog_attach Works

The cgroup implementation provides a well-established pattern for fd-to-cgroup conversion:

```c
int cgroup_bpf_prog_attach(const union bpf_attr *attr, ...)
{
    struct cgroup *cgrp;
    
    cgrp = cgroup_get_from_fd(attr->target_fd);  // Key conversion
    if (IS_ERR(cgrp))
        return PTR_ERR(cgrp);
    
    // Use the cgroup...
    
    cgroup_put(cgrp);  // Release reference
    return ret;
}
```

### The cgroup fd-to-struct conversion chain:

1. **`cgroup_get_from_fd(fd)`** → `cgroup_v1v2_get_from_fd(fd)`
2. **`cgroup_v1v2_get_from_fd(fd)`** → Gets file from fd, calls `cgroup_v1v2_get_from_file(file)`
3. **`cgroup_v1v2_get_from_file(file)`** → Calls `css_tryget_online_from_dir(dentry, NULL)`
4. **`css_tryget_online_from_dir(dentry, NULL)`** → Validates filesystem type, extracts cgroup from kernfs

## Implementing rdtgroup_get_from_fd()

Following the cgroup pattern, here's how to implement `rdtgroup_get_from_fd()`:

### 1. Main Function Structure

```c
struct rdtgroup *rdtgroup_get_from_fd(int fd)
{
    CLASS(fd_raw, f)(fd);  // Modern cleanup pattern
    if (fd_empty(f))
        return ERR_PTR(-EBADF);
    return rdtgroup_get_from_file(fd_file(f));
}
```

### 2. File-to-rdtgroup Conversion

```c
static struct rdtgroup *rdtgroup_get_from_file(struct file *f)
{
    struct rdtgroup *rdtgrp;
    
    rdtgrp = rdtgroup_tryget_from_dentry(f->f_path.dentry);
    if (IS_ERR(rdtgrp))
        return rdtgrp;
    
    return rdtgrp;
}
```

### 3. Core Validation and Reference Acquisition

```c
static struct rdtgroup *rdtgroup_tryget_from_dentry(struct dentry *dentry)
{
    struct kernfs_node *kn;
    struct file_system_type *s_type = dentry->d_sb->s_type;
    struct rdtgroup *rdtgrp = NULL;
    
    // Verify it's actually a resctrl filesystem
    if (s_type != &rdt_fs_type)
        return ERR_PTR(-EBADF);
    
    kn = kernfs_node_from_dentry(dentry);
    if (!kn || kernfs_type(kn) != KERNFS_DIR)
        return ERR_PTR(-EBADF);
    
    rcu_read_lock();
    
    // Extract rdtgroup from kernfs node using existing resctrl logic
    rdtgrp = kernfs_to_rdtgroup(kn);
    if (!rdtgrp) {
        rcu_read_unlock();
        return ERR_PTR(-ENOENT);
    }
    
    // Try to acquire a reference - check if group is still live
    if (!rdtgroup_tryget_live(rdtgrp))
        rdtgrp = ERR_PTR(-ENOENT);
    
    rcu_read_unlock();
    return rdtgrp;
}
```

### 4. Reference Counting Functions

We need to implement reference counting similar to what cgroups do:

```c
static bool rdtgroup_tryget_live(struct rdtgroup *rdtgrp)
{
    // Check if group is being deleted
    if (rdtgrp->flags & RDT_DELETED)
        return false;
    
    // Increment reference count atomically
    atomic_inc(&rdtgrp->waitcount);
    
    // Double-check after incrementing (race with deletion)
    if (unlikely(rdtgrp->flags & RDT_DELETED)) {
        atomic_dec(&rdtgrp->waitcount);
        return false;
    }
    
    return true;
}

void rdtgroup_put(struct rdtgroup *rdtgrp)
{
    if (atomic_dec_and_test(&rdtgrp->waitcount)) {
        // If this was the last reference and group is deleted,
        // trigger cleanup (similar to rdtgroup_kn_put logic)
        if (rdtgrp->flags & RDT_DELETED) {
            // Schedule or perform cleanup
            rdtgroup_remove(rdtgrp);
        }
    }
}
```

## Leveraging Existing resctrl Infrastructure

The resctrl subsystem already has the necessary infrastructure:

### kernfs_to_rdtgroup() Function
Located in `fs/resctrl/rdtgroup.c:2365`:

```c
static struct rdtgroup *kernfs_to_rdtgroup(struct kernfs_node *kn)
{
    if (kernfs_type(kn) == KERNFS_DIR) {
        // Resource directories use kn->priv to point to rdtgroup
        if (kn == kn_info || rcu_access_pointer(kn->__parent) == kn_info)
            return NULL;  // info directories don't have rdtgroups
        else
            return kn->priv;  // Direct pointer to rdtgroup
    } else {
        return rdt_kn_parent_priv(kn);  // Files get rdtgroup from parent
    }
}
```

### Filesystem Type Validation
The `rdt_fs_type` is defined in `fs/resctrl/rdtgroup.c:2981` and can be used for validation:

```c
extern struct file_system_type rdt_fs_type;  // Need to export this
```

### Existing Reference Counting
resctrl already uses `rdtgrp->waitcount` for reference counting:
- `rdtgroup_kn_get()` - increments waitcount  
- `rdtgroup_kn_put()` - decrements waitcount
- `rdtgroup_kn_lock_live()` - gets live reference with locking
- `rdtgroup_kn_unlock()` - releases reference with unlocking

## Locking Strategy for Perf Events

Based on the resctrl locking analysis from the research document:

### For Monitoring Groups (Simple Case)
```c
int resctrl_perf_read_monitoring_group(struct rdtgroup *rdtgrp, ...)
{
    // Only need cpus_read_lock for domain list stability
    cpus_read_lock();
    
    // Check if group is still valid
    if (rdtgrp->flags & RDT_DELETED) {
        cpus_read_unlock();
        return -ENOENT;
    }
    
    // Perform the read using existing mon_event_count logic
    // ...
    
    cpus_read_unlock();
    return 0;
}
```

### For Control Groups (Complex Case)
```c
int resctrl_perf_read_control_group(struct rdtgroup *rdtgrp, ...)
{
    // Need both rdtgroup_mutex and cpus_read_lock for control groups
    // because we need to sum monitoring groups under the control group
    
    mutex_lock(&rdtgroup_mutex);
    cpus_read_lock();
    
    if (rdtgrp->flags & RDT_DELETED) {
        cpus_read_unlock();
        mutex_unlock(&rdtgroup_mutex);
        return -ENOENT;
    }
    
    // Sum control group + all its monitoring subgroups
    // ...
    
    cpus_read_unlock();
    mutex_unlock(&rdtgroup_mutex);
    return 0;
}
```

### Lock-Free Read Path for Perf
For high-frequency perf reads, we can optimize by checking the group type:

```c
int resctrl_perf_read(struct rdtgroup *rdtgrp, ...)
{
    if (rdtgrp->type == RDTCTRL_GROUP) {
        return resctrl_perf_read_control_group(rdtgrp, ...);
    } else {
        return resctrl_perf_read_monitoring_group(rdtgrp, ...);
    }
}
```

## Validation Checks

When converting fd to rdtgroup, perform these validations:

### 1. Filesystem Type Check
```c
if (dentry->d_sb->s_type != &rdt_fs_type)
    return ERR_PTR(-EBADF);
```

### 2. Directory Validation  
```c
if (!kn || kernfs_type(kn) != KERNFS_DIR)
    return ERR_PTR(-EBADF);
```

### 3. Info Directory Exclusion
```c
if (kn == kn_info || rcu_access_pointer(kn->__parent) == kn_info)
    return ERR_PTR(-EBADF);  // Can't monitor info directories
```

### 4. Pseudo-Lock Mode Check
```c
if (rdtgrp->mode == RDT_MODE_PSEUDO_LOCKSETUP || 
    rdtgrp->mode == RDT_MODE_PSEUDO_LOCKED)
    return ERR_PTR(-EINVAL);  // Monitoring disabled in pseudo-lock modes
```

### 5. Deletion State Check
```c
if (rdtgrp->flags & RDT_DELETED)
    return ERR_PTR(-ENOENT);  // Group is being deleted
```

## Usage Example in Perf PMU

```c
static int resctrl_pmu_event_init(struct perf_event *event)
{
    struct rdtgroup *rdtgrp;
    struct resctrl_perf_ctx *ctx;
    
    // Get rdtgroup from cgroup fd (using PERF_FLAG_PID_CGROUP pattern)
    rdtgrp = rdtgroup_get_from_fd(event->attr.cgroup_fd);
    if (IS_ERR(rdtgrp))
        return PTR_ERR(rdtgrp);
    
    // Allocate per-event context
    ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx) {
        rdtgroup_put(rdtgrp);
        return -ENOMEM;
    }
    
    ctx->rdtgrp = rdtgrp;  // Store reference
    ctx->evtid = /* extract from event->attr.config */;
    event->pmu_private = ctx;
    
    // Set up cleanup callback
    event->destroy = resctrl_pmu_event_destroy;
    
    return 0;
}

static void resctrl_pmu_event_destroy(struct perf_event *event)
{
    struct resctrl_perf_ctx *ctx = event->pmu_private;
    
    rdtgroup_put(ctx->rdtgrp);  // Release reference
    kfree(ctx);
}
```

## Summary

This approach provides:

1. **Safe Conversion**: Filesystem type validation ensures fd points to resctrl
2. **Proper Reference Counting**: Prevents use-after-free with deleted groups  
3. **Efficient Locking**: Minimal locks for monitoring groups, appropriate locks for control groups
4. **Error Handling**: Clear error codes for various failure modes
5. **Integration**: Leverages existing resctrl infrastructure (kernfs_to_rdtgroup, waitcount, etc.)

The pattern closely follows the proven cgroup approach while adapting to resctrl's specific data structures and locking requirements.