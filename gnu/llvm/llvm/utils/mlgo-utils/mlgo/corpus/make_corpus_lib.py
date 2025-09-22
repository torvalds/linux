# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Library functions for making a corpus from arbitrary bitcode."""

import pathlib
import os
import shutil
import json

from typing import List, Optional

BITCODE_EXTENSION = ".bc"


def load_bitcode_from_directory(bitcode_base_dir: str) -> List[str]:
    """Finds bitcode files to extract from a given directory.

    Args:
      bitcode_base_dir: The base directory where the bitcode to be copied
        is from.
      output_dir: The directory to place the bitcode in.

    Returns an array of paths representing the relative path to the bitcode
    file from the base direcotry.
    """
    paths = [
        str(p)[: -len(BITCODE_EXTENSION)]
        for p in pathlib.Path(bitcode_base_dir).glob("**/*" + BITCODE_EXTENSION)
    ]

    return [os.path.relpath(full_path, start=bitcode_base_dir) for full_path in paths]


def copy_bitcode(
    relative_paths: List[str], bitcode_base_dir: str, output_dir: str
) -> None:
    """Copies bitcode files from the base directory to the output directory.

    Args:
      relative_paths: An array of relative paths to bitcode files that are copied
        over to the output directory, preserving relative location.
      bitcode_base_dir: The base directory where the bitcode is located.
      output_dir: The output directory to place the bitcode in.
    """
    for relative_path in relative_paths:
        base_path = os.path.join(bitcode_base_dir, relative_path + BITCODE_EXTENSION)
        destination_path = os.path.join(output_dir, relative_path + BITCODE_EXTENSION)
        os.makedirs(os.path.dirname(destination_path), exist_ok=True)
        shutil.copy(base_path, destination_path)


def write_corpus_manifest(
    relative_output_paths: List[str],
    output_dir: str,
    default_args: Optional[List[str]] = None,
) -> None:
    """Creates a corpus manifest describing the bitcode that has been found.

    Args:
      relative_output_paths: A list of paths to each bitcode file relative to the
        output directory.
      outout_dir: The output directory where the corpus is being created.
      default_args: An array of compiler flags that should be used to compile
        the bitcode when using further downstream tooling."""
    if default_args is None:
        default_args = []
    corpus_description = {
        "global_command_override": default_args,
        "has_thinlto": False,
        "modules": [path for path in relative_output_paths if path is not None],
    }

    with open(
        os.path.join(output_dir, "corpus_description.json"), "w", encoding="utf-8"
    ) as description_file:
        json.dump(corpus_description, description_file, indent=2)
