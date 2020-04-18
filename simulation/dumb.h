
#include <stdbool.h>

#ifndef _DUMB_H_
#define _DUMB_H_


enum dumb_mode { DUMB_RECOVER, DUMB_STABLE, DUMB_DRAIN,
                 DUMB_GAIN_1, DUMB_GAIN_2 };

struct dumb {
    enum dumb_mode mode;
    double trans_time;

    unsigned long mss;
    unsigned long cwnd;
    unsigned long ssthresh;

    unsigned long bdp;
    unsigned long ss_last_bdp;

    unsigned long inc_factor;

    double pacing_rate;

    double last_rtt;
    double min_rtt;
};


void dumb_init(struct dumb *d, double time,
               unsigned long mss);
void dumb_on_ack(struct dumb *d, double time, double rtt,
                 unsigned long inflight);
void dumb_on_loss(struct dumb *d, double time);


#endif /* _DUMB_H_ */
