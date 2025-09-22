#!/usr/bin/env python

import os


def get_libcxx_paths():
    utils_path = os.path.dirname(os.path.abspath(__file__))
    script_name = os.path.basename(__file__)
    assert os.path.exists(utils_path)
    src_root = os.path.dirname(utils_path)
    test_path = os.path.join(src_root, 'test', 'libcxx', 'inclusions')
    assert os.path.exists(test_path)
    assert os.path.exists(os.path.join(test_path, 'algorithm.inclusions.compile.pass.cpp'))
    return script_name, src_root, test_path


script_name, source_root, test_path = get_libcxx_paths()


# This table was produced manually, by grepping the TeX source of the Standard's
# library clauses for the string "#include". Each header's synopsis contains
# explicit "#include" directives for its mandatory inclusions.
# For example, [algorithm.syn] contains "#include <initializer_list>".
#
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
    # TODO "syncstream": ["ostream"],
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

new_in_version = {
    "chrono": "11",
    "compare": "20",
    "concepts": "20",
    "coroutine": "20",
    "cuchar": "11",
    "expected": "23",
    "filesystem": "17",
    "initializer_list": "11",
    "optional": "17",
    "ranges": "20",
    "string_view": "17",
    "syncstream": "20",
    "system_error": "11",
    "thread": "11",
    "tuple": "11",
    "uchar.h": "11",
    "unordered_map": "11",
    "unordered_set": "11",
    "variant": "17",
}

assert all(v == sorted(v) for k, v in mandatory_inclusions.items())

# Map from each header to the Lit annotations that should be used for
# tests that include that header.
#
# For example, when threads are not supported, any test that includes
# <thread> should be marked as UNSUPPORTED, because including <thread>
# is a hard error in that case.
lit_markup = {
  "barrier": ["UNSUPPORTED: no-threads"],
  "filesystem": ["UNSUPPORTED: no-filesystem"],
  "format": ["UNSUPPORTED: libcpp-has-no-incomplete-format"],
  "iomanip": ["UNSUPPORTED: no-localization"],
  "ios": ["UNSUPPORTED: no-localization"],
  "iostream": ["UNSUPPORTED: no-localization"],
  "istream": ["UNSUPPORTED: no-localization"],
  "latch": ["UNSUPPORTED: no-threads"],
  "locale": ["UNSUPPORTED: no-localization"],
  "mutex": ["UNSUPPORTED: no-threads"],
  "ostream": ["UNSUPPORTED: no-localization"],
  "regex": ["UNSUPPORTED: no-localization"],
  "semaphore": ["UNSUPPORTED: no-threads"],
  "shared_mutex": ["UNSUPPORTED: no-threads"],
  "thread": ["UNSUPPORTED: no-threads"]
}


def get_std_ver_test(includee):
    v = new_in_version.get(includee, "03")
    if v == "03":
        return ''
    versions = ["03", "11", "14", "17", "20"]
    return 'TEST_STD_VER > {} && '.format(max(i for i in versions if i < v))


def get_unsupported_line(includee):
    v = new_in_version.get(includee, "03")
    return {
        "03": [],
        "11": ['UNSUPPORTED: c++03'],
        "14": ['UNSUPPORTED: c++03, c++11'],
        "17": ['UNSUPPORTED: c++03, c++11, c++14'],
        "20": ['UNSUPPORTED: c++03, c++11, c++14, c++17'],
        "2b": ['UNSUPPORTED: c++03, c++11, c++14, c++17, c++20'],
    }[v]


def get_libcpp_header_symbol(header_name):
    return '_LIBCPP_' + header_name.upper().replace('.', '_')


def get_includer_symbol_test(includer):
    symbol = get_libcpp_header_symbol(includer)
    return """
#if !defined({symbol})
 #   error "{message}"
#endif
    """.strip().format(
        symbol=symbol,
        message="<{}> was expected to define {}".format(includer, symbol),
    )


def get_ifdef(includer, includee):
    version = max(new_in_version.get(h, "03") for h in [includer, includee])
    symbol = get_libcpp_header_symbol(includee)
    return """
#if {includee_test}!defined({symbol})
 #   error "{message}"
#endif
    """.strip().format(
        includee_test=get_std_ver_test(includee),
        symbol=symbol,
        message="<{}> should include <{}> in C++{} and later".format(includer, includee, version)
    )


test_body_template = """
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// WARNING: This test was generated by {script_name}
// and should not be edited manually.
//
// clang-format off
{markup}
// <{header}>

// Test that <{header}> includes all the other headers it's supposed to.

#include <{header}>
#include "test_macros.h"

{test_includers_symbol}
{test_per_includee}
""".strip()


def produce_tests():
    for includer, includees in mandatory_inclusions.items():
        markup_tags = get_unsupported_line(includer) + lit_markup.get(includer, [])
        test_body = test_body_template.format(
            script_name=script_name,
            header=includer,
            markup=('\n' + '\n'.join('// ' + m for m in markup_tags) + '\n') if markup_tags else '',
            test_includers_symbol=get_includer_symbol_test(includer),
            test_per_includee='\n'.join(get_ifdef(includer, includee) for includee in includees),
        )
        test_name = "{header}.inclusions.compile.pass.cpp".format(header=includer)
        out_path = os.path.join(test_path, test_name)
        with open(out_path, 'w', newline='\n') as f:
            f.write(test_body + '\n')


if __name__ == '__main__':
    produce_tests()
