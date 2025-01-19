# Android Binder fuzzer

This folder contains implementation of libprotobuf-mutator-based Android Binder
fuzzer.

# Build instructions

Android Binder fuzzer is based on `libprotobuf-mutator`
[project](https://github.com/google/libprotobuf-mutator). Thus,
to build the fuzzer you would need to checkout and build `libprotobuf-mutator`.

`libprotobuf-mutator` in turn depends on `libprotobuf` library. You can build
`libprotobuf-mutator` either using system-installed `libprotobuf` or
automatically downloaded the latest version of `libprotobuf` during build
process. On some systems the system version of library is too old and might
cause build issues for the fuzzers. Thus, it is advised to instruct `cmake`
which is used for building `libprotobuf-mutator` to automatically download and
build a working version of protobuf library. The instructions provided below
are based on this approach.

Assuming that all the necessary dependencies per `libprobotub-mutator`
[documentation](https://github.com/google/libprotobuf-mutator/blob/master/README.md#quick-start-on-debianubuntu):

1. Run `tools/lkl/scripts/libprotobuf-mutator-build.sh` passing path to the
folder which `libprotobuf-mutator` should be downloaded to via
`PROTOBUF_MUTATOR_DIR` variable, for example:

```
PROTOBUF_MUTATOR_DIR=/tmp/libprotobuf-mutator \
  tools/lkl/scripts/libprotobuf-mutator-build.sh
```

This script will checkout `libprotobuf-mutator` into `/tmp/libprotobuf-mutator`
folder with its dependencies including the working version of `libprotobuf`
and build the project. `libprotobuf-mutator-build.sh` script uses
`libprotobuf-mutator`
[version 1.4](https://github.com/google/libprotobuf-mutator/releases/tag/v1.4)
(which has been confirmed working with the current implementation of LKL build
system).

Upon successful invocation of the commands above we should get the following
layout:

* `/tmp/libprotobuf-mutator/build/external.protobuf` -- downloaded and built
working version of protobuf library

* `/tmp/libprotobuf-mutator/build/external.protobuf/bin/protoc` -- built
protobuf compiler which generates .pb.cpp and .pb.h files from the
corresponding .proto file

* `/tmp/libprotobuf-mutator/build/external.protobuf/include` -- header files
necessary for building the fuzzer harness

* `/tmp/libprotobuf-mutator/build/external.protobuf/lib` -- built static
libraries needed for building the fuzzer harness

* `/tmp/libprotobuf-mutator/build/src` -- contains compiled
`libprotobuf-mutator.a` static library

* `/tmp/libprotobuf-mutator/build/src/libfuzzer` -- contains compiled
`libprotobuf-mutator-libfuzzer.a` static library for integration with
`libfuzzer` engine

At this point we have all the necessary `libprotobuf-mutator`-related
dependencies and can move to building the actual fuzzer.

2. Build `binder-fuzzer`

```
make -C tools/lkl LKL_FUZZING=1 MMU=1 \
  PROTOBUF_MUTATOR_DIR=/tmp/libprotobuf-mutator \
  clean-conf fuzzers -jX
```

where `X` is the number of parallel jobs to speed up building the fuzzer,
`LKL_FUZZING` enables fuzzing instrumentation (such as code coverage, KASan),
`MMU` enables `CONFIG_MMU` config which binder driver depends on,
`PROTOBUF_MUTATOE_DIR` provides path to the `libprotobuf-mutator` directory
with the header files and static lib dependencies.

Upon successful completion of the `make` command above the fuzzer binary
can be located at `tools/lkl/fuzzers/binder/binder-fuzzer`.

# Reproducing CVE-2023-20938

Here are the instructions on how to reproduce the UAF vulnerability with the
fuzzer:

1. Roll back
[patch](https://github.com/torvalds/linux/commit/bdc1c5fac982845a58d28690cdb56db8c88a530d)
for CVE-2023-21255 using the following command:

```
$ cat > /tmp/CVE-2023-21255.rollback << EOF
--- a/drivers/android/binder.c
+++ b/drivers/android/binder.c
@@ -1941,22 +1941,24 @@ static void binder_deferred_fd_close(int fd)
 static void binder_transaction_buffer_release(struct binder_proc *proc,
 					      struct binder_thread *thread,
 					      struct binder_buffer *buffer,
-					      binder_size_t off_end_offset,
+					      binder_size_t failed_at,
 					      bool is_failure)
 {
 	int debug_id = buffer->debug_id;
-	binder_size_t off_start_offset, buffer_offset;
+	binder_size_t off_start_offset, buffer_offset, off_end_offset;

 	binder_debug(BINDER_DEBUG_TRANSACTION,
 		     "%d buffer release %d, size %zd-%zd, failed at %llx\n",
 		     proc->pid, buffer->debug_id,
 		     buffer->data_size, buffer->offsets_size,
-		     (unsigned long long)off_end_offset);
+		     (unsigned long long)failed_at);

 	if (buffer->target_node)
 		binder_dec_node(buffer->target_node, 1, 0);

 	off_start_offset = ALIGN(buffer->data_size, sizeof(void *));
+	off_end_offset = is_failure && failed_at ? failed_at :
+				off_start_offset + buffer->offsets_size;

 	for (buffer_offset = off_start_offset; buffer_offset < off_end_offset;
 	     buffer_offset += sizeof(binder_size_t)) {
@@ -2117,21 +2119,6 @@ static void binder_transaction_buffer_release(struct binder_proc *proc,
 	}
 }

-/* Clean up all the objects in the buffer */
-static inline void binder_release_entire_buffer(struct binder_proc *proc,
-						struct binder_thread *thread,
-						struct binder_buffer *buffer,
-						bool is_failure)
-{
-	binder_size_t off_end_offset;
-
-	off_end_offset = ALIGN(buffer->data_size, sizeof(void *));
-	off_end_offset += buffer->offsets_size;
-
-	binder_transaction_buffer_release(proc, thread, buffer,
-					  off_end_offset, is_failure);
-}
-
 static int binder_translate_binder(struct flat_binder_object *fp,
 				   struct binder_transaction *t,
 				   struct binder_thread *thread)
@@ -2827,7 +2814,7 @@ static int binder_proc_transaction(struct binder_transaction *t,
 		t_outdated->buffer = NULL;
 		buffer->transaction = NULL;
 		trace_binder_transaction_update_buffer_release(buffer);
-		binder_release_entire_buffer(proc, NULL, buffer, false);
+		binder_transaction_buffer_release(proc, NULL, buffer, 0, 0);
 		binder_alloc_free_buf(&proc->alloc, buffer);
 		kfree(t_outdated);
 		binder_stats_deleted(BINDER_STAT_TRANSACTION);
@@ -3800,7 +3787,7 @@ binder_free_buf(struct binder_proc *proc,
 		binder_node_inner_unlock(buf_node);
 	}
 	trace_binder_transaction_buffer_release(buffer);
-	binder_release_entire_buffer(proc, thread, buffer, is_failure);
+	binder_transaction_buffer_release(proc, thread, buffer, 0, is_failure);
 	binder_alloc_free_buf(&proc->alloc, buffer);
 }

EOF

$ git apply /tmp/CVE-2023-21255.rollback
```

2. Roll back
[patch](https://github.com/torvalds/linux/commit/6d98eb95b450a75adb4516a1d33652dc78d2b20c)
for CVE-2023-20938 using the following command:

```
$ cat > /tmp/CVE-2023-20938.rollback << EOF
--- a/drivers/android/binder.c
+++ b/drivers/android/binder.c
@@ -3260,6 +3260,20 @@ static void binder_transaction(struct binder_proc *proc,
 	t->buffer->clear_on_free = !!(t->flags & TF_CLEAR_BUF);
 	trace_binder_transaction_alloc_buf(t->buffer);

+	if (binder_alloc_copy_user_to_buffer(
+				&target_proc->alloc,
+				t->buffer, 0,
+				(const void __user *)
+					(uintptr_t)tr->data.ptr.buffer,
+				tr->data_size)) {
+		binder_user_error("%d:%d got transaction with invalid data ptr\n",
+				proc->pid, thread->pid);
+		return_error = BR_FAILED_REPLY;
+		return_error_param = -EFAULT;
+		return_error_line = __LINE__;
+		goto err_copy_data_failed;
+	}
+
 	if (binder_alloc_copy_user_to_buffer(
 				&target_proc->alloc,
 				t->buffer,
@@ -3318,27 +3332,9 @@ static void binder_transaction(struct binder_proc *proc,
 			return_error_line = __LINE__;
 			goto err_bad_offset;
 		}
+		object_size = binder_get_object(target_proc, NULL, t->buffer,
+						object_offset, &object);

-		/*
-		 * Copy the source user buffer up to the next object
-		 * that will be processed.
-		 */
-		copy_size = object_offset - user_offset;
-		if (copy_size && (user_offset > object_offset ||
-				binder_alloc_copy_user_to_buffer(
-					&target_proc->alloc,
-					t->buffer, user_offset,
-					user_buffer + user_offset,
-					copy_size))) {
-			binder_user_error("%d:%d got transaction with invalid data ptr\n",
-					proc->pid, thread->pid);
-			return_error = BR_FAILED_REPLY;
-			return_error_param = -EFAULT;
-			return_error_line = __LINE__;
-			goto err_copy_data_failed;
-		}
-		object_size = binder_get_object(target_proc, user_buffer,
-				t->buffer, object_offset, &object);
 		if (object_size == 0 || object_offset < off_min) {
 			binder_user_error("%d:%d got transaction with invalid offset (%lld, min %lld max %lld) or object.\n",
 					  proc->pid, thread->pid,
@@ -3556,19 +3552,6 @@ static void binder_transaction(struct binder_proc *proc,
 			goto err_bad_object_type;
 		}
 	}
-	/* Done processing objects, copy the rest of the buffer */
-	if (binder_alloc_copy_user_to_buffer(
-				&target_proc->alloc,
-				t->buffer, user_offset,
-				user_buffer + user_offset,
-				tr->data_size - user_offset)) {
-		binder_user_error("%d:%d got transaction with invalid data ptr\n",
-				proc->pid, thread->pid);
-		return_error = BR_FAILED_REPLY;
-		return_error_param = -EFAULT;
-		return_error_line = __LINE__;
-		goto err_copy_data_failed;
-	}

 	ret = binder_do_deferred_txn_copies(&target_proc->alloc, t->buffer,
 					    &sgc_head, &pf_head);

EOF

$ git apply /tmp/CVE-2023-20938.rollback
```

3. Build the binder fuzzer (replace `X` with the number of parallel make jobs):

```
make -C tools/lkl LKL_FUZZING=1 MMU=1 \
  PROTOBUF_MUTATOR_DIR=/tmp/libprotobuf-mutator \
  clean-conf fuzzers -jX
```

4. Run the reproducer from `tools/lkl/fuzzers/binder/seeds/CVE-2023-20938`:

```
$ tools/lkl/fuzzers/binder/binder-fuzzer \
    tools/lkl/fuzzers/binder/seeds/CVE-2023-20938

...
Running: tools/lkl/fuzzers/binder/seeds/CVE-2023-20938
...
[    0.624939] ==================================================================
[    0.624977] BUG: KASAN: slab-use-after-free in __unnamed_1+0x2a6c1a/0x1f6c380
[    0.625064] Read of size 16 at addr 0000000050bed658 by task host4/29
[    0.625094]
[    0.625113] CPU: 0 PID: 29 Comm: host4 Not tainted 6.6.0+ #1
[    0.625149]  Call Trace:
[    0.625166]  #00 [<0x000055fa9266541b>] psmouse_protocols+0x1b/0x60
[    0.625227]  #01 [<0x000055fa92879b4e>] trace_event_fields_global_dirty_state+0x2e/0x180
[    0.625273]  #02 [<0x000055fa9287a392>] trace_event_fields_balance_dirty_pages+0xf2/0x320
[    0.625316]  #03 [<0x000055fa9287c069>] __unnamed_1+0x5e9/0x2ec0
[    0.625356]  #04 [<0x000055fa92c468fa>] __unnamed_1+0x2a6c1a/0x1f6c380
[    0.625394]  #05 [<0x000055fa92c3ed04>] __unnamed_1+0x29f024/0x1f6c380
[    0.625432]  #06 [<0x000055fa928beb3e>] print_fmt_io_uring_req_failed+0x25e/0x260
[    0.625474]  #07 [<0x000055fa92682eed>] c2u_E6+0x16d/0x280
[    0.625518]  #08 [<0x000055fa9261770a>] 0x55fa9261770a
[    0.625548]  #09 [<0x000055fa926162ad>] 0x55fa926162ad
[    0.625578]  #10 [<0x0000000000000000>] 0x0
[    0.625606]  #11 [<0x000055fa9261ab40>] 0x55fa9261ab40
[    0.625636]  #12 [<0xbde8087b8d480974>] 0xbde8087b8d480974
[    0.625667]
[    0.625685] The buggy address belongs to the object at 0000000050bed600
[    0.625685]  which belongs to the cache kmalloc-128 of size 128
[    0.625714] The buggy address is located 88 bytes inside of
[    0.625714]  freed 128-byte region [0000000050bed600, 0000000050bed680)
[    0.625747]
[    0.625764] The buggy address belongs to the physical page:
[    0.625784] page:(____ptrval____) refcount:1 mapcount:0 mapping:0000000000000000 index:0x0 pfn:0x50bed
[    0.625818] flags: 0x800(slab|zone=0)
[    0.625846] page_type: 0xffffffff()
[    0.625877] raw: 0000000000000800 0000000050001700 0000000000000100 0000000000000122
[    0.625908] raw: 0000000000000000 0000000000100010 00000001ffffffff
[    0.625931] page dumped because: kasan: bad access detected
[    0.625952]
[    0.625968] Memory state around the buggy address:
[    0.625990]  0000000050bed500: fa fb fb fb fb fb fb fb fb fb fb fb fb fb fb fb
[    0.626017]  0000000050bed580: fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc
[    0.626051] >0000000050bed600: fa fb fb fb fb fb fb fb fb fb fb fb fb fb fb fb
[    0.626074]                                                     ^
[    0.626099]  0000000050bed680: fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc
[    0.626126]  0000000050bed700: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 fc fc
[    0.626149] ==================================================================
[    0.626170] Disabling lock debugging due to kernel taint
[    0.626243] binder: 29:29 ioctl c0306201 7f3ef9dff400 returned -11
[    0.626588] binder: undelivered TRANSACTION_COMPLETE
[    0.626632] binder: undelivered TRANSACTION_ERROR: 29201
[    0.626668] binder: undelivered transaction 2, process died.
Executed tools/lkl/fuzzers/binder/seeds/CVE-2023-20938 in 17 ms
```

# Resources

"How to Fuzz Your Way to Android Universal Root"
[presentation](https://www.youtube.com/watch?v=U-xSM159YLI)
([slides](https://androidoffsec.withgoogle.com/posts/attacking-android-binder-analysis-and-exploitation-of-cve-2023-20938/offensivecon_24_binder.pdf)) provides information on fuzzer design and vulnerabilities caught using the fuzzer.

