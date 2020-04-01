
#include <stdio.h>

#include "dumb.h"


static const unsigned long MIN_CWND = 2;
static const unsigned long MAX_CWND = 33554432;//32768;

static const unsigned long REC_START = 2;

static const double MAX_RTT_GAIN = 5.0e-3;
static const double RTT_INF = 10.0;

static const double MAX_CYCLE_TIME = 10.0;


#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))


static inline unsigned long target_cwnd(struct dumb *d)
{
    unsigned long bdp = min(d->base_cwnd, d->max_rate*d->min_rtt);
    unsigned long gain_cwnd = bdp*(d->min_rtt + MAX_RTT_GAIN)/d->min_rtt;
    return min(2*bdp, gain_cwnd);
}


static inline unsigned long cwnd_gain(struct dumb *d)
{
    return max(1, d->min_rtt*(target_cwnd(d) - d->base_cwnd)/MAX_CYCLE_TIME);
}


void dumb_init(struct dumb *d)
{
    d->cwnd = MIN_CWND;
    d->base_cwnd = MAX_CWND;
    d->ssthresh = MAX_CWND;

    d->rec_count = 0;
    d->rtt_count = 0;

    d->min_rtt = RTT_INF;
    d->min_rtt_save = RTT_INF;
    d->max_rate = 0;
}


void dumb_on_ack(struct dumb *d, double rtt, unsigned long inflight)
{
    d->rtt_count++;

    if (rtt > 0) {
        d->min_rtt = min(d->min_rtt, rtt);
        d->min_rtt_save = min(d->min_rtt_save, rtt);
        d->max_rate = max(d->max_rate, d->cwnd/rtt);
    }

    if (d->rtt_count > max(d->cwnd, inflight)) {
        if (d->rec_count > 0) {
            d->rec_count--;

            if (d->rec_count == 0) {
                d->cwnd = d->max_rate*d->min_rtt;
                d->base_cwnd = d->cwnd;
                d->ssthresh = d->cwnd;

                d->min_rtt = d->min_rtt_save;
                d->min_rtt_save = RTT_INF;
                d->max_rate = 0;
            }
        } else if (d->cwnd > target_cwnd(d)) {
            dumb_on_loss(d);
        } else {
            d->cwnd += cwnd_gain(d);
        }

        d->rtt_count = 0;
    } else if (d->cwnd < d->ssthresh) {
        d->cwnd++;
    }

    d->cwnd = min(max(MIN_CWND, d->cwnd), MAX_CWND);
}


void dumb_on_loss(struct dumb *d)
{
    d->rec_count = REC_START;

    d->cwnd = MIN_CWND;
    d->ssthresh = d->cwnd;
}
