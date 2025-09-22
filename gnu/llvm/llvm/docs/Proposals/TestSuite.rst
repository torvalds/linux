=====================
Test-Suite Extensions
=====================

.. contents::
   :depth: 1
   :local:

Abstract
========

These are ideas for additional programs, benchmarks, applications and
algorithms that could be added to the LLVM Test-Suite.
The test-suite could be much larger than it is now, which would help us
detecting compiler errors (crashes, miscompiles) during development.

Most probably, the reason why the programs below have not been added to
the test-suite yet is that nobody has found time to do it. But there
might be other issues as well, such as

 * Licensing (Support can still be added as external module,
              like for the SPEC benchmarks)

 * Language (in particular, there is no official LLVM frontend
             for FORTRAN yet)

 * Parallelism (currently, all programs in test-suite use
                one thread only)

Benchmarks
==========

SPEC CPU 2017
-------------
https://www.spec.org/cpu2017/

The following have not been included yet because they contain Fortran
code.

In case of cactuBSSN only a small portion is Fortran. The hosts's
Fortran compiler could be used for these parts.

Note that CMake's Ninja generator has difficulties with Fortran. See the
`CMake documentation <https://cmake.org/cmake/help/v3.13/generator/Ninja.html#fortran-support>`_
for details.

 * 503.bwaves_r/603.bwaves_s
 * 507.cactuBSSN_r
 * 521.wrf_r/621.wrf_s
 * 527.cam4_r/627.cam4_s
 * 628.pop2_s
 * 548.exchange2_r/648.exchange2_s
 * 549.fotonik3d_r/649.fotonik3d_s
 * 554.roms_r/654.roms_s

SPEC OMP2012
------------
https://www.spec.org/omp2012/

 * 350.md
 * 351.bwaves
 * 352.nab
 * 357.bt331
 * 358.botsalgn
 * 359.botsspar
 * 360.ilbdc
 * 362.fma3d
 * 363.swim
 * 367.imagick
 * 370.mgrid331
 * 371.applu331
 * 372.smithwa
 * 376.kdtree

OpenCV
------
https://opencv.org/

OpenMP 4.x SIMD Benchmarks
--------------------------
https://github.com/flwende/simd_benchmarks

PWM-benchmarking
----------------
https://github.com/tbepler/PWM-benchmarking

SLAMBench
---------
https://github.com/pamela-project/slambench

FireHose
--------
http://firehose.sandia.gov/

A Benchmark for the C/C++ Standard Library
------------------------------------------
https://github.com/hiraditya/std-benchmark

OpenBenchmarking.org CPU / Processor Suite
------------------------------------------
https://openbenchmarking.org/suite/pts/cpu

This is a subset of the
`Phoronix Test Suite <https://github.com/phoronix-test-suite/phoronix-test-suite/>`_
and is itself a collection of benchmark suites

Parboil Benchmarks
------------------
http://impact.crhc.illinois.edu/parboil/parboil.aspx

MachSuite
---------
https://breagen.github.io/MachSuite/

Rodinia
-------
http://lava.cs.virginia.edu/Rodinia/download_links.htm

Rodinia has already been partially included in
MultiSource/Benchmarks/Rodinia. Benchmarks still missing are:

 * streamcluster
 * particlefilter
 * nw
 * nn
 * myocyte
 * mummergpu
 * lud
 * leukocyte
 * lavaMD
 * kmeans
 * hotspot3D
 * heartwall
 * cfd
 * bfs
 * b+tree

vecmathlib tests harness
------------------------
https://bitbucket.org/eschnett/vecmathlib/wiki/Home

PARSEC
------
http://parsec.cs.princeton.edu/

Graph500 reference implementations
----------------------------------
https://github.com/graph500/graph500/tree/v2-spec

NAS Parallel Benchmarks
-----------------------
https://www.nas.nasa.gov/publications/npb.html

The official benchmark is written in Fortran, but an unofficial
C-translation is available as well:
https://github.com/benchmark-subsetting/NPB3.0-omp-C

DARPA HPCS SSCA#2 C/OpenMP reference implementation
---------------------------------------------------
http://www.highproductivity.org/SSCABmks.htm

This web site does not exist any more, but there seems to be a copy of
some of the benchmarks
https://github.com/gtcasl/hpc-benchmarks/tree/master/SSCA2v2.2

Kokkos
------
https://github.com/kokkos/kokkos-kernels/tree/master/perf_test
https://github.com/kokkos/kokkos/tree/master/benchmarks

PolyMage
--------
https://github.com/bondhugula/polymage-benchmarks

PolyBench
---------
https://sourceforge.net/projects/polybench/

A modified version of Polybench 3.2 is already presented in
SingleSource/Benchmarks/Polybench. A newer version 4.2.1 is available.

High Performance Geometric Multigrid
------------------------------------
https://crd.lbl.gov/departments/computer-science/PAR/research/hpgmg/

RAJA Performance Suite
----------------------
https://github.com/LLNL/RAJAPerf

CORAL-2 Benchmarks
------------------
https://asc.llnl.gov/coral-2-benchmarks/

Many of its programs have already been integrated in
MultiSource/Benchmarks/DOE-ProxyApps-C and
MultiSource/Benchmarks/DOE-ProxyApps-C++.

 * Nekbone
 * QMCPack
 * LAMMPS
 * Kripke
 * Quicksilver
 * PENNANT
 * Big Data Analytic Suite
 * Deep Learning Suite
 * Stream
 * Stride
 * ML/DL micro-benchmark
 * Pynamic
 * ACME
 * VPIC
 * Laghos
 * Parallel Integer Sort
 * Havoq

NWChem
------
http://www.nwchem-sw.org/index.php/Benchmarks

TVM
----
https://github.com/dmlc/tvm/tree/main/apps/benchmark

HydroBench
----------
https://github.com/HydroBench/Hydro

ParRes
------
https://github.com/ParRes/Kernels/tree/default/Cxx11

Applications/Libraries
======================

GnuPG
-----
https://gnupg.org/

Blitz++
-------
https://sourceforge.net/projects/blitz/

FFmpeg
------
https://ffmpeg.org/

FreePOOMA
---------
http://www.nongnu.org/freepooma/

FTensors
--------
http://www.wlandry.net/Projects/FTensor

rawspeed
--------
https://github.com/darktable-org/rawspeed

Its test dataset is 756 MB in size, which is too large to be included
into the test-suite repository.

C++ Performance Benchmarks
--------------------------
https://gitlab.com/chriscox/CppPerformanceBenchmarks

Generic Algorithms
==================

Image processing
----------------

Resampling
``````````

 * Bilinear
 * Bicubic
 * Lanczos

Dither
``````

 * Threshold
 * Random
 * Halftone
 * Bayer
 * Floyd-Steinberg
 * Jarvis
 * Stucki
 * Burkes
 * Sierra
 * Atkinson
 * Gradient-based

Feature detection
`````````````````

 * Harris
 * Histogram of Oriented Gradients

Color conversion
````````````````

 * RGB to grayscale
 * HSL to RGB

Graph
-----

Search Algorithms
`````````````````

 * Breadth-First-Search
 * Depth-First-Search
 * Dijkstra's algorithm
 * A-Star

Spanning Tree
`````````````

 * Kruskal's algorithm
 * Prim's algorithm
