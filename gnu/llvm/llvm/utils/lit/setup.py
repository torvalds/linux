import os
import sys

from setuptools import setup, find_packages

# setuptools expects to be invoked from within the directory of setup.py, but it
# is nice to allow:
#   python path/to/setup.py install
# to work (for scripts, etc.)
os.chdir(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ".")

import lit

with open("README.rst", "r", encoding="utf-8") as f:
    long_description = f.read()

setup(
    name="lit",
    version=lit.__version__,
    author=lit.__author__,
    author_email=lit.__email__,
    url="http://llvm.org",
    license="Apache-2.0 with LLVM exception",
    license_files=["LICENSE.TXT"],
    description="A Software Testing Tool",
    keywords="test C++ automatic discovery",
    long_description=long_description,
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Environment :: Console",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Apache Software License",
        "Natural Language :: English",
        "Operating System :: OS Independent",
        "Programming Language :: Python",
        "Topic :: Software Development :: Testing",
    ],
    zip_safe=False,
    packages=find_packages(),
    entry_points={
        "console_scripts": [
            "lit = lit.main:main",
        ],
    },
)
