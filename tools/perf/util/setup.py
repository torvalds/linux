from os import getenv, path
from subprocess import Popen, PIPE
from re import sub
import shlex

cc = getenv("CC")
assert cc, "Environment variable CC not set"

# Check if CC has options, as is the case in yocto, where it uses CC="cc --sysroot..."
cc_tokens = cc.split()
if len(cc_tokens) > 1:
    cc = cc_tokens[0]
    cc_options = " ".join([str(e) for e in cc_tokens[1:]]) + " "
else:
    cc_options = ""

# ignore optional stderr could be None as it is set to PIPE to avoid that.
# mypy: disable-error-code="union-attr"
cc_is_clang = b"clang version" in Popen([cc, "-v"], stderr=PIPE).stderr.readline()

srctree = getenv('srctree')
assert srctree, "Environment variable srctree, for the Linux sources, not set"
src_feature_tests  = f'{srctree}/tools/build/feature'

def clang_has_option(option):
    cmd = shlex.split(f"{cc} {cc_options} {option}")
    cmd.append(path.join(src_feature_tests, "test-hello.c"))
    cc_output = Popen(cmd, stderr=PIPE).stderr.readlines()
    return [o for o in cc_output if ((b"unknown argument" in o) or (b"is not supported" in o) or (b"unknown warning option" in o))] == [ ]

if cc_is_clang:
    from sysconfig import get_config_vars
    vars = get_config_vars()
    for var in ('CFLAGS', 'OPT'):
        vars[var] = sub("-specs=[^ ]+", "", vars[var])
        if not clang_has_option("-mcet"):
            vars[var] = sub("-mcet", "", vars[var])
        if not clang_has_option("-fcf-protection"):
            vars[var] = sub("-fcf-protection", "", vars[var])
        if not clang_has_option("-fstack-clash-protection"):
            vars[var] = sub("-fstack-clash-protection", "", vars[var])
        if not clang_has_option("-fstack-protector-strong"):
            vars[var] = sub("-fstack-protector-strong", "", vars[var])
        if not clang_has_option("-fno-semantic-interposition"):
            vars[var] = sub("-fno-semantic-interposition", "", vars[var])
        if not clang_has_option("-ffat-lto-objects"):
            vars[var] = sub("-ffat-lto-objects", "", vars[var])
        if not clang_has_option("-ftree-loop-distribute-patterns"):
            vars[var] = sub("-ftree-loop-distribute-patterns", "", vars[var])
        if not clang_has_option("-gno-variable-location-views"):
            vars[var] = sub("-gno-variable-location-views", "", vars[var])

from setuptools import setup, Extension

from setuptools.command.build_ext   import build_ext   as _build_ext
from setuptools.command.install_lib import install_lib as _install_lib

class build_ext(_build_ext):
    def finalize_options(self):
        _build_ext.finalize_options(self)
        self.build_lib  = build_lib
        self.build_temp = build_tmp

class install_lib(_install_lib):
    def finalize_options(self):
        _install_lib.finalize_options(self)
        self.build_dir = build_lib


cflags = getenv('CFLAGS', '').split()
# switch off several checks (need to be at the end of cflags list)
cflags += ['-fno-strict-aliasing', '-Wno-write-strings', '-Wno-unused-parameter', '-Wno-redundant-decls' ]
if cc_is_clang:
    cflags += ["-Wno-unused-command-line-argument" ]
    if clang_has_option("-Wno-cast-function-type-mismatch"):
        cflags += ["-Wno-cast-function-type-mismatch" ]
else:
    cflags += ['-Wno-cast-function-type' ]

# The python headers have mixed code with declarations (decls after asserts, for instance)
cflags += [ "-Wno-declaration-after-statement" ]

src_perf  = f'{srctree}/tools/perf'
build_lib = getenv('PYTHON_EXTBUILD_LIB')
build_tmp = getenv('PYTHON_EXTBUILD_TMP')

perf = Extension('perf',
                 sources = [ src_perf + '/util/python.c' ],
		         include_dirs = ['util/include'],
		         extra_compile_args = cflags,
                 )

setup(name='perf',
      version='0.1',
      description='Interface with the Linux profiling infrastructure',
      author='Arnaldo Carvalho de Melo',
      author_email='acme@redhat.com',
      license='GPLv2',
      url='http://perf.wiki.kernel.org',
      ext_modules=[perf],
      cmdclass={'build_ext': build_ext, 'install_lib': install_lib})
