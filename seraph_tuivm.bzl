load(":msm_kernel_vm.bzl", "define_msm_vm")
load(":target_variants.bzl", "vm_variants")

target_name = "seraph-tuivm"

def define_seraph_tuivm():
    for variant in vm_variants:
        define_msm_vm(
            msm_target = target_name,
            variant = variant,
        )
