.. spaceship-status:

==============================================
libc++ Spaceship Operator Status (operator<=>)
==============================================

.. include:: ../Helpers/Styles.rst

.. contents::
   :local:


Overview
================================

This document contains the status of the C++20 spaceship operator support
in libc++. It is used to track both the status of the sub-projects of the effort
and who is assigned to these sub-projects. This is imperative to effective
implementation so that work is not duplicated and implementors are not blocked
by each other.

If you are interested in contributing to this effort, please send a message
to the #libcxx channel in the LLVM discord. Please *do not* start working on any
of the assigned items below.


Sub-Projects in the Implementation Effort
=========================================

.. csv-table::
   :file: SpaceshipProjects.csv
   :header-rows: 1
   :widths: auto

.. note::

   .. [#note-strongorder] ``std::strong_order(long double, long double)`` is not yet implemented.


Misc. Items and TODOs
====================================

(Note: files with required updates will contain the TODO at the beginning of the
list item so they can be easily found via global search.)


Paper and Issue Status
====================================

.. csv-table::
   :file: SpaceshipPapers.csv
   :header-rows: 1
   :widths: auto
