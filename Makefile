# Makefile
#

CC = gcc
CFLAGS = -O2 -Wall
LIBS = -lm -lpthread -ldl
SRC = src/main.c src/audioid.c src/minfft.c src/miniaudio.c
INC = src/audioid.h src/minfft.h src/miniaudio.h
all: audioid

audioid: Makefile $(SRC) $(INC)
	$(CC) -o audioid $(CFLAGS) $(SRC) -I/usr/local/include -L/usr/local/lib $(LIBS)

clean:
	rm -f *.o core audioid
