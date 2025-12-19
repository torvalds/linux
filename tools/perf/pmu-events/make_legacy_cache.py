#!/usr/bin/env python3
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
import json

hw_cache_id = [
    (0, # PERF_COUNT_HW_CACHE_L1D
     ["L1-dcache",  "l1-d",     "l1d",      "L1-data",],
     [0, 1, 2,], # read, write, prefetch
     "Level 1 data cache",
     ),
    (1, # PERF_COUNT_HW_CACHE_L1I
     ["L1-icache",  "l1-i",     "l1i",      "L1-instruction",],
     [0, 2,], # read, prefetch
     "Level 1 instruction cache",
     ),
    (2, # PERF_COUNT_HW_CACHE_LL
     ["LLC", "L2"],
     [0, 1, 2,], # read, write, prefetch
     "Last level cache",
     ),
    (3, # PERF_COUNT_HW_CACHE_DTLB
     ["dTLB",   "d-tlb",    "Data-TLB",],
     [0, 1, 2,], # read, write, prefetch
     "Data TLB",
     ),
    (4, # PERF_COUNT_HW_CACHE_ITLB
     ["iTLB",   "i-tlb",    "Instruction-TLB",],
     [0,], # read
     "Instruction TLB",
     ),
    (5, # PERF_COUNT_HW_CACHE_BPU
     ["branch", "branches", "bpu",      "btb",      "bpc",],
     [0,], # read
     "Branch prediction unit",
     ),
    (6, # PERF_COUNT_HW_CACHE_NODE
     ["node",],
     [0, 1, 2,], # read, write, prefetch
     "Local memory",
     ),
]

hw_cache_op = [
    (0, # PERF_COUNT_HW_CACHE_OP_READ
     ["load",   "loads",    "read",],
     "read"),
    (1, # PERF_COUNT_HW_CACHE_OP_WRITE
     ["store",  "stores",   "write",],
     "write"),
    (2, # PERF_COUNT_HW_CACHE_OP_PREFETCH
     ["prefetch",   "prefetches",   "speculative-read", "speculative-load",],
     "prefetch"),
]

hw_cache_result = [
    (0, # PERF_COUNT_HW_CACHE_RESULT_ACCESS
     ["refs",   "Reference",    "ops",      "access",],
     "accesses"),
    (1, # PERF_COUNT_HW_CACHE_RESULT_MISS
     ["misses", "miss",],
     "misses"),
]

events = []
def add_event(name: str,
              cache_id: int, cache_op: int, cache_result: int,
              desc: str,
              deprecated: bool) -> None:
    # Avoid conflicts with PERF_TYPE_HARDWARE events which are higher priority.
    if name in ["branch-misses", "branches"]:
        return

    # Tweak and deprecate L2 named events.
    if name.startswith("L2"):
        desc = desc.replace("Last level cache", "Level 2 (or higher) last level cache")
        deprecated = True

    event = {
        "EventName": name,
        "BriefDescription": desc,
        "LegacyCacheCode": f"0x{cache_id | (cache_op << 8) | (cache_result << 16):06x}",
    }

    # Deprecate events with the name starting L2 as it is actively
    # confusing as on many machines it actually means the L3 cache.
    if deprecated:
        event["Deprecated"] = "1"
    events.append(event)

for (cache_id, names, ops, cache_desc) in hw_cache_id:
    for name in names:
        add_event(name,
                  cache_id,
                  0, # PERF_COUNT_HW_CACHE_OP_READ
                  0, # PERF_COUNT_HW_CACHE_RESULT_ACCESS
                  f"{cache_desc} read accesses.",
                  deprecated=True)

        for (op, op_names, op_desc) in hw_cache_op:
            if op not in ops:
                continue
            for op_name in op_names:
                deprecated = (names[0] != name or op_names[1] != op_name)
                add_event(f"{name}-{op_name}",
                          cache_id,
                          op,
                          0, # PERF_COUNT_HW_CACHE_RESULT_ACCESS
                          f"{cache_desc} {op_desc} accesses.",
                          deprecated)

                for (result,  result_names, result_desc) in hw_cache_result:
                    for result_name in result_names:
                        deprecated = ((names[0] != name or op_names[0] != op_name) or
                                      (result == 0) or (result_names[0] != result_name))
                        add_event(f"{name}-{op_name}-{result_name}",
                                  cache_id, op, result,
                                  f"{cache_desc} {op_desc} {result_desc}.",
                                  deprecated)

        for (result,  result_names, result_desc) in hw_cache_result:
            for result_name in result_names:
                add_event(f"{name}-{result_name}",
                          cache_id,
                          0, # PERF_COUNT_HW_CACHE_OP_READ
                          result,
                          f"{cache_desc} read {result_desc}.",
                          deprecated=True)

print(json.dumps(events, indent=2))
