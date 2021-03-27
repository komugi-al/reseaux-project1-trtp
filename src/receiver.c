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
uint8_t next_seqnum = 0;
stat_t stats;

int print_usage(char *prog_name) {
	ERROR("Usage:\n\t%s [-s stats_filename] listen_ip listen_port", prog_name);
	return EXIT_FAILURE;
}

void send_statistics(const char* filename){
	FILE *fd = filename == NULL ? stderr : fopen(filename, "w");
	if(fd == NULL) {
		fprintf(stderr, "Error while opening file.\n");
		fprintf(stderr, "Writing to stderr instead.\n");
		fd = stderr;
	}

	fprintf(fd, "data_sent:%d\n", stats.data_sent);
	fprintf(fd, "data_received:%d\n", stats.data_received);
	fprintf(fd, "data_truncated_received:%d\n", stats.data_truncated_received);
	fprintf(fd, "ack_sent:%d\n", stats.ack_sent);
	fprintf(fd, "ack_received:%d\n", stats.ack_received);
	fprintf(fd, "nack_sent:%d\n", stats.nack_sent);
	fprintf(fd, "nack_received:%d\n", stats.nack_received);
	fprintf(fd, "packets_ignored:%d\n", stats.packet_ignored);
	fprintf(fd, "packets_duplicated:%d\n", stats.packet_duplicated);

	if(fd != stderr){
		fclose(fd);
	}
}

/* Handle a packet
 * @return: 0 when all data has been received, 1 when a packet can be send back, 2 when the packet had to be ignored
 */
int handle_packet(char* buffer, int length){
	/* Initialisation of received packet */
	pkt_t* recv_pkt = pkt_new();
	int ret = 1;

	/* If there was any errors during packet decoding, ignore it */
	if(pkt_decode(buffer, length, recv_pkt)){
	  ERROR("Could not decode packet\n");
	  pkt_del(recv_pkt);
  	  return 2;
	}

	DEBUG("recv_pkt->length %d\n", recv_pkt->length);
	
	uint8_t recv_seqnum = pkt_get_seqnum(recv_pkt);
	pkt_last_timestamp = pkt_get_timestamp(recv_pkt);
	
	uint8_t min = next_seqnum;
	uint8_t max = (next_seqnum+WINDOW_MAX_SIZE) % MAX_SEQ_SIZE;
	if(max < min){
		if(recv_seqnum < min && recv_seqnum >= max){
			ERROR("Unexpected seqnum\n");
			stats.packet_ignored += 1;
			pkt_del(recv_pkt);
			return 2;
		}
	}else{
		if(recv_seqnum < min || recv_seqnum >= max){
			ERROR("Unexpected seqnum\n");
			stats.packet_ignored += 1;
			pkt_del(recv_pkt);
			return 2;
		}
	}

	/* End of data transmission */
	if(!pkt_get_length(recv_pkt) && (recv_seqnum == next_seqnum)){
		DEBUG("EOT received\n");
		ret = 0;
	}
	
	/* Response packet to send back */
	pkt_t* resp_pkt = pkt_new();

	/* Send NACK */
	if(pkt_get_tr(recv_pkt)) {
		stats.data_truncated_received += 1;
		stats.nack_sent += 1;

		DEBUG("Starting NACK\n");
		pkt_set_type(resp_pkt, 3);
		pkt_set_seqnum(resp_pkt, recv_seqnum);
	} else {
		stats.data_received += 1;
		stats.ack_sent += 1;

		DEBUG("Starting ACK\n");
		pkt_set_type(resp_pkt, 2);

		/* Add the packet to the buffer */
		if(window[recv_seqnum % N] == NULL){
			window[recv_seqnum % N] = recv_pkt;
			window_size--;
		
			/* Iterate over the buffer until there is no more packets, i.d. next_seqnum hasn't arrived yet */
			uint8_t idx = next_seqnum % N;
			int n_wri;
			while(window[idx] != NULL){
				n_wri = write(1, window[idx]->payload, pkt_get_length(window[idx]));
				if(n_wri == -1) ERROR("Error while writing packet to stdout\n");
				pkt_del(window[idx]);
				window[idx] = NULL;
				window_size++;
				next_seqnum = (next_seqnum + 1) % MAX_SEQ_SIZE;
				idx = (idx + 1) % N;
			}
			pkt_set_seqnum(resp_pkt, next_seqnum);
		}else{
			stats.packet_duplicated += 1;
		}	
	}

	size_t enco_len = RESP_LEN;
	pkt_set_window(resp_pkt, window_size);
	pkt_set_timestamp(resp_pkt, pkt_last_timestamp);
	pkt_encode(resp_pkt, buffer, &enco_len);
	
	pkt_del(resp_pkt);

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
				if(ret!=2){
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

	DEBUG("Sender connected\n");

	/* Data array initialization */
	int i=0;
	for(;i<N;i++){
		window[i] = NULL;
	}

	memset(&stats, 0, sizeof(stat_t));

	receiver_handler(sfd);

	send_statistics(stats_filename);

	close(sfd);

	return EXIT_SUCCESS;
}
