
#include "packet.h"



void packet_buffer_enqueue(struct packet_buffer *buf,
                           struct packet *packet)
{
    if (buf->head == NULL) {
        buf->head = packet;
        buf->tail = packet;
    } else {
        buf->tail->next = packet;
        buf->tail = packet;
    }
}


struct packet* packet_buffer_dequeue(struct packet_buffer *buf)
{
    struct packet *packet = buf->head;

    if (packet != NULL) {
        buf->head = packet->next;
        packet->next = NULL;
    }

    if (buf->head == NULL)
        buf->tail = NULL;

    return packet;
}


struct packet* packet_buffer_peek(struct packet_buffer *buf)
{
    return buf->head;
}
