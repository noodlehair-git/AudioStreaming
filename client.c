#define _GNU_SOURCE // F_SETSIG
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <arpa/inet.h> 
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <errno.h>
#define SA struct sockaddr

void createUDP();
struct sockaddr_in tcp_servaddr, udp_servaddr, cliaddr;
int tcp_sock, udp_sock, connfd, fd, read_bytes, tcp_port, payload_size, cli_udp_port, buf_size, target_buf, flag;
float fl_Gamma, fl_Q, fl_Q_star;
char ip[15], sendbuff[100], sendbuf[100], tmp[10], mode[2], str[10], tcp_ip[15], srv_udp_port[6], feedback[100], Q[10], Q_star[10], Gamma[10], audio_data[1500], buf[3000];
struct ifreq ifr;
socklen_t length;
struct timeval tv;

void prepare_feedback(){
	memset(feedback, '\0', sizeof(feedback));
	memset(Q, '\0', sizeof(Q));
	memset(Q_star, '\0', sizeof(Q_star));
	memset(Gamma, '\0', sizeof(Gamma));
	fl_Q = fl_Q_star - strlen(buf);	
	sprintf(Q, "%f", fl_Q);
	sprintf(Q_star, "%f", fl_Q_star);
	sprintf(Gamma, "%f", fl_Gamma);
	strcpy(feedback, Q);
	strcat(feedback, "#\0");
	strcat(feedback, Q_star);
	strcat(feedback, "#\0");
	strcat(feedback, Gamma);
	strcat(feedback, "#\0");

}

void createUDP(){

	if ( (udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("socket creation failed"); 
		exit(EXIT_FAILURE); 
    	} 

	udp_servaddr.sin_family = AF_INET; 
	udp_servaddr.sin_port = htons(atoi(srv_udp_port)); 
	udp_servaddr.sin_addr.s_addr = inet_addr(tcp_ip); 

	//Getting the IP address of eth0 interface used by the host 
	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
	ioctl(udp_sock, SIOCGIFADDR, &ifr);
	strcpy(ip, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));

	cliaddr.sin_family = AF_INET; 
	cliaddr.sin_port = htons(0); 
	cliaddr.sin_addr.s_addr = inet_addr(ip); 

	if ( bind(udp_sock, (const struct sockaddr *)&cliaddr, sizeof(cliaddr)) < 0 ){ 	    
		perror("bind failed"); 
		exit(EXIT_FAILURE); 
	} 

	//Fetching the port number assigned by the OS to the server and displaying it for the client to use
	if (getsockname(udp_sock, (struct sockaddr *)&udp_servaddr, &length) == -1)
		perror("getsockname failed");
	else
		fprintf(stdout," UDP port number: %d\n", ntohs(udp_servaddr.sin_port));

	
	sprintf(sendbuff, "%d", ntohs(udp_servaddr.sin_port));
	write(tcp_sock, sendbuff, sizeof(sendbuff));
}

int main(int argc, char** argv) {
	

	//processing command-line input arguments 
	strcpy(tcp_ip,(argv[1]));
	tcp_port = atoi(argv[2]);
	payload_size = atoi(argv[3]);
	fl_Gamma = atof(argv[4]);
	buf_size = atoi(argv[5]);
	target_buf = atoi(argv[6]);
	//logfile

	//memetting strings
	memset(sendbuf, '\0', sizeof(sendbuf));
	memset(sendbuff, '\0', sizeof(sendbuff));
	memset(buf, '\0', sizeof(buf));
	memset(audio_data, '\0', sizeof(audio_data));
	//memset(tmp, '\0', sizeof(tmp));

	tcp_sock = socket(AF_INET, SOCK_STREAM, 0);

	if (tcp_sock == -1) { 
		fprintf(stdout, "socket creation failed...\n"); 
		exit(0); 
	} 
	else
		fprintf(stdout, " Socket successfully created..\n"); 

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
	 else
	         printf(" Connected to the server...\n"); 
	printf(" Enter filename: ");
	scanf("%s", sendbuf);

	write(tcp_sock, sendbuf, sizeof(sendbuf));

	while((read_bytes = read(tcp_sock, tmp, sizeof(tmp))) > 0){
		if(tmp[0] == '2')
			printf(" File found!\n");
		else{
			printf(" File not found!\n");
			exit(0);
		}

	}

	strncpy(srv_udp_port, tmp+1, 5);
	srv_udp_port[5]='\0';

	createUDP();

	flag = 0;

	while (1){
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET( udp_sock, &readfds);
		n = udp_sock + 1;
		tv.tv_sec = 1/fl_Gamma;
		tv.tv_usec = 0;
		activity = select(n, &readfds, NULL, NULL, NULL);

		if (activity < 0){
            		printf("select error");
        	}
		if (activity == 0){

		//write to dev/audio/ which automatically plays (some part of buf)

		//if buf empty and flag=1 : exit
		}
		else{	if(flag==0){
				printf("\n Activity detected on UDP sock....");
				prepare_feedback();
				if ((read_bytes = recvfrom(udp_sock, (char *)audio_data, sizeof(audio_data), 0, (struct sockaddr *) &udp_servaddr, &len)) > 0){
					if((audio_data[0]=='5')&&(audio_data[1]=='5')&&(audio_data[2]=='5')&&(audio_data[3]=='5')){
						printf("\n Transmission Complete");
						flag=1;
					}
					if(flag==0){	
						sendto(udp_sock, (const char *)feedback, strlen(feedback), 0, (const struct sockaddr *) &udp_servaddr, sizeof(udp_servaddr)); 
						strcat(buf, audio_data);
					}	
				}
			}

		}


	}

return 0;
}

