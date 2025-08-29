So I'm going to collect some thoughts on how the resource control framework in Linux works. we're trying to add support for reading measurement of cache allocation and memory bandwidth from resource control using perf events. And the reason is we want to enable very frequent readings of the resource control counters from the CPU, order of one millisecond. - I think supporting Perf events involves writing a PMU (`struct pmu`). We have a separate write-up on how the PMU interacts with the kernel and what is required in order to build a PMU. Here we want to focus on what is required to interface with the resource control subsystem in the kernel. The resource control subsystem allows users to create new groups and then assign tasks to them. And we want to keep that interface when we support perf events. So in essence, we would like the perf event to read the measurements associated with the resource control groups configured using the filsystem based interface.

The user opening a perf event using the perf_event_open system call would need to specify the group and what type of event they want to measure. there are multiple metrics that resource control offers. A user might pass that using configuration parameters to the perf_event_open system call. We need a way for the users to specify which resource control group they want to monitor. And then we need to manage lifetimes of the perf structures in a way that makes sense with a lifetime of the resource control objects.

We need to answer a few questions:
1. How do users specify the resource control group and the event they want to measure?
2. What happens to the perf event when a user deletes a resource control group? What are the semantics: is there an error? does the event just stop reading new values?
3. How do we manage lifetimes, making sure that all of the perf calls remain valid as users might interact with resource control, and the system correctly preserves lifetimes and frees data structures?

And so for that, we want to understand and how the resource control subsystem is organized.

There are _monitoring_ groups and _control_ groups.

The root of the filesystem is managed through a variable `rdtgroup_default`.

The hierarchy allows creating resctrl groups under the root. If monitoring is supported, there is a `mon_groups` directory under the root, which contains subdirectories for each monitoring group. If allocation is supported, the user can create new resctrl groups using `mkdir` under the root directory, and these will allow allocation and also contain a `mon_groups` directory where monitoring groups can be created. This allows the user to create a hierarchy with a control groups and then monitoring groups underneath them.

## Group lifecycle
It is important to understand lifecycle management, reference counting for the resource control groups in order to support perf. So let's look at how that is managed. The struct for resource control groups is `struct rdtgroup`. the rdtgroup is closely associated with a kernelfs node which is the file system node users can access resource control. Lifecycle is intertwined with kernfs. Kernfs implements a feature called active references, which are references on the kernfs nodes that are held during operations on kernfs nodes. Unlike the regular reference counts that prevent memory deallocation, the active references also prevent removal of the node from the kernfs file system. The resctrl implementation maintains the invariant that active reference holders can safely access the `rdtgroup`p associated with that node. On every kernfs operation (which holds an active reference), the code quickly exchanges the active referenc with a reference count on the `rdtgroup` using `rdtgroup_kn_lock_live`. That reference count is stored in the `waitcount` field of rdtgroup. That reference count protects the RDT group from being deallocated. 

When the user removes the `rdtgroup`, for example using RMDIR in `rdtgroup_rmdir_mon` or `rdtgroup_rmdir_ctrl`, the code sets the flag `RDT_DELETED` on the rdtgroup, and removes the kernfs node. The node removal waits for all the active references to be released. At that point all the operations in flight would have commuted their active reference to the kernfs node to a rdtgroup reference (`waitcount`), and no new operations can start, since the node has been removed. The last rdtgroup reference to be released via `rdtgroup_kn_unlock` will call `rdtgroup_kn_put` which will then call `rdtgroup_remove`, which releases the last regular reference to the kernfs node and frees the `rdtgroup`.

Note that it seems like the way this subsystem was written was to ensure that removals can proceed relatively quickly in the face of filesystem resctrl operations like removals. The file operations quickly free up active references, so nodes can be removed: the implementation avoids tying up groups' active references during long-lived operations. If we want to keep this agility of removal of groups, we would need to ensure that our implementation does not hinder group removals by holding active references long.


## Group Renaming
The rename functionality allows very specific moves of groups inside the resource control file system. The moves it allows are moving monitoring groups from under one parent to another parent. and those parents are always either the default group or control group. this allows changing the resources allocated to a group without changing the monitoring. Looking at the function `mongrp_reparent`, It moves an existing struct rdt group from one parent to another. By setting the parent pointer of the control group and and moving itself from the old groups list to the new groups. This means that any perf event should continue operation through such a move. 


## Monitoring Uses Resources, Events and Domains
Okay, so what is the interface that the kernel's resource control provides internally to read the monitoring values? 

`rdtgroup_mondata_show` is the `seq_show` handler for monitoring files, set for files added with `mon_addfile`. The `priv` member of the `kernfs_node` for the monitoring files encodes several fields: `rid`, `evtid`, `domid`, and `sum` (`struct mon_data_bits` contains the union to encode this). So how are resources (`rid`), events (`evtid`) and domains (`domid`) maintained, and what is the `sum` field?

## Resources and Events
Each architecture (currently x86) defines a `resctrl_arch_get_resource` function that returns a `struct rdt_resource` for a given resource ID, from 0 to `RDT_NUM_RESOURCES - 1`. In 6.15.6, the kernel defines (https://elixir.bootlin.com/linux/v6.15.6/source/include/linux/resctrl_types.h#L34) `RDT_RESOURCE_L3`, `RDT_RESOURCE_L2`, `RDT_RESOURCE_MBA`, `RDT_RESOURCE_SMBA`. And the variable that links to domains is `rdt_resources_all` https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/core.c#L59 . `rdt_resources_all` initializes lists `ctrl_domains` and `mon_domaines` of the `struct rdt_resource` it contains, depending on whether the resource supports control or monitoring.

Monitoring events in v6.15.6 are only supported for the L3 cache, and are initialized in `l3_mon_evt_init` https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/monitor.c#L1100 : LLC occupancy, MBM total and MBM local. So in the monitoring files, we should only have `rid` set to `RST_RESOURCE_L3`, and the `evtid` one of `QOS_L3_OCCUP_EVENT_ID`, ,`QOS_L3_MBM_TOTAL_EVENT_ID`, or `QOS_L3_MBM_LOCAL_EVENT_ID`. https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/monitor.c#L1078


## Domains

Whereas Resources are the hardware resources that can be controlled or monitored, domains are the logical groupings of CPUs and caches for control or measurement.

When resctrl is initialized, it subscribes to CPU state changes using `cpuhp_setup_state`: https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/core.c#L1029, with callback `resctrl_arch_online_cpu` to process new online CPUs. This callback calls `domain_add_cpu` for each resource, which calls `domain_add_cpu_ctrl` and `domain_add_cpu_mon` depending on the resource (control vs monitoring). These add a domain if it doesn't exist, and add the CPU to the domain's CPU mask.

The Sub-NUMA Clustering (SNC) feature influences how domains are initialized. In SNC mode, the CPU assigns memory addresses to L3 caches closest to the memory controller that serves them. This reduces latency when accessing memory local to the Sub-NUMA cluster. SNC mode creates multiple L3 domain for each cache, one for each SNC domain. In SNC mode, the counters for each RMID are distributed across the SNC domains, so to obtain a measurement, the RMID needs to be summed up across the domains, which is what the `sum` field in `mon_data_bits` signifies.

Each domain maintains a pointer to a `struct cacheinfo` which holds an ID for the cache. When the `sum` field is set, the `domid` field is the cache ID, and the `rdtgroup_mondata_show` function sums up the RMID's values for all domains on the cache with that cache ID. Otherwise, the `domid` field is the domain ID, and the `rdtgroup_mondata_show` function reads the RMID value for that domain. The `mon_data` directory created by `mkdir_mondata_subdir` contains a directory `mon_L3_NN` to read the RMID values, which in SNC mode sums up the relevant domains (and NN is the cache ID). In SNC mode, `mkdir_mondata_subdir` also adds directories `mon_sub_L3_<domid>` that reads the RMID on the specific SNC domain, with `sum` set to 0.

`snc_get_config` determines whether SNC is enabled, and sets the variable `snc_nodes_per_l3_cache` to the number of SNC nodes per L3 cache.


## Reading Event Counts

So, going back to what `rdtgroup_mondata_show` does, it retrieves the `struct rdtgroup` from the `kernfs_node`., unpacks the `priv` field, gets the resource for the `rid` (which in v6.15.6 is always L3), finds the CPU mask associated with `domid` (the relevant domain's if `sum` is 0, or the cache's if `sum` is set), and calls `mon_event_read` with the CPU mask for the domain/cache. `mon_event_read` calls `mon_event_count` on one of the CPUs in the mask. Measuring a control group also adds the measurements of all its monitoring groups, each read using `__mon_event_count`. `__mon_event_count` verifies that the CPU running it is legal for the domain or cache being read, and uses `resctrl_arch_rmid_read` to read the RMID value for the domain, or multiple RMID values for the cache, by passing the cache's different SNC domains as the domain parameter to `resctrl_arch_rmid_read`.


## Mutual exclusion and locking

TODO: overview

Do reads hold the global resctrl lock `rdtgroup_mutex`? `rdtgroup_kn_lock_live` locks it, and `rdtgroup_kn_unlock` unlocks it. Indeed `rdtgroup_mondata_show` uses these, and reads counters while holding the lock. These lock/unlock functions also hold the `cpus_read_lock()` so it can read the domain lists without worrying about them changing during the read. And the mutex also ensures `mon_event_count` can traverse the nested monitoring groups list of a control group to sum those values up. It seems that the `cpus_read_lock()` would be sufficient for reads of non control groups, since those reads just look at domains and rdt_resources, and read the fields closid and rmid of the `struct rdtgroup`. So a perf event that reads a monitoring group need not hold the `rdtgroup_mutex`, but it would need to hold the `cpus_read_lock()` to read entries in the domain list.

So from this locking discussion, it seems that the perf event reading a monitoring group could check if the group's type is a control group. If it is, it would need to hold the `rdtgroup_mutex` and the `cpus_read_lock()` while reading the counters. If it is not a control group, it would only need to hold the `cpus_read_lock()` while reading the counters.

## Counter Reads through the MBM Overflow Handler

MBM appears to have its own pathways to read event counts via `mbm_update` which calls `mbm_update_one_event` on the total and local event IDs. This is used in `mbm_handle_overflow` which iterates all resource control groups (top level and nested monitoring groups). When the filesystem is mounted or a new domain comes online, `mbm_setup_overflow_handler` schedules `mbm_handle_overflow` every `MBM_OVERFLOW_INTERVAL` milliseconds (defined as 1000 in v6.15.6 https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/internal.h#L21), and `mbm_handle_overflow` renews the timer. This ensures a minimum reading frequency for MBM events, which the designers seem to have intended to be frequent enough to allow the kernel to detect all counter overflows. 

This mechanism doesn't seem to have an interaction with the `mon_data` file read flow.

TODO: but why do this? Where is the state being updated? Where is it? If we're going to relax mutual exclusion for reads, is there a conflict on that state?


## Pseudo Locking
Cache pseudo locking is explained in kernel docs: https://elixir.bootlin.com/linux/v6.15.6/source/Documentation/arch/x86/resctrl.rst#L701 . It allocates an RDT group with exclusive access to a portion of the cache and memory that will map into this cache and ensures that and loads that memory into the cache because that RDT group has exclusive access to that cache area and that RDT group is then no longer used by any task or CPU, you, then any process accessing this memory will have it in cache and it would not be evicted by other tasks? The mechanism exposes this memory through a character device that it creates so that a user could memory map this area into their application to access this. high performance region of memory because most of it resides in cache. 

To set up sudo locking, the user sets up a control group and changes its mode to sudo lock setup. Then when setting the resources of the control group, it becomes the kernel, creates the memory and loads it into the cache. Moving into locksetup mode is done in the `rdtgroup_locksetup_enter` function. That function checks no monitoring is in progress and frees the RMID. So this would interfere with any perf events that are monitoring the group -- there should be a check ensuring there are no such perf events before entering the lock setup mode. The perf code should also disallow new perf events on the group in lock setup and in locked modes. `rdtgroup_locksetup_exit` is used when moving back to shareable or exclusive mode. It re-allocates an RMID, so should re-enable perf functionality.

Once locked, the rdtgroup cannot change mode and monitoring is disallowed, imposing no further constraints on the perf implementation.


## CPU Monitoring
one interesting feature of the resctrl subsystem is the cpu monitoring functionality. One of the files that is created for every resource group is `cpus`. The cpu mask in the `cpus` file controls the resctrl group that is responsible for tasks that have not been assigned to an explicit rdtgroup, that are running on those CPUs. so by assigning CPUs to a resource control group through this CPU mask, one can control which group controls and monitors tasks that are not assigned to any group, running on those CPUs. Each CPU is in exactly in one of the default mask or any of the control groups. Each CPU in the mask of the default group or a control group might also belong at most one monitoring group under the default/control group. `cpus_ctrl_write`, `cpus_mon_write`, `rdtgroup_rmdir_ctrl`, and `rdtgroup_rmdir_mon` maintain the CPU mask invariants, and cause MSRs to be updated on affected CPUs using `update_closid_rmid`. Due to this feature, control groups also have an RMID (Resource Monitoring ID) associated with them, and it is possible to monitor control groups.





## Perf and CPU locality

The perf subsystem already takes care of reading counters on suitable CPUs, so it can skip the `smp_call_on_cpu()` or `smp_call_function_any()` calls. `perf_event_read` already calls `smp_call_function_single` to a suitable CPU. In cases where the PMU specifies that an event can be read on any CPU on the same package, and the current CPU is on the same package as `event->oncpu`, `__perf_event_read_cpu` would return is a the current CPU, and `smp_call_function_single` would run the function locally -- a performance bonus. Indeed, perf also offers a `perf_event_read_local` function that reads the event on the current CPU without calling `smp_call_function_single`, which is NMI-safe and is the function used for perf reads from eBPF.

The new perf implementation can leverage perf's existing CPU selection logic and avoid the logic in `mon_event_read`, instead scheduling `mon_event_count`. 

Can we specify that events are package events (`event->event_caps |= PERF_EV_CAP_READ_ACTIVE_PKG`) When the user asks to monitor a domain or cache? Never specifying `PERF_EV_CAP_READ_ACTIVE_PKG` is safe but would restrict the user to read the perf event on that single core. Since reads are L3-scoped, we would need to know that the package only has one L3 cache. Both non-SNC and SNC modes allow any core in the cache to query every RMID (the difference is that SNC mode requires multiple RMID reads to obtain the value), so a single cache per package would allow the flag to be set. We still need to check that CPUs guarantee the one-to-one cache to package mapping.


## Maintaining State in Perf Events
One of the gaps we still have is How do PMUs handle their state? How can each PMU keep its own set of variables for each perf event? The perf event subsystem has has several data structures for perf event: `struct perf_event`, `struct perf_event_context` and `struct perf_event_pmu_context`. We should understand how custom state could be saved by PMUs.

It turns out this seems to be well provisioned in the perf subsystem. PMUs can declare an `event_init` function where they can set the `event->pmu_priv` field that is controlled by the PMU. The PMU can set the `event->destroy` callback, which allows the PMU to free any resources tied up with that private data. 

Another location of event data is in `event->hw` which is a `struct hw_perf_event`. It holds a union for different event types, and maintainers might support adding union members there rather than allocating/deallocating private data per event.





As the user opens perf events, what are the semantics in the face of group removals? This would influence the complexity and cost of coordination between the perf implementation and the resource control subsystem. One option would be that once the perf event was opened with a CLOSID and RMID, it would continue to read the same group, even if the group was removed; it would be the responsibility of the user to ensure that the group is not removed while the perf event is open. The failure mode could be if those IDs are re-allocated to a different group, then the perf event would read the wrong group. This is a simple solution, but it requires the user to be careful about group removals.

A richer semantics would be to have the perf event stop reading the group when it is removed. It could return the same measurement over and over (i.e., no change), or return an error on the next read. This would allow users to remove groups without worrying about perf events reading them, but it would add some complexity to the perf implementation. We would prefer any mechanism to have minimal performance impact in the regular case, and to push coordination overhead to group removals -- while not blocking removals.

when an rdt group is removed from kernfs, its `flags` member is marked with `RDT_DELETED`. if we could maintain a list of all of the perf events related to the RDT group we could require the process of marking the RDT group as deleted to also mark all of the perf events with deleted flags. Then each perf event would only have to check its own flags on every read to check if it is still valid, which would be local to the other state that the perf event keeps and and so should have very low overhead. and in regular operation there would be very little coordination overhead. We We impose this coordination overhead only when the RDT group is deleted where whoever Whoever is removing the RDT group has the burden of traversing whatever perf events in the system are related to that RDT group and notifying them. And that would be a handful of cash line bounces to set these flags, which should be acceptable overhead. In this case every perf event would hold a reference count to the RDT group to ensure that the list of perf events is not, is still accessible throughout the lifetime of the perf event. it. Adding a new perf event would mean enqueuing, adding it to the list. And closing the perf event would entail removing it from the list. and so the the data structure holding that perf event list needs to have a lifetime longer than all of the perf events. events. In this case, if every perf event holds a reference to the RDT group, then that would would ensure that the list remains alive as long as it needs to. It might cause removing of of freeing of the RDT group memory to take longer. but since the perf events would still be open, That would be acceptable behavior to keep the RDT group as long as there is a perf event open. it does not having this extra reference does it still allows the removal of the RDT group which is good which is what we wanted to have. 






links:
- rdtgroup_kn_lock_live: https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/rdtgroup.c#L2556
- rdtgroup_kn_unlock: https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/rdtgroup.c#L2575
- rdtgroup_kn_put: https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/rdtgroup.c#L2542
-  rdtgroup_rmdir_mon: https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/rdtgroup.c#L3801
- rdtgroup_rmdir_ctrl: https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/rdtgroup.c#L3849
-  rdtgroup_locksetup_exit: https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/pseudo_lock.c#L769
-  rdtgroup_mondata_show: https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/kernel/cpu/resctrl/ctrlmondata.c#L661