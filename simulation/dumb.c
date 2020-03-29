
#include <stdio.h>

#include "dumb.h"


static const unsigned long MIN_CWND = 2;
static const unsigned long MAX_CWND = 32768*32768;

static const unsigned long REC_START = 4;

static const double MAX_RTT_GAIN = 5.0e-3;
static const double RTT_LOW = 1.0e-6;
static const double RTT_HIGH = 10.0;


#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))


void dumb_init(struct dumb *d)
{
    d->cwnd = MIN_CWND;
    d->ssthresh = MAX_CWND;

    d->rec_count = 0;
    d->save_cwnd = MIN_CWND;
    
    d->min_rtt = RTT_HIGH;
    d->max_rtt = RTT_LOW;
    
    d->rtt_sum = 0;
    d->rtt_count = 0;
}


void dumb_on_ack(struct dumb *d, double rtt, unsigned long inflight)
{
    double avg_rtt;
    unsigned long acks = d->cwnd;

    d->rtt_sum += rtt;
    d->rtt_count++;

    avg_rtt = dumb_avg_rtt(d);

    if (d->rtt_count > max(d->cwnd, inflight)) {
        double target_rtt = dumb_target_rtt(d);

        if (d->rec_count > 0) {
            d->rec_count--;

            if (d->rec_count == 0) {
                d->cwnd = d->save_cwnd*d->min_rtt/d->max_rtt;
                d->ssthresh = d->cwnd;

                d->save_cwnd = MIN_CWND;
                
                d->min_rtt = avg_rtt;
                d->max_rtt = avg_rtt;
            }
        } else if (avg_rtt > target_rtt) {
            dumb_on_loss(d);
        } else {
            d->cwnd++;
        }

        if (avg_rtt > 0) {
            d->min_rtt = min(d->min_rtt, avg_rtt);
            d->max_rtt = max(d->max_rtt, avg_rtt);
        }

        d->rtt_sum = rtt;
        d->rtt_count = 1;
    } else if (d->cwnd < d->ssthresh) {
        d->cwnd++;
    }


    if (d->cwnd < MIN_CWND)
        d->cwnd = MIN_CWND;
}


void dumb_on_loss(struct dumb *d)
{
    d->rec_count = REC_START;
    d->save_cwnd = d->cwnd;

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
