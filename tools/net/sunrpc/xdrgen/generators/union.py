#!/usr/bin/env python3
# ex: set filetype=python:

"""Generate code to handle XDR unions"""

from jinja2 import Environment

from generators import SourceGenerator
from generators import create_jinja2_environment, get_jinja2_template

from xdr_ast import _XdrBasic, _XdrUnion, _XdrVoid, get_header_name
from xdr_ast import _XdrDeclaration, _XdrCaseSpec, public_apis, big_endian


def emit_union_declaration(environment: Environment, node: _XdrUnion) -> None:
    """Emit one declaration pair for an XDR union type"""
    if node.name in public_apis:
        template = get_jinja2_template(environment, "declaration", "close")
        print(template.render(name=node.name))


def emit_union_switch_spec_definition(
    environment: Environment, node: _XdrDeclaration
) -> None:
    """Emit a definition for an XDR union's discriminant"""
    assert isinstance(node, _XdrBasic)
    template = get_jinja2_template(environment, "definition", "switch_spec")
    print(
        template.render(
            name=node.name,
            type=node.spec.type_name,
            classifier=node.spec.c_classifier,
        )
    )


def emit_union_case_spec_definition(
    environment: Environment, node: _XdrDeclaration
) -> None:
    """Emit a definition for an XDR union's case arm"""
    if isinstance(node.arm, _XdrVoid):
        return
    assert isinstance(node.arm, _XdrBasic)
    template = get_jinja2_template(environment, "definition", "case_spec")
    print(
        template.render(
            name=node.arm.name,
            type=node.arm.spec.type_name,
            classifier=node.arm.spec.c_classifier,
        )
    )


def emit_union_definition(environment: Environment, node: _XdrUnion) -> None:
    """Emit one XDR union definition"""
    template = get_jinja2_template(environment, "definition", "open")
    print(template.render(name=node.name))

    emit_union_switch_spec_definition(environment, node.discriminant)

    for case in node.cases:
        emit_union_case_spec_definition(environment, case)

    if node.default is not None:
        emit_union_case_spec_definition(environment, node.default)

    template = get_jinja2_template(environment, "definition", "close")
    print(template.render(name=node.name))


def emit_union_switch_spec_decoder(
    environment: Environment, node: _XdrDeclaration
) -> None:
    """Emit a decoder for an XDR union's discriminant"""
    assert isinstance(node, _XdrBasic)
    template = get_jinja2_template(environment, "decoder", "switch_spec")
    print(template.render(name=node.name, type=node.spec.type_name))


def emit_union_case_spec_decoder(
    environment: Environment, node: _XdrCaseSpec, big_endian_discriminant: bool
) -> None:
    """Emit decoder functions for an XDR union's case arm"""

    if isinstance(node.arm, _XdrVoid):
        return

    if big_endian_discriminant:
        template = get_jinja2_template(environment, "decoder", "case_spec_be")
    else:
        template = get_jinja2_template(environment, "decoder", "case_spec")
    for case in node.values:
        print(template.render(case=case))

    assert isinstance(node.arm, _XdrBasic)
    template = get_jinja2_template(environment, "decoder", node.arm.template)
    print(
        template.render(
            name=node.arm.name,
            type=node.arm.spec.type_name,
            classifier=node.arm.spec.c_classifier,
        )
    )

    template = get_jinja2_template(environment, "decoder", "break")
    print(template.render())


def emit_union_default_spec_decoder(environment: Environment, node: _XdrUnion) -> None:
    """Emit a decoder function for an XDR union's default arm"""
    default_case = node.default

    # Avoid a gcc warning about a default case with boolean discriminant
    if default_case is None and node.discriminant.spec.type_name == "bool":
        return

    template = get_jinja2_template(environment, "decoder", "default_spec")
    print(template.render())

    if default_case is None or isinstance(default_case.arm, _XdrVoid):
        template = get_jinja2_template(environment, "decoder", "break")
        print(template.render())
        return

    assert isinstance(default_case.arm, _XdrBasic)
    template = get_jinja2_template(environment, "decoder", default_case.arm.template)
    print(
        template.render(
            name=default_case.arm.name,
            type=default_case.arm.spec.type_name,
            classifier=default_case.arm.spec.c_classifier,
        )
    )


def emit_union_decoder(environment: Environment, node: _XdrUnion) -> None:
    """Emit one XDR union decoder"""
    template = get_jinja2_template(environment, "decoder", "open")
    print(template.render(name=node.name))

    emit_union_switch_spec_decoder(environment, node.discriminant)

    for case in node.cases:
        emit_union_case_spec_decoder(
            environment,
            case,
            node.discriminant.spec.type_name in big_endian,
        )

    emit_union_default_spec_decoder(environment, node)

    template = get_jinja2_template(environment, "decoder", "close")
    print(template.render())


def emit_union_switch_spec_encoder(
    environment: Environment, node: _XdrDeclaration
) -> None:
    """Emit an encoder for an XDR union's discriminant"""
    assert isinstance(node, _XdrBasic)
    template = get_jinja2_template(environment, "encoder", "switch_spec")
    print(template.render(name=node.name, type=node.spec.type_name))


def emit_union_case_spec_encoder(
    environment: Environment, node: _XdrCaseSpec, big_endian_discriminant: bool
) -> None:
    """Emit encoder functions for an XDR union's case arm"""

    if isinstance(node.arm, _XdrVoid):
        return

    if big_endian_discriminant:
        template = get_jinja2_template(environment, "encoder", "case_spec_be")
    else:
        template = get_jinja2_template(environment, "encoder", "case_spec")
    for case in node.values:
        print(template.render(case=case))

    template = get_jinja2_template(environment, "encoder", node.arm.template)
    print(
        template.render(
            name=node.arm.name,
            type=node.arm.spec.type_name,
        )
    )

    template = get_jinja2_template(environment, "encoder", "break")
    print(template.render())


def emit_union_default_spec_encoder(environment: Environment, node: _XdrUnion) -> None:
    """Emit an encoder function for an XDR union's default arm"""
    default_case = node.default

    # Avoid a gcc warning about a default case with boolean discriminant
    if default_case is None and node.discriminant.spec.type_name == "bool":
        return

    template = get_jinja2_template(environment, "encoder", "default_spec")
    print(template.render())

    if default_case is None or isinstance(default_case.arm, _XdrVoid):
        template = get_jinja2_template(environment, "encoder", "break")
        print(template.render())
        return

    template = get_jinja2_template(environment, "encoder", default_case.arm.template)
    print(
        template.render(
            name=default_case.arm.name,
            type=default_case.arm.spec.type_name,
        )
    )


def emit_union_encoder(environment, node: _XdrUnion) -> None:
    """Emit one XDR union encoder"""
    template = get_jinja2_template(environment, "encoder", "open")
    print(template.render(name=node.name))

    emit_union_switch_spec_encoder(environment, node.discriminant)

    for case in node.cases:
        emit_union_case_spec_encoder(
            environment,
            case,
            node.discriminant.spec.type_name in big_endian,
        )

    emit_union_default_spec_encoder(environment, node)

    template = get_jinja2_template(environment, "encoder", "close")
    print(template.render())


def emit_union_maxsize(environment: Environment, node: _XdrUnion) -> None:
    """Emit one maxsize macro for an XDR union type"""
    macro_name = get_header_name().upper() + "_" + node.name + "_sz"
    template = get_jinja2_template(environment, "maxsize", "union")
    print(
        template.render(
            macro=macro_name,
            width=" + ".join(node.symbolic_width()),
        )
    )


class XdrUnionGenerator(SourceGenerator):
    """Generate source code for XDR unions"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        self.environment = create_jinja2_environment(language, "union")
        self.peer = peer

    def emit_declaration(self, node: _XdrUnion) -> None:
        """Emit one declaration pair for an XDR union"""
        emit_union_declaration(self.environment, node)

    def emit_definition(self, node: _XdrUnion) -> None:
        """Emit one definition for an XDR union"""
        emit_union_definition(self.environment, node)

    def emit_decoder(self, node: _XdrUnion) -> None:
        """Emit one decoder function for an XDR union"""
        emit_union_decoder(self.environment, node)

    def emit_encoder(self, node: _XdrUnion) -> None:
        """Emit one encoder function for an XDR union"""
        emit_union_encoder(self.environment, node)

    def emit_maxsize(self, node: _XdrUnion) -> None:
        """Emit one maxsize macro for an XDR union"""
        emit_union_maxsize(self.environment, node)
