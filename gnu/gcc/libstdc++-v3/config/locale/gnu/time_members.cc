// std::time_get, std::time_put implementation, GNU version -*- C++ -*-

// Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.
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
// Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
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
#include <bits/c++locale_internal.h>

_GLIBCXX_BEGIN_NAMESPACE(std)

  template<>
    void
    __timepunct<char>::
    _M_put(char* __s, size_t __maxlen, const char* __format, 
	   const tm* __tm) const
    {
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
      const size_t __len = __strftime_l(__s, __maxlen, __format, __tm,
					_M_c_locale_timepunct);
#else
      char* __old = strdup(setlocale(LC_ALL, NULL));
      setlocale(LC_ALL, _M_name_timepunct);
      const size_t __len = strftime(__s, __maxlen, __format, __tm);
      setlocale(LC_ALL, __old);
      free(__old);
#endif
      // Make sure __s is null terminated.
      if (__len == 0)
	__s[0] = '\0';
    }

  template<> 
    void
    __timepunct<char>::_M_initialize_timepunct(__c_locale __cloc)
    {
      if (!_M_data)
	_M_data = new __timepunct_cache<char>;

      if (!__cloc)
	{
	  // "C" locale
	  _M_c_locale_timepunct = _S_get_c_locale();

	  _M_data->_M_date_format = "%m/%d/%y";
	  _M_data->_M_date_era_format = "%m/%d/%y";
	  _M_data->_M_time_format = "%H:%M:%S";
	  _M_data->_M_time_era_format = "%H:%M:%S";
	  _M_data->_M_date_time_format = "";
	  _M_data->_M_date_time_era_format = "";
	  _M_data->_M_am = "AM";
	  _M_data->_M_pm = "PM";
	  _M_data->_M_am_pm_format = "";

	  // Day names, starting with "C"'s Sunday.
	  _M_data->_M_day1 = "Sunday";
	  _M_data->_M_day2 = "Monday";
	  _M_data->_M_day3 = "Tuesday";
	  _M_data->_M_day4 = "Wednesday";
	  _M_data->_M_day5 = "Thursday";
	  _M_data->_M_day6 = "Friday";
	  _M_data->_M_day7 = "Saturday";

	  // Abbreviated day names, starting with "C"'s Sun.
	  _M_data->_M_aday1 = "Sun";
	  _M_data->_M_aday2 = "Mon";
	  _M_data->_M_aday3 = "Tue";
	  _M_data->_M_aday4 = "Wed";
	  _M_data->_M_aday5 = "Thu";
	  _M_data->_M_aday6 = "Fri";
	  _M_data->_M_aday7 = "Sat";

	  // Month names, starting with "C"'s January.
	  _M_data->_M_month01 = "January";
	  _M_data->_M_month02 = "February";
	  _M_data->_M_month03 = "March";
	  _M_data->_M_month04 = "April";
	  _M_data->_M_month05 = "May";
	  _M_data->_M_month06 = "June";
	  _M_data->_M_month07 = "July";
	  _M_data->_M_month08 = "August";
	  _M_data->_M_month09 = "September";
	  _M_data->_M_month10 = "October";
	  _M_data->_M_month11 = "November";
	  _M_data->_M_month12 = "December";

	  // Abbreviated month names, starting with "C"'s Jan.
	  _M_data->_M_amonth01 = "Jan";
	  _M_data->_M_amonth02 = "Feb";
	  _M_data->_M_amonth03 = "Mar";
	  _M_data->_M_amonth04 = "Apr";
	  _M_data->_M_amonth05 = "May";
	  _M_data->_M_amonth06 = "Jun";
	  _M_data->_M_amonth07 = "Jul";
	  _M_data->_M_amonth08 = "Aug";
	  _M_data->_M_amonth09 = "Sep";
	  _M_data->_M_amonth10 = "Oct";
	  _M_data->_M_amonth11 = "Nov";
	  _M_data->_M_amonth12 = "Dec";
	}
      else
	{
	  _M_c_locale_timepunct = _S_clone_c_locale(__cloc); 

	  _M_data->_M_date_format = __nl_langinfo_l(D_FMT, __cloc);
	  _M_data->_M_date_era_format = __nl_langinfo_l(ERA_D_FMT, __cloc);
	  _M_data->_M_time_format = __nl_langinfo_l(T_FMT, __cloc);
	  _M_data->_M_time_era_format = __nl_langinfo_l(ERA_T_FMT, __cloc);
	  _M_data->_M_date_time_format = __nl_langinfo_l(D_T_FMT, __cloc);
	  _M_data->_M_date_time_era_format = __nl_langinfo_l(ERA_D_T_FMT,
							     __cloc);
	  _M_data->_M_am = __nl_langinfo_l(AM_STR, __cloc);
	  _M_data->_M_pm = __nl_langinfo_l(PM_STR, __cloc);
	  _M_data->_M_am_pm_format = __nl_langinfo_l(T_FMT_AMPM, __cloc);

	  // Day names, starting with "C"'s Sunday.
	  _M_data->_M_day1 = __nl_langinfo_l(DAY_1, __cloc);
	  _M_data->_M_day2 = __nl_langinfo_l(DAY_2, __cloc);
	  _M_data->_M_day3 = __nl_langinfo_l(DAY_3, __cloc);
	  _M_data->_M_day4 = __nl_langinfo_l(DAY_4, __cloc);
	  _M_data->_M_day5 = __nl_langinfo_l(DAY_5, __cloc);
	  _M_data->_M_day6 = __nl_langinfo_l(DAY_6, __cloc);
	  _M_data->_M_day7 = __nl_langinfo_l(DAY_7, __cloc);

	  // Abbreviated day names, starting with "C"'s Sun.
	  _M_data->_M_aday1 = __nl_langinfo_l(ABDAY_1, __cloc);
	  _M_data->_M_aday2 = __nl_langinfo_l(ABDAY_2, __cloc);
	  _M_data->_M_aday3 = __nl_langinfo_l(ABDAY_3, __cloc);
	  _M_data->_M_aday4 = __nl_langinfo_l(ABDAY_4, __cloc);
	  _M_data->_M_aday5 = __nl_langinfo_l(ABDAY_5, __cloc);
	  _M_data->_M_aday6 = __nl_langinfo_l(ABDAY_6, __cloc);
	  _M_data->_M_aday7 = __nl_langinfo_l(ABDAY_7, __cloc);

	  // Month names, starting with "C"'s January.
	  _M_data->_M_month01 = __nl_langinfo_l(MON_1, __cloc);
	  _M_data->_M_month02 = __nl_langinfo_l(MON_2, __cloc);
	  _M_data->_M_month03 = __nl_langinfo_l(MON_3, __cloc);
	  _M_data->_M_month04 = __nl_langinfo_l(MON_4, __cloc);
	  _M_data->_M_month05 = __nl_langinfo_l(MON_5, __cloc);
	  _M_data->_M_month06 = __nl_langinfo_l(MON_6, __cloc);
	  _M_data->_M_month07 = __nl_langinfo_l(MON_7, __cloc);
	  _M_data->_M_month08 = __nl_langinfo_l(MON_8, __cloc);
	  _M_data->_M_month09 = __nl_langinfo_l(MON_9, __cloc);
	  _M_data->_M_month10 = __nl_langinfo_l(MON_10, __cloc);
	  _M_data->_M_month11 = __nl_langinfo_l(MON_11, __cloc);
	  _M_data->_M_month12 = __nl_langinfo_l(MON_12, __cloc);

	  // Abbreviated month names, starting with "C"'s Jan.
	  _M_data->_M_amonth01 = __nl_langinfo_l(ABMON_1, __cloc);
	  _M_data->_M_amonth02 = __nl_langinfo_l(ABMON_2, __cloc);
	  _M_data->_M_amonth03 = __nl_langinfo_l(ABMON_3, __cloc);
	  _M_data->_M_amonth04 = __nl_langinfo_l(ABMON_4, __cloc);
	  _M_data->_M_amonth05 = __nl_langinfo_l(ABMON_5, __cloc);
	  _M_data->_M_amonth06 = __nl_langinfo_l(ABMON_6, __cloc);
	  _M_data->_M_amonth07 = __nl_langinfo_l(ABMON_7, __cloc);
	  _M_data->_M_amonth08 = __nl_langinfo_l(ABMON_8, __cloc);
	  _M_data->_M_amonth09 = __nl_langinfo_l(ABMON_9, __cloc);
	  _M_data->_M_amonth10 = __nl_langinfo_l(ABMON_10, __cloc);
	  _M_data->_M_amonth11 = __nl_langinfo_l(ABMON_11, __cloc);
	  _M_data->_M_amonth12 = __nl_langinfo_l(ABMON_12, __cloc);
	}
    }

#ifdef _GLIBCXX_USE_WCHAR_T
  template<>
    void
    __timepunct<wchar_t>::
    _M_put(wchar_t* __s, size_t __maxlen, const wchar_t* __format, 
	   const tm* __tm) const
    {
#if __GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 2)
      const size_t __len = __wcsftime_l(__s, __maxlen, __format, __tm,
					_M_c_locale_timepunct);
#else
      char* __old = strdup(setlocale(LC_ALL, NULL));
      setlocale(LC_ALL, _M_name_timepunct);
      const size_t __len = wcsftime(__s, __maxlen, __format, __tm);
      setlocale(LC_ALL, __old);
      free(__old);
#endif
      // Make sure __s is null terminated.
      if (__len == 0)
	__s[0] = L'\0';
    }

  template<> 
    void
    __timepunct<wchar_t>::_M_initialize_timepunct(__c_locale __cloc)
    {
      if (!_M_data)
	_M_data = new __timepunct_cache<wchar_t>;

      if (!__cloc)
	{
	  // "C" locale
	  _M_c_locale_timepunct = _S_get_c_locale();

	  _M_data->_M_date_format = L"%m/%d/%y";
	  _M_data->_M_date_era_format = L"%m/%d/%y";
	  _M_data->_M_time_format = L"%H:%M:%S";
	  _M_data->_M_time_era_format = L"%H:%M:%S";
	  _M_data->_M_date_time_format = L"";
	  _M_data->_M_date_time_era_format = L"";
	  _M_data->_M_am = L"AM";
	  _M_data->_M_pm = L"PM";
	  _M_data->_M_am_pm_format = L"";

	  // Day names, starting with "C"'s Sunday.
	  _M_data->_M_day1 = L"Sunday";
	  _M_data->_M_day2 = L"Monday";
	  _M_data->_M_day3 = L"Tuesday";
	  _M_data->_M_day4 = L"Wednesday";
	  _M_data->_M_day5 = L"Thursday";
	  _M_data->_M_day6 = L"Friday";
	  _M_data->_M_day7 = L"Saturday";

	  // Abbreviated day names, starting with "C"'s Sun.
	  _M_data->_M_aday1 = L"Sun";
	  _M_data->_M_aday2 = L"Mon";
	  _M_data->_M_aday3 = L"Tue";
	  _M_data->_M_aday4 = L"Wed";
	  _M_data->_M_aday5 = L"Thu";
	  _M_data->_M_aday6 = L"Fri";
	  _M_data->_M_aday7 = L"Sat";

	  // Month names, starting with "C"'s January.
	  _M_data->_M_month01 = L"January";
	  _M_data->_M_month02 = L"February";
	  _M_data->_M_month03 = L"March";
	  _M_data->_M_month04 = L"April";
	  _M_data->_M_month05 = L"May";
	  _M_data->_M_month06 = L"June";
	  _M_data->_M_month07 = L"July";
	  _M_data->_M_month08 = L"August";
	  _M_data->_M_month09 = L"September";
	  _M_data->_M_month10 = L"October";
	  _M_data->_M_month11 = L"November";
	  _M_data->_M_month12 = L"December";

	  // Abbreviated month names, starting with "C"'s Jan.
	  _M_data->_M_amonth01 = L"Jan";
	  _M_data->_M_amonth02 = L"Feb";
	  _M_data->_M_amonth03 = L"Mar";
	  _M_data->_M_amonth04 = L"Apr";
	  _M_data->_M_amonth05 = L"May";
	  _M_data->_M_amonth06 = L"Jun";
	  _M_data->_M_amonth07 = L"Jul";
	  _M_data->_M_amonth08 = L"Aug";
	  _M_data->_M_amonth09 = L"Sep";
	  _M_data->_M_amonth10 = L"Oct";
	  _M_data->_M_amonth11 = L"Nov";
	  _M_data->_M_amonth12 = L"Dec";
	}
      else
	{
	  _M_c_locale_timepunct = _S_clone_c_locale(__cloc); 

	  union { char *__s; wchar_t *__w; } __u;

	  __u.__s = __nl_langinfo_l(_NL_WD_FMT, __cloc);
	  _M_data->_M_date_format = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WERA_D_FMT, __cloc);
	  _M_data->_M_date_era_format = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WT_FMT, __cloc);
	  _M_data->_M_time_format = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WERA_T_FMT, __cloc);
	  _M_data->_M_time_era_format = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WD_T_FMT, __cloc);
	  _M_data->_M_date_time_format = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WERA_D_T_FMT, __cloc);
	  _M_data->_M_date_time_era_format = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WAM_STR, __cloc);
	  _M_data->_M_am = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WPM_STR, __cloc);
	  _M_data->_M_pm = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WT_FMT_AMPM, __cloc);
	  _M_data->_M_am_pm_format = __u.__w;

	  // Day names, starting with "C"'s Sunday.
	  __u.__s = __nl_langinfo_l(_NL_WDAY_1, __cloc);
	  _M_data->_M_day1 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WDAY_2, __cloc);
	  _M_data->_M_day2 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WDAY_3, __cloc);
	  _M_data->_M_day3 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WDAY_4, __cloc);
	  _M_data->_M_day4 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WDAY_5, __cloc);
	  _M_data->_M_day5 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WDAY_6, __cloc);
	  _M_data->_M_day6 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WDAY_7, __cloc);
	  _M_data->_M_day7 = __u.__w;

	  // Abbreviated day names, starting with "C"'s Sun.
	  __u.__s = __nl_langinfo_l(_NL_WABDAY_1, __cloc);
	  _M_data->_M_aday1 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABDAY_2, __cloc);
	  _M_data->_M_aday2 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABDAY_3, __cloc);
	  _M_data->_M_aday3 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABDAY_4, __cloc);
	  _M_data->_M_aday4 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABDAY_5, __cloc);
	  _M_data->_M_aday5 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABDAY_6, __cloc);
	  _M_data->_M_aday6 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABDAY_7, __cloc);
	  _M_data->_M_aday7 = __u.__w;

	  // Month names, starting with "C"'s January.
	  __u.__s = __nl_langinfo_l(_NL_WMON_1, __cloc);
	  _M_data->_M_month01 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WMON_2, __cloc);
	  _M_data->_M_month02 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WMON_3, __cloc);
	  _M_data->_M_month03 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WMON_4, __cloc);
	  _M_data->_M_month04 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WMON_5, __cloc);
	  _M_data->_M_month05 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WMON_6, __cloc);
	  _M_data->_M_month06 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WMON_7, __cloc);
	  _M_data->_M_month07 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WMON_8, __cloc);
	  _M_data->_M_month08 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WMON_9, __cloc);
	  _M_data->_M_month09 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WMON_10, __cloc);
	  _M_data->_M_month10 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WMON_11, __cloc);
	  _M_data->_M_month11 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WMON_12, __cloc);
	  _M_data->_M_month12 = __u.__w;

	  // Abbreviated month names, starting with "C"'s Jan.
	  __u.__s = __nl_langinfo_l(_NL_WABMON_1, __cloc);
	  _M_data->_M_amonth01 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABMON_2, __cloc);
	  _M_data->_M_amonth02 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABMON_3, __cloc);
	  _M_data->_M_amonth03 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABMON_4, __cloc);
	  _M_data->_M_amonth04 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABMON_5, __cloc);
	  _M_data->_M_amonth05 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABMON_6, __cloc);
	  _M_data->_M_amonth06 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABMON_7, __cloc);
	  _M_data->_M_amonth07 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABMON_8, __cloc);
	  _M_data->_M_amonth08 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABMON_9, __cloc);
	  _M_data->_M_amonth09 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABMON_10, __cloc);
	  _M_data->_M_amonth10 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABMON_11, __cloc);
	  _M_data->_M_amonth11 = __u.__w;
	  __u.__s = __nl_langinfo_l(_NL_WABMON_12, __cloc);
	  _M_data->_M_amonth12 = __u.__w;
	}
    }
#endif

_GLIBCXX_END_NAMESPACE
