#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>

#include "log.h"
#include "socket_helpers.h"
#include "packet_interface.h"

int print_usage(char *prog_name) {
    ERROR("Usage:\n\t%s [-f filename] [-s stats_filename] receiver_ip receiver_port", prog_name);
    return EXIT_FAILURE;
}

pkt_t* packets[256];
uint8_t startBuf = 0, endBuf = 0, freeBuf = 255;

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
    ERROR("Sender has following arguments: filename is %s, stats_filename is %s, receiver_ip is %s, receiver_port is %u",
        filename, stats_filename, receiver_ip, receiver_port);

    DEBUG("You can only see me if %s", "you built me using `make debug`");
    ERROR("This is not an error, %s", "now let's code!");

    // Now let's code!
    
    //Create the FD who would need to get the data
    int fd;
    if(filename == NULL){
        fd = fcntl(STDIN_FILENO,  F_DUPFD, 0);
    }
    else{
        fd = fopen(filename, O_RDONLY);
    }

    // Create the socket to connect with receiver
    struct sockaddr_in6 addr;
	const char *err = real_address(receiver_ip, &addr);
    int sock = create_socket(NULL, -1, &addr, port);

    /* Process I/O */
	read_write_loop(sfd, fd);

    return EXIT_SUCCESS;
}

void read_write_loop(const int sfd, int fd){
	struct pollfd fds[] = {{.fd=sfd, .events=POLLIN},{.fd=fd, .events=POLLIN}};
	int n_fds = 2;
	int ret;
	int n_ret;
	while(1){
		ret = poll(fds, n_fds, -1);
		if(ret == -1) fprintf(stderr, "error with poll()");
		else {
			char buffer[513];
			for(int i=0; i<n_fds; i++){
				if(!fds[i].revents) continue;
				if(fds[i].revents){
					if(fds[i].fd==fd && freeBuf != 0){
                        char packetToSend[517];
                        freeBuf--;
                        n_ret = read(fds[i].fd, buffer, 517);
                        fprintf(stderr,"Data read=%d, fd=%d\n", n_ret, fds[i].fd);
                        if(n_ret==0) break;
                        // create the packet and initialize its values
                        pkt* pkt = create_packet(buffer, n_ret);
                        pkt_set_type(pkt, PTYPE_DATA);
                        pkt_set_tr(pkt, 0);
                        pkt_set_window
                        pkt_set_seqnum(pkt, startBuf);
                        pkt_set_length
                        pkt_set_timestamp
                        pkt_set_crc1
                        // encode the packet before sending it
                        n_ret = pkt_encode(pkt,packetToSend,576);
						n_ret = write(sfd, packetToSend, n_ret);

					} else if(fds[i].fd==sfd){
						n_ret = write(1, buffer, n_ret);
					}
					fflush(NULL);
				} 
			}
		}
	}
}

int inc_buf(int index){
    return (index+1) % 256;
}

pkt* create_packet_data(char* buffer, int len){
    return NULL;
}