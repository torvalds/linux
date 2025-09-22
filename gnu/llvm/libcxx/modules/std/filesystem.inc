// -*- C++ -*-
//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

export namespace std::filesystem {
  // [fs.class.path], paths
  using std::filesystem::path;

  // [fs.path.nonmember], path non-member functions
  using std::filesystem::hash_value;
  using std::filesystem::swap;

  // [fs.class.filesystem.error], filesystem errors
  using std::filesystem::filesystem_error;

#ifndef _LIBCPP_HAS_NO_FILESYSTEM
  // [fs.class.directory.entry], directory entries
  using std::filesystem::directory_entry;

  // [fs.class.directory.iterator], directory iterators
  using std::filesystem::directory_iterator;

  // [fs.dir.itr.nonmembers], range access for directory iterators
  using std::filesystem::begin;
  using std::filesystem::end;

  // [fs.class.rec.dir.itr], recursive directory iterators
  using std::filesystem::recursive_directory_iterator;
#endif // _LIBCPP_HAS_NO_FILESYSTEM

  // [fs.rec.dir.itr.nonmembers], range access for recursive directory iterators

  // [fs.class.file.status], file status
  using std::filesystem::file_status;
  using std::filesystem::space_info;

  // [fs.enum], enumerations
  using std::filesystem::copy_options;
  using std::filesystem::directory_options;
  using std::filesystem::file_type;
  using std::filesystem::perm_options;
  using std::filesystem::perms;

  using std::filesystem::file_time_type;

  // several of these enums are a bitmask type.
  // [bitmask.types] specified operators
  using std::filesystem::operator&;
  using std::filesystem::operator&=;
  using std::filesystem::operator^;
  using std::filesystem::operator^=;
  using std::filesystem::operator|;
  using std::filesystem::operator|=;
  using std::filesystem::operator~;

#ifndef _LIBCPP_HAS_NO_FILESYSTEM
  // [fs.op.funcs], filesystem operations
  using std::filesystem::absolute;
  using std::filesystem::canonical;
  using std::filesystem::copy;
  using std::filesystem::copy_file;
  using std::filesystem::copy_symlink;
  using std::filesystem::create_directories;
  using std::filesystem::create_directory;
  using std::filesystem::create_directory_symlink;
  using std::filesystem::create_hard_link;
  using std::filesystem::create_symlink;
  using std::filesystem::current_path;
  using std::filesystem::equivalent;
  using std::filesystem::exists;
  using std::filesystem::file_size;
  using std::filesystem::hard_link_count;

  using std::filesystem::is_block_file;
  using std::filesystem::is_character_file;
  using std::filesystem::is_directory;
  using std::filesystem::is_empty;
  using std::filesystem::is_fifo;
  using std::filesystem::is_other;
  using std::filesystem::is_regular_file;
  using std::filesystem::is_socket;
  using std::filesystem::is_symlink;

  using std::filesystem::last_write_time;
  using std::filesystem::permissions;
  using std::filesystem::proximate;
  using std::filesystem::read_symlink;
  using std::filesystem::relative;
  using std::filesystem::remove;

  using std::filesystem::remove_all;
  using std::filesystem::rename;
  using std::filesystem::resize_file;
  using std::filesystem::space;
  using std::filesystem::status;
  using std::filesystem::status_known;
  using std::filesystem::symlink_status;
  using std::filesystem::temp_directory_path;
  using std::filesystem::weakly_canonical;
#endif // _LIBCPP_HAS_NO_FILESYSTEM

  // [depr.fs.path.factory]
  using std::filesystem::u8path;
} // namespace std::filesystem

// [fs.path.hash], hash support
export namespace std {
  using std::hash;
}

export namespace std::ranges {
#ifndef _LIBCPP_HAS_NO_FILESYSTEM
  using std::ranges::enable_borrowed_range;
  using std::ranges::enable_view;
#endif // _LIBCPP_HAS_NO_FILESYSTEM
} // namespace std::ranges
