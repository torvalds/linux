load(":target_variants.bzl", "vm_variants")
load(":msm_kernel_vm.bzl", "define_msm_vm")

target_name = "kalama-oemvm"

def define_kalama_oemvm():
    for variant in vm_variants:
        define_msm_vm(
            msm_target = target_name,
            variant = variant,
            defconfig = "kalama_tuivm",
        )
