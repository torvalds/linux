# $FreeBSD$
#
# A unsorted set of macro expansions. This should be sorted out.
#
A	= 0
AV	= 1
test1:
	@echo $A
	@echo ${AV}
	@echo ${A}
	@# The following are soo broken why no syntax error?
	@echo $(
	@echo $)
	@echo ${
	@echo ${A
	@echo ${A)
	@echo ${A){
	@echo ${AV
	@echo ${AV)
	@echo ${AV){
	@echo ${AV{
	@echo ${A{
	@echo $}
