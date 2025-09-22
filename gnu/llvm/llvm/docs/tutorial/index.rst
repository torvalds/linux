================================
LLVM Tutorial: Table of Contents
================================

Kaleidoscope: Implementing a Language with LLVM
===============================================

.. toctree::
   :hidden:

   MyFirstLanguageFrontend/index

:doc:`MyFirstLanguageFrontend/index`
  This is the "Kaleidoscope" Language tutorial, showing how to implement a simple
  language using LLVM components in C++.

.. toctree::
   :titlesonly:
   :glob:
   :numbered:

   MyFirstLanguageFrontend/LangImpl*

Building a JIT in LLVM
===============================================

.. toctree::
   :titlesonly:
   :glob:
   :numbered:

   BuildingAJIT*

External Tutorials
==================

`Tutorial: Creating an LLVM Backend for the Cpu0 Architecture <http://jonathan2251.github.io/lbd/>`_
   A step-by-step tutorial for developing an LLVM backend. Under
   active development at `<https://github.com/Jonathan2251/lbd>`_ (please
   contribute!).

`Howto: Implementing LLVM Integrated Assembler`_
   A simple guide for how to implement an LLVM integrated assembler for an
   architecture.

.. _`Howto: Implementing LLVM Integrated Assembler`: http://www.embecosm.com/appnotes/ean10/ean10-howto-llvmas-1.0.html

Advanced Topics
===============

#. `Writing an Optimization for LLVM <https://llvm.org/pubs/2004-09-22-LCPCLLVMTutorial.html>`_

