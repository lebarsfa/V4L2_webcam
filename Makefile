# Makefile for Linux, tested with Ubuntu 10.10. 
# You might need to install C/C++ development tools by typing :
#    sudo apt-get install build-essential
# in a terminal.

PROGS = V4L2_webcam

CC = gcc
CFLAGS += -g
#CFLAGS += -O3
CFLAGS += -Wall
CFLAGS += -Wextra -Winline

CFLAGS += -D _DEBUG

CFLAGS += -I.

#LDFLAGS += -lm
#LDFLAGS += -lpthread -lrt

default: $(PROGS)

############################# PROGS #############################

Main.o: Main.c Conversions.h
	$(CC) $(CFLAGS) -c $<

V4L2_webcam: Main.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

clean:
	rm -f $(PROGS) $(PROGS).elf $(PROGS).exe *.o *.obj core *.gch
