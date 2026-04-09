CC = gcc
CFLAGS = -Wall -Wextra -pthread

all: servudp cliudp servbeuip clibeuip testcreme testmess

servudp: servudp.c
	$(CC) $(CFLAGS) -o servudp servudp.c

cliudp: cliudp.c
	$(CC) $(CFLAGS) -o cliudp cliudp.c

servbeuip: servbeuip.c creme.c creme.h
	$(CC) $(CFLAGS) -o servbeuip servbeuip.c creme.c

clibeuip: clibeuip.c
	$(CC) $(CFLAGS) -o clibeuip clibeuip.c

testcreme: testcreme.c creme.c creme.h
	$(CC) $(CFLAGS) -o testcreme testcreme.c creme.c

testmess: testmess.c creme.c creme.h
	$(CC) $(CFLAGS) -o testmess testmess.c creme.c

clean:
	rm -f servudp cliudp servbeuip clibeuip testcreme testmess *.o