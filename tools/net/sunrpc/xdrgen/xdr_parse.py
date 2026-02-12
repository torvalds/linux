#!/usr/bin/env python3
# ex: set filetype=python:

"""Common parsing code for xdrgen"""

import sys
from typing import Callable

from lark import Lark
from lark.exceptions import UnexpectedInput, UnexpectedToken, VisitError


# Set to True to emit annotation comments in generated source
annotate = False

# Set to True to emit enum value validation in decoders
enum_validation = True

# Map internal Lark token names to human-readable names
TOKEN_NAMES = {
    "__ANON_0": "identifier",
    "__ANON_1": "number",
    "SEMICOLON": "';'",
    "LBRACE": "'{'",
    "RBRACE": "'}'",
    "LPAR": "'('",
    "RPAR": "')'",
    "LSQB": "'['",
    "RSQB": "']'",
    "LESSTHAN": "'<'",
    "MORETHAN": "'>'",
    "EQUAL": "'='",
    "COLON": "':'",
    "COMMA": "','",
    "STAR": "'*'",
    "$END": "end of file",
}


class XdrParseError(Exception):
    """Raised when XDR parsing fails"""


def set_xdr_annotate(set_it: bool) -> None:
    """Set 'annotate' if --annotate was specified on the command line"""
    global annotate
    annotate = set_it


def get_xdr_annotate() -> bool:
    """Return True if --annotate was specified on the command line"""
    return annotate


def set_xdr_enum_validation(set_it: bool) -> None:
    """Set 'enum_validation' based on command line options"""
    global enum_validation
    enum_validation = set_it


def get_xdr_enum_validation() -> bool:
    """Return True when enum validation is enabled for decoder generation"""
    return enum_validation


def make_error_handler(source: str, filename: str) -> Callable[[UnexpectedInput], bool]:
    """Create an error handler that reports the first parse error and aborts.

    Args:
        source: The XDR source text being parsed
        filename: The name of the file being parsed

    Returns:
        An error handler function for use with Lark's on_error parameter
    """
    lines = source.splitlines()

    def handle_parse_error(e: UnexpectedInput) -> bool:
        """Report a parse error with context and abort parsing"""
        line_num = e.line
        column = e.column
        line_text = lines[line_num - 1] if 0 < line_num <= len(lines) else ""

        # Build the error message
        msg_parts = [f"{filename}:{line_num}:{column}: parse error"]

        # Show what was found vs what was expected
        if isinstance(e, UnexpectedToken):
            token = e.token
            if token.type == "__ANON_0":
                found = f"identifier '{token.value}'"
            elif token.type == "__ANON_1":
                found = f"number '{token.value}'"
            else:
                found = f"'{token.value}'"
            msg_parts.append(f"Unexpected {found}")

            # Provide helpful expected tokens list
            expected = e.expected
            if expected:
                readable = [
                    TOKEN_NAMES.get(exp, exp.lower().replace("_", " "))
                    for exp in sorted(expected)
                ]
                if len(readable) == 1:
                    msg_parts.append(f"Expected {readable[0]}")
                elif len(readable) <= 4:
                    msg_parts.append(f"Expected one of: {', '.join(readable)}")
        else:
            msg_parts.append(str(e).split("\n")[0])

        # Show the offending line with a caret pointing to the error
        msg_parts.append("")
        msg_parts.append(f"    {line_text}")
        prefix = line_text[: column - 1].expandtabs()
        msg_parts.append(f"    {' ' * len(prefix)}^")

        sys.stderr.write("\n".join(msg_parts) + "\n")
        raise XdrParseError()

    return handle_parse_error


def handle_transform_error(e: VisitError, source: str, filename: str) -> None:
    """Report a transform error with context.

    Args:
        e: The VisitError from Lark's transformer
        source: The XDR source text being parsed
        filename: The name of the file being parsed
    """
    lines = source.splitlines()

    # Extract position from the tree node if available
    line_num = 0
    column = 0
    if hasattr(e.obj, "meta") and e.obj.meta:
        line_num = e.obj.meta.line
        column = e.obj.meta.column

    line_text = lines[line_num - 1] if 0 < line_num <= len(lines) else ""

    # Build the error message
    msg_parts = [f"{filename}:{line_num}:{column}: semantic error"]

    # The original exception is typically a KeyError for undefined types
    if isinstance(e.orig_exc, KeyError):
        msg_parts.append(f"Undefined type '{e.orig_exc.args[0]}'")
    else:
        msg_parts.append(str(e.orig_exc))

    # Show the offending line with a caret pointing to the error
    if line_text:
        msg_parts.append("")
        msg_parts.append(f"    {line_text}")
        prefix = line_text[: column - 1].expandtabs()
        msg_parts.append(f"    {' ' * len(prefix)}^")

    sys.stderr.write("\n".join(msg_parts) + "\n")


def xdr_parser() -> Lark:
    """Return a Lark parser instance configured with the XDR language grammar"""

    return Lark.open(
        "grammars/xdr.lark",
        rel_to=__file__,
        start="specification",
        debug=True,
        strict=True,
        propagate_positions=True,
        parser="lalr",
        lexer="contextual",
    )
