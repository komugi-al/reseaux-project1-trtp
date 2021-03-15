#include "socket_helpers.h"

const char * real_address(const char *address, struct sockaddr_in6 *rval){
	struct addrinfo *result;
	struct addrinfo hints;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family=AF_INET6;
	hints.ai_socktype = SOCK_DGRAM; /* Datagram socket */
	hints.ai_flags = 0;    
	hints.ai_protocol = 0;          /* Any protocol */
	
	int ret = getaddrinfo(address, NULL, &hints, &result);
	if(ret){
		return gai_strerror(ret);
	}
	memcpy(rval, (struct sockaddr_in6 *)result->ai_addr, sizeof(struct sockaddr_in6));
	return NULL;
}

int create_socket(struct sockaddr_in6 *source_addr,
                 int src_port,
                 struct sockaddr_in6 *dest_addr,
                 int dst_port){
	int sock = socket(AF_INET6, SOCK_DGRAM, 17); // UDP = 17
	if(sock == -1){
		fprintf(stderr, "could not create the IPv6 SOCK_DGRAM socket");
		return -1;
	}

	int err;
	if(source_addr != NULL){
		if(src_port > 0) source_addr->sin6_port = htons(src_port);
		err = bind(sock, (struct sockaddr *)source_addr, sizeof(*source_addr));
		if(err == -1){
			fprintf(stderr, "could not bind to the socket: %d %s\n", errno, strerror(errno));
			return -1;
		}
	}

	if(dest_addr != NULL){
		if(dst_port > 0) dest_addr->sin6_port = htons(dst_port);
		err = connect(sock, (struct sockaddr *)dest_addr, sizeof(*dest_addr));
		if(err == -1){
			fprintf(stderr, "could not connect the socket\n");
			return -1;
		}
	}

	return sock;
}

int wait_for_client(int sfd){
	struct sockaddr_storage src_addr;
	socklen_t src_addr_len = sizeof(struct sockaddr_storage);

	int ret = recvfrom(sfd, NULL, 0, MSG_PEEK, (struct sockaddr *)&src_addr, &src_addr_len); 
	if(ret == -1){
		fprintf(stderr,"could not receive message\n");
		return -1;
	}

	ret = connect(sfd, (struct sockaddr *)&src_addr, src_addr_len);
	if(ret == -1){
		printf("could not connect the socket\n");
		return -1;
	}
	printf("Connected\n");
	return 0;
}

