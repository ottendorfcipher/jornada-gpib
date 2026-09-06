! Integer division helpers with GCC's SuperH libcall register contracts.
!
! GCC does not call __udivsi3 / __sdivsi3 like C functions on SH-1..SH-3: it assumes only a
! few registers are clobbered (sh.md: udivsi3_i1 clobbers T, PR, r1, r4; divsi3_i1 clobbers
! T, PR, r1, r2, r3) and keeps live values in the others, so a C implementation would corrupt
! the caller. These routines honour those contracts using the DIV0U/DIV1 step instructions in
! the 64-by-32 form from the SH-3 programming manual (upper half zero): rotate the dividend
! so its top bit enters T, then one divide step on the remainder register; the quotient bits
! accumulate in the dividend register.
! __umodsi3 / __modsi3 are ordinary C-ABI libcalls and live in crt.c.
!
! Dividend in r4, divisor in r5, quotient returned in r0. Division by zero yields 0.

	.text

	.align	2
	.globl	___udivsi3
___udivsi3:
	tst	r5,r5
	bt	.Ludiv_zero
	mov	#0,r1		! remainder
	div0u
	.rept	32
	rotcl	r4
	div1	r5,r1
	.endr
	rotcl	r4		! r4 = quotient
	rts
	mov	r4,r0
.Ludiv_zero:
	rts
	mov	#0,r0

	.align	2
	.globl	___sdivsi3
___sdivsi3:
	mov	r4,r1		! |dividend|, becomes the quotient
	mov	r5,r2		! |divisor|
	mov	r4,r3
	xor	r5,r3		! bit 31 set when the signs differ
	cmp/pz	r1
	bt	.Lsdiv_1
	neg	r1,r1
.Lsdiv_1:
	cmp/pz	r2
	bt	.Lsdiv_2
	neg	r2,r2
.Lsdiv_2:
	tst	r2,r2
	bt	.Lsdiv_zero
	mov	#0,r0		! remainder
	div0u
	.rept	32
	rotcl	r1
	div1	r2,r0
	.endr
	rotcl	r1		! r1 = |quotient|
	cmp/pz	r3
	bt	.Lsdiv_done
	neg	r1,r1
.Lsdiv_done:
	rts
	mov	r1,r0
.Lsdiv_zero:
	rts
	mov	#0,r0
