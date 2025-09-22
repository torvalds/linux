=================
Time Zone Support
=================

Introduction
============

Starting with C++20 the ``<chrono>`` library has support for time zones.
These are available in the
`IANA Time Zone Database <https://data.iana.org/time-zones/tz-link.html>`_.
This page describes the design decisions and trade-offs made to implement this
feature. This page contains several links with more information regarding the
contents of the IANA database, this page assumes the reader is familiar with
this information.

Which version of the Time Zone Database to use
==============================================

The data of the database is available on several platforms in different forms:

- Typically Unix systems ship the database as
  `TZif files <https://www.rfc-editor.org/rfc/rfc8536.html>`_. This format has
  3 versions and the ``time_zone_link`` information is not always available.
  If available, they are symlinks in the file system.
  These files don't provide the database version information. This information
  is needed for the functions ``std::chrono:: remote_version()`` and
  ``std::chrono::reload_tzdb()``.

- On several Unix systems the time zone source files are available. These files
  are stored in several regions, mainly the continents. This file contains a
  large amount of comment with historical information regarding time zones.
  The format is documented in the
  `IANA documentation <https://data.iana.org/time-zones/tz-how-to.html>`_
  and in the `man page <https://man7.org/linux/man-pages/man8/zic.8.html>`_ of zic.
  The disadvantage of this version is that at least Linux versions don't have
  the database version information. This information is needed for the functions
  ``std::chrono:: remote_version()`` and ``std::chrono::reload_tzdb()``.

- On Linux systems ``tzdata.zi`` is available. This contains the same
  information as the source files but in one file without the comments. This
  file uses the same format as the sources, but shortens the names. For example
  ``Rule`` is abbreviated to ``R``. This file contains the database version
  information.

The disadvantage of the ``TZif`` format (which is a binary format) is that it's
not possible to get the proper ``time_zone_link`` information on all platforms.
The time zone database version number is also missing from ``TZif`` files.
Since the time zone database is supposed to contain both these informations,
``TZif`` files can't be used to create a conforming implementation.

Since it's easier to parse one file than a set of files we decided
to use the ``tzdata.zi``. The other benefit is that the ``tzdata.zi`` file
contains the database version information needed for a conforming
implementation.

The ``tzdata.zi`` file is not available on all platforms as of August 2023, so
some vendors will need to make changes to their platform. Most vendors already
ship the database, so they only need to adjust the packaging of their time zone
package to include the files we require. One notable exception is Windows,
where no IANA time zone database is provided at all. However it's possible for
Windows packagers to add these files to their libc++ packages. The IANA
databases can be
`downloaded <https://data.iana.org/time-zones/releases/>`_.

An alternative would be to ship the database with libc++, either as a file or
compiled in the dylib. The text file is about 112 KB. For now libc++ will not
ship this file. If it's hard to get vendors to ship these files we can
reconsider based on that information.

Leap seconds
------------

For the leap seconds libc++ will use the source file ``leap-seconds.list``.
This file is easier to parse than the ``leapseconds`` file. Both files are
present on Linux, but not always on other platforms. Since these platforms need
to change their packaging for ``tzdata.zi``, adding two instead of one files
seems a small change.


Updating the Time Zone Database
===============================

Per `[time.zone.db.remote]/1 <http://eel.is/c++draft/time.zone#db.remote-1>`_

.. code-block:: text

  The local time zone database is that supplied by the implementation when the
  program first accesses the database, for example via current_zone(). While the
  program is running, the implementation may choose to update the time zone
  database. This update shall not impact the program in any way unless the
  program calls the functions in this subclause. This potentially updated time
  zone database is referred to as the remote time zone database.

There is an update mechanism in libc++, however this is not done automatically.
Invoking the function ``std::chrono::remote_version()`` will parse the version
information of the ``tzdata.zi`` file and return that information. Similarly,
``std::chrono::reload_tzdb()`` will parse the ``tzdata.zi`` and
``leap-seconds.list`` again. This makes it possible to update the database if
needed by the application and gives the user full power over the update policy.

This approach has several advantages:

- It is simple to implement.
- The library does not need to start a periodic background process to poll
  changes to the filesystem. When using a background process, it may become
  active when the application is busy with its core task, taking away resources
  from that task.
- If there is no threading available this polling
  becomes more involved. For example, query the file every *x* calls to
  ``std::chrono::get_tzdb()``. This mean calls to ``std::chrono::get_tzdb()``
  would have different performance characteristics.

The small drawback is:

- On platforms with threading enabled updating the database may take longer.
  On these platforms the remote database could have been loaded in a background
  process.

Another issue with the automatic update is that it may not be considered
Standard compliant, since the Standard uses the wording "This update shall not
impact the program in any way". Using resources could be considered as
impacting the program.
