# SPDX-License-Identifier: GPL-2.0-only OR MIT
# Copyright (C) 2025 TNG Technology Consulting GmbH

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
from enum import Enum
import os
from typing import Any
from sbom.path_utils import PathStr


class KernelSpdxDocumentKind(Enum):
    SOURCE = "source"
    BUILD = "build"
    OUTPUT = "output"


@dataclass
class KernelSbomConfig:
    src_tree: PathStr
    """Absolute path to the Linux kernel source directory."""

    obj_tree: PathStr
    """Absolute path to the build output directory."""

    root_paths: list[PathStr]
    """List of paths to root outputs (relative to obj_tree) to base the SBOM on."""

    generate_spdx: bool
    """Whether to generate SPDX SBOM documents. If False, no SPDX files are created."""

    spdx_file_names: dict[KernelSpdxDocumentKind, str]
    """If `generate_spdx` is True, defines the file names for each SPDX SBOM kind
    (source, build, output) to store on disk."""

    generate_used_files: bool
    """Whether to generate a flat list of all source files used in the build.
    If False, no used-files document is created."""

    used_files_file_name: str
    """If `generate_used_files` is True, specifies the file name for the used-files document."""

    output_directory: PathStr
    """Path to the directory where the generated output documents will be saved."""

    debug: bool
    """Whether to enable debug logging."""

    fail_on_unknown_build_command: bool
    """Whether to fail if an unknown build command is encountered in a .cmd file."""

    write_output_on_error: bool
    """Whether to write output documents even if errors occur."""

    created: datetime
    """Datetime to use for the SPDX created property of the CreationInfo element."""

    spdxId_prefix: str
    """Prefix to use for all SPDX element IDs."""

    build_type: str
    """SPDX buildType property to use for all Build elements."""

    build_id: str | None
    """SPDX buildId property to use for all Build elements."""

    package_license: str
    """License expression applied to all SPDX Packages."""

    package_version: str | None
    """Version string applied to all SPDX Packages."""

    package_copyright_text: str | None
    """Copyright text applied to all SPDX Packages."""

    prettify_json: bool
    """Whether to pretty-print generated SPDX JSON documents."""


def _parse_cli_arguments(parser: argparse.ArgumentParser) -> dict[str, Any]:
    """
    Parse command-line arguments using argparse.

    Returns:
        Dictionary of parsed arguments.
    """
    parser.add_argument(
        "--src-tree",
        default="../linux",
        help="Path to the kernel source tree (default: ../linux)",
    )
    parser.add_argument(
        "--obj-tree",
        default="../linux/kernel_build",
        help="Path to the build output directory (default: ../linux/kernel_build)",
    )
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument(
        "--roots",
        nargs="+",
        help="Space-separated list of paths relative to obj-tree for which the SBOM will be created.\n"
        "Cannot be used together with --roots-file.",
    )
    group.add_argument(
        "--roots-file",
        help="Path to a file containing the root paths (one per line). Cannot be used together with --roots.",
    )
    parser.add_argument(
        "--generate-spdx",
        action="store_true",
        default=False,
        help=(
            "Whether to create sbom-source.spdx.json, sbom-build.spdx.json and "
            "sbom-output.spdx.json documents (default: False)"
        ),
    )
    parser.add_argument(
        "--generate-used-files",
        action="store_true",
        default=False,
        help=(
            "Whether to create the sbom.used-files.txt file, a flat list of all "
            "source files used for the kernel build.\n"
            "If src-tree and obj-tree are equal it is not possible to reliably "
            "classify source files.\n"
            "In this case sbom.used-files.txt will contain all files used for the "
            "kernel build including all build artifacts. (default: False)"
        ),
    )
    parser.add_argument(
        "--output-directory",
        default=".",
        help="Path to the directory where the generated output documents will be stored (default: .)",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        default=False,
        help="Enable debug logs (default: False)",
    )

    # Error handling settings
    parser.add_argument(
        "--do-not-fail-on-unknown-build-command",
        action="store_true",
        default=False,
        help=(
            "Whether to fail if an unknown build command is encountered in a .cmd file.\n"
            "If set to True, errors are logged as warnings instead. (default: False)"
        ),
    )
    parser.add_argument(
        "--write-output-on-error",
        action="store_true",
        default=False,
        help=(
            "Write output documents even if errors occur. The resulting documents "
            "may be incomplete.\n"
            "A summary of warnings and errors can be found in the 'comment' property "
            "of the CreationInfo element. (default: False)"
        ),
    )

    # SPDX specific options
    spdx_group = parser.add_argument_group("SPDX options", "Options for customizing SPDX document generation")
    spdx_group.add_argument(
        "--spdxId-prefix",
        default="urn:spdx.dev:",
        help="The prefix to use for all spdxId properties. (default: urn:spdx.dev:)",
    )
    spdx_group.add_argument(
        "--build-type",
        default="urn:spdx.dev:Kbuild",
        help="The SPDX buildType property to use for all Build elements. (default: urn:spdx.dev:Kbuild)",
    )
    spdx_group.add_argument(
        "--build-id",
        default=None,
        help="The SPDX buildId property to use for all Build elements.\n"
        "If not provided the spdxId of the high level Build element is used as the buildId. (default: None)",
    )
    spdx_group.add_argument(
        "--package-license",
        default="NOASSERTION",
        help=(
            "The SPDX licenseExpression property to use for the LicenseExpression "
            "linked to all SPDX Package elements. (default: NOASSERTION)"
        ),
    )
    spdx_group.add_argument(
        "--package-version",
        default=None,
        help="The SPDX packageVersion property to use for all SPDX Package elements. (default: None)",
    )
    spdx_group.add_argument(
        "--package-copyright-text",
        default=None,
        help=(
            "The SPDX copyrightText property to use for all SPDX Package elements.\n"
            "If not specified, and if a COPYING file exists in the source tree,\n"
            "the package-copyright-text is set to the content of this file. "
            "(default: None)"
        ),
    )
    spdx_group.add_argument(
        "--prettify-json",
        action="store_true",
        default=False,
        help="Whether to pretty print the generated spdx.json documents (default: False)",
    )

    args = vars(parser.parse_args())
    return args


def get_config() -> KernelSbomConfig:
    """
    Parse command-line arguments and construct the configuration object.

    Returns:
        KernelSbomConfig: Configuration object with all settings for SBOM generation.
    """
    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawTextHelpFormatter,
        description="Generate SPDX SBOM documents for kernel builds",
    )
    args = _parse_cli_arguments(parser)

    # Extract and validate cli arguments
    src_tree = os.path.realpath(args["src_tree"])
    obj_tree = os.path.realpath(args["obj_tree"])
    root_paths = []
    if args["roots_file"]:
        with open(args["roots_file"], "rt", encoding="utf-8") as f:
            root_paths = [root.strip() for root in f.readlines()]
        if len(root_paths) == 0:
            parser.error("--roots-file must contain at least one path")
    else:
        root_paths = args["roots"]
    _validate_path_arguments(parser, src_tree, obj_tree, root_paths)

    generate_spdx = args["generate_spdx"]
    generate_used_files = args["generate_used_files"]
    output_directory = os.path.realpath(args["output_directory"])
    debug = args["debug"]

    fail_on_unknown_build_command = not args["do_not_fail_on_unknown_build_command"]
    write_output_on_error = args["write_output_on_error"]

    created = datetime.fromtimestamp(
        max([os.path.getmtime(os.path.join(obj_tree, root_path)) for root_path in root_paths]),
        tz=timezone.utc,
    )
    spdxId_prefix = args["spdxId_prefix"]
    build_type = args["build_type"]
    build_id = args["build_id"]
    package_license = args["package_license"]
    package_version = args["package_version"] if args["package_version"] is not None else None
    package_copyright_text: str | None = None
    if args["package_copyright_text"] is not None:
        package_copyright_text = args["package_copyright_text"]
    elif os.path.isfile(copying_path := os.path.join(src_tree, "COPYING")):
        with open(copying_path, "r", encoding="utf-8") as f:
            package_copyright_text = f.read()
    prettify_json = args["prettify_json"]

    # Hardcoded config
    spdx_file_names = {
        KernelSpdxDocumentKind.SOURCE: "sbom-source.spdx.json",
        KernelSpdxDocumentKind.BUILD: "sbom-build.spdx.json",
        KernelSpdxDocumentKind.OUTPUT: "sbom-output.spdx.json",
    }
    used_files_file_name = "sbom.used-files.txt"

    return KernelSbomConfig(
        src_tree=src_tree,
        obj_tree=obj_tree,
        root_paths=root_paths,
        generate_spdx=generate_spdx,
        spdx_file_names=spdx_file_names,
        generate_used_files=generate_used_files,
        used_files_file_name=used_files_file_name,
        output_directory=output_directory,
        debug=debug,
        fail_on_unknown_build_command=fail_on_unknown_build_command,
        write_output_on_error=write_output_on_error,
        created=created,
        spdxId_prefix=spdxId_prefix,
        build_type=build_type,
        build_id=build_id,
        package_license=package_license,
        package_version=package_version,
        package_copyright_text=package_copyright_text,
        prettify_json=prettify_json,
    )


def _validate_path_arguments(
    parser: argparse.ArgumentParser,
    src_tree: PathStr,
    obj_tree: PathStr,
    root_paths: list[PathStr],
) -> None:
    """
    Validate that the provided paths exist.

    Args:
        parser: The argument parser, used to emit well-formatted error messages.
        src_tree: Absolute path to the source tree.
        obj_tree: Absolute path to the object tree.
        root_paths: List of root paths relative to obj_tree.
    """
    if not os.path.exists(src_tree):
        parser.error(f"--src-tree {src_tree} does not exist")
    if not os.path.exists(obj_tree):
        parser.error(f"--obj-tree {obj_tree} does not exist")
    for root_path in root_paths:
        if not os.path.isfile(root_path_absolute := os.path.join(obj_tree, root_path)):
            parser.error(f"path to root artifact {root_path_absolute} is not a file")
