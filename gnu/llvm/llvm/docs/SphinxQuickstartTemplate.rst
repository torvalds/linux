==========================
Sphinx Quickstart Template
==========================

This article is intended to take someone in the state of “I want to write documentation and get it added to LLVM’s docs” and help them start writing documentation as fast as possible and with as little nonsense as possible.

.. contents::
   :local:

Overview
========

LLVM documentation is written in `reStructuredText`_, a markup syntax similar to markdown (but much more powerful). The LLVM documentation site itself uses `Sphinx`_, a documentation generator originally written for Python documentation.

.. _`reStructuredText`: http://www.sphinx-doc.org/en/master/usage/restructuredtext/basics.html
.. _`Sphinx`: http://www.sphinx-doc.org

How to use this template
========================

This article is located in ``docs/SphinxQuickstartTemplate.rst``. To use it as a template, make a copy and open it in a text editor. You can then write your docs, and then send the new article to llvm-commits for review.

To view the restructuredText source file for this article, click **Show Source** on the right sidebar.

Authoring Guidelines
====================

Focus on *content*. It is easy to fix the Sphinx (reStructuredText) syntax
later if necessary, although reStructuredText tries to imitate common
plain-text conventions so it should be quite natural. A basic knowledge of
reStructuredText syntax is useful when writing the document, so the last
~half of this document (starting with `Example Section`_) gives examples
which should cover 99% of use cases.

Let me say that again: focus on *content*. But if you really need to verify
Sphinx's output, see ``docs/README.txt`` for information. Once you have finished with the content, please send the ``.rst`` file to
llvm-commits for review.

Creating New Articles
---------------------

Before creating a new article, consider the following questions:

#. Why would I want to read this document?

#. What should I know to be able to follow along with this document?

#. What will I have learned by the end of this document?

A standard best practice is to make your articles task-oriented. You generally should not be writing documentation that isn't based around "how to" do something
unless there's already an existing "how to" article for the topic you're documenting. The reason for this is that without a "how to" article to read first, it might be difficult for
someone unfamiliar with the topic to understand a more advanced, conceptual article.

When creating a task-oriented article, follow existing LLVM articles by giving it a filename that starts with ``HowTo*.rst``. This format is usually the easiest for another person to understand and also the most useful.

Focus on content (yes, I had to say it again).

The rest of this document shows example reStructuredText markup constructs
that are meant to be read by you in your text editor after you have copied
this file into a new file for the documentation you are about to write.

Example Section
===============

An article can contain one or more sections (i.e., headings). Sections (like ``Example Section`` above) help give your document its
structure. Use the same kind of adornments (e.g. ``======`` vs. ``------``)
as are used in this document. The adornment must be the same length as the
text above it. For Vim users, variations of ``yypVr=`` might be handy.

Example Nested Subsection
-------------------------

Subsections can also be nested beneath other subsections. For more information on sections, see Sphinx's `reStructuredText Primer`_.

.. _`reStructuredText Primer`: http://www.sphinx-doc.org/en/master/usage/restructuredtext/basics.html#sections

Text Formatting
===============

Text can be *emphasized*, **bold**, or ``monospace``.

To create a new paragraph, simply insert a blank line.

Links
=====

You can format a link `like this <https://llvm.org/>`_. A more `sophisticated syntax`_ allows you to place the ``.. _`link text`: <URL>`` block
pretty much anywhere else in the document. This is useful when linking to especially long URLs.

.. _`sophisticated syntax`: http://en.wikipedia.org/wiki/LLVM

Lists
=====

restructuredText allows you to create ordered lists...

#. A list starting with ``#.`` will be automatically numbered.

#. This is a second list element.

   #. Use indentation to create nested lists.

...as well as unordered lists:

* Stuff.

  + Deeper stuff.

* More stuff.

Code Blocks
===========

You can make blocks of code like this:

.. code-block:: c++

   int main() {
     return 0;
   }

For a shell session, use a ``console`` code block (some existing docs use
``bash``):

.. code-block:: console

   $ echo "Goodbye cruel world!"
   $ rm -rf /

If you need to show LLVM IR use the ``llvm`` code block.

.. code-block:: llvm

   define i32 @test1() {
   entry:
     ret i32 0
   }

Some other common code blocks you might need are ``c``, ``objc``, ``make``,
and ``cmake``. If you need something beyond that, you can look at the `full
list`_ of supported code blocks.

.. _`full list`: http://pygments.org/docs/lexers/

However, don't waste time fiddling with syntax highlighting when you could
be adding meaningful content. When in doubt, show preformatted text
without any syntax highlighting like this:

::

                          .
                           +:.
                       ..:: ::
                    .++:+:: ::+:.:.
                   .:+           :
            ::.::..::            .+.
          ..:+    ::              :
    ......+:.                    ..
          :++.    ..              :
            .+:::+::              :
            ..   . .+            ::
                     +.:      .::+.
                      ...+. .: .
                         .++:..
                          ...


Generating the documentation
============================

You can generate the HTML documentation from the sources locally if you want to
see what they would look like. In addition to the normal
`build tools <GettingStarted.html>`_
you need to install `Sphinx`_ and the necessary extensions
using the following command inside the ``llvm-project`` checkout:

.. code-block:: console

   pip install --user -r ./llvm/docs/requirements.txt

Then run cmake to build the documentation inside the ``llvm-project`` checkout:

.. code-block:: console

   mkdir build
   cd build
   cmake -DLLVM_ENABLE_SPHINX=On ../llvm
   cmake --build . --target docs-llvm-html

In case you already have the Cmake build set up and want to reuse that,
just set the CMake variable ``LLVM_ENABLE_SPHINX=On``.

After that you find the generated documentation in ``build/docs/html``
folder.
