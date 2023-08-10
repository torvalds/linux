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
load("@bazel_skylib//rules:write_file.bzl", "write_file")
load(":msm_common.bzl", "define_top_level_config", "gen_config_without_source_lines", "get_out_dir")
load(":msm_dtc.bzl", "define_dtc_dist")
load(":image_opts.bzl", "vm_image_opts")
load(":target_variants.bzl", "vm_variants")

def define_make_vm_dtb_img(target, dtb_list, page_size):
    compiled_dtbs = ["//msm-kernel:{}/{}".format(target, t) for t in dtb_list]
    dtb_cmd = "compiled_dtb_list=\"{}\"\n".format(" ".join(["$(location {})".format(d) for d in compiled_dtbs]))
    dtb_cmd += """
      $(location //prebuilts/kernel-build-tools:linux-x86/bin/mkdtboimg) \\
        create "$@" --page_size={page_size} $${{compiled_dtb_list}}
    """.format(page_size = page_size)

    native.genrule(
        name = "{}_vm_dtb_img".format(target),
        srcs = compiled_dtbs,
        outs = ["{}-dtb.img".format(target)],
        tools = ["//prebuilts/kernel-build-tools:linux-x86/bin/mkdtboimg"],
        cmd_bash = dtb_cmd,
    )

def _define_build_config(
        msm_target,
        variant,
        target,
        defconfig = None,
        vm_image_opts = vm_image_opts(),
        build_config_fragments = []):
    """Creates a kernel_build_config for an MSM target

    Creates a `kernel_build_config` for input to a `kernel_build` rule.

    Args:
      msm_target: name of target platform (e.g. "kalama")
      variant: variant of kernel to build (e.g. "gki")
      target: combined name (e.g. "kalama_gki")
      vm_image_opts: vm_image_opts structure containing boot image options
      build_config_fragments: build.config fragments to embed
    """

    if defconfig:
        msm_arch = defconfig
    else:
        msm_arch = msm_target.replace("-", "_")

    write_file(
        name = "{}_build_config_bazel".format(target),
        out = "build.config.msm.{}.generated".format(target),
        content = [
            'KERNEL_DIR="msm-kernel"',
            "VARIANTS=({})".format(" ".join([v.replace("-", "_") for v in vm_variants])),
            "MSM_ARCH={}".format(msm_arch),
            "VARIANT={}".format(variant.replace("-", "_")),
            "PREFERRED_USERSPACE={}".format(vm_image_opts.preferred_usespace),
            "VM_DTB_IMG_CREATE={}".format(int(vm_image_opts.vm_dtb_img_create)),
            "KERNEL_OFFSET=0x%X" % vm_image_opts.kernel_offset,
            "DTB_OFFSET=0x%X" % vm_image_opts.dtb_offset,
            "RAMDISK_OFFSET=0x%X" % vm_image_opts.ramdisk_offset,
            "CMDLINE_CPIO_OFFSET=0x%X" % vm_image_opts.cmdline_cpio_offset,
            "VM_SIZE_EXT4={}".format(vm_image_opts.vm_size_ext4),
            "DUMMY_IMG_SIZE={}".format(vm_image_opts.dummy_img_size),
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
            "build.config.msm.vm",
        ],
    )

def _define_kernel_build(
        target,
        dtb_list,
        dtbo_list,
        dtstree):
    """Creates a `kernel_build` and other associated definitions

    This is where the main kernel build target is created (e.g. `//msm-kernel:kalama_gki`).
    Many other rules will take this `kernel_build` as an input.

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
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

    # VM builds don't build compressed, so remove from list
    out_list.remove("Image.lz4")
    out_list.remove("Image.gz")

    # Add initramfs outputs
    out_list.extend([
        "usr/gen_init_cpio",
        "usr/initramfs_data.cpio",
        "usr/initramfs_inc_data",
        "scripts/sign-file",
        "certs/signing_key.x509",
    ])

    kernel_build(
        name = target,
        outs = out_list,
        build_config = ":{}_build_config".format(target),
        dtstree = dtstree,
        kmi_symbol_list = None,
        additional_kmi_symbol_lists = None,
        module_signing_key = ":signing_key",
        system_trusted_key = ":verity_cert.pem",
        abi_definition = None,
        visibility = ["//visibility:public"],
    )

    kernel_images(
        name = "{}_images".format(target),
        kernel_build = ":{}".format(target),
        boot_image_outs = [],
        build_boot = False,
        kernel_modules_install = None,
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

    msm_dist_targets = [
        # do not sort
        ":{}".format(target),
        ":{}_images".format(target),
        ":{}_merged_kernel_uapi_headers".format(target),
        ":{}_build_config".format(target),
        ":{}_vm_dtb_img".format(target),
        ":signing_key",
        ":verity_key",
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
            "**/vmlinux": "755",
            "**/Image": "755",
            "**/*.dtb*": "755",
            "**/gen_init_cpio": "755",
            "**/sign-file": "755",
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

def define_msm_vm(
        msm_target,
        variant,
        defconfig = None,
        vm_image_opts = vm_image_opts()):
    """Top-level kernel build definition macro for a VM MSM platform

    Args:
      msm_target: name of target platform (e.g. "kalama")
      variant: variant of kernel to build (e.g. "gki")
      vm_image_opts: vm_image_opts structure containing boot image options
    """

    if not variant in vm_variants:
        fail("{} not defined in target_variants.bzl vm_variants!".format(variant))

    # Enforce format of "//msm-kernel:target-foo_variant-bar" (underscore is the delimeter
    # between target and variant)
    target = msm_target.replace("_", "-") + "_" + variant.replace("_", "-")

    dtb_list = get_dtb_list(msm_target)
    dtbo_list = get_dtbo_list(msm_target)
    dtstree = get_dtstree(msm_target)
    build_config_fragments = get_build_config_fragments(msm_target)

    _define_build_config(
        msm_target,
        variant,
        target,
        defconfig,
        vm_image_opts = vm_image_opts,
        build_config_fragments = build_config_fragments,
    )

    _define_kernel_build(
        target,
        dtb_list,
        dtbo_list,
        dtstree,
    )

    _define_kernel_dist(target, msm_target, variant)

    _define_uapi_library(target)

    define_dtc_dist(target, msm_target, variant)

    # use only dtbs related to the variant for dtb image creation
    if "tuivm" in msm_target:
        seg_dtb_list = [dtb for dtb in dtb_list if "-vm-" in dtb]
    elif "oemvm" in msm_target:
        seg_dtb_list = [dtb for dtb in dtb_list if "-oemvm-" in dtb]

    define_make_vm_dtb_img(target, seg_dtb_list, vm_image_opts.dummy_img_size)

    define_extras(target, flavor = "vm")
