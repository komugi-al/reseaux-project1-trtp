#include "packet_interface.h"

/* Extra #includes */
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <zlib.h>

struct __attribute__((__packed__)) pkt {
	uint8_t window:5;
	uint8_t tr:1;
	uint8_t type:2;
	uint16_t length;
	uint8_t seqnum;
	unsigned int timestamp;
	unsigned int crc1;
	char* payload;
	unsigned int crc2;
};

/* Extra code */
/* Your code will be inserted here */

pkt_t* pkt_new()
{
	pkt_t* pkt = (pkt_t*) malloc(sizeof(pkt_t));
	if(pkt==NULL){
		printf("Error with malloc()\n");
		return NULL;
	}
	return memset(pkt, 0, sizeof(struct pkt));
}

void pkt_del(pkt_t *pkt)
{
	free(pkt->payload);
	free(pkt);
}

/* 
 * Checks for errors, if any, places the first one encoutered at position 0 in the errs array
 */
void check_errors(uint8_t* errs, uint8_t len){
	int i=0;
	for(; i<len; i++){
		if(errs[i]) {
			errs[0] = errs[i];
			return;
		}
	}
}

uLong compute_crc(Bytef* buf, size_t sz){
	uLong crc = crc32(0L, Z_NULL, 0);
	crc = crc32(crc, buf, sz);
	return crc;
}

pkt_status_code pkt_decode(const char *data, const size_t len, pkt_t *pkt)
{
	uint8_t* ret = (uint8_t*) malloc(10*sizeof(uint8_t));
	if(ret==NULL) return E_NOMEM;

	pkt_t* pkt_tmp = pkt_new();
	memcpy(pkt_tmp, (pkt_t*) data, sizeof(pkt_t));

	if((size_t)predict_header_length(pkt_tmp) > len) return E_NOHEADER; 

	// Set common struct fields to check for errors in the header
	ret[0] = pkt_set_type(pkt_tmp, pkt_tmp->type);
	ret[1] = pkt_set_tr(pkt_tmp, pkt_tmp->tr);
	ret[2] = pkt_set_window(pkt_tmp, pkt_tmp->window);
	if(pkt_tmp->type == 1) {
		ret[3] = pkt_set_seqnum(pkt_tmp, *(data+3));
		ret[4] = pkt_set_timestamp(pkt_tmp,*(uint32_t*)(data+4));
		ret[5] = pkt_set_crc1(pkt_tmp, ntohl(*(uint32_t*)(data+8)));
	}else{
		ret[3] = pkt_set_seqnum(pkt_tmp, *(data+1));
		ret[4] = pkt_set_timestamp(pkt_tmp,*(uint32_t*)(data+2));
		ret[5] = pkt_set_crc1(pkt_tmp, ntohl(*(uint32_t*)(data+6)));
	}
	
	// If there are errors, return the first one encountered
	check_errors(ret, 6);
	if(ret[0]) return ret[0];	

	// Prepare to check CRC1 as well as the size of the data received
	uLong original_crc = (uLong) pkt_get_crc1(pkt_tmp); 
	uLong crc;
	uint8_t old_tr = pkt_tmp->tr;
	pkt_tmp->tr = 0;
	uint16_t total;
	if(pkt_tmp->type == 1) {
		// PTYPE est égal à PTYPE_DATA
		crc = compute_crc((Bytef*)pkt_tmp, 8);
		// Set length to the right endianness
		ret[0] = pkt_set_length(pkt_tmp, ntohs(pkt_tmp->length));
		if(ret[0]) return ret[0];
		total = !old_tr ? 12 + pkt_tmp->length + 4 : 12;
	}else{
		// PTYPE est égal à PTYPE_ACK ou PTYPE_NACK
		total = 10;
		Bytef header[6];
		memcpy(header, pkt_tmp, 1);
		memcpy(header+1, &pkt_tmp->seqnum, 5);
		crc = compute_crc(header, 6);
	}

	// Check if the given length is consistent
	if(total != len) return E_UNCONSISTENT;
	// Checks if packet type is PTYPE_DATA then verify consistency
	if(pkt_tmp->type == 1 && pkt_tmp->tr && pkt_tmp->length != 0) return E_UNCONSISTENT;
	// Checks if the crc is correct
	if(crc != original_crc) return E_CRC;

	// Initializing common fields
	pkt->window = pkt_tmp->window;
	pkt->tr = old_tr;
	pkt->type = pkt_tmp->type;
	pkt->seqnum = pkt_tmp->seqnum;
	pkt->timestamp = pkt_tmp->timestamp;
	pkt->crc1 = pkt_tmp->crc1;
	
	if(pkt_tmp->type==1 && !old_tr){
		// Setting the payload and the crc, adding the corresponding offset based on the data pointer
		uint16_t offset = 12;
		ret[0] = pkt_set_payload(pkt_tmp, data+offset, pkt_tmp->length);
		offset += pkt_tmp->length;
		ret[1] = pkt_set_crc2(pkt_tmp, ntohl(*(uint32_t*)(data+offset)));

		check_errors(ret, 2);
		if(ret[0]) return ret[0];	

		// Checks for the CRC2
		original_crc = (uLong) pkt_get_crc2(pkt_tmp); 
		crc = compute_crc((Bytef*)pkt_tmp->payload, pkt_tmp->length);

		if(crc != original_crc) return E_CRC;

		pkt->length = pkt_tmp->length;
		pkt->payload = pkt_tmp->payload;
		pkt->crc2 = pkt_tmp->crc2;
	}

	return PKT_OK;
}


pkt_status_code pkt_encode(const pkt_t* pkt, char *buf, size_t *len)
{
	size_t total = predict_header_length(pkt); 
	// Potentiellement incorrect
	total += pkt->type == 1 && !pkt->tr ? 4 + pkt->length + 4 : 4; 
	if(total > *len) return E_NOMEM;
	
	memcpy(buf, pkt, 1);
	size_t offset = 1;

	if(pkt->type==1) {
		uint16_t length = htons(pkt->length);
		memcpy(buf+offset, &length, 2);
		offset+=2;
	}

	memcpy(buf+offset, &pkt->seqnum, 5);
	offset+=5;

	uint32_t crc = htonl(compute_crc((Bytef*)buf, offset));
	memcpy(buf+offset, &crc, 4);
	offset+=4;

	if(pkt->type==1 && !pkt->tr){
		memcpy(buf+offset, pkt->payload, pkt->length);
		offset+=pkt->length;
		crc = htonl(compute_crc((Bytef*)pkt->payload, pkt->length));
		memcpy(buf+offset, &crc, 4);
		offset+=4;
	}

	*len=offset;

	return PKT_OK;
}

ptypes_t pkt_get_type(const pkt_t* pkt)
{
	return pkt->type;
}

uint8_t  pkt_get_tr(const pkt_t* pkt)
{
	return pkt->tr;
}

uint8_t  pkt_get_window(const pkt_t* pkt)
{
	return pkt->window;
}

uint8_t  pkt_get_seqnum(const pkt_t* pkt)
{
	return pkt->seqnum;
}

uint16_t pkt_get_length(const pkt_t* pkt)
{
	return pkt->length;
}

uint32_t pkt_get_timestamp   (const pkt_t* pkt)
{
	return pkt->timestamp;
}

uint32_t pkt_get_crc1   (const pkt_t* pkt)
{
	return pkt->crc1;
}

uint32_t pkt_get_crc2   (const pkt_t* pkt)
{
	return pkt->crc2;
}

const char* pkt_get_payload(const pkt_t* pkt)
{
	return pkt->payload;
}


pkt_status_code pkt_set_type(pkt_t *pkt, const ptypes_t type)
{
	if(!type) return E_TYPE;
	pkt->type = type;
	return PKT_OK;
}

pkt_status_code pkt_set_tr(pkt_t *pkt, const uint8_t tr)
{
	pkt->tr = tr;
	return PKT_OK;
}

pkt_status_code pkt_set_window(pkt_t *pkt, const uint8_t window)
{
	if(window > MAX_WINDOW_SIZE) return E_WINDOW;
	pkt->window = window;
	return PKT_OK;
}

pkt_status_code pkt_set_seqnum(pkt_t *pkt, const uint8_t seqnum)
{
	pkt->seqnum = seqnum;
	return PKT_OK;
}

pkt_status_code pkt_set_length(pkt_t *pkt, const uint16_t length)
{
	if(length > MAX_PAYLOAD_SIZE) return E_LENGTH;
	pkt->length = length;
	return PKT_OK;
}

pkt_status_code pkt_set_timestamp(pkt_t *pkt, const uint32_t timestamp)
{
	pkt->timestamp = timestamp;
	return PKT_OK;
}

pkt_status_code pkt_set_crc1(pkt_t *pkt, const uint32_t crc1)
{
	pkt->crc1 = crc1;
	return PKT_OK;
}

pkt_status_code pkt_set_crc2(pkt_t *pkt, const uint32_t crc2)
{
	pkt->crc2 = crc2;
	return PKT_OK;
}

pkt_status_code pkt_set_payload(pkt_t *pkt,
                                const char *data,
                                const uint16_t length)
{
	uint16_t ret = pkt_set_length(pkt, length);
	if(ret) return ret;
	pkt->payload = (char*) malloc(length*sizeof(char));
	if(pkt->payload==NULL) return E_NOMEM;
	memcpy(pkt->payload, data, length);
	fprintf(stderr, "set_payload length: %d - payload: %c\n", length, *pkt->payload);
	return PKT_OK;
}

ssize_t predict_header_length(const pkt_t *pkt)
{
	if(pkt_get_type(pkt)==0) return E_TYPE;
	if(pkt_get_type(pkt)==1) return 8;
	else{
		return 6;
	}
}
	
/**int main(int argc, char* argv[]){

	printf("%d - %s\n", argc, argv[1]);
	pkt_t* pkt = pkt_new();
	pkt->window = 28;
	pkt->type = 1;
	pkt->tr = 0;
	pkt->seqnum = 123;
	pkt->timestamp = 17;
	char header[] = "Hello world";
	pkt_set_payload(pkt, header, 11);
	char* buffer = (char*) malloc(27*sizeof(char));
	size_t len = 27;
	pkt_status_code p = pkt_encode(pkt, buffer, &len);
	printf("Encode %d\n", p);
	printf("\n");
	pkt_t* pkt_d = pkt_new();
	p = pkt_decode(buffer, len, pkt_d);
	printf("Decode %d\n", p);
	char data[1];
	data[0] = 0b00011101;
	printf("data[0]=%d\n", data[0]);
	data[0] = *(uint8_t*)data & (uint8_t)0xFB;
	printf("data[0]=%d\n", data[0]);
	return EXIT_SUCCESS;
}*/
