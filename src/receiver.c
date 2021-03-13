#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include "log.h"
#include "packet.h"
#include "socket_helpers.h"

#define N 32
#define RESP_LEN 10
#define SEQ_MAX_SIZE 256
#define PKT_MAX_SIZE 12+MAX_PAYLOAD_SIZE+4

pkt_t *data[N];
uint8_t window_size = N; // Logical size
uint32_t pkt_last_timestamp;
uint8_t next_seqnum;

int print_usage(char *prog_name) {
	ERROR("Usage:\n\t%s [-s stats_filename] listen_ip listen_port", prog_name);
	return EXIT_FAILURE;
}

/* Handle a packet
 * @return: 0 when all data has been received, 1 when a packet can be send back, 2 when the packet had to be ignored
 */
int handle_packet(char* buffer, int length){
	/* Initialisation of received packet */
	pkt_t* recv_pkt = pkt_new();
	pkt_status_code ret = pkt_decode(buffer, length, recv_pkt);
	/* If there was any errors during packet decoding, ignore it */
	if(ret) return 2;
	
	uint8_t recv_seqnum = pkt_get_seqnum(recv_pkt);
	pkt_last_timestamp = pkt_get_timestamp(recv_pkt);
	
	uint8_t min = next_seqnum;
	uint8_t max = next_seqnum+N % SEQ_MAX_SIZE;
	uint8_t idx = 0; // Index of the seqnum in my buffer
	if(max < min){
		if(recv_seqnum < min && recv_seqnum >= max){
			fprintf(stderr, "Unexpected seqnum\n");
			return 2;
		} else {
			idx = (recv_seqnum + SEQ_MAX_SIZE) - min; // Calculate index if max < min
		}
	}else{
		if(recv_seqnum < min || recv_seqnum >= max){
			fprintf(stderr, "Unexpected seqnum\n");
			return 2;
		} else {
			idx = recv_seqnum - min; // Calculate index if min < max
		}
	}
	
	/* End of data transmission */
	if(!pkt_get_length(recv_pkt) && (recv_seqnum == next_seqnum)){
		return 0;
	}

	/* Response packet to send back */
	pkt_t* resp_pkt = pkt_new();
	char resp_buffer[RESP_LEN];

	/* Send NACK */
	if(pkt_get_tr(recv_pkt)) {
		//paquet tronqué -> PTYPE_NACK
		pkt_set_type(resp_pkt, 0b11);
		pkt_set_seqnum(resp_pkt, recv_seqnum);
	} else {
		pkt_set_type(resp_pkt, 0b10);
		/* Add the packet to the buffer */
		data[idx] = recv_pkt;
		window_size--;
		/* The received seqnum is not equal to the expected seqnum */
		if(recv_seqnum != next_seqnum && data[0] == NULL){
			pkt_set_seqnum(resp_pkt, next_seqnum);
		} else {
			/* Iterate over the buffer until there is no more packets, i.d. next_seqnum hasn't arrived yet */
			int ret;
			while(data[idx] != NULL && idx < N){
				ret = write(1, data[idx]->payload, pkt_get_length(data[idx]));
				if(ret == -1) fprintf(stderr, "Error while writing packet to stdout\n");
				pkt_del(data[idx]);
				data[idx] = NULL;
				window_size++;
				idx = idx + 1;
			}
			next_seqnum = next_seqnum + idx % SEQ_MAX_SIZE;
			pkt_set_seqnum(resp_pkt, next_seqnum);
		}	
	}
	size_t enco_len = RESP_LEN;
	pkt_set_window(resp_pkt, window_size);
	pkt_set_timestamp(resp_pkt, pkt_last_timestamp);
	pkt_encode(resp_pkt, resp_buffer, &enco_len);

	return 1;
}

void receiver_handler(const int sfd){
	struct pollfd fds[] = {{.fd=sfd, .events=POLLIN}};
	int n_fds = 1;
	int ret = 1;
	int n_ret;
	while(ret){
		if(poll(fds, n_fds, -1) == -1) fprintf(stderr, "error with poll()");
		else {
			char buffer[PKT_MAX_SIZE];
			if(fds[0].revents && POLLIN){
				n_ret = read(fds[0].fd, buffer, PKT_MAX_SIZE);
				if(n_ret==-1) {
					fprintf(stderr, "Error while reading sfd\n");
				}
				ret = handle_packet(buffer, n_ret);
				if(ret==1){
					n_ret = write(sfd, buffer, RESP_LEN);
				}
			}
			fflush(NULL);
		}
	}
}


int main(int argc, char **argv) {
	int opt;

	char *stats_filename = NULL;
	char *listen_ip = NULL;
	char *listen_port_err;
	uint16_t listen_port;

	while ((opt = getopt(argc, argv, "s:h")) != -1) {
	  switch (opt) {
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

	listen_ip = argv[optind];
	listen_port = (uint16_t) strtol(argv[optind + 1], &listen_port_err, 10);
	if (*listen_port_err != '\0') {
		ERROR("Receiver port parameter is not a number");
		return print_usage(argv[0]);
	}

	ASSERT(1 == 1); // Try to change it to see what happens when it fails
	DEBUG_DUMP("Some bytes", 11); // You can use it with any pointer type

	// This is not an error per-se.
	ERROR("Receiver has following arguments: stats_filename is %s, listen_ip is %s, listen_port is %u",
	  stats_filename, listen_ip, listen_port);

	DEBUG("You can only see me if %s", "you built me using `make debug`");
	ERROR("This is not an error, %s", "now let's code!");

	struct sockaddr_in6 addr;
	const char *ret = real_address(listen_ip, &addr);
	if (ret) {
		fprintf(stderr, "Could not resolve hostname %s: %s\n", listen_ip, ret);
		return EXIT_FAILURE;
	}

	/* Create socket */
	int sfd = create_socket(&addr, listen_port, NULL, -1);
	if(sfd < 0) {
		fprintf(stderr, "Could not create socket\n");
		close(sfd);
		return EXIT_FAILURE;
	}

	/* Connection establishment */
	if(wait_for_client(sfd) < 0) {
		fprintf(stderr, "Could not connect the socket upon receiving the first message\n");
		return EXIT_FAILURE;   
	}

	/* Data array initialization */
	int i=0;
	for(;i<N;i++){
		data[i] = NULL;
	}

	receiver_handler(sfd);

	close(sfd);

	return EXIT_SUCCESS;
}
