#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""Tests for revert_checker.

Note that these tests require having LLVM's git history available, since our
repository has a few interesting instances of edge-cases.
"""

import os
import logging
import unittest
from typing import List

import revert_checker

# pylint: disable=protected-access


def get_llvm_project_path() -> str:
    """Returns the path to llvm-project's root."""
    my_dir = os.path.dirname(__file__)
    return os.path.realpath(os.path.join(my_dir, "..", ".."))


class _SilencingFilter(logging.Filter):
    """Silences all log messages.

    Also collects info about log messages that would've been emitted.
    """

    def __init__(self) -> None:
        self.messages: List[str] = []

    def filter(self, record: logging.LogRecord) -> bool:
        self.messages.append(record.getMessage())
        return False


class Test(unittest.TestCase):
    """Tests for revert_checker."""

    def silence_logging(self) -> _SilencingFilter:
        root = logging.getLogger()
        filt = _SilencingFilter()
        root.addFilter(filt)
        self.addCleanup(root.removeFilter, filt)
        return filt

    def test_log_stream_with_known_sha_range(self) -> None:
        start_sha = "e241573d5972d34a323fa5c64774c4207340beb3"
        end_sha = "a7a37517751ffb0f5529011b4ba96e67fcb27510"
        commits = [
            revert_checker._LogEntry(
                "e241573d5972d34a323fa5c64774c4207340beb3",
                "\n".join(
                    (
                        "[mlir] NFC: remove IntegerValueSet / MutableIntegerSet",
                        "",
                        "Summary:",
                        "- these are unused and really not needed now given flat "
                        "affine",
                        "  constraints",
                        "",
                        "Differential Revision: https://reviews.llvm.org/D75792",
                    )
                ),
            ),
            revert_checker._LogEntry(
                "97572fa6e9daecd648873496fd11f7d1e25a55f0",
                "[NFC] use hasAnyOperatorName and hasAnyOverloadedOperatorName "
                "functions in clang-tidy matchers",
            ),
        ]

        logs = list(
            revert_checker._log_stream(
                get_llvm_project_path(),
                root_sha=start_sha,
                end_at_sha=end_sha,
            )
        )
        self.assertEqual(commits, logs)

    def test_reverted_noncommit_object_is_a_nop(self) -> None:
        log_filter = self.silence_logging()
        # c9944df916e41b1014dff5f6f75d52297b48ecdc mentions reverting a non-commit
        # object. It sits between the given base_ref and root.
        reverts = revert_checker.find_reverts(
            git_dir=get_llvm_project_path(),
            across_ref="c9944df916e41b1014dff5f6f75d52297b48ecdc~",
            root="c9944df916e41b1014dff5f6f75d52297b48ecdc",
        )
        self.assertEqual(reverts, [])

        complaint = (
            "Failed to resolve reverted object "
            "edd18355be574122aaa9abf58c15d8c50fb085a1"
        )
        self.assertTrue(
            any(x.startswith(complaint) for x in log_filter.messages),
            log_filter.messages,
        )

    def test_known_reverts_across_arbitrary_llvm_rev(self) -> None:
        reverts = revert_checker.find_reverts(
            git_dir=get_llvm_project_path(),
            across_ref="c47f971694be0159ffddfee8a75ae515eba91439",
            root="9f981e9adf9c8d29bb80306daf08d2770263ade6",
        )
        self.assertEqual(
            reverts,
            [
                revert_checker.Revert(
                    sha="4e0fe038f438ae1679eae9e156e1f248595b2373",
                    reverted_sha="65b21282c710afe9c275778820c6e3c1cf46734b",
                ),
                revert_checker.Revert(
                    sha="9f981e9adf9c8d29bb80306daf08d2770263ade6",
                    reverted_sha="4060016fce3e6a0b926ee9fc59e440a612d3a2ec",
                ),
            ],
        )


if __name__ == "__main__":
    unittest.main()
