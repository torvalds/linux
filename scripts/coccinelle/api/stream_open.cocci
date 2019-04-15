// SPDX-License-Identifier: GPL-2.0
// Author: Kirill Smelkov (kirr@nexedi.com)
//
// Search for stream-like files that are using nonseekable_open and convert
// them to stream_open. A stream-like file is a file that does not use ppos in
// its read and write. Rationale for the conversion is to avoid deadlock in
// between read and write.

virtual report
virtual patch
virtual explain  // explain decisions in the patch (SPFLAGS="-D explain")

// stream-like reader & writer - ones that do not depend on f_pos.
@ stream_reader @
identifier readstream, ppos;
identifier f, buf, len;
type loff_t;
@@
  ssize_t readstream(struct file *f, char *buf, size_t len, loff_t *ppos)
  {
    ... when != ppos
  }

@ stream_writer @
identifier writestream, ppos;
identifier f, buf, len;
type loff_t;
@@
  ssize_t writestream(struct file *f, const char *buf, size_t len, loff_t *ppos)
  {
    ... when != ppos
  }


// a function that blocks
@ blocks @
identifier block_f;
identifier wait_event =~ "^wait_event_.*";
@@
  block_f(...) {
    ... when exists
    wait_event(...)
    ... when exists
  }

// stream_reader that can block inside.
//
// XXX wait_* can be called not directly from current function (e.g. func -> f -> g -> wait())
// XXX currently reader_blocks supports only direct and 1-level indirect cases.
@ reader_blocks_direct @
identifier stream_reader.readstream;
identifier wait_event =~ "^wait_event_.*";
@@
  readstream(...)
  {
    ... when exists
    wait_event(...)
    ... when exists
  }

@ reader_blocks_1 @
identifier stream_reader.readstream;
identifier blocks.block_f;
@@
  readstream(...)
  {
    ... when exists
    block_f(...)
    ... when exists
  }

@ reader_blocks depends on reader_blocks_direct || reader_blocks_1 @
identifier stream_reader.readstream;
@@
  readstream(...) {
    ...
  }


// file_operations + whether they have _any_ .read, .write, .llseek ... at all.
//
// XXX add support for file_operations xxx[N] = ...	(sound/core/pcm_native.c)
@ fops0 @
identifier fops;
@@
  struct file_operations fops = {
    ...
  };

@ has_read @
identifier fops0.fops;
identifier read_f;
@@
  struct file_operations fops = {
    .read = read_f,
  };

@ has_read_iter @
identifier fops0.fops;
identifier read_iter_f;
@@
  struct file_operations fops = {
    .read_iter = read_iter_f,
  };

@ has_write @
identifier fops0.fops;
identifier write_f;
@@
  struct file_operations fops = {
    .write = write_f,
  };

@ has_write_iter @
identifier fops0.fops;
identifier write_iter_f;
@@
  struct file_operations fops = {
    .write_iter = write_iter_f,
  };

@ has_llseek @
identifier fops0.fops;
identifier llseek_f;
@@
  struct file_operations fops = {
    .llseek = llseek_f,
  };

@ has_no_llseek @
identifier fops0.fops;
@@
  struct file_operations fops = {
    .llseek = no_llseek,
  };

@ has_mmap @
identifier fops0.fops;
identifier mmap_f;
@@
  struct file_operations fops = {
    .mmap = mmap_f,
  };

@ has_copy_file_range @
identifier fops0.fops;
identifier copy_file_range_f;
@@
  struct file_operations fops = {
    .copy_file_range = copy_file_range_f,
  };

@ has_remap_file_range @
identifier fops0.fops;
identifier remap_file_range_f;
@@
  struct file_operations fops = {
    .remap_file_range = remap_file_range_f,
  };

@ has_splice_read @
identifier fops0.fops;
identifier splice_read_f;
@@
  struct file_operations fops = {
    .splice_read = splice_read_f,
  };

@ has_splice_write @
identifier fops0.fops;
identifier splice_write_f;
@@
  struct file_operations fops = {
    .splice_write = splice_write_f,
  };


// file_operations that is candidate for stream_open conversion - it does not
// use mmap and other methods that assume @offset access to file.
//
// XXX for simplicity require no .{read/write}_iter and no .splice_{read/write} for now.
// XXX maybe_steam.fops cannot be used in other rules - it gives "bad rule maybe_stream or bad variable fops".
@ maybe_stream depends on (!has_llseek || has_no_llseek) && !has_mmap && !has_copy_file_range && !has_remap_file_range && !has_read_iter && !has_write_iter && !has_splice_read && !has_splice_write @
identifier fops0.fops;
@@
  struct file_operations fops = {
  };


// ---- conversions ----

// XXX .open = nonseekable_open -> .open = stream_open
// XXX .open = func -> openfunc -> nonseekable_open

// read & write
//
// if both are used in the same file_operations together with an opener -
// under that conditions we can use stream_open instead of nonseekable_open.
@ fops_rw depends on maybe_stream @
identifier fops0.fops, openfunc;
identifier stream_reader.readstream;
identifier stream_writer.writestream;
@@
  struct file_operations fops = {
      .open  = openfunc,
      .read  = readstream,
      .write = writestream,
  };

@ report_rw depends on report @
identifier fops_rw.openfunc;
position p1;
@@
  openfunc(...) {
    <...
     nonseekable_open@p1
    ...>
  }

@ script:python depends on report && reader_blocks @
fops << fops0.fops;
p << report_rw.p1;
@@
coccilib.report.print_report(p[0],
  "ERROR: %s: .read() can deadlock .write(); change nonseekable_open -> stream_open to fix." % (fops,))

@ script:python depends on report && !reader_blocks @
fops << fops0.fops;
p << report_rw.p1;
@@
coccilib.report.print_report(p[0],
  "WARNING: %s: .read() and .write() have stream semantic; safe to change nonseekable_open -> stream_open." % (fops,))


@ explain_rw_deadlocked depends on explain && reader_blocks @
identifier fops_rw.openfunc;
@@
  openfunc(...) {
    <...
-    nonseekable_open
+    nonseekable_open /* read & write (was deadlock) */
    ...>
  }


@ explain_rw_nodeadlock depends on explain && !reader_blocks @
identifier fops_rw.openfunc;
@@
  openfunc(...) {
    <...
-    nonseekable_open
+    nonseekable_open /* read & write (no direct deadlock) */
    ...>
  }

@ patch_rw depends on patch @
identifier fops_rw.openfunc;
@@
  openfunc(...) {
    <...
-   nonseekable_open
+   stream_open
    ...>
  }


// read, but not write
@ fops_r depends on maybe_stream && !has_write @
identifier fops0.fops, openfunc;
identifier stream_reader.readstream;
@@
  struct file_operations fops = {
      .open  = openfunc,
      .read  = readstream,
  };

@ report_r depends on report @
identifier fops_r.openfunc;
position p1;
@@
  openfunc(...) {
    <...
    nonseekable_open@p1
    ...>
  }

@ script:python depends on report @
fops << fops0.fops;
p << report_r.p1;
@@
coccilib.report.print_report(p[0],
  "WARNING: %s: .read() has stream semantic; safe to change nonseekable_open -> stream_open." % (fops,))

@ explain_r depends on explain @
identifier fops_r.openfunc;
@@
  openfunc(...) {
    <...
-   nonseekable_open
+   nonseekable_open /* read only */
    ...>
  }

@ patch_r depends on patch @
identifier fops_r.openfunc;
@@
  openfunc(...) {
    <...
-   nonseekable_open
+   stream_open
    ...>
  }


// write, but not read
@ fops_w depends on maybe_stream && !has_read @
identifier fops0.fops, openfunc;
identifier stream_writer.writestream;
@@
  struct file_operations fops = {
      .open  = openfunc,
      .write = writestream,
  };

@ report_w depends on report @
identifier fops_w.openfunc;
position p1;
@@
  openfunc(...) {
    <...
    nonseekable_open@p1
    ...>
  }

@ script:python depends on report @
fops << fops0.fops;
p << report_w.p1;
@@
coccilib.report.print_report(p[0],
  "WARNING: %s: .write() has stream semantic; safe to change nonseekable_open -> stream_open." % (fops,))

@ explain_w depends on explain @
identifier fops_w.openfunc;
@@
  openfunc(...) {
    <...
-   nonseekable_open
+   nonseekable_open /* write only */
    ...>
  }

@ patch_w depends on patch @
identifier fops_w.openfunc;
@@
  openfunc(...) {
    <...
-   nonseekable_open
+   stream_open
    ...>
  }


// no read, no write - don't change anything
