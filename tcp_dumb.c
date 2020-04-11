// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dumb congestion control
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>


static const u32 MIN_CWND = 4;

static const u32 REC_RTTS = 2;
static const u32 DRAIN_RTTS = 2;
static const u32 STABLE_RTTS = 32;
static const u32 GAIN_1_RTTS = 2;
static const u32 GAIN_2_RTTS = 2;

static const unsigned long MIN_INC_FACTOR = 2;
static const unsigned long MAX_INC_FACTOR = 128;

static const u32 RTT_INF = 10*USEC_PER_SEC;


enum dumb_mode { DUMB_RECOVER, DUMB_DRAIN, DUMB_STABLE,
                 DUMB_GAIN_1, DUMB_GAIN_2 };

struct dumb {
    enum dumb_mode mode;
    u64 trans_time;

    // max_rate is in mss/second
    u64 max_rate;

    u32 inc_factor;

    u32 last_rtt;
    u32 min_rtt, max_rtt;

    u32 bdp;
};


static inline u32 gain_cwnd(struct dumb *dumb)
{
    u32 cwnd;

    dumb->inc_factor = clamp_t(u32, MIN_INC_FACTOR, dumb->inc_factor, MAX_INC_FACTOR);
    cwnd = (dumb->inc_factor + 1)*dumb->bdp/dumb->inc_factor;

    return max_t(u32, dumb->bdp + MIN_CWND, cwnd);
}


static inline u32 drain_cwnd(struct dumb *dumb)
{
    return dumb->bdp/2;
}


static inline u64 dumb_current_time(void)
{
    return ktime_get_ns()/NSEC_PER_USEC;
}


static void dumb_drain(struct sock *sk, u64 now)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    dumb->mode = DUMB_DRAIN;
    dumb->trans_time = now;

    dumb->bdp = dumb->max_rate*dumb->min_rtt/USEC_PER_SEC;
    dumb->bdp = max_t(u32, MIN_CWND, dumb->bdp);

    tp->snd_cwnd = drain_cwnd(dumb);
    tp->snd_ssthresh = tp->snd_cwnd;
}


void tcp_dumb_init(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct dumb *dumb = inet_csk_ca(sk);

    tp->snd_cwnd = MIN_CWND;
    tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;

    dumb->mode = DUMB_DRAIN;
    dumb->trans_time = dumb_current_time();

    dumb->bdp = MAX_TCP_WINDOW;

    dumb->inc_factor = 2;

    dumb->max_rate = 0;

    dumb->last_rtt = 1;
    dumb->min_rtt = RTT_INF;
    dumb->max_rtt = 0;
}
EXPORT_SYMBOL_GPL(tcp_dumb_init);


void tcp_dumb_release(struct sock *sk)
{
    struct dumb *dumb = inet_csk_ca(sk);
}
EXPORT_SYMBOL_GPL(tcp_dumb_release);


u32 tcp_dumb_ssthresh(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);

    return tp->snd_ssthresh;
}
EXPORT_SYMBOL_GPL(tcp_dumb_ssthresh);


u32 tcp_dumb_undo_cwnd(struct sock *sk)
{
    // TODO: Does this get called on ECN CE event?
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    u64 now = dumb_current_time();

    if ((dumb->mode == DUMB_GAIN_1 || dumb->mode == DUMB_GAIN_2)
        && dumb->inc_factor < MAX_INC_FACTOR) {
        dumb->inc_factor *= 2;

        dumb->mode = DUMB_RECOVER;
        dumb->trans_time = now;

        tp->snd_cwnd = dumb->bdp;
        tp->snd_ssthresh = dumb->bdp;
    } else if (tcp_in_slow_start(tp)) {
        dumb->bdp = max(MIN_CWND, dumb->bdp/2);

        dumb->mode = DUMB_RECOVER;
        dumb->trans_time = now;

        tp->snd_cwnd = dumb->bdp;
        tp->snd_ssthresh = dumb->bdp;
    }

    return tp->snd_cwnd;
}
EXPORT_SYMBOL_GPL(tcp_dumb_undo_cwnd);


void tcp_dumb_cong_control(struct sock *sk, const struct rate_sample *rs)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    u64 now = dumb_current_time();

    if (rs->rtt_us > 0) {
        if (dumb->mode == DUMB_GAIN_2 || tcp_in_slow_start(tp)) {
            u64 rate = rs->prior_in_flight*USEC_PER_SEC/rs->rtt_us;
            dumb->max_rate = max_t(u64, dumb->max_rate, rate);
        }

        dumb->last_rtt = rs->rtt_us;
        dumb->min_rtt = min_t(u32, dumb->min_rtt, rs->rtt_us);
        dumb->max_rtt = max_t(u32, dumb->max_rtt, rs->rtt_us);
    }


    if (tcp_in_slow_start(tp)) {
        if (now > dumb->trans_time + dumb->last_rtt) {
            u32 new_bdp = dumb->max_rate*dumb->min_rtt/USEC_PER_SEC;
            new_bdp = max_t(u32, MIN_CWND, new_bdp);

            dumb->trans_time = now;

            if (dumb->max_rtt > 3*dumb->min_rtt/2 || dumb->bdp == new_bdp) {
                dumb_drain(sk, now);
            } else {
                dumb->bdp = new_bdp;
                tp->snd_cwnd = gain_cwnd(dumb);
            }
        }
    } else if (dumb->mode == DUMB_DRAIN) {
        if (now > dumb->trans_time + DRAIN_RTTS*dumb->last_rtt) {
            dumb->mode = DUMB_STABLE;
            dumb->trans_time = now;

            dumb->bdp = dumb->max_rate*dumb->min_rtt/USEC_PER_SEC;
            dumb->bdp = max_t(u32, MIN_CWND, dumb->bdp);

            tp->snd_cwnd = dumb->bdp;
            tp->snd_ssthresh = dumb->bdp;

            dumb->inc_factor--;

            //printk(KERN_DEBUG "tcp_dumb: max_rate = %llu, "
            //       "min_rtt = %u, bdp = %u, gain_cwnd = %u\n",
            //       dumb->max_rate, dumb->min_rtt, dumb->bdp, tp->snd_cwnd);
        } else {
            dumb->bdp = dumb->max_rate*dumb->min_rtt/USEC_PER_SEC;
            dumb->bdp = max_t(u32, MIN_CWND, dumb->bdp);

            tp->snd_cwnd = drain_cwnd(dumb);
            tp->snd_ssthresh = tp->snd_cwnd;
        }
    } else if (dumb->mode == DUMB_RECOVER || dumb->mode == DUMB_STABLE) {
        unsigned long rtts = dumb->mode == DUMB_RECOVER ? REC_RTTS : STABLE_RTTS;

        if (now > dumb->trans_time + rtts*dumb->last_rtt) {
            dumb->mode = DUMB_GAIN_1;
            dumb->trans_time = now;

            tp->snd_cwnd = gain_cwnd(dumb);
        }
    } else if (dumb->mode == DUMB_GAIN_1) {
        if (now > dumb->trans_time + GAIN_1_RTTS*dumb->last_rtt) {
            dumb->mode = DUMB_GAIN_2;
            dumb->trans_time = now;

            dumb->max_rate = 0;

            dumb->min_rtt = RTT_INF;
            dumb->max_rtt = 0;
        }
    } else if (dumb->mode == DUMB_GAIN_2) {
        if (now > dumb->trans_time + GAIN_2_RTTS*dumb->last_rtt) {
            dumb_drain(sk, now);
        }
    } else {
        printk(KERN_ERR "tcp_dumb: Got to undefined mode %d at time %llu\n",
               dumb->mode, now);

        dumb_drain(sk, now);
    }

    tp->snd_cwnd = clamp_t(u32, tp->snd_cwnd, MIN_CWND, MAX_TCP_WINDOW);
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
