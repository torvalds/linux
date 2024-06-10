load(":target_variants.bzl", "le_32_variants")
load(":msm_kernel_le.bzl", "define_msm_le")
load(":image_opts.bzl", "boot_image_opts")

target_name = "mdm9607"

def define_mdm9607():
    _mdm9607_le_in_tree_modules = [
        "drivers/net/phy/at803x.ko",
        "drivers/net/phy/qca8337.ko",
        "drivers/net/ethernet/qualcomm/emac/qcom-emac.ko",
        # keep sorted
    ]

    for variant in le_32_variants:
        mod_list = _mdm9607_le_in_tree_modules

        define_msm_le(
            msm_target = target_name,
            variant = variant,
            defconfig = "build.config.msm.mdm9607",
            in_tree_module_list = mod_list,
            target_arch = "arm",
            target_variants = le_32_variants,
            boot_image_opts = boot_image_opts(
                boot_image_header_version = 2,
                base_address = 0x80000000,
                page_size = 4096,
            ),
        )
