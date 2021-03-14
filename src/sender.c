#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <time.h>

#include "log.h"
#include "socket_helpers.h"
#include "packet.h"

#define N 31
#define MAX_SEQ_SIZE 256
#define MAX_PKT_SIZE 12+MAX_PAYLOAD_SIZE+4

pkt_t* packets[N];
uint8_t start_window = 0;
uint8_t next_idx = 0;
uint8_t next_seqnum = 0;
uint8_t eot = false;

int print_usage(char *prog_name) {
    ERROR("Usage:\n\t%s [-f filename] [-s stats_filename] receiver_ip receiver_port", prog_name);
    return EXIT_FAILURE;
}

/*
 *	Clear all packets received to a valid seqnum and update de start_window value to the next not yet received packet
 */
void clear_received_packets(int recv_seqnum){
	uint8_t min = next_seqnum;
	uint8_t max = (next_seqnum+N) % MAX_SEQ_SIZE;
	if(max < min){
		if(recv_seqnum < min && recv_seqnum >= max){
			fprintf(stderr, "Unexpected seqnum\n");
			return;
		} 
	}else{
		if(recv_seqnum < min || recv_seqnum >= max){
			fprintf(stderr, "Unexpected seqnum\n");
			return;
		} 
	}
	
	DEBUG("start_window: %d, recv_seqnum: %d, packets[x] == NULL: %d\n", start_window, recv_seqnum, packets[start_window]==NULL);
	uint8_t total = 0;
	uint8_t first_pkt = packets[start_window]->seqnum;
	if(recv_seqnum < first_pkt){
		total = recv_seqnum + MAX_SEQ_SIZE - first_pkt;
	} else {
		total = recv_seqnum - first_pkt;
	}

	DEBUG("clear_received_packets(): beginning clearing\n");
	int idx = 0;
	while(idx < total){
		DEBUG("start_window: %d, recv_seqnum: %d, packets[x] == NULL: %d\n", start_window, recv_seqnum, packets[start_window]==NULL);
		pkt_del(packets[start_window]);
		packets[start_window] = NULL;
		start_window = (start_window + 1) % N;
		idx++;
	}	
}

/* 
 * Create and send a new data packet over the socket
 */
pkt_t* create_and_save_packet_data(char* buffer, int len){
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

	packets[next_idx] = new_pkt;

	next_idx = (next_idx + 1) % N;
	
	return new_pkt;
}

int encode_and_send_packet_data(pkt_t* pkt, int fd){
	size_t length = MAX_PKT_SIZE;
	char* new_buffer = (char*) malloc(length);
	time_t second = 0;
	time(&second);
	pkt_set_timestamp(pkt, second);
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
		int idx = start_window;
		time_t second = 0;
		time(&second);
		while(packets[idx] != NULL && idx < next_idx){
			if(difftime(packets[idx]->timestamp, second-2000) <= 0){
				encode_and_send_packet_data(packets[idx], sfd);
			}
			idx = idx + 1 % N;
		}

		ret = poll(fds, n_fds, 2000);
		if(ret == -1) {
			fprintf(stderr, "Error with poll()");
		} else {
			char buffer[MAX_PAYLOAD_SIZE];
			pkt_t* pkt = pkt_new();
			for(int i=0; i<n_fds; i++){
				if(!fds[i].revents) continue;
				if(fds[i].revents){
					if(fds[i].fd==fd && rwindow != 0){
						rwindow--;
						n_read = read(fds[i].fd, buffer, MAX_PAYLOAD_SIZE);
						pkt = create_and_save_packet_data(buffer, n_read);
						encode_and_send_packet_data(pkt, sfd);
						if(n_read==0) {
							eot = true;
						}
					} else if(fds[i].fd==sfd){ // Data from socket
						n_read = read(fds[i].fd, buffer, MAX_PAYLOAD_SIZE);
						int ret = pkt_decode(buffer, n_read, pkt);
						if(ret == PKT_OK){
							rwindow = pkt->window;
							if(pkt->type == PTYPE_ACK) {
								DEBUG("pkt->type is PTYPE_ACK, window: %d\n", pkt->window);
								/* If end of transmission, stop*/
								if(eot && (pkt->seqnum % N) == start_window) break; 
								clear_received_packets(pkt->seqnum);
								DEBUG("clear_received_packets() done\n");
							}else if(pkt->type == PTYPE_NACK){
								DEBUG("pkt->type is PTYPE_NACK\n");
								/* Send a packet again if there was an error */
								encode_and_send_packet_data(packets[pkt->seqnum%N], sfd);
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
	if(err) {
		fprintf(stderr, "Could not resolve hostname %s: %s\n", receiver_ip, err);
		return EXIT_FAILURE;
	}
   int sfd = create_socket(NULL, -1, &addr, receiver_port);

	memset(packets, 0, sizeof(packets));


   /* Process I/O */
	sender_handler(sfd, fd);

	close(fd);
	close(sfd);

   return EXIT_SUCCESS;
}


