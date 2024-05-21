# Add unprotected modules corresponding to each platform
_unprotected_modules_map = {
    "pitti": [
        # keep sorted
        "drivers/block/zram/zram.ko",
        "mm/zsmalloc.ko",
    ],
}

def get_unprotected_vendor_modules_list(msm_target = None):
    """ Provides the list of unprotected vendor modules.

    Args:
      msm_target: name of target platform (e.g. "pitti").

    Returns:
      The list of unprotected vendor modules for the given msm_target.
    """
    unprotected_modules_list = []
    for t, m in _unprotected_modules_map.items():
        if msm_target in t:
            unprotected_modules_list = [] + _unprotected_modules_map[msm_target]
    return unprotected_modules_list
