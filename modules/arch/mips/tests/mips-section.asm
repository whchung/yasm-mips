;
; Macro section
;
%define C_FIVE 5
%define C_SIX  6

;
; Data section
;
section .data

message		db 'hello,world',10,0
zerobuf		times 4 db 0
wordvar		dw 123

;
; BSS Section
;
section .bss

var1 resw 	1 	; word
arr1 resb	64	; 64 bytes

;
; Code section
;

section .text
global _main

_main:
ADD R7, r6, r5
addi r4, r3, 2+1+3
sll r0, r0, 2+1+3+5
addiu r3, r1, C_SIX
sltu r30, r30, r29

label1:
and r2, r1, r0
addi r2, r5, wordvar
j label1
bal label2
xori r2, r10, C_FIVE
beq r1, r2, label2

label2:
add r31, r29, r30
movn r0, r1, r4
j label3.loop

addi r0, r1, wordvar
addi r0, r1, 101b

label3	; some code
.loop	; some more code
	j .loop

label4	; some code
.loop	; some more code
	j .loop

