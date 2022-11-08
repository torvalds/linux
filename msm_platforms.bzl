load(":kalama.bzl", "define_kalama")
load(":kalama_tuivm.bzl", "define_kalama_tuivm")
load(":pineapple.bzl", "define_pineapple")
load(":pineapple_tuivm.bzl", "define_pineapple_tuivm")
load("//build/kernel/kleaf:kernel.bzl", "ddk_headers")
load("//build/bazel_extensions:msm_kernel_extensions.bzl", "define_top_level_rules")

def _define_ddk_headers():
    ddk_headers(
        name = "all_headers",
        hdrs = native.glob([
            "arch/arm64/include/**/*.h",
            "include/**/*.h",
        ]),
        includes = [
            "arch/arm64/include",
            "arch/arm64/include/uapi",
            "include",
            "include/uapi",
        ],
    )

def define_msm_platforms():
    _define_ddk_headers()
    define_top_level_rules()
    define_kalama()
    define_kalama_tuivm()
    define_pineapple()
    define_pineapple_tuivm()
