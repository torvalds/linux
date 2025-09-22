#!/usr/bin/env python3
# ===----------------------------------------------------------------------===##
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===----------------------------------------------------------------------===##
"""A linter that detects potential typos in FileCheck directive names.

Consider a broken test foo.cpp:

// RUN: clang -cc1 -ast-dump %s | FileCheck %s --check-prefix=NEW
// RUN: clang -cc1 -ast-dump %s -std=c++98 | FileCheck %s --check-prefix=OLD
auto x = 42;
// NEWW: auto is a c++11 extension
// ODL-NOT: auto is a c++11 extension

We first detect the locally valid FileCheck directive prefixes by parsing the
--check-prefix flags. Here we get {CHECK, NEW, OLD}, so our directive names are
{CHECK, NEW, OLD, CHECK-NOT, NEW-NOT, ...}.

Then we look for lines that look like directives. These are of the form 'FOO:',
usually at the beginning of a line or a comment. If any of these are a
"near-miss" for a directive name, then we suspect this is a typo and report it.

Usage: filecheck_lint path/to/test/file/1 ... path/to/test/file/n
"""

import itertools
import logging
import pathlib
import re
import sys
from typing import Generator, Sequence, Tuple

_distance_threshold = 3
_prefixes = {"CHECK"}
_suffixes = {"-DAG", "-COUNT", "-EMPTY", "-LABEL", "-NEXT", "-NOT", "-SAME"}
# 'NOTE' and 'TODO' are not directives, but are likely to be false positives
# if encountered and to generate noise as a result. We filter them out also to
# avoid this.
_lit_directives = {
    "RUN",
    "REQUIRES",
    "UNSUPPORTED",
    "XFAIL",
    "DEFINE",
    "REDEFINE",
}
# 'COM' and 'RUN' are default comment prefixes for FileCheck.
_comment_prefixes = {"COM", "RUN"}
_ignore = _lit_directives.union(_comment_prefixes).union({"NOTE", "TODO"})


def levenshtein(s1: str, s2: str) -> int:  # pylint: disable=g-doc-args
    """Computes the edit distance between two strings.

    Additions, deletions, and substitutions all count as a single operation.
    """
    if not s1:
        return len(s2)
    if not s2:
        return len(s1)

    distances = range(len(s2) + 1)
    for i in range(len(s1)):
        new_distances = [i + 1]
        for j in range(len(s2)):
            cost = min(
                distances[j] + int(s1[i] != s2[j]),
                distances[j + 1] + 1,
                new_distances[-1] + 1,
            )
            new_distances.append(cost)
        distances = new_distances
    return distances[-1]


class FileRange:
    """Stores the coordinates of a span on a single line within a file.

    Attributes:
      content:    line str
      start_byte: the (inclusive) byte offset the span starts
      end_byte:   the (inclusive) byte offset the span ends
    """

    content: str
    start_byte: int
    end_byte: int

    def __init__(
        self, content: str, start_byte: int, end_byte: int
    ):  # pylint: disable=g-doc-args
        """
        Stores the coordinates of a span based on a string and start/end bytes.

        `start_byte` and `end_byte` are assumed to be on the same line.
        """
        self.content = content
        self.start_byte = start_byte
        self.end_byte = end_byte

    def as_str(self):
        """
        Derives span from line and coordinates.

        start_column: the (inclusive) column where the span starts
        end_column:   the (inclusive) column where the span ends
        """
        content_before_span = self.content[: self.start_byte]
        line = content_before_span.count("\n") + 1
        start_column = self.start_byte - content_before_span.rfind("\n")
        end_column = start_column + (self.end_byte - self.start_byte - 1)

        return f"{line}:{start_column}-{end_column}"


class Diagnostic:
    """Stores information about one typo and a suggested fix.

    Attributes:
      filepath:   the path to the file in which the typo was found
      filerange:  the position at which the typo was found in the file
      typo:       the typo
      fix:        a suggested fix
    """

    filepath: pathlib.Path
    filerange: FileRange
    typo: str
    fix: str

    def __init__(
        self,
        filepath: pathlib.Path,
        filerange: FileRange,
        typo: str,
        fix: str,  # pylint: disable=redefined-outer-name
    ):
        self.filepath = filepath
        self.filerange = filerange
        self.typo = typo
        self.fix = fix

    def __str__(self) -> str:
        return f"{self.filepath}:" + self.filerange.as_str() + f": {self.summary()}"

    def summary(self) -> str:
        return (
            f'Found potentially misspelled directive "{self.typo}". Did you mean '
            f'"{self.fix}"?'
        )


def find_potential_directives(
    content: str,
) -> Generator[Tuple[FileRange, str], None, None]:
    """Extracts all the potential FileCheck directives from a string.

    What constitutes a potential directive is loosely defined---we err on the side
    of capturing more strings than is necessary, rather than missing any.

    Args:
      content: the string in which to look for directives

    Yields:
      Tuples (p, d) where p is the span where the potential directive occurs
      within the string and d is the potential directive.
    """
    directive_pattern = re.compile(
        r"(?:^|//|;|#)[^\d\w\-_]*([\d\w\-_][\s\d\w\-_]*):", re.MULTILINE
    )
    for match in re.finditer(directive_pattern, content):
        potential_directive, span = match.group(1), match.span(1)
        yield (FileRange(content, span[0], span[1]), potential_directive)


# TODO(bchetioui): also parse comment prefixes to ignore.
def parse_custom_prefixes(
    content: str,
) -> Generator[str, None, None]:  # pylint: disable=g-doc-args
    """Parses custom prefixes defined in the string provided.

    For example, given the following file content:
      RUN: something | FileCheck %s -check-prefixes CHECK1,CHECK2
      RUN: something_else | FileCheck %s -check-prefix 'CHECK3'

    the custom prefixes are CHECK1, CHECK2, and CHECK3.
    """
    param_re = r"|".join([r"'[^']*'", r'"[^"]*"', r'[^\'"\s]+'])
    for m in re.finditer(
        r"-check-prefix(?:es)?(?:\s+|=)({})".format(param_re), content
    ):
        prefixes = m.group(1)
        if prefixes.startswith("'") or prefixes.startswith('"'):
            prefixes = prefixes[1:-1]
        for prefix in prefixes.split(","):
            yield prefix


def find_directive_typos(
    content: str,
    filepath: pathlib.Path,
    threshold: int = 3,
) -> Generator[Diagnostic, None, None]:
    """Detects potential typos in FileCheck directives.

    Args:
      content: the content of the file
      filepath: the path to the file to check for typos in directives
      threshold: the (inclusive) maximum edit distance between a potential
        directive and an actual directive, such that the potential directive is
        classified as a typo

    Yields:
      Diagnostics, in order from the top of the file.
    """
    all_prefixes = _prefixes.union(set(parse_custom_prefixes(content)))
    all_directives = (
        [
            f"{prefix}{suffix}"
            for prefix, suffix in itertools.product(all_prefixes, _suffixes)
        ]
        + list(_ignore)
        + list(all_prefixes)
    )

    def find_best_match(typo):
        return min(
            [(threshold + 1, typo)]
            + [
                (levenshtein(typo, d), d)
                for d in all_directives
                if abs(len(d) - len(typo)) <= threshold
            ],
            key=lambda tup: tup[0],
        )

    potential_directives = find_potential_directives(content)
    # Cache score and best_match to skip recalculating.
    score_and_best_match_for_potential_directive = dict()
    for filerange, potential_directive in potential_directives:
        # TODO(bchetioui): match count directives more finely. We skip directives
        # starting with 'CHECK-COUNT-' for the moment as they require more complex
        # logic to be handled correctly.
        if any(
            potential_directive.startswith(f"{prefix}-COUNT-")
            for prefix in all_prefixes
        ):
            continue

        # Ignoring potential typos that will not be matched later due to a too low
        # threshold, in order to avoid potentially long computation times.
        if len(potential_directive) > max(map(len, all_directives)) + threshold:
            continue

        if potential_directive not in score_and_best_match_for_potential_directive:
            score, best_match = find_best_match(potential_directive)
            score_and_best_match_for_potential_directive[potential_directive] = (
                score,
                best_match,
            )
        else:
            score, best_match = score_and_best_match_for_potential_directive[
                potential_directive
            ]
        if score == 0:  # This is an actual directive, ignore.
            continue
        elif score <= threshold and best_match not in _ignore:
            yield Diagnostic(filepath, filerange, potential_directive, best_match)


def main(argv: Sequence[str]):
    if len(argv) < 2:
        print(f"Usage: {argv[0]} path/to/file/1 ... path/to/file/n")
        exit(1)

    for filepath in argv[1:]:
        logging.info("Checking %s", filepath)
        with open(filepath, "rt") as f:
            content = f.read()
        for diagnostic in find_directive_typos(
            content,
            pathlib.Path(filepath),
            threshold=_distance_threshold,
        ):
            print(diagnostic)


if __name__ == "__main__":
    main(sys.argv)
