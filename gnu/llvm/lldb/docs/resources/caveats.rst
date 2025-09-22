Caveats
=======

.. _python_caveat:

Python
------

LLDB has a powerful scripting interface which is accessible through Python.
Python is available either from within LLDB through a (interactive) script
interpreter, or as a Python module which you can import from the Python
interpreter.

To make this possible, LLDB links against the Python shared library. Linking
against Python comes with some constraints to be aware of.

1.  It is not possible to build and link LLDB against a Python 3 library and
    use it from Python 2 and vice versa.

2.  It is not possible to build and link LLDB against one distribution on
    Python and use it through an interpreter coming from another distribution.
    For example, on macOS, if you build and link against Python from
    python.org, you cannot import the lldb module from the Python interpreter
    installed with Homebrew.

3.  To use third party Python packages from inside LLDB, you need to install
    them using a utility (such as ``pip``) from the same Python distribution as
    the one used to build and link LLDB.

The previous considerations are especially important during development, but
apply to binary distributions of LLDB as well.

LLDB in Xcode on macOS
``````````````````````

Users of lldb in Xcode on macOS commonly run into these issues when they
install Python, often unknowingly as a dependency pulled in by Homebrew or
other package managers. The problem is the symlinks that get created in
``/usr/local/bin``, which comes before ``/usr/bin`` in your path. You can use
``which python3`` to check to what it resolves.

To be sure you use the Python that matches with the lldb in Xcode use ``xcrun``
or use the absolute path to the shims in ``/usr/bin``.

::

   $ xcrun python3
   $ /usr/bin/python3

Similarly, to install packages and be able to use them from within lldb, you'll
need to install them with the matching ``pip3``.

::

   $ xcrun pip3
   $ /usr/bin/pip3

The same is true for Python 2. Although Python 2 comes with the operating
system rather than Xcode, you can still use ``xcrun`` to launch the system
variant.

::

   $ xcrun python
   $ /usr/bin/python

Keep in mind that Python 2 is deprecated and no longer maintained. Future
versions of macOS will not include Python 2.7.
