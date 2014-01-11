add r7, r6, r5
addi r4, r3, 2
addiu r3, r1, 6
sltu r30, r30, r29
label:
and r2, r1, r0
and r2, r5, r0
j label
bal label2

label2:
add r31, r29, r30
movn r0, r1, r4