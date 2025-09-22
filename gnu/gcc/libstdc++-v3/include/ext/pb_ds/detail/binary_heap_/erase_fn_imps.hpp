// -*- C++ -*-

// Copyright (C) 2005, 2006 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software
// Foundation; either version 2, or (at your option) any later
// version.

// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this library; see the file COPYING.  If not, write to
// the Free Software Foundation, 59 Temple Place - Suite 330, Boston,
// MA 02111-1307, USA.

// As a special exception, you may use this file as part of a free
// software library without restriction.  Specifically, if other files
// instantiate templates or use macros or inline functions from this
// file, or you compile this file and link it with other files to
// produce an executable, this file does not by itself cause the
// resulting executable to be covered by the GNU General Public
// License.  This exception does not however invalidate any other
// reasons why the executable file might be covered by the GNU General
// Public License.

// Copyright (C) 2004 Ami Tavory and Vladimir Dreizin, IBM-HRL.

// Permission to use, copy, modify, sell, and distribute this software
// is hereby granted without fee, provided that the above copyright
// notice appears in all copies, and that both that copyright notice
// and this permission notice appear in supporting documentation. None
// of the above authors, nor IBM Haifa Research Laboratories, make any
// representation about the suitability of this software for any
// purpose. It is provided "as is" without express or implied
// warranty.

/**
 * @file erase_fn_imps.hpp
 * Contains an implementation class for a binary_heap.
 */

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
clear()
{
  for (size_type i = 0; i < m_size; ++i)
    erase_at(m_a_entries, i, s_no_throw_copies_ind);

  try
    {
      const size_type actual_size = resize_policy::get_new_size_for_arbitrary(0);

      entry_pointer a_entries = s_entry_allocator.allocate(actual_size);

      resize_policy::notify_arbitrary(actual_size);

      s_entry_allocator.deallocate(m_a_entries, m_actual_size);

      m_actual_size = actual_size;

      m_a_entries = a_entries;
    }
  catch(...)
    { }

  m_size = 0;

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
erase_at(entry_pointer a_entries, size_type i, false_type)
{
  a_entries[i]->~value_type();

  s_value_allocator.deallocate(a_entries[i], 1);
}

PB_DS_CLASS_T_DEC
void
PB_DS_CLASS_C_DEC::
erase_at(entry_pointer, size_type, true_type)
{ }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
pop()
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    _GLIBCXX_DEBUG_ASSERT(!empty());

  erase_at(m_a_entries, 0, s_no_throw_copies_ind);

  std::pop_heap(m_a_entries, m_a_entries + m_size, static_cast<entry_cmp& >(*this));

  resize_for_erase_if_needed();

  _GLIBCXX_DEBUG_ASSERT(m_size > 0);
  --m_size;

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

PB_DS_CLASS_T_DEC
template<typename Pred>
typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
erase_if(Pred pred)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)

    typedef
    typename entry_pred<
    value_type,
    Pred,
    simple_value,
    Allocator>::type
    pred_t;

  const size_type left = partition(pred_t(pred));

  _GLIBCXX_DEBUG_ASSERT(m_size >= left);

  const size_type ersd = m_size - left;

  for (size_type i = left; i < m_size; ++i)
    erase_at(m_a_entries, i, s_no_throw_copies_ind);

  try
    {
      const size_type actual_size =
	resize_policy::get_new_size_for_arbitrary(left);

      entry_pointer a_entries = s_entry_allocator.allocate(actual_size);

      std::copy(m_a_entries, m_a_entries + left, a_entries);

      s_entry_allocator.deallocate(m_a_entries, m_actual_size);

      m_actual_size = actual_size;

      resize_policy::notify_arbitrary(m_actual_size);
    }
  catch(...)
    { };

  m_size = left;

  std::make_heap(m_a_entries, m_a_entries + m_size, static_cast<entry_cmp& >(*this));

  _GLIBCXX_DEBUG_ONLY(assert_valid();)

    return ersd;
}

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
erase(point_iterator it)
{
  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    _GLIBCXX_DEBUG_ASSERT(!empty());

  const size_type fix_pos = it.m_p_e - m_a_entries;

  std::swap(*it.m_p_e, m_a_entries[m_size - 1]);

  erase_at(m_a_entries, m_size - 1, s_no_throw_copies_ind);

  resize_for_erase_if_needed();

  _GLIBCXX_DEBUG_ASSERT(m_size > 0);
  --m_size;

  _GLIBCXX_DEBUG_ASSERT(fix_pos <= m_size);

  if (fix_pos != m_size)
    fix(m_a_entries + fix_pos);

  _GLIBCXX_DEBUG_ONLY(assert_valid();)
    }

PB_DS_CLASS_T_DEC
inline void
PB_DS_CLASS_C_DEC::
resize_for_erase_if_needed()
{
  if (!resize_policy::resize_needed_for_shrink(m_size))
    return;

  try
    {
      const size_type new_actual_size =
	resize_policy::get_new_size_for_shrink();

      entry_pointer a_new_entries = s_entry_allocator.allocate(new_actual_size);

      resize_policy::notify_shrink_resize();

      _GLIBCXX_DEBUG_ASSERT(m_size > 0);
      std::copy(m_a_entries, m_a_entries + m_size - 1, a_new_entries);

      s_entry_allocator.deallocate(m_a_entries, m_actual_size);

      m_actual_size = new_actual_size;

      m_a_entries = a_new_entries;
    }
  catch(...)
    { }
}

PB_DS_CLASS_T_DEC
template<typename Pred>
typename PB_DS_CLASS_C_DEC::size_type
PB_DS_CLASS_C_DEC::
partition(Pred pred)
{
  size_type left = 0;
  size_type right = m_size - 1;

  while (right + 1 != left)
    {
      _GLIBCXX_DEBUG_ASSERT(left <= m_size);

      if (!pred(m_a_entries[left]))
	++left;
      else if (pred(m_a_entries[right]))
	--right;
      else
        {
	  _GLIBCXX_DEBUG_ASSERT(left < right);

	  std::swap(m_a_entries[left], m_a_entries[right]);

	  ++left;
	  --right;
        }
    }

  return left;
}

