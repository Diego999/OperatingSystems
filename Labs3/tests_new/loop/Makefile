CC = gcc
CFLAGS = -DNDEBUG -O3 -Wall -D_GNU_SOURCE

.PHONY: all clean

all: loop

loop: loop.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f loop
