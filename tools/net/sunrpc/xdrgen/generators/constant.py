#!/usr/bin/env python3
# ex: set filetype=python:

"""Generate code to handle XDR constants"""

from generators import SourceGenerator, create_jinja2_environment
from xdr_ast import _XdrConstant

class XdrConstantGenerator(SourceGenerator):
    """Generate source code for XDR constants"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        self.environment = create_jinja2_environment(language, "constants")
        self.peer = peer

    def emit_definition(self, node: _XdrConstant) -> None:
        """Emit one definition for a constant"""
        template = self.environment.get_template("definition.j2")
        print(template.render(name=node.name, value=node.value))
