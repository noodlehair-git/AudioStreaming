CC=gcc
CFLAGS=-I/include

all: streamerd playaudio

streamerd: server.o
		$(CC) -o streamerd server.o

playaudio: client.o
		$(CC) -pthread -o playaudio client.o -lasound

clean:
	@echo removing .o files
	rm server.o client.o
