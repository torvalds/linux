/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __COREDUMP_TEST_HELPERS_H
#define __COREDUMP_TEST_HELPERS_H

#include <link.h>
#include <stdbool.h>
#include <sys/types.h>
#include <linux/coredump.h>

#include "../pidfd/pidfd.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#define NUM_THREAD_SPAWN 128

/* Size of the mostly unpopulated mapping the sparse coredump test maps. */
#define SPARSE_MAPPING_SIZE (256 * 1024 * 1024)

/* A task mapping at least this much is worth a record stream. */
#define SPARSE_STREAM_THRESHOLD (SPARSE_MAPPING_SIZE / 2)

/* Size of the shared anonymous mapping the memory types tests map. */
#define MEMORY_MAPPING_SIZE (4 * 1024 * 1024)

/* Leave the coredump_filter the crashing child inherited alone. */
#define FILTER_TASK_INHERIT ((__u64)-1)

/* Every memory type the kernel is expected to advertise. */
#define TEST_MEMORY_ALL						\
	(COREDUMP_MEMORY_ANON_PRIVATE | COREDUMP_MEMORY_ANON_SHARED |	\
	 COREDUMP_MEMORY_FILE_PRIVATE | COREDUMP_MEMORY_FILE_SHARED |	\
	 COREDUMP_MEMORY_ELF_HEADERS |					\
	 COREDUMP_MEMORY_HUGETLB_PRIVATE | COREDUMP_MEMORY_HUGETLB_SHARED | \
	 COREDUMP_MEMORY_DAX_PRIVATE | COREDUMP_MEMORY_DAX_SHARED)

/* Shared helper function declarations */
void *do_nothing(void *arg);
void crashing_child(void);
void crashing_child_sparse(size_t size);
void crashing_child_memory(__u64 task_filter, int fd_addr);
bool find_coredump_segment(int fd, __u64 vaddr, ElfW(Phdr) *segment);
bool sum_coredump_segments(int fd, __u64 *data, __u64 *notes);
bool check_coredump_extent(int fd);
bool peer_coredump_filter(int fd_peer_pidfd, __u64 *memory_types);
ssize_t peer_read_mem(int fd_peer_pidfd, __u64 addr, void *buf, size_t len);
ssize_t recv_coredump_records(int fd_coredump, int fd_core_file,
			      off_t *coredump_size, bool *truncated,
			      int fd_peer_pidfd);
ssize_t recv_coredump_compact(int fd_coredump, int fd_object, int fd_reference,
			      off_t *coredump_size);
ssize_t recv_coredump_bytes(int fd_coredump, int fd_core_file);
ssize_t peer_vm_size(int fd_peer_pidfd);
bool is_elf_core(int fd);
int check_compact_coredump(int fd_object, int fd_reference);
int create_detached_tmpfs(void);
int create_and_listen_unix_socket(const char *path);
bool set_core_pattern(const char *pattern);
int get_peer_pidfd(int fd);
bool get_pidfd_info(int fd_peer_pidfd, struct pidfd_info *info);

/* Protocol helper function declarations */
ssize_t recv_marker(int fd);
bool read_marker(int fd, enum coredump_mark mark);
bool read_hangup(int fd);
bool read_coredump_req(int fd, struct coredump_req *req);
bool read_coredump_req_sized(int fd, struct coredump_req *req, size_t user_size);
bool send_coredump_ack(int fd, const struct coredump_req *req,
		       __u64 mask, size_t size_ack);
bool send_coredump_ack_types(int fd, const struct coredump_req *req,
			      __u64 mask, __u64 memory_types, size_t size_ack);
bool send_coredump_ack_bytes(int fd, const struct coredump_ack *ack, size_t len);
bool check_coredump_req(const struct coredump_req *req);
int open_coredump_tmpfile(int fd_tmpfs_detached);
void process_coredump_worker(int fd_coredump, int fd_peer_pidfd, int fd_core_file);

#endif /* __COREDUMP_TEST_HELPERS_H */
