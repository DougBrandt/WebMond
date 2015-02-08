# Makefile

CC = gcc
CFLAGS = -Wall -Werror -g -O2
INCLUDES = -I/usr/include

all: mond example

mond: singlyLinkedList.o commands.o monitorThread.o systemThread.o mond.o logLibrary.o webmon.o
	$(CC) $(CFLAGS) -o $@ mond.o monitorThread.o systemThread.o commands.o singlyLinkedList.o logLibrary.o webmon.o $(INCLUDES) -lreadline -pthread

mond.o: mond.c mond.h commands.c commands.h singlyLinkedList.c singlyLinkedList.h
	$(CC) $(CFLAGS) -c mond.c -o $@

monitorThread.o: monitorThread.c monitorThread.h logLibrary.o
	$(CC) $(CFLAGS) -c monitorThread.c -o $@

systemThread.o: systemThread.c systemThread.h logLibrary.o
	$(CC) $(CFLAGS) -c systemThread.c -o $@

commands.o: commands.c commands.h singlyLinkedList.c singlyLinkedList.h webmon.o
	$(CC) $(CFLAGS) -c commands.c -o $@

singlyLinkedList.o: singlyLinkedList.c singlyLinkedList.h
	$(CC) $(CFLAGS) -c singlyLinkedList.c -o $@

logLibrary.o: logLibrary.c logLibrary.h
	$(CC) $(CFLAGS) -c logLibrary.c -o $@

webmon.o: webmon.c webmon.h logLibrary.o singlyLinkedList.o
	$(CC) $(CFLAGS) -c webmon.c -o $@

example: example.c
	$(CC) $(CFLAGS) example.c -o $@

clean:
	rm -f *.o mond example

