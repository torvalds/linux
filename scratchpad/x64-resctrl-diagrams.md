# x86 ResCtrl Architecture Function Flow Diagrams

This document contains mermaid diagrams showing the important function flows in the x86 ResCtrl architecture implementation, focusing on the interaction between the filesystem layer (`fs/resctrl/`) and the architecture-specific layer (`arch/x86/kernel/cpu/resctrl/`).

## 1. Reference Counting Flow - rdtgroup_kn_lock_live

This diagram shows the critical reference counting mechanism that ensures safe access to rdtgroup structures during concurrent operations.

```mermaid
graph TD
    A[rdtgroup_kn_lock_live] --> B[kernfs_to_rdtgroup]
    A --> C[rdtgroup_kn_get]
    A --> D[cpus_read_lock]
    A --> E[mutex_lock rdtgroup_mutex]
    A --> F{flags & RDT_DELETED?}
    F -->|Yes| G[Return NULL]
    F -->|No| H[Return rdtgrp]
    
    C --> I[atomic_inc waitcount]
    C --> J[kernfs_break_active_protection]
    
    K[rdtgroup_kn_unlock] --> L[mutex_unlock rdtgroup_mutex]
    K --> M[cpus_read_unlock]
    K --> N[rdtgroup_kn_put]
    
    N --> O[atomic_dec_and_test waitcount]
    O --> P{waitcount == 0 && RDT_DELETED?}
    P -->|Yes| Q[rdtgroup_pseudo_lock_remove]
    P -->|Yes| R[kernfs_unbreak_active_protection]
    P -->|Yes| S[rdtgroup_remove]
    P -->|No| T[kernfs_unbreak_active_protection]
    
    S --> U[kernfs_put]
    S --> V[kfree rdtgrp]
    
    style A fill:#e1f5fe
    style K fill:#e8f5e8
    style C fill:#fff3e0
    style N fill:#ffebee
    style P fill:#f3e5f5
```

## 2. rmdir Operation Flow

This diagram shows how directory removal operations flow through both the filesystem and architecture layers.

```mermaid
graph TD
    A[rdtgroup_rmdir] --> B[rdtgroup_kn_lock_live]
    B --> C{rdtgrp valid?}
    C -->|No| D[Return -EPERM]
    C -->|Yes| E{Group Type?}
    
    E -->|Control Group| F[rdtgroup_rmdir_ctrl]
    E -->|Monitor Group| G[rdtgroup_rmdir_mon]
    
    F --> H[Free extra groups]
    F --> I[rdt_move_group_tasks]
    F --> J[Set flags = RDT_DELETED]
    F --> K[update_closid_rmid]
    F --> L[rdtgroup_ctrl_remove]
    
    G --> M[rdt_move_group_tasks]
    G --> N[Set flags = RDT_DELETED]
    G --> O[update_closid_rmid]
    G --> P[free_rmid]
    
    I --> Q[Move tasks to parent]
    M --> Q
    
    K --> R[resctrl_arch_sync_cpu_closid_rmid]
    O --> R
    
    R --> S[this_cpu_write pqr_state]
    R --> T[resctrl_arch_sched_in]
    
    L --> U[closid_free]
    L --> V[kernfs_remove]
    
    P --> W[Release monitoring ID]
    
    X[rdtgroup_kn_unlock] --> Y[Cleanup if waitcount == 0]
    
    style A fill:#e1f5fe
    style F fill:#ffebee
    style G fill:#fff3e0
    style R fill:#f3e5f5
    style B fill:#e8f5e8
    style X fill:#e8f5e8
```

## 3. Monitoring Directory Creation Flow

This diagram shows how the monitoring directory structure is created, starting with mkdir_mondata_all().

```mermaid
graph TD
    A[mkdir_mondata_all] --> B[mongroup_create_dir]
    B --> C[Create 'mon_data' directory]
    
    A --> D[for_each_mon_capable_rdt_resource]
    D --> E[mkdir_mondata_subdir_alldom]
    
    E --> F[for each domain in r->mon_domains]
    F --> G[mkdir_mondata_subdir]
    
    G --> H[Create domain directory]
    G --> I{SNC enabled?}
    I -->|No| J
    I -->|Yes| K[Create SNC subdirectories]
    K --> L
    
    J["Directory: mon_[resource]_[domain_id]"] --> M[mon_add_all_files]
    L["Directory: mon_sub_[resource]_[domain_id]"] --> M
    
    M --> N[for each event in r->evt_list]
    N --> O[mon_addfile]
    
    O --> P[Create event file]
    O --> Q[Set kf_mondata_ops]
    O --> R[Store mon_data in kn->priv]
    
    P --> S[Files: llc_occupancy, mbm_total_bytes, mbm_local_bytes]
    
    style A fill:#e1f5fe
    style B fill:#e8f5e8
    style E fill:#fff3e0
    style G fill:#f3e5f5
    style M fill:#f1f8e9
    style O fill:#fce4ec
```

## 4. Monitoring Data Read Flow

This diagram shows how monitoring data flows from MSRs through the architecture layer to the filesystem layer.

```mermaid
graph TD
    A[rdtgroup_mondata_show] --> C[Parse event/domain from kernfs]
    C --> D[resctrl_arch_rmid_read]
    
    D --> E[logical_rmid_to_physical_rmid]
    E --> F[__rmid_read_phys]
    
    F --> G[wrmsrl MSR_IA32_QM_EVTSEL]
    F --> H[rdmsrl MSR_IA32_QM_CTR]
    F --> I{Counter valid?}
    I -->|Error bit set| J[Return -EIO]
    I -->|Unavailable| K[Return -EINVAL]
    I -->|Valid| L[Process counter value]
    
    L --> M[get_corrected_mbm_count]
    L --> N[Apply mon_scale factor]
    L --> O[Handle MBM overflow]
    
    M --> P[Apply hardware corrections]
    O --> Q[Update arch_mbm_state]
    
    
    style A fill:#e1f5fe
    style D fill:#e8f5e8
    style F fill:#fff3e0
    style G fill:#f3e5f5
    style H fill:#f3e5f5
```

## 4. MSR Access and Hardware Interface

This diagram shows the low-level MSR access patterns for monitoring and control.

```mermaid
graph TD
    A[Hardware MSR Interface] --> B[Monitoring MSRs]
    A --> C[Control MSRs]
    A --> D[Configuration MSRs]
    
    B --> E[MSR_IA32_QM_EVTSEL 0xc8d]
    B --> F[MSR_IA32_QM_CTR 0xc8e]
    B --> G[MSR_IA32_PQR_ASSOC 0xc8f]
    
    C --> H[MSR_IA32_L3_CBM_BASE]
    C --> I[MSR_IA32_L2_CBM_BASE]
    C --> J[MSR_IA32_MBA_THRTL_BASE]
    
    D --> K[MSR_IA32_L3_QOS_CFG]
    D --> L[MSR_IA32_L2_QOS_CFG]
    D --> M[MSR_RMID_SNC_CONFIG 0xca0]
    D --> N[MSR_IA32_EVT_CFG_BASE 0xc0000400]
    
    E --> O[Event ID + RMID selection]
    F --> P[Counter value read]
    G --> Q[CLOSID/RMID association]
    
    K --> R[l3_qos_cfg_update]
    L --> S[l2_qos_cfg_update]
    M --> T[SNC configuration]
    N --> U[Event configuration]
    
    R --> V[CDP enable/disable]
    S --> V
    
    style A fill:#e1f5fe
    style B fill:#e8f5e8
    style C fill:#fff3e0
    style D fill:#f3e5f5
```

## 5. SNC (Sub-NUMA Cluster) Support Flow

This diagram shows how SNC support works for monitoring in multi-node configurations.

```mermaid
graph TD
    A[resctrl_arch_rmid_read] --> B[logical_rmid_to_physical_rmid]
    B --> C{SNC enabled?}
    C -->|No| D[physical_rmid = logical_rmid]
    C -->|Yes| E[Calculate node_id from domain]
    
    E --> F[physical_rmid = logical_rmid + node_id * num_rmid]
    F --> G[Use physical_rmid for MSR access]
    D --> G
    
    G --> H[__rmid_read_phys]
    H --> I[wrmsrl MSR_IA32_QM_EVTSEL]
    H --> J[rdmsrl MSR_IA32_QM_CTR]
    
    K[SNC Configuration] --> L[MSR_RMID_SNC_CONFIG]
    L --> M[Set sharing mode]
    M --> N[Configure node count]
    
    O[Domain Creation] --> P[snc_nodes_per_l3_cache]
    P --> Q[Calculate domains per SNC node]
    
    style A fill:#e1f5fe
    style B fill:#e8f5e8
    style C fill:#fff3e0
    style E fill:#f3e5f5
    style K fill:#f1f8e9
    style O fill:#fce4ec
```

## 6. Architecture Resource Initialization

This diagram shows how architecture-specific resources are initialized and configured.

```mermaid
graph TD
    A[resctrl_cpu_detect] --> B[CPUID checks]
    B --> C[rdt_get_mon_l3_config]
    B --> D[rdt_get_cache_alloc_cfg]
    B --> E[rdt_get_mba_config]
    
    C --> F[Configure MBM width offset]
    C --> G[Set occupancy scale]
    C --> H[Initialize BMEC mask]
    C --> I[Setup SNC configuration]
    
    D --> J[Configure cache CBM]
    D --> K[Set CDP capability]
    D --> L[Initialize control domains]
    
    E --> M[Configure MBA throttle]
    E --> N[Set MBA capability]
    
    O[Resource Registration] --> P[rdt_resources_all array]
    P --> Q[L3 Resource]
    P --> R[L2 Resource]
    P --> S[MBA Resource]
    
    Q --> T[ctrl_domains list]
    Q --> U[mon_domains list]
    R --> T
    S --> T
    
    style A fill:#e1f5fe
    style C fill:#e8f5e8
    style D fill:#fff3e0
    style E fill:#f3e5f5
    style O fill:#f1f8e9
    style P fill:#fce4ec
```

## 7. CPU Online and Domain Creation Flow

This diagram shows the complete flow from CPU coming online to domain creation and initialization.

```mermaid
graph TD
    A[resctrl_arch_online_cpu] --> B[mutex_lock domain_list_lock]
    B --> C[for_each_capable_rdt_resource]
    C --> D[domain_add_cpu]
    
    D --> E{Resource capabilities?}
    E -->|alloc_capable| F[domain_add_cpu_ctrl]
    E -->|mon_capable| G[domain_add_cpu_mon]
    
    F --> H[get_domain_id_from_scope]
    G --> I[get_domain_id_from_scope]
    
    H --> J[resctrl_find_domain ctrl_domains]
    I --> K[resctrl_find_domain mon_domains]
    
    J --> L{Domain exists?}
    K --> M{Domain exists?}
    
    L -->|Yes| N[Add CPU to existing ctrl domain]
    L -->|No| O[Create new ctrl domain]
    M -->|Yes| P[Add CPU to existing mon domain]
    M -->|No| Q[Create new mon domain]
    
    O --> R[domain_setup_ctrlval]
    Q --> S[arch_mon_domain_online]
    Q --> T[arch_domain_mbm_alloc]
    
    R --> U[Allocate ctrl_val arrays]
    R --> V[setup_default_ctrlval]
    R --> W[msr_update - program MSRs]
    
    S --> X[Configure SNC if needed]
    T --> Y[Allocate MBM counter arrays]
    
    AA[Filesystem Integration] --> BB[resctrl_online_ctrl_domain]
    AA --> CC[resctrl_online_mon_domain]
    
    CC --> DD[domain_setup_mon_state]
    CC --> EE[Setup MBM overflow handler]
    CC --> FF[mkdir_mondata_subdir_allrdtgrp]
    
    DD --> GG[Allocate rmid_busy_llc bitmap]
    DD --> HH[Allocate mbm_total/local arrays]
    
    style A fill:#e1f5fe
    style D fill:#e8f5e8
    style F fill:#fff3e0
    style G fill:#f3e5f5
    style O fill:#f1f8e9
    style Q fill:#fce4ec
    style R fill:#e1f5fe
    style S fill:#e8f5e8
```

## 8. Domain ID Resolution and CPU Topology Mapping

This diagram shows how CPU topology is mapped to domain IDs for different resource scopes.

```mermaid
graph TD
    A[get_domain_id_from_scope] --> B{Resource scope?}
    
    B -->|RESCTRL_L3_CACHE| C[get_cpu_cacheinfo_id cpu, 3]
    B -->|RESCTRL_L2_CACHE| D[get_cpu_cacheinfo_id cpu, 2] 
    B -->|RESCTRL_L3_NODE| E[cpu_to_node cpu]
    
    C --> F[L3 Cache ID]
    D --> G[L2 Cache ID]
    E --> H[NUMA Node ID]
    
    I[CPU Topology Examples] --> J[CPUs 0-15 share L3 cache → Domain 0]
    I --> K[CPUs 16-31 share L3 cache → Domain 1]
    I --> L[CPUs 0-1 share L2 cache → Domain 0]
    I --> M[CPUs 2-3 share L2 cache → Domain 1]
    
    N[Domain Lists] --> O[r->ctrl_domains - sorted by ID]
    N --> P[r->mon_domains - sorted by ID]
    
    style A fill:#e1f5fe
    style B fill:#e8f5e8
    style C fill:#fff3e0
    style D fill:#f3e5f5
    style E fill:#f1f8e9
    style I fill:#fce4ec
```

## 8. Event Configuration Interface

This diagram shows how monitoring events are configured through the architecture layer.

```mermaid
graph TD
    A[Event Configuration] --> B[mon_event_config_index_get]
    B --> C{Event ID?}
    C -->|QOS_L3_MBM_TOTAL| D[Return index 0]
    C -->|QOS_L3_MBM_LOCAL| E[Return index 1]
    C -->|Invalid| F[Return INVALID_CONFIG_INDEX]
    
    G[resctrl_arch_mon_event_config_write] --> H[mon_event_config_index_get]
    H --> I[wrmsrq MSR_IA32_EVT_CFG_BASE + index]
    
    J[resctrl_arch_mon_event_config_read] --> K[mon_event_config_index_get]
    K --> L[rdmsrq MSR_IA32_EVT_CFG_BASE + index]
    L --> M[Apply MAX_EVT_CONFIG_BITS mask]
    
    style A fill:#e1f5fe
    style B fill:#e8f5e8
    style C fill:#fff3e0
    style G fill:#f3e5f5
    style J fill:#f1f8e9
```

## 9. Rename/Move Operation Flow

This diagram shows the complete workflow for renaming/moving monitoring groups between parent control groups.

```mermaid
graph TD
    A[rdtgroup_rename] --> B[rdtgroup_kn_lock_live src]
    A --> C[rdtgroup_kn_lock_live dst_parent]
    
    B --> D[Validation Checks]
    C --> D
    
    D --> E[alloc_cpumask_var]
    E --> F[kernfs_rename]
    F --> G[mongrp_reparent]
    
    G --> H[Update parent lists]
    G --> I[Update CLOSID]
    G --> J[rdt_move_group_tasks]
    G --> K[update_closid_rmid]
    
    J --> L[Move tasks to new parent]
    K --> M[resctrl_arch_sync_cpu_closid_rmid]
    
    M --> N[Update CPU MSRs]
    
    style A fill:#e1f5fe
    style D fill:#e8f5e8
    style F fill:#fff3e0
    style G fill:#f3e5f5
    style J fill:#f1f8e9
    style K fill:#f1f8e9
    style M fill:#fce4ec
```

## 10. Group Reparenting Details - mongrp_reparent

This diagram shows the detailed steps within the `mongrp_reparent` function.

```mermaid
graph TD
    A[mongrp_reparent] --> B[Remove from old parent list]
    A --> C[Add to new parent list]
    A --> D[Update parent pointer]
    A --> E[Update CLOSID]
    
    B --> F[list_del old_prdtgrp->mon.crdtgrp_list]
    C --> G[list_add new_prdtgrp->mon.crdtgrp_list]
    D --> H[rdtgrp->parent = new_prdtgrp]
    E --> I[rdtgrp->closid = new_prdtgrp->closid]
    
    I --> J[rdt_move_group_tasks]
    J --> K[task_rq_lock each task]
    J --> L[task->closid = new_closid]
    J --> M[task_rq_unlock each task]
    
    M --> N[update_closid_rmid]
    N --> O[on_each_cpu_mask]
    O --> P[resctrl_arch_sync_cpu_closid_rmid]
    
    P --> Q[this_cpu_write pqr_state.default_closid]
    P --> R[this_cpu_write pqr_state.default_rmid]
    P --> S[resctrl_arch_sched_in current]
    
    S --> T[Update MSR_IA32_PQR_ASSOC if needed]
    
    style A fill:#e1f5fe
    style F fill:#e8f5e8
    style G fill:#e8f5e8
    style J fill:#fff3e0
    style N fill:#f3e5f5
    style P fill:#f1f8e9
    style S fill:#fce4ec
```

## 11. Pseudo-Lock Operation Overview

Pseudo-locking is a feature that allows loading specific memory regions into cache and preventing them from being evicted by future cache allocation operations. This provides deterministic cache allocation for critical workloads.

### Pseudo-Lock Concepts

**Cache Pseudo-Locking** works by:
1. **Isolation**: Setting up a dedicated cache capacity bitmask (CBM) for the region
2. **Loading**: Reading the target memory while using the dedicated CBM to load it into cache
3. **Protection**: Preventing future CBM allocations from overlapping with the pseudo-locked region

**Key Components**:
- **Pseudo-Lock Region**: Memory region to be locked into cache
- **CLOSID**: Cache allocation class used during the locking process
- **CBM**: Cache capacity bitmask defining which cache ways are reserved
- **Thread**: Kernel thread that performs the actual cache loading
- **Measurement**: Performance monitoring to verify locking effectiveness

## 12. Pseudo-Lock State Machine and Mode Transitions

This diagram shows the state transitions in the pseudo-lock lifecycle.

```mermaid
graph TD
    A[RDT_MODE_SHAREABLE/EXCLUSIVE] --> B[Write 'pseudo-locksetup' to mode file]
    B --> C[rdtgroup_locksetup_enter]
    
    C --> D[RDT_MODE_PSEUDO_LOCKSETUP]
    D --> E[Write schemata with CBM]
    D --> F[Write 'shareable/exclusive' to mode file]
    
    E --> G[rdtgroup_pseudo_lock_create]
    F --> H[rdtgroup_locksetup_exit]
    
    G --> I[RDT_MODE_PSEUDO_LOCKED]
    H --> A
    
    I --> J[Group deletion only]
    J --> K[rdtgroup_pseudo_lock_remove]
    
    style A fill:#e1f5fe
    style D fill:#fff3e0
    style I fill:#ffebee
    style C fill:#e8f5e8
    style G fill:#f3e5f5
    style H fill:#f1f8e9
    style K fill:#fce4ec
```

## 13. Pseudo-Lock Setup Flow (rdtgroup_locksetup_enter)

This diagram shows the validation and setup process when entering pseudo-lock setup mode.

```mermaid
graph TD
    A[rdtgroup_locksetup_enter] --> B[Validation Checks]
    
    B --> C[Not default group?]
    B --> D[CDP disabled?]
    B --> E[Platform supports prefetch disable?]
    B --> F[No monitoring in progress?]
    B --> G[No tasks/CPUs assigned?]
    
    H[All validations pass] --> I[Restrict filesystem permissions]
    I --> J[Initialize pseudo_lock_region struct]
    J --> K[Free monitoring RMID]
    K --> L[Set mode = RDT_MODE_PSEUDO_LOCKSETUP]
    
    style A fill:#e1f5fe
    style B fill:#e8f5e8
    style H fill:#fff3e0
    style I fill:#f3e5f5
    style J fill:#f1f8e9
    style L fill:#fce4ec
```

## 14. Pseudo-Lock Creation Flow (rdtgroup_pseudo_lock_create)

This diagram shows the complete process of creating an active pseudo-lock.

```mermaid
graph TD
    A[rdtgroup_pseudo_lock_create] --> B[Allocate memory region]
    B --> C[Constrain CPU C-states]
    C --> D[Create kernel thread on target CPU]
    
    D --> E[kthread_run_on_cpu]
    E --> F[resctrl_arch_pseudo_lock_fn]
    
    F --> G[Cache Loading Process]
    G --> H[Wait for thread completion]
    
    H --> I[Create character device /dev/groupname]
    I --> J[Create debugfs measurement interface]
    J --> K[Set mode = RDT_MODE_PSEUDO_LOCKED]
    K --> L[Free CLOSID]
    L --> M[Update file permissions]
    
    style A fill:#e1f5fe
    style E fill:#e8f5e8
    style F fill:#fff3e0
    style G fill:#f3e5f5
    style I fill:#f1f8e9
    style K fill:#fce4ec
```

## 15. Arch-Specific Cache Loading Process (resctrl_arch_pseudo_lock_fn)

This diagram shows the low-level cache loading implementation in the architecture layer.

```mermaid
graph TD
    A[resctrl_arch_pseudo_lock_fn] --> B[wbinvd - Flush all caches]
    B --> C[local_irq_disable]
    C --> D[Disable hardware prefetchers]
    D --> E[Save current CLOSID/RMID]
    E --> F[Set pseudo-lock CLOSID]
    
    F --> G[Critical Section Begin]
    G --> H[First loop: Page-level access]
    H --> I[Second loop: Cache-line access]
    I --> J[Critical Section End]
    
    J --> K[Restore original CLOSID/RMID]
    K --> L[Re-enable hardware prefetchers]
    L --> M[local_irq_enable]
    M --> N[Wake up waiting thread]
    
    style A fill:#e1f5fe
    style B fill:#e8f5e8
    style G fill:#fff3e0
    style H fill:#f3e5f5
    style I fill:#f3e5f5
    style J fill:#f1f8e9
    style N fill:#fce4ec
```

## 16. Pseudo-Lock Performance Measurement

This diagram shows how pseudo-lock effectiveness is measured using performance counters.

```mermaid
graph TD
    A[Performance Measurement] --> B[resctrl_arch_measure_cycles_lat_fn]
    A --> C[resctrl_arch_measure_l2_residency]
    A --> D[resctrl_arch_measure_l3_residency]
    
    B --> E[Measure memory access latency]
    E --> F[rdtsc_ordered for timing]
    E --> G[Access memory at 32-byte stride]
    E --> H[trace_pseudo_lock_mem_latency]
    
    C --> I[Create perf events for L2]
    C --> J[MEM_LOAD_UOPS_RETIRED events]
    C --> K[L2_HIT and L2_MISS counters]
    
    D --> L[Create perf events for L3]
    D --> M[LONGEST_LAT_CACHE events]
    D --> N[Cache references and misses]
    
    K --> O[measure_residency_fn]
    N --> O
    O --> P[Disable prefetchers]
    O --> Q[Read perf counters before/after]
    O --> R[Access pseudo-locked memory]
    O --> S[Calculate hit/miss ratios]
    
    style A fill:#e1f5fe
    style B fill:#e8f5e8
    style C fill:#fff3e0
    style D fill:#f3e5f5
    style O fill:#f1f8e9
    style S fill:#fce4ec
```

## 17. Pseudo-Lock Hardware Support Detection

This diagram shows how hardware support for pseudo-locking is detected.

```mermaid
graph TD
    A[resctrl_arch_get_prefetch_disable_bits] --> B[Check CPU vendor and family]
    B --> C{Intel x86 family 6?}
    C -->|No| D[Return 0 - Not supported]
    C -->|Yes| E[Check CPU model]
    
    E --> F{BROADWELL_X?}
    E --> G{GOLDMONT/GOLDMONT_PLUS?}
    E --> H{Other models?}
    
    F -->|Yes| I[prefetch_disable_bits = 0xF]
    G -->|Yes| J[prefetch_disable_bits = 0x5]
    H -->|Yes| K[prefetch_disable_bits = 0]
    
    I --> L[L2 HW Prefetcher Disable]
    I --> M[L2 Adjacent Line Prefetcher Disable]
    I --> N[DCU HW Prefetcher Disable]
    I --> O[DCU IP Prefetcher Disable]
    
    J --> P[L2 HW Prefetcher Disable]
    J --> Q[DCU HW Prefetcher Disable]
    
    style A fill:#e1f5fe
    style C fill:#e8f5e8
    style E fill:#fff3e0
    style F fill:#f3e5f5
    style G fill:#f3e5f5
    style I fill:#f1f8e9
    style J fill:#fce4ec
```

## Key Integration Points

The diagrams show several critical integration points between the filesystem and architecture layers:

1. **Reference Counting**: The `rdtgroup_kn_lock_live`/`rdtgroup_kn_unlock` mechanism ensures safe concurrent access
2. **MSR Abstraction**: Architecture layer provides clean MSR interface to filesystem layer
3. **Domain Management**: CPU hotplug events are handled transparently by the architecture layer
4. **Error Handling**: Hardware errors and unavailable conditions are properly propagated
5. **Resource Management**: Architecture-specific resource initialization is abstracted from the filesystem layer
6. **Rename Operations**: Monitoring groups can be safely moved between parent control groups with proper validation and MSR updates
7. **Task Migration**: When groups are reparented, all associated tasks are moved and their MSRs are updated atomically
8. **Pseudo-Lock Integration**: Mode transitions and cache loading operations bridge filesystem control with hardware-specific cache manipulation
9. **Performance Measurement**: Provides comprehensive measurement capabilities using hardware performance counters and tracing

## Rename Operation Characteristics

The rename/move workflow has several important characteristics:

- **Atomic Operations**: Uses kernfs_rename followed by mongrp_reparent to ensure consistency
- **Reference Safety**: Uses the same reference counting mechanism as other operations
- **Validation**: Extensive validation prevents invalid moves (e.g., moving control groups, moving to non-mon_groups directories)
- **CPU Constraint Enforcement**: Prevents moving MON groups that are actively monitoring CPUs between different parent CTRL_MON groups
- **MSR Synchronization**: All affected CPUs have their MSRs updated when tasks are moved between CLOSIDs
- **Error Recovery**: Proper cleanup on all error paths ensures no partial state corruption

## Pseudo-Lock Operation Characteristics

The pseudo-lock feature has several key characteristics:

- **Hardware Requirements**: Requires specific Intel CPU models with prefetch disable capability
- **State Machine**: Uses a 4-state model with clear transitions and validation
- **Deterministic Loading**: Two-pass memory access ensures reliable cache loading
- **Performance Measurement**: Comprehensive measurement using hardware performance counters
- **Resource Isolation**: Creates exclusive cache regions that cannot be evicted by other allocations
- **Thread Safety**: Uses kernel threads and proper synchronization for cache loading operations
- **Device Interface**: Provides character device and debugfs interfaces for user access and debugging

These flows demonstrate how the ResCtrl subsystem maintains a clean separation between filesystem operations and hardware-specific implementation details while ensuring proper synchronization and error handling throughout the stack.