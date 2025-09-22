=====================================
The Module Information Stream
=====================================

.. contents::
   :local:

.. _modi_stream_intro:

Introduction
============

The Module Info Stream (henceforth referred to as the Modi stream) contains
information about a single module (object file, import library, etc that
contributes to the binary this PDB contains debug information about.  There
is one modi stream for each module, and the mapping between modi stream index
and module is contained in the :doc:`DBI Stream <DbiStream>`.  The modi stream
for a single module contains line information for the compiland, as well as
all CodeView information for the symbols defined in the compiland.  Finally,
there is a "global refs" substream which is not well understood.

.. _modi_stream_layout:

Stream Layout
=============

A modi stream is laid out as follows:


.. code-block:: c++

  struct ModiStream {
    uint32_t Signature;
    uint8_t Symbols[SymbolSize-4];
    uint8_t C11LineInfo[C11Size];
    uint8_t C13LineInfo[C13Size];

    uint32_t GlobalRefsSize;
    uint8_t GlobalRefs[GlobalRefsSize];
  };

- **Signature** - Unknown.  In practice only the value of ``4`` has been
  observed.  It is hypothesized that this value corresponds to the set of
  ``CV_SIGNATURE_xx`` defines in ``cvinfo.h``, with the value of ``4``
  meaning that this module has C13 line information (as opposed to C11 line
  information).  A corollary of this is that we expect to only ever see
  C13 line info, and that we do not understand the format of C11 line info.

- **Symbols** - The :ref:`CodeView Symbol Substream <modi_symbol_substream>`.
  ``SymbolSize`` is equal to the value of ``SymByteSize`` for the
  corresponding module's entry in the :ref:`Module Info Substream
  <dbi_mod_info_substream>` of the :doc:`DBI Stream <DbiStream>`.

- **C11LineInfo** - A block containing CodeView line information in C11
  format.  ``C11Size`` is equal to the value of ``C11ByteSize`` from the
  :ref:`Module Info Substream <dbi_mod_info_substream>` of the
  :doc:`DBI Stream <DbiStream>`.  If this value is ``0``, then C11 line
  information is not present.  As mentioned previously, the format of
  C11 line info is not understood and we assume all line in modern PDBs
  to be in C13 format.

- **C13LineInfo** - A block containing CodeView line information in C13
  format.  ``C13Size`` is equal to the value of ``C13ByteSize`` from the
  :ref:`Module Info Substream <dbi_mod_info_substream>` of the
  :doc:`DBI Stream <DbiStream>`.  If this value is ``0``, then C13 line
  information is not present.

- **GlobalRefs** - The meaning of this substream is not understood.

.. _modi_symbol_substream:

The CodeView Symbol Substream
=============================

The CodeView Symbol Substream.  This is an array of variable length
records describing the functions, variables, inlining information,
and other symbols defined in the compiland.  The entire array consumes
``SymbolSize-4`` bytes.  The format of a CodeView Symbol Record (and
thusly, an array of CodeView Symbol Records) is described in
:doc:`CodeViewSymbols`.
