CFLAGS+= -g -Wall -Wextra -std=c99 -pedantic
CPPFLAGS+= -D_XOPEN_SOURCE=700

.PHONY: all clean

all: minim

clean:
	rm -f minim
