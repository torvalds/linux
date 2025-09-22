# REQUIRES: system-linux

## Test the functionality of extract_ir_lib

import os.path
import sys

from mlgo.corpus import extract_ir_lib

## Test that we can convert a compilation database with a single compilation
## command in it.

# RUN: %python %s test_one_conversion | FileCheck %s --check-prefix CHECK-ONE-CONVERSION


def test_one_conversion():
    obj = extract_ir_lib.convert_compile_command_to_objectfile(
        {
            "directory": "/output/directory",
            "command": "-cc1 -c /some/path/lib/foo/bar.cc -o lib/bar.o",
            "file": "/some/path/lib/foo/bar.cc",
        },
        "/corpus/destination/path",
    )
    print(obj.input_obj())
    # CHECK-ONE-CONVERSION: /output/directory/lib/bar.o
    print(obj.relative_output_path())
    # CHECK-ONE-CONVERSION: lib/bar.o
    print(obj.cmd_file())
    # CHECK-ONE-CONVERSION: /corpus/destination/path/lib/bar.o.cmd
    print(obj.bc_file())
    # CHECK-ONE-CONVERSION: /corpus/destination/path/lib/bar.o.bc
    print(obj.thinlto_index_file())
    # CHECK-ONE-CONVERSION: /corpus/destination/path/lib/bar.o.thinlto.bc


## Test that we can convert an arguments style compilation database

# RUN: %python %s test_one_conversion_arguments_style | FileCheck %s --check-prefix CHECK-ARGUMENTS-STYLE


def test_one_conversion_arguments_style():
    obj = extract_ir_lib.convert_compile_command_to_objectfile(
        {
            "directory": "/output/directory",
            "arguments": [
                "-cc1",
                "-c",
                "/some/path/lib/foo/bar.cc",
                "-o",
                "lib/bar.o",
            ],
            "file": "/some/path/lib/foo/bar.cc",
        },
        "/corpus/destination/path",
    )
    print(obj.input_obj())
    # CHECK-ARGUMENTS-STYLE: /output/directory/lib/bar.o
    print(obj.relative_output_path())
    # CHECK-ARGUMENTS-STYLE: lib/bar.o
    print(obj.cmd_file())
    # CHECK-ARGUMENTS-STYLE: /corpus/destination/path/lib/bar.o.cmd
    print(obj.bc_file())
    # CHECK-ARGUMENTS-STYLE: /corpus/destination/path/lib/bar.o.bc
    print(obj.thinlto_index_file())
    # CHECK-ARGUMENTS-STYLE: /corpus/destination/path/lib/bar.o.thinlto.bc


## Test that converting multiple files works as well

# RUN: %python %s test_multiple_conversion | FileCheck %s --check-prefix CHECK-MULTIPLE-CONVERSION


def test_multiple_conversion():
    res = extract_ir_lib.load_from_compile_commands(
        [
            {
                "directory": "/output/directory",
                "command": "-cc1 -c /some/path/lib/foo/bar.cc -o lib/bar.o",
                "file": "/some/path/lib/foo/bar.cc",
            },
            {
                "directory": "/output/directory",
                "command": "-cc1 -c /some/path/lib/foo/baz.cc -o lib/other/baz.o",
                "file": "/some/path/lib/foo/baz.cc",
            },
        ],
        "/corpus/destination/path",
    )
    res = list(res)
    print(res[0].input_obj())
    # CHECK-MULTIPLE-CONVERSION: /output/directory/lib/bar.o
    print(res[0].relative_output_path())
    # CHECK-MULTIPLE-CONVERSION: lib/bar.o
    print(res[0].cmd_file())
    # CHECK-MULTIPLE-CONVERSION: /corpus/destination/path/lib/bar.o.cmd
    print(res[0].bc_file())
    # CHECK-MULTIPLE-CONVERSION: /corpus/destination/path/lib/bar.o.bc
    print(res[0].thinlto_index_file())
    # CHECK-MULTIPLE-CONVERSION: /corpus/destination/path/lib/bar.o.thinlto.bc

    print(res[1].input_obj(), "/output/directory/lib/other/baz.o")
    # CHECK-MULTIPLE-CONVERSION: /output/directory/lib/other/baz.o
    print(res[1].relative_output_path(), "lib/other/baz.o")
    # CHECK-MULTIPLE-CONVERSION: lib/other/baz.o
    print(res[1].cmd_file())
    # CHECK-MULTIPLE-CONVERSION: /corpus/destination/path/lib/other/baz.o.cmd
    print(res[1].bc_file())
    # CHECK-MULTIPLE-CONVERSION: /corpus/destination/path/lib/other/baz.o.bc
    print(res[1].thinlto_index_file())
    # CHECK-MULTIPLE-CONVERSION: /corpus/destination/path/lib/other/baz.o.thinlto.bc


## Test that we generate the correct objcopy commands for extracting commands

# RUN: %python %s test_command_extraction | FileCheck %s --check-prefix CHECK-COMMAND-EXTRACT


def test_command_extraction():
    obj = extract_ir_lib.TrainingIRExtractor(
        obj_relative_path="lib/obj_file.o",
        output_base_dir="/where/corpus/goes",
        obj_base_dir="/foo/bar",
    )
    extraction_cmd1 = obj._get_extraction_cmd_command(
        "/bin/llvm_objcopy_path", ".llvmcmd"
    )
    for part in extraction_cmd1:
        print(part)
    # CHECK-COMMAND-EXTRACT: /bin/llvm_objcopy_path
    # CHECK-COMMAND-EXTRACT: --dump-section=.llvmcmd=/where/corpus/goes/lib/obj_file.o.cmd
    # CHECK-COMMAND-EXTRACT: /foo/bar/lib/obj_file.o
    # CHECK-COMMAND-EXTRACT: /dev/null

    extraction_cmd2 = obj._get_extraction_bc_command(
        "/bin/llvm_objcopy_path", ".llvmbc"
    )
    for part in extraction_cmd2:
        print(part)
    # CHECK-COMMAND-EXTRACT: /bin/llvm_objcopy_path
    # CHECK-COMMAND-EXTRACT: --dump-section=.llvmbc=/where/corpus/goes/lib/obj_file.o.bc
    # CHECK-COMMAND-EXTRACT: /foo/bar/lib/obj_file.o
    # CHECK-COMMAND-EXTRACT: /dev/null


## Test that we generate the correct extraction commands without specifying
## an output base directory.

# RUN: %python %s test_command_extraction_no_basedir | FileCheck %s --check-prefix CHECK-COMMAND-EXTRACT-NOBASEDIR


def test_command_extraction_no_basedir():
    obj = extract_ir_lib.TrainingIRExtractor("lib/obj_file.o", "/where/corpus/goes")
    extraction_cmd1 = obj._get_extraction_cmd_command(
        "/bin/llvm_objcopy_path", ".llvmcmd"
    )
    for part in extraction_cmd1:
        print(part)
    # CHECK-COMMAND-EXTRACT-NOBASEDIR: /bin/llvm_objcopy_path
    # CHECK-COMMAND-EXTRACT-NOBASEDIR: --dump-section=.llvmcmd=/where/corpus/goes/lib/obj_file.o.cmd
    # CHECK-COMMAND-EXTRACT-NOBASEDIR: lib/obj_file.o
    # CHECK-COMMAND-EXTRACT-NOBASEDIR: /dev/null

    extraction_cmd2 = obj._get_extraction_bc_command(
        "/bin/llvm_objcopy_path", ".llvmbc"
    )
    for part in extraction_cmd2:
        print(part)
    # CHECK-COMMAND-EXTRACT-NOBASEDIR: /bin/llvm_objcopy_path
    # CHECK-COMMAND-EXTRACT-NOBASEDIR: --dump-section=.llvmbc=/where/corpus/goes/lib/obj_file.o.bc
    # CHECK-COMMAND-EXTRACT-NOBASEDIR: lib/obj_file.o
    # CHECK-COMMAND-EXTRACT-NOBASEDIR: /dev/null


## Test that we can extract a corpus from lld parameters

# RUN: %python %s test_lld_params | FileCheck %s --check-prefix CHECK-LLD-PARAMS


def test_lld_params():
    lld_opts = [
        "-o",
        "output/dir/exe",
        "lib/obj1.o",
        "somelib.a",
        "-W,blah",
        "lib/dir/obj2.o",
    ]
    obj = extract_ir_lib.load_from_lld_params(lld_opts, "/some/path", "/tmp/out")
    print(obj[0].input_obj())
    # CHECK-LLD-PARAMS: /some/path/lib/obj1.o
    print(obj[0].relative_output_path())
    # CHECK-LLD-PARAMS: lib/obj1.o
    print(obj[0].cmd_file())
    # CHECK-LLD-PARAMS: /tmp/out/lib/obj1.o.cmd
    print(obj[0].thinlto_index_file())
    # CHECK-LLD-PARAMS: /tmp/out/lib/obj1.o.thinlto.bc
    print(obj[1].input_obj())
    # CHECK-LLD-PARMAS: /some/path/lib/dir/obj2.o


## Test that we can load a corpus from a directory containing object files

# RUN: rm -rf %t.dir && mkdir %t.dir
# RUN: mkdir %t.dir/subdir
# RUN: touch %t.dir/subdir/test1.o
# RUN: touch %t.dir/subdir/test2.o
# RUN: %python %s test_load_from_directory %t.dir | FileCheck %s --check-prefix CHECK-LOAD-DIR


def test_load_from_directory(tempdir):
    objs = extract_ir_lib.load_from_directory(tempdir, "/output")
    for index, obj in enumerate(sorted(objs, key=lambda x: x._obj_relative_path)):
        print(obj._obj_relative_path, f"subdir/test{index + 1:d}.o")
        # CHECK-LOAD-DIR: subdir/test1.o
        # Explicitly check for equality here as we can not check within
        # FileCheck the exact value as lit substitutions do not work in
        # FileCheck lines.
        print(obj._obj_base_dir == tempdir)
        # CHECK-LOAD-DIR: True
        print(obj._output_base_dir)
        # CHECK-LOAD-DIR /output


## Test that we can load a corpus in the lld thinLTO case

# RUN: rm -rf %.dir && mkdir %t.dir
# RUN: touch %t.dir/1.3.import.bc
# RUN: touch %t.dir/2.3.import.bc
# RUN: touch %t.dir/3.3.import.bc
# RUN: touch %t.dir/1.thinlto.bc
# RUN: touch %t.dir/2.thinlto.bc
# RUN: touch %t.dir/3.thinlto.bc
# RUN: %python %s test_lld_thinlto_discovery %t.dir | FileCheck %s --check-prefix CHECK-LLD-THINLTO-DISCOVERY


def test_lld_thinlto_discovery(tempdir):
    obj = extract_ir_lib.load_for_lld_thinlto(tempdir, "/output")
    for i, o in enumerate(sorted(obj, key=lambda x: x._obj_relative_path)):
        print(o._obj_relative_path)
        # Explicitly check for equality as we can not check within FileCheck
        # using the lit substitution for the temp dir
        print(o._obj_base_dir == tempdir)
        print(o._output_base_dir)  # outdir
    # CHECK-LLD-THINLTO-DISCOVERY: 1
    # CHECK-LLD-THINLTO-DISCOVERY: True
    # CHECK-LLD-THINLTO-DISCOVERY: /output
    # CHECK-LLD-THINLTO-DISCOVERY: 2
    # CHECK-LLD-THINLTO-DISCOVERY: True
    # CHECK-LLD-THINLTO-DISCOVERY: /output
    # CHECK-LLD-THINLTO-DISCOVERY: 3
    # CHECK-LLD-THINLTO-DISCOVERY: True
    # CHECK-LLD-THINLTO-DISCOVERY: /output


## Test that we can load a corpus in the nested lld thinLTO case

# RUN: mkdir %t.dir/nest
# RUN: mv %t.dir/*.bc %t.dir/nest
# RUN: %python %s test_lld_thinlto_discovery_nested %t.dir | FileCheck %s --check-prefix CHECK-LLD-THINLTO-DISCOVERY-NESTED


def test_lld_thinlto_discovery_nested(outer):
    obj = extract_ir_lib.load_for_lld_thinlto(outer, "/output")
    for i, o in enumerate(sorted(obj, key=lambda x: x._obj_relative_path)):
        print(o._obj_relative_path)
        print(o._obj_base_dir == outer)
        print(o._output_base_dir)
    # CHECK-LLD-THINLTO-DISCOVERY-NESTED: nest/1
    # CHECK-LLD-THINLTO-DISCOVERY-NESTED: True
    # CHECK-LLD-THINLTO-DISCOVERY-NESTED: /output
    # CHECK-LLD-THINLTO-DISCOVERY-NESTED: nest/2
    # CHECK-LLD-THINLTO-DISCOVERY-NESTED: True
    # CHECK-LLD-THINLTO-DISCOVERY-NESTED: /output
    # CHECK-LLD-THINLTO-DISCOVERY-NESTED: nest/3
    # CHECK-LLD-THINLTO-DISCOVERY-NESTED: True
    # CHECK-LLD-THINLTO-DISCOVERY-NESTED: /output


## Test the lld extraction works as expected

# RUN: rm -rf  %t.dir.out && mkdir %t.dir.out
# RUN: %python %s test_lld_thinlto_extraction %t.dir %t.dir.out | FileCheck %s --check-prefix CHECK-LLD-THINLTO-EXTRACTION-PY
# ls %t.dir.out/nest | FileChceck %s --check-prefix CHECK-LLD-THINLTO-EXTRACTION

# CHECK-LLD-THINLTO-EXTRACTION: 1
# CHECK-LLD-THINLTO-EXTRACTION: 2
# CHECK-LLD-THINLTO-EXTRACTION: 3
# CHECK-LLD-THINLTO-EXTRACTION: 1.bc
# CHECK-LLD-THINLTO-EXTRACTION: 2.bc
# CHECK-LLD-THINLTO-EXTRACTION: 3.bc
# CHECK-LLD-THINLTO-EXTRACTION: 1.thinlto.bc
# CHECK-LLD-THINLTO-EXTRACTION: 2.thinlto.bc
# CHECK-LLD-THINLTO-EXTRACTION: 3.thinlto.bc


def test_lld_thinlto_extraction(outer, outdir):
    obj = extract_ir_lib.load_for_lld_thinlto(outer, outdir)
    for i, o in enumerate(sorted(obj, key=lambda x: x._obj_relative_path)):
        mod_path = o.extract(thinlto_build="local")
        print(mod_path)
    # CHECK-LLD-THINLTO-EXTRACTION-PY: 1
    # CHECK-LLD-THINLTO-EXTRACTION-PY: 2
    # CHECK-LLD-THINLTO-EXTRACTION-PY: 3


## Test that we can load a bazel query JSON as expected.

# RUN: %python %s test_load_bazel_aquery | FileCheck %s --check-prefix CHECK-TEST-LOAD-BAZEL-AQUERY


def test_load_bazel_aquery():
    obj = extract_ir_lib.load_bazel_aquery(
        {
            "actions": [
                {"mnemonic": "not-link", "arguments": []},
                {
                    "mnemonic": "CppLink",
                    "arguments": ["clang", "-o", "output_binary", "test1.o", "test2.o"],
                },
            ]
        },
        "/some/path",
        "/tmp/out",
    )
    print(obj[0].input_obj())
    # CHECK-TEST-LOAD-BAZEL-AQUERY: /some/path/test1.o
    print(obj[0].relative_output_path())
    # CHECK-TEST-LOAD-BAZEL-AQUERY: test1.o
    print(obj[0].cmd_file())
    # CHECK-TEST-LOAD-BAZEL-AQUERY: /tmp/out/test1.o.cmd
    print(obj[0].bc_file())
    # CHECK-TEST-LOAD-BAZEL-AQUERY: /tmp/out/test1.o.bc
    print(obj[1].input_obj())
    # CHECK-TEST-LOAD-BAZEL-AQUERY: /some/path/test2.o
    print(obj[1].relative_output_path())
    # CHECK-TEST-LOAD-BAZEL-AQUERY: test2.o
    print(obj[1].cmd_file())
    # CHECK-TEST-LOAD-BAZEL-AQUERY: /tmp/out/test2.o.cmd
    print(obj[1].bc_file())
    # CHECK-TEST-LOAD-BAZEL-AQUERY: /tmp/out/test2.o.bc


## Test that filtering works correctly

# RUN: %python %s test_filtering | FileCheck %s --check-prefix CHECK-TEST-FILTERING


def test_filtering():
    cmdline = "-cc1\0x/y/foobar.cpp\0-Oz\0-Ifoo\0-o\0bin/out.o"
    print(extract_ir_lib.should_include_module(cmdline, None))
    # CHECK-TEST-FILTERING: True
    print(extract_ir_lib.should_include_module(cmdline, ".*"))
    # CHECK-TEST-FILTERING: True
    print(extract_ir_lib.should_include_module(cmdline, "^-Oz$"))
    # CHECK-TEST-FILTERING: True
    print(extract_ir_lib.should_include_module(cmdline, "^-O3$"))
    # CHECK-TEST-FILTERING: False


## Test that we extract the thinLTO index correctly

# RUN: %python %s test_thinlto_index_extractor | FileCheck %s --check-prefix CHECK-THINLTO-INDEX-EXTRACTOR


def test_thinlto_index_extractor():
    cmdline = (
        "-cc1\0x/y/foobar.cpp\0-Oz\0-Ifoo\0-o\0bin/"
        "out.o\0-fthinlto-index=foo/bar.thinlto.bc"
    )
    print(extract_ir_lib.get_thinlto_index(cmdline, "/the/base/dir"))
    # CHECK-THINLTO-INDEX-EXTRACTOR: /the/base/dir/foo/bar.thinlto.bc


if __name__ == "__main__":
    globals()[sys.argv[1]](*sys.argv[2:])
