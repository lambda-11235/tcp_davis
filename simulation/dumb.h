
struct dumb {
    unsigned long cwnd;
    unsigned long base_cwnd;
    unsigned long ssthresh;

    unsigned long rec_count;

    double min_rtt, min_rtt_save;
    double max_rate;

    double rtt_sum;
    unsigned long rtt_count;
};


void dumb_init(struct dumb *d);
void dumb_on_ack(struct dumb *d, double rtt, unsigned long inflight);
void dumb_on_loss(struct dumb *d);

double dumb_avg_rtt(struct dumb *d);
