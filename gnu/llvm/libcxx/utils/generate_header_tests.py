#!/usr/bin/env python

import contextlib
import glob
import io
import os
import pathlib
import re

header_restrictions = {
    "barrier": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "future": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "latch": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "mutex": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "semaphore": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "shared_mutex": "!defined(_LIBCPP_HAS_NO_THREADS)",
    "stdatomic.h": "__cplusplus > 202002L && !defined(_LIBCPP_HAS_NO_THREADS)",
    "thread": "!defined(_LIBCPP_HAS_NO_THREADS)",

    "filesystem": "!defined(_LIBCPP_HAS_NO_FILESYSTEM_LIBRARY)",

    # TODO LLVM17: simplify this to __cplusplus >= 202002L
    "coroutine": "(defined(__cpp_impl_coroutine) && __cpp_impl_coroutine >= 201902L) || (defined(__cpp_coroutines) && __cpp_coroutines >= 201703L)",

    "clocale": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "codecvt": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "fstream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION) && !defined(_LIBCPP_HAS_NO_FSTREAM)",
    "iomanip": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "ios": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "iostream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "istream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "locale.h": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "locale": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "ostream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "regex": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "sstream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "streambuf": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",
    "strstream": "!defined(_LIBCPP_HAS_NO_LOCALIZATION)",

    "wctype.h": "!defined(_LIBCPP_HAS_NO_WIDE_CHARACTERS)",
    "cwctype": "!defined(_LIBCPP_HAS_NO_WIDE_CHARACTERS)",
    "cwchar": "!defined(_LIBCPP_HAS_NO_WIDE_CHARACTERS)",
    "wchar.h": "!defined(_LIBCPP_HAS_NO_WIDE_CHARACTERS)",

    "experimental/algorithm": "__cplusplus >= 201103L",
    "experimental/coroutine": "__cplusplus >= 202002L && !defined(_LIBCPP_HAS_NO_EXPERIMENTAL_COROUTINES)",
    "experimental/deque": "__cplusplus >= 201103L",
    "experimental/forward_list": "__cplusplus >= 201103L",
    "experimental/functional": "__cplusplus >= 201103L",
    "experimental/iterator": "__cplusplus >= 201103L",
    "experimental/list": "__cplusplus >= 201103L",
    "experimental/map": "__cplusplus >= 201103L",
    "experimental/memory_resource": "__cplusplus >= 201103L",
    "experimental/propagate_const": "__cplusplus >= 201103L",
    "experimental/regex": "!defined(_LIBCPP_HAS_NO_LOCALIZATION) && __cplusplus >= 201103L",
    "experimental/set": "__cplusplus >= 201103L",
    "experimental/simd": "__cplusplus >= 201103L",
    "experimental/span": "__cplusplus >= 201103L",
    "experimental/string": "__cplusplus >= 201103L",
    "experimental/type_traits": "__cplusplus >= 201103L",
    "experimental/unordered_map": "__cplusplus >= 201103L",
    "experimental/unordered_set": "__cplusplus >= 201103L",
    "experimental/utility": "__cplusplus >= 201103L",
    "experimental/vector": "__cplusplus >= 201103L",
}

private_headers_still_public_in_modules = [
    '__assert', '__bsd_locale_defaults.h', '__bsd_locale_fallbacks.h', '__config',
    '__config_site.in', '__debug', '__hash_table',
    '__threading_support', '__tree', '__undef_macros', '__verbose_abort'
]

def find_script(file):
    """Finds the script used to generate a file inside the file itself. The script is delimited by
       BEGIN-SCRIPT and END-SCRIPT markers.
    """
    with open(file, 'r') as f:
        content = f.read()

    match = re.search(r'^BEGIN-SCRIPT$(.+)^END-SCRIPT$', content, flags=re.MULTILINE | re.DOTALL)
    if not match:
        raise RuntimeError("Was unable to find a script delimited with BEGIN-SCRIPT/END-SCRIPT markers in {}".format(test_file))
    return match.group(1)

def execute_script(script, variables):
    """Executes the provided Mako template with the given variables available during the
       evaluation of the script, and returns the result.
    """
    code = compile(script, 'fake-filename', 'exec')
    output = io.StringIO()
    with contextlib.redirect_stdout(output):
        exec(code, variables)
        output = output.getvalue()
    return output

def generate_new_file(file, new_content):
    """Generates the new content of the file by inserting the new content in-between
       two '// GENERATED-MARKER' markers located in the file.
    """
    with open(file, 'r') as f:
        old_content = f.read()

    try:
        before, begin_marker, _, end_marker, after = re.split(r'(// GENERATED-MARKER\n)', old_content, flags=re.MULTILINE | re.DOTALL)
    except ValueError:
        raise RuntimeError("Failed to split {} based on markers, please make sure the file has exactly two '// GENERATED-MARKER' occurrences".format(file))

    return before + begin_marker + new_content + end_marker + after

def produce(test_file, variables):
    script = find_script(test_file)
    result = execute_script(script, variables)
    new_content = generate_new_file(test_file, result)
    with open(test_file, 'w', newline='\n') as f:
        f.write(new_content)

def is_header(file):
    """Returns whether the given file is a header (i.e. not a directory or the modulemap file)."""
    return not file.is_dir() and not file.name == 'module.modulemap.in' and file.name != 'libcxx.imp'


def main():
    monorepo_root = pathlib.Path(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
    include = pathlib.Path(os.path.join(monorepo_root, 'libcxx', 'include'))
    test = pathlib.Path(os.path.join(monorepo_root, 'libcxx', 'test'))
    assert(monorepo_root.exists())

    toplevel_headers     = sorted(str(p.relative_to(include)) for p in include.glob('[a-z]*') if is_header(p))
    experimental_headers = sorted(str(p.relative_to(include)) for p in include.glob('experimental/[a-z]*') if is_header(p))
    extended_headers     = sorted(str(p.relative_to(include)) for p in include.glob('ext/[a-z]*') if is_header(p))
    public_headers       = toplevel_headers + experimental_headers + extended_headers
    private_headers      = sorted(str(p.relative_to(include)) for p in include.rglob('*') if is_header(p) and str(p.relative_to(include)).startswith('__'))
    variables = {
        'toplevel_headers': toplevel_headers,
        'experimental_headers': experimental_headers,
        'extended_headers': extended_headers,
        'public_headers': public_headers,
        'private_headers': private_headers,
        'header_restrictions': header_restrictions,
        'private_headers_still_public_in_modules': private_headers_still_public_in_modules
    }

    produce(test.joinpath('libcxx/assertions/headers_declare_verbose_abort.sh.cpp'), variables)
    produce(test.joinpath('libcxx/clang_tidy.sh.cpp'), variables)
    produce(test.joinpath('libcxx/double_include.sh.cpp'), variables)
    produce(test.joinpath('libcxx/min_max_macros.compile.pass.cpp'), variables)
    produce(test.joinpath('libcxx/modules_include.sh.cpp'), variables)
    produce(test.joinpath('libcxx/nasty_macros.compile.pass.cpp'), variables)
    produce(test.joinpath('libcxx/no_assert_include.compile.pass.cpp'), variables)
    produce(test.joinpath('libcxx/private_headers.verify.cpp'), variables)
    produce(test.joinpath('libcxx/transitive_includes.sh.cpp'), variables)


if __name__ == '__main__':
    main()
