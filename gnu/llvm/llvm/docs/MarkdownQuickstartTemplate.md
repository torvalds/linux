# Markdown Quickstart Template

## Introduction and Quickstart

This document is meant to get you writing documentation as fast as possible
even if you have no previous experience with Markdown. The goal is to take
someone in the state of "I want to write documentation and get it added to
LLVM's docs" and turn that into useful documentation mailed to llvm-commits
with as little nonsense as possible.

You can find this document in `docs/MarkdownQuickstartTemplate.md`. You
should copy it, open the new file in your text editor, write your docs, and
then send the new document to llvm-commits for review.

Focus on *content*. It is easy to fix the Markdown syntax
later if necessary, although Markdown tries to imitate common
plain-text conventions so it should be quite natural. A basic knowledge of
Markdown syntax is useful when writing the document, so the last
~half of this document (starting with [Example Section](#example-section)) gives examples
which should cover 99% of use cases.

Let me say that again: focus on *content*. But if you really need to verify
Sphinx's output, see `docs/README.txt` for information.

Once you have finished with the content, please send the `.md` file to
llvm-commits for review.

## Guidelines

Try to answer the following questions in your first section:

1. Why would I want to read this document?

2. What should I know to be able to follow along with this document?

3. What will I have learned by the end of this document?

Common names for the first section are `Introduction`, `Overview`, or
`Background`.

If possible, make your document a "how to". Give it a name `HowTo*.md`
like the other "how to" documents. This format is usually the easiest
for another person to understand and also the most useful.

You generally should not be writing documentation other than a "how to"
unless there is already a "how to" about your topic. The reason for this
is that without a "how to" document to read first, it is difficult for a
person to understand a more advanced document.

Focus on content (yes, I had to say it again).

The rest of this document shows example Markdown markup constructs
that are meant to be read by you in your text editor after you have copied
this file into a new file for the documentation you are about to write.

## Example Section

Your text can be *emphasized*, **bold**, or `monospace`.

Use blank lines to separate paragraphs.

Headings (like `Example Section` just above) give your document its
structure.

### Example Subsection

Make a link [like this](https://llvm.org/). There is also a more
sophisticated syntax which [can be more readable] for longer links since
it disrupts the flow less. You can put the `[link name]: <URL>` block
pretty much anywhere later in the document.

[can be more readable]: http://en.wikipedia.org/wiki/LLVM

Lists can be made like this:

1. A list starting with `[0-9].` will be automatically numbered.

1. This is a second list element.

   1. Use indentation to create nested lists.

You can also use unordered lists.

* Stuff.

  + Deeper stuff.

* More stuff.

#### Example Subsubsection

You can make blocks of code like this:

```
int main() {
  return 0;
}
```

As an extension to markdown, you can also specify a highlighter to use.

``` C++
int main() {
  return 0;
}
```

For a shell session, use a `console` code block.

```console
$ echo "Goodbye cruel world!"
$ rm -rf /
```

If you need to show LLVM IR use the `llvm` code block.

``` llvm
define i32 @test1() {
entry:
  ret i32 0
}
```

Some other common code blocks you might need are `c`, `objc`, `make`,
and `cmake`. If you need something beyond that, you can look at the [full
list] of supported code blocks.

[full list]: http://pygments.org/docs/lexers/

However, don't waste time fiddling with syntax highlighting when you could
be adding meaningful content. When in doubt, show preformatted text
without any syntax highlighting like this:

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

##### Hopefully you won't need to be this deep

If you need to do fancier things than what has been shown in this document,
you can mail the list or check the [Common Mark spec].  Sphinx specific
integration documentation can be found in the [myst-parser docs].

[Common Mark spec]: http://spec.commonmark.org/0.28/
[myst-parser docs]: https://myst-parser.readthedocs.io/en/latest/

## Generating the documentation

see [Sphinx Quickstart Template](project:SphinxQuickstartTemplate.rst#Generating the documentation)
