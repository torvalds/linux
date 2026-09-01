# SPDX-License-Identifier: GPL-2.0
"""
Live event session helper using perf.evlist.

This module provides a LiveSession class that allows running a callback
for each event collected live from the system, similar to perf.session
but without requiring a perf.data file.
"""

import perf


class LiveSession:
    """Represents a live event collection session."""

    def __init__(self, event_string: str, sample_callback):
        self.event_string = event_string
        self.sample_callback = sample_callback
        # Create a cpu map for all online CPUs
        self.cpus = perf.cpu_map()
        # Parse events and set maps
        self.evlist = perf.parse_events(self.event_string, self.cpus)
        self.evlist.config()

    def run(self):
        """Run the live session."""
        try:
            self.evlist.open()
            self.evlist.mmap()
            self.evlist.enable()

            while True:
                # Poll for events with 100ms timeout
                try:
                    self.evlist.poll(100)
                except InterruptedError:
                    continue
                for cpu in self.cpus:
                    for _ in range(1000): # Limit to 1000 events per CPU per poll to prevent starvation
                        try:
                            event = self.evlist.read_on_cpu(cpu)
                        except TypeError as e:
                            if "Unknown CPU" in str(e):
                                # CPU might be unmapped or offline, wait for mmap event
                                break
                            if "Unexpected header type" in str(e):
                                # Ignore valid but unsupported event types
                                continue
                            raise

                        if event is None:
                            break

                        if event.type == perf.RECORD_SAMPLE:
                            self.sample_callback(event)
        except KeyboardInterrupt:
            pass
        finally:
            self.evlist.close()
