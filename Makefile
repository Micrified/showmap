CC=gcc
CFLAGS=-D_GNU_SOURCE -g
all: pmap.c showmap.c
	${CC} ${CFLAGS} -o showmap showmap.c pmap.c
