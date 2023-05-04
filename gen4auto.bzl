load(":target_variants.bzl", "qx_variants")
load(":msm_kernel_qx.bzl", "define_msm_qx")
load(":image_opts.bzl", "boot_image_opts")

target_name = "gen4auto"

def define_gen4auto():
    _gen4auto_qx_in_tree_modules = [
	# keep sorted
    ]

    for variant in qx_variants:
        mod_list = _gen4auto_qx_in_tree_modules

        define_msm_qx(
            msm_target = target_name,
            variant = variant,
            defconfig = "build.config.msm.auto",
            in_tree_module_list = mod_list,
            boot_image_opts = boot_image_opts(
                boot_image_header_version = 2,
                base_address = 0x80000000,
                page_size = 4096),
        )
