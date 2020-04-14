
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "dumb.h"
#include "packet.h"


#define MBPS 131072
#define GBPS 134217728

const unsigned long MSS = 512;

const double LOSS_PROB = 0.0;//2.5e-7;
const int LOSS_RAND_CUTOFF = LOSS_PROB*RAND_MAX;

inline double min_rtt(double t) { return 30e-3; }
inline double max_bw(double t) { return 10.0*GBPS; }

inline double app_rate(double t) { return 2*max_bw(t); }

inline unsigned long bdp(double t) { return max_bw(t)*min_rtt(t)/MSS; }
inline unsigned long buf_size(double t) { return bdp(t); }

const double RUNTIME = 60;
inline double report_interval(double t) { return min_rtt(t); }


enum event_type { NONE, SEND, ARRIVAL, DEPARTURE };


int main(int argc, char *argv[])
{
    struct dumb d;

    srand(time(NULL));

    if ((int) (RAND_MAX*LOSS_PROB) == 0 && LOSS_PROB != 0.0)
        fprintf(stderr, "Loss probability defaulting to 0\n");

    printf("time,rtt,cwnd,rate,losses,");
    printf("max_rate,min_rtt,bdp,mode\n");

    unsigned int last_perc = 0;
    double last_print_time = 0;
    double time = 0;

    struct packet_buffer network = packet_buffer_empty;
    struct packet_buffer bottleneck = packet_buffer_empty;
    struct packet_buffer lost = packet_buffer_empty;
    double next_bottleneck_time = time;
    double next_send_time = time;

    unsigned long inflight = 0;
    bool is_cwnd_limited = true;

    double rate_sent = 0;
    unsigned long losses = 0;
    double last_loss_time = 0;
    double rtt = 0;

    dumb_init(&d, time, MSS);

    while (time < RUNTIME) {
        enum event_type event = NONE;
        struct packet *net_packet = packet_buffer_peek(&network);
        struct packet *bn_packet = packet_buffer_peek(&bottleneck);

        /*** Caculate next event ***/
        time = 2*RUNTIME;

        if (net_packet != NULL) {
            event = ARRIVAL;

            time = net_packet->send_time + min_rtt(net_packet->send_time);
        }

        if (bn_packet != NULL && next_bottleneck_time < time) {
            event = DEPARTURE;
            time = next_bottleneck_time;
        }

        if (inflight < d.cwnd && next_send_time < time) {
            event = SEND;
            time = next_send_time;
        }

        /*** Progress update ***/
        unsigned int perc = 100*time/RUNTIME;
        if (perc > last_perc) {
            fprintf(stderr, "%u%%    \r", perc);
            last_perc = perc;
        }


        double send_rate = app_rate(time);

        if (d.pacing_rate > 0 && d.pacing_rate < send_rate)
            send_rate = d.pacing_rate;


        if (event == ARRIVAL) {
            if (bn_packet == NULL)
                next_bottleneck_time = time + MSS/max_bw(time);

            net_packet = packet_buffer_dequeue(&network);

            if (bottleneck.length >= buf_size(time) || rand() < LOSS_RAND_CUTOFF)
                packet_buffer_enqueue(&lost, net_packet);
            else
                packet_buffer_enqueue(&bottleneck, net_packet);
        } else if (event == DEPARTURE) {
            if (inflight >= d.cwnd)
                next_send_time = time + MSS/send_rate;

            bn_packet = packet_buffer_dequeue(&bottleneck);
            inflight--;

            rtt = time - bn_packet->send_time;
            dumb_on_ack(&d, time, rtt, inflight);

            next_bottleneck_time = time + MSS/max_bw(time);
            free(bn_packet);
        } else if (event == SEND) {
            struct packet *p = malloc(sizeof(struct packet));
            p->send_time = time;
            p->next = NULL;

            packet_buffer_enqueue(&network, p);

            rate_sent += MSS;
            inflight++;

            next_send_time = time + MSS/send_rate;
        }


        struct packet *lost_packet = packet_buffer_dequeue(&lost);
        while (lost_packet != NULL) {
            inflight--;
            losses++;
            dumb_on_loss(&d, time);

            free(lost_packet);
            lost_packet = packet_buffer_dequeue(&lost);
        }


        /*** Log data ***/
        if (time > last_print_time + report_interval(time)) {
            double rate = rate_sent/(time - last_print_time);
            rate_sent = 0;
            last_print_time = time;

            printf("%f,%f,%lu,%f,%lu,", time, rtt, d.cwnd, rate,
                   losses);
            printf("%f,%f,%lu,%u\n", d.max_rate, d.min_rtt, d.bdp, d.mode);
        }
    }

    return 0;
}
