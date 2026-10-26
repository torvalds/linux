#!/usr/bin/env python3
# ex: set filetype=python:

"""Generate code to handle XDR struct types"""

from jinja2 import Environment

from generators import SourceGenerator, kernel_c_type
from generators import create_jinja2_environment, get_jinja2_template

from xdr_ast import _XdrBasic, _XdrString
from xdr_ast import _XdrFixedLengthOpaque, _XdrVariableLengthOpaque
from xdr_ast import _XdrFixedLengthArray, _XdrVariableLengthArray
from xdr_ast import _XdrOptionalData, _XdrStruct, _XdrDeclaration
from xdr_ast import public_apis, get_header_name, aggregate_members
from xdr_ast import pages_members, pages_member_maxsize, pages_member_is_decoded


def aggregate_hook_base(struct_name: str) -> str:
    """Return the application hook base name for a struct's aggregate members.

    The pragma header name prefixes it so two programs that share a
    type name (nfs_acl2 vs nfs_acl3) derive distinct external hook
    symbols. All marked members of one struct share a hook set; the
    cursor's member_id tells them apart.
    """
    return "_".join((get_header_name(), struct_name))


def aggregate_member_symbol(struct_name: str, member_name: str) -> str:
    """Return the symbolic member id for one marked aggregate member.

    The generated framing sets the cursor's member_id to this constant
    and the hooks compare against it. The enum in aggregate_hooks.j2
    assigns the values implicitly in field order, so a hook that
    compared against the bare integer would silently bind to the
    wrong member once the specification's members were reordered.
    """
    return "_".join((aggregate_hook_base(struct_name), member_name)).upper()


def emit_struct_declaration(environment: Environment, node: _XdrStruct) -> None:
    """Emit one declaration pair for an XDR struct type"""
    if node.name in public_apis:
        template = get_jinja2_template(environment, "declaration", "close")
        print(template.render(name=node.name))
    marked = [
        field
        for field in node.fields
        if (node.name, field.name) in aggregate_members
    ]
    if marked:
        template = get_jinja2_template(
            environment, "declaration", "aggregate_hooks"
        )
        print(
            template.render(
                hook=aggregate_hook_base(node.name),
                c_type=kernel_c_type(marked[0].spec),
                classifier=marked[0].spec.c_classifier,
                members=[
                    aggregate_member_symbol(node.name, field.name)
                    for field in marked
                ],
            )
        )


def emit_struct_member_definition(
    environment: Environment, field: _XdrDeclaration, struct_name: str, peer: str
) -> None:
    """Emit a definition for one field in an XDR struct"""
    if peer == "server" and pages_member_is_decoded(struct_name, field.name):
        template = get_jinja2_template(environment, "definition", "pages_opaque")
        print(template.render(name=field.name))
        return
    if isinstance(field, _XdrBasic):
        template = get_jinja2_template(environment, "definition", field.template)
        print(
            template.render(
                name=field.name,
                type=kernel_c_type(field.spec),
                classifier=field.spec.c_classifier,
            )
        )
    elif isinstance(field, _XdrFixedLengthOpaque):
        template = get_jinja2_template(environment, "definition", field.template)
        print(
            template.render(
                name=field.name,
                size=field.size,
            )
        )
    elif isinstance(field, _XdrVariableLengthOpaque):
        template = get_jinja2_template(environment, "definition", field.template)
        print(template.render(name=field.name))
    elif isinstance(field, _XdrString):
        template = get_jinja2_template(environment, "definition", field.template)
        print(template.render(name=field.name))
    elif isinstance(field, _XdrFixedLengthArray):
        template = get_jinja2_template(environment, "definition", field.template)
        print(
            template.render(
                name=field.name,
                type=kernel_c_type(field.spec),
                size=field.size,
            )
        )
    elif isinstance(field, _XdrVariableLengthArray):
        template = get_jinja2_template(environment, "definition", field.template)
        print(
            template.render(
                name=field.name,
                type=kernel_c_type(field.spec),
                classifier=field.spec.c_classifier,
            )
        )
    elif isinstance(field, _XdrOptionalData):
        template = get_jinja2_template(environment, "definition", field.template)
        print(
            template.render(
                name=field.name,
                type=kernel_c_type(field.spec),
                classifier=field.spec.c_classifier,
            )
        )


def emit_struct_definition(
    environment: Environment, node: _XdrStruct, peer: str
) -> None:
    """Emit one definition for an XDR struct type"""
    template = get_jinja2_template(environment, "definition", "open")
    print(template.render(name=node.name))

    for field in node.fields:
        emit_struct_member_definition(environment, field, node.name, peer)

    template = get_jinja2_template(environment, "definition", "close")
    print(template.render(name=node.name))


def emit_struct_member_decoder(
    environment: Environment,
    field: _XdrDeclaration,
    struct_name: str,
    peer: str,
) -> None:
    """Emit a decoder for one field in an XDR struct"""
    if peer == "server" and pages_member_is_decoded(struct_name, field.name):
        template = get_jinja2_template(environment, "decoder", "pages_opaque")
        print(
            template.render(
                name=field.name,
                maxsize=pages_member_maxsize(field),
            )
        )
        return
    if isinstance(field, _XdrVariableLengthArray) and (
        (struct_name, field.name) in aggregate_members
    ):
        if peer != "server":
            raise NotImplementedError(
                "pragma aggregate is server-side only; "
                + peer
                + " generation is not yet supported"
            )
        template = get_jinja2_template(environment, "decoder", "aggregate_array")
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                c_type=kernel_c_type(field.spec),
                classifier=field.spec.c_classifier,
                maxsize=field.maxsize,
                hook=aggregate_hook_base(struct_name),
                member_sym=aggregate_member_symbol(struct_name, field.name),
            )
        )
        return
    if isinstance(field, _XdrOptionalData) and (
        (struct_name, field.name) in aggregate_members
    ):
        if peer != "server":
            raise NotImplementedError(
                "pragma aggregate is server-side only; "
                + peer
                + " generation is not yet supported"
            )
        template = get_jinja2_template(environment, "decoder", "aggregate_optional")
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                c_type=kernel_c_type(field.spec),
                classifier=field.spec.c_classifier,
                hook=aggregate_hook_base(struct_name),
                member_sym=aggregate_member_symbol(struct_name, field.name),
            )
        )
        return
    if isinstance(field, _XdrBasic):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                classifier=field.spec.c_classifier,
            )
        )
    elif isinstance(field, _XdrFixedLengthOpaque):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                size=field.size,
            )
        )
    elif isinstance(field, _XdrVariableLengthOpaque):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                maxsize=field.maxsize,
            )
        )
    elif isinstance(field, _XdrString):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                maxsize=field.maxsize,
            )
        )
    elif isinstance(field, _XdrFixedLengthArray):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                size=field.size,
                classifier=field.spec.c_classifier,
            )
        )
    elif isinstance(field, _XdrVariableLengthArray):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                maxsize=field.maxsize,
                classifier=field.spec.c_classifier,
            )
        )
    elif isinstance(field, _XdrOptionalData):
        template = get_jinja2_template(environment, "decoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                classifier=field.spec.c_classifier,
            )
        )


def emit_struct_decoder(
    environment: Environment, node: _XdrStruct, peer: str
) -> None:
    """Emit one decoder function for an XDR struct type"""
    template = get_jinja2_template(environment, "decoder", "open")
    print(template.render(name=node.name))

    for field in node.fields:
        emit_struct_member_decoder(environment, field, node.name, peer)
        # The optional-data aggregate decoder fails and closes the
        # function; the members after it are never decoded.
        if isinstance(field, _XdrOptionalData) and (
            (node.name, field.name) in aggregate_members
        ):
            return

    template = get_jinja2_template(environment, "decoder", "close")
    print(template.render())


def emit_struct_member_encoder(
    environment: Environment,
    field: _XdrDeclaration,
    struct_name: str,
    peer: str,
) -> None:
    """Emit an encoder for one field in an XDR struct"""
    if isinstance(field, _XdrVariableLengthArray) and (
        (struct_name, field.name) in aggregate_members
    ):
        if peer != "server":
            raise NotImplementedError(
                "pragma aggregate is server-side only; "
                + peer
                + " generation is not yet supported"
            )
        template = get_jinja2_template(environment, "encoder", "aggregate_array")
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                c_type=kernel_c_type(field.spec),
                classifier=field.spec.c_classifier,
                maxsize=field.maxsize,
                hook=aggregate_hook_base(struct_name),
                member_sym=aggregate_member_symbol(struct_name, field.name),
            )
        )
        return
    if isinstance(field, _XdrOptionalData) and (
        (struct_name, field.name) in aggregate_members
    ):
        if peer != "server":
            raise NotImplementedError(
                "pragma aggregate is server-side only; "
                + peer
                + " generation is not yet supported"
            )
        template = get_jinja2_template(environment, "encoder", "aggregate_optional")
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                c_type=kernel_c_type(field.spec),
                classifier=field.spec.c_classifier,
                hook=aggregate_hook_base(struct_name),
                member_sym=aggregate_member_symbol(struct_name, field.name),
            )
        )
        return
    if (struct_name, field.name) in pages_members:
        if peer != "server":
            raise NotImplementedError(
                "pragma pages is server-side only; "
                + peer
                + " generation is not yet supported"
            )
        # Both representations carry the length field, which is all the
        # pages encoder reads, so pages_member_is_decoded() does not
        # gate this site.
        template = get_jinja2_template(environment, "encoder", "pages_opaque")
        print(
            template.render(
                name=field.name,
                maxsize=pages_member_maxsize(field),
            )
        )
        return
    if isinstance(field, _XdrBasic):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
            )
        )
    elif isinstance(field, _XdrFixedLengthOpaque):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                size=field.size,
            )
        )
    elif isinstance(field, _XdrVariableLengthOpaque):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                maxsize=field.maxsize,
            )
        )
    elif isinstance(field, _XdrString):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                maxsize=field.maxsize,
            )
        )
    elif isinstance(field, _XdrFixedLengthArray):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                size=field.size,
            )
        )
    elif isinstance(field, _XdrVariableLengthArray):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                maxsize=field.maxsize,
            )
        )
    elif isinstance(field, _XdrOptionalData):
        template = get_jinja2_template(environment, "encoder", field.template)
        print(
            template.render(
                name=field.name,
                type=field.spec.type_name,
                classifier=field.spec.c_classifier,
            )
        )


def emit_struct_encoder(environment: Environment, node: _XdrStruct, peer: str) -> None:
    """Emit one encoder function for an XDR struct type"""
    template = get_jinja2_template(environment, "encoder", "open")
    print(template.render(name=node.name))

    for field in node.fields:
        emit_struct_member_encoder(environment, field, node.name, peer)

    template = get_jinja2_template(environment, "encoder", "close")
    print(template.render())


def emit_struct_maxsize(environment: Environment, node: _XdrStruct) -> None:
    """Emit one maxsize macro for an XDR struct type"""
    macro_name = get_header_name().upper() + "_" + node.name + "_sz"
    template = get_jinja2_template(environment, "maxsize", "struct")
    print(
        template.render(
            macro=macro_name,
            width=" + ".join(node.symbolic_width()),
        )
    )


class XdrStructGenerator(SourceGenerator):
    """Generate source code for XDR structs"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        self.environment = create_jinja2_environment(language, "struct")
        self.peer = peer

    def emit_declaration(self, node: _XdrStruct) -> None:
        """Emit one declaration pair for an XDR struct type"""
        emit_struct_declaration(self.environment, node)

    def emit_definition(self, node: _XdrStruct) -> None:
        """Emit one definition for an XDR struct type"""
        emit_struct_definition(self.environment, node, self.peer)

    def emit_decoder(self, node: _XdrStruct) -> None:
        """Emit one decoder function for an XDR struct type"""
        emit_struct_decoder(self.environment, node, self.peer)

    def emit_encoder(self, node: _XdrStruct) -> None:
        """Emit one encoder function for an XDR struct type"""
        emit_struct_encoder(self.environment, node, self.peer)

    def emit_maxsize(self, node: _XdrStruct) -> None:
        """Emit one maxsize macro for an XDR struct type"""
        emit_struct_maxsize(self.environment, node)
