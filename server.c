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

struct sockaddr_in tcp_servaddr, udp_servaddr, cliaddr;
int tcp_sock, udp_sock, connfd, fd, read_bytes, tcp_port, payload_size, udp_port, k, i, j, x, flag;
float lambda, fl_a, fl_delta, fl_epsilon, fl_beta, fl_Q, fl_Q_star, fl_Gamma;
FILE * mode_fd;
char ip[15], readbuff[100], readbuf[100], tmp[10], mode[2], str[10], cli_udp_port[10], a[5], delta[5], epsilon[5], beta[5], Q, Q_star, Gamma, buf[100], feedback[200], last_pack[6];
struct ifreq ifr;
socklen_t length;

//Reading Control Parameter File
void read_mode(){
	mode_fd = fopen("control-param.dat", "r");
	mode[1]='\0';
	while((fgets(buf,60,mode_fd))){

	 if(mode[0]==buf[0])
		break;

	}
	x=0;
	for(i = 2; buf[i]!='#'; i++){
		a[x] = buf[i];
		x++;
	}
	
	a[x]='\0';
	x=0;

	for(j = i+1; buf[j]!='#'; j++){
		delta[x] = buf[j];
		x++;
	}

	
	delta[x]='\0';
	x=0;

	for(i = j+1; buf[i]!='#'; i++){
		epsilon[x] = buf[i];
		x++;
	}
	
	epsilon[x]='\0';
	x=0;
		
	for(j = i+1; buf[j]!='#'; j++){
		beta[x] = buf[j];
		x++;
	}
	
	beta[x]='\0';

	fl_a = atof(a);
	fl_delta = atof(delta);
	fl_epsilon = atof(epsilon);
	fl_beta = atof(beta);

	//test parameters
	printf("\nmode : %c", mode[0]);
	printf("\na : %f", atof(a));
	printf("\ndelta : %f", atof(delta));
	printf("\nepsilon : %f", atof(epsilon));
	printf("\nbeta : %f\n", atof(beta)); 
}



//Handling the feedback from the client
client_feedback_handler(){

	while ((read_bytes = recvfrom(udp_sock, (char *)feedback, sizeof(feedback), 0, (struct sockaddr *) &cliaddr, &len)) > 0){
		x=0;
		for(i = 0; feedback[i]!= '#'; i++){
			Q[x] = feedback[i];
			x++;
		}
		Q[x]='\0';
		x=0;
		
		for(j = i+1; feedback[j]!= '#'; j++){
			Q_star[x] = feedback[j];
			x++;
		}
		Q_star[x]='\0';
		x=0;
		for(i = j+1; feedback[i]!= '#'; i++){
			Gamma[x] = feedback[i];
			x++;
		}
		Gamma[x]='\0';
		x=0;
			
	}

	fl_Gamma = atof(Gamma);	
	fl_Q = atof(Q);
	fl_Q_star = atof(Q_star);

	printf("\n feedback:\n");
	printf("\n Gamma: %f\n", fl_Gamma);
	printf("\n Q: %f\n", fl_Q);	
	printf("\n Q_star: %f\n", fl_Q_star);	

}


//Creating a UDP socket for communication
void createUDP(){

	if ( (udp_sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) { 
		perror("socket creation failed"); 
		exit(EXIT_FAILURE); 
    	} 

	udp_servaddr.sin_family = AF_INET;  
    	udp_servaddr.sin_addr.s_addr = inet_addr(ip); 
    	udp_servaddr.sin_port = htons(0);
 	
	    
    	// Bind the socket with the server address 
   	 if ( bind(udp_sock, (const struct sockaddr *)&udp_servaddr,  
    	        sizeof(udp_servaddr)) < 0 ) 
    	{ 
     	   perror("bind failed"); 
     	   exit(EXIT_FAILURE); 
   	 } 

	//Fetching the port number assigned by the OS to the server and displaying it for the client to use
	if (getsockname(udp_sock, (struct sockaddr *)&udp_servaddr, &length) == -1)
		perror("getsockname failed");
	else
		fprintf(stdout," UDP port number: %d\n", ntohs(udp_servaddr.sin_port));


	sprintf(str, "%d", ntohs(udp_servaddr.sin_port));
}


int main(int argc, char** argv) {
	

	//processing command-line input arguments 
	tcp_port = atoi(argv[1]);
	strcpy(mode, argv[4]);
	payload_size = atoi(argv[2]);
	lambda = atoi(argv[3]);
	//logfile

	char audio_data[payload_size];
	//memetting strings
	memset(readbuf, '\0', sizeof(readbuf));
	memset(readbuff, '\0', sizeof(readbuff));
	memset(ip, '\0', sizeof(ip));
	memset(str, '\0', sizeof(str));
	memset(tmp, '\0', sizeof(tmp));
	memset(cli_udp_port, '\0', sizeof(cli_udp_port));

	tcp_sock = socket(AF_INET, SOCK_STREAM, 0); 
	if (tcp_sock == -1) { 
		printf("socket creation failed...\n"); 
		exit(0); 
	} else
		printf(" Socket successfully created..\n"); 

	//Getting the IP address of eth0 interface used by the host 
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
	} else
		printf(" Socket successfully binded..\n"); 

	printf(" My IP address: %s\n", ip);
	length = sizeof(tcp_servaddr);


	//server listens to requests 
	if ((listen(tcp_sock, 5)) != 0) { 
		printf("Listen failed...\n"); 
		exit(0); 
	} else
		printf(" Server listening...\n"); 
	
	
	//while for multiple requests
        printf("******************************SERVER*****************************"); 
        while(1){
		//server accepts the connection from client and creates a new socket 
		connfd = accept(tcp_sock, (SA*)&cliaddr, &length); 
		if (connfd < 0) { 
			printf("server accept failed...\n");
			close(connfd); 
			exit(0); 
		} else
			printf(" \n Server accepted the client...\n"); 

		printf("\n Waiting for file name...\n");		
		
		
		//reading the command request from the client
                read_bytes = read(connfd, readbuf, sizeof(readbuf));
		readbuf[read_bytes] = '\0';
                printf("\n Searching for filename: %s\n", readbuf);


                strcpy(readbuff, "/tmp/");
		strcat(readbuff, readbuf);
        	fd = open(readbuff, O_RDONLY, 0666); 
		if (fd < 0) 
           		{printf("\n File open failed!\n"); 
			 printf(" I will wait for another valid request"); 
                         tmp[0] = '0';
			 write(connfd, tmp, strlen(tmp));
			 continue;
                        }
        	else { printf("\n File openned!\n"); 
                         tmp[0] = '2'; 
		}
		
		tmp[1]='\0';
		createUDP();
		strcat(tmp, str);
		printf(" Sending %s", tmp);

		write(connfd, tmp, strlen(tmp));
		read_bytes = read(connfd, cli_udp_port, sizeof(cli_udp_port));

		//changing client side port to the UDP port 
    		cliaddr.sin_port = htons(atoi(cli_udp_port));

		//reading control parameters from the file
		read_mode();
		k = fork();
		if (k==0) {

				struct sigaction handler;

				//Set signal handler for SIGIO 
				handler.sa_handler = client_feedback_handler;
				handler.sa_flags = 0;

				//Create mask that mask all signals 
				if (sigfillset(&handler.sa_mask) < 0) 
					printf("sigfillset() failed");
				//No flags 
				handler.sa_flags = 0;

				if (sigaction(SIGIO, &handler, 0) < 0)
					printf("sigaction() failed for SIGIO");

				//We must own the socket to receive the SIGIO message
				if (fcntl(udp_sock, F_SETOWN, getpid()) < 0)
					printf("Unable to set process owner to us");

				//Arrange for nonblocking I/O and SIGIO delivery
				if (fcntl(udp_sock, F_SETFL, O_NONBLOCK | FASYNC) < 0)
					printf("Unable to put client sock into non-blocking/async mode");
				


				whlie(1){
					if((read_file = read(fd, audio_data, payload_size)) < 0){
						strcpy(last_pack, "55555");
						last_pack[5]='\0';
						sendto(udp_sock, (const char *)last_pack, sizeof(last_pack), 0, (const struct sockaddr *) &cliaddr, sizeof(cliaddr));
						break;
					}						
					
					while((read_file = read(fd, audio_data, payload_size)) > 0){
						audio_data[read_file] = 0x00;
						sendto(udp_sock, (const char *)audio_data, sizeof(audio_data), 0, (const struct sockaddr *) &cliaddr, sizeof(cliaddr));

					}
					usleep(1/lambda);

					if(method == 'a'){

						if(Q < Q_star)
							lambda = lambda + fl_a;
						else if(Q > Q_star)
							lambda = lambda - fl_a;
						else 
							lambda = lambda;
					}	

					else if(method == 'b'){

						if(Q < Q_star)
							lambda = lambda + fl_a;
						else if(Q > Q_star)
							lambda = lambda * fl_delta;
						else 
							lambda = lambda;
					}
					else if(method == 'c'){
						lambda = lambda + fl_epsilon * (Q_star - Q);
					}	
					else
						lambda = lambda + fl_epsilon (Q_star - Q) - fl_beta(lambda - Gamma);

				
				

				}

			

		}
	}

	close(connfd);
	close(tcp_sock);
	return 0;

}
	  
