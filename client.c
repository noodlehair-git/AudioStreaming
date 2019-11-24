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
#include <sys/ioctl.h>
#include <net/if.h>
#include <semaphore.h>
#include <alsa/asoundlib.h>
#include <time.h>
#define SA struct sockaddr

void createUDP();
void recv_audio_data_and_send_feedback();
void mulawopen(size_t *bufsiz);
void mulawclose(void);

struct sockaddr_in tcp_servaddr, udp_servaddr, cliaddr;
int tcp_sock, udp_sock, read_bytes, srv_udp_port, tcp_port, cli_udp_port, buf_size, target_buf, flag;
size_t block_size;
float Gamma, Q, Q_star;
char ip[15], tcp_ip[15], audio_data[1500], buf[3000];
char audiofile[100], logfile[100];
char * audio_buf;
struct ifreq ifr;
socklen_t length;
struct timeval tv;
struct timespec ts;

// Read and write positions for circular buffer
int read_pos, write_pos;

// Semaphore to prevent concurrent access in circular buffer
sem_t mutex;

static snd_pcm_t *mulawdev;
static snd_pcm_uframes_t mulawfrms;

#define mulawwrite(x) snd_pcm_writei(mulawdev, x, mulawfrms)

int main(int argc, char** argv) {
	if(argc < 8){
		printf("Please input all arguments: [tcp-ip][tcp-port][audiofile][block-size][gamma][buf-size][target-buf][logfile]\n");
		exit(1);
	}

	//Processing command-line input arguments
	strcpy(tcp_ip,(argv[1]));
	tcp_port = atoi(argv[2]);
	strcpy(audiofile,(argv[3]));
	audiofile[strlen(argv[3])] = '\0'; // TODO: Check if necessary
	block_size = atoi(argv[4]);
	Gamma = atof(argv[5]);
	buf_size = atoi(argv[6]);
	Q_star = target_buf = atoi(argv[7]);
	strcpy(logfile,(argv[8]));

	// Initialize Sleep time
	ts.tv_sec = 0;
	ts.tv_sec = Gamma * 1000000L; // Assume Gamma is given is Msec for now

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

	// // Outer while loop necessary for control plane
	// while(1){
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

	unsigned int file_size;
	// Copy server-udp-port from tmp buf
	memcpy(&srv_udp_port, tmp+1, 4);
	memcpy(&file_size, tmp+5, 4);

	printf("Filesize: %u\n", file_size);

	// Establishes relevant variables for UDP
	createUDP();

	printf("Client Udp port: %d\n", cli_udp_port);

	// Sends client-udp-port to server
	char sendbuf[4];
	memcpy(sendbuf, &cli_udp_port, 4);
	write(tcp_sock, sendbuf, sizeof(sendbuf));

	printf("Hit before child\n");
	int k = fork();
	if(k == 0){
		printf("Hit in child\n");
		// Signal SIGIO
		signal(SIGIO, recv_audio_data_and_send_feedback);
			// We must own the socket to receive the SIGIO message
		if (fcntl(udp_sock, F_SETOWN, getpid()) < 0)
			printf("Unable to set process owner to us");

		// Arrange for nonblocking I/O and SIGIO delivery. TODO: Is nonblocking necessary?
		if (fcntl(udp_sock, F_SETFL, FASYNC) < 0)
			printf("Unable to put client sock into non-blocking/async mode");
			
		
	
		// Consumer loop to read from buffer. Handles reading audio packets
		while (1){
			mulawopen(&block_size);		// initialize audio codec
			char * temp_buf = (char *) malloc(block_size);
			while(Q >= Q_star){ // Client commences playback after prefetching reaches Q*
				printf("Reading\n");
				sem_wait(&mutex);

				for(int i = 0; i < block_size; i++){
					char c = audio_buf[read_pos];
					temp_buf[i] = c;
					read_pos = (read_pos + 1) % buf_size;
					Q--;
					// printf("Read pos: %d\n", read_pos);
				}
				sem_post(&mutex);
				mulawwrite(temp_buf);
				// nanosleep(&ts, NULL);
				usleep(Gamma);
			}
			mulawclose();					
		}
	}

	while(1){
		char recv_buf[2];
		recv(tcp_sock, recv_buf, 1, 0);
		// printf("Recv buf[0]: %d\n", recv_buf[0]);
		// printf("Recvbuf [1]: %c\n", recv_buf[1]);
		if(recv_buf[0] == '5'){
			printf("Hit here\n");
			printf("Termination Received\n");
			kill(k, SIGKILL); // Kill Child Process
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
	cliaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(udp_sock, (const struct sockaddr *)&cliaddr, sizeof(cliaddr)) < 0 ){
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	// Gets Client IP and Port Number
	struct sockaddr_in bound_addr;
	socklen_t size = sizeof(bound_addr);
	//Fetching the port number assigned by the OS to the server and displaying it for the client to use
	if (getsockname(udp_sock, (struct sockaddr *)&bound_addr, &size) == -1)
		perror("getsockname failed");
	
	fprintf(stdout,"Server UDP port number: %d\n", ntohs(udp_servaddr.sin_port));
	cli_udp_port = ntohs(bound_addr.sin_port);

}

void recv_audio_data_and_send_feedback(){
	printf("Hit in recvaudio\n");
	char feedback[12];
	socklen_t srv_addr_size = sizeof(udp_servaddr);
	ssize_t bytes_read;

	bytes_read = recvfrom(udp_sock, (char *)audio_data, sizeof(audio_data), 0,
		 (struct sockaddr *) &udp_servaddr, &srv_addr_size);

	// Write to buffer. TODO: MAKE SURE SERVER IS SENDING SEQ NO
	sem_wait(&mutex);
	for(int i = 0; i < bytes_read; i++){ // Write to buffer excluding seq no
		audio_buf[write_pos] = audio_data[i];
		write_pos = (write_pos + 1) % buf_size;
	}
	sem_post(&mutex);

		// Buffer occupancy; Sets current buffer level
	if(read_pos < write_pos){ // TODO: Check that this is correct
		Q = write_pos - read_pos;
	}
	else{
		Q = buf_size - read_pos + write_pos; // Account for wrap around
	}


	// printf("Hit in recv audio data\n");
	// printf("Q: %f\n", Q);
	// printf("Q_star: %f\n", Q_star);
	// printf("Gamma: %f\n", Gamma);

	memcpy(feedback, &Q, 4); // Copies Q into feedback
	memcpy(feedback + 4, &Q_star, 4); // Copies Q_star (target_buf) into feedback
	memcpy(feedback + 8, &Gamma, 4); // Copies Gamma into feedback

	int bytes_sent = sendto(udp_sock, (const char *)feedback, sizeof(feedback), 0,
	 (const struct sockaddr *) &udp_servaddr, sizeof(udp_servaddr)); // Send feedback to server

	// Reset sleep timer upon Signal
	// nanosleep(&ts, NULL);
}

void mulawopen(size_t *bufsiz) {
	snd_pcm_hw_params_t *p;
	unsigned int rate = 8000;

	snd_pcm_open(&mulawdev, "default", SND_PCM_STREAM_PLAYBACK, 0);
	snd_pcm_hw_params_alloca(&p);
	snd_pcm_hw_params_any(mulawdev, p);
	snd_pcm_hw_params_set_access(mulawdev, p, SND_PCM_ACCESS_RW_INTERLEAVED);
	snd_pcm_hw_params_set_format(mulawdev, p, SND_PCM_FORMAT_MU_LAW);
	snd_pcm_hw_params_set_channels(mulawdev, p, 1);
	snd_pcm_hw_params_set_rate_near(mulawdev, p, &rate, 0);
	snd_pcm_hw_params(mulawdev, p);
	snd_pcm_hw_params_get_period_size(p, &mulawfrms, 0);
	*bufsiz = (size_t)mulawfrms;
	return;
}

void mulawclose(void) {
	snd_pcm_drain(mulawdev);
	snd_pcm_close(mulawdev);
}