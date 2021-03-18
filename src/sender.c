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
#include "config.h"

pkt_t* windows[N];
uint8_t start_window = 0;
uint8_t next_seqnum = 0;
bool eot = false;

int print_usage(char *prog_name) {
    ERROR("Usage:\n\t%s [-f filename] [-s stats_filename] receiver_ip receiver_port", prog_name);
    return EXIT_FAILURE;
}

/*
 *	Clear all packets received to a valid seqnum and update de start_window value to the next not yet received packet
 */
void clear_received_packets(int recv_seqnum){
	uint8_t min = start_window;
	uint8_t max = (start_window+N) % MAX_SEQ_SIZE;
	if(max < min){
		if(recv_seqnum < min && recv_seqnum >= max){
			ERROR("Unexpected seqnum\n");
		} 
	}else{
		if(recv_seqnum < min || recv_seqnum >= max){
			ERROR("Unexpected seqnum\n");
		}
	}
	
		
	int idx = recv_seqnum % N;
	while(start_window != idx){
		DEBUG("start_window: %d, recv_seqnum: %d, windows[x] == NULL: %d\n", start_window, recv_seqnum, windows[start_window]==NULL);
		pkt_del(windows[start_window]);
		windows[start_window] = NULL;
		start_window = (start_window + 1) % N;
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
	time_t second = 0;
	time(&second);
	pkt_set_timestamp(new_pkt, second);
	pkt_set_payload(new_pkt, buffer, len);

	windows[next_seqnum % N] = new_pkt;

	next_seqnum++;
	
	return new_pkt;
}

void encode_and_send_packet_data(pkt_t* pkt, int fd){
	size_t length = MAX_PKT_SIZE;
	char* new_buffer = (char*) malloc(length);
	time_t second = 0;
	time(&second);
	pkt_set_timestamp(pkt, second);
	pkt_status_code ret = pkt_encode(pkt, new_buffer, &length);
	
	if(ret){
	  	ERROR("Error while encoding data packet.\n");
	}	

	size_t n_ret = write(fd, new_buffer, length);
	if(n_ret != length) {
		ERROR("Error with write() in encode_and_send_packet_data()\n");
		ERROR("Bytes written: %lu, Bytes expected: %lu\n", n_ret, length);
	}
}

void resend_timedout_packet(int sfd, int timeout){
	int idx = start_window;
	int i = 0;
	time_t second = 0;
	time(&second);
	while(windows[idx] != NULL && i < N){
		if(difftime(windows[idx]->timestamp, second-timeout) <= 0){
			encode_and_send_packet_data(windows[idx], sfd);
			idx = idx + 1 % N;
		}
		i++;
	}
}

void sender_handler(const int sfd, int fdin){
	struct pollfd fds[] = {{.fd=sfd, .events=POLLIN},{.fd=fdin, .events=POLLIN}};
	int n_fds = 2;
	int timeout = 50;
	int ret;
	int n_read = 0;
	uint8_t rwindow = 1;
	uint8_t end = false;
	while(!end && n_read != -1){
		
		resend_timedout_packet(sfd, timeout);

		ret = poll(fds, n_fds, timeout);
		if(ret == -1) {
			ERROR("Error with poll()\n");
		} else {
			char buffer[MAX_PAYLOAD_SIZE];
			pkt_t* pkt = pkt_new();
			for(int i=0; i<n_fds; i++){

				if(!fds[i].revents) continue;

				if(fds[i].fd==fdin && rwindow != 0 && !eot){
					DEBUG("Reading from stdin\n");
					rwindow--;
					n_read = read(fds[i].fd, buffer, MAX_PAYLOAD_SIZE);
					pkt = create_and_save_packet_data(buffer, n_read);
					encode_and_send_packet_data(pkt, sfd);

					if(n_read==0) {
						DEBUG("EOF received\n");
						eot = true;
					}
				} else if(fds[i].fd==sfd) { // Data from socket (ACK & NACK)
					DEBUG("Reading from socket\n");
					n_read = read(fds[i].fd, buffer, MAX_PAYLOAD_SIZE);
					if(n_read >= 0){
						DEBUG("read() return >= 0\n");
						ret = pkt_decode(buffer, n_read, pkt);
						if(ret) {
							ERROR("Error with pkt_decode() %d read %d\n", ret, n_read);
						} else {
							if(pkt->seqnum == next_seqnum){
								rwindow = pkt->window;
							}
							if(pkt->type == PTYPE_ACK) {
								DEBUG("pkt->type is PTYPE_ACK\n");
								/* If end of transmission, stop*/
								if(eot && (pkt->seqnum == next_seqnum)) end = true; 
								DEBUG("pkt->seqnum %d, next_seqnum %d\n", pkt->seqnum, next_seqnum);
								clear_received_packets(pkt->seqnum);
							}else if(pkt->type == PTYPE_NACK){
								DEBUG("pkt->type is PTYPE_NACK\n");
								/* Send a packet again if there was an error */
								encode_and_send_packet_data(windows[pkt->seqnum%N], sfd);
							}
						}
					}
				}
				fflush(NULL);
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

	memset(windows, 0, sizeof(windows));


   /* Process I/O */
	sender_handler(sfd, fd);

	close(fd);
	close(sfd);

   return EXIT_SUCCESS;
}


