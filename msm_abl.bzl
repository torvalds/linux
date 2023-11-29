load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load(":msm_common.bzl", "get_out_dir")

def define_abl_dist(target, msm_target, variant):
    """Creates ABL distribution target

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
    """
    if msm_target == "autogvm":
        return
    if msm_target == "mdm9607":
        return

    native.alias(
        name = "{}_abl".format(target),
        actual = "//bootable/bootloader/edk2:{}_abl".format(target),
    )

    copy_to_dist_dir(
        name = "{}_abl_dist".format(target),
        archives = ["{}_abl".format(target)],
        dist_dir = "{}/dist".format(get_out_dir(msm_target, variant)),
        flat = True,
        wipe_dist_dir = False,
        log = "info",
    )
