
#include <stdbool.h>

#ifndef _DAVIS_H_
#define _DAVIS_H_


enum davis_mode { DAVIS_STABLE, DAVIS_DRAIN, DAVIS_GAIN_1, DAVIS_GAIN_2 };

struct davis {
    enum davis_mode mode;
    double trans_time;

    unsigned long mss;
    unsigned long cwnd;
    unsigned long ssthresh;

    unsigned long delivered_start;
    double delivered_start_time;

    unsigned long bdp;
    unsigned long ss_last_bdp;

    double pacing_rate;

    double srtt;
    double min_rtt, min_rtt_time;
};


void davis_init(struct davis *d, double time,
               unsigned long mss);
void davis_on_ack(struct davis *d, double time, double rtt,
                 unsigned long pkts_delivered);
void davis_on_loss(struct davis *d, double time);


#endif /* _DAVIS_H_ */
