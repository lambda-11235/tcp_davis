
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "davis.h"


static const unsigned long MIN_CWND = 4;
static const unsigned long MAX_CWND = 33554432;//32768;

static const unsigned long MIN_GAIN_CWND = 1;
static const unsigned long MAX_GAIN_FACTOR = 16;
static const double GAIN_RATE = 1048576;

static const unsigned long DRAIN_RTTS = 2;
static const unsigned long GAIN_1_RTTS = 1;
static const unsigned long GAIN_2_RTTS = 1;

static const double RTT_INF = 10;
static const double RTT_TIMEOUT = 10;


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

    d->min_rtt = d->srtt;
}


static unsigned long gain_cwnd(struct davis *d)
{
    unsigned long gain = GAIN_RATE*d->min_rtt/d->mss;
    gain = min(gain, d->bdp/MAX_GAIN_FACTOR);
    gain = max(gain, MIN_GAIN_CWND);

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

    d->delivered_start = 0;
    d->delivered_start_time = time;

    d->bdp = MIN_CWND;
    d->ss_last_bdp = 0;

    d->pacing_rate = 0;

    d->srtt = 0;
    d->min_rtt = RTT_INF;
    d->min_rtt_time = time;
}


static void davis_slow_start(struct davis *d, double time, double rtt,
                             unsigned long pkts_delivered)
{
    if (d->mode == DAVIS_GAIN_1) {
        if (time > d->trans_time + GAIN_1_RTTS*d->srtt) {
            d->mode = DAVIS_GAIN_2;
            d->trans_time = time;

            d->delivered_start = pkts_delivered;
            d->delivered_start_time = time;
        }
    } else if (d->mode == DAVIS_GAIN_2) {
        if (time > d->trans_time + GAIN_2_RTTS*d->srtt) {
            unsigned long diff_deliv = pkts_delivered - d->delivered_start;
            double interval = time - d->delivered_start_time;
            d->bdp = ceil(diff_deliv*d->min_rtt/interval);

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
                  unsigned long pkts_delivered)
{
    if (rtt > 0) {
        if (d->srtt == 0)
            d->srtt = rtt;
        else
            d->srtt = (7*d->srtt + rtt)/8;
    }

    if (d->srtt > 0 && d->srtt < d->min_rtt) {
        d->min_rtt = d->srtt;
        d->min_rtt_time = time;
    }


    if (in_slow_start(d)) {
        davis_slow_start(d, time, rtt, pkts_delivered);
    } else if (d->mode == DAVIS_DRAIN) {
        if (time > d->trans_time + DRAIN_RTTS*d->srtt) {
            d->mode = DAVIS_GAIN_1;
            d->trans_time = time;

            d->cwnd = d->bdp + gain_cwnd(d);
        }
    } else if (d->mode == DAVIS_GAIN_1) {
        if (time > d->trans_time + GAIN_1_RTTS*d->srtt) {
            d->mode = DAVIS_GAIN_2;
            d->trans_time = time;

            d->delivered_start = pkts_delivered;
            d->delivered_start_time = time;
        }
    } else if (d->mode == DAVIS_GAIN_2) {
        if (time > d->trans_time + GAIN_2_RTTS*d->srtt) {
            unsigned long diff_deliv = pkts_delivered - d->delivered_start;
            double interval = time - d->delivered_start_time;
            d->bdp = ceil(diff_deliv*d->min_rtt/interval);

            if (time > d->min_rtt_time + RTT_TIMEOUT) {
                d->mode = DAVIS_DRAIN;
                d->trans_time = time;

                d->cwnd = MIN_CWND;
                d->min_rtt = d->srtt;
                d->min_rtt_time = time;
            } else {
                d->mode = DAVIS_GAIN_1;
                d->trans_time = time;

                d->cwnd = d->bdp + gain_cwnd(d);
            }
        }
    } else {
        fprintf(stderr, "Got to undefined mode %d at time %f\n", d->mode, time);

        d->mode = DAVIS_DRAIN;
        d->trans_time = time;

        d->cwnd = MIN_CWND;
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
