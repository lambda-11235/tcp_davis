
#include <stdio.h>

#include "dumb.h"


const unsigned long MSS = 512;
const double MIN_RTT = 30e-3;
const double MAX_RTT = 60e-3;
const double MAX_BW = 100<<17;

const unsigned long BDP = MAX_BW*MIN_RTT/MSS;
const unsigned long BUF_SIZE = MAX_BW*MAX_RTT/MSS - BDP;

const double RUNTIME = 60;
const double REPORT_INTERVAL = 0.1;


int main(int argc, char *argv[])
{
    struct dumb d;
    dumb_init(&d);

    d.cwnd = BDP*5/4;

    printf("time,rtt,cwnd,rate,losses,");
    printf("max_rate,min_rtt,avg_rtt\n");

    unsigned long losses = 0;
    double last_print_time = 0;
    double time = 0;
    while (time < RUNTIME) {
        double rtt = MIN_RTT*d.cwnd/(double) BDP;

        if (rtt < MIN_RTT)
            rtt = MIN_RTT;

        if (rtt > MAX_RTT) {
            rtt = MAX_RTT;
            losses++;
            
            dumb_on_loss(&d);

            time += rtt/(BDP + BUF_SIZE);
        } else {
            dumb_on_ack(&d, rtt, d.cwnd);

            time += rtt/d.cwnd;
        }

        if (time > last_print_time + REPORT_INTERVAL) {
            double rate = d.cwnd*MSS/rtt;

            if (rate > MAX_BW)
                rate = MAX_BW;
            
            last_print_time = time;
            printf("%f,%f,%lu,%f,%lu,", time, rtt, d.cwnd, rate,
                   losses);
            printf("%f,%f,%f\n", d.max_rate*MSS, d.min_rtt,
                   dumb_avg_rtt(&d));
        }
    }

    return 0;
}
