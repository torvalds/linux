# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Extract IR for training.

Extract IR for training, either from a compile_commands.json file produced by
cmake, or a linker parameter list file.

Only run with
'python compiler_opt/tools/extract_ir.py ...'

The compilation is assumed to have been performed with clang, using
-fembed-bitcode=all passed to cc1 (i.e. pass clang -Xclang=-fembed-bitcode=all)

In a distributed ThinLTO case, the compilation is assumed to have been performed
specifying -mllvm -lto-embed-bitcode=post-merge-pre-opt.

In a local ThinLTO case, the compilation is assumedto have been performed
specifying -Wl,--save-temps=import -Wl,--thinlto-emit-index-files

To change the logging verbosity, pass an integer representing the desired
verbosity to the --verbosity flag. Use 0 for all logs, status information,
and detailed debug information, -1 for solely warnings, and -2 to not produce
any output.
"""

import argparse
import json
import logging
import multiprocessing

from mlgo.corpus import extract_ir_lib


def parse_args_and_run():
    parser = argparse.ArgumentParser(
        description="A tool for making a corpus from build artifacts"
    )
    parser.add_argument(
        "--input",
        type=str,
        help="Input file or directory - either compile_commands.json, a linker "
        "parameter list, or a path to a directory containing object files.",
    )
    parser.add_argument(
        "--input_type",
        type=str,
        help="Input file type - JSON, LLD params, directory, or bazel aquery.",
        choices=["json", "params", "directory", "bazel_aquery"],
        default="json",
        nargs="?",
    )
    parser.add_argument("--output_dir", type=str, help="Output directory")
    parser.add_argument(
        "--num_workers",
        type=int,
        help="Number of parallel works for objcopy. `None` for maximum available.",
        default=None,
        nargs="?",
    )
    parser.add_argument(
        "--llvm_objcopy_path",
        type=str,
        help="Path to llvm-objcopy",
        default="llvm-objcopy",
        nargs="?",
    )
    parser.add_argument(
        "--obj_base_dir",
        type=str,
        help="Base directory for object files. Defaults to current working dir.",
        default="",
        nargs="?",
    )
    parser.add_argument(
        "--cmd_filter",
        type=str,
        help="Include only those modules with a command line matching this regular "
        "expression. Set it to None to not perform any filtering. Note that the "
        "regular expression is applied independently for each separate command line "
        "option. For example, ^-Oz$ will match Oz built binaries. This does not work "
        "with thinlto_build=lld.",
        default=None,
        nargs="?",
    )
    parser.add_argument(
        "--thinlto_build",
        type=str,
        help="Set if the build was performed with either 'distributed' or 'local' "
        "ThinLTO. This ensures the thinlto.bc files are also copied. The build is "
        "assumed to have had -mllvm -lto-embed-bitcode=post-merge-pre-opt passed in "
        "the distributed case or -Wl,--save-temps=import and "
        "-Wl,--thinlto-emit-index-files passed in the local case",
        choices=["distributed", "local"],
        default=None,
        nargs="?",
    )
    parser.add_argument(
        "--cmd_section_name",
        type=str,
        help="The section name passed to llvm-objcopy. For ELF object files, the "
        "default .llvmcmd is correct. For Mach-O object files, one should use "
        "something like __LLVM,__cmdline",
        default=".llvmcmd",
        nargs="?",
    )
    parser.add_argument(
        "--bitcode_section_name",
        type=str,
        help="The section name passed to llvm-objcopy. For ELF object files, the "
        "default .llvmbc is correct. For Mach-O object files, one should use "
        "__LLVM,__bitcode",
        default=".llvmbc",
        nargs="?",
    )
    args = parser.parse_args()
    main(args)


def main(args):
    objs = []
    if args.input is not None and args.thinlto_build == "local":
        raise ValueError("--thinlto_build=local cannot be run with --input")
    if args.input is None:
        if args.thinlto_build != "local":
            raise ValueError("--input or --thinlto_build=local must be provided")
        objs = extract_ir_lib.load_for_lld_thinlto(args.obj_base_dir, args.output_dir)
    elif args.input_type == "json":
        with open(args.input, encoding="utf-8") as f:
            objs = extract_ir_lib.load_from_compile_commands(
                json.load(f), args.output_dir
            )
    elif args.input_type == "params":
        if not args.obj_base_dir:
            logging.info(
                "-obj_base_dir is unspecified, assuming current directory."
                "If no objects are found, use this option to specify the root"
                "directory for the object file paths in the input file."
            )
        with open(args.input, encoding="utf-8") as f:
            objs = extract_ir_lib.load_from_lld_params(
                [l.strip() for l in f.readlines()], args.obj_base_dir, args.output_dir
            )
    elif args.input_type == "directory":
        logging.warning(
            "Using the directory input is only recommended if the build system"
            "your project uses does not support any structured output that"
            "ml-compiler-opt understands. If your build system provides a"
            "structured compilation database, use that instead"
        )
        objs = extract_ir_lib.load_from_directory(args.input, args.output_dir)
    elif args.input_type == "bazel_aquery":
        with open(args.input, encoding="utf-8") as aquery_json_handle:
            objs = extract_ir_lib.load_bazel_aquery(
                json.load(aquery_json_handle), args.obj_base_dir, args.output_dir
            )
    else:
        logging.error("Unknown input type: %s", args.input_type)

    relative_output_paths = extract_ir_lib.run_extraction(
        objs,
        args.num_workers,
        args.llvm_objcopy_path,
        args.cmd_filter,
        args.thinlto_build,
        args.cmd_section_name,
        args.bitcode_section_name,
    )

    extract_ir_lib.write_corpus_manifest(
        args.thinlto_build, relative_output_paths, args.output_dir
    )

    logging.info(
        "Converted %d files out of %d",
        len(objs) - relative_output_paths.count(None),
        len(objs),
    )


if __name__ == "__main__":
    parse_args_and_run()
