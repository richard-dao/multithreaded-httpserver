EXECBIN  = httpserver
SOURCES  = $(wildcard *.c)
OBJECTS  = $(SOURCES:%.c=%.o)
FORMATS  = $(SOURCES:%.c=%.fmt)


CC = clang
CFLAGS = -Wall -Wextra -Werror -Wpedantic
FORMAT = clang-format

all: httpserver

httpserver : httpserver.o
	$(CC) -o httpserver httpserver.o socket_bytes_handlers.a

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f httpserver *.o

format: $(FORMATS)

%.fmt: %.c
	$(FORMAT) -i $<
	touch $@

