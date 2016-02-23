///
/// Use *_pool_zalloc rather than *_pool_alloc followed by memset with 0
///
// Copyright: (C) 2015 Intel Corp.  GPLv2.
// Options: --no-includes --include-headers
//
// Keywords: dma_pool_zalloc, pci_pool_zalloc
//

virtual context
virtual patch
virtual org
virtual report

//----------------------------------------------------------
//  For context mode
//----------------------------------------------------------

@depends on context@
expression x;
statement S;
@@

* x = \(dma_pool_alloc\|pci_pool_alloc\)(...);
  if ((x==NULL) || ...) S
* memset(x,0, ...);

//----------------------------------------------------------
//  For patch mode
//----------------------------------------------------------

@depends on patch@
expression x;
expression a,b,c;
statement S;
@@

- x = dma_pool_alloc(a,b,c);
+ x = dma_pool_zalloc(a,b,c);
  if ((x==NULL) || ...) S
- memset(x,0,...);

@depends on patch@
expression x;
expression a,b,c;
statement S;
@@

- x = pci_pool_alloc(a,b,c);
+ x = pci_pool_zalloc(a,b,c);
  if ((x==NULL) || ...) S
- memset(x,0,...);

//----------------------------------------------------------
//  For org and report mode
//----------------------------------------------------------

@r depends on org || report@
expression x;
expression a,b,c;
statement S;
position p;
@@

 x = @p\(dma_pool_alloc\|pci_pool_alloc\)(a,b,c);
 if ((x==NULL) || ...) S
 memset(x,0, ...);

@script:python depends on org@
p << r.p;
x << r.x;
@@

msg="%s" % (x)
msg_safe=msg.replace("[","@(").replace("]",")")
coccilib.org.print_todo(p[0], msg_safe)

@script:python depends on report@
p << r.p;
x << r.x;
@@

msg="WARNING: *_pool_zalloc should be used for %s, instead of *_pool_alloc/memset" % (x)
coccilib.report.print_report(p[0], msg)
