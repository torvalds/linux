#
# Makefile for the Linux filesystems.
#
# 14 Sep 2000, Christoph Hellwig <hch@infradead.org>
# Rewritten to use lists instead of if-statements.
#

obj-y :=

obj-$(CONFIG_QUOTA)		+= dquot.o
obj-$(CONFIG_QFMT_V1)		+= quota_v1.o
obj-$(CONFIG_QFMT_V2)		+= quota_v2.o
obj-$(CONFIG_QUOTA_TREE)	+= quota_tree.o
obj-$(CONFIG_QUOTACTL)		+= quota.o
