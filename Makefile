CC = gcc
CFLAGS = -Wall -Wextra -O2

all: server client

server: server.c
	$(CC) $(CFLAGS) server.c -o echonet_server

client: client.c
	$(CC) $(CFLAGS) client.c -o echonet_client

clean:
	rm -f echonet_server echonet_client

