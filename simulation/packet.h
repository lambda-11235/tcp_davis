
#include <stdbool.h>
#include <stdlib.h>

#ifndef _PACKET_H_
#define _PACKET_H_


struct packet {
    double send_time;
    bool lost;
    struct packet *next;
};

struct packet_buffer {
    struct packet *head;
    struct packet *tail;
};


#define packet_buffer_empty {NULL, NULL}


void packet_buffer_enqueue(struct packet_buffer *buf,
                           struct packet *packet);

struct packet* packet_buffer_dequeue(struct packet_buffer *buf);

struct packet* packet_buffer_peek(struct packet_buffer *buf);



#endif /* _PACKET_H_ */
