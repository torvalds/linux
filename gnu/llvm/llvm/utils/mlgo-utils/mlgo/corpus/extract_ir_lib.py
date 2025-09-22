# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Library functions for IR extraction."""

import os
import pathlib
import re
import shutil
import subprocess
import multiprocessing
import functools
import json
import logging

from typing import Dict, List, Optional

_UNSPECIFIED_OVERRIDE = ["<UNSPECIFIED>"]


# TODO(ml-compiler-opt): maybe we can also convert here the cmdline file,from a
# \0 - separated list of strings, to a \n one.
def should_include_module(cmdline: str, match_regexp: Optional[str]) -> bool:
    """Determine if the module should be included."""
    if match_regexp is None:
        return True
    lines = cmdline.split("\0")
    return any(len(re.findall(match_regexp, l)) for l in lines)


def get_thinlto_index(cmdline: str, basedir: str) -> Optional[str]:
    opts = cmdline.split("\0")
    for option in opts:
        if option.startswith("-fthinlto-index"):
            return os.path.join(basedir, option.split("=")[1])
    return None


class TrainingIRExtractor:
    """IR and command line extraction from an object file."""

    def __init__(self, obj_relative_path, output_base_dir, obj_base_dir=None):
        """Set up a TrainingIRExtractor.

        Args:
          obj_relative_path: relative path to the input object file. It will be also
            used to construct the absolute path of the output IR and cmd files, by
            appending it to output_base_dir.
          output_base_dir: the directory under which the output will be produced.
          obj_base_dir: the base directory for all the input object files.
        """
        self._obj_relative_path = obj_relative_path
        self._output_base_dir = output_base_dir
        self._obj_base_dir = obj_base_dir if obj_base_dir is not None else ""

    def obj_base_dir(self):
        return self._obj_base_dir

    def output_base_dir(self):
        return self._output_base_dir

    def relative_output_path(self):
        return self._obj_relative_path

    def input_obj(self):
        return os.path.join(self.obj_base_dir(), self._obj_relative_path)

    def lld_src_bc(self):
        # .3.import.bc is the suffix attached to post-merge-pre-opt ('postimport')
        # IR bitcode saved by lld. It is hardcoded into lld.
        return os.path.join(
            self._obj_base_dir, self._obj_relative_path + ".3.import.bc"
        )

    def lld_src_thinlto(self):
        return os.path.join(self._obj_base_dir, self._obj_relative_path + ".thinlto.bc")

    def dest_dir(self):
        return os.path.join(
            self.output_base_dir(), os.path.dirname(self._obj_relative_path)
        )

    def module_name(self):
        return os.path.basename(self._obj_relative_path)

    def cmd_file(self):
        return os.path.join(self.dest_dir(), self.module_name() + ".cmd")

    def bc_file(self):
        return os.path.join(self.dest_dir(), self.module_name() + ".bc")

    def thinlto_index_file(self):
        return os.path.join(self.dest_dir(), self.module_name() + ".thinlto.bc")

    def _get_extraction_cmd_command(
        self, llvm_objcopy_path: str, cmd_section_name: str
    ):
        """Get llvm-objcopy and process args to a produce a command string that,
        when invoked, will extract the cmd section info ths self.cmd_file() file.
        """
        return [
            llvm_objcopy_path,
            "--dump-section=" + cmd_section_name + "=" + self.cmd_file(),
            self.input_obj(),
            "/dev/null",
        ]

    def _get_extraction_bc_command(
        self, llvm_objcopy_path: str, bitcode_section_name: str
    ):
        """Gets llvm-objcopy and process args to produce a command string that,
        when invoked, will extract the bitcode section into the self.bc_file()
        file.
        """
        return [
            llvm_objcopy_path,
            "--dump-section=" + bitcode_section_name + "=" + self.bc_file(),
            self.input_obj(),
            "/dev/null",
        ]

    def _extract_clang_artifacts(
        self,
        llvm_objcopy_path: str,
        cmd_filter: str,
        is_thinlto: bool,
        cmd_section_name: str,
        bitcode_section_name: str,
    ) -> Optional[str]:
        """Run llvm-objcopy to extract the .bc and command line."""
        if not os.path.exists(self.input_obj()):
            logging.info("%s does not exist.", self.input_obj())
            return None
        os.makedirs(self.dest_dir(), exist_ok=True)
        try:
            subprocess.check_output(
                self._get_extraction_cmd_command(llvm_objcopy_path, cmd_section_name),
                stderr=subprocess.STDOUT,
                encoding="utf-8",
            )
            if cmd_filter is not None or is_thinlto:
                with open(self.cmd_file(), encoding="utf-8") as f:
                    lines = f.readlines()
                assert len(lines) == 1
                cmdline = lines[0]
                if not should_include_module(cmdline, cmd_filter):
                    logging.info(
                        "Excluding module %s because it does not match the filter",
                        self.input_obj(),
                    )
                    os.remove(self.cmd_file())
                    return None
                if is_thinlto:
                    index_file = get_thinlto_index(cmdline, self.obj_base_dir())
                    shutil.copy(index_file, self.thinlto_index_file())

            subprocess.check_output(
                self._get_extraction_bc_command(
                    llvm_objcopy_path, bitcode_section_name
                ),
                stderr=subprocess.STDOUT,
                encoding="utf-8",
            )
        except subprocess.CalledProcessError as e:
            # This may happen if  .o file was build from asm (.S source).
            logging.warning("%s was not processed: %s", self.input_obj(), e)
            logging.info(e.output)
            return None
        assert (
            os.path.exists(self.cmd_file())
            and os.path.exists(self.bc_file())
            and (not is_thinlto or os.path.exists(self.thinlto_index_file()))
        )
        return self.relative_output_path()

    def _extract_lld_artifacts(self) -> Optional[str]:
        """Extract the .bc file with ThinLTO index from an lld ThinLTO invocation."""
        if not os.path.exists(self.lld_src_bc()):
            logging.info("%s does not exist.", self.lld_src_bc())
            return None
        if not os.path.exists(self.lld_src_thinlto()):
            logging.info("%s does not exist.", self.lld_src_thinlto())
            return None
        os.makedirs(self.dest_dir(), exist_ok=True)

        # Copy over the files
        shutil.copy(self.lld_src_bc(), self.bc_file())
        shutil.copy(self.lld_src_thinlto(), self.thinlto_index_file())

        assert os.path.exists(self.bc_file())
        assert os.path.exists(self.thinlto_index_file())
        return self._obj_relative_path

    def extract(
        self,
        llvm_objcopy_path: Optional[str] = None,
        cmd_filter: Optional[str] = None,
        thinlto_build: Optional[str] = None,
        cmd_section_name: Optional[str] = ".llvmcmd",
        bitcode_section_name: Optional[str] = ".llvmbc",
    ) -> Optional[str]:
        if thinlto_build == "local":
            return self._extract_lld_artifacts()
        return self._extract_clang_artifacts(
            llvm_objcopy_path=llvm_objcopy_path,
            cmd_filter=cmd_filter,
            is_thinlto=thinlto_build == "distributed",
            cmd_section_name=cmd_section_name,
            bitcode_section_name=bitcode_section_name,
        )


def convert_compile_command_to_objectfile(
    command: Dict[str, str], output_dir: str
) -> Optional[TrainingIRExtractor]:
    obj_base_dir = command["directory"]
    if "arguments" in command:
        cmd_parts = command["arguments"]
    elif "command" in command:
        cmd_parts = command["command"].split()
    else:
        logging.info("compile_commands element has no command and arguments")
        return None

    try:
        obj_index = cmd_parts.index("-o") + 1
    except ValueError:
        # This could happen if there are non-clang commands in compile_commands.json
        logging.info("Command has no -o option: %s", " ".join(cmd_parts))
        return None
    obj_rel_path = cmd_parts[obj_index]
    # TODO(mtrofin): is the obj_base_dir correct for thinlto index bc files?
    return TrainingIRExtractor(
        obj_relative_path=obj_rel_path,
        output_base_dir=output_dir,
        obj_base_dir=obj_base_dir,
    )


def load_from_compile_commands(
    json_array: List[Dict[str, str]], output_dir: str
) -> List[TrainingIRExtractor]:
    objs = [
        convert_compile_command_to_objectfile(cmd, output_dir) for cmd in json_array
    ]
    # Filter out None, in case there were non-clang commands in the .json
    return [obj for obj in objs if obj is not None]


def load_from_lld_params(
    params_array: List[str], obj_base_dir: str, output_dir: str
) -> List[TrainingIRExtractor]:
    """Create an ObjectFile array based on lld's parameters."""
    # yank out -o and the output. After that, anything not starting with '-', and
    # ending in a '.o', is an object file.
    try:
        minus_o_idx = params_array.index("-o")
        del params_array[minus_o_idx : minus_o_idx + 2]
        just_obj_paths = [
            o for o in params_array if not o.startswith("-") and o.endswith(".o")
        ]
    except ValueError:
        logging.info("This params file does not have an explicit -o option.")
        just_obj_paths = params_array

    def make_obj(obj_file: str) -> TrainingIRExtractor:
        return TrainingIRExtractor(
            obj_relative_path=obj_file,
            output_base_dir=output_dir,
            obj_base_dir=obj_base_dir,
        )

    return [make_obj(obj_file) for obj_file in just_obj_paths]


def load_from_directory(
    obj_base_dir: str, output_dir: str
) -> List[TrainingIRExtractor]:
    """Create an object file array by globbing an entire drectory.

    Args:
      obj_base_dir: The base build directory that all object files will be
        written out as being relative to.
      output_dir: The output directory where extracted .bc and .cmd files should
        be placed.
    """
    paths = [str(p) for p in pathlib.Path(obj_base_dir).glob("**/*.o")]

    def make_spec(obj_file: str):
        return TrainingIRExtractor(
            obj_relative_path=os.path.relpath(obj_file, start=obj_base_dir),
            output_base_dir=output_dir,
            obj_base_dir=obj_base_dir,
        )

    return [make_spec(path) for path in paths]


def load_for_lld_thinlto(
    obj_base_dir: str, output_dir: str
) -> List[TrainingIRExtractor]:
    # .3.import.bc is the suffix attached to post-merge-pre-opt ('postimport')
    # IR bitcode saved by lld. It is hardcoded into lld. ThinLTO index files
    # are also emitted next to the postimport bitcode, with the suffix
    # .thinlto.bc instead
    paths = [str(p) for p in pathlib.Path(obj_base_dir).glob("**/*.3.import.bc")]

    def make_spec(obj_file: str):
        return TrainingIRExtractor(
            # Cut away .3.import.bc
            obj_relative_path=os.path.relpath(obj_file, start=obj_base_dir)[:-12],
            output_base_dir=output_dir,
            obj_base_dir=obj_base_dir,
        )

    return [make_spec(path) for path in paths]


def load_bazel_aquery(aquery_json, obj_base_dir: str, output_dir: str):
    """Creates an object file array by looking at the JSON output of bazel aquery.

    Args:
      aquery_json: The JSON-formatted output of the bazel aquery command for
        the target of interest. The bazel aquery JSON should be a JSON
        serialized version of the analysis.ActionGraphContainer proto.
        https://github.com/bazelbuild/bazel/blob/master/src/main/protobuf/analysis_v2.proto
      obj_base_dir: The base build directory that all object files will be
        written out as arelative to.
      output_dir: The output directory where extracted .bc and .cmd files should
        be placed.
    """
    linker_params = []

    for action_info in aquery_json["actions"]:
        if action_info["mnemonic"] != "CppLink":
            continue
        linker_params = action_info["arguments"]

    return load_from_lld_params(linker_params, obj_base_dir, output_dir)


def run_extraction(
    objs: List[TrainingIRExtractor],
    num_workers: int,
    llvm_objcopy_path: str,
    cmd_filter: str,
    thinlto_build: str,
    cmd_section_name: str,
    bitcode_section_name: str,
):
    """Extracts all specified object files into the corpus directory.

    Args:
      objs: A list of TrainingIRExtractor Objects that represent the object files
        to extract bitcode/commands from.
      num_workers: The number of parallel processes to spawn to run the
        extraction.
      llvm_objcopy_path: The path to the llvm-objcopy to use for dumping sections.
      cmd_filter: A regular expression that is used to select for compilations
        performed with specific flags. If you want to include all compilations,
        set this to None.
      thinlto_build: Whether or not this is a ThinLTO build, and if so, the type.
        Set this to None if the build was not done with ThinLTO.
      cmd_section_name: The name of the command line section created by the
        bitcode embedding.
      bitcode_section_name: The name of the bitcode section created by the
        bitcode embedding.
    """
    extract_artifacts = functools.partial(
        TrainingIRExtractor.extract,
        llvm_objcopy_path=llvm_objcopy_path,
        cmd_filter=cmd_filter,
        thinlto_build=thinlto_build,
        cmd_section_name=cmd_section_name,
        bitcode_section_name=bitcode_section_name,
    )

    with multiprocessing.Pool(num_workers) as pool:
        relative_output_paths = pool.map(extract_artifacts, objs)
        pool.close()
        pool.join()
    return relative_output_paths


def write_corpus_manifest(
    thinlto_build: str, relative_output_paths: List[str], output_dir: str
):
    """Writes a corpus_manifest.json containing all necessary information about
    the corpus.

    Args:
      thinlto_build: Whether or not the build was done with ThinLTO and if so,
        what kind of ThinLTO. Set this to none if the build was not performed with
        ThinLTO.
      relative_output_paths: The relative (to the corpus directory) output paths
        of all the bitcode files that should be placed in the corpus manifest
      output_dir: The corpus directory where the corpus manifest should be
        placed.
    """
    # This comes first rather than later so global_command_override is at the top
    # of the .json after being written
    if thinlto_build == "local":
        corpus_description = {"global_command_override": _UNSPECIFIED_OVERRIDE}
    else:
        corpus_description = {}

    corpus_description.update(
        {
            "has_thinlto": thinlto_build is not None,
            "modules": [path for path in relative_output_paths if path is not None],
        }
    )

    with open(
        os.path.join(output_dir, "corpus_description.json"), "w", encoding="utf-8"
    ) as f:
        json.dump(corpus_description, f, indent=2)
