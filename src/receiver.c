#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include "log.h"
#include "packet.h"
#include "socket_helpers.h"
#include "config.h"

#define RESP_LEN 10

pkt_t *window[N];
uint8_t window_size = 31; // Logical size
uint32_t pkt_last_timestamp;
uint8_t next_seqnum;
uint8_t idx = 0; // Index of the seqnum in my buffer
stat_t stats;

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
	int ret = 1;
	/* If there was any errors during packet decoding, ignore it */
	if(pkt_decode(buffer, length, recv_pkt)) return 2;

	DEBUG("recv_pkt->length %d\n", recv_pkt->length);
	
	uint8_t recv_seqnum = pkt_get_seqnum(recv_pkt);
	pkt_last_timestamp = pkt_get_timestamp(recv_pkt);
	
	uint8_t min = next_seqnum;
	uint8_t max = (next_seqnum+N) % MAX_SEQ_SIZE;
	if(max < min){
		if(recv_seqnum < min && recv_seqnum >= max){
			ERROR("Unexpected seqnum\n");
			stats.packet_ignored += 1;
			return 2;
		}
	}else{
		if(recv_seqnum < min || recv_seqnum >= max){
			ERROR("Unexpected seqnum\n");
			stats.packet_ignored += 1;
			return 2;
		}
	}

	/* End of data transmission */
	if(!pkt_get_length(recv_pkt) && (recv_seqnum == next_seqnum)){
		ret = 0;
	}
	
	idx = recv_seqnum % N;
	
	/* Response packet to send back */
	pkt_t* resp_pkt = pkt_new();

	/* Send NACK */
	if(pkt_get_tr(recv_pkt)) {
		//paquet tronqué -> PTYPE_NACK
		stats.data_truncated_received += 1;
		stats.nack_sent += 1;
		DEBUG("Starting NACK\n");
		pkt_set_type(resp_pkt, 0b11);
		pkt_set_seqnum(resp_pkt, recv_seqnum);
	} else {
		stats.data_received += 1;
		stats.ack_sent += 1;
		DEBUG("Starting ACK\n");
		pkt_set_type(resp_pkt, 0b10);
		/* Add the packet to the buffer */
		if(window[idx] != NULL){
			stats.packet_duplicated += 1;
		}
		window[idx] = recv_pkt;
		window_size--;
		/* The received seqnum is not equal to the expected seqnum */
		if(recv_seqnum != next_seqnum && window[0] == NULL){
			pkt_set_seqnum(resp_pkt, next_seqnum);
		} else {
			/* Iterate over the buffer until there is no more packets, i.d. next_seqnum hasn't arrived yet */
			int n_wri;
			while(window[idx] != NULL && idx < N){
				n_wri = write(1, window[idx]->payload, pkt_get_length(window[idx]));
				if(n_wri == -1) ERROR("Error while writing packet to stdout\n");
				pkt_del(window[idx]);
				window[idx] = NULL;
				window_size++;
				next_seqnum = (next_seqnum + 1) % MAX_SEQ_SIZE;
				idx = (idx + 1) % N;
			}
			pkt_set_seqnum(resp_pkt, next_seqnum);
		}	
	}

	size_t enco_len = RESP_LEN;
	pkt_set_window(resp_pkt, window_size);
	pkt_set_timestamp(resp_pkt, pkt_last_timestamp);
	pkt_encode(resp_pkt, buffer, &enco_len);
	
	DEBUG("window_size: %d, next_seqnum: %d\n", resp_pkt->window, resp_pkt->seqnum);

	return ret;
}

void receiver_handler(const int sfd){
	struct pollfd fds[] = {{.fd=sfd, .events=POLLIN}};
	int n_fds = 1;
	int ret = 1;
	int n_ret;
	while(ret){
		if(poll(fds, n_fds, -1) == -1){
			ERROR("Error with poll()");
		} else {
			char buffer[MAX_PKT_SIZE];
			if(fds[0].revents && POLLIN){
				n_ret = read(fds[0].fd, buffer, MAX_PKT_SIZE);
				if(n_ret==-1) {
					ERROR("Error while reading sfd\n");
				}
				DEBUG("STARTING handle_packet()\n");
				ret = handle_packet(buffer, n_ret);
				DEBUG("handle_packet() returned %d\n", ret);
				if(ret>=0){
					DEBUG("Writing response to socket\n");
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
	// init stats packet
	memset(&stats, 0, sizeof(stat_t));
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
		window[i] = NULL;
	}

	memset(stats, 0, sizeof(stat_t));

	receiver_handler(sfd);

	close(sfd);

	ERROR("Data received : %d\n", stats.data_received);
	ERROR("Data truncated recieved : %d\n", stats.data_truncated_received);
	ERROR("Ack sent : %d\n", stats.ack_sent);
	ERROR("Nack sent : %d\n", stats.nack_sent);
	ERROR("Ignored packets : %d\n", stats.packet_ignored);
	ERROR("Duplicated packets : %d\n", stats.packet_duplicated);

	return EXIT_SUCCESS;
}
