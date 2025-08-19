#!/usr/bin/env python3
# SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
"""Interactive perf list."""

import argparse
from typing import Any, Dict, Optional, Tuple
import perf
from textual import on
from textual.app import App, ComposeResult
from textual.binding import Binding
from textual.containers import Horizontal, HorizontalGroup, Vertical, VerticalScroll
from textual.command import SearchIcon
from textual.screen import ModalScreen
from textual.widgets import Button, Footer, Header, Input, Label, Sparkline, Static, Tree
from textual.widgets.tree import TreeNode


class ErrorScreen(ModalScreen[bool]):
    """Pop up dialog for errors."""

    CSS = """
    ErrorScreen {
        align: center middle;
    }
    """

    def __init__(self, error: str):
        self.error = error
        super().__init__()

    def compose(self) -> ComposeResult:
        yield Button(f"Error: {self.error}", variant="primary", id="error")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        self.dismiss(True)


class SearchScreen(ModalScreen[str]):
    """Pop up dialog for search."""

    CSS = """
    SearchScreen Horizontal {
        align: center middle;
        margin-top: 1;
    }
    SearchScreen Input {
        width: 1fr;
    }
    """

    def compose(self) -> ComposeResult:
        yield Horizontal(SearchIcon(), Input(placeholder="Event name"))

    def on_input_submitted(self, event: Input.Submitted) -> None:
        """Handle the user pressing Enter in the input field."""
        self.dismiss(event.value)


class Counter(HorizontalGroup):
    """Two labels for a CPU and its counter value."""

    CSS = """
    Label {
        gutter: 1;
    }
    """

    def __init__(self, cpu: int) -> None:
        self.cpu = cpu
        super().__init__()

    def compose(self) -> ComposeResult:
        label = f"cpu{self.cpu}" if self.cpu >= 0 else "total"
        yield Label(label + " ")
        yield Label("0", id=f"counter_{label}")


class CounterSparkline(HorizontalGroup):
    """A Sparkline for a performance counter."""

    def __init__(self, cpu: int) -> None:
        self.cpu = cpu
        super().__init__()

    def compose(self) -> ComposeResult:
        label = f"cpu{self.cpu}" if self.cpu >= 0 else "total"
        yield Label(label)
        yield Sparkline([], summary_function=max, id=f"sparkline_{label}")


class IListApp(App):
    TITLE = "Interactive Perf List"

    BINDINGS = [
        Binding(key="s", action="search", description="Search",
                tooltip="Search events and PMUs"),
        Binding(key="n", action="next", description="Next",
                tooltip="Next search result or item"),
        Binding(key="p", action="prev", description="Previous",
                tooltip="Previous search result or item"),
        Binding(key="c", action="collapse", description="Collapse",
                tooltip="Collapse the current PMU"),
        Binding(key="^q", action="quit", description="Quit",
                tooltip="Quit the app"),
    ]

    CSS = """
        /* Make the 'total' sparkline a different color. */
        #sparkline_total > .sparkline--min-color {
            color: $accent;
        }
        #sparkline_total > .sparkline--max-color {
            color: $accent 30%;
        }
        /*
         * Make the active_search initially not displayed with the text in
         * the middle of the line.
         */
        #active_search {
            display: none;
            width: 100%;
            text-align: center;
        }
    """

    def __init__(self, interval: float) -> None:
        self.interval = interval
        self.evlist = None
        self.search_results: list[TreeNode[str]] = []
        self.cur_search_result: TreeNode[str] | None = None
        super().__init__()

    def expand_and_select(self, node: TreeNode[Any]) -> None:
        """Expand select a node in the tree."""
        if node.parent:
            node.parent.expand()
            if node.parent.parent:
                node.parent.parent.expand()
        node.expand()
        node.tree.select_node(node)
        node.tree.scroll_to_node(node)

    def set_searched_tree_node(self, previous: bool) -> None:
        """Set the cur_search_result node to either the next or previous."""
        l = len(self.search_results)

        if l < 1:
            tree: Tree[str] = self.query_one("#pmus", Tree)
            if previous:
                tree.action_cursor_up()
            else:
                tree.action_cursor_down()
            return

        if self.cur_search_result:
            idx = self.search_results.index(self.cur_search_result)
            if previous:
                idx = idx - 1 if idx > 0 else l - 1
            else:
                idx = idx + 1 if idx < l - 1 else 0
        else:
            idx = l - 1 if previous else 0

        node = self.search_results[idx]
        if node == self.cur_search_result:
            return

        self.cur_search_result = node
        self.expand_and_select(node)

    def action_search(self) -> None:
        """Search was chosen."""
        def set_initial_focus(event: str | None) -> None:
            """Sets the focus after the SearchScreen is dismissed."""

            search_label = self.query_one("#active_search", Label)
            search_label.display = True if event else False
            if not event:
                return
            event = event.lower()
            search_label.update(f'Searching for events matching "{event}"')

            tree: Tree[str] = self.query_one("#pmus", Tree)

            def find_search_results(event: str, node: TreeNode[str],
                                    cursor_seen: bool = False,
                                    match_after_cursor: Optional[TreeNode[str]] = None
                                    ) -> Tuple[bool, Optional[TreeNode[str]]]:
                """Find nodes that match the search remembering the one after the cursor."""
                if not cursor_seen and node == tree.cursor_node:
                    cursor_seen = True
                if node.data and event in node.data:
                    if cursor_seen and not match_after_cursor:
                        match_after_cursor = node
                    self.search_results.append(node)

                if node.children:
                    for child in node.children:
                        (cursor_seen, match_after_cursor) = \
                            find_search_results(event, child, cursor_seen, match_after_cursor)
                return (cursor_seen, match_after_cursor)

            self.search_results.clear()
            (_, self.cur_search_result) = find_search_results(event, tree.root)
            if len(self.search_results) < 1:
                self.push_screen(ErrorScreen(f"Failed to find pmu/event {event}"))
                search_label.display = False
            elif self.cur_search_result:
                self.expand_and_select(self.cur_search_result)
            else:
                self.set_searched_tree_node(previous=False)

        self.push_screen(SearchScreen(), set_initial_focus)

    def action_next(self) -> None:
        """Next was chosen."""
        self.set_searched_tree_node(previous=False)

    def action_prev(self) -> None:
        """Previous was chosen."""
        self.set_searched_tree_node(previous=True)

    def action_collapse(self) -> None:
        """Collapse the potentially large number of events under a PMU."""
        tree: Tree[str] = self.query_one("#pmus", Tree)
        node = tree.cursor_node
        if node and node.parent and node.parent.parent:
            node.parent.collapse_all()
            node.tree.scroll_to_node(node.parent)

    def update_counts(self) -> None:
        """Called every interval to update counts."""
        if not self.evlist:
            return

        def update_count(cpu: int, count: int):
            # Update the raw count display.
            counter: Label = self.query(f"#counter_cpu{cpu}" if cpu >= 0 else "#counter_total")
            if not counter:
                return
            counter = counter.first(Label)
            counter.update(str(count))

            # Update the sparkline.
            line: Sparkline = self.query(f"#sparkline_cpu{cpu}" if cpu >= 0 else "#sparkline_total")
            if not line:
                return
            line = line.first(Sparkline)
            # If there are more events than the width, remove the front event.
            if len(line.data) > line.size.width:
                line.data.pop(0)
            line.data.append(count)
            line.mutate_reactive(Sparkline.data)

        # Update the total and each CPU counts, assume there's just 1 evsel.
        total = 0
        self.evlist.disable()
        for evsel in self.evlist:
            for cpu in evsel.cpus():
                aggr = 0
                for thread in evsel.threads():
                    counts = evsel.read(cpu, thread)
                    aggr += counts.val
                update_count(cpu, aggr)
                total += aggr
        update_count(-1, total)
        self.evlist.enable()

    def on_mount(self) -> None:
        """When App starts set up periodic event updating."""
        self.update_counts()
        self.set_interval(self.interval, self.update_counts)

    def set_pmu_and_event(self, pmu: str, event: str) -> None:
        """Updates the event/description and starts the counters."""
        # Remove previous event information.
        if self.evlist:
            self.evlist.disable()
            self.evlist.close()
            lines = self.query(CounterSparkline)
            for line in lines:
                line.remove()
            lines = self.query(Counter)
            for line in lines:
                line.remove()

        def pmu_event_description(pmu: str, event: str) -> str:
            """Find and format event description for {pmu}/{event}/."""
            def get_info(info: Dict[str, str], key: str):
                return (info[key] + "\n") if key in info else ""

            for p in perf.pmus():
                if p.name() != pmu:
                    continue
                for info in p.events():
                    if "name" not in info or info["name"] != event:
                        continue

                    desc = get_info(info, "topic")
                    desc += get_info(info, "event_type_desc")
                    desc += get_info(info, "desc")
                    desc += get_info(info, "long_desc")
                    desc += get_info(info, "encoding_desc")
                    return desc
            return "description"

        # Parse event, update event text and description.
        full_name = event if event.startswith(pmu) or ':' in event else f"{pmu}/{event}/"
        self.query_one("#event_name", Label).update(full_name)
        self.query_one("#event_description", Static).update(pmu_event_description(pmu, event))

        # Open the event.
        try:
            self.evlist = perf.parse_events(full_name)
            if self.evlist:
                self.evlist.open()
                self.evlist.enable()
        except:
            self.evlist = None

        if not self.evlist:
            self.push_screen(ErrorScreen(f"Failed to open {full_name}"))
            return

        # Add spark lines for all the CPUs. Note, must be done after
        # open so that the evlist CPUs have been computed by propagate
        # maps.
        lines = self.query_one("#lines")
        line = CounterSparkline(cpu=-1)
        lines.mount(line)
        for cpu in self.evlist.all_cpus():
            line = CounterSparkline(cpu)
            lines.mount(line)
        line = Counter(cpu=-1)
        lines.mount(line)
        for cpu in self.evlist.all_cpus():
            line = Counter(cpu)
            lines.mount(line)

    def compose(self) -> ComposeResult:
        """Draws the app."""
        def pmu_event_tree() -> Tree:
            """Create tree of PMUs with events under."""
            tree: Tree[str] = Tree("PMUs", id="pmus")
            tree.root.expand()
            for pmu in perf.pmus():
                pmu_name = pmu.name().lower()
                pmu_node = tree.root.add(pmu_name, data=pmu_name)
                try:
                    for event in sorted(pmu.events(), key=lambda x: x["name"]):
                        if "name" in event:
                            e = event["name"].lower()
                            if "alias" in event:
                                pmu_node.add_leaf(f'{e} ({event["alias"]})', data=e)
                            else:
                                pmu_node.add_leaf(e, data=e)
                except:
                    # Reading events may fail with EPERM, ignore.
                    pass
            return tree

        yield Header(id="header")
        yield Horizontal(Vertical(pmu_event_tree(), id="events"),
                         Vertical(Label("event name", id="event_name"),
                                  Static("description", markup=False, id="event_description"),
                                  ))
        yield Label(id="active_search")
        yield VerticalScroll(id="lines")
        yield Footer(id="footer")

    @on(Tree.NodeSelected)
    def on_tree_node_selected(self, event: Tree.NodeSelected[str]) -> None:
        """Called when a tree node is selected, selecting the event."""
        if event.node.parent and event.node.parent.parent:
            assert event.node.parent.data is not None
            assert event.node.data is not None
            self.set_pmu_and_event(event.node.parent.data, event.node.data)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument('-I', '--interval', help="Counter update interval in seconds", default=0.1)
    args = ap.parse_args()
    app = IListApp(float(args.interval))
    app.run()
