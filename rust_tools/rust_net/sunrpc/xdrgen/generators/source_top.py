#!/usr/bin/env python3
# ex: set filetype=python:

"""Generate source code boilerplate"""

import os.path
import time

from generators import Boilerplate
from generators import find_xdr_program_name, create_jinja2_environment
from xdr_ast import _RpcProgram, Specification, get_header_name


class XdrSourceTopGenerator(Boilerplate):
    """Generate source code boilerplate"""

    def __init__(self, language: str, peer: str):
        """Initialize an instance of this class"""
        self.environment = create_jinja2_environment(language, "source_top")
        self.peer = peer

    def emit_source(self, filename: str, root: Specification) -> None:
        """Emit the top source boilerplate"""
        name = find_xdr_program_name(root)
        template = self.environment.get_template(self.peer + ".j2")
        print(
            template.render(
                program=name,
                filename=filename,
                mtime=time.ctime(os.path.getmtime(filename)),
            )
        )
