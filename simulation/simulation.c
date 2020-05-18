
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "davis.h"
#include "packet.h"


#define MBPS 131072
#define GBPS 134217728

#define NUM_FLOWS 1
const unsigned long MSS = 512;

const double LOSS_PROB = 0.0;//2.5e-7;
const int LOSS_RAND_CUTOFF = LOSS_PROB*RAND_MAX;

static inline double base_rtt(double t, size_t flow) {
    return 30e-3;//*(1 + flow/(NUM_FLOWS - 1.0));
}

static inline double max_bw(double t) { return 10.0*GBPS; }

static inline double app_rate(double t, size_t flow) {
    return 2*max_bw(t);
}

static inline unsigned long bdp(double t, size_t flow) {
    return max_bw(t)*base_rtt(t, flow)/MSS;
}

unsigned long buf_size(double t) {
    unsigned long max_bdp = 0;

    for (size_t i = 0; i < NUM_FLOWS; i++) {
        if (bdp(t, i) > max_bdp)
            max_bdp = bdp(t, i);
    }

    return max_bdp;
}

const double RUNTIME = 60*10;
static inline double report_interval(double t) { return RUNTIME/10000; }

static inline double flow_start_time(size_t flow) {
    return flow*RUNTIME/(4*(NUM_FLOWS + 1));
}


enum event_type { NONE, SEND, ARRIVAL, DEPARTURE };


int main(int argc, char *argv[])
{
    struct davis d[NUM_FLOWS];

    srand(time(NULL));

    if ((int) (RAND_MAX*LOSS_PROB) == 0 && LOSS_PROB != 0.0)
        fprintf(stderr, "Loss probability defaulting to 0\n");

    printf("flow_id,time,rtt,cwnd,bytes_sent,losses,");
    printf("pacing_rate,min_rtt,bdp,mode\n");

    unsigned int last_perc = 0;
    double last_print_time = 0;
    double time = 0;

    struct packet_buffer network[NUM_FLOWS] = {packet_buffer_empty};
    struct packet_buffer bottleneck = packet_buffer_empty;
    struct packet_buffer lost = packet_buffer_empty;
    double next_bottleneck_time = time;
    double next_send_time[NUM_FLOWS] = {time};

    unsigned long inflight[NUM_FLOWS] = {0};
    unsigned long bytes_sent[NUM_FLOWS] = {0};
    unsigned long pkts_delivered[NUM_FLOWS] = {0};
    unsigned long losses[NUM_FLOWS] = {0};
    double last_loss_time[NUM_FLOWS] = {0};
    double rtt[NUM_FLOWS] = {0};

    for (size_t i = 0; i < NUM_FLOWS; i++)
        davis_init(&d[i], time, MSS);

    while (time < RUNTIME) {
        enum event_type event = NONE;
        size_t flow = 0;
        struct packet *net_packet;
        struct packet *bn_packet = packet_buffer_peek(&bottleneck);

        /*** Caculate next event ***/
        time = 2*RUNTIME;

        for (size_t i = 0; i < NUM_FLOWS; i++) {
            net_packet = packet_buffer_peek(&network[i]);

            if (net_packet != NULL) {
                double arrival_time = net_packet->send_time + base_rtt(net_packet->send_time, net_packet->flow_id);

                if (arrival_time < time) {
                    event = ARRIVAL;
                    flow = net_packet->flow_id;
                    time = arrival_time;
                }
            }
        }

        if (bn_packet != NULL && next_bottleneck_time < time) {
            event = DEPARTURE;
            flow = bn_packet->flow_id;
            time = next_bottleneck_time;
        }

        for (size_t i = 0; i < NUM_FLOWS; i++) {
            bool cond = flow_start_time(i) < time;
            cond = cond && inflight[i] < d[i].cwnd;
            cond = cond && next_send_time[i] < time;

            if (cond) {
                event = SEND;
                flow = i;
                time = next_send_time[i];

                if (time < flow_start_time(i))
                    time = flow_start_time(i);
            }
        }

        /*** Progress update ***/
        unsigned int perc = 100*time/RUNTIME;
        if (perc > last_perc) {
            fprintf(stderr, "%u%%    \r", perc);
            last_perc = perc;
        }


        double send_rate[NUM_FLOWS];

        for (size_t i = 0; i < NUM_FLOWS; i++) {
            send_rate[i] = app_rate(time, i);

            if (d[i].pacing_rate > 0 && d[i].pacing_rate < send_rate[i])
                send_rate[i] = d[i].pacing_rate;
        }


        if (event == ARRIVAL) {
            if (bn_packet == NULL)
                next_bottleneck_time = time + MSS/max_bw(time);

            net_packet = packet_buffer_dequeue(&network[flow]);

            if (bottleneck.length >= buf_size(time) || rand() < LOSS_RAND_CUTOFF)
                packet_buffer_enqueue(&lost, net_packet);
            else
                packet_buffer_enqueue(&bottleneck, net_packet);
        } else if (event == DEPARTURE) {
            if (inflight[flow] >= d[flow].cwnd)
                next_send_time[flow] = time + MSS/send_rate[flow];

            bn_packet = packet_buffer_dequeue(&bottleneck);
            inflight[flow]--;
            pkts_delivered[flow]++;

            rtt[flow] = time - bn_packet->send_time;
            davis_on_ack(&d[flow], time, rtt[flow], pkts_delivered[flow]);

            next_bottleneck_time = time + MSS/max_bw(time);
            free(bn_packet);
        } else if (event == SEND) {
            struct packet *p = malloc(sizeof(struct packet));
            p->flow_id = flow;
            p->send_time = time;
            p->next = NULL;

            packet_buffer_enqueue(&network[flow], p);

            bytes_sent[flow] += MSS;
            inflight[flow]++;

            next_send_time[flow] = time + MSS/send_rate[flow];
        }


        struct packet *lost_packet = packet_buffer_dequeue(&lost);
        while (lost_packet != NULL) {
            size_t flow = lost_packet->flow_id;
            inflight[flow]--;
            losses[flow]++;
            davis_on_loss(&d[flow], time);

            free(lost_packet);
            lost_packet = packet_buffer_dequeue(&lost);
        }


        /*** Log data ***/
        if (time > last_print_time + report_interval(time)) {
            for (size_t i = 0; i < NUM_FLOWS; i++) {
                printf("%ld,%f,%f,%lu,%lu,%lu,", i, time, rtt[i],
                       d[i].cwnd, bytes_sent[i], losses[i]);
                printf("%f,%f,%lu,%u\n", d->pacing_rate, d[i].min_rtt,
                       d[i].bdp, d[i].mode);

                bytes_sent[i] = 0;
            }

            last_print_time = time;
        }
    }

    return 0;
}
