==================================================
Capturing configuration information in the headers
==================================================

.. contents::
   :local:

The Problem
===========

libc++ supports building the library with a number of different configuration options.
In order to support persistent configurations and reduce arbitrary preprocessor logic
in the headers, libc++ has a mechanism to capture configuration options in the
installed headers so they can be used in the rest of the code.


Design Goals
============

* The solution should be simple, consistent and robust to avoid subtle bugs.

* Developers should test the code the same way it will be deployed -- in other words,
  the headers used to run tests should be the same that we install in order
  to avoid bugs creeping up.

* It should allow different targets or flavors of the library to use a different
  configuration without having to duplicate all the libc++ headers.


The Solution
============

When you first configure libc++ using CMake, a ``__config_site`` file is generated
to capture the various configuration options you selected. The ``__config`` header
used by all other headers includes this ``__config_site`` header first in order to
get the correct configuration.

The ``__config_site`` header is hence the only place where persistent configuration
is stored in the library. That header essentially reflects how the vendor configured
the library. As we evolve the library, we can lift configuration options into that
header in order to reduce arbitrary hardcoded choices elsewhere in the code. For
example, instead of assuming that a specific platform doesn't provide some functionality,
we can create a generic macro to guard it and vendors can define the macro when
configuring the library on that platform. This makes the "carve off" reusable in
other circumstances instead of tying it tightly to a single platform.

Furthermore, the Clang driver now looks for headers in a target-specific directory
for libc++. By installing the ``__config_site`` header (and only that header) to
this target-specific directory, it is possible to share the libc++ headers for
multiple targets, and only duplicate the persistent information located in the
``__config_site`` header. For example:

.. code-block:: bash

  include/c++/v1/
    vector
    map
    etc...

  include/<targetA>/c++/v1/
    __config_site

  include/<targetB>/c++/v1/
    __config_site

When compiling for ``targetA``, Clang will use the ``__config_site`` inside
``include/<targetA>/c++/v1/``, and the corresponding ``__config_site`` for
``targetB``.
