#!/usr/bin/env python
#
# ===- exploded-graph-rewriter.py - ExplodedGraph dump tool -----*- python -*--#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===-----------------------------------------------------------------------===#


from __future__ import print_function

import argparse
import collections
import difflib
import json
import logging
import os
import re


# ===-----------------------------------------------------------------------===#
# These data structures represent a deserialized ExplodedGraph.
# ===-----------------------------------------------------------------------===#


# A helper function for finding the difference between two dictionaries.
def diff_dicts(curr, prev):
    removed = [k for k in prev if k not in curr or curr[k] != prev[k]]
    added = [k for k in curr if k not in prev or curr[k] != prev[k]]
    return (removed, added)


# Represents any program state trait that is a dictionary of key-value pairs.
class GenericMap:
    def __init__(self, items):
        self.generic_map = collections.OrderedDict(items)

    def diff(self, prev):
        return diff_dicts(self.generic_map, prev.generic_map)

    def is_different(self, prev):
        removed, added = self.diff(prev)
        return len(removed) != 0 or len(added) != 0


# A deserialized source location.
class SourceLocation:
    def __init__(self, json_loc):
        logging.debug("json: %s" % json_loc)
        self.line = json_loc["line"]
        self.col = json_loc["column"]
        self.filename = (
            os.path.basename(json_loc["file"]) if "file" in json_loc else "(main file)"
        )
        self.spelling = (
            SourceLocation(json_loc["spelling"]) if "spelling" in json_loc else None
        )

    def is_macro(self):
        return self.spelling is not None


# A deserialized program point.
class ProgramPoint:
    def __init__(self, json_pp):
        self.kind = json_pp["kind"]
        self.tag = json_pp["tag"]
        self.node_id = json_pp["node_id"]
        self.is_sink = bool(json_pp["is_sink"])
        self.has_report = bool(json_pp["has_report"])
        if self.kind == "Edge":
            self.src_id = json_pp["src_id"]
            self.dst_id = json_pp["dst_id"]
        elif self.kind == "Statement":
            logging.debug(json_pp)
            self.stmt_kind = json_pp["stmt_kind"]
            self.cast_kind = json_pp["cast_kind"] if "cast_kind" in json_pp else None
            self.stmt_point_kind = json_pp["stmt_point_kind"]
            self.stmt_id = json_pp["stmt_id"]
            self.pointer = json_pp["pointer"]
            self.pretty = json_pp["pretty"]
            self.loc = (
                SourceLocation(json_pp["location"])
                if json_pp["location"] is not None
                else None
            )
        elif self.kind == "BlockEntrance":
            self.block_id = json_pp["block_id"]


# A single expression acting as a key in a deserialized Environment.
class EnvironmentBindingKey:
    def __init__(self, json_ek):
        # CXXCtorInitializer is not a Stmt!
        self.stmt_id = (
            json_ek["stmt_id"] if "stmt_id" in json_ek else json_ek["init_id"]
        )
        self.pretty = json_ek["pretty"]
        self.kind = json_ek["kind"] if "kind" in json_ek else None

    def _key(self):
        return self.stmt_id

    def __eq__(self, other):
        return self._key() == other._key()

    def __hash__(self):
        return hash(self._key())


# Deserialized description of a location context.
class LocationContext:
    def __init__(self, json_frame):
        self.lctx_id = json_frame["lctx_id"]
        self.caption = json_frame["location_context"]
        self.decl = json_frame["calling"]
        self.loc = (
            SourceLocation(json_frame["location"])
            if json_frame["location"] is not None
            else None
        )

    def _key(self):
        return self.lctx_id

    def __eq__(self, other):
        return self._key() == other._key()

    def __hash__(self):
        return hash(self._key())


# A group of deserialized Environment bindings that correspond to a specific
# location context.
class EnvironmentFrame:
    def __init__(self, json_frame):
        self.location_context = LocationContext(json_frame)
        self.bindings = collections.OrderedDict(
            [(EnvironmentBindingKey(b), b["value"]) for b in json_frame["items"]]
            if json_frame["items"] is not None
            else []
        )

    def diff_bindings(self, prev):
        return diff_dicts(self.bindings, prev.bindings)

    def is_different(self, prev):
        removed, added = self.diff_bindings(prev)
        return len(removed) != 0 or len(added) != 0


# A deserialized Environment. This class can also hold other entities that
# are similar to Environment, such as Objects Under Construction or
# Indices Of Elements Under Construction.
class GenericEnvironment:
    def __init__(self, json_e):
        self.frames = [EnvironmentFrame(f) for f in json_e]

    def diff_frames(self, prev):
        # TODO: It's difficult to display a good diff when frame numbers shift.
        if len(self.frames) != len(prev.frames):
            return None

        updated = []
        for i in range(len(self.frames)):
            f = self.frames[i]
            prev_f = prev.frames[i]
            if f.location_context == prev_f.location_context:
                if f.is_different(prev_f):
                    updated.append(i)
            else:
                # We have the whole frame replaced with another frame.
                # TODO: Produce a nice diff.
                return None

        # TODO: Add support for added/removed.
        return updated

    def is_different(self, prev):
        updated = self.diff_frames(prev)
        return updated is None or len(updated) > 0


# A single binding key in a deserialized RegionStore cluster.
class StoreBindingKey:
    def __init__(self, json_sk):
        self.kind = json_sk["kind"]
        self.offset = json_sk["offset"]

    def _key(self):
        return (self.kind, self.offset)

    def __eq__(self, other):
        return self._key() == other._key()

    def __hash__(self):
        return hash(self._key())


# A single cluster of the deserialized RegionStore.
class StoreCluster:
    def __init__(self, json_sc):
        self.base_region = json_sc["cluster"]
        self.bindings = collections.OrderedDict(
            [(StoreBindingKey(b), b["value"]) for b in json_sc["items"]]
        )

    def diff_bindings(self, prev):
        return diff_dicts(self.bindings, prev.bindings)

    def is_different(self, prev):
        removed, added = self.diff_bindings(prev)
        return len(removed) != 0 or len(added) != 0


# A deserialized RegionStore.
class Store:
    def __init__(self, json_s):
        self.ptr = json_s["pointer"]
        self.clusters = collections.OrderedDict(
            [(c["pointer"], StoreCluster(c)) for c in json_s["items"]]
        )

    def diff_clusters(self, prev):
        removed = [k for k in prev.clusters if k not in self.clusters]
        added = [k for k in self.clusters if k not in prev.clusters]
        updated = [
            k
            for k in prev.clusters
            if k in self.clusters and prev.clusters[k].is_different(self.clusters[k])
        ]
        return (removed, added, updated)

    def is_different(self, prev):
        removed, added, updated = self.diff_clusters(prev)
        return len(removed) != 0 or len(added) != 0 or len(updated) != 0


# Deserialized messages from a single checker in a single program state.
# Basically a list of raw strings.
class CheckerLines:
    def __init__(self, json_lines):
        self.lines = json_lines

    def diff_lines(self, prev):
        lines = difflib.ndiff(prev.lines, self.lines)
        return [l.strip() for l in lines if l.startswith("+") or l.startswith("-")]

    def is_different(self, prev):
        return len(self.diff_lines(prev)) > 0


# Deserialized messages of all checkers, separated by checker.
class CheckerMessages:
    def __init__(self, json_m):
        self.items = collections.OrderedDict(
            [(m["checker"], CheckerLines(m["messages"])) for m in json_m]
        )

    def diff_messages(self, prev):
        removed = [k for k in prev.items if k not in self.items]
        added = [k for k in self.items if k not in prev.items]
        updated = [
            k
            for k in prev.items
            if k in self.items and prev.items[k].is_different(self.items[k])
        ]
        return (removed, added, updated)

    def is_different(self, prev):
        removed, added, updated = self.diff_messages(prev)
        return len(removed) != 0 or len(added) != 0 or len(updated) != 0


# A deserialized program state.
class ProgramState:
    def __init__(self, state_id, json_ps):
        logging.debug("Adding ProgramState " + str(state_id))

        store_key = "store"
        env_key = "environment"
        constraints_key = "constraints"
        dyn_ty_key = "dynamic_types"
        ctor_key = "constructing_objects"
        ind_key = "index_of_element"
        init_loop_key = "pending_init_loops"
        dtor_key = "pending_destructors"
        msg_key = "checker_messages"

        if json_ps is None:
            json_ps = {
                store_key: None,
                env_key: None,
                constraints_key: None,
                dyn_ty_key: None,
                ctor_key: None,
                ind_key: None,
                init_loop_key: None,
                dtor_key: None,
                msg_key: None,
            }

        self.state_id = state_id

        self.store = (
            Store(json_ps[store_key]) if json_ps[store_key] is not None else None
        )

        self.environment = (
            GenericEnvironment(json_ps[env_key]["items"])
            if json_ps[env_key] is not None
            else None
        )

        self.constraints = (
            GenericMap([(c["symbol"], c["range"]) for c in json_ps[constraints_key]])
            if json_ps[constraints_key] is not None
            else None
        )

        self.dynamic_types = (
            GenericMap(
                [
                    (
                        t["region"],
                        "%s%s"
                        % (
                            t["dyn_type"],
                            " (or a sub-class)" if t["sub_classable"] else "",
                        ),
                    )
                    for t in json_ps[dyn_ty_key]
                ]
            )
            if json_ps[dyn_ty_key] is not None
            else None
        )

        self.checker_messages = (
            CheckerMessages(json_ps[msg_key]) if json_ps[msg_key] is not None else None
        )

        # State traits
        #
        # For traits we always check if a key exists because if a trait
        # has no imformation, nothing will be printed in the .dot file
        # we parse.

        self.constructing_objects = (
            GenericEnvironment(json_ps[ctor_key])
            if ctor_key in json_ps and json_ps[ctor_key] is not None
            else None
        )

        self.index_of_element = (
            GenericEnvironment(json_ps[ind_key])
            if ind_key in json_ps and json_ps[ind_key] is not None
            else None
        )

        self.pending_init_loops = (
            GenericEnvironment(json_ps[init_loop_key])
            if init_loop_key in json_ps and json_ps[init_loop_key] is not None
            else None
        )

        self.pending_destructors = (
            GenericEnvironment(json_ps[dtor_key])
            if dtor_key in json_ps and json_ps[dtor_key] is not None
            else None
        )


# A deserialized exploded graph node. Has a default constructor because it
# may be referenced as part of an edge before its contents are deserialized,
# and in this moment we already need a room for predecessors and successors.
class ExplodedNode:
    def __init__(self):
        self.predecessors = []
        self.successors = []

    def construct(self, node_id, json_node):
        logging.debug("Adding " + node_id)
        self.ptr = node_id[4:]
        self.points = [ProgramPoint(p) for p in json_node["program_points"]]
        self.node_id = self.points[-1].node_id
        self.state = ProgramState(
            json_node["state_id"],
            json_node["program_state"]
            if json_node["program_state"] is not None
            else None,
        )

        assert self.node_name() == node_id

    def node_name(self):
        return "Node" + self.ptr


# A deserialized ExplodedGraph. Constructed by consuming a .dot file
# line-by-line.
class ExplodedGraph:
    # Parse .dot files with regular expressions.
    node_re = re.compile(
        '^(Node0x[0-9a-f]*) \\[shape=record,.*label="{(.*)\\\\l}"\\];$'
    )
    edge_re = re.compile("^(Node0x[0-9a-f]*) -> (Node0x[0-9a-f]*);$")

    def __init__(self):
        self.nodes = collections.defaultdict(ExplodedNode)
        self.root_id = None
        self.incomplete_line = ""

    def add_raw_line(self, raw_line):
        if raw_line.startswith("//"):
            return

        # Allow line breaks by waiting for ';'. This is not valid in
        # a .dot file, but it is useful for writing tests.
        if len(raw_line) > 0 and raw_line[-1] != ";":
            self.incomplete_line += raw_line
            return
        raw_line = self.incomplete_line + raw_line
        self.incomplete_line = ""

        # Apply regexps one by one to see if it's a node or an edge
        # and extract contents if necessary.
        logging.debug("Line: " + raw_line)
        result = self.edge_re.match(raw_line)
        if result is not None:
            logging.debug("Classified as edge line.")
            pred = result.group(1)
            succ = result.group(2)
            self.nodes[pred].successors.append(succ)
            self.nodes[succ].predecessors.append(pred)
            return
        result = self.node_re.match(raw_line)
        if result is not None:
            logging.debug("Classified as node line.")
            node_id = result.group(1)
            if len(self.nodes) == 0:
                self.root_id = node_id
            # Note: when writing tests you don't need to escape everything,
            # even though in a valid dot file everything is escaped.
            node_label = (
                result.group(2)
                .replace("&nbsp;", "")
                .replace('\\"', '"')
                .replace("\\{", "{")
                .replace("\\}", "}")
                .replace("\\\\", "\\")
                .replace("\\|", "|")
                .replace("\\<", "\\\\<")
                .replace("\\>", "\\\\>")
                .rstrip(",")
            )
            # Handle `\l` separately because a string literal can be in code
            # like "string\\literal" with the `\l` inside.
            # Also on Windows macros __FILE__ produces specific delimiters `\`
            # and a directory or file may starts with the letter `l`.
            # Find all `\l` (like `,\l`, `}\l`, `[\l`) except `\\l`,
            # because the literal as a rule contains multiple `\` before `\l`.
            node_label = re.sub(r"(?<!\\)\\l", "", node_label)
            logging.debug(node_label)
            json_node = json.loads(node_label)
            self.nodes[node_id].construct(node_id, json_node)
            return
        logging.debug("Skipping.")


# ===-----------------------------------------------------------------------===#
# Visitors traverse a deserialized ExplodedGraph and do different things
# with every node and edge.
# ===-----------------------------------------------------------------------===#


# A visitor that dumps the ExplodedGraph into a DOT file with fancy HTML-based
# syntax highlighing.
class DotDumpVisitor:
    def __init__(
        self, do_diffs, dark_mode, gray_mode, topo_mode, dump_html_only, dump_dot_only
    ):
        assert not (dump_html_only and dump_dot_only), (
            "Option dump_html_only and dump_dot_only are conflict, "
            "they cannot be true at the same time."
        )

        self._do_diffs = do_diffs
        self._dark_mode = dark_mode
        self._gray_mode = gray_mode
        self._topo_mode = topo_mode
        self._dump_html_only = dump_html_only
        self._dump_dot_only = dump_dot_only
        self._output = []

    def _dump_raw(self, s):
        if self._dump_dot_only:
            print(s, end="")
        else:
            self._output.append(s)

    def output(self):
        assert not self._dump_dot_only
        return "".join(self._output)

    def _dump(self, s):
        s = (
            s.replace("&", "&amp;")
            .replace("{", "\\{")
            .replace("}", "\\}")
            .replace("\\<", "&lt;")
            .replace("\\>", "&gt;")
            .replace("|", "\\|")
        )
        s = re.sub(r"(?<!\\)\\l", "<br />", s)
        if self._gray_mode:
            s = re.sub(r'<font color="[a-z0-9]*">', "", s)
            s = re.sub(r"</font>", "", s)
        self._dump_raw(s)

    @staticmethod
    def _diff_plus_minus(is_added):
        if is_added is None:
            return ""
        if is_added:
            return '<font color="forestgreen">+</font>'
        return '<font color="red">-</font>'

    @staticmethod
    def _short_pretty(s):
        if s is None:
            return None
        if len(s) < 20:
            return s
        left = s.find("{")
        right = s.rfind("}")
        if left == -1 or right == -1 or left >= right:
            return s
        candidate = s[0 : left + 1] + " ... " + s[right:]
        if len(candidate) >= len(s):
            return s
        return candidate

    @staticmethod
    def _make_sloc(loc):
        if loc is None:
            return "<i>Invalid Source Location</i>"

        def make_plain_loc(loc):
            return "%s:<b>%s</b>:<b>%s</b>" % (loc.filename, loc.line, loc.col)

        if loc.is_macro():
            return '%s <font color="royalblue1">' "(<i>spelling at </i> %s)</font>" % (
                make_plain_loc(loc),
                make_plain_loc(loc.spelling),
            )

        return make_plain_loc(loc)

    def visit_begin_graph(self, graph):
        self._graph = graph
        self._dump_raw('digraph "ExplodedGraph" {\n')
        if self._dark_mode:
            self._dump_raw('bgcolor="gray10";\n')
        self._dump_raw('label="";\n')

    def visit_program_point(self, p):
        if p.kind in ["Edge", "BlockEntrance", "BlockExit"]:
            color = "gold3"
        elif p.kind in ["PreStmtPurgeDeadSymbols", "PostStmtPurgeDeadSymbols"]:
            color = "red"
        elif p.kind in ["CallEnter", "CallExitBegin", "CallExitEnd"]:
            color = "dodgerblue" if self._dark_mode else "blue"
        elif p.kind in ["Statement"]:
            color = "cyan4"
        else:
            color = "forestgreen"

        self._dump('<tr><td align="left">%s.</td>' % p.node_id)

        if p.kind == "Statement":
            # This avoids pretty-printing huge statements such as CompoundStmt.
            # Such statements show up only at [Pre|Post]StmtPurgeDeadSymbols
            skip_pretty = "PurgeDeadSymbols" in p.stmt_point_kind
            stmt_color = "cyan3"
            self._dump(
                '<td align="left" width="0">%s:</td>'
                '<td align="left" width="0"><font color="%s">'
                "%s</font> </td>"
                '<td align="left"><i>S%s</i></td>'
                '<td align="left"><font color="%s">%s</font></td>'
                '<td align="left">%s</td></tr>'
                % (
                    self._make_sloc(p.loc),
                    color,
                    "%s (%s)" % (p.stmt_kind, p.cast_kind)
                    if p.cast_kind is not None
                    else p.stmt_kind,
                    p.stmt_id,
                    stmt_color,
                    p.stmt_point_kind,
                    self._short_pretty(p.pretty) if not skip_pretty else "",
                )
            )
        elif p.kind == "Edge":
            self._dump(
                '<td width="0"></td>'
                '<td align="left" width="0">'
                '<font color="%s">%s</font></td><td align="left">'
                "[B%d] -\\> [B%d]</td></tr>" % (color, "BlockEdge", p.src_id, p.dst_id)
            )
        elif p.kind == "BlockEntrance":
            self._dump(
                '<td width="0"></td>'
                '<td align="left" width="0">'
                '<font color="%s">%s</font></td>'
                '<td align="left">[B%d]</td></tr>' % (color, p.kind, p.block_id)
            )
        else:
            # TODO: Print more stuff for other kinds of points.
            self._dump(
                '<td width="0"></td>'
                '<td align="left" width="0" colspan="2">'
                '<font color="%s">%s</font></td></tr>' % (color, p.kind)
            )

        if p.tag is not None:
            self._dump(
                '<tr><td width="0"></td><td width="0"></td>'
                '<td colspan="3" align="left">'
                '<b>Tag: </b> <font color="crimson">'
                "%s</font></td></tr>" % p.tag
            )

        if p.has_report:
            self._dump(
                '<tr><td width="0"></td><td width="0"></td>'
                '<td colspan="3" align="left">'
                '<font color="red"><b>Bug Report Attached'
                "</b></font></td></tr>"
            )
        if p.is_sink:
            self._dump(
                '<tr><td width="0"></td><td width="0"></td>'
                '<td colspan="3" align="left">'
                '<font color="cornflowerblue"><b>Sink Node'
                "</b></font></td></tr>"
            )

    def visit_environment(self, e, prev_e=None):
        self._dump('<table border="0">')

        def dump_location_context(lc, is_added=None):
            self._dump(
                "<tr><td>%s</td>"
                '<td align="left"><b>%s</b></td>'
                '<td align="left" colspan="2">'
                '<font color="gray60">%s </font>'
                "%s</td></tr>"
                % (
                    self._diff_plus_minus(is_added),
                    lc.caption,
                    lc.decl,
                    ("(%s)" % self._make_sloc(lc.loc)) if lc.loc is not None else "",
                )
            )

        def dump_binding(f, b, is_added=None):
            self._dump(
                "<tr><td>%s</td>"
                '<td align="left"><i>S%s</i></td>'
                "%s"
                '<td align="left">%s</td>'
                '<td align="left">%s</td></tr>'
                % (
                    self._diff_plus_minus(is_added),
                    b.stmt_id,
                    '<td align="left"><font color="%s"><i>'
                    "%s</i></font></td>"
                    % (
                        "lavender" if self._dark_mode else "darkgreen",
                        ("(%s)" % b.kind) if b.kind is not None else " ",
                    ),
                    self._short_pretty(b.pretty),
                    f.bindings[b],
                )
            )

        frames_updated = e.diff_frames(prev_e) if prev_e is not None else None
        if frames_updated:
            for i in frames_updated:
                f = e.frames[i]
                prev_f = prev_e.frames[i]
                dump_location_context(f.location_context)
                bindings_removed, bindings_added = f.diff_bindings(prev_f)
                for b in bindings_removed:
                    dump_binding(prev_f, b, False)
                for b in bindings_added:
                    dump_binding(f, b, True)
        else:
            for f in e.frames:
                dump_location_context(f.location_context)
                for b in f.bindings:
                    dump_binding(f, b)

        self._dump("</table>")

    def visit_environment_in_state(self, selector, title, s, prev_s=None):
        e = getattr(s, selector)
        prev_e = getattr(prev_s, selector) if prev_s is not None else None
        if e is None and prev_e is None:
            return

        self._dump('<hr /><tr><td align="left"><b>%s: </b>' % title)
        if e is None:
            self._dump("<i> Nothing!</i>")
        else:
            if prev_e is not None:
                if e.is_different(prev_e):
                    self._dump('</td></tr><tr><td align="left">')
                    self.visit_environment(e, prev_e)
                else:
                    self._dump("<i> No changes!</i>")
            else:
                self._dump('</td></tr><tr><td align="left">')
                self.visit_environment(e)

        self._dump("</td></tr>")

    def visit_store(self, s, prev_s=None):
        self._dump('<table border="0">')

        def dump_binding(s, c, b, is_added=None):
            self._dump(
                "<tr><td>%s</td>"
                '<td align="left">%s</td>'
                '<td align="left">%s</td>'
                '<td align="left">%s</td>'
                '<td align="left">%s</td></tr>'
                % (
                    self._diff_plus_minus(is_added),
                    s.clusters[c].base_region,
                    b.offset,
                    "(<i>Default</i>)" if b.kind == "Default" else "",
                    s.clusters[c].bindings[b],
                )
            )

        if prev_s is not None:
            clusters_removed, clusters_added, clusters_updated = s.diff_clusters(prev_s)
            for c in clusters_removed:
                for b in prev_s.clusters[c].bindings:
                    dump_binding(prev_s, c, b, False)
            for c in clusters_updated:
                bindings_removed, bindings_added = s.clusters[c].diff_bindings(
                    prev_s.clusters[c]
                )
                for b in bindings_removed:
                    dump_binding(prev_s, c, b, False)
                for b in bindings_added:
                    dump_binding(s, c, b, True)
            for c in clusters_added:
                for b in s.clusters[c].bindings:
                    dump_binding(s, c, b, True)
        else:
            for c in s.clusters:
                for b in s.clusters[c].bindings:
                    dump_binding(s, c, b)

        self._dump("</table>")

    def visit_store_in_state(self, s, prev_s=None):
        st = s.store
        prev_st = prev_s.store if prev_s is not None else None
        if st is None and prev_st is None:
            return

        self._dump('<hr /><tr><td align="left"><b>Store: </b>')
        if st is None:
            self._dump("<i> Nothing!</i>")
        else:
            if self._dark_mode:
                self._dump(' <font color="gray30">(%s)</font>' % st.ptr)
            else:
                self._dump(' <font color="gray">(%s)</font>' % st.ptr)
            if prev_st is not None:
                if s.store.is_different(prev_st):
                    self._dump('</td></tr><tr><td align="left">')
                    self.visit_store(st, prev_st)
                else:
                    self._dump("<i> No changes!</i>")
            else:
                self._dump('</td></tr><tr><td align="left">')
                self.visit_store(st)
        self._dump("</td></tr>")

    def visit_generic_map(self, m, prev_m=None):
        self._dump('<table border="0">')

        def dump_pair(m, k, is_added=None):
            self._dump(
                "<tr><td>%s</td>"
                '<td align="left">%s</td>'
                '<td align="left">%s</td></tr>'
                % (self._diff_plus_minus(is_added), k, m.generic_map[k])
            )

        if prev_m is not None:
            removed, added = m.diff(prev_m)
            for k in removed:
                dump_pair(prev_m, k, False)
            for k in added:
                dump_pair(m, k, True)
        else:
            for k in m.generic_map:
                dump_pair(m, k, None)

        self._dump("</table>")

    def visit_generic_map_in_state(self, selector, title, s, prev_s=None):
        m = getattr(s, selector)
        prev_m = getattr(prev_s, selector) if prev_s is not None else None
        if m is None and prev_m is None:
            return

        self._dump("<hr />")
        self._dump('<tr><td align="left">' "<b>%s: </b>" % title)
        if m is None:
            self._dump("<i> Nothing!</i>")
        else:
            if prev_m is not None:
                if m.is_different(prev_m):
                    self._dump('</td></tr><tr><td align="left">')
                    self.visit_generic_map(m, prev_m)
                else:
                    self._dump("<i> No changes!</i>")
            else:
                self._dump('</td></tr><tr><td align="left">')
                self.visit_generic_map(m)

        self._dump("</td></tr>")

    def visit_checker_messages(self, m, prev_m=None):
        self._dump('<table border="0">')

        def dump_line(l, is_added=None):
            self._dump(
                "<tr><td>%s</td>"
                '<td align="left">%s</td></tr>' % (self._diff_plus_minus(is_added), l)
            )

        def dump_chk(chk, is_added=None):
            dump_line("<i>%s</i>:" % chk, is_added)

        if prev_m is not None:
            removed, added, updated = m.diff_messages(prev_m)
            for chk in removed:
                dump_chk(chk, False)
                for l in prev_m.items[chk].lines:
                    dump_line(l, False)
            for chk in updated:
                dump_chk(chk)
                for l in m.items[chk].diff_lines(prev_m.items[chk]):
                    dump_line(l[1:], l.startswith("+"))
            for chk in added:
                dump_chk(chk, True)
                for l in m.items[chk].lines:
                    dump_line(l, True)
        else:
            for chk in m.items:
                dump_chk(chk)
                for l in m.items[chk].lines:
                    dump_line(l)

        self._dump("</table>")

    def visit_checker_messages_in_state(self, s, prev_s=None):
        m = s.checker_messages
        prev_m = prev_s.checker_messages if prev_s is not None else None
        if m is None and prev_m is None:
            return

        self._dump("<hr />")
        self._dump('<tr><td align="left">' "<b>Checker State: </b>")
        if m is None:
            self._dump("<i> Nothing!</i>")
        else:
            if prev_m is not None:
                if m.is_different(prev_m):
                    self._dump('</td></tr><tr><td align="left">')
                    self.visit_checker_messages(m, prev_m)
                else:
                    self._dump("<i> No changes!</i>")
            else:
                self._dump('</td></tr><tr><td align="left">')
                self.visit_checker_messages(m)

        self._dump("</td></tr>")

    def visit_state(self, s, prev_s):
        self.visit_store_in_state(s, prev_s)
        self.visit_environment_in_state("environment", "Expressions", s, prev_s)
        self.visit_generic_map_in_state("constraints", "Ranges", s, prev_s)
        self.visit_generic_map_in_state("dynamic_types", "Dynamic Types", s, prev_s)
        self.visit_environment_in_state(
            "constructing_objects", "Objects Under Construction", s, prev_s
        )
        self.visit_environment_in_state(
            "index_of_element", "Indices Of Elements Under Construction", s, prev_s
        )
        self.visit_environment_in_state(
            "pending_init_loops", "Pending Array Init Loop Expressions", s, prev_s
        )
        self.visit_environment_in_state(
            "pending_destructors", "Indices of Elements Under Destruction", s, prev_s
        )
        self.visit_checker_messages_in_state(s, prev_s)

    def visit_node(self, node):
        self._dump("%s [shape=record," % (node.node_name()))
        if self._dark_mode:
            self._dump('color="white",fontcolor="gray80",')
        self._dump('label=<<table border="0">')

        self._dump(
            '<tr><td bgcolor="%s"><b>State %s</b></td></tr>'
            % (
                "gray20" if self._dark_mode else "gray70",
                node.state.state_id if node.state is not None else "Unspecified",
            )
        )
        if not self._topo_mode:
            self._dump('<tr><td align="left" width="0">')
            if len(node.points) > 1:
                self._dump("<b>Program points:</b></td></tr>")
            else:
                self._dump("<b>Program point:</b></td></tr>")
        self._dump(
            '<tr><td align="left" width="0">'
            '<table border="0" align="left" width="0">'
        )
        for p in node.points:
            self.visit_program_point(p)
        self._dump("</table></td></tr>")

        if node.state is not None and not self._topo_mode:
            prev_s = None
            # Do diffs only when we have a unique predecessor.
            # Don't do diffs on the leaf nodes because they're
            # the important ones.
            if (
                self._do_diffs
                and len(node.predecessors) == 1
                and len(node.successors) > 0
            ):
                prev_s = self._graph.nodes[node.predecessors[0]].state
            self.visit_state(node.state, prev_s)
        self._dump_raw("</table>>];\n")

    def visit_edge(self, pred, succ):
        self._dump_raw(
            "%s -> %s%s;\n"
            % (
                pred.node_name(),
                succ.node_name(),
                ' [color="white"]' if self._dark_mode else "",
            )
        )

    def visit_end_of_graph(self):
        self._dump_raw("}\n")

        if not self._dump_dot_only:
            import sys
            import tempfile

            def write_temp_file(suffix, prefix, data):
                fd, filename = tempfile.mkstemp(suffix, prefix, ".", True)
                print('Writing "%s"...' % filename)
                with os.fdopen(fd, "w") as fp:
                    fp.write(data)
                print("Done! Please remember to remove the file.")
                return filename

            try:
                import graphviz
            except ImportError:
                # The fallback behavior if graphviz is not installed!
                print("Python graphviz not found. Please invoke")
                print("  $ pip install graphviz")
                print("in order to enable automatic conversion to HTML.")
                print()
                print("You may also convert DOT to SVG manually via")
                print("  $ dot -Tsvg input.dot -o output.svg")
                print()
                write_temp_file(".dot", "egraph-", self.output())
                return

            svg = graphviz.pipe("dot", "svg", self.output().encode()).decode()

            filename = write_temp_file(
                ".html",
                "egraph-",
                '<html><body bgcolor="%s">%s</body></html>'
                % ("#1a1a1a" if self._dark_mode else "white", svg),
            )
            if self._dump_html_only:
                return
            if sys.platform == "win32":
                os.startfile(filename)
            elif sys.platform == "darwin":
                os.system('open "%s"' % filename)
            else:
                os.system('xdg-open "%s"' % filename)


# ===-----------------------------------------------------------------------===#
# Explorers know how to traverse the ExplodedGraph in a certain order.
# They would invoke a Visitor on every node or edge they encounter.
# ===-----------------------------------------------------------------------===#


# BasicExplorer explores the whole graph in no particular order.
class BasicExplorer:
    def explore(self, graph, visitor):
        visitor.visit_begin_graph(graph)
        for node in sorted(graph.nodes):
            logging.debug("Visiting " + node)
            visitor.visit_node(graph.nodes[node])
            for succ in sorted(graph.nodes[node].successors):
                logging.debug("Visiting edge: %s -> %s " % (node, succ))
                visitor.visit_edge(graph.nodes[node], graph.nodes[succ])
        visitor.visit_end_of_graph()


# ===-----------------------------------------------------------------------===#
# Trimmers cut out parts of the ExplodedGraph so that to focus on other parts.
# Trimmers can be combined together by applying them sequentially.
# ===-----------------------------------------------------------------------===#


# SinglePathTrimmer keeps only a single path - the leftmost path from the root.
# Useful when the trimmed graph is still too large.
class SinglePathTrimmer:
    def trim(self, graph):
        visited_nodes = set()
        node_id = graph.root_id
        while True:
            visited_nodes.add(node_id)
            node = graph.nodes[node_id]
            if len(node.successors) > 0:
                succ_id = node.successors[0]
                succ = graph.nodes[succ_id]
                node.successors = [succ_id]
                succ.predecessors = [node_id]
                if succ_id in visited_nodes:
                    break
                node_id = succ_id
            else:
                break
        graph.nodes = {node_id: graph.nodes[node_id] for node_id in visited_nodes}


# TargetedTrimmer keeps paths that lead to specific nodes and discards all
# other paths. Useful when you cannot use -trim-egraph (e.g. when debugging
# a crash).
class TargetedTrimmer:
    def __init__(self, target_nodes):
        self._target_nodes = target_nodes

    @staticmethod
    def parse_target_node(node, graph):
        if node.startswith("0x"):
            ret = "Node" + node
            assert ret in graph.nodes
            return ret
        else:
            for other_id in graph.nodes:
                other = graph.nodes[other_id]
                if other.node_id == int(node):
                    return other_id

    @staticmethod
    def parse_target_nodes(target_nodes, graph):
        return [
            TargetedTrimmer.parse_target_node(node, graph)
            for node in target_nodes.split(",")
        ]

    def trim(self, graph):
        queue = self._target_nodes
        visited_nodes = set()

        while len(queue) > 0:
            node_id = queue.pop()
            visited_nodes.add(node_id)
            node = graph.nodes[node_id]
            for pred_id in node.predecessors:
                if pred_id not in visited_nodes:
                    queue.append(pred_id)
        graph.nodes = {node_id: graph.nodes[node_id] for node_id in visited_nodes}
        for node_id in graph.nodes:
            node = graph.nodes[node_id]
            node.successors = [
                succ_id for succ_id in node.successors if succ_id in visited_nodes
            ]
            node.predecessors = [
                succ_id for succ_id in node.predecessors if succ_id in visited_nodes
            ]


# ===-----------------------------------------------------------------------===#
# The entry point to the script.
# ===-----------------------------------------------------------------------===#


def main():
    parser = argparse.ArgumentParser(
        description="Display and manipulate Exploded Graph dumps."
    )
    parser.add_argument(
        "filename", type=str, help="the .dot file produced by the Static Analyzer"
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_const",
        dest="loglevel",
        const=logging.DEBUG,
        default=logging.WARNING,
        help="enable info prints",
    )
    parser.add_argument(
        "-d",
        "--diff",
        action="store_const",
        dest="diff",
        const=True,
        default=False,
        help="display differences between states",
    )
    parser.add_argument(
        "-t",
        "--topology",
        action="store_const",
        dest="topology",
        const=True,
        default=False,
        help="only display program points, omit states",
    )
    parser.add_argument(
        "-s",
        "--single-path",
        action="store_const",
        dest="single_path",
        const=True,
        default=False,
        help="only display the leftmost path in the graph "
        "(useful for trimmed graphs that still "
        "branch too much)",
    )
    parser.add_argument(
        "--to",
        type=str,
        default=None,
        help="only display execution paths from the root "
        "to the given comma-separated list of nodes "
        "identified by a pointer or a stable ID; "
        "compatible with --single-path",
    )
    parser.add_argument(
        "--dark",
        action="store_const",
        dest="dark",
        const=True,
        default=False,
        help="dark mode",
    )
    parser.add_argument(
        "--gray",
        action="store_const",
        dest="gray",
        const=True,
        default=False,
        help="black-and-white mode",
    )
    dump_conflict = parser.add_mutually_exclusive_group()
    dump_conflict.add_argument(
        "--dump-html-only",
        action="store_const",
        dest="dump_html_only",
        const=True,
        default=False,
        help="dump the rewritten egraph to a temporary HTML file, "
        "but do not open it immediately as by default",
    )
    dump_conflict.add_argument(
        "--dump-dot-only",
        action="store_const",
        dest="dump_dot_only",
        const=True,
        default=False,
        help="instead of writing an HTML file and immediately "
        "displaying it, dump the rewritten dot file "
        "to stdout",
    )
    args = parser.parse_args()
    logging.basicConfig(level=args.loglevel)

    graph = ExplodedGraph()
    with open(args.filename) as fd:
        for raw_line in fd:
            raw_line = raw_line.strip()
            graph.add_raw_line(raw_line)

    trimmers = []
    if args.to is not None:
        trimmers.append(
            TargetedTrimmer(TargetedTrimmer.parse_target_nodes(args.to, graph))
        )
    if args.single_path:
        trimmers.append(SinglePathTrimmer())

    explorer = BasicExplorer()

    visitor = DotDumpVisitor(
        args.diff,
        args.dark,
        args.gray,
        args.topology,
        args.dump_html_only,
        args.dump_dot_only,
    )

    for trimmer in trimmers:
        trimmer.trim(graph)

    explorer.explore(graph, visitor)


if __name__ == "__main__":
    main()
