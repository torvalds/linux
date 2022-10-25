targets = [
    # keep sorted
    "kalama",
    "pineapple",
]

la_variants = [
    # keep sorted
    "consolidate",
    "gki",
]

vm_types = [
    "tuivm",
    #    "oemvm",
]

vm_targets = ["{}-{}".format(t, vt) for t in targets for vt in vm_types]

vm_variants = [
    # keep sorted
    "debug-defconfig",
    "defconfig",
]

def get_all_la_variants():
    return [(t, v) for t in targets for v in la_variants]

def get_all_vm_variants():
    return [(t, v) for t in vm_targets for v in vm_variants]

def get_all_variants():
    return get_all_la_variants() + get_all_vm_variants()
