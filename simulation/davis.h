
#include <stdbool.h>

#ifndef _DAVIS_H_
#define _DAVIS_H_


enum davis_mode { DAVIS_DRAIN, DAVIS_GAIN_1, DAVIS_GAIN_2 };

struct davis {
    enum davis_mode mode;
    double trans_time;

    unsigned long mss;
    unsigned long cwnd;
    unsigned long ssthresh;

    unsigned long delivered;
    double dinterval;

    unsigned long bdp;
    unsigned long ss_last_bdp;

    double pacing_rate;

    double last_rtt;
    double min_rtt;
};


void davis_init(struct davis *d, double time,
               unsigned long mss);
void davis_on_ack(struct davis *d, double time, double rtt,
                 unsigned long delivered);
void davis_on_loss(struct davis *d, double time);


#endif /* _DAVIS_H_ */
