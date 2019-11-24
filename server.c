// Streaming Server: streamerd
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <net/if.h>
#define SA struct sockaddr

struct sockaddr_in tcp_servaddr, udp_servaddr, cli_addr;
int tcp_sock, srv_udp_sock, connfd, audio_file_fd, bytes_read, tcp_port, payload_size, srv_udp_port;
float lambda, Q, Q_star, Gamma, fl_a, fl_delta, fl_epsilon, fl_beta;
float ** control_param_list;
FILE * mode_fd;
char logfile[100], filename[100], str[10], a[5], delta[5], epsilon[5], beta[5];
char tmp_path[110];
int mode;
struct timespec ts;

void control_congestion();
void createUDP();
void client_feedback_handler();
void read_control_param_list();

struct ifreq ifr;

int main(int argc, char** argv) {
	if(argc < 6){
		printf("Please input all arguments: [tcp-port][payload-size][init-lambda][mode][logfile]\n");
		exit(1);
	}

	//processing command-line input arguments
	tcp_port = atoi(argv[1]);
	payload_size = atoi(argv[2]);
	lambda = atoi(argv[3]);
	mode = atoi(argv[4]);
	strcpy(logfile, argv[5]);

	// Initialize Sleep time
	ts.tv_sec = 0;
	ts.tv_sec = lambda * 1000000L; // Assume Gamma is given is Msec for now

	read_control_param_list(); // Reads control param list
	

	if ((tcp_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		printf("socket creation failed...\n");
		exit(0);
	}

	//Getting the IP address of eth0 interface used by the host. Copies into string ip
	// ifr.ifr_addr.sa_family = AF_INET;
	// strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
	// ioctl(tcp_sock, SIOCGIFADDR, &ifr);
	// strcpy(ip, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

	
	tcp_servaddr.sin_family = AF_INET;
	tcp_servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	tcp_servaddr.sin_port = htons(tcp_port);

	// Bind the socket with the server address
	if ((bind(tcp_sock, (SA*)&tcp_servaddr, sizeof(tcp_servaddr))) != 0) {
		printf("socket bind failed...\n");
		exit(0);
	}

	// Gets Server IP and Port Number
	struct sockaddr_in bound_addr;
	socklen_t size = sizeof(bound_addr);
	// Get port number that is assigned by OS to server process
	if(getsockname(tcp_sock, (struct sockaddr *) &bound_addr, &size) == -1){
		perror("Server: Error with get socket name");
		exit(1);
	}
	char srv_local_ip[30];
	inet_ntop(AF_INET, &bound_addr, srv_local_ip, sizeof(srv_local_ip));
	printf("Server Port: %d\n", ntohs(bound_addr.sin_port));
	printf("Server IP: %s\n", inet_ntoa(bound_addr.sin_addr));

	// Listen blocks until client connection request arrives
	if ((listen(tcp_sock, 5)) != 0) {
		perror("Listen failed...\n");
		exit(0);
	}

	printf("******************************SERVER*****************************\n");
	while(1){
		socklen_t cli_addr_size = sizeof(cli_addr);

		//Server accepts the connection from client and creates a new socket. Blocks on accept
		if ((connfd = accept(tcp_sock, (SA*)&cli_addr, &cli_addr_size)) < 0) {
			printf("server accept failed...\n");
			close(connfd);
			exit(0);
		}

		// Fork child process that handles TCP interaction/initiation
		int k = fork();
		if(k == 0){ 
			// Read command request (filename) from the client
			bytes_read = read(connfd, filename, sizeof(filename));
			filename[bytes_read] = '\0';
			
			strcpy(tmp_path, "/tmp/");
			strcat(tmp_path, filename);
			printf("tmp path: %s\n", tmp_path);
			FILE * tmp_file;
			tmp_file = fopen(tmp_path, "r");
			if (tmp_file == NULL){ // File does not exist on server
				printf("File does not exist on server. Waiting for valid request\n");
				write(connfd, "0", 1); // Send 0 back to client
				close(connfd);
			}
			else{ // File exists on server
				printf("File exists !\n");

				// Seek the file to get file size and rewind so that seek position is at beginning
				fseek(tmp_file, 0L, SEEK_END);
				unsigned int file_size = ftell(tmp_file);
				rewind(tmp_file);

				createUDP(); // Establishes relevant variables for UDP

				char sesh_init_response[9];
				sesh_init_response[0] = '2';
				memcpy(sesh_init_response + 1, &srv_udp_port, 4); // Copies srv_udp port to buf
				memcpy(sesh_init_response + 5, &file_size, 4); // Copies size of file to buf
				write(connfd, sesh_init_response, sizeof(sesh_init_response)); // Send 2 back to client with srv_udp_port
				
				for(int i = 1; i < sizeof(sesh_init_response); i++){
					printf("Tmp: %x\n", sesh_init_response[i]);
				}

				char recv_buf[4];
				// Reads client udp port in response to sending 2 back to client
				bytes_read = read(connfd, recv_buf, sizeof(recv_buf));

				unsigned int cli_udp_port;
				memcpy(&cli_udp_port, recv_buf, 4);
				// Changes client side port to the UDP port
				cli_addr.sin_port = htons(cli_udp_port); 
				printf("Client port received: %d\n", cli_udp_port);

				// Forks a process to handle mainly UDP audio packets (data plane)
				k = fork();
				if (k==0) {
					// Signal SIGIO
					signal(SIGIO, client_feedback_handler);

					// We must own the socket to receive the SIGIO message
					if (fcntl(srv_udp_sock, F_SETOWN, getpid()) < 0)
						printf("Unable to set process owner to us");

					// Arrange for nonblocking I/O and SIGIO delivery. TODO: Check if need async
					if (fcntl(srv_udp_sock, F_SETFL, FASYNC) < 0)
						printf("Unable to put client sock into non-blocking/async mode");

					int read_file;
					char audio_data[payload_size];
					int audio_file_fd = open(tmp_path, O_RDONLY, 0666);
					int count = 0;
					// Read packets of audio file and transmit via UDP
					while((read_file = read(audio_file_fd, audio_data, payload_size)) > 0){
						audio_data[read_file] = 0x00; // Set last byte to 0

						int num_sent = sendto(srv_udp_sock, audio_data, sizeof(audio_data),
							0, (const struct sockaddr *) &cli_addr, sizeof(cli_addr));
						// usleep(1/lambda);
						usleep(lambda);
						// nanosleep(&ts, NULL);
						control_congestion(); // Function to enforce congestion control
						printf("Num bytes sent: %d\n", num_sent);
						printf("Client port: %d\n", ntohs(cli_addr.sin_port));
					}

					printf("Sending termination\n");
					// No more bytes of file left to read so transmit character '5' 5 times
					// for(int i = 0; i < 5; i++){
					// 	write(connfd, "5", 1);
					// }
				}
				
				// close(connfd);
				// close(srv_udp_sock);
			}
		}
		// close(connfd);
	}
}

//Handling the feedback from the client
void client_feedback_handler(){
	char feedback[12];
	socklen_t cli_addr_size = sizeof(cli_addr);
	bytes_read = recvfrom(srv_udp_sock, (char *)feedback, sizeof(feedback), 0,
		 (struct sockaddr *) &cli_addr, &cli_addr_size);

	for(int i = 0; i < 12; i++){
		printf("Feedback[i]: %x\n", feedback[i]);
	}
	// Copy first four bytes into Q
	memcpy(&Q, feedback, 4);
	// Copy next four bytes into Q_Star
	memcpy(&Q_star, feedback + 4, 4);
	// Copy next four bytes into gamma
	memcpy(&Gamma, feedback + 8, 4);

	printf("\n feedback:\n");
	printf("\n Gamma: %f\n", Gamma);
	printf("\n Q: %f\n", Q);
	printf("\n Q_star: %f\n", Q_star);
	
}

//Creating a UDP socket for communication
void createUDP(){
	if ((srv_udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	udp_servaddr.sin_family = AF_INET;
	udp_servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	udp_servaddr.sin_port = htons(0);

	printf("Hit before bind createudp\n");
	// Bind the socket with the server address
	if (bind(srv_udp_sock, (const struct sockaddr *)&udp_servaddr, sizeof(udp_servaddr)) < 0 ){
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	printf("Hit middle createudp\n");
	socklen_t length = sizeof(udp_servaddr);
	//Fetching the port number assigned by the OS to the server and displaying it for the client to use
	if (getsockname(srv_udp_sock, (struct sockaddr *)&udp_servaddr, &length) == -1){
		perror("getsockname failed");
		exit(0);
	}
	fprintf(stdout,"Server UDP port number: %d\n", ntohs(udp_servaddr.sin_port));
	srv_udp_port = ntohs(udp_servaddr.sin_port);
}

void control_congestion(){
	if(mode == 0){
		if(Q < Q_star){
			lambda = lambda + fl_a;
		}
		else if(Q > Q_star){
			lambda = lambda - fl_a;
		}
	}
	else if(mode == 1){
		if(Q < Q_star){
			lambda = lambda + fl_a;
		}
		else if(Q > Q_star){
			lambda = lambda * fl_delta;
		}
	}
	else if(mode == 2){
		lambda = lambda + fl_epsilon * (Q_star - Q);
	}
	else{
		lambda = lambda + fl_epsilon * (Q_star - Q) - fl_beta * (lambda - Gamma);
	}
}

void read_control_param_list(){
	// Size of each initially set to 4 rows
	control_param_list = malloc(4);

	FILE * fp;
	char * line;
	size_t len = 0;
	int mode_idx = 0;

	fp = fopen("control-param.dat", "r");
	if(fp == NULL){
		perror("Error opening control param file\n");
		exit(1);
	}

	// Reads control paramater file
	while ((bytes_read = getline(&line, &len, fp)) != -1) {
		// Allocates memory for each row of 2d array
		control_param_list[mode_idx] = (float *) malloc(4 * sizeof(float));
		// Stores method type (eg. 0, 1, 2, 3) in split
		char * split = strtok(line, "#");
		// Stores first value (a) in split
		split = strtok(NULL, "#");
		control_param_list[mode_idx][0] = atof(split);
		// Stores second value (delta) in split
		split = strtok(NULL, "#");
		control_param_list[mode_idx][1] = atof(split);
		// Stores third value (epsilon) in split
		split = strtok(NULL, "#");
		control_param_list[mode_idx][2] = atof(split);
		// Stores fourth value (beta) in split
		split = strtok(NULL, "#");
		control_param_list[mode_idx][3] = atof(split);
		mode_idx++;
	}

	// For debugging purposes
	// for(int i = 0; i < 4; i++){
	// 	for(int j = 0; j < 4; j++){
	// 		printf("%f\n", control_param_list[i][j]);
	// 	}
	// }
}