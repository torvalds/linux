.. SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)

libperf

The libperf library provides an API to access the linux kernel perf
events subsystem. It provides the following high level objects:

  - struct perf_cpu_map
  - struct perf_thread_map
  - struct perf_evlist
  - struct perf_evsel

reference
=========
Function reference by header files:

perf/core.h
-----------
.. code-block:: c

  typedef int (\*libperf_print_fn_t)(enum libperf_print_level level,
                                     const char \*, va_list ap);

  void libperf_set_print(libperf_print_fn_t fn);

perf/cpumap.h
-------------
.. code-block:: c

  struct perf_cpu_map \*perf_cpu_map__dummy_new(void);
  struct perf_cpu_map \*perf_cpu_map__new(const char \*cpu_list);
  struct perf_cpu_map \*perf_cpu_map__read(FILE \*file);
  struct perf_cpu_map \*perf_cpu_map__get(struct perf_cpu_map \*map);
  void perf_cpu_map__put(struct perf_cpu_map \*map);
  int perf_cpu_map__cpu(const struct perf_cpu_map \*cpus, int idx);
  int perf_cpu_map__nr(const struct perf_cpu_map \*cpus);
  perf_cpu_map__for_each_cpu(cpu, idx, cpus)

perf/threadmap.h
----------------
.. code-block:: c

  struct perf_thread_map \*perf_thread_map__new_dummy(void);
  void perf_thread_map__set_pid(struct perf_thread_map \*map, int thread, pid_t pid);
  char \*perf_thread_map__comm(struct perf_thread_map \*map, int thread);
  struct perf_thread_map \*perf_thread_map__get(struct perf_thread_map \*map);
  void perf_thread_map__put(struct perf_thread_map \*map);

perf/evlist.h
-------------
.. code-block::

  void perf_evlist__init(struct perf_evlist \*evlist);
  void perf_evlist__add(struct perf_evlist \*evlist,
                      struct perf_evsel \*evsel);
  void perf_evlist__remove(struct perf_evlist \*evlist,
                         struct perf_evsel \*evsel);
  struct perf_evlist \*perf_evlist__new(void);
  void perf_evlist__delete(struct perf_evlist \*evlist);
  struct perf_evsel\* perf_evlist__next(struct perf_evlist \*evlist,
                                     struct perf_evsel \*evsel);
  int perf_evlist__open(struct perf_evlist \*evlist);
  void perf_evlist__close(struct perf_evlist \*evlist);
  void perf_evlist__enable(struct perf_evlist \*evlist);
  void perf_evlist__disable(struct perf_evlist \*evlist);
  perf_evlist__for_each_evsel(evlist, pos)
  void perf_evlist__set_maps(struct perf_evlist \*evlist,
                           struct perf_cpu_map \*cpus,
                           struct perf_thread_map \*threads);

perf/evsel.h
------------
.. code-block:: c

  struct perf_counts_values {
        union {
                struct {
                        uint64_t val;
                        uint64_t ena;
                        uint64_t run;
                };
                uint64_t values[3];
        };
  };

  void perf_evsel__init(struct perf_evsel \*evsel,
                      struct perf_event_attr \*attr);
  struct perf_evsel \*perf_evsel__new(struct perf_event_attr \*attr);
  void perf_evsel__delete(struct perf_evsel \*evsel);
  int perf_evsel__open(struct perf_evsel \*evsel, struct perf_cpu_map \*cpus,
                     struct perf_thread_map \*threads);
  void perf_evsel__close(struct perf_evsel \*evsel);
  int perf_evsel__read(struct perf_evsel \*evsel, int cpu, int thread,
                     struct perf_counts_values \*count);
  int perf_evsel__enable(struct perf_evsel \*evsel);
  int perf_evsel__disable(struct perf_evsel \*evsel);
  int perf_evsel__apply_filter(struct perf_evsel \*evsel, const char \*filter);
  struct perf_cpu_map \*perf_evsel__cpus(struct perf_evsel \*evsel);
  struct perf_thread_map \*perf_evsel__threads(struct perf_evsel \*evsel);
  struct perf_event_attr \*perf_evsel__attr(struct perf_evsel \*evsel);
