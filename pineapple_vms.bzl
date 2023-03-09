load(":target_variants.bzl", "vm_types", "vm_variants")
load("//msm-kernel:msm_common.bzl", "get_out_dir")
load("//build/bazel_common_rules/dist:dist.bzl", "copy_to_dist_dir")
load("//build:msm_kernel_extensions.bzl", "define_combined_vm_image")
load(":image_opts.bzl", "vm_image_opts")

target_name = "pineapple-vms"

def define_pineapple_vms(vm_image_opts = vm_image_opts()):
    base_target = "pineapple-tuivm"
    for variant in vm_variants:
        base_tv = "{}_{}".format(base_target, variant)

        out_dtb_list = [":pineapple-{}_{}_vm_dtb_img".format(vt, variant) for vt in vm_types]
        dist_targets = [
            # do not sort
            ":{}".format(base_tv),
            ":{}_images".format(base_tv),
            ":{}_merged_kernel_uapi_headers".format(base_tv),
            ":{}_build_config".format(base_tv),
            ":signing_key",
            ":verity_key",
        ] + out_dtb_list

        copy_to_dist_dir(
            name = "{}_{}_dist".format(target_name, variant),
            data = dist_targets,
            dist_dir = "{}/dist".format(get_out_dir(target_name, variant)),
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
        )

        define_combined_vm_image(target_name, variant, vm_image_opts.vm_size_ext4)
