#!/usr/bin/python

from os import getenv
from subprocess import Popen, PIPE
from re import sub

def clang_has_option(option):
    return [o for o in Popen(['clang', option], stderr=PIPE).stderr.readlines() if b"unknown argument" in o] == [ ]

cc = getenv("CC")
if cc == "clang":
    from distutils.sysconfig import get_config_vars
    vars = get_config_vars()
    for var in ('CFLAGS', 'OPT'):
        vars[var] = sub("-specs=[^ ]+", "", vars[var])
        if not clang_has_option("-mcet"):
            vars[var] = sub("-mcet", "", vars[var])
        if not clang_has_option("-fcf-protection"):
            vars[var] = sub("-fcf-protection", "", vars[var])
        if not clang_has_option("-fstack-clash-protection"):
            vars[var] = sub("-fstack-clash-protection", "", vars[var])

from distutils.core import setup, Extension

from distutils.command.build_ext   import build_ext   as _build_ext
from distutils.command.install_lib import install_lib as _install_lib

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
if cc != "clang":
    cflags += ['-Wno-cast-function-type' ]

src_perf  = getenv('srctree') + '/tools/perf'
build_lib = getenv('PYTHON_EXTBUILD_LIB')
build_tmp = getenv('PYTHON_EXTBUILD_TMP')
libtraceevent = getenv('LIBTRACEEVENT')
libapikfs = getenv('LIBAPI')

ext_sources = [f.strip() for f in open('util/python-ext-sources')
				if len(f.strip()) > 0 and f[0] != '#']

# use full paths with source files
ext_sources = list(map(lambda x: '%s/%s' % (src_perf, x) , ext_sources))

perf = Extension('perf',
		  sources = ext_sources,
		  include_dirs = ['util/include'],
		  extra_compile_args = cflags,
		  extra_objects = [libtraceevent, libapikfs],
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
