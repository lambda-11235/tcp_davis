// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dumb congestion control
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>


static const u32 MIN_CWND = 2;

static const u32 MAX_RTT_GAIN = 5*USEC_PER_MSEC;
static const u32 RTT_INF = 10*USEC_PER_SEC;
static const u32 MAX_CYCLE_TIME = 10*USEC_PER_SEC;

static const u8 REC_START = 2;


struct dumb {
    u32 base_cwnd;

    // max_rate is in mss/second
    u64 max_rate;
    u32 min_rtt, min_rtt_save;

    u8 rec_count;
};


static inline u32 target_rtt(struct dumb *dumb)
{
    return dumb->min_rtt + MAX_RTT_GAIN;
}


static inline u32 target_cwnd(struct dumb *dumb)
{
    u32 bdp = min(dumb->base_cwnd, (u32) (dumb->max_rate*dumb->min_rtt/USEC_PER_SEC));
    u32 gain_cwnd = bdp*(dumb->min_rtt + MAX_RTT_GAIN)/dumb->min_rtt;
    return min(2*bdp, gain_cwnd);
}


/**
 * Each ACK we increase the snd_cwnd by a certain amount.
 * This function determines how much we must increase the snd_cwnd by
 * to reach our maximum snd_cwnd in MAX_CYCLE_TIME.
 */
static inline u32 cwnd_gain(struct dumb *dumb)
{
    return max(1U, dumb->min_rtt*(target_cwnd(dumb) - dumb->base_cwnd)/MAX_CYCLE_TIME);
}


void tcp_dumb_init(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct dumb *dumb = inet_csk_ca(sk);

    tp->snd_cwnd = MIN_CWND;
    tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;
    tp->snd_cwnd_cnt = 0;

    dumb->base_cwnd = tp->snd_cwnd_clamp;

    dumb->rec_count = 0;

    dumb->min_rtt = RTT_INF;
    dumb->min_rtt_save = RTT_INF;
    dumb->max_rate = 0;
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

    // We can get multiple ACKs at once, so assume they all have the
    // same RTT
    tp->snd_cwnd_cnt += rs->acked_sacked;

    if (rs->rtt_us > 0) {
        u64 rate = tp->snd_cwnd*USEC_PER_SEC/rs->rtt_us;

        dumb->min_rtt = min(dumb->min_rtt, (u32) rs->rtt_us);
        dumb->min_rtt_save = min(dumb->min_rtt_save, (u32) rs->rtt_us);
        dumb->max_rate = max(dumb->max_rate, rate);
    }

    if (tp->snd_cwnd_cnt > max(tp->snd_cwnd, rs->prior_in_flight)) {
        if (dumb->rec_count > 0) {
            dumb->rec_count--;

            if (dumb->rec_count == 0) {
                tp->snd_cwnd = dumb->max_rate*dumb->min_rtt/USEC_PER_SEC;
                tp->snd_ssthresh = tp->snd_cwnd;

                dumb->base_cwnd = tp->snd_cwnd;

                dumb->min_rtt = dumb->min_rtt_save;
                dumb->min_rtt_save = RTT_INF;
                dumb->max_rate = 0;
            }
        } else if (tp->snd_cwnd > target_cwnd(dumb) || !tcp_is_cwnd_limited(sk)) {
            // If snd_cwnd is over data being sent by app, then probe
            tcp_dumb_undo_cwnd(sk);
        } else {
            tp->snd_cwnd += cwnd_gain(dumb);
        }

        tp->snd_cwnd_cnt = 0;
    } else if (tcp_in_slow_start(tp) && tcp_is_cwnd_limited(sk)) {
        tp->snd_cwnd++;
    }


    tp->snd_cwnd = min(max(MIN_CWND, tp->snd_cwnd), tp->snd_cwnd_clamp);
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
