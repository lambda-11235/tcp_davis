
#include <stdbool.h>
#include <stdlib.h>

#ifndef _PACKET_H_
#define _PACKET_H_


struct packet {
    size_t flow_id;
    double send_time;
    struct packet *next;
};

struct packet_buffer {
    size_t length;
    struct packet *head;
    struct packet *tail;
};


#define packet_buffer_empty {0, NULL, NULL}


void packet_buffer_enqueue(struct packet_buffer *buf,
                           struct packet *packet);

struct packet* packet_buffer_dequeue(struct packet_buffer *buf);

struct packet* packet_buffer_peek(struct packet_buffer *buf);



#endif /* _PACKET_H_ */
