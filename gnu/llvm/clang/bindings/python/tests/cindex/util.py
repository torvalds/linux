# This file provides common utility functions for the test suite.

import os

HAS_FSPATH = hasattr(os, "fspath")

if HAS_FSPATH:
    from pathlib import Path as str_to_path
else:
    str_to_path = None

import unittest

from clang.cindex import Cursor
from clang.cindex import TranslationUnit


def get_tu(source, lang="c", all_warnings=False, flags=[]):
    """Obtain a translation unit from source and language.

    By default, the translation unit is created from source file "t.<ext>"
    where <ext> is the default file extension for the specified language. By
    default it is C, so "t.c" is the default file name.

    Supported languages are {c, cpp, objc}.

    all_warnings is a convenience argument to enable all compiler warnings.
    """
    args = list(flags)
    name = "t.c"
    if lang == "cpp":
        name = "t.cpp"
        args.append("-std=c++11")
    elif lang == "objc":
        name = "t.m"
    elif lang != "c":
        raise Exception("Unknown language: %s" % lang)

    if all_warnings:
        args += ["-Wall", "-Wextra"]

    return TranslationUnit.from_source(name, args, unsaved_files=[(name, source)])


def get_cursor(source, spelling):
    """Obtain a cursor from a source object.

    This provides a convenient search mechanism to find a cursor with specific
    spelling within a source. The first argument can be either a
    TranslationUnit or Cursor instance.

    If the cursor is not found, None is returned.
    """
    # Convenience for calling on a TU.
    root_cursor = source if isinstance(source, Cursor) else source.cursor

    for cursor in root_cursor.walk_preorder():
        if cursor.spelling == spelling:
            return cursor

    return None


def get_cursors(source, spelling):
    """Obtain all cursors from a source object with a specific spelling.

    This provides a convenient search mechanism to find all cursors with
    specific spelling within a source. The first argument can be either a
    TranslationUnit or Cursor instance.

    If no cursors are found, an empty list is returned.
    """
    # Convenience for calling on a TU.
    root_cursor = source if isinstance(source, Cursor) else source.cursor

    cursors = []
    for cursor in root_cursor.walk_preorder():
        if cursor.spelling == spelling:
            cursors.append(cursor)

    return cursors


skip_if_no_fspath = unittest.skipUnless(
    HAS_FSPATH, "Requires file system path protocol / Python 3.6+"
)

__all__ = [
    "get_cursor",
    "get_cursors",
    "get_tu",
    "skip_if_no_fspath",
    "str_to_path",
]
