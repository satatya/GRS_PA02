# MT25084 - PA02 Makefile

CC=gcc
CFLAGS=-O2 -Wall -Wextra -pthread
LDFLAGS=-pthread

ALL= \
	MT25084_Part_A1_Server MT25084_Part_A1_Client \
	MT25084_Part_A2_Server MT25084_Part_A2_Client \
	MT25084_Part_A3_Server MT25084_Part_A3_Client

all: $(ALL)

MT25084_Part_A1_Server: MT25084_Part_A1_Server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

MT25084_Part_A1_Client: MT25084_Part_A1_Client.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

MT25084_Part_A2_Server: MT25084_Part_A2_Server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

MT25084_Part_A2_Client: MT25084_Part_A2_Client.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

MT25084_Part_A3_Server: MT25084_Part_A3_Server.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

MT25084_Part_A3_Client: MT25084_Part_A3_Client.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(ALL) *.o perf_*.txt
