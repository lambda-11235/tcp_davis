
#include <stdbool.h>

#ifndef _DUMB_H_
#define _DUMB_H_


enum dumb_mode { DUMB_RECOVER, DUMB_DRAIN, DUMB_STABLE,
                 DUMB_GAIN_1, DUMB_GAIN_2 };

struct dumb {
    enum dumb_mode mode;
    double trans_time;

    unsigned long bdp;
    unsigned long cwnd;
    unsigned long ssthresh;

    double max_rate;

    double last_rtt;
    double min_rtt, max_rtt;
};


void dumb_init(struct dumb *d, double time);
void dumb_on_ack(struct dumb *d, double time, double rtt,
                 unsigned long inflight);
void dumb_on_loss(struct dumb *d, double time);


#endif /* _DUMB_H_ */
