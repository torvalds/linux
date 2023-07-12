load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load("//build/bazel_common_rules/test_mappings:test_mappings.bzl", "test_mappings_dist")
load(
    "//build/kernel/kleaf:kernel.bzl",
    "kernel_abi",
    "kernel_abi_dist",
    "kernel_build",
    "kernel_build_config",
    "kernel_compile_commands",
    "kernel_images",
    "kernel_modules_install",
    "kernel_uapi_headers_cc_library",
    "merged_kernel_uapi_headers",
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
load(":msm_common.bzl", "define_top_level_config", "gen_config_without_source_lines", "get_out_dir")
load(":msm_dtc.bzl", "define_dtc_dist")
load(":msm_abl.bzl", "define_abl_dist")
load(":super_image.bzl", "super_image")
load(":avb_boot_img.bzl", "avb_sign_boot_image")
load(":image_opts.bzl", "boot_image_opts")
load(":target_variants.bzl", "la_variants")
load(":modules.bzl", "COMMON_GKI_MODULES_LIST")

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

    gen_config_command = """
      cat << 'EOF' > "$@"
KERNEL_DIR="msm-kernel"
VARIANTS=(%s)
MSM_ARCH=%s
VARIANT=%s
ABL_SRC=bootable/bootloader/edk2
BOOT_IMAGE_HEADER_VERSION=%d
BASE_ADDRESS=0x%X
PAGE_SIZE=%d
BUILD_VENDOR_DLKM=1
PREPARE_SYSTEM_DLKM=1
SUPER_IMAGE_SIZE=0x%X
TRIM_UNUSED_MODULES=1
BUILD_INIT_BOOT_IMG=1
LZ4_RAMDISK=%d
[ -z "$$DT_OVERLAY_SUPPORT" ] && DT_OVERLAY_SUPPORT=1

if [ "$$KERNEL_CMDLINE_CONSOLE_AUTO" != "0" ]; then
    KERNEL_VENDOR_CMDLINE+=' earlycon=%s '
fi

KERNEL_VENDOR_CMDLINE+=' %s '
VENDOR_BOOTCONFIG+='androidboot.first_stage_console=1 androidboot.hardware=qcom_kp'
EOF
    """ % (
        " ".join(la_variants),
        msm_target.replace("-", "_"),
        variant.replace("-", "_"),
        boot_image_opts.boot_image_header_version,
        boot_image_opts.base_address,
        boot_image_opts.page_size,
        boot_image_opts.super_image_size,
        int(boot_image_opts.lz4_ramdisk),
        boot_image_opts.earlycon_addr,
        " ".join(boot_image_opts.kernel_vendor_cmdline_extras),
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
            "build.config.msm.gki",
        ],
    )

    board_cmdline_extras = " ".join(boot_image_opts.board_kernel_cmdline_extras)
    if board_cmdline_extras:
        native.genrule(
            name = "{}_extra_cmdline".format(target),
            outs = ["board_extra_cmdline_{}".format(target)],
            cmd_bash = """
                echo {} > "$@"
            """.format(board_cmdline_extras),
        )

    board_bc_extras = " ".join(boot_image_opts.board_bootconfig_extras)
    if board_bc_extras:
        native.genrule(
            name = "{}_extra_bootconfig".format(target),
            outs = ["board_extra_bootconfig_{}".format(target)],
            cmd_bash = """
                echo {} > "$@"
            """.format(board_bc_extras),
        )

def _define_kernel_build(
        target,
        base_kernel,
        in_tree_module_list,
        dtb_list,
        dtbo_list,
        dtstree,
        define_abi_targets,
        kmi_enforced):
    """Creates a `kernel_build` and other associated definitions

    This is where the main kernel build target is created (e.g. `//msm-kernel:kalama_gki`).
    Many other rules will take this `kernel_build` as an input.

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
      base_kernel: base kernel to pass into `kernel_build` (e.g. `//common:kernel_aarch64`)
      in_tree_module_list: list of in-tree modules
      dtb_list: device tree blobs expected to be built
      dtbo_list: device tree overlay blobs expected to be built
      define_abi_targets: boolean determining if ABI targets should be defined
      kmi_enforced: boolean determining if the KMI contract should be enforced
    """
    out_list = [".config", "Module.symvers"]
    if dtb_list:
        out_list += dtb_list
    if dtbo_list:
        out_list += dtbo_list

    kernel_build(
        name = target,
        module_outs = in_tree_module_list,
        module_implicit_outs = COMMON_GKI_MODULES_LIST,
        outs = out_list,
        build_config = ":{}_build_config".format(target),
        dtstree = dtstree,
        base_kernel = base_kernel,
        kmi_symbol_list = "android/abi_gki_aarch64_qcom" if define_abi_targets else None,
        additional_kmi_symbol_lists = ["{}_all_kmi_symbol_lists".format(base_kernel)] if define_abi_targets else None,
        protected_exports_list = "android/abi_gki_protected_exports_aarch64" if define_abi_targets else None,
        protected_modules_list = "android/gki_aarch64_protected_modules" if define_abi_targets else None,
        collect_unstripped_modules = define_abi_targets,
        visibility = ["//visibility:public"],
    )

    if define_abi_targets:
        kernel_abi(
            name = "{}_abi".format(target),
            kernel_build = ":{}".format(target),
            define_abi_targets = True,
            kmi_enforced = kmi_enforced,
            module_grouping = False,
            kmi_symbol_list_add_only = True,
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
        build_vendor_boot = False,
        build_vendor_kernel_boot = False,
        build_vendor_dlkm = True,
        build_system_dlkm = False,
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
      build_vendor_boot: whether to build a vendor boot image
      build_vendor_kernel_boot: whether to build a vendor kernel boot image
      build_vendor_dlkm: whether to build a vendor dlkm image
      build_system_dlkm: whether to build a system dlkm image
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
        build_vendor_boot = build_vendor_boot,
        build_vendor_kernel_boot = build_vendor_kernel_boot,
        build_vendor_dlkm = build_vendor_dlkm,
        build_system_dlkm = build_system_dlkm,
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

    # Defined separately because `super.img` is not generated by upstream kleaf
    super_image(
        name = "{}_super_image".format(target),
        kernel_modules_install = "{}_modules_install".format(target),
        deps = [
            "{}_images_system_dlkm_image".format(base_kernel),
            ":{}_images_vendor_dlkm_image".format(target),
        ],
    )

def _define_kernel_dist(
        target,
        msm_target,
        variant,
        base_kernel,
        define_abi_targets,
        boot_image_opts = boot_image_opts()):
    """Creates distribution targets for kernel builds

    When Bazel builds everything, the outputs end up buried in `bazel-bin`.
    These rules are used to copy the build artifacts to `out/msm-kernel-<target>/dist/`
    with proper permissions, etc.

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
      msm_target: name of just the platform target (e.g. `kalama`)
      variant: name of just the variant (e.g. `gki`)
      base_kernel: base kernel to fetch artifacts from (e.g. `//common:kernel_aarch64`)
      define_abi_targets: boolean determining if ABI targets should be defined
    """

    dist_dir = get_out_dir(msm_target, variant) + "/dist"

    msm_dist_targets = [base_kernel]

    if define_abi_targets:
        msm_dist_targets.append("{}_gki_artifacts".format(base_kernel))

    msm_dist_targets.extend([
        # do not sort
        "{}_images_system_dlkm_image".format(base_kernel),
        "{}_headers".format(base_kernel),
        ":{}".format(target),
        ":{}_images".format(target),
        ":{}_super_image".format(target),
        ":{}_merged_kernel_uapi_headers".format(target),
        ":{}_build_config".format(target),
    ])

    msm_dist_targets.append("{}_avb_sign_boot_image".format(target))

    board_cmdline_extras = " ".join(boot_image_opts.board_kernel_cmdline_extras)
    if board_cmdline_extras:
        msm_dist_targets.append("{}_extra_cmdline".format(target))

    board_bc_extras = " ".join(boot_image_opts.board_bootconfig_extras)
    if board_bc_extras:
        msm_dist_targets.append("{}_extra_bootconfig".format(target))

    if define_abi_targets:
        kernel_abi_dist(
            name = "{}_dist".format(target),
            kernel_abi = ":{}_abi".format(target),
            kernel_build_add_vmlinux = False,
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
    else:
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

def _define_uapi_library(target):
    """Define a cc_library for userspace programs to use

    Args:
      target: kernel_build target name (e.g. "kalama_gki")
    """
    kernel_uapi_headers_cc_library(
        name = "{}_uapi_header_library".format(target),
        kernel_build = ":{}".format(target),
    )

def define_msm_la(
        msm_target,
        variant,
        in_tree_module_list,
        kmi_enforced = True,
        boot_image_opts = boot_image_opts()):
    """Top-level kernel build definition macro for an MSM platform

    Args:
      msm_target: name of target platform (e.g. "kalama")
      variant: variant of kernel to build (e.g. "gki")
      in_tree_module_list: list of in-tree modules
      kmi_enforced: boolean determining if the KMI contract should be enforced
      boot_image_header_version: boot image header version (for `boot.img`)
      base_address: edk2 base address
      page_size: kernel page size
      super_image_size: size of super image partition
      lz4_ramdisk: whether to use an lz4-compressed ramdisk
    """

    if not variant in la_variants:
        fail("{} not defined in target_variants.bzl la_variants!".format(variant))

    # Enforce format of "//msm-kernel:target-foo_variant-bar" (underscore is the delimeter
    # between target and variant)
    target = msm_target.replace("_", "-") + "_" + variant.replace("_", "-")

    if variant == "consolidate":
        base_kernel = "//common:kernel_aarch64_consolidate"
        define_abi_targets = False
    else:
        base_kernel = "//common:kernel_aarch64"
        define_abi_targets = True

    dtb_list = get_dtb_list(msm_target)
    dtbo_list = get_dtbo_list(msm_target)
    dtstree = get_dtstree(msm_target)
    vendor_ramdisk_binaries = get_vendor_ramdisk_binaries(target)
    gki_ramdisk_prebuilt_binary = get_gki_ramdisk_prebuilt_binary()
    build_config_fragments = get_build_config_fragments(msm_target)

    _define_build_config(
        msm_target,
        target,
        variant,
        boot_image_opts = boot_image_opts,
        build_config_fragments = build_config_fragments,
    )

    _define_kernel_build(
        target,
        base_kernel,
        in_tree_module_list,
        dtb_list,
        dtbo_list,
        dtstree,
        define_abi_targets,
        kmi_enforced,
    )

    _define_image_build(
        target,
        msm_target,
        base_kernel,
        # When building a GKI target, we take the kernel and boot.img directly from
        # common, so no need to build here.
        build_boot = False if define_abi_targets else True,
        build_dtbo = True if dtbo_list else False,
        build_initramfs = True,
        build_vendor_boot = True,
        dtbo_list = dtbo_list,
        vendor_ramdisk_binaries = vendor_ramdisk_binaries,
        gki_ramdisk_prebuilt_binary = gki_ramdisk_prebuilt_binary,
        boot_image_opts = boot_image_opts,
        boot_image_outs = None if dtb_list else ["boot.img", "init_boot.img"],
        in_tree_module_list = in_tree_module_list,
    )

    _define_kernel_dist(
        target,
        msm_target,
        variant,
        base_kernel,
        define_abi_targets,
        boot_image_opts = boot_image_opts,
    )

    _define_uapi_library(target)

    define_abl_dist(target, msm_target, variant)

    define_dtc_dist(target, msm_target, variant)

    define_extras(target)
