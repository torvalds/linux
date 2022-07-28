targets = [
    # keep sorted
    "kalama",
    "pineapple",
]

variants = [
    # keep sorted
    "consolidate",
    "gki",
]

def get_all_variants():
    return [(t, v) for t in targets for v in variants]
