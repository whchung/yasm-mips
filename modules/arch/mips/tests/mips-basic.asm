%define FIVE 5
%define SIX 6

db 0x55
dw 'a'
message db 'hello,world',10,0
zerobuf: times 64 db 0
wordvar dw 123

add r7, r6, r5
addi r4, r3, 2+1+3
sll r0, r0, 2+1+3+5
addiu r3, r1, SIX
sltu r30, r30, r29
label:
and r2, r1, r0
and r2, r5, r0
j label
bal label2
xori r2, r10, FIVE
beq r1, r2, label2
label2:
add r31, r29, r30
movn r0, r1, r4

addi r0, r1, wordvar
addi r0, r1, 101b

label1	; some code
.loop	; some more code
	j .loop

label3	; some code
.loop	; some more code
	j .loop
