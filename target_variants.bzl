targets = [
    # keep sorted
    "gen3auto",
    "autogvm",
    "pineapple",
    "blair",
]

la_variants = [
    # keep sorted
    "consolidate",
    "gki",
]

qx_variants = [
    # keep sorted
    "debug-defconfig",
    "perf-defconfig",
]

qx_targets = [
    # keep sorted
    "gen4auto",
]

le_targets = [
    # keep sorted
    "pineapple-allyes",
]

le_variants = [
    # keep sorted
    "perf-defconfig",
]

vm_types = [
    "tuivm",
    "oemvm",
]

vm_target_bases = [
    "pineapple",
]

vm_targets = ["{}-{}".format(t, vt) for t in vm_target_bases for vt in vm_types]

vm_variants = [
    # keep sorted
    "debug-defconfig",
    "defconfig",
]

def get_all_la_variants():
    return [(t, v) for t in targets for v in la_variants]

def get_all_le_variants():
    return [(t, v) for t in le_targets for v in le_variants]

def get_all_qx_variants():
    return [(t, v) for t in qx_targets for v in qx_variants]

def get_all_vm_variants():
    return [(t, v) for t in vm_targets for v in vm_variants]

def get_all_variants():
    return get_all_la_variants() + get_all_le_variants() + get_all_qx_variants() + get_all_vm_variants()
