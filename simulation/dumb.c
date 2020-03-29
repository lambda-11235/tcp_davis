
#include <stdio.h>

#include "dumb.h"


static const unsigned long MIN_CWND = 2;
static const unsigned long MAX_CWND = 32768*32768;

static const unsigned long REC_START = 4;

static const double MAX_RTT_GAIN = 5.0e-3;
static const double RTT_INF = 10.0;


#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))


void dumb_init(struct dumb *d)
{
    d->cwnd = MIN_CWND;
    d->ssthresh = MAX_CWND;
    d->rec_count = 0;

    d->min_rtt = RTT_INF;
    d->max_rate = 0;

    d->rtt_sum = 0;
    d->rtt_count = 0;
}


void dumb_on_ack(struct dumb *d, double rtt, unsigned long inflight)
{
    double avg_rtt;

    d->rtt_sum += rtt;
    d->rtt_count++;

    avg_rtt = dumb_avg_rtt(d);

    if (d->rtt_count > max(d->cwnd, inflight)) {
        double target_rtt = dumb_target_rtt(d);

        if (d->rec_count > 0) {
            d->rec_count--;

            if (d->rec_count == 0) {
                d->cwnd = d->max_rate*d->min_rtt;
                d->ssthresh = d->cwnd;

                d->min_rtt = avg_rtt;
                d->max_rate = 0;
            }
        } else if (avg_rtt > target_rtt) {
            dumb_on_loss(d);
        } else {
            d->cwnd++;
        }

        if (avg_rtt > 0) {
            double rate = d->cwnd/avg_rtt;

            d->min_rtt = min(d->min_rtt, avg_rtt);
            d->max_rate = max(d->max_rate, rate);
        }

        d->rtt_sum = rtt;
        d->rtt_count = 1;
    } else if (d->cwnd < d->ssthresh) {
        if (rtt > 0)
            d->max_rate = max(d->max_rate, d->cwnd/rtt);

        d->cwnd++;
    }


    if (d->cwnd < MIN_CWND)
        d->cwnd = MIN_CWND;
}


void dumb_on_loss(struct dumb *d)
{
    d->rec_count = REC_START;

    d->cwnd = MIN_CWND;
    d->ssthresh = d->cwnd;
}


double dumb_avg_rtt(struct dumb *d)
{
    return d->rtt_sum/d->rtt_count;
}


double dumb_target_rtt(struct dumb *d)
{
    double target_rtt = d->min_rtt;

    if (d->min_rtt < MAX_RTT_GAIN)
        target_rtt += d->min_rtt;
    else
        target_rtt += MAX_RTT_GAIN;

    return target_rtt;
}
