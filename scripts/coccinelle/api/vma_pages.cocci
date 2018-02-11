// SPDX-License-Identifier: GPL-2.0
///
/// Use vma_pages function on vma object instead of explicit computation.
///
//  Confidence: High
//  Keywords: vma_pages vma
//  Comment: Based on resource_size.cocci

virtual context
virtual patch
virtual org
virtual report

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@r_context depends on context && !patch && !org && !report@
struct vm_area_struct *vma;
@@

* (vma->vm_end - vma->vm_start) >> PAGE_SHIFT

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@r_patch depends on !context && patch && !org && !report@
struct vm_area_struct *vma;
@@

- ((vma->vm_end - vma->vm_start) >> PAGE_SHIFT)
+ vma_pages(vma)

//----------------------------------------------------------
//  For org mode
//----------------------------------------------------------

@r_org depends on !context && !patch && (org || report)@
struct vm_area_struct *vma;
position p;
@@

  (vma->vm_end@p - vma->vm_start) >> PAGE_SHIFT

@script:python depends on report@
p << r_org.p;
x << r_org.vma;
@@

msg="WARNING: Consider using vma_pages helper on %s" % (x)
coccilib.report.print_report(p[0], msg)

@script:python depends on org@
p << r_org.p;
x << r_org.vma;
@@

msg="WARNING: Consider using vma_pages helper on %s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)
