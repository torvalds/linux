=========
LibFormat
=========

LibFormat is a library that implements automatic source code formatting based
on Clang. This documents describes the LibFormat interface and design as well
as some basic style discussions.

If you just want to use `clang-format` as a tool or integrated into an editor,
checkout :doc:`ClangFormat`.

Design
------

FIXME: Write up design.


Interface
---------

The core routine of LibFormat is ``reformat()``:

.. code-block:: c++

  tooling::Replacements reformat(const FormatStyle &Style, Lexer &Lex,
                                 SourceManager &SourceMgr,
                                 std::vector<CharSourceRange> Ranges);

This reads a token stream out of the lexer ``Lex`` and reformats all the code
ranges in ``Ranges``. The ``FormatStyle`` controls basic decisions made during
formatting. A list of options can be found under :ref:`style-options`.

The style options are described in :doc:`ClangFormatStyleOptions`.


.. _style-options:

Style Options
-------------

The style options describe specific formatting options that can be used in
order to make `ClangFormat` comply with different style guides. Currently,
several style guides are hard-coded:

.. code-block:: c++

  /// Returns a format style complying with the LLVM coding standards:
  /// https://llvm.org/docs/CodingStandards.html.
  FormatStyle getLLVMStyle();

  /// Returns a format style complying with Google's C++ style guide:
  /// http://google-styleguide.googlecode.com/svn/trunk/cppguide.xml.
  FormatStyle getGoogleStyle();

  /// Returns a format style complying with Chromium's style guide:
  /// https://chromium.googlesource.com/chromium/src/+/refs/heads/main/styleguide/styleguide.md
  FormatStyle getChromiumStyle();

  /// Returns a format style complying with the GNU coding standards:
  /// https://www.gnu.org/prep/standards/standards.html
  FormatStyle getGNUStyle();

  /// Returns a format style complying with Mozilla's style guide
  /// https://firefox-source-docs.mozilla.org/code-quality/coding-style/index.html
  FormatStyle getMozillaStyle();

  /// Returns a format style complying with Webkit's style guide:
  /// https://webkit.org/code-style-guidelines/
  FormatStyle getWebkitStyle();

  /// Returns a format style complying with Microsoft's style guide:
  /// https://docs.microsoft.com/en-us/visualstudio/ide/editorconfig-code-style-settings-reference
  FormatStyle getMicrosoftStyle();

These options are also exposed in the :doc:`standalone tools <ClangFormat>`
through the `-style` option.

In the future, we plan on making this configurable.
