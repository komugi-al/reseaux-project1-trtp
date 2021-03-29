#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <time.h>
#include <errno.h>

#include "log.h"
#include "socket_helpers.h"
#include "packet.h"
#include "config.h"

pkt_t* windows[N];
uint8_t start_window = 0;
uint8_t size_window = 0;
uint8_t next_seqnum = 0;
time_t timeout_counter = 0;
stat_t stats;

int print_usage(char *prog_name) {
    ERROR("Usage:\n\t%s [-f filename] [-s stats_filename] receiver_ip receiver_port", prog_name);
    return EXIT_FAILURE;
}

void send_statistics(const char* filename){
	FILE *fd = filename == NULL ? stderr : fopen(filename, "w");
	if(fd == NULL) {
		fprintf(stderr, "Error while opening file.\n");
		fprintf(stderr, "Writing to stderr instead.\n");
		fd = stderr;
	}

	fprintf(fd, "data_sent,%d\n", stats.data_sent);
	fprintf(fd, "data_received,%d\n", stats.data_received);
	fprintf(fd, "data_truncated_received,%d\n", stats.data_truncated_received);
	fprintf(fd, "ack_sent,%d\n", stats.ack_sent);
	fprintf(fd, "ack_received,%d\n", stats.ack_received);
	fprintf(fd, "nack_sent,%d\n", stats.nack_sent);
	fprintf(fd, "nack_received,%d\n", stats.nack_received);
	fprintf(fd, "packets_ignored,%d\n", stats.packet_ignored);
	fprintf(fd, "min_rtt,%d\n", stats.min_rtt*1000);
	fprintf(fd, "max_rtt,%d\n", stats.max_rtt*1000);
	fprintf(fd, "packets_retransmitted,%d\n", stats.packet_retransmitted);

	if(fd != stderr){
		fclose(fd);
	}
}

/*
 *	Clear all packets received to a valid seqnum and update de start_window value to the next not yet received packet
 */
void clear_received_packets(int recv_seqnum){
	int idx = recv_seqnum % N;
	while(start_window != idx){
		pkt_del(windows[start_window]);
		windows[start_window] = NULL;
		size_window++;
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
	pkt_set_type(new_pkt, 1);
	pkt_set_seqnum(new_pkt, next_seqnum);
	time_t second = 0;
	time(&second);
	pkt_set_timestamp(new_pkt, second);
	pkt_set_payload(new_pkt, buffer, len);

	windows[next_seqnum % N] = new_pkt;

	next_seqnum = (next_seqnum + 1) % MAX_SEQ_SIZE;

	return new_pkt;
}

void encode_and_send_packet_data(pkt_t* pkt, int fd){
	DEBUG("Sending packet, seqnum %d\n", pkt->seqnum);
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

	free(new_buffer);
}

void resend_timedout_packet(int sfd, int timeout){
	uint8_t idx = start_window;
	time_t second = 0;
	time(&second);
	while(windows[idx] != NULL && (difftime(windows[idx]->timestamp, second-(timeout/1000)) <= 0)){
		DEBUG("Retransmitting\n");
		stats.packet_retransmitted += 1;
		encode_and_send_packet_data(windows[idx], sfd);
		idx = (idx + 1) % N;
	}
}

void compute_rtt(int timestamp){
	time_t now = 0;
	time(&now);
	int time = now - timestamp;
	if(time < stats.min_rtt){
		stats.min_rtt = time*1000;
	}
	if(time > stats.max_rtt){
		stats.max_rtt = time*1000;
	}
}

void sender_handler(const int sfd, int fdin){
	struct pollfd fds[] = {{.fd=sfd, .events=POLLIN},{.fd=fdin, .events=POLLIN}};
	int n_fds = 2;
	int timeout = 5000;
	bool eot = false;
	bool end = false;
	uint8_t receiver_window = 1;
	int n_read = 0;
	while(!end && n_read != -1){

		if(poll(fds, n_fds, timeout) == -1){
			ERROR("Error with poll()\n");
		} else {
			char buffer[MAX_PAYLOAD_SIZE];
			pkt_t* pkt = NULL;

			for(int i=0; i<n_fds; i++){

				if(!fds[i].revents) continue;

				if(fds[i].fd==fdin && receiver_window && !eot){
					DEBUG("Reading from stdin\n");
					stats.data_sent += 1;
					receiver_window--;
					size_window--;
					n_read = read(fds[i].fd, buffer, MAX_PAYLOAD_SIZE);
					pkt = create_and_save_packet_data(buffer, n_read);
					encode_and_send_packet_data(pkt, sfd);

					if(n_read==0){
						DEBUG("EOT received\n");
						eot=true;
						time(&timeout_counter);
						fds[1].fd = -1;
						n_fds = 1;
					}
				} else if (fds[i].fd==sfd) {
					DEBUG("Reading from socket\n");
					n_read = read(fds[i].fd, buffer, MAX_PAYLOAD_SIZE);
					if(n_read == -1){
						perror("Couldn't read socket\n");
					}else{
						pkt = pkt_new();
						int ret = pkt_decode(buffer, n_read, pkt);
						if(ret) {
							ERROR("Error with pkt_decode() %d\n", ret);
						} else {
							if(!timeout_counter){
								time(&timeout_counter);
							}
							if(pkt->type == PTYPE_ACK){
								DEBUG("pkt->type is PTYPE_ACK\n");
								stats.ack_received += 1;
								
								compute_rtt(pkt->timestamp);

								if(stats.max_rtt > timeout){
									timeout = stats.max_rtt;
								}

								if(!timeout_counter){
									time(&timeout_counter);
								}
								
								if(eot && pkt->seqnum == next_seqnum) end = true;

								DEBUG("pkt->seqnum %d, next_seqnum %d\n", pkt->seqnum, next_seqnum);
								
								clear_received_packets(pkt->seqnum);

								if(pkt->window > size_window){
									receiver_window = pkt->window;
								}
							} else if(pkt->type == PTYPE_NACK){
								DEBUG("pkt->type is PTYPE_NACK\n");
								stats.nack_received += 1;
								encode_and_send_packet_data(windows[pkt->seqnum%N], sfd);
							}
						}
						pkt_del(pkt);
					}
				}
			}
			fflush(NULL);
		}
		time_t now = 0;
		time(&now);
		if(timeout_counter && (now-timeout_counter >= ((timeout*4)/1000))){
		  	end = true;
		}else{	  
			resend_timedout_packet(sfd, timeout);
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

	ERROR("Sender has following arguments: filename is %s, stats_filename is %s, receiver_ip is %s, receiver_port is %u", filename, stats_filename, receiver_ip, receiver_port);

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

	if(sfd == -1) {
		ERROR("Couldn't create socket\n");
		return EXIT_FAILURE;
	}

	memset(windows, 0, sizeof(windows));

	memset(&stats, 0, sizeof(stat_t));
	stats.min_rtt = INT_MAX;
	stats.max_rtt= INT_MIN;

	/* Process I/O */
	sender_handler(sfd, fd);

	send_statistics(stats_filename);

	close(fd);
	close(sfd);

	return EXIT_SUCCESS;
}


