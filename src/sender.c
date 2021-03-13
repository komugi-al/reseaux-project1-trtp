#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <time.h>

#include "log.h"
#include "socket_helpers.h"
#include "packet.h"

#define N 32
#define SEQ_MAX_SIZE 256
#define MAX_PKT_SIZE 12+MAX_PAYLOAD_SIZE+4

pkt_t* packets[N];
uint8_t start_window = 0;
uint8_t next_seqnum = 0;


int print_usage(char *prog_name) {
    ERROR("Usage:\n\t%s [-f filename] [-s stats_filename] receiver_ip receiver_port", prog_name);
    return EXIT_FAILURE;
}

int inc_buf(int index){
    return (index+1) % 32;
}

int update_packets(int recv_seqnum){
	uint8_t min = next_seqnum;
	uint8_t max = (next_seqnum+N) % SEQ_MAX_SIZE;
	if(max < min){
		if(recv_seqnum < min && recv_seqnum >= max){
			fprintf(stderr, "Unexpected seqnum\n");
			return -1;
		} 
	}else{
		if(recv_seqnum < min || recv_seqnum >= max){
			fprintf(stderr, "Unexpected seqnum\n");
			return -1;
		}
	}
	return recv_seqnum % N;
}

pkt_t* read_packet(char* buffer, int len){

}

/* 
 * Create and send a new data packet over the socket
 */
pkt_t* create_and_save_packet_data(char* buffer, int len, int fd){
	// Maybe checking return values of setters would be a good idea
	// Create a new packet and set corresponding fields
	pkt_t* new_pkt = pkt_new();
	pkt_set_type(new_pkt, 0b01);
	pkt_set_seqnum(new_pkt, next_seqnum);
	next_seqnum++;
	time_t second = 0;
	time(&second);
	pkt_set_timestamp(new_pkt, second);
	pkt_set_payload(new_pkt, buffer, len);

	packets[next_seqnum % N] = new_pkt;

	return new_pkt;
}

int encode_and_send_packet_data(pkt_t* pkt, int fd){
	size_t length = MAX_PKT_SIZE;
	char* new_buffer = (char*) malloc(length);
	pkt_status_code ret = pkt_encode(pkt, new_buffer, &length);
	if(ret) fprintf(stderr, "Error while encoding data packet.\n");	

	int n_ret = write(fd, new_buffer, length);
	return n_ret;
}

void sender_handler(const int sfd, int fd){
	struct pollfd fds[] = {{.fd=sfd, .events=POLLIN},{.fd=fd, .events=POLLIN}};
	int n_fds = 2;
	int ret;
	int n_read;
	uint8_t rwindow = 1;
	while(1){
		ret = poll(fds, n_fds, -1);
		if(ret == -1) fprintf(stderr, "Error with poll()");
		else {
			char buffer[MAX_PAYLOAD_SIZE];
			pkt_t* pkt;
			for(int i=0; i<n_fds; i++){
				if(!fds[i].revents) continue;
				if(fds[i].revents){
					if(fds[i].fd==fd && rwindow != 0){
						rwindow--;
						n_read = read(fds[i].fd, buffer, MAX_PAYLOAD_SIZE);
						fprintf(stderr,"Data read=%d, fd=%d\n", n_read, fds[i].fd);
						pkt = create_and_save_packet_data(buffer, n_read, fd);
						encode_and_send_packet_data(pkt, sfd);
						if(n_read==0) break; 
					} else if(fds[i].fd==sfd){
						n_read = read(fds[i].fd, buffer, MAX_PAYLOAD_SIZE);
						pkt = read_packet(buffer, n_read); 
						rwindow = pkt->window;
						if(pkt->type == PTYPE_ACK) {
							int idx = update_packets(pkt->seqnum);
							if(idx != -1){
								while(start_window <= idx){
									pkt_del(packets[start_window]);
									start_window++;
								}	
							}								
						}
							
					}
					fflush(NULL);
				} 
			}
		}
	}
}

int main(int argc, char **argv) {

   int opt;
   char *filename = NULL;
   char *stats_filename = NULL;
   char *receiver_ip = NULL;
   char *receiver_port_err;
   uint16_t receiver_port;

   while ((opt = getopt(argc, argv, "f:s:h")) != -1) {
       switch (opt) {
       case 'f':
           filename = optarg;
           break;
       case 'h':
           return print_usage(argv[0]);
       case 's':
           stats_filename = optarg;
           break;
       default:
           return print_usage(argv[0]);
       }
   }

   if (optind + 2 != argc) {
       ERROR("Unexpected number of positional arguments");
       return print_usage(argv[0]);
   }

   receiver_ip = argv[optind];
   receiver_port = (uint16_t) strtol(argv[optind + 1], &receiver_port_err, 10);
   if (*receiver_port_err != '\0') {
       ERROR("Receiver port parameter is not a number");
       return print_usage(argv[0]);
   }

   ASSERT(1 == 1); // Try to change it to see what happens when it fails
   DEBUG_DUMP("Some bytes", 11); // You can use it with any pointer type

    // This is not an error per-se.
   ERROR("Sender has following arguments: filename is %s, stats_filename is %s, receiver_ip is %s, receiver_port is %u", filename, stats_filename, receiver_ip, receiver_port);

   DEBUG("You can only see me if %s", "you built me using `make debug`");
   ERROR("This is not an error, %s", "now let's code!");

   // Now let's code!
    
   //Create the FD who would need to get the data
   int fd = filename == NULL ? 0 : open(filename, O_RDONLY);
	if(fd < 0) {
		fprintf(stderr, "Error while opening file.\n");
		return EXIT_FAILURE;
	}

   // Create the socket to connect with receiver
	struct sockaddr_in6 addr;
	const char *err = real_address(receiver_ip, &addr);
   int sfd = create_socket(NULL, -1, &addr, receiver_port);

   /* Process I/O */
	sender_handler(sfd, fd);

	close(fd);
	close(sfd);

   return EXIT_SUCCESS;
}


