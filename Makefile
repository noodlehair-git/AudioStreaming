CC=gcc
CFLAGS=-I/include

all: streamerd playaudio

streamerd: server.o
		$(CC) -o streamerd server.o

playaudio: client.o
		$(CC) -o playaudio client.o

clean:
	@echo removing .o files
	rm server.o client.o
