# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##

import os, pathlib

header_restrictions = {
    # headers with #error directives
    "atomic": "!defined(_LIBCPP_HAS_NO_ATOMIC_HEADER)",
    "stdatomic.h": "!defined(_LIBCPP_HAS_NO_ATOMIC_HEADER)",

    # headers with #error directives
    "ios": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "locale.h": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    # transitive includers of the above headers
    "clocale": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "codecvt": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "fstream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "iomanip": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "iostream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "istream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "locale": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "ostream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "regex": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "sstream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "streambuf": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "strstream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "syncstream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",

    # headers with #error directives
    "barrier": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "future": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "latch": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "semaphore": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "shared_mutex": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "stop_token": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "thread": "!defined(_LIBCPP_HAS_NO_THREADS)",

    # headers with #error directives
    "wchar.h": "!defined(_LIBCPP_HAS_NO_WIDE_CHARACTERS)",
    "wctype.h": "!defined(_LIBCPP_HAS_NO_WIDE_CHARACTERS)",
    # transitive includers of the above headers
    "cwchar": "!defined(_LIBCPP_HAS_NO_WIDE_CHARACTERS)",
    "cwctype": "!defined(_LIBCPP_HAS_NO_WIDE_CHARACTERS)",
}

lit_header_restrictions = {
    "barrier": "// UNSUPPORTED: no-threads, c++03, c++11, c++14, c++17",
    "clocale": "// UNSUPPORTED: no-localization",
    "codecvt": "// UNSUPPORTED: no-localization",
    "coroutine": "// UNSUPPORTED: c++03, c++11, c++14, c++17",
    "cwchar": "// UNSUPPORTED: no-wide-characters",
    "cwctype": "// UNSUPPORTED: no-wide-characters",
    "experimental/iterator": "// UNSUPPORTED: c++03",
    "experimental/propagate_const": "// UNSUPPORTED: c++03",
    "experimental/simd": "// UNSUPPORTED: c++03",
    "experimental/type_traits": "// UNSUPPORTED: c++03",
    "experimental/utility": "// UNSUPPORTED: c++03",
    "filesystem": "// UNSUPPORTED: no-filesystem, c++03, c++11, c++14",
    "fstream": "// UNSUPPORTED: no-localization, no-filesystem",
    "future": "// UNSUPPORTED: no-threads, c++03",
    "iomanip": "// UNSUPPORTED: no-localization",
    "ios": "// UNSUPPORTED: no-localization",
    "iostream": "// UNSUPPORTED: no-localization",
    "istream": "// UNSUPPORTED: no-localization",
    "latch": "// UNSUPPORTED: no-threads, c++03, c++11, c++14, c++17",
    "locale": "// UNSUPPORTED: no-localization",
    "locale.h": "// UNSUPPORTED: no-localization",
    "mutex": "// UNSUPPORTED: no-threads, c++03",
    "ostream": "// UNSUPPORTED: no-localization",
    "print": "// UNSUPPORTED: no-filesystem, c++03, c++11, c++14, c++17, c++20, availability-fp_to_chars-missing", # TODO PRINT investigate
    "regex": "// UNSUPPORTED: no-localization",
    "semaphore": "// UNSUPPORTED: no-threads, c++03, c++11, c++14, c++17",
    "shared_mutex": "// UNSUPPORTED: no-threads, c++03, c++11",
    "sstream": "// UNSUPPORTED: no-localization",
    "stdatomic.h": "// UNSUPPORTED: no-threads, c++03, c++11, c++14, c++17, c++20",
    "stop_token": "// UNSUPPORTED: no-threads, c++03, c++11, c++14, c++17",
    "streambuf": "// UNSUPPORTED: no-localization",
    "strstream": "// UNSUPPORTED: no-localization",
    "syncstream": "// UNSUPPORTED: no-localization",
    "thread": "// UNSUPPORTED: no-threads, c++03",
    "wchar.h": "// UNSUPPORTED: no-wide-characters",
    "wctype.h": "// UNSUPPORTED: no-wide-characters",
}

# This table was produced manually, by grepping the TeX source of the Standard's
# library clauses for the string "#include". Each header's synopsis contains
# explicit "#include" directives for its mandatory inclusions.
# For example, [algorithm.syn] contains "#include <initializer_list>".
mandatory_inclusions = {
    "algorithm": ["initializer_list"],
    "array": ["compare", "initializer_list"],
    "bitset": ["iosfwd", "string"],
    "chrono": ["compare"],
    "cinttypes": ["cstdint"],
    "complex.h": ["complex"],
    "coroutine": ["compare"],
    "deque": ["compare", "initializer_list"],
    "filesystem": ["compare"],
    "forward_list": ["compare", "initializer_list"],
    "ios": ["iosfwd"],
    "iostream": ["ios", "istream", "ostream", "streambuf"],
    "iterator": ["compare", "concepts"],
    "list": ["compare", "initializer_list"],
    "map": ["compare", "initializer_list"],
    "memory": ["compare"],
    "optional": ["compare"],
    "queue": ["compare", "initializer_list"],
    "random": ["initializer_list"],
    "ranges": ["compare", "initializer_list", "iterator"],
    "regex": ["compare", "initializer_list"],
    "set": ["compare", "initializer_list"],
    "stack": ["compare", "initializer_list"],
    "string_view": ["compare"],
    "string": ["compare", "initializer_list"],
    "syncstream": ["ostream"],
    "system_error": ["compare"],
    "tgmath.h": ["cmath", "complex"],
    "thread": ["compare"],
    "tuple": ["compare"],
    "typeindex": ["compare"],
    "unordered_map": ["compare", "initializer_list"],
    "unordered_set": ["compare", "initializer_list"],
    "utility": ["compare", "initializer_list"],
    "valarray": ["initializer_list"],
    "variant": ["compare"],
    "vector": ["compare", "initializer_list"],
}


# These headers are not yet implemented in libc++
#
# These headers are required by the latest (draft) Standard but have not been
# implemented yet. They are used in the generated module input. The C++23 standard
# modules will fail to build if a header is added but this list is not updated.
headers_not_available = [
    "debugging",
    "flat_map",
    "flat_set",
    "generator",
    "hazard_pointer",
    "inplace_vector",
    "linalg",
    "rcu",
    "spanstream",
    "stacktrace",
    "stdfloat",
    "text_encoding",
]


def is_header(file):
    """Returns whether the given file is a header (i.e. not a directory or the modulemap file)."""
    return not file.is_dir() and not file.name in [
        "module.modulemap",
        "CMakeLists.txt",
        "libcxx.imp",
    ]


def is_public_header(header):
    return "__" not in header and not header.startswith("ext/")


def is_modulemap_header(header):
    """Returns whether a header should be listed in the modulemap"""
    # TODO: Should `__config_site` be in the modulemap?
    if header == "__config_site":
        return False

    if header == "__assertion_handler":
        return False

    # exclude libc++abi files
    if header in ["cxxabi.h", "__cxxabi_config.h"]:
        return False

    # exclude headers in __support/ - these aren't supposed to work everywhere,
    # so they shouldn't be included in general
    if header.startswith("__support/"):
        return False

    # exclude ext/ headers - these are non-standard extensions and are barely
    # maintained. People should migrate away from these and we don't need to
    # burden ourself with maintaining them in any way.
    if header.startswith("ext/"):
        return False
    return True

libcxx_root = pathlib.Path(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
include = pathlib.Path(os.path.join(libcxx_root, "include"))
test = pathlib.Path(os.path.join(libcxx_root, "test"))
assert libcxx_root.exists()

all_headers = sorted(
    p.relative_to(include).as_posix() for p in include.rglob("[_a-z]*") if is_header(p)
)
toplevel_headers = sorted(
    p.relative_to(include).as_posix() for p in include.glob("[_a-z]*") if is_header(p)
)
experimental_headers = sorted(
    p.relative_to(include).as_posix()
    for p in include.glob("experimental/[a-z]*")
    if is_header(p)
)

public_headers = [p for p in all_headers if is_public_header(p)]

# The headers used in the std and std.compat modules.
#
# This is the set of all C++23-and-later headers, excluding C compatibility headers.
module_headers = [
    header
    for header in toplevel_headers
    if not header.endswith(".h") and is_public_header(header)
    # These headers have been removed in C++20 so are never part of a module.
    and not header in ["ccomplex", "ciso646", "cstdbool", "ctgmath"]
]

# The C headers used in the std and std.compat modules.
module_c_headers = [
    "cassert",
    "cctype",
    "cerrno",
    "cfenv",
    "cfloat",
    "cinttypes",
    "climits",
    "clocale",
    "cmath",
    "csetjmp",
    "csignal",
    "cstdarg",
    "cstddef",
    "cstdint",
    "cstdio",
    "cstdlib",
    "cstring",
    "ctime",
    "cuchar",
    "cwchar",
    "cwctype",
]
