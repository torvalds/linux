#!/usr/bin/python2

from distutils.core import setup, Extension
from os import getenv

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


cflags = ['-fno-strict-aliasing', '-Wno-write-strings']
cflags += getenv('CFLAGS', '').split()

build_lib = getenv('PYTHON_EXTBUILD_LIB')
build_tmp = getenv('PYTHON_EXTBUILD_TMP')
libtraceevent = getenv('LIBTRACEEVENT')
liblk = getenv('LIBLK')

ext_sources = [f.strip() for f in file('util/python-ext-sources')
				if len(f.strip()) > 0 and f[0] != '#']

perf = Extension('perf',
		  sources = ext_sources,
		  include_dirs = ['util/include'],
		  extra_compile_args = cflags,
		  extra_objects = [libtraceevent, liblk],
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
