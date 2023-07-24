load(":target_variants.bzl", "le_variants")
load(":msm_kernel_le.bzl", "define_msm_le")
load(":image_opts.bzl", "boot_image_opts")

target_name = "pineapple-allyes"

def define_pineapple_allyes():
    _pineapple_le_in_tree_modules = [
        # keep sorted
    ]

    for variant in le_variants:
        mod_list = _pineapple_le_in_tree_modules

        define_msm_le(
            msm_target = target_name,
            variant = variant,
            defconfig = "build.config.allyesconfig",
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                boot_image_header_version = 2,
                base_address = 0x80000000,
                page_size = 4096,
            ),
        )
