
PREFIX = /usr/local

#
# See what our hardware machine is and our compiler (gcc or clang)
#
MACHINE = $(shell uname -m)
COMPILER = $(shell $(CC) 2>&1 | ( fgrep -q clang && echo clang || echo gcc ) )

DEBUGFLAGS_gcc = -pg
DEBUGFLAGS = -g -O0 $(DEBUGFLAGS_$(COMPILER))
DEBUGFLAGS = 

FLOATFLAGS_clang = -ffast-math -O3
FLOATFLAGS_gcc = -ffast-math
FLOATFLAGS_armv7l_gcc = -ftree-vectorize -mfpu=neon 
FLOATFLAGS = $(FLOATFLAGS_$(MACHINE)_$(COMPILER)) $(FLOATFLAGS_$(COMPILER))

COMPILERFLAGS_gcc = -std=c99 
COMPILERFLAGS = $(COMPILERFLAGS_$(COMPILER))

LDLIBS += -lm

CPPFLAGS = -MMD

CFLAGS = $(COMPILERFLAGS) -Wall -Werror -D_POSIX_C_SOURCE=200112L -D_BSD_SOURCE=1 -D_DARWIN_C_SOURCE=1 $(DEBUGFLAGS) $(FLOATFLAGS)
DAEMON_LDLIBS = -lrtlsdr

all : daemon clients go-clients

daemon : ookd

clients : ookdump wh1080 

go-clients : go/bin/ooklog go/bin/ookanalyze

go/bin/% : $(wildcard go/src/*/*.go )
	( cd go ; GOPATH=`pwd` go install $(@:go/bin/%=%) )

ookd : ookd.o rtl.o ook.o
	$(LINK.c) $^ $(LOADLIBES) $(DAEMON_LDLIBS) $(LDLIBS) -o $@

ookdump : ookdump.o ook.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@

wh1080 : wh1080.o ook.o
	$(LINK.c) $^ $(LOADLIBES) $(LDLIBS) -o $@

clean :
	rm -f *.o ookd ookdump wh1080

install : ookd ookdump wh1080
	install $^ $(PREFIX)/bin

ookd.o : ook.h rtl.h

ookdump.o wh1080.o : ook.h

.PHONY : clean all install


include $(wildcard %.d)