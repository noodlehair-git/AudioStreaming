// Streaming client: playaudio
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <semaphore.h>
#define SA struct sockaddr

void createUDP();
void recv_audio_data_and_send_feedback();
struct sockaddr_in tcp_servaddr, udp_servaddr, cliaddr;
int tcp_sock, udp_sock, read_bytes, srv_udp_port, tcp_port, payload_size, cli_udp_port, buf_size, target_buf, flag;
float Gamma, Q, Q_star;
char ip[15], sendbuf[100], tcp_ip[15], audio_data[1500], buf[3000];
char audiofile[100], logfile[100];
char * audio_buf;
struct ifreq ifr;
socklen_t length;
struct timeval tv;

// Read and write positions for circular buffer
int read_pos, write_pos;

// Semaphore to prevent concurrent access in circular buffer
sem_t mutex;

int main(int argc, char** argv) {
	if(argc < 8){
		printf("Please input all arguments: [tcp-ip][tcp-port][audiofile][payload-size][gamma][buf-size][target-buf][logfile]\n");
		exit(1);
	}
	// Signal SIGIO
	signal(SIGIO, recv_audio_data_and_send_feedback);

	//Processing command-line input arguments
	strcpy(tcp_ip,(argv[1]));
	tcp_port = atoi(argv[2]);
	strcpy(audiofile,(argv[3]));
	audiofile[strlen(argv[3])] = '\0'; // TODO: Check if necessary
	payload_size = atoi(argv[4]);
	Gamma = atof(argv[5]);
	buf_size = atoi(argv[6]);
	Q_star = target_buf = atoi(argv[7]);
	strcpy(logfile,(argv[8]));

	audio_buf = (char *) malloc(buf_size); // Allocate memory circular audio buffer
	read_pos = write_pos = 0; // Initialize read and write positio to 0
	if(sem_init(&mutex, 1, 1) < 0){
		perror("Error with semahore intialization");
		exit(0);
	}

	if ((tcp_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		printf("socket creation failed...\n");
		exit(0);
	}

	// Filling server information
	// IP address of server and port number provided as command line argument
	tcp_servaddr.sin_family = AF_INET;
	tcp_servaddr.sin_port = htons(tcp_port);
	tcp_servaddr.sin_addr.s_addr = inet_addr(tcp_ip);

	// Outer while loop necessary for control plane
	while(1){
		// connect the client socket to server socket
		if (connect(tcp_sock, (SA*)&tcp_servaddr, sizeof(tcp_servaddr)) != 0) {
			printf("connection with the server failed...\n");
			exit(0);
		}

		// Sends audio file name to server
		write(tcp_sock, audiofile, strlen(audiofile));

		char tmp[5];
		// Read response from server whether file exists
		if(read(tcp_sock, tmp, sizeof(tmp)) > 0){
			if(tmp[0] != '2'){
				printf("File does not exist on server\n");
				exit(0);
			}
		}

		// for(int i = 1; i < 5; i++){
		// 	printf("Tmp: %x\n", tmp[i]);
		// }

		// Copy server-udp-port from tmp buf
		memcpy(&srv_udp_port, tmp+1, 4);

		// Establishes relevant variables for UDP
		createUDP();

		// Sends client-udp-port to server
		memcpy(sendbuf, &cli_udp_port, 4);
		write(tcp_sock, sendbuf, sizeof(sendbuf));

		int k = fork();
		if(k == 0){
			//int audio_fd = open("/dev/audio", O_WRONLY, 0666);
			int audio_fd = open("test.txt", O_RDWR | O_CREAT, 0666);
			// Consumer loop to read from buffer. Handles reading audio packets
			while (1){
				// Write to dev/audio/ at fixed playback rate gamma. Prevent concurrent access
				sem_wait(&mutex);
				for(int i = 0; i < Gamma; i++){
					char c = audio_buf[read_pos];
					write(audio_fd, &c, 1);
					read_pos = (read_pos + 1) % buf_size;
				}
				sem_post(&mutex);
			}
		}

		char recv_buf[2];
		read(tcp_sock, recv_buf, sizeof(recv_buf));
		if(recv_buf[0] = '5'){
			printf("Termination Received\n");
			exit(0);
		}	
	}
}


void createUDP(){
	if ((udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
    	}

	udp_servaddr.sin_family = AF_INET;
	udp_servaddr.sin_port = htons(srv_udp_port);
	udp_servaddr.sin_addr.s_addr = inet_addr(tcp_ip);

	//Getting the IP address of eth0 interface used by the host
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
	ioctl(udp_sock, SIOCGIFADDR, &ifr);
	strcpy(ip, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

	cliaddr.sin_family = AF_INET;
	cliaddr.sin_port = htons(0);
	cliaddr.sin_addr.s_addr = inet_addr(ip);

	if (bind(udp_sock, (const struct sockaddr *)&cliaddr, sizeof(cliaddr)) < 0 ){
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	//Fetching the port number assigned by the OS to the server and displaying it for the client to use
	if (getsockname(udp_sock, (struct sockaddr *)&udp_servaddr, &length) == -1)
		perror("getsockname failed");
	
	fprintf(stdout,"Server UDP port number: %d\n", ntohs(udp_servaddr.sin_port));
	cli_udp_port = ntohs(udp_servaddr.sin_port);

	// We must own the socket to receive the SIGIO message
	if (fcntl(udp_sock, F_SETOWN, getpid()) < 0)
		printf("Unable to set process owner to us");

	// Arrange for nonblocking I/O and SIGIO delivery
	if (fcntl(udp_sock, F_SETFL, O_NONBLOCK | FASYNC) < 0)
		printf("Unable to put client sock into non-blocking/async mode");
}

void recv_audio_data_and_send_feedback(){
	char feedback[12];
	socklen_t srv_addr_size = sizeof(udp_servaddr);
	ssize_t bytes_read;

	bytes_read = recvfrom(udp_sock, (char *)audio_data, sizeof(audio_data), 0,
		 (struct sockaddr *) &udp_servaddr, &srv_addr_size);

	if(audio_data[0] == '5'){
		printf("Termination Received\n");
		exit(0);
	}
	// Buffer occupancy
	if(read_pos < write_pos){ // TODO: Check that this is correct
		Q = write_pos - read_pos;
	}
	else{
		Q = buf_size - 1 - read_pos + write_pos; // Account for wrap around
	}

	// Write to buffer
	sem_wait(&mutex);
	for(int i = 4; i < bytes_read; i++){ // Write to buffer excluding seq no
		audio_buf[write_pos] = audio_data[i];
		write_pos = (write_pos + 1) % buf_size;
	}
	sem_post(&mutex);

	printf("Hit in recv audio data\n");
	printf("Q: %f\n", Q);
	printf("Q_star: %f\n", Q_star);
	printf("Gamma: %f\n", Gamma);

	memcpy(feedback, &Q, 4); // Copies Q into feedback
	memcpy(feedback + 4, &Q_star, 4); // Copies Q_star (target_buf) into feedback
	memcpy(feedback + 8, &Gamma, 4); // Copies Gamma into feedback

	sendto(udp_sock, (const char *)feedback, strlen(feedback), 0,
	 (const struct sockaddr *) &udp_servaddr, sizeof(udp_servaddr)); // Send feedback to server
}
