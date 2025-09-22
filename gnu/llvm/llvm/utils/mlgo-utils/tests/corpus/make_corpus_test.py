# REQUIRES: system-linux

## Test the functionality of make_corpus_lib

import json
import os
import sys

from mlgo.corpus import make_corpus_lib

## Test that when we load the bitcode from a directory using the
## load_bitcode_from_directory function, we get the expected results.

# RUN: rm -rf %t.dir && mkdir %t.dir
# RUN: mkdir %t.dir/nested
# RUN: touch %t.dir/nested/test1.bc
# RUN: touch %t.dir/nested/test2.bc
# RUN: %python %s test_load_bitcode_from_directory %t.dir | FileCheck %s --check-prefix CHECK-LOAD


def test_load_bitcode_from_directory(work_dir):
    relative_paths = make_corpus_lib.load_bitcode_from_directory(work_dir)
    relative_paths = sorted(relative_paths)
    for relative_path in relative_paths:
        print(relative_path)
    # CHECK-LOAD: nested/test1
    # CHECK-LOAD: nested/test2


## Test that when we copy the bitcode given a list of relative paths, the
## appropriate files are copied over.

# RUN: rm -rf %t.dir1 && mkdir %t.dir1
# RUN: %python %s test_copy_bitcode %t.dir %t.dir1
# RUN: ls %t.dir1/nested | FileCheck %s --check-prefix CHECK-COPY

# CHECK-COPY: test1.bc
# CHECK-COPY: test2.bc


def test_copy_bitcode(directory, output_dir):
    relative_paths = ["nested/test1", "nested/test2"]
    make_corpus_lib.copy_bitcode(relative_paths, directory, output_dir)


## Test that we get the expected corpus manifest when writing a corpus
## manifest to the specificed directory.

# RUN: %python %s test_write_corpus_manifest %t.dir1 | FileCheck %s --check-prefix CHECK-MANIFEST


def test_write_corpus_manifest(output_dir):
    relative_output_paths = ["test/test1", "test/test2"]
    default_args = ["-O3", "-c"]
    make_corpus_lib.write_corpus_manifest(
        relative_output_paths, output_dir, default_args
    )
    with open(
        os.path.join(output_dir, "corpus_description.json"), encoding="utf-8"
    ) as corpus_description_file:
        corpus_description = json.load(corpus_description_file)
    print(corpus_description["global_command_override"])
    # CHECK-MANIFEST: ['-O3', '-c']
    print(corpus_description["has_thinlto"])
    # CHECK-MANIFEST: False
    print(corpus_description["modules"])
    # CHECK-MANIFEST: ['test/test1', 'test/test2']


if __name__ == "__main__":
    globals()[sys.argv[1]](*sys.argv[2:])
