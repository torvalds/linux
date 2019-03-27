$FreeBSD$

A small tool to do the statistics legwork on benchmarks etc.

Prepare your data into two files, one number per line
run 
	./ministat data_before data_after

and see what it says.

You need at least three data points in each data set, but the more
you have the better your result generally gets.

Here are two typical outputs:

x _1
+ _2
+--------------------------------------------------------------------------+
|x            +    x+      x            x   x             +           ++   |
|        |_________|______AM_______________|__A___________M_______________||
+--------------------------------------------------------------------------+
    N           Min           Max        Median           Avg        Stddev
x   5         36060         36138         36107       36105.6     31.165686
+   5         36084         36187         36163       36142.6     49.952978
No difference proven at 95.0% confidence

Here nothing can be concluded from the numbers.  It _may_ be possible to
prove something if many more measurements are made, but with only five
measurements, nothing is proven.


x _1
+ _2
+--------------------------------------------------------------------------+
|                                                               +          |
|                               x                               +         +|
|x                    x         x          x                    +         +|
|         |_______________A_____M_________|                   |_M___A____| |
+--------------------------------------------------------------------------+
    N           Min           Max        Median           Avg        Stddev
x   5         0.133         0.137         0.136        0.1354  0.0015165751
+   5         0.139          0.14         0.139        0.1394 0.00054772256
Difference at 95.0% confidence
        0.004 +/- 0.00166288
        2.95421% +/- 1.22812%
        (Student's t, pooled s = 0.00114018)

Here we have a clearcut difference, not very big, but clear and unambiguous.


