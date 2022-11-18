load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")

def define_abl_dist(target):
    """Creates ABL distribution target

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
    """
    native.alias(
        name = "{}_abl".format(target),
        actual = "//bootable/bootloader/edk2:{}_abl".format(target),
    )

    copy_to_dist_dir(
        name = "{}_abl_dist".format(target),
        archives = ["{}_abl".format(target)],
        dist_dir = "out/msm-kernel-{}/dist".format(target.replace("_", "-")),
        flat = True,
        wipe_dist_dir = False,
        log = "info",
    )
