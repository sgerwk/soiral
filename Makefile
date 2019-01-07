PROGS=irblast signal2pbm remote layout serial serial2sound

CFLAGS+=-g -Wall -Wextra
irblast remote layout: LDLIBS+=-lasound

all: $(PROGS)

remote layout: microphone.o filters.o protocols.o
layout: LDLIBS+=-lpthread

clean:
	rm -f $(PROGS) *.o

