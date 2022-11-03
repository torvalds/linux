load(":kalama.bzl", "define_kalama")
load(":kalama_tuivm.bzl", "define_kalama_tuivm")
load(":pineapple.bzl", "define_pineapple")
load(":pineapple_tuivm.bzl", "define_pineapple_tuivm")
load("//build/bazel_extensions:msm_kernel_extensions.bzl", "define_top_level_rules")

def define_msm_platforms():
    define_top_level_rules()
    define_kalama()
    define_kalama_tuivm()
    define_pineapple()
    define_pineapple_tuivm()
