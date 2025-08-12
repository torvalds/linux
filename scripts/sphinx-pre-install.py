#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2017-2025 Mauro Carvalho Chehab <mchehab+huawei@kernel.org>
#
# pylint: disable=C0103,C0114,C0115,C0116,C0301
# pylint: disable=R0902,R0904,R0912,R0915,R1705,R1710,E1121

# Note: this script requires at least Python 3.6 to run.
# Don't add changes not compatible with it, it is meant to report
# incompatible python versions.

import argparse
import os
import re
import subprocess
import sys
from glob import glob


def parse_version(version):
    """Convert a major.minor.patch version into a tuple"""
    return tuple(int(x) for x in version.split("."))


def ver_str(version):
    """Returns a version tuple as major.minor.patch"""

    return ".".join([str(x) for x in version])


RECOMMENDED_VERSION = parse_version("3.4.3")
MIN_PYTHON_VERSION = parse_version("3.7")


class DepManager:

    # Internal types of dependencies.
    _SYS_TYPE = 0
    _PHY_TYPE = 1
    _PDF_TYPE = 2

    # Let's define keys as a tuple with the type and mandatory/optional.
    # This way, checking for optional or type is easy.

    SYSTEM_MANDATORY = (_SYS_TYPE, True)
    PYTHON_MANDATORY = (_PHY_TYPE, True)
    PDF_MANDATORY = (_PDF_TYPE, True)

    # Currently we're not using all optional types, but let's keep all
    # combinations here, as we may end needing them in the future. Also,
    # it allows a name() function that handles all possibilities.
    SYSTEM_OPTIONAL = (_SYS_TYPE, False)
    PYTHON_OPTIONAL = (_PHY_TYPE, False)
    PDF_OPTIONAL = (_PDF_TYPE, True)

    def __init__(self, pdf):
        self.missing = {}
        self.run = {}
        self.need = 0
        self.optional = 0
        self.pdf = pdf

    @staticmethod
    def name(dtype):
        if dtype[0] == DepManager._SYS_TYPE:
            msg = "build"
        elif dtype[0] == DepManager._PHY_TYPE:
            msg = "Python"
        else:
            msg = "PDF"

        if dtype[1]:
            return f"ERROR: {msg} mandatory deps missing"
        else:
            out = f"Warning: {msg} optional deps missing"

    @staticmethod
    def is_optional(dtype):
        return not dtype[1]

    @staticmethod
    def is_pdf(dtype):
        if (dtype[0] == DepManager._PDF_TYPE):
            return True

        return False

    def add_package(self, package, dtype):
        is_optional = DepManager.is_optional(dtype)
        self.missing[package] = dtype
        if is_optional:
            self.optional += 1
        else:
            self.need += 1

    def del_package(self, package):
        if package in self.missing:
            del self.missing[package]

    def clear_deps(self):
        """
        Clear dependencies without changing needed/optional.

        This is an ackward way to have a separate section to recommend
        a package after system main dependencies.

        TODO: rework the logic to prevent needing it
        """

        self.missing = {}

    def check_missing(self, progs):
        self.run = {}

        for prog, dtype in sorted(self.missing.items()):
            # At least on some LTS distros like CentOS 7, texlive doesn't
            # provide all packages we need. When such distros are
            # detected, we have to disable PDF output.
            #
            # So, we need to ignore the packages that distros would
            # need for LaTeX to work
            if DepManager.is_pdf(dtype) and not self.pdf:
                self.optional -= 1
                continue

            if not dtype in self.run:
                self.run[dtype] = []

            self.run[dtype].append(progs.get(prog, prog))

        install = []
        for dtype in self.run.keys():
            install += self.run[dtype]

        return " ".join(sorted(set(install)))

    def warn_install(self):

        output_msg = ""

        for dtype in sorted(self.run.keys()):
            progs = " ".join(sorted(set(self.run[dtype])))

            try:
                name = DepManager.name(dtype)
                output_msg += f'{name}:\t{progs}\n'
            except KeyError:
                raise KeyError(f"ERROR!!!: invalid dtype for {progs}: {dtype}")

        if output_msg:
            print(f"\n{output_msg}\n")

class SphinxDependencyChecker:
    # List of required texlive packages on Fedora and OpenSuse
    texlive = {
        "amsfonts.sty":       "texlive-amsfonts",
        "amsmath.sty":        "texlive-amsmath",
        "amssymb.sty":        "texlive-amsfonts",
        "amsthm.sty":         "texlive-amscls",
        "anyfontsize.sty":    "texlive-anyfontsize",
        "atbegshi.sty":       "texlive-oberdiek",
        "bm.sty":             "texlive-tools",
        "capt-of.sty":        "texlive-capt-of",
        "cmap.sty":           "texlive-cmap",
        "ctexhook.sty":       "texlive-ctex",
        "ecrm1000.tfm":       "texlive-ec",
        "eqparbox.sty":       "texlive-eqparbox",
        "eu1enc.def":         "texlive-euenc",
        "fancybox.sty":       "texlive-fancybox",
        "fancyvrb.sty":       "texlive-fancyvrb",
        "float.sty":          "texlive-float",
        "fncychap.sty":       "texlive-fncychap",
        "footnote.sty":       "texlive-mdwtools",
        "framed.sty":         "texlive-framed",
        "luatex85.sty":       "texlive-luatex85",
        "multirow.sty":       "texlive-multirow",
        "needspace.sty":      "texlive-needspace",
        "palatino.sty":       "texlive-psnfss",
        "parskip.sty":        "texlive-parskip",
        "polyglossia.sty":    "texlive-polyglossia",
        "tabulary.sty":       "texlive-tabulary",
        "threeparttable.sty": "texlive-threeparttable",
        "titlesec.sty":       "texlive-titlesec",
        "ucs.sty":            "texlive-ucs",
        "upquote.sty":        "texlive-upquote",
        "wrapfig.sty":        "texlive-wrapfig",
    }

    def __init__(self, args):
        self.pdf = args.pdf
        self.virtualenv = args.virtualenv
        self.version_check = args.version_check

        self.deps = DepManager(self.pdf)

        self.need_symlink = 0
        self.need_sphinx = 0
        self.need_pip = 0
        self.rec_sphinx_upgrade = 0
        self.verbose_warn_install = 1

        self.system_release = ""
        self.install = ""
        self.virtenv_dir = ""
        self.python_cmd = ""
        self.activate_cmd = ""

        # Some distros may not have a Sphinx shipped package compatible with
        # our minimal requirements
        self.package_supported = True

        # Recommend a new python version
        self.recommend_python = None

        # Certain hints are meant to be shown only once
        self.first_hint = True

        self.min_version = (0, 0, 0)
        self.cur_version = (0, 0, 0)
        self.latest_avail_ver = (0, 0, 0)
        self.venv_ver = (0, 0, 0)

        prefix = os.environ.get("srctree", ".") + "/"

        self.conf = prefix + "Documentation/conf.py"
        self.requirement_file = prefix + "Documentation/sphinx/requirements.txt"
        self.virtenv_prefix = ["sphinx_", "Sphinx_" ]

    #
    # Ancillary methods that don't depend on self
    #

    @staticmethod
    def which(prog):
        for path in os.environ.get("PATH", "").split(":"):
            full_path = os.path.join(path, prog)
            if os.access(full_path, os.X_OK):
                return full_path

        return None

    @staticmethod
    def get_python_version(cmd):

        result = SphinxDependencyChecker.run([cmd, "--version"],
                                            capture_output=True, text=True)
        version = result.stdout.strip()

        match = re.search(r"(\d+\.\d+\.\d+)", version)
        if match:
            return parse_version(match.group(1))

        print(f"Can't parse version {version}")
        return (0, 0, 0)

    @staticmethod
    def find_python():

        patterns = [
            "python3.[0-9]",
            "python3.[0-9][0-9]",
        ]

        new_python_cmd = None

        # Seek for a python binary newer than MIN_PYTHON_VERSION
        for path in os.getenv("PATH", "").split(":"):
            for pattern in patterns:
                for cmd in glob(os.path.join(path, pattern)):
                    if os.path.isfile(cmd) and os.access(cmd, os.X_OK):
                        version = SphinxDependencyChecker.get_python_version(cmd)
                        if version >= MIN_PYTHON_VERSION:
                            return(cmd)

    @staticmethod
    def check_python():

        cur_ver = sys.version_info[:3]
        if cur_ver >= MIN_PYTHON_VERSION:
            ver = ver_str(cur_ver)
            print(f"Python version: {ver}")

            # This could be useful for debugging purposes
            if SphinxDependencyChecker.which("docutils"):
                result = SphinxDependencyChecker.run(["docutils", "--version"],
                                                    capture_output=True, text=True)
                ver = result.stdout.strip()
                match = re.search(r"(\d+\.\d+\.\d+)", ver)
                if match:
                    ver = match.group(1)

                print(f"Docutils version: {ver}")

            return

        python_ver = ver_str(cur_ver)

        new_python_cmd = SphinxDependencyChecker.find_python()
        if not new_python_cmd:
            print(f"ERROR: Python version {python_ver} is not spported anymore")
            print(f"       Can't find a new version. This script may fail")
            return

        # Restart script using the newer version
        script_path = os.path.abspath(sys.argv[0])
        args = [new_python_cmd, script_path] + sys.argv[1:]

        print(f"Python {python_ver} not supported. Changing to {new_python_cmd}")

        try:
            os.execv(new_python_cmd, args)
        except OSError as e:
            sys.exit(f"Failed to restart with {new_python_cmd}: {e}")

    @staticmethod
    def run(*args, **kwargs):
        """Excecute a command, hiding its output by default"""

        capture_output = kwargs.pop('capture_output', False)

        if capture_output:
            if 'stdout' not in kwargs:
                kwargs['stdout'] = subprocess.PIPE
            if 'stderr' not in kwargs:
                kwargs['stderr'] = subprocess.PIPE
        else:
            if 'stdout' not in kwargs:
                kwargs['stdout'] = subprocess.DEVNULL
            if 'stderr' not in kwargs:
                kwargs['stderr'] = subprocess.DEVNULL

        # Don't break with older Python versions
        if 'text' in kwargs and sys.version_info < (3, 7):
            kwargs['universal_newlines'] = kwargs.pop('text')

        return subprocess.run(*args, **kwargs)

    #
    # Methods to check if a feature exists
    #

    def check_missing_file(self, files, package, dtype):
        for f in files:
            if os.path.exists(f):
                return
        self.deps.add_package(package, dtype)

    def check_program(self, prog, dtype):
        found = self.which(prog)
        if found:
            return found

        self.deps.add_package(prog, dtype)

        return None

    def check_perl_module(self, prog, dtype):
        # While testing with lxc download template, one of the
        # distros (Oracle) didn't have perl - nor even an option to install
        # before installing oraclelinux-release-el9 package.
        #
        # Check it before running an error. If perl is not there,
        # add it as a mandatory package, as some parts of the doc builder
        # needs it.
        if not self.which("perl"):
            self.deps.add_package("perl", DepManager.SYSTEM_MANDATORY)
            self.deps.add_package(prog, dtype)
            return

        try:
            self.run(["perl", f"-M{prog}", "-e", "1"], check=True)
        except subprocess.CalledProcessError:
            self.deps.add_package(prog, dtype)

    def check_python_module(self, module, is_optional=False):
        if is_optional:
            dtype = DepManager.PYTHON_OPTIONAL
        else:
            dtype = DepManager.PYTHON_MANDATORY

        try:
            self.run([self.python_cmd, "-c", f"import {module}"], check=True)
        except subprocess.CalledProcessError:
            self.deps.add_package(module, dtype)

    def check_rpm_missing(self, pkgs, dtype):
        for prog in pkgs:
            try:
                self.run(["rpm", "-q", prog], check=True)
            except subprocess.CalledProcessError:
                self.deps.add_package(prog, dtype)

    def check_pacman_missing(self, pkgs, dtype):
        for prog in pkgs:
            try:
                self.run(["pacman", "-Q", prog], check=True)
            except subprocess.CalledProcessError:
                self.deps.add_package(prog, dtype)

    def check_missing_tex(self, is_optional=False):
        if is_optional:
            dtype = DepManager.PDF_OPTIONAL
        else:
            dtype = DepManager.PDF_MANDATORY

        kpsewhich = self.which("kpsewhich")
        for prog, package in self.texlive.items():

            # If kpsewhich is not there, just add it to deps
            if not kpsewhich:
                self.deps.add_package(package, dtype)
                continue

            # Check if the package is needed
            try:
                result = self.run(
                    [kpsewhich, prog], stdout=subprocess.PIPE, text=True, check=True
                )

                # Didn't find. Add it
                if not result.stdout.strip():
                    self.deps.add_package(package, dtype)

            except subprocess.CalledProcessError:
                # kpsewhich returned an error. Add it, just in case
                self.deps.add_package(package, dtype)

    def get_sphinx_fname(self):
        if "SPHINXBUILD" in os.environ:
            return os.environ["SPHINXBUILD"]

        fname = "sphinx-build"
        if self.which(fname):
            return fname

        fname = "sphinx-build-3"
        if self.which(fname):
            self.need_symlink = 1
            return fname

        return ""

    def get_sphinx_version(self, cmd):
        try:
            result = self.run([cmd, "--version"],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT,
                              text=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            return None

        for line in result.stdout.split("\n"):
            match = re.match(r"^sphinx-build\s+([\d\.]+)(?:\+(?:/[\da-f]+)|b\d+)?\s*$", line)
            if match:
                return parse_version(match.group(1))

            match = re.match(r"^Sphinx.*\s+([\d\.]+)\s*$", line)
            if match:
                return parse_version(match.group(1))

    def check_sphinx(self):
        try:
            with open(self.conf, "r", encoding="utf-8") as f:
                for line in f:
                    match = re.match(r"^\s*needs_sphinx\s*=\s*[\'\"]([\d\.]+)[\'\"]", line)
                    if match:
                        self.min_version = parse_version(match.group(1))
                        break
        except IOError:
            sys.exit(f"Can't open {self.conf}")

        if not self.min_version:
            sys.exit(f"Can't get needs_sphinx version from {self.conf}")

        self.virtenv_dir = self.virtenv_prefix[0] + "latest"

        sphinx = self.get_sphinx_fname()
        if not sphinx:
            self.need_sphinx = 1
            return

        self.cur_version = self.get_sphinx_version(sphinx)
        if not self.cur_version:
            sys.exit(f"{sphinx} didn't return its version")

        if self.cur_version < self.min_version:
            curver = ver_str(self.cur_version)
            minver = ver_str(self.min_version)

            print(f"ERROR: Sphinx version is {curver}. It should be >= {minver}")
            self.need_sphinx = 1
            return

        # On version check mode, just assume Sphinx has all mandatory deps
        if self.version_check and self.cur_version >= RECOMMENDED_VERSION:
            sys.exit(0)

    def catcheck(self, filename):
        if os.path.exists(filename):
            with open(filename, "r", encoding="utf-8") as f:
                return f.read().strip()
        return ""

    def check_missing(self, progs):
        self.install += self.deps.check_missing(progs)
        if self.verbose_warn_install:
            self.deps.warn_install()

        if not self.deps.need and not self.deps.optional:
            return False

        return True


    #
    # Distro-specific hints methods
    #

    def give_debian_hints(self):
        progs = {
            "Pod::Usage":    "perl-modules",
            "convert":       "imagemagick",
            "dot":           "graphviz",
            "ensurepip":     "python3-venv",
            "python-sphinx": "python3-sphinx",
            "rsvg-convert":  "librsvg2-bin",
            "virtualenv":    "virtualenv",
            "xelatex":       "texlive-xetex",
            "yaml":          "python3-yaml",
        }

        if self.pdf:
            pdf_pkgs = {
                "texlive-lang-chinese": [
                    "/usr/share/texlive/texmf-dist/tex/latex/ctex/ctexhook.sty",
                ],
                "fonts-dejavu": [
                    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                ],
                "fonts-noto-cjk": [
                    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                    "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
                ],
            }

            for package, files in pdf_pkgs.items():
                self.check_missing_file(files, package, DepManager.PDF_MANDATORY)

            self.check_program("dvipng", DepManager.PDF_MANDATORY)

        if self.check_missing(progs):
            return

        if self.verbose_warn_install:
            print("You should run:")
        print(f"\n\tsudo apt-get install {self.install}")

    def give_redhat_hints(self):
        progs = {
            "Pod::Usage":       "perl-Pod-Usage",
            "convert":          "ImageMagick",
            "dot":              "graphviz",
            "python-sphinx":    "python3-sphinx",
            "rsvg-convert":     "librsvg2-tools",
            "virtualenv":       "python3-virtualenv",
            "xelatex":          "texlive-xetex-bin",
            "yaml":             "python3-pyyaml",
        }

        fedora_tex_pkgs = [
            "dejavu-sans-fonts",
            "dejavu-sans-mono-fonts",
            "dejavu-serif-fonts",
            "texlive-collection-fontsrecommended",
            "texlive-collection-latex",
            "texlive-xecjk",
        ]

        fedora = False
        rel = None

        match = re.search(r"(release|Linux)\s+(\d+)", self.system_release)
        if match:
            rel = int(match.group(2))

        if not rel:
            print("Couldn't identify release number")
            self.pdf = False
        elif re.search("Fedora", self.system_release):
            # Fedora 38 and upper use this CJK font

            noto_sans_redhat = "google-noto-sans-cjk-fonts"
            fedora = True
        else:
            # Almalinux, CentOS, RHEL, ...

            # at least up to version 9 (and Fedora < 38), that's the CJK font
            noto_sans_redhat = "google-noto-sans-cjk-ttc-fonts"

            progs["virtualenv"] = "python-virtualenv"

            if not rel or rel < 8:
                print("ERROR: Distro not supported. Too old?")
                return

            # RHEL 8 uses Python 3.6, which is not compatible with
            # the build system anymore. Suggest Python 3.11
            if rel == 8:
                self.deps.add_package("python39", DepManager.SYSTEM_MANDATORY)
                self.recommend_python = True

            if self.first_hint:
                print("Note: RHEL-based distros typically require extra repositories.\n" \
                      "For most, enabling epel and crb are enough:\n" \
                      "\tsudo dnf install -y epel-release", \
                      "\tsudo dnf config-manager --set-enabled crb\n" \
                      "Yet, some may have other required repositories. Those commands could be useful:\n" \
                      "\tsudo dnf repolist all\n" \
                      "\tsudo dnf repoquery --available --info <pkgs>\n",
                      "\tsudo dnf config-manager --set-enabled '*' # enable all - probably not what you want")

                self.first_hint = False


        if self.pdf:
            pdf_pkgs = [
                "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
                "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
            ]

            self.check_missing_file(pdf_pkgs, noto_sans_redhat, DepManager.PDF_MANDATORY)

            self.check_rpm_missing(fedora_tex_pkgs, DepManager.PDF_MANDATORY)

            self.check_missing_tex(DepManager.PDF_MANDATORY)

            # There's no texlive-ctex on RHEL 8 repositories. This will
            # likely affect CJK pdf build only.
            if not fedora and rel == 8:
                self.deps.del_package("texlive-ctex")

        if self.check_missing(progs):
            return

        if self.verbose_warn_install:
            print("You should run:")

        print(f"\n\tsudo dnf install -y {self.install}")

    def give_opensuse_hints(self):
        progs = {
            "Pod::Usage":    "perl-Pod-Usage",
            "convert":       "ImageMagick",
            "dot":           "graphviz",
            "python-sphinx": "python3-sphinx",
            "virtualenv":    "python3-virtualenv",
            "xelatex":       "texlive-xetex-bin",
            "yaml":          "python3-pyyaml",
        }

        suse_tex_pkgs = [
            "texlive-babel-english",
            "texlive-caption",
            "texlive-colortbl",
            "texlive-courier",
            "texlive-dvips",
            "texlive-helvetic",
            "texlive-makeindex",
            "texlive-metafont",
            "texlive-metapost",
            "texlive-palatino",
            "texlive-preview",
            "texlive-times",
            "texlive-zapfchan",
            "texlive-zapfding",
        ]

        progs["latexmk"] = "texlive-latexmk-bin"

        match = re.search(r"(Leap)\s+(\d+).(\d)", self.system_release)
        if match:
            rel = int(match.group(2))

            # Leap 15.x uses Python 3.6, which is not compatible with
            # the build system anymore. Suggest Python 3.11
            if rel == 15:
                if not self.which(self.python_cmd):
                    self.recommend_python = True
                    self.deps.add_package(self.python_cmd, DepManager.SYSTEM_MANDATORY)

                progs.update({
                    "python-sphinx": "python311-Sphinx",
                    "virtualenv":    "python311-virtualenv",
                    "yaml":          "python311-PyYAML",
                })
        else:
            # Tumbleweed defaults to Python 3.11

            progs.update({
                "python-sphinx": "python313-Sphinx",
                "virtualenv":    "python313-virtualenv",
                "yaml":          "python313-PyYAML",
            })

        # FIXME: add support for installing CJK fonts
        #
        # I tried hard, but was unable to find a way to install
        # "Noto Sans CJK SC" on openSUSE

        if self.pdf:
            self.check_rpm_missing(suse_tex_pkgs, DepManager.PDF_MANDATORY)
        if self.pdf:
            self.check_missing_tex()

        if self.check_missing(progs):
            return

        if self.verbose_warn_install:
            print("You should run:")
        print(f"\n\tsudo zypper install --no-recommends {self.install}")

    def give_mageia_hints(self):
        progs = {
            "Pod::Usage":    "perl-Pod-Usage",
            "convert":       "ImageMagick",
            "dot":           "graphviz",
            "python-sphinx": "python3-sphinx",
            "rsvg-convert":  "librsvg2",
            "virtualenv":    "python3-virtualenv",
            "xelatex":       "texlive",
            "yaml":          "python3-yaml",
        }

        tex_pkgs = [
            "texlive-fontsextra",
        ]

        if re.search(r"OpenMandriva", self.system_release):
            packager_cmd = "dnf install"
            noto_sans = "noto-sans-cjk-fonts"
            tex_pkgs = ["texlive-collection-fontsextra"]

            # Tested on OpenMandriva Lx 4.3
            progs["convert"] = "imagemagick"
            progs["yaml"] = "python-pyyaml"

        else:
            packager_cmd = "urpmi"
            noto_sans = "google-noto-sans-cjk-ttc-fonts"

        progs["latexmk"] = "texlive-collection-basic"

        if self.pdf:
            pdf_pkgs = [
                "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc",
                "/usr/share/fonts/TTF/NotoSans-Regular.ttf",
            ]

            self.check_missing_file(pdf_pkgs, noto_sans, DepManager.PDF_MANDATORY)
            self.check_rpm_missing(tex_pkgs, DepManager.PDF_MANDATORY)

        if self.check_missing(progs):
            return

        if self.verbose_warn_install:
            print("You should run:")
        print(f"\n\tsudo {packager_cmd} {self.install}")

    def give_arch_linux_hints(self):
        progs = {
            "convert":      "imagemagick",
            "dot":          "graphviz",
            "latexmk":      "texlive-core",
            "rsvg-convert": "extra/librsvg",
            "virtualenv":   "python-virtualenv",
            "xelatex":      "texlive-xetex",
            "yaml":         "python-yaml",
        }

        archlinux_tex_pkgs = [
            "texlive-core",
            "texlive-latexextra",
            "ttf-dejavu",
        ]

        if self.pdf:
            self.check_pacman_missing(archlinux_tex_pkgs, DepManager.PDF_MANDATORY)

            self.check_missing_file(
                ["/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc"],
                "noto-fonts-cjk",
                2,
            )

        if self.check_missing(progs):
            return

        if self.verbose_warn_install:
            print("You should run:")
        print(f"\n\tsudo pacman -S {self.install}")

    def give_gentoo_hints(self):
        progs = {
            "convert":       "media-gfx/imagemagick",
            "dot":           "media-gfx/graphviz",
            "rsvg-convert":  "gnome-base/librsvg",
            "virtualenv":    "dev-python/virtualenv",
            "xelatex":       "dev-texlive/texlive-xetex media-fonts/dejavu",
            "yaml":          "dev-python/pyyaml",
            "python-sphinx": "dev-python/sphinx",
        }

        if self.pdf:
            pdf_pkgs = {
                "media-fonts/dejavu": [
                    "/usr/share/fonts/dejavu/DejaVuSans.ttf",
                ],
                "media-fonts/noto-cjk": [
                    "/usr/share/fonts/noto-cjk/NotoSansCJKsc-Regular.otf",
                    "/usr/share/fonts/noto-cjk/NotoSerifCJK-Regular.ttc",
                ],
            }
            for package, files in pdf_pkgs.items():
                self.check_missing_file(files, package, DepManager.PDF_MANDATORY)

        if self.check_missing(progs):
            return

        if self.verbose_warn_install:
            print("You should run:")
        print("\n")

        # Handling dependencies is a nightmare, as Gentoo refuses to emerge
        # some packages if there's no package.use file describing them.
        # To make it worse, compilation flags shall also be present there
        # for some packages. If USE is not perfect, error/warning messages
        #   like those are shown:
        #
        #   !!! The following binary packages have been ignored due to non matching USE:
        #
        #    =media-gfx/graphviz-12.2.1-r1 X pdf -python_single_target_python3_13 qt6 svg
        #    =media-gfx/graphviz-12.2.1-r1 X pdf python_single_target_python3_12 -python_single_target_python3_13 qt6 svg
        #    =media-gfx/graphviz-12.2.1-r1 X pdf qt6 svg
        #    =media-gfx/graphviz-12.2.1-r1 X pdf -python_single_target_python3_10 qt6 svg
        #    =media-gfx/graphviz-12.2.1-r1 X pdf -python_single_target_python3_10 python_single_target_python3_12 -python_single_target_python3_13 qt6 svg
        #    =media-fonts/noto-cjk-20190416 X
        #    =app-text/texlive-core-2024-r1 X cjk -xetex
        #    =app-text/texlive-core-2024-r1 X -xetex
        #    =app-text/texlive-core-2024-r1 -xetex
        #    =dev-libs/zziplib-0.13.79-r1 sdl
        #
        # And will ignore such packages, installing the remaining ones. That
        # affects mostly the image extension and PDF generation.

        # Package dependencies and the minimal needed args:
        portages = {
            "graphviz": "media-gfx/graphviz",
            "imagemagick": "media-gfx/imagemagick",
            "media-libs": "media-libs/harfbuzz icu",
            "media-fonts": "media-fonts/noto-cjk",
            "texlive": "app-text/texlive-core xetex",
            "zziblib": "dev-libs/zziplib sdl",
        }

        if self.first_hint:
            use_base = "/etc/portage/package.use"
            files = glob(f"{use_base}/*")

            for fname, portage in portages.items():
                install = False

                while install == False:
                    if not files:
                        # No files under package.usage. Install all
                        install = True
                        break

                    args = portage.split(" ")

                    name = args.pop(0)

                    cmd = ["grep", "-l", "-E", rf"^{name}\b" ] + files
                    result = self.run(cmd, stdout=subprocess.PIPE, text=True)
                    if result.returncode or not result.stdout.strip():
                        # File containing portage name not found
                        install = True
                        break

                    # Ensure that needed USE flags are present
                    if args:
                        match_fname = result.stdout.strip()
                        with open(match_fname, 'r', encoding='utf8',
                                errors='backslashreplace') as fp:
                            for line in fp:
                                for arg in args:
                                    if arg.startswith("-"):
                                        continue

                                if not re.search(rf"\s*{arg}\b", line):
                                    # Needed file argument not found
                                    install = True
                                    break

                    # Everything looks ok, don't install
                    break

                # emit a code to setup missing USE
                if install:
                    print(f"\tsudo su -c 'echo \"{portage}\" > {use_base}/{fname}'")

            self.first_hint = False

        # Now, we can use emerge and let it respect USE
        print(f"\tsudo emerge --ask --changed-use --binpkg-respect-use=y {self.install}")

    #
    # Dispatch the check to an os_specific hinter
    #

    def check_distros(self):
        # OS-specific hints logic
        os_hints = {
            re.compile("Red Hat Enterprise Linux"):   self.give_redhat_hints,
            re.compile("Fedora"):                     self.give_redhat_hints,
            re.compile("AlmaLinux"):                  self.give_redhat_hints,
            re.compile("Amazon Linux"):               self.give_redhat_hints,
            re.compile("CentOS"):                     self.give_redhat_hints,
            re.compile("openEuler"):                  self.give_redhat_hints,
            re.compile("Oracle Linux Server"):        self.give_redhat_hints,
            re.compile("Rocky Linux"):                self.give_redhat_hints,
            re.compile("Springdale Open Enterprise"): self.give_redhat_hints,

            re.compile("Ubuntu"):                     self.give_debian_hints,
            re.compile("Debian"):                     self.give_debian_hints,
            re.compile("Devuan"):                     self.give_debian_hints,
            re.compile("Kali"):                       self.give_debian_hints,
            re.compile("Mint"):                       self.give_debian_hints,

            re.compile("openSUSE"):                   self.give_opensuse_hints,

            re.compile("Mageia"):                     self.give_mageia_hints,
            re.compile("OpenMandriva"):               self.give_mageia_hints,

            re.compile("Arch Linux"):                 self.give_arch_linux_hints,
            re.compile("Gentoo"):                     self.give_gentoo_hints,
        }

        # If the OS is detected, use per-OS hint logic
        for regex, os_hint in os_hints.items():
            if regex.search(self.system_release):
                os_hint()

                return

        #
        # Fall-back to generic hint code for other distros
        # That's far from ideal, specially for LaTeX dependencies.
        #
        progs = {"sphinx-build": "sphinx"}
        if self.pdf:
            self.check_missing_tex()

        self.check_missing(progs)

        print(f"I don't know distro {self.system_release}.")
        print("So, I can't provide you a hint with the install procedure.")
        print("There are likely missing dependencies.")

    #
    # Common dependencies
    #
    def deactivate_help(self):
        print("\n    If you want to exit the virtualenv, you can use:")
        print("\tdeactivate")

    def get_virtenv(self):
        cwd = os.getcwd()

        activates = []

        # Add all sphinx prefixes with possible version numbers
        for p in self.virtenv_prefix:
            activates += glob(f"{cwd}/{p}[0-9]*/bin/activate")

        activates.sort(reverse=True, key=str.lower)

        # Place sphinx_latest first, if it exists
        for p in self.virtenv_prefix:
            activates = glob(f"{cwd}/{p}*latest/bin/activate") + activates

        ver = (0, 0, 0)
        for f in activates:
            # Discard too old Sphinx virtual environments
            match = re.search(r"(\d+)\.(\d+)\.(\d+)", f)
            if match:
                ver = (int(match.group(1)), int(match.group(2)), int(match.group(3)))

                if ver < self.min_version:
                    continue

            sphinx_cmd = f.replace("activate", "sphinx-build")
            if not os.path.isfile(sphinx_cmd):
                continue

            ver = self.get_sphinx_version(sphinx_cmd)

            if not ver:
                venv_dir = f.replace("/bin/activate", "")
                print(f"Warning: virtual environment {venv_dir} is not working.\n" \
                      "Python version upgrade? Remove it with:\n\n" \
                      "\trm -rf {venv_dir}\n\n")
            else:
                if self.need_sphinx and ver >= self.min_version:
                    return (f, ver)
                elif parse_version(ver) > self.cur_version:
                    return (f, ver)

        return ("", ver)

    def recommend_sphinx_upgrade(self):
        # Avoid running sphinx-builds from venv if cur_version is good
        if self.cur_version and self.cur_version >= RECOMMENDED_VERSION:
            self.latest_avail_ver = self.cur_version
            return None

        # Get the highest version from sphinx_*/bin/sphinx-build and the
        # corresponding command to activate the venv/virtenv
        self.activate_cmd, self.venv_ver = self.get_virtenv()

        # Store the highest version from Sphinx existing virtualenvs
        if self.activate_cmd and self.venv_ver > self.cur_version:
            self.latest_avail_ver = self.venv_ver
        else:
            if self.cur_version:
                self.latest_avail_ver = self.cur_version
            else:
                self.latest_avail_ver = (0, 0, 0)

        # As we don't know package version of Sphinx, and there's no
        # virtual environments, don't check if upgrades are needed
        if not self.virtualenv:
            if not self.latest_avail_ver:
                return None

            return self.latest_avail_ver

        # Either there are already a virtual env or a new one should be created
        self.need_pip = 1

        if not self.latest_avail_ver:
            return None

        # Return if the reason is due to an upgrade or not
        if self.latest_avail_ver != (0, 0, 0):
            if self.latest_avail_ver < RECOMMENDED_VERSION:
                self.rec_sphinx_upgrade = 1

        return self.latest_avail_ver

    def recommend_package(self):

        print("\n2) As a package with:")

        old_need = self.deps.need
        old_optional = self.deps.optional

        self.pdf = False
        self.optional = 0
        self.install = ""
        old_verbose = self.verbose_warn_install
        self.verbose_warn_install = 0

        self.deps.clear_deps()

        self.deps.add_package("python-sphinx", DepManager.PYTHON_MANDATORY)

        self.check_distros()

        self.need = old_need
        self.optional = old_optional
        self.verbose_warn_install = old_verbose

    def recommend_sphinx_version(self, virtualenv_cmd):
        # The logic here is complex, as it have to deal with different versions:
        #	- minimal supported version;
        #	- minimal PDF version;
        #	- recommended version.
        # It also needs to work fine with both distro's package and venv/virtualenv

        if self.recommend_python:
            print("\nPython version is incompatible with doc build.\n" \
                  "Please upgrade it and re-run.\n")
            return


        # Version is OK. Nothing to do.
        if self.cur_version != (0, 0, 0) and self.cur_version >= RECOMMENDED_VERSION:
            return

        if self.latest_avail_ver:
            latest_avail_ver = ver_str(self.latest_avail_ver)

        if not self.need_sphinx:
            # sphinx-build is present and its version is >= $min_version

            # only recommend enabling a newer virtenv version if makes sense.
            if self.latest_avail_ver and self.latest_avail_ver > self.cur_version:
                print(f"\nYou may also use the newer Sphinx version {latest_avail_ver} with:")
                if f"{self.virtenv_prefix}" in os.getcwd():
                    print("\tdeactivate")
                print(f"\t. {self.activate_cmd}")
                self.deactivate_help()
                return

            if self.latest_avail_ver and self.latest_avail_ver >= RECOMMENDED_VERSION:
                return

        if not self.virtualenv:
            # No sphinx either via package or via virtenv. As we can't
            # Compare the versions here, just return, recommending the
            # user to install it from the package distro.
            if not self.latest_avail_ver or self.latest_avail_ver == (0, 0, 0):
                return

            # User doesn't want a virtenv recommendation, but he already
            # installed one via virtenv with a newer version.
            # So, print commands to enable it
            if self.latest_avail_ver > self.cur_version:
                print(f"\nYou may also use the Sphinx virtualenv version {latest_avail_ver} with:")
                if f"{self.virtenv_prefix}" in os.getcwd():
                    print("\tdeactivate")
                print(f"\t. {self.activate_cmd}")
                self.deactivate_help()
                return
            print("\n")
        else:
            if self.need_sphinx:
                self.deps.need += 1

        # Suggest newer versions if current ones are too old
        if self.latest_avail_ver and self.latest_avail_ver >= self.min_version:
            if self.latest_avail_ver >= RECOMMENDED_VERSION:
                print(f"\nNeed to activate Sphinx (version {latest_avail_ver}) on virtualenv with:")
                print(f"\t. {self.activate_cmd}")
                self.deactivate_help()
                return

            # Version is above the minimal required one, but may be
            # below the recommended one. So, print warnings/notes
            if self.latest_avail_ver < RECOMMENDED_VERSION:
                print(f"Warning: It is recommended at least Sphinx version {RECOMMENDED_VERSION}.")

        # At this point, either it needs Sphinx or upgrade is recommended,
        # both via pip

        if self.rec_sphinx_upgrade:
            if not self.virtualenv:
                print("Instead of install/upgrade Python Sphinx pkg, you could use pip/pypi with:\n\n")
            else:
                print("To upgrade Sphinx, use:\n\n")
        else:
            print("\nSphinx needs to be installed either:\n1) via pip/pypi with:\n")

        if not virtualenv_cmd:
            print("   Currently not possible.\n")
            print("   Please upgrade Python to a newer version and run this script again")
        else:
            print(f"\t{virtualenv_cmd} {self.virtenv_dir}")
            print(f"\t. {self.virtenv_dir}/bin/activate")
            print(f"\tpip install -r {self.requirement_file}")
            self.deactivate_help()

        if self.package_supported:
            self.recommend_package()

        print("\n" \
              "   Please note that Sphinx currentlys produce false-positive\n" \
              "   warnings when the same name is used for more than one type (functions,\n" \
              "   structs, enums,...). This is known Sphinx bug. For more details, see:\n" \
              "\thttps://github.com/sphinx-doc/sphinx/pull/8313")

    def check_needs(self):
        self.get_system_release()
        self.python_cmd = sys.executable

        # Check if Sphinx is already accessible from current environment
        self.check_sphinx()

        if self.system_release:
            print(f"Detected OS: {self.system_release}.")
        else:
            print("Unknown OS")
        if self.cur_version != (0, 0, 0):
            ver = ver_str(self.cur_version)
            print(f"Sphinx version: {ver}\n")

        # Check the type of virtual env, depending on Python version
        virtualenv_cmd = None

        if sys.version_info < MIN_PYTHON_VERSION:
            min_ver = ver_str(MIN_PYTHON_VERSION)
            print(f"ERROR: at least python {min_ver} is required to build the kernel docs")
            self.need_sphinx = 1

        self.venv_ver = self.recommend_sphinx_upgrade()

        if self.need_pip:
            if sys.version_info < MIN_PYTHON_VERSION:
                self.need_pip = False
                print("Warning: python version is not supported.")

            else:
                virtualenv_cmd = f"{self.python_cmd} -m venv"
                self.check_python_module("ensurepip")

        # Check for needed programs/tools
        self.check_perl_module("Pod::Usage", DepManager.SYSTEM_MANDATORY)

        self.check_program("make", DepManager.SYSTEM_MANDATORY)
        self.check_program("gcc", DepManager.SYSTEM_MANDATORY)

        self.check_program("dot", DepManager.SYSTEM_OPTIONAL)
        self.check_program("convert", DepManager.SYSTEM_OPTIONAL)

        self.check_python_module("yaml")

        if self.pdf:
            self.check_program("xelatex", DepManager.PDF_MANDATORY)
            self.check_program("rsvg-convert", DepManager.PDF_MANDATORY)
            self.check_program("latexmk", DepManager.PDF_MANDATORY)

        # Do distro-specific checks and output distro-install commands
        self.check_distros()

        if not self.python_cmd:
            if self.need == 1:
                sys.exit("Can't build as 1 mandatory dependency is missing")
            elif self.need:
                sys.exit(f"Can't build as {self.need} mandatory dependencies are missing")

        # Check if sphinx-build is called sphinx-build-3
        if self.need_symlink:
            sphinx_path = self.which("sphinx-build-3")
            if sphinx_path:
                print(f"\tsudo ln -sf {sphinx_path} /usr/bin/sphinx-build\n")

        self.recommend_sphinx_version(virtualenv_cmd)
        print("")

        if not self.optional:
            print("All optional dependencies are met.")

        if self.need == 1:
            sys.exit("Can't build as 1 mandatory dependency is missing")
        elif self.need:
            sys.exit(f"Can't build as {self.need} mandatory dependencies are missing")

        print("Needed package dependencies are met.")

    def get_system_release(self):
        """
        Determine the system type. There's no unique way that would work
        with all distros with a minimal package install. So, several
        methods are used here.

        By default, it will use lsb_release function. If not available, it will
        fail back to reading the known different places where the distro name
        is stored.

        Several modern distros now have /etc/os-release, which usually have
        a decent coverage.
        """

        if self.which("lsb_release"):
            result = self.run(["lsb_release", "-d"], capture_output=True, text=True)
            self.system_release = result.stdout.replace("Description:", "").strip()

        release_files = [
            "/etc/system-release",
            "/etc/redhat-release",
            "/etc/lsb-release",
            "/etc/gentoo-release",
        ]

        if not self.system_release:
            for f in release_files:
                self.system_release = self.catcheck(f)
                if self.system_release:
                    break

        # This seems more common than LSB these days
        if not self.system_release:
            os_var = {}
            try:
                with open("/etc/os-release", "r", encoding="utf-8") as f:
                    for line in f:
                        match = re.match(r"^([\w\d\_]+)=\"?([^\"]*)\"?\n", line)
                        if match:
                            os_var[match.group(1)] = match.group(2)

                self.system_release = os_var.get("NAME", "")
                if "VERSION_ID" in os_var:
                    self.system_release += " " + os_var["VERSION_ID"]
                elif "VERSION" in os_var:
                    self.system_release += " " + os_var["VERSION"]
            except IOError:
                pass

        if not self.system_release:
            self.system_release = self.catcheck("/etc/issue")

        self.system_release = self.system_release.strip()

DESCRIPTION = """
Process some flags related to Sphinx installation and documentation build.
"""


def main():
    parser = argparse.ArgumentParser(description=DESCRIPTION)

    parser.add_argument(
        "--no-virtualenv",
        action="store_false",
        dest="virtualenv",
        help="Recommend installing Sphinx instead of using a virtualenv",
    )

    parser.add_argument(
        "--no-pdf",
        action="store_false",
        dest="pdf",
        help="Don't check for dependencies required to build PDF docs",
    )

    parser.add_argument(
        "--version-check",
        action="store_true",
        dest="version_check",
        help="If version is compatible, don't check for missing dependencies",
    )

    args = parser.parse_args()

    checker = SphinxDependencyChecker(args)

    checker.check_python()
    checker.check_needs()


if __name__ == "__main__":
    main()
