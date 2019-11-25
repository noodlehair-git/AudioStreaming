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

struct sockaddr_in tcp_servaddr, udp_servaddr, cliaddr;
int tcp_sock, cli_udp_sock, read_bytes, tcp_port, cli_udp_port, buf_size, target_buf, flag;
unsigned int srv_udp_port, block_size;
float Gamma, Q, Q_star;
char tcp_ip[15], audiofile[100], logfile[100];
char * audio_buf, * audio_data;
struct timeval tv;
struct timespec ts;
struct timespec rem;

int total_bytes_read; // Debugging purposes

// Read and write positions for circular buffer
int read_pos, write_pos;

// Semaphore to prevent concurrent access in circular buffer
sem_t mutex;

static snd_pcm_t *mulawdev;
static snd_pcm_uframes_t mulawfrms;

#define mulawwrite(x) snd_pcm_writei(mulawdev, x, mulawfrms)

void update_Q(){
	// Buffer occupancy; Sets current buffer level
	if(read_pos < write_pos){
		Q = write_pos - read_pos;
	}
	else{
		Q = buf_size - read_pos + write_pos; // Account for wrap around
	}
}

int add_to_buf(char c){
	// sem_wait(&mutex);
	int next;
	next = write_pos + 1;
	
	if(next >= buf_size){
		next = 0;
	}
	if(next == read_pos){ // write_pos + 1 == read_pos; buffer is full
		return -1;
	}
	audio_buf[write_pos] = c;
	write_pos = next;
	update_Q();
	// sem_post(&mutex);
	return 0;
}

int remove_from_buf(char * data){
	// sem_wait(&mutex);
	int next;
	next = read_pos + 1;
	if(read_pos == write_pos){ // Buffer is empty
		return -1;
	}
	if(next >= buf_size){
		next = 0;
	}
	*data = audio_buf[read_pos];
	read_pos = next;
	update_Q();
	// sem_post(&mutex);
	return 0;
}

void createUDP(){
	if ((cli_udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	udp_servaddr.sin_family = AF_INET;
	udp_servaddr.sin_port = htons(srv_udp_port);
	udp_servaddr.sin_addr.s_addr = inet_addr(tcp_ip);

	cliaddr.sin_family = AF_INET;
	cliaddr.sin_port = htons(0);
	cliaddr.sin_addr.s_addr = htonl(INADDR_ANY);

	if(bind(cli_udp_sock, (const struct sockaddr *)&cliaddr, sizeof(cliaddr)) < 0 ){
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	// Gets Client IP and Port Number
	struct sockaddr_in bound_addr;
	socklen_t size = sizeof(bound_addr);
	//Fetching the port number assigned by the OS to the server and displaying it for the client to use
	if (getsockname(cli_udp_sock, (struct sockaddr *)&bound_addr, &size) == -1)
		perror("getsockname failed");
	
	cli_udp_port = ntohs(bound_addr.sin_port);
}

// void recv_audio_data_and_send_feedback(){
// 		printf("Hit in recvaudio\n");
// }
void recv_audio_data_and_send_feedback(){
	printf("Hit in recvaudio\n");
	char feedback[12];
	socklen_t srv_addr_size = sizeof(udp_servaddr);
	ssize_t bytes_read;

	bytes_read = recvfrom(cli_udp_sock, (char *)audio_data, block_size + 4, 0,
		 (struct sockaddr *) &udp_servaddr, &srv_addr_size);

	// printf("Bytes received from server: %ld\n", bytes_read);
	total_bytes_read += bytes_read;
	// printf("Total bytes received: %d\n", total_bytes_read);

	sem_wait(&mutex);
	for(int i = 4; i < bytes_read; i++){ // Write to buffer excluding seq no
		int ret = add_to_buf(audio_data[i]);
		if(ret == -1){
			printf("Buffer full in writing. Q: %f\n", Q);
		}
	}
	sem_post(&mutex);

	// printf("Finished producing. Q: %f\n", Q);

	memcpy(feedback, &Q, 4); // Copies Q into feedback
	memcpy(feedback + 4, &Q_star, 4); // Copies Q_star (target_buf) into feedback
	memcpy(feedback + 8, &Gamma, 4); // Copies Gamma into feedback

	int bytes_sent = sendto(cli_udp_sock, (const char *)feedback, sizeof(feedback), 0,
	 (const struct sockaddr *) &udp_servaddr, sizeof(udp_servaddr)); // Send feedback to server

	// Reset sleep timer upon Signal
	if(rem.tv_sec != 0 && rem.tv_nsec != 0){ // Case where signal handler is called before first nanosleep is called
		ts.tv_sec = rem.tv_sec;
		ts.tv_nsec = rem.tv_nsec; // Assume Gamma is given is Msec for now
	}
	nanosleep(&ts, &rem);
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

int main(int argc, char** argv) {
	if(argc < 8){
		printf("Please input all arguments: [tcp-ip][tcp-port][audiofile][block-size][gamma][buf-size][target-buf][logfile]\n");
		exit(1);
	}

	total_bytes_read = 0;
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
	audio_data = (char *) malloc(1500);

	// Initialize Sleep time
	ts.tv_sec = 0;
	ts.tv_nsec = Gamma * 1000000L; // Assume Gamma is given is Msec for now

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

	// connect the client socket to server socket
	if (connect(tcp_sock, (SA*)&tcp_servaddr, sizeof(tcp_servaddr)) != 0) {
		printf("connection with the server failed...\n");
		exit(0);
	}

	// Sends audio file name to server
	write(tcp_sock, audiofile, strlen(audiofile));

	char tmp[9];
	// Read response from server whether file exists
	if(read(tcp_sock, tmp, sizeof(tmp)) > 0){
		if(tmp[0] != '2'){
			printf("File does not exist on server\n");
			exit(0);
		}
	}

	unsigned int file_size;
	memcpy(&srv_udp_port, tmp+1, 4); // Copy server-udp-port from tmp buf
	memcpy(&file_size, tmp+5, 4);

	printf("Filesize: %d\n", file_size);
	printf("Server UDP port: %d\n", srv_udp_port);

	createUDP(); // Establishes relevant variables for UDP

	printf("Client UDP port: %d\n", cli_udp_port);

	// Sends client-udp-port to server
	char sendbuf[4];
	memcpy(sendbuf, &cli_udp_port, 4);
	write(tcp_sock, sendbuf, sizeof(sendbuf));

	int k = fork();
	if(k == 0){
		size_t audio_blk_size;
		mulawopen(&audio_blk_size);		// initialize audio codec
		char * output_audio_buf = (char *) malloc(audio_blk_size);
		// Signal SIGIO
		// signal(SIGIO, recv_audio_data_and_send_feedback);
		struct sigaction handler;
		//Set signal handler for SIGIO 
		handler.sa_handler = recv_audio_data_and_send_feedback;
		handler.sa_flags = 0;

		//Create mask that mask all signals 
		if (sigfillset(&handler.sa_mask) < 0) 
			printf("sigfillset() failed");
		//No flags 
		handler.sa_flags = 0;

		if (sigaction(SIGIO, &handler, 0) < 0)
			printf("sigaction() failed for SIGIO");
		// We must own the socket to receive the SIGIO message
		if (fcntl(cli_udp_sock, F_SETOWN, getpid()) < 0)
			printf("Unable to set process owner to us");

		// Arrange for nonblocking I/O and SIGIO delivery. TODO: Is nonblocking necessary?
		if (fcntl(cli_udp_sock, F_SETFL, FASYNC) < 0)
			printf("Unable to put client sock into non-blocking/async mode");
		
		int blocks_read = 0;
		// Consumer loop to read from buffer. Handles reading audio packets
		while (blocks_read <= file_size){
			// printf("While. Q: %f\n", Q);
			if(Q >= audio_blk_size){ // Client commences playback after prefetching reaches Q*
				printf("Reading\n");
				printf("Q: %f\n", Q);
				printf("Q_star: %f\n", Q_star);

				sem_wait(&mutex);
				for(int i = 0; i < audio_blk_size; i++){
					char c;
					int ret = remove_from_buf(&c);
					if(ret == -1){
						printf("Buffer empty in reading\n");
					}
					else{
						output_audio_buf[i] = c;
					}
				}
				sem_post(&mutex);

				printf("Finished Consuming. Q: %f\n", Q);
				mulawwrite(output_audio_buf);
				blocks_read += audio_blk_size;
				
				// Initialize Sleep time
				ts.tv_sec = 0;
				ts.tv_nsec = Gamma * 1000000; // Assume Gamma is given is Msec for now

				nanosleep(&ts, &rem);
				// usleep(Gamma);
			}
		}
		mulawclose();					
	}

	while(1){ // Parent Process
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