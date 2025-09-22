# REQUIRES: system-linux

## Test the functionality of combine_training_corpus_lib

import json
import os
import sys

from mlgo.corpus import combine_training_corpus_lib

## Test that combining two training corpora works as expected

# RUN: rm -rf %t.dir && mkdir %t.dir
# RUN: mkdir %t.dir/subcorpus1
# RUN: mkdir %t.dir/subcorpus2
# RUN: %python %s test_combine_corpus %t.dir | FileCheck %s --check-prefix CHECK-COMBINE-CORPUS


def test_combine_corpus(corpus_dir):
    subcorpus1_dir = os.path.join(corpus_dir, "subcorpus1")
    subcorpus2_dir = os.path.join(corpus_dir, "subcorpus2")
    subcorpus1_description = {
        "has_thinlto": False,
        "modules": ["test1.o", "test2.o"],
    }
    subcorpus2_description = {
        "has_thinlto": False,
        "modules": ["test3.o", "test4.o"],
    }
    with open(
        os.path.join(subcorpus1_dir, "corpus_description.json"), "w"
    ) as corpus1_description_handle:
        json.dump(subcorpus1_description, corpus1_description_handle)
    with open(
        os.path.join(subcorpus2_dir, "corpus_description.json"), "w"
    ) as corpus2_description_handle:
        json.dump(subcorpus2_description, corpus2_description_handle)
    combine_training_corpus_lib.combine_corpus(corpus_dir)
    with open(
        os.path.join(corpus_dir, "corpus_description.json"), encoding="utf-8"
    ) as combined_corpus_description_file:
        combined_corpus_description = json.load(combined_corpus_description_file)
    print(combined_corpus_description["has_thinlto"])
    # CHECK-COMBINE-CORPUS: False
    for module in sorted(combined_corpus_description["modules"]):
        print(module)
    # CHECK-COMBINE-CORPUS: subcorpus1/test1.o
    # CHECK-COMBINE-CORPUS: subcorpus1/test2.o
    # CHECK-COMBINE-CORPUS: subcorpus2/test3.o
    # CHECK-COMBINE-CORPUS: subcorpus2/test4.o


## Test that we handle the empty folder case gracefully

# RUN: rm -rf %t.dir && mkdir %t.dir
# RUN: mkdir %t.dir/subcorpus1
# RUN: mkdir %t.dir/empty_dir
# RUN: %python %s test_empty_folder %t.dir | FileCheck %s --check-prefix CHECK-EMPTY-DIR


def test_empty_folder(corpus_dir):
    subcorpus1_dir = os.path.join(corpus_dir, "subcorpus1")
    subcorpus1_description = {"modules": ["test1.o", "test2.o"]}
    with open(
        os.path.join(subcorpus1_dir, "corpus_description.json"), "w"
    ) as subcorpus1_description_handle:
        json.dump(subcorpus1_description, subcorpus1_description_handle)
    combine_training_corpus_lib.combine_corpus(corpus_dir)
    with open(
        os.path.join(corpus_dir, "corpus_description.json"), encoding="utf-8"
    ) as combined_corpus_description_file:
        combined_corpus_description = json.load(combined_corpus_description_file)
    print(len(combined_corpus_description["modules"]))
    # CHECK-EMPTY-DIR: 2


## Test that we ignore extra files that will not end up contributing to the
## corpus.

# RUN: rm -rf %t.dir && mkdir %t.dir
# RUN: mkdir %t.dir/subcorpus1
# RUN: touch %t.dir/empty.log
# RUN: %python %s test_ignore_extra_file %t.dir | FileCheck %s --check-prefix CHECK-IGNORE-EXTRA-FILE


def test_ignore_extra_file(corpus_dir):
    subcorpus1_dir = os.path.join(corpus_dir, "subcorpus1")
    subcorpus1_description = {"modules": ["test1.o", "test2.o"]}
    with open(
        os.path.join(subcorpus1_dir, "corpus_description.json"), "w"
    ) as subcorpus1_description_handle:
        json.dump(subcorpus1_description, subcorpus1_description_handle)
    combine_training_corpus_lib.combine_corpus(corpus_dir)
    with open(
        os.path.join(corpus_dir, "corpus_description.json"), encoding="utf-8"
    ) as combined_corpus_description_file:
        combined_corpus_description = json.load(combined_corpus_description_file)
    print(len(combined_corpus_description["modules"]))
    # CHECK-IGNORE-EXTRA-FILE: 2


## Test that we raise an error in the case where the corpora differ in a
## substantial way.

# RUN: rm -rf  %t.dir && mkdir %t.dir
# RUN: mkdir %t.dir/subcorpus1
# RUN: mkdir %t.dir/subcorpus2
# RUN: %python %s test_different_corpora %t.dir | FileCheck %s --check-prefix CHECK-DIFFERENT-CORPORA


def test_different_corpora(corpus_dir):
    subcorpus1_dir = os.path.join(corpus_dir, "subcorpus1")
    subcorpus2_dir = os.path.join(corpus_dir, "subcorpus2")
    subcorpus1_description = {"has_thinlto": False, "modules": ["test1.o"]}
    subcorpus2_description = {"has_thinlto": True, "modules": ["test2.o"]}
    with open(
        os.path.join(subcorpus1_dir, "corpus_description.json"), "w"
    ) as subcorpus1_description_handle:
        json.dump(subcorpus1_description, subcorpus1_description_handle)
    with open(
        os.path.join(subcorpus2_dir, "corpus_description.json"), "w"
    ) as subcorpus2_description_handle:
        json.dump(subcorpus2_description, subcorpus2_description_handle)
    try:
        combine_training_corpus_lib.combine_corpus(corpus_dir)
    except ValueError:
        print("ValueError")
        # CHECK-DIFFERENT-CORPORA: ValueError


if __name__ == "__main__":
    globals()[sys.argv[1]](*sys.argv[2:])
