#!/usr/bin/env python3
# ex: set filetype=python:

"""Generate code for an RPC program's procedures"""

from jinja2 import Environment

from generators import SourceGenerator, create_jinja2_environment, get_jinja2_template
from xdr_ast import _RpcProgram, _RpcVersion, excluded_apis
from xdr_ast import max_widths, get_header_name


def emit_version_definitions(
    environment: Environment, program: str, version: _RpcVersion
) -> None:
    """Emit procedure numbers for each RPC version's procedures"""
    template = environment.get_template("definition/open.j2")
    print(template.render(program=program.upper()))

    template = environment.get_template("definition/procedure.j2")
    for procedure in version.procedures:
        if procedure.name not in excluded_apis:
            print(
                template.render(
                    name=procedure.name,
                    value=procedure.number,
                )
            )

    template = environment.get_template("definition/close.j2")
    print(template.render())


def emit_version_declarations(
    environment: Environment, program: str, version: _RpcVersion
) -> None:
    """Emit declarations for each RPC version's procedures"""
    arguments = dict.fromkeys([])
    for procedure in version.procedures:
        if procedure.name not in excluded_apis:
            arguments[procedure.argument.type_name] = None
    if len(arguments) > 0:
        print("")
        template = environment.get_template("declaration/argument.j2")
        for argument in arguments:
            print(template.render(program=program, argument=argument))

    results = dict.fromkeys([])
    for procedure in version.procedures:
        if procedure.name not in excluded_apis:
            results[procedure.result.type_name] = None
    if len(results) > 0:
        print("")
        template = environment.get_template("declaration/result.j2")
        for result in results:
            print(template.render(program=program, result=result))


def emit_version_argument_decoders(
    environment: Environment, program: str, version: _RpcVersion
) -> None:
    """Emit server argument decoders for each RPC version's procedures"""
    arguments = dict.fromkeys([])
    for procedure in version.procedures:
        if procedure.name not in excluded_apis:
            arguments[procedure.argument.type_name] = None

    template = environment.get_template("decoder/argument.j2")
    for argument in arguments:
        print(template.render(program=program, argument=argument))


def emit_version_result_decoders(
    environment: Environment, program: str, version: _RpcVersion
) -> None:
    """Emit client result decoders for each RPC version's procedures"""
    results = dict.fromkeys([])
    for procedure in version.procedures:
        if procedure.name not in excluded_apis:
            results[procedure.result.type_name] = None

    template = environment.get_template("decoder/result.j2")
    for result in results:
        print(template.render(program=program, result=result))


def emit_version_argument_encoders(
    environment: Environment, program: str, version: _RpcVersion
) -> None:
    """Emit client argument encoders for each RPC version's procedures"""
    arguments = dict.fromkeys([])
    for procedure in version.procedures:
        if procedure.name not in excluded_apis:
            arguments[procedure.argument.type_name] = None

    template = environment.get_template("encoder/argument.j2")
    for argument in arguments:
        print(template.render(program=program, argument=argument))


def emit_version_result_encoders(
    environment: Environment, program: str, version: _RpcVersion
) -> None:
    """Emit server result encoders for each RPC version's procedures"""
    results = dict.fromkeys([])
    for procedure in version.procedures:
        if procedure.name not in excluded_apis:
            results[procedure.result.type_name] = None

    template = environment.get_template("encoder/result.j2")
    for result in results:
        print(template.render(program=program, result=result))


class XdrProgramGenerator(SourceGenerator):
    """Generate source code for an RPC program's procedures"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        self.environment = create_jinja2_environment(language, "program")
        self.peer = peer

    def emit_definition(self, node: _RpcProgram) -> None:
        """Emit procedure numbers for each of an RPC programs's procedures"""
        raw_name = node.name
        program = raw_name.lower().removesuffix("_program").removesuffix("_prog")

        for version in node.versions:
            emit_version_definitions(self.environment, program, version)

        template = self.environment.get_template("definition/program.j2")
        print(template.render(name=raw_name, value=node.number))

    def emit_declaration(self, node: _RpcProgram) -> None:
        """Emit a declaration pair for each of an RPC programs's procedures"""
        raw_name = node.name
        program = raw_name.lower().removesuffix("_program").removesuffix("_prog")

        for version in node.versions:
            emit_version_declarations(self.environment, program, version)

    def emit_decoder(self, node: _RpcProgram) -> None:
        """Emit all decoder functions for an RPC program's procedures"""
        raw_name = node.name
        program = raw_name.lower().removesuffix("_program").removesuffix("_prog")
        match self.peer:
            case "server":
                for version in node.versions:
                    emit_version_argument_decoders(
                        self.environment, program, version,
                    )
            case "client":
                for version in node.versions:
                    emit_version_result_decoders(
                        self.environment, program, version,
                    )

    def emit_encoder(self, node: _RpcProgram) -> None:
        """Emit all encoder functions for an RPC program's procedures"""
        raw_name = node.name
        program = raw_name.lower().removesuffix("_program").removesuffix("_prog")
        match self.peer:
            case "server":
                for version in node.versions:
                    emit_version_result_encoders(
                        self.environment, program, version,
                    )
            case "client":
                for version in node.versions:
                    emit_version_argument_encoders(
                        self.environment, program, version,
                    )

    def emit_maxsize(self, node: _RpcProgram) -> None:
        """Emit maxsize macro for maximum RPC argument size"""
        header = get_header_name().upper()

        # Find the largest argument across all versions
        max_arg_width = 0
        max_arg_name = None
        for version in node.versions:
            for procedure in version.procedures:
                if procedure.name in excluded_apis:
                    continue
                arg_name = procedure.argument.type_name
                if arg_name == "void":
                    continue
                if arg_name not in max_widths:
                    continue
                if max_widths[arg_name] > max_arg_width:
                    max_arg_width = max_widths[arg_name]
                    max_arg_name = arg_name

        if max_arg_name is None:
            return

        macro_name = header + "_MAX_ARGS_SZ"
        template = get_jinja2_template(self.environment, "maxsize", "max_args")
        print(
            template.render(
                macro=macro_name,
                width=header + "_" + max_arg_name + "_sz",
            )
        )
