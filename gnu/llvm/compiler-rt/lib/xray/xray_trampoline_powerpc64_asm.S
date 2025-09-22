	.text
	.abiversion 2
	.globl	__xray_FunctionEntry
	.p2align	4
__xray_FunctionEntry:
	std 0, 16(1)
	stdu 1, -408(1)
# Spill r3-r10, f1-f13, and vsr34-vsr45, which are parameter registers.
# If this appears to be slow, the caller needs to pass in number of generic,
# floating point, and vector parameters, so that we only spill those live ones.
	std 3, 32(1)
	ld 3, 400(1) # FuncId
	std 4, 40(1)
	std 5, 48(1)
	std 6, 56(1)
	std 7, 64(1)
	std 8, 72(1)
	std 9, 80(1)
	std 10, 88(1)
	addi 4, 1, 96
	stxsdx 1, 0, 4
	addi 4, 1, 104
	stxsdx 2, 0, 4
	addi 4, 1, 112
	stxsdx 3, 0, 4
	addi 4, 1, 120
	stxsdx 4, 0, 4
	addi 4, 1, 128
	stxsdx 5, 0, 4
	addi 4, 1, 136
	stxsdx 6, 0, 4
	addi 4, 1, 144
	stxsdx 7, 0, 4
	addi 4, 1, 152
	stxsdx 8, 0, 4
	addi 4, 1, 160
	stxsdx 9, 0, 4
	addi 4, 1, 168
	stxsdx 10, 0, 4
	addi 4, 1, 176
	stxsdx 11, 0, 4
	addi 4, 1, 184
	stxsdx 12, 0, 4
	addi 4, 1, 192
	stxsdx 13, 0, 4
	addi 4, 1, 200
	stxvd2x 34, 0, 4
	addi 4, 1, 216
	stxvd2x 35, 0, 4
	addi 4, 1, 232
	stxvd2x 36, 0, 4
	addi 4, 1, 248
	stxvd2x 37, 0, 4
	addi 4, 1, 264
	stxvd2x 38, 0, 4
	addi 4, 1, 280
	stxvd2x 39, 0, 4
	addi 4, 1, 296
	stxvd2x 40, 0, 4
	addi 4, 1, 312
	stxvd2x 41, 0, 4
	addi 4, 1, 328
	stxvd2x 42, 0, 4
	addi 4, 1, 344
	stxvd2x 43, 0, 4
	addi 4, 1, 360
	stxvd2x 44, 0, 4
	addi 4, 1, 376
	stxvd2x 45, 0, 4
	std 2, 392(1)
	mflr 0
	std 0, 400(1)

	li 4, 0
	bl _ZN6__xray23CallXRayPatchedFunctionEi13XRayEntryType
	nop

	addi 4, 1, 96
	lxsdx 1, 0, 4
	addi 4, 1, 104
	lxsdx 2, 0, 4
	addi 4, 1, 112
	lxsdx 3, 0, 4
	addi 4, 1, 120
	lxsdx 4, 0, 4
	addi 4, 1, 128
	lxsdx 5, 0, 4
	addi 4, 1, 136
	lxsdx 6, 0, 4
	addi 4, 1, 144
	lxsdx 7, 0, 4
	addi 4, 1, 152
	lxsdx 8, 0, 4
	addi 4, 1, 160
	lxsdx 9, 0, 4
	addi 4, 1, 168
	lxsdx 10, 0, 4
	addi 4, 1, 176
	lxsdx 11, 0, 4
	addi 4, 1, 184
	lxsdx 12, 0, 4
	addi 4, 1, 192
	lxsdx 13, 0, 4
	addi 4, 1, 200
	lxvd2x 34, 0, 4
	addi 4, 1, 216
	lxvd2x 35, 0, 4
	addi 4, 1, 232
	lxvd2x 36, 0, 4
	addi 4, 1, 248
	lxvd2x 37, 0, 4
	addi 4, 1, 264
	lxvd2x 38, 0, 4
	addi 4, 1, 280
	lxvd2x 39, 0, 4
	addi 4, 1, 296
	lxvd2x 40, 0, 4
	addi 4, 1, 312
	lxvd2x 41, 0, 4
	addi 4, 1, 328
	lxvd2x 42, 0, 4
	addi 4, 1, 344
	lxvd2x 43, 0, 4
	addi 4, 1, 360
	lxvd2x 44, 0, 4
	addi 4, 1, 376
	lxvd2x 45, 0, 4
	ld 0, 400(1)
	mtlr 0
	ld 2, 392(1)
	ld 3, 32(1)
	ld 4, 40(1)
	ld 5, 48(1)
	ld 6, 56(1)
	ld 7, 64(1)
	ld 8, 72(1)
	ld 9, 80(1)
	ld 10, 88(1)

	addi 1, 1, 408
	ld 0, 16(1)
	blr

	.globl	__xray_FunctionExit
	.p2align	4
__xray_FunctionExit:
	std 0, 16(1)
	stdu 1, -256(1)
# Spill r3-r4, f1-f8, and vsr34-vsr41, which are return registers.
# If this appears to be slow, the caller needs to pass in number of generic,
# floating point, and vector parameters, so that we only spill those live ones.
	std 3, 32(1)
	ld 3, 248(1) # FuncId
	std 4, 40(1)
	addi 4, 1, 48
	stxsdx 1, 0, 4
	addi 4, 1, 56
	stxsdx 2, 0, 4
	addi 4, 1, 64
	stxsdx 3, 0, 4
	addi 4, 1, 72
	stxsdx 4, 0, 4
	addi 4, 1, 80
	stxsdx 5, 0, 4
	addi 4, 1, 88
	stxsdx 6, 0, 4
	addi 4, 1, 96
	stxsdx 7, 0, 4
	addi 4, 1, 104
	stxsdx 8, 0, 4
	addi 4, 1, 112
	stxvd2x 34, 0, 4
	addi 4, 1, 128
	stxvd2x 35, 0, 4
	addi 4, 1, 144
	stxvd2x 36, 0, 4
	addi 4, 1, 160
	stxvd2x 37, 0, 4
	addi 4, 1, 176
	stxvd2x 38, 0, 4
	addi 4, 1, 192
	stxvd2x 39, 0, 4
	addi 4, 1, 208
	stxvd2x 40, 0, 4
	addi 4, 1, 224
	stxvd2x 41, 0, 4
	std 2, 240(1)
	mflr 0
	std 0, 248(1)

	li 4, 1
	bl _ZN6__xray23CallXRayPatchedFunctionEi13XRayEntryType
	nop

	addi 4, 1, 48
	lxsdx 1, 0, 4
	addi 4, 1, 56
	lxsdx 2, 0, 4
	addi 4, 1, 64
	lxsdx 3, 0, 4
	addi 4, 1, 72
	lxsdx 4, 0, 4
	addi 4, 1, 80
	lxsdx 5, 0, 4
	addi 4, 1, 88
	lxsdx 6, 0, 4
	addi 4, 1, 96
	lxsdx 7, 0, 4
	addi 4, 1, 104
	lxsdx 8, 0, 4
	addi 4, 1, 112
	lxvd2x 34, 0, 4
	addi 4, 1, 128
	lxvd2x 35, 0, 4
	addi 4, 1, 144
	lxvd2x 36, 0, 4
	addi 4, 1, 160
	lxvd2x 37, 0, 4
	addi 4, 1, 176
	lxvd2x 38, 0, 4
	addi 4, 1, 192
	lxvd2x 39, 0, 4
	addi 4, 1, 208
	lxvd2x 40, 0, 4
	addi 4, 1, 224
	lxvd2x 41, 0, 4
	ld 0, 248(1)
	mtlr 0
	ld 2, 240(1)
	ld 3, 32(1)
	ld 4, 40(1)

	addi 1, 1, 256
	ld 0, 16(1)
	blr
