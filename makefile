SHELL := /bin/sh
CC := gcc
CFLAGS := -g -I. -Wall -L. -pthread
RM := rm -f

IP := 127.0.0.1
PORT1 := 5050
PORT2 := 5051
PORT3 := 8080
PORT4 := 8081

LIBNAME = libksocket.a 
OBJFILES = ksocket.o

# This allows  to run 'make runuser FOUR_USERS=1'
FOUR_USERS ?= 0

all: library init user

library: $(OBJFILES)
	ar rs $(LIBNAME) $(OBJFILES)

ksocket.o: ksocket.h ksocket.c
	$(CC) $(CFLAGS) -c ksocket.c -o ksocket.o

init: library initksocket.c 
	$(CC) $(CFLAGS) -o initk initksocket.c -lksocket -lpthread

user: library user1.c user2.c
	$(CC) $(CFLAGS) -o u1 user1.c -lksocket
	$(CC) $(CFLAGS) -o u2 user2.c -lksocket

runinit: init 
	./initk

runuser: user
	gnome-terminal -- bash -c "./u2 $(IP) $(PORT2) $(IP) $(PORT1); exec bash"
	@sleep 1
	gnome-terminal -- bash -c "./u1 $(IP) $(PORT1) $(IP) $(PORT2); exec bash"
ifeq ($(FOUR_USERS), 1)
	gnome-terminal -- bash -c "./u2 $(IP) $(PORT4) $(IP) $(PORT3); exec bash"
	@sleep 1
	gnome-terminal -- bash -c "./u1 $(IP) $(PORT3) $(IP) $(PORT4); exec bash"   
endif

clean:
	-$(RM) $(OBJFILES) initk u1 u2 recv_file.txt

deepclean: clean
	-$(RM) $(LIBNAME)