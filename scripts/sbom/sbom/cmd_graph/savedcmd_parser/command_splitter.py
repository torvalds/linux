# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import re
from dataclasses import dataclass


# If Block pattern to match a simple, single-level if-then-fi block. Nested If blocks are not supported.
IF_BLOCK_PATTERN = re.compile(
    r"""
    ^if(.*?);\s*         # Match 'if <condition>;' (non-greedy)
    then(.*?);\s*        # Match 'then <body>;' (non-greedy)
    fi\b                 # Match 'fi'
    """,
    re.VERBOSE,
)


@dataclass
class IfBlock:
    condition: str
    then_statement: str


def _unwrap_outer_parentheses(s: str) -> str:
    s = s.strip()
    if not (s.startswith("(") and s.endswith(")")):
        return s

    count = 0
    for i, char in enumerate(s):
        if char == "(":
            count += 1
        elif char == ")":
            count -= 1
            # If count is 0 before the end, outer parentheses don't match
            if count == 0 and i != len(s) - 1:
                return s

    # outer parentheses do match, unwrap once
    return _unwrap_outer_parentheses(s[1:-1])


def _find_first_top_level_command_separator(
    commands: str, separators: list[str] = [";", "&&"]
) -> tuple[int | None, int | None]:
    def is_escaped(index: int) -> bool:
        preceding = commands[:index]
        return (len(preceding) - len(preceding.rstrip("\\"))) % 2 == 1

    in_single_quote = False
    in_double_quote = False
    in_curly_braces = 0
    in_braces = 0
    for i, char in enumerate(commands):
        if char == "'" and not in_double_quote and not is_escaped(i):
            # Toggle single quote state (unless inside double quotes or escaped)
            in_single_quote = not in_single_quote
        elif char == '"' and not in_single_quote and not is_escaped(i):
            # Toggle double quote state (unless inside single quotes or escaped)
            in_double_quote = not in_double_quote

        if in_single_quote or in_double_quote:
            continue

        # Toggle braces state
        if char == "{":
            in_curly_braces += 1
        if char == "}":
            in_curly_braces -= 1

        if char == "(":
            in_braces += 1
        if char == ")":
            in_braces -= 1

        if in_curly_braces > 0 or in_braces > 0:
            continue

        # return found separator position and separator length
        for separator in separators:
            if commands[i : i + len(separator)] == separator:
                return i, len(separator)

    return None, None


def split_commands(commands: str) -> list[str | IfBlock]:
    """
    Splits a string of command-line commands into individual parts.

    This function handles:
    - Top-level command separators (e.g., `;` and `&&`) to split multiple commands.
    - Conditional if-blocks, returning them as `IfBlock` instances.
    - Preserves the order of commands and trims whitespace.

    Args:
        commands (str): The raw command string.

    Returns:
        list[str | IfBlock]: A list of single commands or `IfBlock` objects.
    """
    single_commands: list[str | IfBlock] = []
    remaining_commands = _unwrap_outer_parentheses(commands)
    while len(remaining_commands) > 0:
        remaining_commands = remaining_commands.strip()

        # if block
        matched_if = IF_BLOCK_PATTERN.match(remaining_commands)
        if matched_if:
            condition, then_statement = matched_if.groups()
            single_commands.append(IfBlock(condition.strip(), then_statement.strip()))
            full_matched = matched_if.group(0)
            remaining_commands = remaining_commands.removeprefix(full_matched).lstrip("; \n")
            continue

        # command until next separator
        separator_position, separator_length = _find_first_top_level_command_separator(remaining_commands)
        if separator_position is not None and separator_length is not None:
            single_commands.append(remaining_commands[:separator_position].strip())
            remaining_commands = remaining_commands[separator_position + separator_length :].strip()
            continue

        # single last command
        single_commands.append(remaining_commands)
        break

    return single_commands
