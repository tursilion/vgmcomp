	pseg
	even

	def	main
main
	dect r10
	mov  r11, *r10
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
L13
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
L10
	movb r2, @>8C00
	inc  r1
	movb *r1, r2
	jne  L10
L5
	inc  r3
	ci   r3, >17
	jeq  L2
	inc  r1
	movb *r1, r2
	jne  L13
L2
	li   r1, >A000
	clr  r2
	bl   @stinit
L8
* Begin inline assembler code
* 47 "quickplay.c" 1
	clr r12
	tb 2
	jeq -4
	movb @>8802,r12
* 0 "" 2
* 48 "quickplay.c" 1
	bl @stplay
	lwpi >8300
* 0 "" 2
* 50 "quickplay.c" 1
	LIMI 2
* 0 "" 2
* 50 "quickplay.c" 1
	LIMI 0
* 0 "" 2
* End of inline assembler code
	b    @L8
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

	ref	stinit

	ref	charsetlc

	ref	vdpmemset

	ref	gImage

	ref	set_graphics
