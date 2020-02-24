	pseg
	even

	def	main
main
	ai   r10, >FFFA
	mov  r10, r0
	mov  r11, *r0+
	mov  r9, *r0+
	mov  r13, *r0+
	clr  r1
	bl   @set_graphics
	mov  @gImage, r1
	li   r2, >20
	li   r3, >300
	bl   @vdpmemset
	bl   @charsetlc
	clr  r3
	li   r1, textout
	li   r5, >1F00
	movb *r1, r2
	jeq  L2
L15
	cb   r2, r5
	jh  JMP_0
	b    @L3
JMP_0
	li   r2, >10
L4
	inc  r1
	mov  r3, r4
	sla  r4, >5
	a    r2, r4
	mov  r4, r2
	swpb r2
	movb r2, @>8C02
	movb r4, @>8C02
	movb *r1, r2
	jeq  L5
L11
	movb r2, @>8C00
	inc  r1
	movb *r1, r2
	jne  L11
L5
	inc  r3
	ci   r3, >17
	jeq  L2
	inc  r1
	movb *r1, r2
	jne  L15
L2
	li   r1, >A000
	clr  r2
	bl   @stinit
	mov  @pDone, r13
	li   r9, vdpwaitvint
L13
	bl   *r9
	movb *r13, r1
	jne  JMP_1
	b    @L8
JMP_1
L16
* Begin inline assembler code
* 50 "quickplay.c" 1
	bl @stplay
	lwpi >8300
* 0 "" 2
* End of inline assembler code
	bl   *r9
	movb *r13, r1
	jeq  JMP_2
	b    @L16
JMP_2
L8
	li   r2, >9FBF
	movb r2, @>8400
	swpb r2
	movb r2, @>8400
	li   r1, >DFFF
	movb r1, @>8400
	swpb r1
	movb r1, @>8400
	b    @L13
L3
	srl  r2, 8
	ai   r2, >FFE0
	neg  r2
	sra  r2, >1
	andi r2, >FF
	b    @L4
	.size	main, .-main

	def	dummy
	.type	dummy, @object
	.size	dummy, 1
dummy
	byte	97

	def	textout
	.type	textout, @object
	.size	textout, 800
textout
	text '~~~~DATAHERE~~~~'
	byte 0
	byte 0
	bss 782

	ref	vdpwaitvint

	ref	pDone

	ref	stinit

	ref	charsetlc

	ref	vdpmemset

	ref	gImage

	ref	set_graphics
