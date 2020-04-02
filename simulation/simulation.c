
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "dumb.h"

#define MBPS 131072
#define GBPS 134217728

const bool rtt_limited = false;
const bool bdp_limited = false;

const unsigned long MSS = 512;
const double MIN_RTT = 30e-3;
const double MAX_RTT = (rtt_limited ? 1 : 2)*MIN_RTT + 5e-3;
const double MAX_BW = 10*GBPS;
const double LOSS_PROB = 0;//2.5e-6;

const unsigned long BDP = MAX_BW*MIN_RTT/MSS;
const unsigned long BUF_SIZE = (bdp_limited ? 1 : 2)*BDP;

const double RUNTIME = 60;
const double REPORT_INTERVAL = RUNTIME/1000;


int main(int argc, char *argv[])
{
    struct dumb d;

    srand(time(NULL));

    if ((int) (RAND_MAX*LOSS_PROB) == 0 && LOSS_PROB != 0.0)
        fprintf(stderr, "Loss probability defaulting to 0\n");

    printf("time,rtt,cwnd,rate,losses,");
    printf("max_rate,min_rtt\n");

    unsigned long losses = 0;
    double last_print_time = 0;
    double time = 0;

    dumb_init(&d, time);

    while (time < RUNTIME) {
        double rtt = MIN_RTT*d.cwnd/(double) BDP;
        bool is_cwnd_limited = true;

        if (rtt < MIN_RTT)
            rtt = MIN_RTT;

        if (rtt > MAX_RTT || rand() < RAND_MAX*LOSS_PROB) {
            rtt = MAX_RTT;
            is_cwnd_limited = false;
        }

        if (d.cwnd > BDP + BUF_SIZE) {
            losses++;

            dumb_on_loss(&d, time);
            time += rtt/(BDP + BUF_SIZE);
        } else {
            dumb_on_ack(&d, time, rtt, is_cwnd_limited);

            time += rtt/d.cwnd;
        }

        if (time > last_print_time + REPORT_INTERVAL) {
            double rate = d.cwnd*MSS/rtt;

            if (rate > MAX_BW)
                rate = MAX_BW;

            last_print_time = time;
            printf("%f,%f,%lu,%f,%lu,", time, rtt, d.cwnd, rate,
                   losses);
            printf("%f,%f\n", d.max_rate*MSS, d.min_rtt);
        }
    }

    return 0;
}
