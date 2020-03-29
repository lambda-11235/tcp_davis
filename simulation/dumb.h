
struct dumb {
    unsigned long cwnd;
    unsigned long ssthresh;

    unsigned long rec_count;
    unsigned long save_cwnd;

    double min_rtt;
    double max_rtt;

    double rtt_sum;
    unsigned long rtt_count;
};


void dumb_init(struct dumb *d);
void dumb_on_ack(struct dumb *d, double rtt, unsigned long inflight);
void dumb_on_loss(struct dumb *d);

double dumb_avg_rtt(struct dumb *d);
double dumb_target_rtt(struct dumb *d);
