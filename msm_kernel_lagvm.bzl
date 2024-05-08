load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load("//build/kernel/kleaf:constants.bzl", "aarch64_outs")
load(
    "//build/kernel/kleaf:kernel.bzl",
    "kernel_build",
    "kernel_build_config",
    "kernel_compile_commands",
    "kernel_images",
    "kernel_modules_install",
    "kernel_uapi_headers_cc_library",
    "merged_kernel_uapi_headers",
    "super_image",
    "unsparsed_image",
)
load(
    "//build:msm_kernel_extensions.bzl",
    "define_extras",
    "get_build_config_fragments",
    "get_dtb_list",
    "get_dtbo_list",
    "get_dtstree",
    "get_gki_ramdisk_prebuilt_binary",
    "get_vendor_ramdisk_binaries",
)
load("@bazel_skylib//rules:write_file.bzl", "write_file")
load(":msm_common.bzl", "define_top_level_config", "gen_config_without_source_lines", "get_out_dir")
load(":msm_dtc.bzl", "define_dtc_dist")
load(":msm_abl.bzl", "define_abl_dist")
load(":avb_boot_img.bzl", "avb_sign_boot_image")
load(":image_opts.bzl", "boot_image_opts")
load(":target_variants.bzl", "lxc_variants")
load(":modules.bzl", "get_gki_modules_list")

def _define_build_config(
        msm_target,
        target,
        variant,
        boot_image_opts = boot_image_opts(),
        build_config_fragments = []):
    """Creates a kernel_build_config for an MSM target

    Creates a `kernel_build_config` for input to a `kernel_build` rule.

    Args:
      msm_target: name of target platform (e.g. "kalama")
      variant: variant of kernel to build (e.g. "gki")
    """

    # keep earlycon addr in earlycon cmdline param only when provided explicitly in target's bazel file
    # otherwise, rely on stdout-path
    earlycon_param = "={}".format(boot_image_opts.earlycon_addr) if boot_image_opts.earlycon_addr != None else ""
    earlycon_param = '[ "$KERNEL_CMDLINE_CONSOLE_AUTO" != "0" ] && KERNEL_VENDOR_CMDLINE+=\' earlycon{} \''.format(earlycon_param)

    write_file(
        name = "{}_build_config_bazel".format(target),
        out = "build.config.msm.{}.generated".format(target),
        content = [
            'KERNEL_DIR="msm-kernel"',
            "VARIANTS=({})".format(" ".join([v.replace("-", "_") for v in lxc_variants])),
            "MSM_ARCH={}".format(msm_target.replace("-", "_")),
            "VARIANT={}".format(variant.replace("-", "_")),
            "ABL_SRC=bootable/bootloader/edk2",
            "BOOT_IMAGE_HEADER_VERSION={}".format(boot_image_opts.boot_image_header_version),
            "BASE_ADDRESS=0x%X" % boot_image_opts.base_address,
            "PAGE_SIZE={}".format(boot_image_opts.page_size),
            "BUILD_VENDOR_DLKM=1",
            "PREPARE_SYSTEM_DLKM=1",
            "SUPER_IMAGE_SIZE=0x%X" % boot_image_opts.super_image_size,
            "TRIM_UNUSED_MODULES=1",
            "BUILD_INIT_BOOT_IMG=1",
            "LZ4_RAMDISK={}".format(int(boot_image_opts.lz4_ramdisk)),
            '[ -z "$DT_OVERLAY_SUPPORT" ] && DT_OVERLAY_SUPPORT=1',
            earlycon_param,
            "KERNEL_VENDOR_CMDLINE+=' {} '".format(" ".join(boot_image_opts.kernel_vendor_cmdline_extras)),
            "VENDOR_BOOTCONFIG+='androidboot.first_stage_console=1 androidboot.hardware=qcom_kp'",
            "",  # Needed for newline at end of file
        ],
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
            "build.config.msm.autoghgvm",
        ],
    )

    board_cmdline_extras = " ".join(boot_image_opts.board_kernel_cmdline_extras)
    if board_cmdline_extras:
        write_file(
            name = "{}_extra_cmdline".format(target),
            out = "board_extra_cmdline_{}".format(target),
            content = [board_cmdline_extras, ""],
        )

    board_bc_extras = " ".join(boot_image_opts.board_bootconfig_extras)
    if board_bc_extras:
        write_file(
            name = "{}_extra_bootconfig".format(target),
            out = "board_extra_bootconfig_{}".format(target),
            content = [board_bc_extras, ""],
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

    # Remove unused compressed formats
    out_list.remove("Image.lz4")
    out_list.remove("Image.gz")

    kernel_build(
        name = target,
        module_outs = in_tree_module_list,
        module_implicit_outs = get_gki_modules_list("arm64"),
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

def _define_image_build(
        target,
        msm_target,
        base_kernel,
        build_boot = True,
        build_dtbo = False,
        build_initramfs = False,
        boot_image_opts = boot_image_opts(),
        boot_image_outs = None,
        dtbo_list = [],
        vendor_ramdisk_binaries = None,
        gki_ramdisk_prebuilt_binary = None,
        in_tree_module_list = []):
    """Creates a `kernel_images` target which will generate bootable device images

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
      msm_target: name of target platform (e.g. "kalama")
      base_kernel: kernel_build base kernel
      build_boot: whether to build a boot image
      build_dtbo: whether to build a dtbo image
      build_initramfs: whether to build an initramfs image
      boot_image_outs: boot image outputs,
      dtbo_list: list of device tree overlay blobs to be built into `dtbo.img`
      vendor_ramdisk_binaries: ramdisk binaries (cpio archives)
    """

    # Generate the vendor_dlkm list
    native.genrule(
        name = "{}_vendor_dlkm_modules_list_generated".format(target),
        srcs = [],
        outs = ["modules.list.vendor_dlkm.{}".format(target)],
        cmd_bash = """
          touch "$@"
          for module in {mod_list}; do
            basename "$$module" >> "$@"
          done
        """.format(mod_list = " ".join(in_tree_module_list)),
    )

    kernel_images(
        name = "{}_images".format(target),
        kernel_modules_install = ":{}_modules_install".format(target),
        kernel_build = ":{}".format(target),
        base_kernel_images = "{}_images".format(base_kernel),
        build_boot = build_boot,
        build_dtbo = build_dtbo,
        build_initramfs = build_initramfs,
        build_vendor_boot = False,
        build_vendor_kernel_boot = False,
        build_vendor_dlkm = False,
        build_system_dlkm = False,
        modules_list = "modules.list.msm.{}".format(msm_target),
        system_dlkm_modules_list = "android/gki_system_dlkm_modules",
        vendor_dlkm_modules_list = ":{}_vendor_dlkm_modules_list_generated".format(target),
        system_dlkm_modules_blocklist = "modules.systemdlkm_blocklist.msm.{}".format(msm_target),
        vendor_dlkm_modules_blocklist = "modules.vendor_blocklist.msm.{}".format(msm_target),
        dtbo_srcs = [":{}/".format(target) + d for d in dtbo_list] if dtbo_list else None,
        vendor_ramdisk_binaries = vendor_ramdisk_binaries,
        gki_ramdisk_prebuilt_binary = gki_ramdisk_prebuilt_binary,
        boot_image_outs = boot_image_outs,
        deps = [
            "modules.list.msm.{}".format(msm_target),
            "modules.vendor_blocklist.msm.{}".format(msm_target),
            "modules.systemdlkm_blocklist.msm.{}".format(msm_target),
            "android/gki_system_dlkm_modules",
        ],
    )

    if build_boot:
        artifacts = "{}_images".format(target)
    else:
        artifacts = "{}_gki_artifacts".format(base_kernel)

    avb_sign_boot_image(
        name = "{}_avb_sign_boot_image".format(target),
        artifacts = artifacts,
        avbtool = "//prebuilts/kernel-build-tools:linux-x86/bin/avbtool",
        key = "//tools/mkbootimg:gki/testdata/testkey_rsa4096.pem",
        props = [
            "com.android.build.boot.os_version:13",
            "com.android.build.boot.security_patch:2023-05-05",
        ],
        boot_partition_size = int(boot_image_opts.boot_partition_size),
    )

    native.filegroup(
        name = "{}_system_dlkm_image_file".format(target),
        srcs = ["{}_images".format(base_kernel)],
        output_group = "system_dlkm.img",
    )

    native.filegroup(
        name = "{}_vendor_dlkm_image_file".format(target),
        srcs = [":{}_images".format(target)],
        output_group = "vendor_dlkm.img",
    )

    super_image(
        name = "{}_super_image".format(target),
        system_dlkm_image = ":{}_system_dlkm_image_file".format(target),
        vendor_dlkm_image = ":{}_vendor_dlkm_image_file".format(target),
    )

    unsparsed_image(
        name = "{}_unsparsed_image".format(target),
        src = "{}_super_image".format(target),
        out = "super_unsparsed.img",
    )

def _define_kernel_dist(
        target,
        msm_target,
        variant,
        boot_image_opts = boot_image_opts()):
    """Creates distribution targets for kernel builds

    When Bazel builds everything, the outputs end up buried in `bazel-bin`.
    These rules are used to copy the build artifacts to `out/msm-kernel-<target>/dist/`
    with proper permissions, etc.

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
      msm_target: name of just the platform target (e.g. `kalama`)
      variant: name of just the variant (e.g. `gki`)
    """

    dist_dir = get_out_dir(msm_target, variant) + "/dist"

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

def _define_uapi_library(target):
    """Define a cc_library for userspace programs to use

    Args:
      target: kernel_build target name (e.g. "kalama_gki")
    """
    kernel_uapi_headers_cc_library(
        name = "{}_uapi_header_library".format(target),
        kernel_build = ":{}".format(target),
    )

def define_msm_lagvm(
        msm_target,
        variant,
        in_tree_module_list,
        boot_image_opts = boot_image_opts()):
    """Top-level kernel build definition macro for an MSM platform

    Args:
      msm_target: name of target platform (e.g. "kalama")
      variant: variant of kernel to build (e.g. "gki")
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

    # Always set base_kernel to kernel_aarch64
    base_kernel = "//common:kernel_aarch64"

    lxc_target = msm_target.split("-")[0]

    dtb_list = get_dtb_list(lxc_target)
    dtbo_list = get_dtbo_list(lxc_target)
    dtstree = get_dtstree(lxc_target)
    vendor_ramdisk_binaries = get_vendor_ramdisk_binaries(target)
    gki_ramdisk_prebuilt_binary = get_gki_ramdisk_prebuilt_binary()
    build_config_fragments = get_build_config_fragments(lxc_target)

    _define_build_config(
        lxc_target,
        target,
        variant,
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

    _define_image_build(
        target,
        msm_target,
        base_kernel,
        # When building a GKI target, we take the kernel and boot.img directly from
        # common, so no need to build here.
        build_boot = True,
        build_dtbo = True,
        build_initramfs = True,
        dtbo_list = dtbo_list,
        vendor_ramdisk_binaries = vendor_ramdisk_binaries,
        boot_image_opts = boot_image_opts,
        boot_image_outs = None if dtb_list else ["boot.img", "init_boot.img"],
        in_tree_module_list = in_tree_module_list,
    )

    _define_kernel_dist(
        target,
        msm_target,
        variant,
        boot_image_opts = boot_image_opts,
    )

    _define_uapi_library(target)

    define_abl_dist(target, msm_target, variant)

    define_dtc_dist(target, msm_target, variant)

    define_extras(target)
