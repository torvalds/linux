#!/usr/bin/env python

from __future__ import print_function

import argparse
import errno
import functools
import html
import io
from multiprocessing import cpu_count
import os.path
import re
import shutil
import sys

from pygments import highlight
from pygments.lexers.c_cpp import CppLexer
from pygments.formatters import HtmlFormatter

import optpmap
import optrecord


desc = """Generate HTML output to visualize optimization records from the YAML files
generated with -fsave-optimization-record and -fdiagnostics-show-hotness.

The tools requires PyYAML and Pygments Python packages."""


# This allows passing the global context to the child processes.
class Context:
    def __init__(self, caller_loc=dict()):
        # Map function names to their source location for function where inlining happened
        self.caller_loc = caller_loc


context = Context()


def suppress(remark):
    if remark.Name == "sil.Specialized":
        return remark.getArgDict()["Function"][0].startswith('"Swift.')
    elif remark.Name == "sil.Inlined":
        return remark.getArgDict()["Callee"][0].startswith(
            ('"Swift.', '"specialized Swift.')
        )
    return False


class SourceFileRenderer:
    def __init__(self, source_dir, output_dir, filename, no_highlight):
        self.filename = filename
        existing_filename = None
        if os.path.exists(filename):
            existing_filename = filename
        else:
            fn = os.path.join(source_dir, filename)
            if os.path.exists(fn):
                existing_filename = fn

        self.no_highlight = no_highlight
        self.stream = io.open(
            os.path.join(output_dir, optrecord.html_file_name(filename)),
            "w",
            encoding="utf-8",
        )
        if existing_filename:
            self.source_stream = io.open(existing_filename, encoding="utf-8")
        else:
            self.source_stream = None
            print(
                """
<html>
<h1>Unable to locate file {}</h1>
</html>
            """.format(
                    filename
                ),
                file=self.stream,
            )

        self.html_formatter = HtmlFormatter(encoding="utf-8")
        self.cpp_lexer = CppLexer(stripnl=False)

    def render_source_lines(self, stream, line_remarks):
        file_text = stream.read()

        if self.no_highlight:
            html_highlighted = file_text
        else:
            html_highlighted = highlight(file_text, self.cpp_lexer, self.html_formatter)

            # Note that the API is different between Python 2 and 3.  On
            # Python 3, pygments.highlight() returns a bytes object, so we
            # have to decode.  On Python 2, the output is str but since we
            # support unicode characters and the output streams is unicode we
            # decode too.
            html_highlighted = html_highlighted.decode("utf-8")

            # Take off the header and footer, these must be
            #   reapplied line-wise, within the page structure
            html_highlighted = html_highlighted.replace(
                '<div class="highlight"><pre>', ""
            )
            html_highlighted = html_highlighted.replace("</pre></div>", "")

        for (linenum, html_line) in enumerate(html_highlighted.split("\n"), start=1):
            print(
                """
<tr>
<td><a name=\"L{linenum}\">{linenum}</a></td>
<td></td>
<td></td>
<td><div class="highlight"><pre>{html_line}</pre></div></td>
</tr>""".format(
                    **locals()
                ),
                file=self.stream,
            )

            for remark in line_remarks.get(linenum, []):
                if not suppress(remark):
                    self.render_inline_remarks(remark, html_line)

    def render_inline_remarks(self, r, line):
        inlining_context = r.DemangledFunctionName
        dl = context.caller_loc.get(r.Function)
        if dl:
            dl_dict = dict(list(dl))
            link = optrecord.make_link(dl_dict["File"], dl_dict["Line"] - 2)
            inlining_context = "<a href={link}>{r.DemangledFunctionName}</a>".format(
                **locals()
            )

        # Column is the number of characters *including* tabs, keep those and
        # replace everything else with spaces.
        indent = line[: max(r.Column, 1) - 1]
        indent = re.sub("\S", " ", indent)

        # Create expanded message and link if we have a multiline message.
        lines = r.message.split("\n")
        if len(lines) > 1:
            expand_link = '<a style="text-decoration: none;" href="" onclick="toggleExpandedMessage(this); return false;">+</a>'
            message = lines[0]
            expand_message = """
<div class="full-info" style="display:none;">
  <div class="col-left"><pre style="display:inline">{}</pre></div>
  <div class="expanded col-left"><pre>{}</pre></div>
</div>""".format(
                indent, "\n".join(lines[1:])
            )
        else:
            expand_link = ""
            expand_message = ""
            message = r.message
        print(
            """
<tr>
<td></td>
<td>{r.RelativeHotness}</td>
<td class=\"column-entry-{r.color}\">{r.PassWithDiffPrefix}</td>
<td><pre style="display:inline">{indent}</pre><span class=\"column-entry-yellow\">{expand_link} {message}&nbsp;</span>{expand_message}</td>
<td class=\"column-entry-yellow\">{inlining_context}</td>
</tr>""".format(
                **locals()
            ),
            file=self.stream,
        )

    def render(self, line_remarks):
        if not self.source_stream:
            return

        print(
            """
<html>
<title>{}</title>
<meta charset="utf-8" />
<head>
<link rel='stylesheet' type='text/css' href='style.css'>
<script type="text/javascript">
/* Simple helper to show/hide the expanded message of a remark. */
function toggleExpandedMessage(e) {{
  var FullTextElems = e.parentElement.parentElement.getElementsByClassName("full-info");
  if (!FullTextElems || FullTextElems.length < 1) {{
      return false;
  }}
  var FullText = FullTextElems[0];
  if (FullText.style.display == 'none') {{
    e.innerHTML = '-';
    FullText.style.display = 'block';
  }} else {{
    e.innerHTML = '+';
    FullText.style.display = 'none';
  }}
}}
</script>
</head>
<body>
<div class="centered">
<table class="source">
<thead>
<tr>
<th style="width: 2%">Line</td>
<th style="width: 3%">Hotness</td>
<th style="width: 10%">Optimization</td>
<th style="width: 70%">Source</td>
<th style="width: 15%">Inline Context</td>
</tr>
</thead>
<tbody>""".format(
                os.path.basename(self.filename)
            ),
            file=self.stream,
        )
        self.render_source_lines(self.source_stream, line_remarks)

        print(
            """
</tbody>
</table>
</body>
</html>""",
            file=self.stream,
        )


class IndexRenderer:
    def __init__(
        self, output_dir, should_display_hotness, max_hottest_remarks_on_index
    ):
        self.stream = io.open(
            os.path.join(output_dir, "index.html"), "w", encoding="utf-8"
        )
        self.should_display_hotness = should_display_hotness
        self.max_hottest_remarks_on_index = max_hottest_remarks_on_index

    def render_entry(self, r, odd):
        escaped_name = html.escape(r.DemangledFunctionName)
        print(
            """
<tr>
<td class=\"column-entry-{odd}\"><a href={r.Link}>{r.DebugLocString}</a></td>
<td class=\"column-entry-{odd}\">{r.RelativeHotness}</td>
<td class=\"column-entry-{odd}\">{escaped_name}</td>
<td class=\"column-entry-{r.color}\">{r.PassWithDiffPrefix}</td>
</tr>""".format(
                **locals()
            ),
            file=self.stream,
        )

    def render(self, all_remarks):
        print(
            """
<html>
<meta charset="utf-8" />
<head>
<link rel='stylesheet' type='text/css' href='style.css'>
</head>
<body>
<div class="centered">
<table>
<tr>
<td>Source Location</td>
<td>Hotness</td>
<td>Function</td>
<td>Pass</td>
</tr>""",
            file=self.stream,
        )

        max_entries = None
        if self.should_display_hotness:
            max_entries = self.max_hottest_remarks_on_index

        for i, remark in enumerate(all_remarks[:max_entries]):
            if not suppress(remark):
                self.render_entry(remark, i % 2)
        print(
            """
</table>
</body>
</html>""",
            file=self.stream,
        )


def _render_file(source_dir, output_dir, ctx, no_highlight, entry, filter_):
    global context
    context = ctx
    filename, remarks = entry
    SourceFileRenderer(source_dir, output_dir, filename, no_highlight).render(remarks)


def map_remarks(all_remarks):
    # Set up a map between function names and their source location for
    # function where inlining happened
    for remark in optrecord.itervalues(all_remarks):
        if (
            isinstance(remark, optrecord.Passed)
            and remark.Pass == "inline"
            and remark.Name == "Inlined"
        ):
            for arg in remark.Args:
                arg_dict = dict(list(arg))
                caller = arg_dict.get("Caller")
                if caller:
                    try:
                        context.caller_loc[caller] = arg_dict["DebugLoc"]
                    except KeyError:
                        pass


def generate_report(
    all_remarks,
    file_remarks,
    source_dir,
    output_dir,
    no_highlight,
    should_display_hotness,
    max_hottest_remarks_on_index,
    num_jobs,
    should_print_progress,
):
    try:
        os.makedirs(output_dir)
    except OSError as e:
        if e.errno == errno.EEXIST and os.path.isdir(output_dir):
            pass
        else:
            raise

    if should_print_progress:
        print("Rendering index page...")
    if should_display_hotness:
        sorted_remarks = sorted(
            optrecord.itervalues(all_remarks),
            key=lambda r: (
                r.Hotness,
                r.File,
                r.Line,
                r.Column,
                r.PassWithDiffPrefix,
                r.yaml_tag,
                r.Function,
            ),
            reverse=True,
        )
    else:
        sorted_remarks = sorted(
            optrecord.itervalues(all_remarks),
            key=lambda r: (
                r.File,
                r.Line,
                r.Column,
                r.PassWithDiffPrefix,
                r.yaml_tag,
                r.Function,
            ),
        )
    IndexRenderer(
        output_dir, should_display_hotness, max_hottest_remarks_on_index
    ).render(sorted_remarks)

    shutil.copy(
        os.path.join(os.path.dirname(os.path.realpath(__file__)), "style.css"),
        output_dir,
    )

    _render_file_bound = functools.partial(
        _render_file, source_dir, output_dir, context, no_highlight
    )
    if should_print_progress:
        print("Rendering HTML files...")
    optpmap.pmap(
        _render_file_bound, file_remarks.items(), num_jobs, should_print_progress
    )


def main():
    parser = argparse.ArgumentParser(description=desc)
    parser.add_argument(
        "yaml_dirs_or_files",
        nargs="+",
        help="List of optimization record files or directories searched "
        "for optimization record files.",
    )
    parser.add_argument(
        "--output-dir",
        "-o",
        default="html",
        help="Path to a directory where generated HTML files will be output. "
        "If the directory does not already exist, it will be created. "
        '"%(default)s" by default.',
    )
    parser.add_argument(
        "--jobs",
        "-j",
        default=None,
        type=int,
        help="Max job count (defaults to %(default)s, the current CPU count)",
    )
    parser.add_argument("--source-dir", "-s", default="", help="set source directory")
    parser.add_argument(
        "--no-progress-indicator",
        "-n",
        action="store_true",
        default=False,
        help="Do not display any indicator of how many YAML files were read "
        "or rendered into HTML.",
    )
    parser.add_argument(
        "--max-hottest-remarks-on-index",
        default=1000,
        type=int,
        help="Maximum number of the hottest remarks to appear on the index page",
    )
    parser.add_argument(
        "--no-highlight",
        action="store_true",
        default=False,
        help="Do not use a syntax highlighter when rendering the source code",
    )
    parser.add_argument(
        "--demangler",
        help="Set the demangler to be used (defaults to %s)"
        % optrecord.Remark.default_demangler,
    )

    parser.add_argument(
        "--filter",
        default="",
        help="Only display remarks from passes matching filter expression",
    )

    # Do not make this a global variable.  Values needed to be propagated through
    # to individual classes and functions to be portable with multiprocessing across
    # Windows and non-Windows.
    args = parser.parse_args()

    print_progress = not args.no_progress_indicator
    if args.demangler:
        optrecord.Remark.set_demangler(args.demangler)

    files = optrecord.find_opt_files(*args.yaml_dirs_or_files)
    if not files:
        parser.error("No *.opt.yaml files found")
        sys.exit(1)

    all_remarks, file_remarks, should_display_hotness = optrecord.gather_results(
        files, args.jobs, print_progress, args.filter
    )

    map_remarks(all_remarks)

    generate_report(
        all_remarks,
        file_remarks,
        args.source_dir,
        args.output_dir,
        args.no_highlight,
        should_display_hotness,
        args.max_hottest_remarks_on_index,
        args.jobs,
        print_progress,
    )


if __name__ == "__main__":
    main()
