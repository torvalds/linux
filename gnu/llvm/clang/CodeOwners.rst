=================
Clang Code Owners
=================

This file is a list of the
`code owners <https://llvm.org/docs/DeveloperPolicy.html#code-owners>`_ for
Clang.

.. contents::
   :depth: 2
   :local:

Current Code Owners
===================
The following people are the active code owners for the project. Please reach
out to them for code reviews, questions about their area of expertise, or other
assistance.

All parts of Clang not covered by someone else
----------------------------------------------
| Aaron Ballman
| aaron\@aaronballman.com (email), aaron.ballman (Phabricator), AaronBallman (GitHub), AaronBallman (Discourse), aaronballman (Discord), AaronBallman (IRC)


Contained Components
--------------------
These code owners are responsible for particular high-level components within
Clang that are typically contained to one area of the compiler.

AST matchers
~~~~~~~~~~~~
| Manuel Klimek
| klimek\@google.com (email), klimek (Phabricator), r4nt (GitHub)


Clang LLVM IR generation
~~~~~~~~~~~~~~~~~~~~~~~~
| John McCall
| rjmccall\@apple.com (email), rjmccall (Phabricator), rjmccall (GitHub)

| Eli Friedman
| efriedma\@quicinc.com (email), efriedma (Phabricator), efriedma-quic (GitHub)

| Anton Korobeynikov
| anton\@korobeynikov.info (email), asl (Phabricator), asl (GitHub)


Analysis & CFG
~~~~~~~~~~~~~~
| Dmitri Gribenko
| gribozavr\@gmail.com (email), gribozavr (Phabricator), gribozavr (GitHub)

| Yitzhak Mandelbaum
| yitzhakm\@google.com (email), ymandel (Phabricator), ymand (GitHub)

| Stanislav Gatev
| sgatev\@google.com (email), sgatev (Phabricator), sgatev (GitHub)


Experimental new constant interpreter
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
| Timm Bäder
| tbaeder\@redhat.com (email), tbaeder (Phabricator), tbaederr (GitHub), tbaeder (Discourse), tbaeder (Discord)


Modules & serialization
~~~~~~~~~~~~~~~~~~~~~~~
| Chuanqi Xu
| yedeng.yd\@linux.alibaba.com (email), ChuanqiXu (Phabricator), ChuanqiXu9 (GitHub)

| Michael Spencer
| bigcheesegs\@gmail.com (email), Bigcheese (Phabricator), Bigcheese (GitHub)


Templates
~~~~~~~~~
| Erich Keane
| ekeane\@nvidia.com (email), ErichKeane (Phabricator), erichkeane (GitHub)


Debug information
~~~~~~~~~~~~~~~~~
| Adrian Prantl
| aprantl\@apple.com (email), aprantl (Phabricator), adrian-prantl (GitHub)

| David Blaikie
| dblaikie\@gmail.com (email), dblaikie (Phabricator), dwblaikie (GitHub)

| Eric Christopher
| echristo\@gmail.com (email), echristo (Phabricator), echristo (GitHub)


Exception handling
~~~~~~~~~~~~~~~~~~
| Anton Korobeynikov
| anton\@korobeynikov.info (email), asl (Phabricator), asl (GitHub)


Clang static analyzer
~~~~~~~~~~~~~~~~~~~~~
| Artem Dergachev
| adergachev\@apple.com (email), NoQ (Phabricator), haoNoQ (GitHub)

| Gábor Horváth
| xazax.hun\@gmail.com (email), xazax.hun (Phabricator), Xazax-hun (GitHub)


Compiler options
~~~~~~~~~~~~~~~~
| Jan Svoboda
| jan_svoboda\@apple.com (email), jansvoboda11 (Phabricator), jansvoboda11 (GitHub)


OpenBSD driver
~~~~~~~~~~~~~~
| Brad Smith
| brad\@comstyle.com (email), brad (Phabricator), brad0 (GitHub)


Driver parts not covered by someone else
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
| Fangrui Song
| maskray\@google.com (email), MaskRay (Phabricator), MaskRay (GitHub)


Tools
-----
These code owners are responsible for user-facing tools under the Clang
umbrella or components used to support such tools.

Tooling library
~~~~~~~~~~~~~~~
| Manuel Klimek
| klimek\@google.com (email), klimek (Phabricator), r4nt (GitHub)


clang-format
~~~~~~~~~~~~
| MyDeveloperDay
| mydeveloperday\@gmail.com (email), MyDeveloperDay (Phabricator), MyDeveloperDay (GitHub)

| Owen Pan
| owenpiano\@gmail.com (email), owenpan (Phabricator), owenca (GitHub)


ABIs
----
The following people are responsible for decisions involving ABI.

Itanium ABI
~~~~~~~~~~~
| John McCall
| rjmccall\@apple.com (email), rjmccall (Phabricator), rjmccall (GitHub)


Microsoft ABI
~~~~~~~~~~~~~
| Reid Kleckner
| rnk\@google.com (email), rnk (Phabricator), rnk (GitHub)


ARM EABI
~~~~~~~~
| Anton Korobeynikov
| anton\@korobeynikov.info (email), asl (Phabricator), asl (GitHub)


Compiler-Wide Topics
--------------------
The following people are responsible for functionality that does not fit into
a single part of the compiler, but instead span multiple components within the
compiler.

Attributes
~~~~~~~~~~
| Erich Keane
| ekeane\@nvidia.com (email), ErichKeane (Phabricator), erichkeane (GitHub)


Inline assembly
~~~~~~~~~~~~~~~
| Eric Christopher
| echristo\@gmail.com (email), echristo (Phabricator), echristo (GitHub)


Text encodings
~~~~~~~~~~~~~~
| Tom Honermann
| tom\@honermann.net (email), tahonermann (Phabricator), tahonermann (GitHub)

| Corentin Jabot
| corentin.jabot\@gmail.com (email), cor3ntin (Phabricator), cor3ntin (GitHub)


CMake integration
~~~~~~~~~~~~~~~~~
| Petr Hosek
| phosek\@google.com (email), phosek (Phabricator), petrhosek (GitHub)

| John Ericson
| git\@johnericson.me (email), Ericson2314 (Phabricator), Ericson2314 (GitHub)


General Windows support
~~~~~~~~~~~~~~~~~~~~~~~
| Reid Kleckner
| rnk\@google.com (email), rnk (Phabricator), rnk (GitHub)


Incremental compilation, REPLs, clang-repl
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
| Vassil Vassilev
| Vassil.Vassilev\@cern.ch (email), v.g.vassilev (Phabricator), vgvassilev (GitHub)


Standards Conformance
---------------------
The following people are responsible for validating that changes are conforming
to a relevant standard. Contact them for questions about how to interpret a
standard, when fixing standards bugs, or when implementing a new standard feature.

C conformance
~~~~~~~~~~~~~
| Aaron Ballman
| aaron\@aaronballman.com (email), aaron.ballman (Phabricator), AaronBallman (GitHub), AaronBallman (Discourse), aaronballman (Discord), AaronBallman (IRC)


C++ conformance
~~~~~~~~~~~~~~~
| Hubert Tong
| hubert.reinterpretcast\@gmail.com (email), hubert.reinterpretcast (Phabricator), hubert-reinterpretcast (GitHub)


Objective-C/C++ conformance
~~~~~~~~~~~~~~~~~~~~~~~~~~~
| John McCall
| rjmccall\@apple.com (email), rjmccall (Phabricator), rjmccall (GitHub)


OpenMP conformance
~~~~~~~~~~~~~~~~~~
| Alexey Bataev
| a.bataev\@hotmail.com (email), ABataev (Phabricator), alexey-bataev (GitHub)


OpenCL conformance
~~~~~~~~~~~~~~~~~~
| Anastasia Stulova
| anastasia\@compiler-experts.com (email), Anastasia (Phabricator), AnastasiaStulova (GitHub)


SYCL conformance
~~~~~~~~~~~~~~~~
| Alexey Bader
| alexey.bader\@intel.com (email), bader (Phabricator), bader (GitHub)


Former Code Owners
==================
The following people have graciously spent time performing code ownership
responsibilities but are no longer active in that role. Thank you for all your
help with the success of the project!

Emeritus owners
---------------
| Doug Gregor (dgregor\@apple.com)
| Richard Smith (richard\@metafoo.co.uk)


Former component owners
-----------------------
| Chandler Carruth (chandlerc\@gmail.com, chandlerc\@google.com) -- CMake, library layering
| Devin Coughlin (dcoughlin\@apple.com) -- Clang static analyzer
