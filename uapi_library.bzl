load(":uapi_unpacker.bzl", "uapi_unpacker")

def define_uapi_library(target):
    """Create a header-only cc_library of the kernel's UAPI headers

    Args:
      target: name of main Bazel target (e.g. `kalama_gki`)
    """
    uapi_unpacker(
        name = "{}_uapi_unpacker".format(target),
        kernel_uapi_headers = ":{}_uapi_headers".format(target),
    )

    native.cc_library(
        name = "{}_uapi_header_library".format(target),
        hdrs = [":{}_uapi_unpacker".format(target)],
        includes = ["{}_uapi_unpacker_uapi_headers".format(target)],
    )
