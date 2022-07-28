load(":kalama.bzl", "define_kalama")
load(":pineapple.bzl", "define_pineapple")

def define_msm_platforms():
    define_kalama()
    define_pineapple()
