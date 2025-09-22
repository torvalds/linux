#!/usr/bin/env python
# ===- test.py -  ---------------------------------------------*- python -*--===#
#
# Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
# See https://llvm.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# ===------------------------------------------------------------------------===#

from cppreference_parser import _ParseSymbolPage, _ParseIndexPage

import unittest


class TestStdGen(unittest.TestCase):
    def testParseIndexPage(self):
        html = """
 <a href="abs.html" title="abs"><tt>abs()</tt></a> (int) <br>
 <a href="complex/abs.html" title="abs"><tt>abs&lt;&gt;()</tt></a> (std::complex) <br>
 <a href="acos.html" title="acos"><tt>acos()</tt></a> <br>
 <a href="acosh.html" title="acosh"><tt>acosh()</tt></a> <span class="t-mark-rev">(since C++11)</span> <br>
 <a href="as_bytes.html" title="as bytes"><tt>as_bytes&lt;&gt;()</tt></a> <span class="t-mark-rev t-since-cxx20">(since C++20)</span> <br>
 """

        actual = _ParseIndexPage(html)
        expected = [
            ("abs", "abs.html", "int"),
            ("abs", "complex/abs.html", "std::complex"),
            ("acos", "acos.html", None),
            ("acosh", "acosh.html", None),
            ("as_bytes", "as_bytes.html", None),
        ]
        self.assertEqual(len(actual), len(expected))
        for i in range(0, len(actual)):
            self.assertEqual(expected[i][0], actual[i][0])
            self.assertTrue(actual[i][1].endswith(expected[i][1]))
            self.assertEqual(expected[i][2], actual[i][2])

    def testParseSymbolPage_SingleHeader(self):
        # Defined in header <cmath>
        html = """
 <table class="t-dcl-begin"><tbody>
  <tr class="t-dsc-header">
  <td> <div>Defined in header <code><a href="cmath.html" title="cmath">&lt;cmath&gt;</a></code>
   </div></td>
  <td></td>
  <td></td>
  </tr>
  <tr class="t-dcl">
    <td>void foo()</td>
    <td>this is matched</td>
  </tr>
</tbody></table>
"""
        self.assertEqual(_ParseSymbolPage(html, "foo"), set(["<cmath>"]))

    def testParseSymbolPage_MulHeaders(self):
        #  Defined in header <cstddef>
        #  Defined in header <cstdio>
        #  Defined in header <cstdlib>
        html = """
<table class="t-dcl-begin"><tbody>
  <tr class="t-dsc-header">
    <td> <div>Defined in header <code><a href="cstddef.html" title="cstddef">&lt;cstddef&gt;</a></code>
     </div></td>
     <td></td>
    <td></td>
  </tr>
  <tr class="t-dcl">
    <td>void bar()</td>
    <td>this mentions foo, but isn't matched</td>
  </tr>
  <tr class="t-dsc-header">
    <td> <div>Defined in header <code><a href="cstdio.html" title="cstdio">&lt;cstdio&gt;</a></code>
     </div></td>
    <td></td>
    <td></td>
  </tr>
  <tr class="t-dsc-header">
    <td> <div>Defined in header <code><a href=".cstdlib.html" title="ccstdlib">&lt;cstdlib&gt;</a></code>
     </div></td>
    <td></td>
    <td></td>
  </tr>
  <tr class="t-dcl">
    <td>
      <span>void</span>
      foo
      <span>()</span>
    </td>
    <td>this is matched</td>
  </tr>
</tbody></table>
"""
        self.assertEqual(_ParseSymbolPage(html, "foo"), set(["<cstdio>", "<cstdlib>"]))

    def testParseSymbolPage_MulHeadersInSameDiv(self):
        # Multile <code> blocks in a Div.
        # Defined in header <algorithm>
        # Defined in header <utility>
        html = """
<table class="t-dcl-begin"><tbody>
<tr class="t-dsc-header">
<td><div>
     Defined in header <code><a href="../header/algorithm.html" title="cpp/header/algorithm">&lt;algorithm&gt;</a></code><br>
     Defined in header <code><a href="../header/utility.html" title="cpp/header/utility">&lt;utility&gt;</a></code>
</div></td>
<td></td>
</tr>
<tr class="t-dcl">
  <td>
    <span>void</span>
    foo
    <span>()</span>
  </td>
  <td>this is matched</td>
</tr>
</tbody></table>
"""
        self.assertEqual(
            _ParseSymbolPage(html, "foo"), set(["<algorithm>", "<utility>"])
        )

    def testParseSymbolPage_MulSymbolsInSameTd(self):
        # defined in header <cstdint>
        #   int8_t
        #   int16_t
        html = """
<table class="t-dcl-begin"><tbody>
<tr class="t-dsc-header">
<td><div>
     Defined in header <code><a href="cstdint.html" title="cstdint">&lt;cstdint&gt;</a></code><br>
</div></td>
<td></td>
</tr>
<tr class="t-dcl">
  <td>
    <span>int8_t</span>
    <span>int16_t</span>
  </td>
  <td>this is matched</td>
</tr>
</tbody></table>
"""
        self.assertEqual(_ParseSymbolPage(html, "int8_t"), set(["<cstdint>"]))
        self.assertEqual(_ParseSymbolPage(html, "int16_t"), set(["<cstdint>"]))


if __name__ == "__main__":
    unittest.main()
