#ifndef PORT
	#define PORT 59598
#endif

PORT = 59599
CFLAGS = -DPORT=\$(PORT) -g -Wall -std=gnu99

all: helpcentre hcq_server

helpcentre: helpcentre.o hcq.o 
	gcc $(CFLAGS) -o helpcentre helpcentre.o hcq.o

helpcentre.o: helpcentre.c hcq.h
	gcc $(CFLAGS) -c helpcentre.c

hcq.o: hcq.c hcq.h
	gcc $(CFLAGS) -c hcq.c
	
hcq_server: hcq.o hcq_server.c
	gcc $(CFLAGS) -o hcq_server hcq_server.c hcq.o

clean: 
	rm helpcentre *.o
