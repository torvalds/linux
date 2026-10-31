#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""treport.py - perf report like tool written using textual."""
from abc import ABC, abstractmethod
from typing import Callable, Dict, List, Optional, Set
import argparse
import os
import sys
import threading
import time
import perf
from rich.markup import escape
from rich.segment import Segment
from rich.style import Style
from textual import events, work
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.color import Color
from textual.scroll_view import ScrollView
from textual.strip import Strip
from textual.widgets import Footer, Header, TabbedContent, TabPane, Tree
from textual.widgets.tree import TreeNode

def make_fixed_length_string(s: str, length: int, pad_char=' '):
    """Make the string s a fixed length.

    Increases or decreases the length of s to be length. If the length is
    increased then pad_char is inserted on the right.
    """
    return s[:length] if len(s) > length else s.ljust(length, pad_char)


class FlameVisitor(ABC):
    """Parent for visitor used by ProfileNode.flame_walk"""
    @abstractmethod
    def visit(self, node: Optional["ProfileNode"], width: int) -> None:
        """Visit a profile node width the specified flame graph width.

        Args:
            node: The `ProfileNode` for the current segment. This may be `None`
                to represent a gap or an unknown portion of the stack.
            width: The calculated width of the flame graph rectangle for this
                node, which is proportional to its sample count.
        """


class ProfileNode:
    """Represents a single node in a call stack tree.

    Generally a ProfileNode corresponds to a symbol in a call stack.
    The root is special, its children are events and the events
    children are process names. After the process name come the
    samples.

    Attributes:
        name (str): The name of the function, process or event.
        value (int): The sample count for this node including counts from its
                     children.
        parent (ProfileNode): The parent of this node, this node belongs to its
                              children.
        children (Dict[str, ProfileNode]): A dictionary of child nodes, keyed by
                                           their names.
    """
    def __init__(self, name: str, parent: Optional["ProfileNode"]):
        """Initializes a ProfileNode."""
        self.name = name
        self.value: int = 0
        self.parent = parent if parent else self
        self.children: Dict[str, ProfileNode] = {}

    def find_or_create_node(self, name: str) -> "ProfileNode":
        """Finds a child node by name or creates it if it doesn't exist."""
        if name in self.children:
            return self.children[name]
        child = ProfileNode(name, self)
        self.children[name] = child
        return child

    def depth(self) -> int:
        """The maximum depth of the call stack tree from this node down."""
        if not self.children:
            return 1
        return max(child.depth() for child in self.children.values()) + 1

    def process_event(self, sample, comm: str) -> None:
        """Processes a single profiling event to update the call stack tree.

        Args:
            sample: a single profiling sample.
            comm: the command name of the sampled thread.
        """
        period = sample.sample_period
        self.value += period

        node = self.find_or_create_node(comm)
        node.value += period

        if sample.callchain:
            for entry in reversed(sample.callchain):
                name = entry.symbol
                if not name or name == "[unknown]":
                    name = entry.dso or "unknown"
                    if entry.ip:
                        name += f" 0x{entry.ip:x}"
                node = node.find_or_create_node(name)
                node.value += period
        else:
            name = sample.symbol
            if not name or name == "[unknown]":
                name = sample.dso or "unknown"
                if sample.sample_ip:
                    name += f" 0x{sample.sample_ip:x}"
            node = node.find_or_create_node(name)
            node.value += period

    def sorted_children(self) -> List["ProfileNode"]:
        """The children, largest value first.

        The root's children are events whose values aren't comparable, each is
        100% of itself, so they are kept in the order they were first seen
        rather than reordering as they load.
        """
        if self.parent is self:
            return list(self.children.values())
        return sorted(self.children.values(), key=lambda pnode: pnode.value, reverse=True)

    def tree_label(self, root_value: int) -> str:
        """Label for the node in a Tree, with a percentage of root_value.

        The percentage is highlighted with reversed colors.
        """
        if root_value == 0:
            return escape(self.name)
        return f"{escape(self.name)} [r]{self.value / root_value * 100:.3g}%[/]"

    def largest_child(self) -> "ProfileNode":
        """Finds the child with the highest value (sample count)."""
        if self.children:
            return self.sorted_children()[0]
        return self

    def child_after(self, sought: "ProfileNode") -> "ProfileNode":
        """Finds the next sibling after the given node, sorted by value."""
        found = False
        for child in self.sorted_children():
            if child == sought:
                found = True
            elif found:
                return child
        return sought

    def child_before(self, sought: "ProfileNode") -> "ProfileNode":
        """Finds the previous sibling before the given node, sorted by value."""
        last = None
        for child in self.sorted_children():
            if child == sought:
                return last if last else sought
            last = child
        return sought

    def has_parent(self, parent: "ProfileNode") -> bool:
        """Checks if the parent node is an ancestor of this node."""
        p = self.parent
        while True:
            if p == parent:
                return True
            new_p = p.parent
            if new_p == p:
                break
            p = new_p
        return False

    def has_child(self, sought: "ProfileNode") -> bool:
        """Checks if the sought node is a descendant of this node."""
        return sought.has_parent(self)

    def flame_walk(self, wanted_strip: int, cur_strip: int, parent_width: int,
                   selected: "ProfileNode", visitor: FlameVisitor) -> None:
        """Recursively walks the tree to visit a single flame graph row.

        This method calculates the proportional width for each child
        based on its value (sample count) relative to its parent. It
        then invokes a `visitor` to process each segment of the flame
        graph row.

        Args:
            wanted_strip (int): The target depth (Y-axis) of the flame graph row
                                to generate.
            cur_strip (int): The current depth of the traversal.
            parent_width (int): The width of the parent of this node.
            selected (ProfileNode): The currently selected node in the UI, used
                                    to adjust rendering to highlight the
                                    selected path.
            visitor (FlameVisitor): A visitor object whose `visit` method is
                                    called for each segment of the flame graph
                                    row.
        """
        if parent_width == 0:
            return

        parent_selected = selected == self or self.has_parent(selected)
        child_selected = not parent_selected and self.has_child(selected)
        if not parent_selected and not child_selected:
            # Branches of the tree with no node selected aren't drawn.
            return

        # left_over is used to check for a gap after the children due
        # to samples being in the parent.
        left_over = parent_width
        for child in self.sorted_children():
            if parent_selected:
                if self.value:
                    desired_width = int((parent_width * child.value) / self.value)
                else:
                    desired_width = parent_width // len(self.children)
                if desired_width == 0:
                    # Nothing can be drawn for this node or later smaller children.
                    break
            elif child == selected or child.has_child(selected):
                desired_width = parent_width
            else:
                # A sibling or its child are selected, but not this branch.
                continue

            # Either visit the wanted_strip or recurse to the next level.
            if wanted_strip == cur_strip:
                visitor.visit(child, desired_width)
            else:
                child.flame_walk(wanted_strip, cur_strip + 1, desired_width,
                                 selected, visitor)
            left_over -= desired_width
            if left_over == 0:
                # No space left to draw in.
                break

        # Always visit the left_over regardless of the wanted_strip as there
        # may be additional gap added to a line by a parent.
        if left_over:
            visitor.visit(None, left_over)

    def make_flame_strip(self, wanted_strip: int, parent_width: int,
                         cursor: "ProfileNode", selected: "ProfileNode",
                         theme_variables: Dict[str, str]) -> Strip:
        """Creates a renderable 'Strip' for a single row of a flame graph.

        This method orchestrates the `flame_walk` traversal with a specialized
        visitor to generate a list of segments. The segments are used by a`Strip`
        object for rendering in the terminal.

        Args:
            wanted_strip (int): The target depth (Y-axis) of the flame graph row.
            parent_width (int): The total width (in characters) of the display
                                area.
            cursor (ProfileNode): The node currently under the cursor, for
                                  highlighting.
            selected (ProfileNode): The node that is actively selected.
            theme_variables(Dict): Values of colors for the textual theme.

        Returns:
            Strip: A renderable strip of segments for the specified row.
        """
        primary = Color.parse(theme_variables["primary"])
        secondary = Color.parse(theme_variables["secondary"])
        surface = Color.parse(theme_variables["surface"])
        def luminance(color: Color) -> float:
            """Computes the luminance of a color from the rgb"""
            return color.r * 0.299 + color.g * 0.587 + color.b * 0.114

        # Set of styles for different flamegraph segments, the styles are
        # cycled through to provide contrast.
        normal_styles = []
        for x in range(0, 125, 25):
            fgcolor = secondary.blend(primary, x/100)
            if luminance(fgcolor) > luminance(surface):
                bgcolor = surface.lighten(0.05+x/500)
            else:
                bgcolor = surface.darken(0.05+x/500)
            normal_styles.append(Style(color=fgcolor.rich_color,
                                       bgcolor=bgcolor.rich_color))

        # Style for the selected flame graph node.
        accent = Color.parse(theme_variables["accent"])
        accent_muted = Color.parse(theme_variables["accent-muted"])
        cursor_style = Style(color=accent.rich_color, bgcolor=accent_muted.rich_color)

        class StripVisitor(FlameVisitor):
            """Visitor creating textual flame graph segments.

            Attributes:
                segments (list): The textual segments that will be placed in a
                                 `Strip`.
                gap_width (int): The width of any outstanding gap between the
                                 last and next node.
                ctr (int): Used to adjust the flame graph segment's color.
            """
            def __init__(self):
                self.segments = []
                self.gap_width = 0
                self.ctr = wanted_strip

            def visit(self, node: Optional[ProfileNode], width: int) -> None:
                if node:
                    if self.gap_width > 0:
                        self.segments.append(Segment(
                            make_fixed_length_string(" ", self.gap_width)))
                        self.gap_width = 0
                    style = cursor_style
                    if node != cursor:
                        style = normal_styles[self.ctr % len(normal_styles)]
                    self.segments.append(Segment(
                        make_fixed_length_string(node.name, width), style))
                else:
                    self.gap_width += width
                self.ctr += 1

        visitor = StripVisitor()
        self.flame_walk(wanted_strip, 0, parent_width, selected, visitor)
        return Strip(visitor.segments) if visitor.segments else Strip.blank(parent_width)

    def find_node(self, sought_x: int, sought_y: int, parent_width: int,
                  selected: "ProfileNode") -> "ProfileNode":
        """Finds the ProfileNode corresponding to specific X, Y coordinates.

        This translates a mouse click on a flame graph back to the
        `ProfileNode` that it represents.

        Args:
            sought_x (int): The X coordinate (character column).
            sought_y (int): The Y coordinate (row or depth).
            parent_width (int): The total width of the display area.
            selected (ProfileNode): The currently selected node, which affects
                                    layout.

        Returns:
            Optional[ProfileNode]: The node found at the coordinates, or None.

        """
        class FindVisitor(FlameVisitor):
            """Visitor locating a `ProfileNode`.

            Attributes:
                x (int): offset within line.
                found (Optional[ProfileNode]): located node
                gap_width (int): The width of any outstanding gap between the
                                 last and next node.
                ctr (int): Used to adjust the flame graph segment's color.
            """
            def __init__(self):
                self.x = 0
                self.found = None

            def visit(self, node: Optional[ProfileNode], width: int) -> None:
                if self.x <= sought_x < self.x + width:
                    self.found = node
                self.x += width

        visitor = FindVisitor()
        self.flame_walk(sought_y, 0, parent_width, selected, visitor)
        return visitor.found


class FlameGraph(ScrollView):
    """A scrollable widget to display a flame graph from a profile.

    Attributes:
        root (ProfileNode): Root of the profile tree.
        cursor (ProfileNode): Currently highlighted cursor node.
        selected (ProfileNode): The currently selected node for zooming.
    """

    # Define key bindings for navigating the flame graph.
    # Allows movement with vim-style keys (h,j,k,l) and arrow keys.
    BINDINGS = [
        Binding("j,down", "move_down", "Down", key_display="↓",
                tooltip="Move cursor down to largest child"),
        Binding("k,up", "move_up", "Up", key_display="↑",
                tooltip="Move cursor up to parent"),
        Binding("l,right", "move_right", "Right", key_display="→",
                tooltip="Move cursor to the right sibling"),
        Binding("h,left", "move_left", "Left", key_display="←",
                tooltip="Move cursor to the left sibling"),
        Binding("enter", "zoom_in", "Zoom In",
                tooltip="Expand the cursor's node to be screen width"),
        Binding("escape", "zoom_out", "Zoom Out",
                tooltip="Zoom out to initial view."),
     ]

    # Default CSS for the widget to ensure it fills its container's width.
    DEFAULT_CSS = """
    FlameGraph {
        width: 100%;
    }
    """

    def __init__(self, root: ProfileNode, lock: threading.Lock, *pos_args, **kwargs):
        """Initialize the FlameGraph widget.

        The lock must be held when reading the profile as it may be being built
        in another thread.
        """
        super().__init__(*pos_args, **kwargs)
        self.root = root
        self.profile_lock = lock
        self.cursor = root
        self.selected = root

    def action_move_down(self) -> None:
        """Handle key press down."""
        with self.profile_lock:
            self.cursor = self.cursor.largest_child()
        self.refresh()

    def action_move_up(self) -> None:
        """Handle key press up."""
        if self.cursor.parent != self.cursor.parent.parent:
            self.cursor = self.cursor.parent
            self.refresh()

    def action_move_right(self) -> None:
        """Handle key press right."""
        with self.profile_lock:
            self.cursor = self.cursor.parent.child_after(self.cursor)
        self.refresh()

    def action_move_left(self) -> None:
        """Handle key press left."""
        with self.profile_lock:
            self.cursor = self.cursor.parent.child_before(self.cursor)
        self.refresh()

    def action_zoom_in(self) -> None:
        """Handle key press zoom in."""
        self.selected = self.cursor
        self.refresh()

    def action_zoom_out(self) -> None:
        """Handle key press zoom out."""
        self.selected = self.root
        self.refresh()

    def render_line(self, y: int) -> Strip:
        """Render a single line (row) of the flame graph."""
        _, scroll_y = self.scroll_offset
        y += scroll_y
        with self.profile_lock:
            return self.root.make_flame_strip(y, self.size.width, self.cursor,
                                              self.selected, self.app.theme_variables)

    def profile_changed(self) -> None:
        """Resize and redraw after the profile changed."""
        with self.profile_lock:
            self.styles.height = self.root.depth()
        self.refresh()

    def on_mount(self) -> None:
        """Set the height of the widget when it is displayed."""
        self.profile_changed()

    def on_click(self, click: events.Click) -> None:
        """Handles a mouse click and update the cursor position."""
        _, scroll_y = self.scroll_offset
        y = scroll_y + click.y
        with self.profile_lock:
            clicked_node = self.root.find_node(click.x, y, self.size.width,
                                               self.selected)
        if clicked_node:
            self.cursor = clicked_node
            self.refresh()


class ProfileTree(Tree):
    """A tree view of the profile that can be updated while it is built.

    Tree nodes are created lazily when expanded, the data of each tree node is
    its ProfileNode. The lock must be held when reading the profile.
    """

    def __init__(self, root: ProfileNode, lock: threading.Lock, *pos_args, **kwargs):
        super().__init__("Profile", *pos_args, **kwargs)
        self.profile = root
        self.profile_lock = lock
        # Events whose initial expansion has been done.
        self.seen_events: Set[str] = set()

    def pnode_of(self, tnode: TreeNode) -> ProfileNode:
        """The ProfileNode shown by tnode, the tree's root shows the profile's root."""
        return tnode.data if tnode.data is not None else self.profile

    def event_value(self, pnode: ProfileNode) -> int:
        """Value of the event containing pnode, percentages are relative to it."""
        while pnode.parent is not self.profile and pnode.parent is not pnode:
            pnode = pnode.parent
        return pnode.value

    @staticmethod
    def expanded_descendants(tnode: TreeNode, expanded: Set[ProfileNode]) -> None:
        """Add the profile nodes of the expanded descendants of tnode to expanded."""
        for tchild in tnode.children:
            if tchild.is_expanded and tchild.data is not None:
                expanded.add(tchild.data)
            if tchild.children:
                ProfileTree.expanded_descendants(tchild, expanded)

    def sync_node(self, tnode: TreeNode, expanded: Optional[Set[ProfileNode]] = None) -> None:
        """Update the children of tnode, and expanded descendants, from the profile.

        expanded holds the profile nodes of descendants that were expanded
        before an ancestor's children were recreated. The lock must be held.
        """
        children = self.pnode_of(tnode).sorted_children()
        if expanded is not None or [tchild.data for tchild in tnode.children] != children:
            # Recreate the children in the new order keeping the expanded
            # ones, and their expanded descendants.
            if expanded is None:
                expanded = set()
            self.expanded_descendants(tnode, expanded)
            tnode.remove_children()
            ancestors: Set[ProfileNode] = set()
            for pnode in expanded:
                parent = pnode.parent
                while parent is not None and parent not in ancestors:
                    ancestors.add(parent)
                    parent = parent.parent
            for child in children:
                label = child.tree_label(self.event_value(child))
                new = tnode.add(label, child, allow_expand=bool(child.children))
                if child in expanded:
                    new.expand()
                if child in expanded or child in ancestors:
                    self.sync_node(new, expanded)
            return
        for tchild in tnode.children:
            child = self.pnode_of(tchild)
            tchild.set_label(child.tree_label(self.event_value(child)))
            tchild.allow_expand = bool(child.children)
            if tchild.is_expanded or tchild.children:
                self.sync_node(tchild)

    def expand_largest(self, tnode: TreeNode) -> None:
        """Expand the chain of largest children below tnode.

        The lock must be held.
        """
        while self.pnode_of(tnode).children:
            self.sync_node(tnode)
            tnode = tnode.children[0]
            tnode.expand()

    def find(self, tnode: TreeNode, pnode: ProfileNode) -> Optional[TreeNode]:
        """Find the visible tree node for pnode."""
        for tchild in tnode.children:
            if tchild.data is pnode:
                return tchild
            if tchild.is_expanded:
                found = self.find(tchild, pnode)
                if found:
                    return found
        return None

    def sync(self) -> None:
        """Update the tree from the profile keeping the expanded nodes and cursor."""
        cursor = self.cursor_node.data if self.cursor_node else None
        with self.profile_lock:
            self.root.expand()
            self.sync_node(self.root)
            for tnode in self.root.children:
                name = self.pnode_of(tnode).name
                if name in self.seen_events:
                    continue
                self.seen_events.add(name)
                self.expand_largest(tnode)
                # If there is only one event, expand it also.
                if len(self.root.children) == 1:
                    tnode.expand()
        if cursor is not None:
            found = self.find(self.root, cursor)
            if found:
                self.move_cursor(found)

    def on_tree_node_expanded(self, event: Tree.NodeExpanded) -> None:
        """Create the children of a node when it is expanded."""
        if event.node is not self.root:
            with self.profile_lock:
                self.sync_node(event.node)


class ReportApp(App):
    """A Textual application to display profiling data."""

    # The ^q binding is implied but having it here adds it in the Footer.
    BINDINGS = [
        Binding(key="^q", action="quit", description="Quit",
                tooltip="Quit the app"),
    ]

    def __init__(self, input_file: str, root: Optional[ProfileNode] = None):
        """Initialize the application.

        If root isn't given the profile is loaded from input_file in the
        background after the application starts, the views show the profile
        as it is built.
        """
        super().__init__()
        self.input_file = input_file
        self.root = root if root else ProfileNode("root", parent=None)
        self.loaded = root is not None
        # Held when reading or changing the profile, which is built in a
        # background thread.
        self.profile_lock = threading.Lock()
        # The profile being built in a background thread.
        self.loading: Optional[ProfileBuilder] = None

    def compose(self) -> ComposeResult:
        """Composes the user interface of the application."""
        yield Header()
        with TabbedContent(initial="report"):
            with TabPane("Report", id="report"):
                yield ProfileTree(self.root, self.profile_lock)
            with TabPane("Flame Graph", id="flame"):
                yield FlameGraph(self.root, self.profile_lock)
        yield Footer()

    def on_mount(self) -> None:
        """Start loading the profile unless it was given."""
        self.sub_title = self.input_file
        if self.loaded:
            self.update_views()
        else:
            self.sub_title = f"Loading {self.input_file}"
            self.loading = ProfileBuilder(self.root, self.profile_lock)
            self.load_profile()

    def update_views(self) -> None:
        """Show the latest state of the profile."""
        self.query_one(ProfileTree).sync()
        self.query_one(FlameGraph).profile_changed()

    @work(thread=True, exclusive=True)
    def load_profile(self) -> None:
        """Build the profile in a thread so the UI stays responsive."""
        loading = self.loading
        assert loading is not None
        profile = loading

        def progress() -> None:
            try:
                self.call_from_thread(self.update_progress, profile)
            except RuntimeError:
                # The app is no longer running.
                profile.cancelled = True

        profile.progress = progress
        try:
            profile.read(self.input_file)
        except LoadCancelled:
            return
        except (OSError, ValueError, RuntimeError) as e:
            self.call_from_thread(self.exit, None, 1,
                                  f"Error processing {self.input_file}: {e}")
            return
        finally:
            profile.progress = None
            self.loading = None
        if not profile.cancelled:
            self.call_from_thread(self.profile_loaded, profile)

    def update_progress(self, profile: "ProfileBuilder") -> None:
        """Show how much of the file has been processed and the profile so far."""
        self.sub_title = (f"Loading {self.input_file}: {profile.nr_samples:,} samples"
                          f"{profile.duration_str()}")
        self.update_views()

    def profile_loaded(self, profile: "ProfileBuilder") -> None:
        """Called on the UI thread when loading completes."""
        self.loaded = True
        self.sub_title = (f"{self.input_file}: {profile.nr_samples:,} samples"
                          f"{profile.duration_str()}")
        self.update_views()

    def cancel_loading(self) -> None:
        """Stop a background load, noticed at the next progress interval."""
        loading = self.loading
        if loading is not None:
            loading.cancelled = True

    async def action_quit(self) -> None:
        """Quit, stopping any background load."""
        self.cancel_loading()
        await super().action_quit()

    def on_unmount(self) -> None:
        """Stop any background load when the app exits."""
        self.cancel_loading()


class LoadCancelled(Exception):
    """Raised from the sample callback to stop processing events early."""


class ProfileBuilder:
    """Constructs a profile tree from a stream of events."""
    # Number of samples between checks for cancellation and progress.
    PROGRESS_INTERVAL = 1000
    # Minimum and maximum time between calls to the progress callback.
    PROGRESS_SECONDS = 1.0
    PROGRESS_MAX_SECONDS = 10.0
    # Updating the views costs more as the profile grows, so the time between
    # progress calls grows as this fraction of the time spent loading. This
    # bounds the fraction of the load time spent updating the views.
    PROGRESS_FRACTION = 0.25

    def __init__(self, root: Optional[ProfileNode] = None,
                 lock: Optional[threading.Lock] = None) -> None:
        """Build into root, holding lock when changing it."""
        self.root = root if root else ProfileNode("root", parent=None)
        self.profile_lock = lock if lock else threading.Lock()
        self.session: Optional[perf.session] = None
        self.nr_samples = 0
        self.first_time = 0
        self.last_time = 0
        # Set, possibly from another thread, to stop processing events.
        self.cancelled = False
        # Called periodically, see PROGRESS_FRACTION, while processing events.
        self.progress: Optional[Callable[[], None]] = None
        self.start_progress = time.monotonic()
        self.last_progress = self.start_progress

    def duration_str(self) -> str:
        """Description of the time covered by the processed samples."""
        if not self.last_time:
            return ""
        return f", {(self.last_time - self.first_time) / 1e9:.3f}s of trace"

    def comm(self, sample) -> str:
        """The command name of the thread of the sample."""
        pid = sample.sample_pid
        try:
            assert self.session
            thread = self.session.find_thread(pid, sample.sample_tid)
            return (thread.comm() if thread else None) or f"unknown ({pid})"
        except (OSError, ValueError, KeyError, RuntimeError, TypeError, AttributeError,
                AssertionError):
            return f"unknown ({pid})"

    def process_event(self, sample) -> None:
        """Called by session.process_events to update the profile tree."""
        self.nr_samples += 1
        if self.nr_samples % self.PROGRESS_INTERVAL == 0:
            if self.cancelled:
                raise LoadCancelled()
            now = time.monotonic()
            interval = min(max(self.PROGRESS_SECONDS,
                               (now - self.start_progress) * self.PROGRESS_FRACTION),
                           self.PROGRESS_MAX_SECONDS)
            if self.progress and now - self.last_progress >= interval:
                # Must not hold the lock as the callback may read the profile.
                self.progress()
                # Time from when the callback, that may block, returns.
                self.last_progress = time.monotonic()
        sample_time = sample.sample_time
        if sample_time:
            if not self.first_time or sample_time < self.first_time:
                self.first_time = sample_time
            self.last_time = max(self.last_time, sample_time)
        ev_name = str(sample.evsel)[6:-1]
        comm = self.comm(sample)
        with self.profile_lock:
            ev_root = self.root.find_or_create_node(ev_name)
            ev_root.process_event(sample, comm)

    def read(self, input_file: str) -> None:
        """Process the events in input_file, raising on errors."""
        if self.cancelled:
            raise LoadCancelled()
        self.start_progress = self.last_progress = time.monotonic()
        try:
            self.session = perf.session(perf.data(input_file), sample=self.process_event)
            self.session.process_events()
        finally:
            # Break the reference cycle between the session and the callback.
            self.session = None


def main() -> None:
    """Parse arguments and run the app."""
    parser = argparse.ArgumentParser(
        description="TUI report and flame graph using perf python module.")
    parser.add_argument("-i", "--input", help="input perf.data file")
    args = parser.parse_args()

    input_file = args.input or "perf.data"
    if input_file == "-":
        # The interactive UI reads the keyboard from stdin.
        print("Error: reading perf.data from stdin isn't supported.", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(input_file):
        print(f"Error: {input_file} not found. (try 'perf record' first)", file=sys.stderr)
        sys.exit(1)

    # The app starts immediately and builds the profile in the background.
    app = ReportApp(input_file)
    app.run()
    sys.exit(app.return_code or 0)


if __name__ == "__main__":
    main()
