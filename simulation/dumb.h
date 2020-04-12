
#include <stdbool.h>

#ifndef _DUMB_H_
#define _DUMB_H_


enum dumb_mode { DUMB_RECOVER, DUMB_STABLE,
                 DUMB_GAIN_1, DUMB_GAIN_2,
                 DUMB_DRAIN };

enum dumb_loss_mode { DUMB_NO_LOSS, DUMB_LOSS_BACKOFF,
                      DUMB_LOSS };

struct dumb {
    enum dumb_mode mode;
    enum dumb_loss_mode loss_mode;
    double trans_time;

    unsigned long bdp;
    unsigned long cwnd;
    unsigned long ssthresh;

    unsigned long inc_factor;

    double max_rate;

    double last_rtt;
    double min_rtt, max_rtt;
};


void dumb_init(struct dumb *d, double time);
void dumb_on_ack(struct dumb *d, double time, double rtt,
                 unsigned long inflight);
void dumb_on_loss(struct dumb *d, double time);


#endif /* _DUMB_H_ */
