load(":target_variants.bzl", "lxc_variants")
load(":msm_kernel_lxc.bzl", "define_msm_lxc")
load(":image_opts.bzl", "boot_image_opts")

target_name = "gen4auto"

def define_gen4auto_lxc():
    _gen4auto_lxc_in_tree_modules = [
        # keep sorted
    ]

    for variant in lxc_variants:
        mod_list = _gen4auto_lxc_in_tree_modules

        define_msm_lxc(
            msm_target = target_name,
            variant = variant,
            defconfig = "build.config.msm.auto",
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                boot_image_header_version = 2,
                base_address = 0x80000000,
                page_size = 4096,
            ),
        )
