
#include <limits.h>
#include <stdio.h>

#include "dumb.h"


static const unsigned long MIN_CWND = 2;
static const unsigned long MAX_CWND = 33554432;//32768;

static const unsigned long REC_RTTS = 2;
static const unsigned long DRAIN_RTTS = 2;
static const unsigned long STABLE_RTTS = 32;
static const unsigned long GAIN_1_RTTS = 2;
static const unsigned long GAIN_2_RTTS = 2;

static const unsigned long MIN_INC_FACTOR = 2;
static const unsigned long MAX_INC_FACTOR = 128;

static const double RTT_INF = ULONG_MAX;


#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))
#define clamp(x, mn, mx) (min(max((x), (mn)), (mx)))


static inline unsigned long gain_cwnd(struct dumb *d)
{
    unsigned long cwnd;

    d->inc_factor = clamp(d->inc_factor, MIN_INC_FACTOR, MAX_INC_FACTOR);
    cwnd = (d->inc_factor + 1)*d->bdp/d->inc_factor;

    return max(d->bdp + MIN_CWND, cwnd);
}


static inline unsigned long drain_cwnd(struct dumb *d)
{
    return d->bdp/2;
}


static void drain(struct dumb *d, double time)
{
    d->mode = DUMB_DRAIN;
    d->trans_time = time;

    d->bdp = max(MIN_CWND, d->max_rate*d->min_rtt);
    d->cwnd = drain_cwnd(d);
    d->ssthresh = d->cwnd;
}


void dumb_init(struct dumb *d, double time)
{
    d->mode = DUMB_DRAIN;
    d->trans_time = time;

    d->bdp = MAX_CWND;
    d->cwnd = MIN_CWND;
    d->ssthresh = MAX_CWND;

    d->inc_factor = 2;

    d->max_rate = 0;

    d->last_rtt = 1;
    d->min_rtt = RTT_INF;
    d->max_rtt = 0;
}


void dumb_on_ack(struct dumb *d, double time, double rtt,
                 unsigned long inflight)
{
    if (rtt > 0) {
        d->max_rate = max(d->max_rate, inflight/rtt);

        d->last_rtt = rtt;
        d->min_rtt = min(d->min_rtt, rtt);
        d->max_rtt = max(d->max_rtt, rtt);
    }


    if (d->cwnd < d->ssthresh) {
        if (time > d->trans_time + d->last_rtt) {
            unsigned long new_bdp = max(MIN_CWND, d->max_rate*d->min_rtt);
            d->trans_time = time;

            if (d->max_rtt > 3*d->min_rtt/2 || d->bdp == new_bdp) {
                drain(d, time);
            } else {
                d->bdp = new_bdp;
                d->cwnd = gain_cwnd(d);
            }
        }
    } else if (d->mode == DUMB_DRAIN) {
        if (time > d->trans_time + DRAIN_RTTS*d->last_rtt) {
            d->mode = DUMB_STABLE;
            d->trans_time = time;

            d->bdp = max(MIN_CWND, d->max_rate*d->min_rtt);
            d->cwnd = d->bdp;
            d->ssthresh = d->bdp;

            d->inc_factor--;
        } else {
            d->bdp = max(MIN_CWND, d->max_rate*d->min_rtt);
            d->cwnd = drain_cwnd(d);
            d->ssthresh = d->cwnd;
        }
    } else if (d->mode == DUMB_RECOVER || d->mode == DUMB_STABLE) {
        unsigned long rtts = d->mode == DUMB_RECOVER ? REC_RTTS : STABLE_RTTS;

        if (time > d->trans_time + rtts*d->last_rtt) {
            d->mode = DUMB_GAIN_1;
            d->trans_time = time;

            d->cwnd = gain_cwnd(d);
        }
    } else if (d->mode == DUMB_GAIN_1) {
        if (time > d->trans_time + GAIN_1_RTTS*d->last_rtt) {
            d->mode = DUMB_GAIN_2;
            d->trans_time = time;

            d->max_rate = 0;

            d->min_rtt = RTT_INF;
            d->max_rtt = 0;
        }
    } else if (d->mode == DUMB_GAIN_2) {
        if (time > d->trans_time + GAIN_2_RTTS*d->last_rtt) {
            drain(d, time);
        }
    } else {
        fprintf(stderr, "Got to undefined mode %d at time %f\n", d->mode, time);
        drain(d, time);
    }

    d->cwnd = clamp(d->cwnd, MIN_CWND, MAX_CWND);
}


void dumb_on_loss(struct dumb *d, double time)
{
    if ((d->mode == DUMB_GAIN_1 || d->mode == DUMB_GAIN_2)
        && d->inc_factor < MAX_INC_FACTOR) {
        d->inc_factor *= 2;

        d->mode = DUMB_RECOVER;
        d->trans_time = time;

        d->cwnd = d->bdp;
        d->ssthresh = d->bdp;
    } else if (d->cwnd < d->ssthresh) {
        d->bdp = max(MIN_CWND, d->bdp/2);

        d->mode = DUMB_RECOVER;
        d->trans_time = time;

        d->cwnd = d->bdp;
        d->ssthresh = d->bdp;
    }
}
