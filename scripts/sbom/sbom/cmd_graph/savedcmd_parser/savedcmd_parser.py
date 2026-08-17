# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import sbom.sbom_logging as sbom_logging
from sbom.cmd_graph.savedcmd_parser.command_splitter import IfBlock, split_commands
from sbom.cmd_graph.savedcmd_parser.command_parser_registry import CommandParserRegistry
from sbom.cmd_graph.savedcmd_parser.tokenizer import CmdParsingError
from sbom.path_utils import PathStr

DEFAULT_COMMAND_PARSER_REGISTRY = CommandParserRegistry.create()


def parse_inputs_from_commands(
    commands: str,
    fail_on_unknown_build_command: bool,
    registry: CommandParserRegistry | None = None,
) -> list[PathStr]:
    """
    Extract input files referenced in a set of command-line commands.

    Args:
        commands (str): Command line expression to parse.
        fail_on_unknown_build_command (bool): Whether to fail if an unknown build command is encountered. If False, errors are logged as warnings.
        registry (CommandParserRegistry | None): Registry of single command parsers.

    Returns:
        list[PathStr]: List of input file paths required by the commands.
    """

    def log_error_or_warning(message: str, /, **kwargs: str) -> None:
        if fail_on_unknown_build_command:
            sbom_logging.error(message, **kwargs)
        else:
            sbom_logging.warning(message, **kwargs)

    if registry is None:
        registry = DEFAULT_COMMAND_PARSER_REGISTRY

    input_files: list[PathStr] = []
    for single_command in split_commands(commands):
        if isinstance(single_command, IfBlock):
            inputs = parse_inputs_from_commands(single_command.then_statement, fail_on_unknown_build_command, registry)
            if inputs:
                log_error_or_warning(
                    "Skipped parsing command {then_statement} because input files in IfBlock 'then' statement are not supported",
                    then_statement=single_command.then_statement,
                )
            continue

        matched_parser = next((parser for pattern, parser in registry if pattern.match(single_command)), None)
        if matched_parser is None:
            log_error_or_warning(
                "Skipped parsing command {single_command} because no matching parser was found",
                single_command=single_command,
            )
            continue
        try:
            inputs = matched_parser(single_command)
            input_files.extend(inputs)
        except (CmdParsingError, IndexError) as e:
            log_error_or_warning(
                "Skipped parsing command {single_command} because of command parsing error: {error_message}",
                single_command=single_command,
                error_message=str(e),
            )

    return [input.strip().rstrip("/") for input in input_files]
