# Makefile for Linux. 
# You might need to install C/C++ development tools by typing :
#    sudo apt-get install build-essential
# in a terminal.
# For more information on the configuration used, see http://www.ensta-bretagne.fr/lebars/Share/Ubuntu.txt .
# Use dos2unix *.txt to ensure line endings are correct for Linux in the configuration files.

PROGS = V4L2_webcam

CC = gcc

CFLAGS += -g
#CFLAGS += -O3
CFLAGS += -Wall -Wno-unknown-pragmas -Wextra
CFLAGS += -Winline

CFLAGS += -D _DEBUG

CFLAGS += -I.

# For Linux, if static needed...
#LDFLAGS += -static-libgcc -static

#LDFLAGS += -lm
#LDFLAGS += -lpthread -lrt

default: $(PROGS)

############################# PROGS #############################

Main.o: Main.c Conversions.h
	$(CC) $(CFLAGS) -c $<

V4L2_webcam: Main.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(PROGS) $(PROGS:%=%.elf) $(PROGS:%=%.exe) *.o *.obj core *.gch
