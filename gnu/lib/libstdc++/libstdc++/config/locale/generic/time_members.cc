// std::time_get, std::time_put implementation, generic version -*- C++ -*-

// Copyright (C) 2001, 2002 Free Software Foundation, Inc.
//
// This file is part of the GNU ISO C++ Library.  This library is free
// software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the
// Free Software Foundation; either version 2, or (at your option)
// any later version.

// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License along
// with this library; see the file COPYING.  If not, write to the Free
// Software Foundation, 59 Temple Place - Suite 330, Boston, MA 02111-1307,
// USA.

// As a special exception, you may use this file as part of a free software
// library without restriction.  Specifically, if other files instantiate
// templates or use macros or inline functions from this file, or you compile
// this file and link it with other files to produce an executable, this
// file does not by itself cause the resulting executable to be covered by
// the GNU General Public License.  This exception does not however
// invalidate any other reasons why the executable file might be covered by
// the GNU General Public License.

//
// ISO C++ 14882: 22.2.5.1.2 - time_get virtual functions
// ISO C++ 14882: 22.2.5.3.2 - time_put virtual functions
//

// Written by Benjamin Kosnik <bkoz@redhat.com>

#include <locale>

namespace std
{
  template<>
    void
    __timepunct<char>::
    _M_put(char* __s, size_t __maxlen, const char* __format, 
	   const tm* __tm) const
    {
      char* __old = strdup(setlocale(LC_ALL, NULL));
      setlocale(LC_ALL, _M_name_timepunct);
      strftime(__s, __maxlen, __format, __tm);
      setlocale(LC_ALL, __old);
      free(__old);
    }

  template<> 
    void
    __timepunct<char>::_M_initialize_timepunct(__c_locale)
    { 
      // "C" locale
      _M_date_format = "%m/%d/%y";
      _M_date_era_format = "%m/%d/%y";
      _M_time_format = "%H:%M:%S";
      _M_time_era_format = "%H:%M:%S";
      _M_date_time_format = "";
      _M_date_time_era_format = "";
      _M_am = "AM";
      _M_pm = "PM";
      _M_am_pm_format = "";
	  
      // Day names, starting with "C"'s Sunday.
      _M_day1 = "Sunday";
      _M_day2 = "Monday";
      _M_day3 = "Tuesday";
      _M_day4 = "Wednesday";
      _M_day5 = "Thursday";
      _M_day6 = "Friday";
      _M_day7 = "Saturday";

      // Abbreviated day names, starting with "C"'s Sun.
      _M_day_a1 = "Sun";
      _M_day_a2 = "Mon";
      _M_day_a3 = "Tue";
      _M_day_a4 = "Wed";
      _M_day_a5 = "Thu";
      _M_day_a6 = "Fri";
      _M_day_a7 = "Sat";

      // Month names, starting with "C"'s January.
      _M_month01 = "January";
      _M_month02 = "February";
      _M_month03 = "March";
      _M_month04 = "April";
      _M_month05 = "May";
      _M_month06 = "June";
      _M_month07 = "July";
      _M_month08 = "August";
      _M_month09 = "September";
      _M_month10 = "October";
      _M_month11 = "November";
      _M_month12 = "December";

      // Abbreviated month names, starting with "C"'s Jan.
      _M_month_a01 = "Jan";
      _M_month_a02 = "Feb";
      _M_month_a03 = "Mar";
      _M_month_a04 = "Apr";
      _M_month_a05 = "May";
      _M_month_a06 = "Jun";
      _M_month_a07 = "July";
      _M_month_a08 = "Aug";
      _M_month_a09 = "Sep";
      _M_month_a10 = "Oct";
      _M_month_a11 = "Nov";
      _M_month_a12 = "Dec";
    }

#if defined(_GLIBCPP_USE_WCHAR_T) || defined(_GLIBCPP_USE_TYPE_WCHAR_T)
  template<>
    void
    __timepunct<wchar_t>::
    _M_put(wchar_t* __s, size_t __maxlen, const wchar_t* __format, 
	   const tm* __tm) const
    {
      char* __old = strdup(setlocale(LC_ALL, NULL));
      setlocale(LC_ALL, _M_name_timepunct);
# if defined(_GLIBCPP_USE_WCHAR_T)
      wcsftime(__s, __maxlen, __format, __tm);
# endif
      setlocale(LC_ALL, __old);
      free(__old);
    }

  template<> 
    void
    __timepunct<wchar_t>::_M_initialize_timepunct(__c_locale)
    {
      // "C" locale
      _M_date_format = L"%m/%d/%y";
      _M_date_era_format = L"%m/%d/%y";
      _M_time_format = L"%H:%M:%S";
      _M_time_era_format = L"%H:%M:%S";
      _M_date_time_format = L"";
      _M_date_time_era_format = L"";
      _M_am = L"AM";
      _M_pm = L"PM";
      _M_am_pm_format = L"";

      // Day names, starting with "C"'s Sunday.
      _M_day1 = L"Sunday";
      _M_day2 = L"Monday";
      _M_day3 = L"Tuesday";
      _M_day4 = L"Wednesday";
      _M_day5 = L"Thursday";
      _M_day6 = L"Friday";
      _M_day7 = L"Saturday";

      // Abbreviated day names, starting with "C"'s Sun.
      _M_day_a1 = L"Sun";
      _M_day_a2 = L"Mon";
      _M_day_a3 = L"Tue";
      _M_day_a4 = L"Wed";
      _M_day_a5 = L"Thu";
      _M_day_a6 = L"Fri";
      _M_day_a7 = L"Sat";

      // Month names, starting with "C"'s January.
      _M_month01 = L"January";
      _M_month02 = L"February";
      _M_month03 = L"March";
      _M_month04 = L"April";
      _M_month05 = L"May";
      _M_month06 = L"June";
      _M_month07 = L"July";
      _M_month08 = L"August";
      _M_month09 = L"September";
      _M_month10 = L"October";
      _M_month11 = L"November";
      _M_month12 = L"December";

      // Abbreviated month names, starting with "C"'s Jan.
      _M_month_a01 = L"Jan";
      _M_month_a02 = L"Feb";
      _M_month_a03 = L"Mar";
      _M_month_a04 = L"Apr";
      _M_month_a05 = L"May";
      _M_month_a06 = L"Jun";
      _M_month_a07 = L"July";
      _M_month_a08 = L"Aug";
      _M_month_a09 = L"Sep";
      _M_month_a10 = L"Oct";
      _M_month_a11 = L"Nov";
      _M_month_a12 = L"Dec";
    }
#endif
}
