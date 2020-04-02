
#include <stdio.h>

#include "dumb.h"


static const unsigned long MIN_CWND = 2;
static const unsigned long MAX_CWND = 33554432;//32768;

static const unsigned long REC_RTTS = 2;
static const unsigned long STABLE_RTTS = 128;
static const unsigned long GAIN_RTTS = 32;

static const double MAX_RTT_GAIN = 5.0e-3;
static const double RTT_INF = 10.0;

static const double MAX_STABLE_TIME = 4.0;
static const double MAX_GAIN_TIME = 1.0;


#define min(x, y) ((x) < (y) ? (x) : (y))
#define max(x, y) ((x) > (y) ? (x) : (y))

static inline unsigned long bdp(struct dumb *d)
{
    return d->max_rate*d->min_rtt;
}

static inline unsigned long target_cwnd(struct dumb *d)
{
    unsigned long cwnd = min(d->base_cwnd, bdp(d));
    unsigned long gain_cwnd = cwnd*(d->min_rtt + MAX_RTT_GAIN)/d->min_rtt;
    return min(2*cwnd, gain_cwnd);
}


static inline unsigned long cwnd_gain(struct dumb *d)
{
    unsigned long final_gain = target_cwnd(d) - d->base_cwnd;
    unsigned long time_limited = d->min_rtt*final_gain/MAX_GAIN_TIME;
    unsigned long rtt_limited = final_gain/GAIN_RTTS;

    return max(1, max(time_limited, rtt_limited));
}


void dumb_init(struct dumb *d, double time)
{
    d->cwnd = MIN_CWND;
    d->base_cwnd = MAX_CWND;
    d->ssthresh = MAX_CWND;

    d->last_time = time;

    d->rec_count = 0;
    d->stable_count = 0;

    d->max_rate = 0;

    d->last_rtt = 1;
    d->min_rtt = RTT_INF;
    d->min_rtt_save = RTT_INF;
}


void dumb_on_ack(struct dumb *d, double time, double rtt,
                 bool is_cwnd_limited)
{
    if (rtt > 0) {
        d->max_rate = max(d->max_rate, d->cwnd/rtt);

        d->last_rtt = rtt;

        d->min_rtt = min(d->min_rtt, rtt);
        d->min_rtt_save = min(d->min_rtt_save, rtt);
    }

    if (d->cwnd < d->ssthresh) {
        d->cwnd++;

        if (time > d->last_time + STABLE_RTTS*d->min_rtt || !is_cwnd_limited)
            dumb_on_loss(d, time);
    } else if (time > d->last_time + d->last_rtt) {
        d->last_time = time;

        if (d->rec_count > 0) {
            d->rec_count--;

            if (d->rec_count == 0) {
                d->stable_count = min(STABLE_RTTS,
                                      MAX_STABLE_TIME/d->min_rtt);

                d->cwnd = bdp(d) + MIN_CWND;
                d->base_cwnd = d->cwnd;
                d->ssthresh = d->cwnd;

                d->min_rtt = d->min_rtt_save;
                d->min_rtt_save = RTT_INF;
                d->max_rate = 0;
            }
        } else if (d->stable_count > 0) {
            d->stable_count--;
        } else if (d->cwnd > target_cwnd(d) || !is_cwnd_limited) {
            // If we aren't cwnd limited, then RTT no longer reflects
            // congestion, so we decrease the cwnd.
            dumb_on_loss(d, time);
        } else {
            d->cwnd += cwnd_gain(d);
        }
    }

    d->cwnd = min(max(MIN_CWND, d->cwnd), MAX_CWND);
}


void dumb_on_loss(struct dumb *d, double time)
{
    d->last_time = time;
    d->rec_count = REC_RTTS;

    d->cwnd = MIN_CWND;
    d->ssthresh = d->cwnd;
}
