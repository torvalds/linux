#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""generate_rust_analyzer - Generates the `rust-project.json` file for `rust-analyzer`.
"""

import argparse
import json
import logging
import os
import pathlib
import subprocess
import sys
from typing import Dict, Iterable, List, Literal, Optional, TypedDict

def invoke_rustc(args: List[str]) -> str:
    return subprocess.check_output(
        [os.environ["RUSTC"]] + args,
        stdin=subprocess.DEVNULL,
    ).decode('utf-8').strip()

def args_crates_cfgs(cfgs: List[str]) -> Dict[str, List[str]]:
    crates_cfgs = {}
    for cfg in cfgs:
        crate, vals = cfg.split("=", 1)
        crates_cfgs[crate] = vals.split()

    return crates_cfgs

class Dependency(TypedDict):
    crate: int
    name: str


class Source(TypedDict):
    include_dirs: List[str]
    exclude_dirs: List[str]


class Crate(TypedDict):
    display_name: str
    root_module: str
    is_workspace_member: bool
    deps: List[Dependency]
    cfg: List[str]
    edition: str
    env: Dict[str, str]


class ProcMacroCrate(Crate):
    is_proc_macro: Literal[True]
    proc_macro_dylib_path: str  # `pathlib.Path` is not JSON serializable.


class CrateWithGenerated(Crate):
    source: Source


def generate_crates(
    srctree: pathlib.Path,
    objtree: pathlib.Path,
    sysroot_src: pathlib.Path,
    external_src: Optional[pathlib.Path],
    cfgs: List[str],
    core_edition: str,
) -> List[Crate]:
    # Generate the configuration list.
    generated_cfg = []
    with open(objtree / "include" / "generated" / "rustc_cfg") as fd:
        for line in fd:
            line = line.replace("--cfg=", "")
            line = line.replace("\n", "")
            generated_cfg.append(line)

    # Now fill the crates list.
    crates: List[Crate] = []
    crates_cfgs = args_crates_cfgs(cfgs)

    def get_crate_name(path: pathlib.Path) -> str:
        return invoke_rustc(["--print", "crate-name", str(path)])

    def build_crate(
        display_name: str,
        root_module: pathlib.Path,
        deps: List[Dependency],
        *,
        cfg: Optional[List[str]],
        is_workspace_member: Optional[bool],
        edition: Optional[str],
    ) -> Crate:
        cfg = cfg if cfg is not None else crates_cfgs.get(display_name, [])
        is_workspace_member = (
            is_workspace_member if is_workspace_member is not None else True
        )
        edition = edition if edition is not None else "2021"
        return {
            "display_name": display_name,
            "root_module": str(root_module),
            "is_workspace_member": is_workspace_member,
            "deps": deps,
            "cfg": cfg,
            "edition": edition,
            "env": {
                "RUST_MODFILE": "This is only for rust-analyzer"
            }
        }

    def append_proc_macro_crate(
        display_name: str,
        root_module: pathlib.Path,
        deps: List[Dependency],
        *,
        cfg: Optional[List[str]] = None,
        is_workspace_member: Optional[bool] = None,
        edition: Optional[str] = None,
    ) -> Dependency:
        crate = build_crate(
            display_name,
            root_module,
            deps,
            cfg=cfg,
            is_workspace_member=is_workspace_member,
            edition=edition,
        )
        proc_macro_dylib_name = invoke_rustc([
            "--print",
            "file-names",
            "--crate-name",
            display_name,
            "--crate-type",
            "proc-macro",
            "-",
        ])
        proc_macro_crate: ProcMacroCrate = {
            **crate,
            "is_proc_macro": True,
            "proc_macro_dylib_path": str(objtree / "rust" / proc_macro_dylib_name),
        }
        return register_crate(proc_macro_crate)

    def register_crate(crate: Crate) -> Dependency:
        index = len(crates)
        crates.append(crate)
        return {"crate": index, "name": crate["display_name"]}

    def append_crate(
        display_name: str,
        root_module: pathlib.Path,
        deps: List[Dependency],
        *,
        cfg: Optional[List[str]] = None,
        is_workspace_member: Optional[bool] = None,
        edition: Optional[str] = None,
    ) -> Dependency:
        return register_crate(
            build_crate(
                display_name,
                root_module,
                deps,
                cfg=cfg,
                is_workspace_member=is_workspace_member,
                edition=edition,
            )
        )

    def append_sysroot_crate(
        display_name: str,
        deps: List[Dependency],
        *,
        cfg: Optional[List[str]] = None,
    ) -> Dependency:
        return append_crate(
            display_name,
            sysroot_src / display_name / "src" / "lib.rs",
            deps,
            cfg=cfg,
            is_workspace_member=False,
            # Miguel Ojeda writes:
            #
            # > ... in principle even the sysroot crates may have different
            # > editions.
            # >
            # > For instance, in the move to 2024, it seems all happened at once
            # > in 1.87.0 in these upstream commits:
            # >
            # >     0e071c2c6a58 ("Migrate core to Rust 2024")
            # >     f505d4e8e380 ("Migrate alloc to Rust 2024")
            # >     0b2489c226c3 ("Migrate proc_macro to Rust 2024")
            # >     993359e70112 ("Migrate std to Rust 2024")
            # >
            # > But in the previous move to 2021, `std` moved in 1.59.0, while
            # > the others in 1.60.0:
            # >
            # >     b656384d8398 ("Update stdlib to the 2021 edition")
            # >     06a1c14d52a8 ("Switch all libraries to the 2021 edition")
            #
            # Link: https://lore.kernel.org/all/CANiq72kd9bHdKaAm=8xCUhSHMy2csyVed69bOc4dXyFAW4sfuw@mail.gmail.com/
            #
            # At the time of writing all rust versions we support build the
            # sysroot crates with the same edition. We may need to relax this
            # assumption if future edition moves span multiple rust versions.
            edition=core_edition,
        )

    # NB: sysroot crates reexport items from one another so setting up our transitive dependencies
    # here is important for ensuring that rust-analyzer can resolve symbols. The sources of truth
    # for this dependency graph are `(sysroot_src / crate / "Cargo.toml" for crate in crates)`.
    core = append_sysroot_crate("core", [])
    alloc = append_sysroot_crate("alloc", [core])
    std = append_sysroot_crate("std", [alloc, core])
    proc_macro = append_sysroot_crate("proc_macro", [core, std])

    compiler_builtins = append_crate(
        "compiler_builtins",
        srctree / "rust" / "compiler_builtins.rs",
        [core],
    )

    proc_macro2 = append_crate(
        "proc_macro2",
        srctree / "rust" / "proc-macro2" / "lib.rs",
        [core, alloc, std, proc_macro],
    )

    quote = append_crate(
        "quote",
        srctree / "rust" / "quote" / "lib.rs",
        [core, alloc, std, proc_macro, proc_macro2],
        edition="2018",
    )

    syn = append_crate(
        "syn",
        srctree / "rust" / "syn" / "lib.rs",
        [std, proc_macro, proc_macro2, quote],
    )

    macros = append_proc_macro_crate(
        "macros",
        srctree / "rust" / "macros" / "lib.rs",
        [std, proc_macro, proc_macro2, quote, syn],
    )

    build_error = append_crate(
        "build_error",
        srctree / "rust" / "build_error.rs",
        [core, compiler_builtins],
    )

    pin_init_internal = append_proc_macro_crate(
        "pin_init_internal",
        srctree / "rust" / "pin-init" / "internal" / "src" / "lib.rs",
        [std, proc_macro, proc_macro2, quote, syn],
    )

    pin_init = append_crate(
        "pin_init",
        srctree / "rust" / "pin-init" / "src" / "lib.rs",
        [core, compiler_builtins, pin_init_internal, macros],
    )

    ffi = append_crate(
        "ffi",
        srctree / "rust" / "ffi.rs",
        [core, compiler_builtins],
    )

    def append_crate_with_generated(
        display_name: str,
        deps: List[Dependency],
    ) -> Dependency:
        crate = build_crate(
            display_name,
            srctree / "rust"/ display_name / "lib.rs",
            deps,
            cfg=generated_cfg,
            is_workspace_member=True,
            edition=None,
        )
        crate["env"]["OBJTREE"] = str(objtree.resolve(True))
        crate_with_generated: CrateWithGenerated = {
            **crate,
            "source": {
                "include_dirs": [
                    str(srctree / "rust" / display_name),
                    str(objtree / "rust"),
                ],
                "exclude_dirs": [],
            },
        }
        return register_crate(crate_with_generated)

    bindings = append_crate_with_generated("bindings", [core, ffi, pin_init])
    uapi = append_crate_with_generated("uapi", [core, ffi, pin_init])
    kernel = append_crate_with_generated(
        "kernel", [core, macros, build_error, pin_init, ffi, bindings, uapi]
    )

    scripts = srctree / "scripts"
    makefile = (scripts / "Makefile").read_text()
    for path in scripts.glob("*.rs"):
        name = path.stem
        if f"{name}-rust" not in makefile:
            continue
        append_crate(
            name,
            path,
            [std],
        )

    def is_root_crate(build_file: pathlib.Path, target: str) -> bool:
        try:
            contents = build_file.read_text()
        except FileNotFoundError:
            return False
        return f"{target}.o" in contents

    # Then, the rest outside of `rust/`.
    #
    # We explicitly mention the top-level folders we want to cover.
    extra_dirs: Iterable[pathlib.Path] = (
        srctree / dir for dir in ("samples", "drivers")
    )
    if external_src is not None:
        extra_dirs = [external_src]
    for folder in extra_dirs:
        for path in folder.rglob("*.rs"):
            logging.info("Checking %s", path)
            file_name = path.stem

            # Skip those that are not crate roots.
            if not is_root_crate(path.parent / "Makefile", file_name) and \
               not is_root_crate(path.parent / "Kbuild", file_name):
                continue

            crate_name = get_crate_name(path)
            logging.info("Adding %s", crate_name)
            append_crate(
                crate_name,
                path,
                [core, kernel, pin_init],
                cfg=generated_cfg,
            )

    return crates

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument('--verbose', '-v', action='store_true')
    parser.add_argument('--cfgs', action='append', default=[])
    parser.add_argument("core_edition")
    parser.add_argument("srctree", type=pathlib.Path)
    parser.add_argument("objtree", type=pathlib.Path)
    parser.add_argument("sysroot", type=pathlib.Path)
    parser.add_argument("sysroot_src", type=pathlib.Path)
    parser.add_argument("exttree", type=pathlib.Path, nargs="?")

    class Args(argparse.Namespace):
        verbose: bool
        cfgs: List[str]
        srctree: pathlib.Path
        objtree: pathlib.Path
        sysroot: pathlib.Path
        sysroot_src: pathlib.Path
        exttree: Optional[pathlib.Path]
        core_edition: str

    args = parser.parse_args(namespace=Args())

    logging.basicConfig(
        format="[%(asctime)s] [%(levelname)s] %(message)s",
        level=logging.INFO if args.verbose else logging.WARNING
    )

    rust_project = {
        "crates": generate_crates(args.srctree, args.objtree, args.sysroot_src, args.exttree, args.cfgs, args.core_edition),
        "sysroot": str(args.sysroot),
    }

    json.dump(rust_project, sys.stdout, sort_keys=True, indent=4)

if __name__ == "__main__":
    main()
