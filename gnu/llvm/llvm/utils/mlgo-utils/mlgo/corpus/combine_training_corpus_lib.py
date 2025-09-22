# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
"""Library for combining training corpora."""

import os
import json
import glob
import logging

_FILE_NAME = "corpus_description.json"


def combine_corpus(root_dir: str) -> None:
    module_names = []
    output_corpus_description = {}

    corpus_description_glob = os.path.join(root_dir, "*/" + _FILE_NAME)
    for corpus_description_path in glob.glob(corpus_description_glob):
        logging.info("processing %s", corpus_description_path)

        with open(corpus_description_path, encoding="utf-8") as f:
            corpus_description = json.load(f)
            sub_dir = os.path.basename(os.path.dirname(corpus_description_path))
            module_names.extend(
                [os.path.join(sub_dir, name) for name in corpus_description["modules"]]
            )
            del corpus_description["modules"]
            if len(output_corpus_description) == 0:
                output_corpus_description = corpus_description
            elif corpus_description != output_corpus_description:
                raise ValueError("Input corpora differ by more than modules.")

    output_corpus_description["modules"] = module_names

    with open(os.path.join(root_dir, _FILE_NAME), "w") as f:
        json.dump(output_corpus_description, f, indent=2)
