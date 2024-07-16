#!/bin/bash
perf record -e compaction:mm_compaction_begin -e compaction:mm_compaction_end -e compaction:mm_compaction_migratepages -e compaction:mm_compaction_isolate_migratepages -e compaction:mm_compaction_isolate_freepages $@
