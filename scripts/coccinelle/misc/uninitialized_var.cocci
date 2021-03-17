// SPDX-License-Identifier: GPL-2.0-only
///
/// Please, don't reintroduce uninitialized_var().
/// From Documentation/process/deprecated.rst:
///  For any compiler warnings about uninitialized variables, just add
///  an initializer. Using warning-silencing tricks is dangerous as it
///  papers over real bugs (or can in the future), and suppresses unrelated
///  compiler warnings (e.g. "unused variable"). If the compiler thinks it
///  is uninitialized, either simply initialize the variable or make compiler
///  changes. Keep in mind that in most cases, if an initialization is
///  obviously redundant, the compiler's dead-store elimination pass will make
///  sure there are no needless variable writes.
///
// Confidence: High
// Copyright: (C) 2020 Denis Efremov ISPRAS
// Options: --no-includes --include-headers
//

virtual context
virtual report
virtual org

@r@
identifier var;
type T;
position p;
@@

(
* T var =@p var;
|
* T var =@p *(&(var));
|
* var =@p var
|
* var =@p *(&(var))
)

@script:python depends on report@
p << r.p;
@@

coccilib.report.print_report(p[0],
  "WARNING this kind of initialization is deprecated (https://www.kernel.org/doc/html/latest/process/deprecated.html#uninitialized-var)")

@script:python depends on org@
p << r.p;
@@

coccilib.org.print_todo(p[0],
  "WARNING this kind of initialization is deprecated (https://www.kernel.org/doc/html/latest/process/deprecated.html#uninitialized-var)")
