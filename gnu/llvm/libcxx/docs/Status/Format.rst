.. format-status:

================================
libc++ Format Status
================================

.. include:: ../Helpers/Styles.rst

.. contents::
   :local:


Overview
========

This document contains the status of the Format library in libc++. It is used to
track both the status of the sub-projects of the Format library and who is assigned to
these sub-projects. This is imperative to effective implementation so that work is not
duplicated and implementors are not blocked by each other.


If you are interested in contributing to the libc++ Format library, please send
a message to the #libcxx channel in the LLVM discord. Please *do not* start
working on any of the assigned items below.


Sub-Projects in the Format library
==================================

.. csv-table::
   :file: FormatPaper.csv
   :header-rows: 1
   :widths: auto


Misc. Items and TODOs
=====================

(Please mark all Format-related TODO comments with the string ``TODO FMT``, so we
can find them easily.)


Paper and Issue Status
======================

.. csv-table::
   :file: FormatIssues.csv
   :header-rows: 1
   :widths: auto
