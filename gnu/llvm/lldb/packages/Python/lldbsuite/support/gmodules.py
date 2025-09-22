# System modules
import os
import re


GMODULES_SUPPORT_MAP = {}
GMODULES_HELP_REGEX = re.compile(r"\s-gmodules\s")


def is_compiler_clang_with_gmodules(compiler_path):
    # Before computing the result, check if we already have it cached.
    if compiler_path in GMODULES_SUPPORT_MAP:
        return GMODULES_SUPPORT_MAP[compiler_path]

    def _gmodules_supported_internal():
        compiler = os.path.basename(compiler_path)
        if "clang" not in compiler:
            return False
        else:
            # Check the compiler help for the -gmodules option.
            clang_help = os.popen("%s --help" % compiler_path).read()
            return GMODULES_HELP_REGEX.search(clang_help, re.DOTALL) is not None

    GMODULES_SUPPORT_MAP[compiler_path] = _gmodules_supported_internal()
    return GMODULES_SUPPORT_MAP[compiler_path]
