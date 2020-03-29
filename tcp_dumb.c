// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dumb congestion control
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>


static const u64 MIN_CWND = 2;
static const u32 MAX_RTT_GAIN = 5000;
static const u32 RTT_INF = 10000000;
static const u8 REC_START = 4;


struct dumb {
    // RTTs are in microseconds
    u64 rtt_sum;
    u64 rtt_count;

    // max_rate is in mss/second
    u64 max_rate;
    u32 min_rtt;

    u8 rec_count;
};


static u32 dumb_target_rtt(struct dumb *dumb)
{
    u32 target_rtt = dumb->min_rtt;
    target_rtt += min(dumb->min_rtt/2, MAX_RTT_GAIN);

    return target_rtt;
}


void tcp_dumb_init(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct dumb *dumb = inet_csk_ca(sk);

    tp->snd_cwnd = MIN_CWND;
    tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;

    dumb->rec_count = 0;

    dumb->min_rtt = RTT_INF;
    dumb->max_rate = 0;

    dumb->rtt_sum = 0;
    dumb->rtt_count = 0;
}
EXPORT_SYMBOL_GPL(tcp_dumb_init);


void tcp_dumb_release(struct sock *sk)
{
    struct dumb *dumb = inet_csk_ca(sk);

    printk(KERN_INFO "tcp_dumb: min_rtt = %u, max_rate = %llu\n",
           dumb->min_rtt, dumb->max_rate);
}
EXPORT_SYMBOL_GPL(tcp_dumb_release);


static inline u32 tcp_dumb_ssthresh(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);

    return tp->snd_ssthresh;
}
EXPORT_SYMBOL_GPL(tcp_dumb_ssthresh);


static u32 tcp_dumb_undo_cwnd(struct sock *sk)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    dumb->rec_count = REC_START;

    tp->snd_cwnd = MIN_CWND;
    tp->snd_ssthresh = tp->snd_cwnd;

    return tp->snd_cwnd;
}
EXPORT_SYMBOL_GPL(tcp_dumb_undo_cwnd);


static void tcp_dumb_cong_control(struct sock *sk, const struct rate_sample *rs)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    u32 avg_rtt;

    // We can get multiple ACKs at once, so assume they all have the
    // same RTT
    dumb->rtt_sum += rs->rtt_us*rs->acked_sacked;
    dumb->rtt_count += rs->acked_sacked;

    avg_rtt = dumb->rtt_sum/dumb->rtt_count;

    if (dumb->rtt_count > max(tp->snd_cwnd, rs->prior_in_flight)) {
        u32 target_rtt = dumb_target_rtt(dumb);

        if (dumb->rec_count > 0) {
            dumb->rec_count--;

            if (dumb->rec_count == 0) {
                tp->snd_cwnd = dumb->max_rate*dumb->min_rtt/USEC_PER_SEC;
                tp->snd_ssthresh = tp->snd_cwnd;

                dumb->min_rtt = avg_rtt;
                dumb->max_rate = 0;
            }
        } else if (avg_rtt > target_rtt) {
            tcp_dumb_undo_cwnd(sk);
        } else {
            tp->snd_cwnd++;
        }

        if (avg_rtt > 0) {
            u64 rate = tp->snd_cwnd*USEC_PER_SEC/avg_rtt;

            dumb->min_rtt = min(dumb->min_rtt, avg_rtt);
            dumb->max_rate = max(dumb->max_rate, rate);
        }

        dumb->rtt_sum = rs->rtt_us;
        dumb->rtt_count = 1;
    } else if (tp->snd_cwnd < tp->snd_ssthresh) {
        if (rs->rtt_us > 0) {
            u64 rate = tp->snd_cwnd*USEC_PER_SEC/rs->rtt_us;
            dumb->max_rate = max(dumb->max_rate, rate);
        }

        tp->snd_cwnd++;
    }


    if (tp->snd_cwnd < MIN_CWND)
        tp->snd_cwnd = MIN_CWND;
}
EXPORT_SYMBOL_GPL(tcp_dumb_cong_control);

static struct tcp_congestion_ops tcp_dumb __read_mostly = {
    .init		= tcp_dumb_init,
    .release	= tcp_dumb_release,
    .ssthresh	= tcp_dumb_ssthresh,
    .undo_cwnd	= tcp_dumb_undo_cwnd,
    .cong_control	= tcp_dumb_cong_control,

    .owner		= THIS_MODULE,
    .name		= "dumb",
};

static int __init tcp_dumb_register(void)
{
    BUILD_BUG_ON(sizeof(struct dumb) > ICSK_CA_PRIV_SIZE);
    tcp_register_congestion_control(&tcp_dumb);
    return 0;
}

static void __exit tcp_dumb_unregister(void)
{
    tcp_unregister_congestion_control(&tcp_dumb);
}

module_init(tcp_dumb_register);
module_exit(tcp_dumb_unregister);

MODULE_AUTHOR("Taran Lynn");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Dumb");
