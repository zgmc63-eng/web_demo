CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic -O2

all: fastcgi_demo

fastcgi_demo: src/fastcgi_demo.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f fastcgi_demo

.PHONY: all clean
