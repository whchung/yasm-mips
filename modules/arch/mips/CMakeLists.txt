YASM_RE2C(
    ${CMAKE_CURRENT_SOURCE_DIR}/arch/mips/mipsid.re
    ${CMAKE_CURRENT_BINARY_DIR}/mipsid.c
    -s
    )

YASM_ADD_MODULE(arch_mips
    arch/mips/mipsarch.c
    arch/mips/mipsbc.c
    mipsid.c
    )
