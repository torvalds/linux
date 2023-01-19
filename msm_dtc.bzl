load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load(":msm_common.bzl", "get_out_dir")

def define_dtc_dist(target, msm_target, variant):
    """Create distribution targets for device tree compiler and associated tools

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
    """
    dtc_bin_targets = [
        "@dtc//:dtc",
        "@dtc//:fdtget",
        "@dtc//:fdtput",
        "@dtc//:fdtdump",
        "@dtc//:fdtoverlay",
        "@dtc//:fdtoverlaymerge",
    ]
    dtc_lib_targets = [
        "@dtc//:dtc_gen",
        "@dtc//:libfdt",
    ]
    dtc_inc_targets = [
        "@dtc//:libfdt/fdt.h",
        "@dtc//:libfdt/libfdt.h",
        "@dtc//:libfdt/libfdt_env.h",
    ]

    dtc_tar_cmd = "mkdir -p bin lib include\n"
    for label in dtc_bin_targets:
        dtc_tar_cmd += "cp $(locations {}) bin/\n".format(label)
    for label in dtc_lib_targets:
        dtc_tar_cmd += "cp $(locations {}) lib/\n".format(label)
    for label in dtc_inc_targets:
        dtc_tar_cmd += "cp $(locations {}) include/\n".format(label)
    dtc_tar_cmd += """
      chmod 755 bin/* lib/*
      chmod 644 include/*
      tar -czf "$@" bin lib include
    """

    native.genrule(
        name = "{}_dtc_tarball".format(target),
        srcs = dtc_bin_targets + dtc_lib_targets + dtc_inc_targets,
        outs = ["{}_dtc.tar.gz".format(target)],
        cmd = dtc_tar_cmd,
    )

    native.alias(
        name = "{}_dtc".format(target),
        actual = ":{}_dtc_tarball".format(target),
    )

    copy_to_dist_dir(
        name = "{}_dtc_dist".format(target),
        archives = [":{}_dtc_tarball".format(target)],
        dist_dir = "{}/host".format(get_out_dir(msm_target, variant)),
        flat = True,
        wipe_dist_dir = True,
        log = "info",
    )
