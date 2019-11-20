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
#include <sys/time.h>
#include <sys/ioctl.h>
#include <net/if.h>
#define SA struct sockaddr

struct sockaddr_in tcp_servaddr, udp_servaddr, cli_addr;
int tcp_sock, srv_udp_sock, connfd, fd, bytes_read, tcp_port, payload_size, srv_udp_port;
float lambda, Q, Q_star, Gamma, fl_a, fl_delta, fl_epsilon, fl_beta;
float ** control_param_list;
FILE * mode_fd;
char ip[15], logfile[100], filename[100], tmp[10], str[10], cli_udp_port[10], a[5], delta[5], epsilon[5], beta[5], buf[100];
char tmp_path[110];
int mode;

void control_congestion();
void createUDP();
void client_feedback_handler();

struct ifreq ifr;

int main(int argc, char** argv) {
	if(argc < 6){
		printf("Please input all arguments: [tcp-port][payload-size][init-lambda][mode][logfile]\n");
		exit(1);
	}
	// Signal SIGIO
	signal(SIGIO, client_feedback_handler);

	//processing command-line input arguments
	tcp_port = atoi(argv[1]);
	payload_size = atoi(argv[2]);
	lambda = atoi(argv[3]);
	mode = atoi(argv[4]);
	strcpy(logfile, argv[5]);

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
	for(int i = 0; i < 4; i++){
		for(int j = 0; j < 4; j++){
			printf("%f\n", control_param_list[i][j]);
		}
	}


	if ((tcp_sock = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		printf("socket creation failed...\n");
		exit(0);
	}

	//Getting the IP address of eth0 interface used by the host. Copies into string ip
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
	ioctl(tcp_sock, SIOCGIFADDR, &ifr);
	strcpy(ip, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

	tcp_servaddr.sin_family = AF_INET;
	tcp_servaddr.sin_addr.s_addr = inet_addr(ip);
	tcp_servaddr.sin_port = htons(tcp_port);

	// Bind the socket with the server address
	if ((bind(tcp_sock, (SA*)&tcp_servaddr, sizeof(tcp_servaddr))) != 0) {
		printf("socket bind failed...\n");
		exit(0);
	}

	printf("Local IP address: %s\n", ip);

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

		//reading the command request from the client
		bytes_read = read(connfd, filename, sizeof(filename));
		filename[bytes_read] = '\0';
		
		strcpy(tmp_path, "/tmp/");
		strcat(tmp_path, filename);
		printf("tmp path: %s\n", tmp_path);
		fd = open(tmp_path, O_RDONLY, 0666);
		if (fd < 0){ // File does not exist on server
			printf("File does not exist on server. Waiting for valid request\n");
			write(connfd, "0", 1); // Send 0 back to client
		}
		else { // File exists on server
			printf("File Exists!!\n");
			// Establishes relevant variables for UDP
			createUDP();

			char sesh_init_response[5];
			sesh_init_response[0] = '2';
			memcpy(sesh_init_response + 1, &srv_udp_port, 4);
			write(connfd, sesh_init_response, 5); // Send 2 back to client
			
			for(int i = 1; i < 5; i++){
				printf("Tmp: %x\n", sesh_init_response[i]);
			}

			// Reads client udp port in response to sending 2 back to client
			bytes_read = read(connfd, cli_udp_port, sizeof(cli_udp_port));

			// Changes client side port to the UDP port
			cli_addr.sin_port = htons(atoi(cli_udp_port)); // TODO: CANNOT DO THIS BC CAN HAVE MULTIPLE CLIENTS

			// Forks a process to handle UDP audio packets (data plane)
			int k = fork();
			if (k==0) {
				int read_file;
				char audio_data[payload_size];
				// Read packets of audio file and transmit via UDP
				while((read_file = read(fd, audio_data, payload_size)) > 0){
					audio_data[read_file] = 0x00;
					sendto(srv_udp_sock, (const char *)audio_data, sizeof(audio_data), 0, (const struct sockaddr *) &cli_addr, sizeof(cli_addr));
					usleep(1/lambda);
					control_congestion(); // Function to enforce congestion control
				}

				// No more bytes of file left to read so transmit character '5' 5 times
				for(int i = 0; i < 5; i++){
					write(tcp_sock, "5", 1);
				}
			}
		}
	}
	close(connfd);
	close(tcp_sock);
}

//Handling the feedback from the client
void client_feedback_handler(){
	char feedback[12];
	socklen_t cli_addr_size = sizeof(cli_addr);
	bytes_read = recvfrom(srv_udp_sock, (char *)feedback, sizeof(feedback), 0,
		 (struct sockaddr *) &cli_addr, &cli_addr_size);

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
	udp_servaddr.sin_addr.s_addr = inet_addr(ip);
	udp_servaddr.sin_port = htons(0);

	// Bind the socket with the server address
	if (bind(srv_udp_sock, (const struct sockaddr *)&udp_servaddr, sizeof(udp_servaddr)) < 0 ){
		perror("bind failed");
		exit(EXIT_FAILURE);
	}

	socklen_t length = sizeof(udp_servaddr);
	//Fetching the port number assigned by the OS to the server and displaying it for the client to use
	if (getsockname(srv_udp_sock, (struct sockaddr *)&udp_servaddr, &length) == -1){
		perror("getsockname failed");
		exit(0);
	}
	fprintf(stdout,"Server UDP port number: %d\n", ntohs(udp_servaddr.sin_port));
	srv_udp_port = ntohs(udp_servaddr.sin_port);

	// We must own the socket to receive the SIGIO message
	if (fcntl(srv_udp_sock, F_SETOWN, getpid()) < 0)
		printf("Unable to set process owner to us");

	// Arrange for nonblocking I/O and SIGIO delivery
	if (fcntl(srv_udp_sock, F_SETFL, O_NONBLOCK | FASYNC) < 0)
		printf("Unable to put client sock into non-blocking/async mode");
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
