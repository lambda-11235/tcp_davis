
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "dumb.h"
#include "packet.h"


#define MBPS 131072
#define GBPS 134217728

const bool rtt_limited = false;
const bool bdp_limited = false;

const unsigned long MSS = 512;
const double MIN_RTT = 30e-3;
const double MAX_BW = 10.0*GBPS;

const double APP_RATE = 100.0*GBPS;

const double LOSS_PROB = 0.0;//2.5e-7;
const int LOSS_RAND_CUTOFF = LOSS_PROB*RAND_MAX;

const unsigned long BDP = MAX_BW*MIN_RTT/MSS;
const unsigned long BUF_SIZE = (bdp_limited ? 1 : 2)*BDP;

const double RUNTIME = 60;
const double REPORT_INTERVAL = RUNTIME/100000;


enum event_type { NONE, ARRIVAL, DEPARTURE };


int main(int argc, char *argv[])
{
    struct dumb d;

    srand(time(NULL));

    if ((int) (RAND_MAX*LOSS_PROB) == 0 && LOSS_PROB != 0.0)
        fprintf(stderr, "Loss probability defaulting to 0\n");

    printf("time,rtt,cwnd,rate,losses,");
    printf("max_rate,min_rtt,bdp\n");

    unsigned int last_perc = 0;
    double last_print_time = 0;
    double time = 0;

    struct packet_buffer network = packet_buffer_empty;
    struct packet_buffer bottleneck = packet_buffer_empty;
    double next_bottleneck_time = time;
    unsigned long inflight = 0;
    unsigned long sent = 0;
    bool is_cwnd_limited = true;

    double rate_sent = 0;
    unsigned long losses = 0;
    double last_loss_time = 0;
    double rtt = 0;

    dumb_init(&d, time);

    while (time < RUNTIME) {
        enum event_type event = NONE;
        struct packet *net_packet = packet_buffer_peek(&network);
        struct packet *bn_packet = packet_buffer_peek(&bottleneck);

        unsigned long app_accrued_packets = APP_RATE*time/MSS;

        /*** Caculate next event ***/
        time *= 2;

        if (net_packet != NULL) {
            event = ARRIVAL;

            time = net_packet->send_time + MIN_RTT;
        }

        if (bn_packet != NULL && next_bottleneck_time < time) {
            event = DEPARTURE;
            time = next_bottleneck_time;
        }

        /*** Progress update ***/
        unsigned int perc = 100*time/RUNTIME;
        if (perc > last_perc) {
            fprintf(stderr, "%u%%    \r", perc);
            last_perc = perc;
        }


        if (event == ARRIVAL) {
            if (bn_packet == NULL)
                next_bottleneck_time = time + MSS/MAX_BW;

            net_packet = packet_buffer_dequeue(&network);
            packet_buffer_enqueue(&bottleneck, net_packet);
        } else if (event == DEPARTURE) {
            struct packet *bn_packet = packet_buffer_dequeue(&bottleneck);
            inflight--;

            if (bn_packet->lost) {
                losses++;
                dumb_on_loss(&d, time);
            } else {
                rtt = time - bn_packet->send_time;
                dumb_on_ack(&d, time, rtt, inflight);
            }

            next_bottleneck_time = time + MSS/MAX_BW;
            free(bn_packet);
        }


        while (inflight < d.cwnd && sent <= app_accrued_packets) {
            struct packet *p = malloc(sizeof(struct packet));
            p->send_time = time;
            p->lost = inflight > BDP + BUF_SIZE - 1 || rand() < LOSS_RAND_CUTOFF;
            p->next = NULL;

            packet_buffer_enqueue(&network, p);

            rate_sent += MSS;

            inflight++;
            sent++;
        }

        is_cwnd_limited = inflight < d.cwnd;


        /*** Log data ***/
        if (time > last_print_time + REPORT_INTERVAL) {
            double rate = rate_sent/(time - last_print_time);
            rate_sent = 0;
            last_print_time = time;

            printf("%f,%f,%lu,%f,%lu,", time, rtt, d.cwnd, rate,
                   losses);
            printf("%f,%f,%lu\n", d.max_rate*MSS, d.min_rtt, d.bdp);
        }
    }

    return 0;
}
