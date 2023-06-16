load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load("//build/bazel_common_rules/test_mappings:test_mappings.bzl", "test_mappings_dist")
load("//build/kernel/kleaf:constants.bzl", "aarch64_outs")
load(
    "//build/kernel/kleaf:kernel.bzl",
    "kernel_build",
    "kernel_build_config",
    "kernel_compile_commands",
    "kernel_images",
    "kernel_modules_install",
    "merged_kernel_uapi_headers",
)
load(
    "//build:msm_kernel_extensions.bzl",
    "define_extras",
    "get_build_config_fragments",
    "get_dtb_list",
    "get_dtbo_list",
    "get_dtstree",
    "get_vendor_ramdisk_binaries",
)
load(":msm_common.bzl", "define_top_level_config", "gen_config_without_source_lines", "get_out_dir")
load(":msm_dtc.bzl", "define_dtc_dist")
load(":msm_abl.bzl", "define_abl_dist")
load(":image_opts.bzl", "boot_image_opts")
load(":target_variants.bzl", "lxc_variants")

def _define_build_config(
        msm_target,
        target,
        variant,
        defconfig,
        boot_image_opts = boot_image_opts(),
        build_config_fragments = []):
    """Creates a kernel_build_config for an MSM target

    Creates a `kernel_build_config` for input to a `kernel_build` rule.

    Args:
      msm_target: name of target platform (e.g. "kalama")
      variant: variant of kernel to build (e.g. "gki")
    """

    gen_config_command = """
      cat << 'EOF' > "$@"
KERNEL_DIR="msm-kernel"
SOC_NAME="monaco_auto"
VARIANTS=(%s)
MSM_ARCH=%s
VARIANT=%s
ABL_SRC=bootable/bootloader/edk2
BOOT_IMAGE_HEADER_VERSION=%d
BASE_ADDRESS=0x%X
PAGE_SIZE=%d
TARGET_HAS_SEPARATE_RD=1
PREFERRED_USERSPACE=lxc
BUILD_BOOT_IMG=1
SKIP_UNPACKING_RAMDISK=1
BUILD_INITRAMFS=1
[ -z "$${DT_OVERLAY_SUPPORT}" ] && DT_OVERLAY_SUPPORT=1
KERNEL_VENDOR_CMDLINE+=' console=ttyMSM0,115200n8 earlycon=qcom_geni,0x99c000 qcom_geni_serial.con_enabled=1 loglevel=8 nokaslr printk.devkmsg=on root=/dev/ram0 rw rootwait'
EOF
    """ % (
        " ".join([v.replace("-", "_") for v in lxc_variants]),  # VARIANTS
        msm_target.replace("-", "_"),
        variant.replace("-", "_"),
        boot_image_opts.boot_image_header_version,
        boot_image_opts.base_address,
        boot_image_opts.page_size,
    )

    # Generate the build config
    native.genrule(
        name = "{}_build_config_bazel".format(target),
        srcs = [],
        outs = ["build.config.msm.{}.generated".format(target)],
        cmd_bash = gen_config_command,
    )

    top_level_config = define_top_level_config(target)
    common_config = gen_config_without_source_lines("build.config.common", target)

    # Concatenate build config fragments to form the final config
    kernel_build_config(
        name = "{}_build_config".format(target),
        srcs = [
            # do not sort
            top_level_config,
            "build.config.constants",
            common_config,
            "build.config.aarch64",
            ":{}_build_config_bazel".format(target),
        ] + [fragment for fragment in build_config_fragments] + [
            "build.config.msm.common",
            defconfig,
        ],
    )

def _define_kernel_build(
        target,
        in_tree_module_list,
        dtb_list,
        dtbo_list,
        dtstree):
    """Creates a `kernel_build` and other associated definitions

    This is where the main kernel build target is created (e.g. `//msm-kernel:kalama_gki`).
    Many other rules will take this `kernel_build` as an input.

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
      in_tree_module_list: list of in-tree modules
      dtb_list: device tree blobs expected to be built
      dtbo_list: device tree overlay blobs expected to be built
    """
    out_list = [".config", "Module.symvers"]

    if dtb_list:
        out_list += dtb_list
    if dtbo_list:
        out_list += dtbo_list

    # Add basic kernel outputs
    out_list += aarch64_outs

    # LE builds don't build compressed, so remove from list
    out_list.remove("Image.lz4")
    out_list.remove("Image.gz")

    kernel_build(
        name = target,
        module_outs = in_tree_module_list,
        outs = out_list,
        build_config = ":{}_build_config".format(target),
        dtstree = dtstree,
        kmi_symbol_list = None,
        additional_kmi_symbol_lists = None,
        abi_definition = None,
        visibility = ["//visibility:public"],
    )

    kernel_modules_install(
        name = "{}_modules_install".format(target),
        kernel_build = ":{}".format(target),
    )

    merged_kernel_uapi_headers(
        name = "{}_merged_kernel_uapi_headers".format(target),
        kernel_build = ":{}".format(target),
    )

    kernel_compile_commands(
        name = "{}_compile_commands".format(target),
        kernel_build = ":{}".format(target),
    )

def _define_kernel_dist(target, msm_target, variant):
    """Creates distribution targets for kernel builds

    When Bazel builds everything, the outputs end up buried in `bazel-bin`.
    These rules are used to copy the build artifacts to `out/msm-kernel-<target>/dist/`
    with proper permissions, etc.

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
    """

    dist_dir = get_out_dir(msm_target, variant) + "/dist"
    lxc_target = msm_target.split("-")[0]

    msm_dist_targets = [
        # do not sort
        ":{}".format(target),
        ":{}_images".format(target),
        ":{}_merged_kernel_uapi_headers".format(target),
        ":{}_build_config".format(target),
    ]

    copy_to_dist_dir(
        name = "{}_dist".format(target),
        data = msm_dist_targets,
        dist_dir = dist_dir,
        flat = True,
        wipe_dist_dir = True,
        allow_duplicate_filenames = True,
        mode_overrides = {
            # do not sort
            "**/*.elf": "755",
            "**/vmlinux": "755",
            "**/Image": "755",
            "**/*.dtb*": "755",
            "**/LinuxLoader*": "755",
            "**/*": "644",
        },
        log = "info",
    )

    native.alias(
        name = "{}_test_mapping".format(target),
        actual = ":{}_dist".format(target),
    )

    test_mappings_dist(
        name = "{}_test_mapping_dist".format(target),
        dist_dir = dist_dir,
    )

def define_msm_lxc(
        msm_target,
        variant,
        defconfig,
        in_tree_module_list,
        boot_image_opts = boot_image_opts()):
    """Top-level kernel build definition macro for an MSM platform

    Args:
      msm_target: name of target platform (e.g. "gen4auto")
      variant: variant of kernel to build (e.g. "perf-defconfig")
      in_tree_module_list: list of in-tree modules
      boot_image_header_version: boot image header version (for `boot.img`)
      base_address: edk2 base address
      page_size: kernel page size
      super_image_size: size of super image partition
      lz4_ramdisk: whether to use an lz4-compressed ramdisk
    """

    if not variant in lxc_variants:
        fail("{} not defined in target_variants.bzl lxc_variants!".format(variant))

    # Enforce format of "//msm-kernel:target-foo_variant-bar" (underscore is the delimeter
    # between target and variant)
    target = msm_target.replace("_", "-") + "_" + variant.replace("_", "-")
    lxc_target = msm_target.split("-")[0]

    dtb_list = get_dtb_list(lxc_target)
    dtbo_list = get_dtbo_list(lxc_target)
    dtstree = get_dtstree(lxc_target)
    vendor_ramdisk_binaries = get_vendor_ramdisk_binaries(target, flavor = "le")
    build_config_fragments = get_build_config_fragments(lxc_target)

    _define_build_config(
        lxc_target,
        target,
        variant,
        defconfig,
        boot_image_opts = boot_image_opts,
        build_config_fragments = build_config_fragments,
    )

    _define_kernel_build(
        target,
        in_tree_module_list,
        dtb_list,
        dtbo_list,
        dtstree,
    )

    kernel_images(
        name = "{}_images".format(target),
        kernel_modules_install = ":{}_modules_install".format(target),
        kernel_build = ":{}".format(target),
        build_boot = True,
        build_dtbo = True if dtbo_list else False,
        build_initramfs = True,
        dtbo_srcs = [":{}/".format(target) + d for d in dtbo_list] if dtbo_list else None,
        vendor_ramdisk_binaries = vendor_ramdisk_binaries,
        boot_image_outs = ["boot.img"],
    )

    _define_kernel_dist(target, msm_target, variant)

    define_abl_dist(target, msm_target, variant)

    define_dtc_dist(target, msm_target, variant)
