==============
File Time Type
==============

.. contents::
   :local:

.. _file-time-type-motivation:

Motivation
==========

The filesystem library provides interfaces for getting and setting the last
write time of a file or directory. The interfaces use the ``file_time_type``
type, which is a specialization of ``chrono::time_point`` for the
"filesystem clock". According to [fs.filesystem.syn]

  trivial-clock is an implementation-defined type that satisfies the
  Cpp17TrivialClock requirements ([time.clock.req]) and that is capable of
  representing and measuring file time values. Implementations should ensure
  that the resolution and range of file_time_type reflect the operating
  system dependent resolution and range of file time values.


On POSIX systems, file times are represented using the ``timespec`` struct,
which is defined as follows:

.. code-block:: cpp

  struct timespec {
    time_t tv_sec;
    long   tv_nsec;
  };

To represent the range and resolution of ``timespec``, we need to (A) have
nanosecond resolution, and (B) use more than 64 bits (assuming a 64 bit ``time_t``).

As the standard requires us to use the ``chrono`` interface, we have to define
our own filesystem clock which specifies the period and representation of
the time points and duration it provides. It will look like this:

.. code-block:: cpp

  struct _FilesystemClock {
    using period = nano;
    using rep = TBD; // What is this?

    using duration = chrono::duration<rep, period>;
    using time_point = chrono::time_point<_FilesystemClock>;

    // ... //
  };

  using file_time_type = _FilesystemClock::time_point;


To get nanosecond resolution, we simply define ``period`` to be ``std::nano``.
But what type can we use as the arithmetic representation that is capable
of representing the range of the ``timespec`` struct?

Problems To Consider
====================

Before considering solutions, let's consider the problems they should solve,
and how important solving those problems are:


Having a Smaller Range than ``timespec``
----------------------------------------

One solution to the range problem is to simply reduce the resolution of
``file_time_type`` to be less than that of nanoseconds. This is what libc++'s
initial implementation of ``file_time_type`` did; it's also what
``std::system_clock`` does. As a result, it can represent time points about
292 thousand years on either side of the epoch, as opposed to only 292 years
at nanosecond resolution.

``timespec`` can represent time points +/- 292 billion years from the epoch
(just in case you needed a time point 200 billion years before the big bang,
and with nanosecond resolution).

To get the same range, we would need to drop our resolution to that of seconds
to come close to having the same range.

This begs the question, is the range problem "really a problem"? Sane usages
of file time stamps shouldn't exceed +/- 300 years, so should we care to support it?

I believe the answer is yes. We're not designing the filesystem time API, we're
providing glorified C++ wrappers for it. If the underlying API supports
a value, then we should too. Our wrappers should not place artificial restrictions
on users that are not present in the underlying filesystem.

Having a smaller range that the underlying filesystem forces the
implementation to report ``value_too_large`` errors when it encounters a time
point that it can't represent. This can cause the call to ``last_write_time``
to throw in cases where the user was confident the call should succeed. (See below)


.. code-block:: cpp

  #include <filesystem>
  using namespace std::filesystem;

  // Set the times using the system interface.
  void set_file_times(const char* path, struct timespec ts) {
    timespec both_times[2];
    both_times[0] = ts;
    both_times[1] = ts;
    int result = ::utimensat(AT_FDCWD, path, both_times, 0);
    assert(result != -1);
  }

  // Called elsewhere to set the file time to something insane, and way
  // out of the 300 year range we might expect.
  void some_bad_persons_code() {
    struct timespec new_times;
    new_times.tv_sec = numeric_limits<time_t>::max();
    new_times.tv_nsec = 0;
    set_file_times("/tmp/foo", new_times); // OK, supported by most FSes
  }

  int main(int, char**) {
    path p = "/tmp/foo";
    file_status st = status(p);
    if (!exists(st) || !is_regular_file(st))
      return 1;
    if ((st.permissions() & perms::others_read) == perms::none)
      return 1;
    // It seems reasonable to assume this call should succeed.
    file_time_type tp = last_write_time(p); // BAD! Throws value_too_large.
    return 0;
  }


Having a Smaller Resolution than ``timespec``
---------------------------------------------

As mentioned in the previous section, one way to solve the range problem
is by reducing the resolution. But matching the range of ``timespec`` using a
64 bit representation requires limiting the resolution to seconds.

So we might ask: Do users "need" nanosecond precision? Is seconds not good enough?
I limit my consideration of the point to this: Why was it not good enough for
the underlying system interfaces? If it wasn't good enough for them, then it
isn't good enough for us. Our job is to match the filesystems range and
representation, not design it.


Having a Larger Range than ``timespec``
----------------------------------------

We should also consider the opposite problem of having a ``file_time_type``
that is able to represent a larger range than ``timespec``. At least in
this case ``last_write_time`` can be used to get and set all possible values
supported by the underlying filesystem; meaning ``last_write_time(p)`` will
never throw an overflow error when retrieving a value.

However, this introduces a new problem, where users are allowed to attempt to
create a time point beyond what the filesystem can represent. Two particular
values which cause this are ``file_time_type::min()`` and
``file_time_type::max()``. As a result, the following code would throw:

.. code-block:: cpp

  void test() {
    last_write_time("/tmp/foo", file_time_type::max()); // Throws
    last_write_time("/tmp/foo", file_time_type::min()); // Throws.
  }

Apart from cases explicitly using ``min`` and ``max``, I don't see users taking
a valid time point, adding a couple hundred billions of years in error,
and then trying to update a file's write time to that value very often.

Compared to having a smaller range, this problem seems preferable. At least
now we can represent any time point the filesystem can, so users won't be forced
to revert back to system interfaces to avoid limitations in the C++ STL.

I posit that we should only consider this concern *after* we have something
with at least the same range and resolution of the underlying filesystem. The
latter two problems are much more important to solve.

Potential Solutions And Their Complications
===========================================

Source Code Portability Across Implementations
-----------------------------------------------

As we've discussed, ``file_time_type`` needs a representation that uses more
than 64 bits. The possible solutions include using ``__int128_t``, emulating a
128 bit integer using a class, or potentially defining a ``timespec`` like
arithmetic type. All three will allow us to, at minimum, match the range
and resolution, and the last one might even allow us to match them exactly.

But when considering these potential solutions we need to consider more than
just the values they can represent. We need to consider the effects they will
have on users and their code. For example, each of them breaks the following
code in some way:

.. code-block:: cpp

  // Bug caused by an unexpected 'rep' type returned by count.
  void print_time(path p) {
    // __int128_t doesn't have streaming operators, and neither would our
    // custom arithmetic types.
    cout << last_write_time(p).time_since_epoch().count() << endl;
  }

  // Overflow during creation bug.
  file_time_type timespec_to_file_time_type(struct timespec ts) {
    // woops! chrono::seconds and chrono::nanoseconds use a 64 bit representation
    // this may overflow before it's converted to a file_time_type.
    auto dur = seconds(ts.tv_sec) + nanoseconds(ts.tv_nsec);
    return file_time_type(dur);
  }

  file_time_type correct_timespec_to_file_time_type(struct timespec ts) {
    // This is the correct version of the above example, where we
    // avoid using the chrono typedefs as they're not sufficient.
    // Can we expect users to avoid this bug?
    using fs_seconds = chrono::duration<file_time_type::rep>;
    using fs_nanoseconds = chrono::duration<file_time_type::rep, nano>;
    auto dur = fs_seconds(ts.tv_sec) + fs_nanoseconds(tv.tv_nsec);
    return file_time_type(dur);
  }

  // Implicit truncation during conversion bug.
  intmax_t get_time_in_seconds(path p) {
    using fs_seconds = duration<file_time_type::rep, ratio<1, 1> >;
    auto tp = last_write_time(p);

    // This works with truncation for __int128_t, but what does it do for
    // our custom arithmetic types.
    return duration_cast<fs_seconds>().count();
  }


Each of the above examples would require a user to adjust their filesystem code
to the particular eccentricities of the representation, hopefully only in such
a way that the code is still portable across implementations.

At least some of the above issues are unavoidable, no matter what
representation we choose. But some representations may be quirkier than others,
and, as I'll argue later, using an actual arithmetic type (``__int128_t``)
provides the least aberrant behavior.


Chrono and ``timespec`` Emulation.
----------------------------------

One of the options we've considered is using something akin to ``timespec``
to represent the ``file_time_type``. It only seems natural seeing as that's
what the underlying system uses, and because it might allow us to match
the range and resolution exactly. But would it work with chrono? And could
it still act at all like a ``timespec`` struct?

For ease of consideration, let's consider what the implementation might
look like.

.. code-block:: cpp

  struct fs_timespec_rep {
    fs_timespec_rep(long long v)
      : tv_sec(v / nano::den), tv_nsec(v % nano::den)
    { }
  private:
    time_t tv_sec;
    long tv_nsec;
  };
  bool operator==(fs_timespec_rep, fs_timespec_rep);
  fs_int128_rep operator+(fs_timespec_rep, fs_timespec_rep);
  // ... arithmetic operators ... //

The first thing to notice is that we can't construct ``fs_timespec_rep`` like
a ``timespec`` by passing ``{secs, nsecs}``. Instead we're limited to
constructing it from a single 64 bit integer.

We also can't allow the user to inspect the ``tv_sec`` or ``tv_nsec`` values
directly. A ``chrono::duration`` represents its value as a tick period and a
number of ticks stored using ``rep``. The representation is unaware of the
tick period it is being used to represent, but ``timespec`` is setup to assume
a nanosecond tick period; which is the only case where the names ``tv_sec``
and ``tv_nsec`` match the values they store.

When we convert a nanosecond duration to seconds, ``fs_timespec_rep`` will
use ``tv_sec`` to represent the number of giga seconds, and ``tv_nsec`` the
remaining seconds. Let's consider how this might cause a bug were users allowed
to manipulate the fields directly.

.. code-block:: cpp

  template <class Period>
  timespec convert_to_timespec(duration<fs_time_rep, Period> dur) {
    fs_timespec_rep rep = dur.count();
    return {rep.tv_sec, rep.tv_nsec}; // Oops! Period may not be nanoseconds.
  }

  template <class Duration>
  Duration convert_to_duration(timespec ts) {
    Duration dur({ts.tv_sec, ts.tv_nsec}); // Oops! Period may not be nanoseconds.
    return file_time_type(dur);
    file_time_type tp = last_write_time(p);
    auto dur =
  }

  time_t extract_seconds(file_time_type tp) {
    // Converting to seconds is a silly bug, but I could see it happening.
    using SecsT = chrono::duration<file_time_type::rep, ratio<1, 1>>;
    auto secs = duration_cast<Secs>(tp.time_since_epoch());
    // tv_sec is now representing gigaseconds.
    return secs.count().tv_sec; // Oops!
  }

Despite ``fs_timespec_rep`` not being usable in any manner resembling
``timespec``, it still might buy us our goal of matching its range exactly,
right?

Sort of. Chrono provides a specialization point which specifies the minimum
and maximum values for a custom representation. It looks like this:

.. code-block:: cpp

  template <>
  struct duration_values<fs_timespec_rep> {
    static fs_timespec_rep zero();
    static fs_timespec_rep min();
    static fs_timespec_rep max() { // assume friendship.
      fs_timespec_rep val;
      val.tv_sec = numeric_limits<time_t>::max();
      val.tv_nsec = nano::den - 1;
      return val;
    }
  };

Notice that ``duration_values`` doesn't tell the representation what tick
period it's actually representing. This would indeed correctly limit the range
of ``duration<fs_timespec_rep, nano>`` to exactly that of ``timespec``. But
nanoseconds isn't the only tick period it will be used to represent. For
example:

.. code-block:: cpp

  void test() {
    using rep = file_time_type::rep;
    using fs_nsec = duration<rep, nano>;
    using fs_sec = duration<rep>;
    fs_nsec nsecs(fs_seconds::max()); // Truncates
  }

Though the above example may appear silly, I think it follows from the incorrect
notion that using a ``timespec`` rep in chrono actually makes it act as if it
were an actual ``timespec``.

Interactions with 32 bit ``time_t``
-----------------------------------

Up until now we've only be considering cases where ``time_t`` is 64 bits, but what
about 32 bit systems/builds where ``time_t`` is 32 bits? (this is the common case
for 32 bit builds).

When ``time_t`` is 32 bits, we can implement ``file_time_type`` simply using 64-bit
``long long``. There is no need to get either ``__int128_t`` or ``timespec`` emulation
involved. And nor should we, as it would suffer from the numerous complications
described by this paper.

Obviously our implementation for 32-bit builds should act as similarly to the
64-bit build as possible. Code which compiles in one, should compile in the other.
This consideration is important when choosing between ``__int128_t`` and
emulating ``timespec``. The solution which provides the most uniformity with
the least eccentricity is the preferable one.

Summary
=======

The ``file_time_type`` time point is used to represent the write times for files.
Its job is to act as part of a C++ wrapper for less ideal system interfaces. The
underlying filesystem uses the ``timespec`` struct for the same purpose.

However, the initial implementation of ``file_time_type`` could not represent
either the range or resolution of ``timespec``, making it unsuitable. Fixing
this requires an implementation which uses more than 64 bits to store the
time point.

We primarily considered two solutions: Using ``__int128_t`` and using a
arithmetic emulation of ``timespec``. Each has its pros and cons, and both
come with more than one complication.

The Potential Solutions
-----------------------

``long long`` - The Status Quo
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Pros:

* As a type ``long long`` plays the nicest with others:

  * It works with streaming operators and other library entities which support
    builtin integer types, but don't support ``__int128_t``.
  * Its the representation used by chrono's ``nanosecond`` and ``second`` typedefs.

Cons:

* It cannot provide the same resolution as ``timespec`` unless we limit it
  to a range of +/- 300 years from the epoch.
* It cannot provide the same range as ``timespec`` unless we limit its resolution
  to seconds.
* ``last_write_time`` has to report an error when the time reported by the filesystem
  is unrepresentable.

__int128_t
~~~~~~~~~~~

Pros:

* It is an integer type.
* It makes the implementation simple and efficient.
* Acts exactly like other arithmetic types.
* Can be implicitly converted to a builtin integer type by the user.

  * This is important for doing things like:

    .. code-block:: cpp

      void c_interface_using_time_t(const char* p, time_t);

      void foo(path p) {
        file_time_type tp = last_write_time(p);
        time_t secs = duration_cast<seconds>(tp.time_since_epoch()).count();
        c_interface_using_time_t(p.c_str(), secs);
      }

Cons:

* It isn't always available (but on 64 bit machines, it normally is).
* It causes ``file_time_type`` to have a larger range than ``timespec``.
* It doesn't always act the same as other builtin integer types. For example
  with ``cout`` or ``to_string``.
* Allows implicit truncation to 64 bit integers.
* It can be implicitly converted to a builtin integer type by the user,
  truncating its value.

Arithmetic ``timespec`` Emulation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Pros:

* It has the exact same range and resolution of ``timespec`` when representing
  a nanosecond tick period.
* It's always available, unlike ``__int128_t``.

Cons:

* It has a larger range when representing any period longer than a nanosecond.
* Doesn't actually allow users to use it like a ``timespec``.
* The required representation of using ``tv_sec`` to store the giga tick count
  and ``tv_nsec`` to store the remainder adds nothing over a 128 bit integer,
  but complicates a lot.
* It isn't a builtin integer type, and can't be used anything like one.
* Chrono can be made to work with it, but not nicely.
* Emulating arithmetic classes come with their own host of problems regarding
  overload resolution (Each operator needs three SFINAE constrained versions of
  it in order to act like builtin integer types).
* It offers little over simply using ``__int128_t``.
* It acts the most differently than implementations using an actual integer type,
  which has a high chance of breaking source compatibility.


Selected Solution - Using ``__int128_t``
=========================================

The solution I selected for libc++ is using ``__int128_t`` when available,
and otherwise falling back to using ``long long`` with nanosecond precision.

When ``__int128_t`` is available, or when ``time_t`` is 32-bits, the implementation
provides same resolution and a greater range than ``timespec``. Otherwise
it still provides the same resolution, but is limited to a range of +/- 300
years. This final case should be rather rare, as ``__int128_t``
is normally available in 64-bit builds, and ``time_t`` is normally 32-bits
during 32-bit builds.

Although falling back to ``long long`` and nanosecond precision is less than
ideal, it also happens to be the implementation provided by both libstdc++
and MSVC. (So that makes it better, right?)

Although the ``timespec`` emulation solution is feasible and would largely
do what we want, it comes with too many complications, potential problems
and discrepancies when compared to "normal" chrono time points and durations.

An emulation of a builtin arithmetic type using a class is never going to act
exactly the same, and the difference will be felt by users. It's not reasonable
to expect them to tolerate and work around these differences. And once
we commit to an ABI it will be too late to change. Committing to this seems
risky.

Therefore, ``__int128_t`` seems like the better solution.
