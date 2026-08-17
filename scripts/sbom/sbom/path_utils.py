# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import os
from functools import lru_cache

PathStr = str
"""Filesystem path represented as a plain string for better performance than pathlib.Path."""


def is_relative_to(path: PathStr, base: PathStr) -> bool:
    return os.path.commonpath([path, base]) == base

@lru_cache(maxsize=None)
def has_link(path: PathStr) -> bool:
    """Returns True if path or any of its ancestor directories is a symlink. Results are cached to avoid duplicate lstat syscalls."""
    if os.path.islink(path):
        return True
    parent = os.path.dirname(path)
    if parent == path:
        return False
    return has_link(parent)
