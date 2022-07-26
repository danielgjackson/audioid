# Makefile
#

CC = gcc
#CFLAGS = -g -O1 -Wall
CFLAGS = -O2 -Wall
LIBS = -lm -lpthread -ldl
SRC = src/main.c src/audioid.c src/minfft.c src/miniaudio.c
INC = src/audioid.h src/dr_wav.h src/minfft.h src/miniaudio.h

# arm requires libatomic
CPU := $(shell gcc -print-multiarch | sed 's/-.*//')

ifeq ($(CPU),arm)
  LIBS += -latomic
endif

all: audioid

audioid: Makefile $(SRC) $(INC)
        echo $(CPU)
        $(CC) -o audioid $(CFLAGS) $(SRC) -I/usr/local/include -L/usr/local/lib $(LIBS)

clean:
        rm -f *.o core audioid
