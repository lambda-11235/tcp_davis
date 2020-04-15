
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>

#include "dumb.h"


static const unsigned long MIN_CWND = 4;
static const unsigned long MAX_CWND = 33554432;//32768;

static const unsigned long REC_RTTS = 1;
static const unsigned long DRAIN_RTTS = 1;
static const unsigned long STABLE_RTTS = 32;
static const unsigned long GAIN_1_RTTS = 2;
static const unsigned long GAIN_2_RTTS = 1;

static const unsigned long MIN_INC_FACTOR = 2;
static const unsigned long MAX_INC_FACTOR = 128;
static const unsigned long SS_INC_FACTOR = 2;

static const double RTT_INF = 10;


#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))
#define clamp(x, mn, mx) (min(max((x), (mn)), (mx)))


static inline bool in_slow_start(struct dumb *d)
{
    return d->cwnd < d->ssthresh;
}


static inline unsigned long gain_cwnd(struct dumb *d)
{
    unsigned long cwnd = (d->inc_factor + 1)*d->bdp/d->inc_factor;
    return max(d->bdp + MIN_CWND, cwnd);
}


static inline unsigned long ss_cwnd(struct dumb *d)
{
    unsigned long cwnd = (SS_INC_FACTOR + 1)*d->bdp/SS_INC_FACTOR;
    return max(d->bdp + MIN_CWND, cwnd);
}




////////// Enter Routines START //////////
static void enter_recovery(struct dumb *d, double time)
{
    d->mode = DUMB_RECOVER;
    d->trans_time = time;

    d->cwnd = d->bdp;
    d->ssthresh = d->bdp;
}


static void enter_stable(struct dumb *d, double time)
{
    d->mode = DUMB_STABLE;
    d->trans_time = time;

    d->bdp = d->max_rate*d->min_rtt/d->mss;
    d->bdp = max(MIN_CWND, d->bdp);

    d->cwnd = d->bdp;
    d->ssthresh = d->bdp;

    d->pacing_rate = d->max_rate;

    d->inc_factor--;
    d->inc_factor = max(d->inc_factor, MIN_INC_FACTOR);
}


static void enter_gain_1(struct dumb *d, double time)
{
    d->mode = DUMB_GAIN_1;
    d->trans_time = time;

    d->cwnd = gain_cwnd(d);
    d->pacing_rate *= 2;
}


static void enter_gain_2(struct dumb *d, double time)
{
    d->mode = DUMB_GAIN_2;
    d->trans_time = time;

    d->max_rate = 0;

    d->min_rtt = RTT_INF;
    d->max_rtt = 0;
}


static void enter_drain(struct dumb *d, double time)
{
    d->mode = DUMB_DRAIN;
    d->trans_time = time;

    d->cwnd = MIN_CWND;
    d->ssthresh = d->cwnd;
}
////////// Enter Routines END //////////




void dumb_init(struct dumb *d, double time,
               unsigned long mss)
{
    d->mode = DUMB_RECOVER;
    d->trans_time = time;

    d->mss = mss;
    d->bdp = MIN_CWND;
    d->cwnd = MIN_CWND;
    d->ssthresh = MAX_CWND;

    d->inc_factor = MIN_INC_FACTOR;

    d->pacing_rate = 0;
    d->max_rate = 0;

    d->last_rtt = 1;
    d->min_rtt = RTT_INF;
    d->max_rtt = 0;
}


static void dumb_slow_start(struct dumb *d, double time, double rtt)
{
    if (d->mode == DUMB_GAIN_1) {
        if (time > d->trans_time + GAIN_1_RTTS*d->last_rtt) {
            d->mode = DUMB_GAIN_2;
            d->trans_time = time;

            d->max_rate = 0;
        }
    } else if (d->mode == DUMB_GAIN_2) {
        if (time > d->trans_time + GAIN_2_RTTS*d->last_rtt) {
            unsigned long new_bdp = max(MIN_CWND, d->max_rate*d->min_rtt/d->mss);

            if (new_bdp > d->bdp) {
                d->mode = DUMB_GAIN_1;
                d->trans_time = time;

                d->bdp = new_bdp;
                d->cwnd = ss_cwnd(d);
                d->pacing_rate = 2*d->max_rate;
            } else {
                d->bdp = new_bdp;
                enter_recovery(d, time);
            }
        }
    } else {
        d->mode = DUMB_GAIN_1;
        d->trans_time = time;

        d->max_rate = 0;
        d->min_rtt = RTT_INF;
        d->max_rtt = 0;

        d->cwnd = ss_cwnd(d);
        d->pacing_rate = 2*d->max_rate;
    }
}


void dumb_on_ack(struct dumb *d, double time, double rtt,
                 unsigned long inflight)
{
    if (rtt > 0) {
        if (d->mode == DUMB_GAIN_2)
            d->max_rate = max(d->max_rate, inflight*d->mss/rtt);

        d->last_rtt = rtt;
        d->min_rtt = min(d->min_rtt, rtt);
        d->max_rtt = max(d->max_rtt, rtt);
    }


    if (in_slow_start(d)) {
        dumb_slow_start(d, time, rtt);
    } else if (d->mode == DUMB_RECOVER || d->mode == DUMB_STABLE) {
        unsigned long rtts = d->mode == DUMB_RECOVER ? REC_RTTS : STABLE_RTTS;

        if (time > d->trans_time + rtts*d->last_rtt) {
            enter_gain_1(d, time);
        }
    } else if (d->mode == DUMB_GAIN_1) {
        if (time > d->trans_time + GAIN_1_RTTS*d->last_rtt) {
            enter_gain_2(d, time);
        }
    } else if (d->mode == DUMB_GAIN_2) {
        if (time > d->trans_time + GAIN_2_RTTS*d->last_rtt) {
            enter_drain(d, time);
        }
    } else if (d->mode == DUMB_DRAIN) {
        if (time > d->trans_time + DRAIN_RTTS*d->last_rtt) {
            enter_stable(d, time);
        }
    } else {
        fprintf(stderr, "Got to undefined mode %d at time %f\n", d->mode, time);
        enter_drain(d, time);
    }

    d->cwnd = clamp(d->cwnd, MIN_CWND, MAX_CWND);
}


void dumb_on_loss(struct dumb *d, double time)
{
    bool react = d->mode == DUMB_GAIN_1 || d->mode == DUMB_GAIN_2;
    react = react && d->inc_factor < MAX_INC_FACTOR;
    react = react || in_slow_start(d);

    if (react) {
        d->inc_factor *= 2;
        d->inc_factor = min(d->inc_factor, MAX_INC_FACTOR);

        enter_recovery(d, time);
    }
}
