
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "davis.h"


static const unsigned long MIN_CWND = 4;
static const unsigned long MAX_CWND = 33554432;//32768;

static const unsigned long MIN_GAIN_CWND = 4;
static const double GAIN_RATE = 1048576;

static const unsigned long DRAIN_RTTS = 2;
static const unsigned long GAIN_1_RTTS = 1;
static const unsigned long GAIN_2_RTTS = 1;

static const double RTT_INF = 10;


#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))
#define clamp(x, mn, mx) (min(max((x), (mn)), (mx)))


static inline bool in_slow_start(struct davis *d)
{
    return d->cwnd < d->ssthresh;
}


static void enter_slow_start(struct davis *d, double time)
{
    d->mode = DAVIS_GAIN_1;
    d->trans_time = time;

    d->bdp = MIN_CWND;
    d->ss_last_bdp = 0;

    d->cwnd = MIN_CWND;

    d->min_rtt = d->last_rtt;
}


static unsigned long gain_cwnd(struct davis *d)
{
    unsigned long gain = GAIN_RATE*d->min_rtt/d->mss;
    gain = clamp(gain, MIN_GAIN_CWND, d->bdp/2);

    return gain;
}


void davis_init(struct davis *d, double time,
               unsigned long mss)
{
    d->mode = DAVIS_GAIN_1;
    d->trans_time = time;

    d->mss = mss;
    d->cwnd = MIN_CWND;
    d->ssthresh = MAX_CWND;

    d->delivered = 0;
    d->dinterval = 0;

    d->bdp = MIN_CWND;
    d->ss_last_bdp = 0;

    d->pacing_rate = 0;

    d->last_rtt = 1;
    d->min_rtt = RTT_INF;
}


static void davis_slow_start(struct davis *d, double time, double rtt)
{
    if (d->mode == DAVIS_GAIN_1) {
        if (time > d->trans_time + GAIN_1_RTTS*d->last_rtt) {
            d->mode = DAVIS_GAIN_2;
            d->trans_time = time;

            d->delivered = 0;
            d->dinterval = 0;
        }
    } else if (d->mode == DAVIS_GAIN_2) {
        if (time > d->trans_time + GAIN_2_RTTS*d->last_rtt) {
            d->bdp = ceil(d->delivered*d->min_rtt/d->dinterval/d->mss);

            if (d->bdp > d->ss_last_bdp) {
                d->mode = DAVIS_GAIN_1;
                d->trans_time = time;

                d->cwnd = 3*d->bdp/2;

                d->ss_last_bdp = d->bdp;
            } else {
                d->mode = DAVIS_GAIN_1;
                d->trans_time = time;

                d->cwnd = d->bdp + gain_cwnd(d);
                d->ssthresh = MIN_CWND;
            }
        }
    } else {
        enter_slow_start(d, time);
    }
}


void davis_on_ack(struct davis *d, double time, double rtt,
                 unsigned long delivered)
{
    if (rtt > 0) {
        if (d->mode == DAVIS_GAIN_2) {
            d->delivered += delivered;
            d->dinterval = time - d->trans_time;
        }

        d->last_rtt = rtt;
        d->min_rtt = min(d->min_rtt, rtt);
    }


    if (in_slow_start(d)) {
        davis_slow_start(d, time, rtt);
    } else if (d->mode == DAVIS_DRAIN) {
        if (time > d->trans_time + DRAIN_RTTS*d->last_rtt) {
            d->mode = DAVIS_GAIN_1;
            d->trans_time = time;

            d->cwnd = d->bdp + gain_cwnd(d);
        }
    } else if (d->mode == DAVIS_GAIN_1) {
        if (time > d->trans_time + GAIN_1_RTTS*d->last_rtt) {
            d->mode = DAVIS_GAIN_2;
            d->trans_time = time;

            d->delivered = 0;
            d->dinterval = 0;
        }
    } else if (d->mode == DAVIS_GAIN_2) {
        if (time > d->trans_time + GAIN_2_RTTS*d->last_rtt) {
            d->bdp = ceil(d->delivered*d->min_rtt/d->dinterval/d->mss);

            d->mode = DAVIS_DRAIN;
            d->trans_time = time;

            d->cwnd = d->bdp - gain_cwnd(d);
        }
    } else {
        fprintf(stderr, "Got to undefined mode %d at time %f\n", d->mode, time);

        d->mode = DAVIS_DRAIN;
        d->trans_time = time;

            d->cwnd = d->bdp - gain_cwnd(d);
    }

    d->cwnd = clamp(d->cwnd, MIN_CWND, MAX_CWND);
    d->pacing_rate = 0;
}


void davis_on_loss(struct davis *d, double time)
{
    if (in_slow_start(d)) {
        d->mode = DAVIS_GAIN_1;
        d->trans_time = time;

        d->cwnd = d->bdp + gain_cwnd(d);
    }
}
