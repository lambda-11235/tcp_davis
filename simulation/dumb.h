
#include <stdbool.h>

struct dumb {
    unsigned long cwnd;
    unsigned long base_cwnd;
    unsigned long ssthresh;

    double last_time;
    unsigned long rec_count;
    unsigned long stable_count;

    double max_rate;

    double last_rtt;
    double min_rtt, min_rtt_save;
};


void dumb_init(struct dumb *d, double time);
void dumb_on_ack(struct dumb *d, double time, double rtt,
                 bool is_cwnd_limited);
void dumb_on_loss(struct dumb *d, double time);
