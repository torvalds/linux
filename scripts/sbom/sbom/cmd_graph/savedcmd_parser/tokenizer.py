# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import re
import shlex
from dataclasses import dataclass
from typing import Union


class CmdParsingError(Exception):
    pass


@dataclass
class Option:
    name: str
    value: str | None = None


@dataclass
class Positional:
    value: str


_SUBCOMMAND_PATTERN = re.compile(r"\$\$\(([^()]*)\)")
"""Pattern to match $$(...) blocks"""


def tokenize_single_command(command: str, flag_options: list[str] | None = None) -> list[Union[Option, Positional]]:
    """
    Parse a shell command into a list of Options and Positionals.
    - Positional: the command and any positional arguments.
    - Options: handles flags and options with values provided as space-separated, or equals-sign
        (e.g., '--opt val', '--opt=val', '--flag').

    Args:
        command: Command line string.
        flag_options: Options that are flags without values (e.g., '--verbose').

    Returns:
        List of `Option` and `Positional` objects in command order.
    """

    #  Wrap all $$(...) blocks in double quotes to prevent shlex from splitting them.
    command_with_protected_subcommands = _SUBCOMMAND_PATTERN.sub(lambda m: f'"$$({m.group(1)})"', command)
    tokens = shlex.split(command_with_protected_subcommands)

    parsed: list[Option | Positional] = []
    i = 0
    while i < len(tokens):
        token = tokens[i]

        # Positional
        if not token.startswith("-"):
            parsed.append(Positional(token))
            i += 1
            continue

        # Option without value (--flag)
        if (token.startswith("-") and i + 1 < len(tokens) and tokens[i + 1].startswith("-")) or (
            flag_options and token in flag_options
        ):
            parsed.append(Option(name=token))
            i += 1
            continue

        # Option with equals sign (--opt=val)
        if "=" in token:
            name, value = token.split("=", 1)
            parsed.append(Option(name=name, value=value))
            i += 1
            continue

        # Option with space-separated value (--opt val)
        if i + 1 < len(tokens) and not tokens[i + 1].startswith("-"):
            parsed.append(Option(name=token, value=tokens[i + 1]))
            i += 2
            continue

        raise CmdParsingError(f"Unrecognized token: {token} in command {command}")

    return parsed


def tokenize_single_command_positionals_only(command: str) -> list[str]:
    command_parts = tokenize_single_command(command)
    positionals = [p.value for p in command_parts if isinstance(p, Positional)]
    if len(positionals) != len(command_parts):
        raise CmdParsingError(
            f"Invalid command format: expected positional arguments only but got options in command {command}."
        )
    return positionals
