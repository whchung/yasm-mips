libyasm_a_SOURCES += modules/arch/mips/mipsarch.c
libyasm_a_SOURCES += modules/arch/mips/mipsarch.h
libyasm_a_SOURCES += modules/arch/mips/mipsbc.c
nodist_libyasm_a_SOURCES += mipsid.c

YASM_MODULES += arch_mips

mipsid.c: $(srcdir)/modules/arch/mips/mipsid.re re2c$(EXEEXT)
	$(top_builddir)/re2c$(EXEEXT) -s -o $@ $(srcdir)/modules/arch/mips/mipsid.re

CLEANFILES += mipsid.c

# [JC 140108] temporary disable tests
# FIXME: need to add it back later in the project 
# EXTRA_DIST += modules/arch/mips/tests/Makefile.inc

EXTRA_DIST += modules/arch/mips/mipsid.re

# [JC 140108] temporary disable tests
# FIXME: need to add it back later in the project 
# include modules/arch/mips/tests/Makefile.inc
