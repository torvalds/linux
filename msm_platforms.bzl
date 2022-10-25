load(":kalama.bzl", "define_kalama")
load(":kalama_tuivm.bzl", "define_kalama_tuivm")
load(":pineapple.bzl", "define_pineapple")
load(":pineapple_tuivm.bzl", "define_pineapple_tuivm")

def define_msm_platforms():
    define_kalama()
    define_kalama_tuivm()
    define_pineapple()
    define_pineapple_tuivm()
