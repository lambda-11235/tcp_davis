// SPDX-License-Identifier: GPL-2.0-only
/*
 * Davis congestion control
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>


#define DAVIS_PRNT "tcp_davis: "

static const u32 MIN_CWND = 4;

static u32 MIN_GAIN_CWND = 4;
static u32 MAX_GAIN_FACTOR = 16;
static u32 GAIN_RATE = 1048576;

static const u32 DRAIN_RTTS = 2;
static const u32 GAIN_1_RTTS = 1;
static const u32 GAIN_2_RTTS = 1;

static const u32 RTT_INF = U32_MAX;
static u32 RTT_TIMEOUT_MS = 10*MSEC_PER_SEC;


// TODO: These parameters should be non-zero. Not sure if it's worth
// writing a custom parameter op for this.

// These are the parameters that really affect performance, and
// therefore should be tuneable. Making the other parameters
// configurable would mainly just be confusing and possibly result in
// bad behavior.
module_param(MIN_GAIN_CWND, uint, 0644);
MODULE_PARM_DESC(MIN_GAIN_CWND, "Minimum increase in snd_cwnd on each gain (packets)");

module_param(MAX_GAIN_FACTOR, uint, 0644);
MODULE_PARM_DESC(MAX_GAIN_FACTOR, "Maximum increase in snd_cwnd is BDP/MAX_GAIN_FACTOR");

module_param(GAIN_RATE, uint, 0644);
MODULE_PARM_DESC(GAIN_RATE, "Amount to increase rate on each gain (bytes/s)");

module_param(RTT_TIMEOUT_MS, uint, 0644);
MODULE_PARM_DESC(RTT_TIMEOUT_MS, "Timeout to probe for new RTT (milliseconds)");


enum davis_mode { DAVIS_DRAIN, DAVIS_GAIN_1, DAVIS_GAIN_2 };

struct davis {
    enum davis_mode mode;
    u64 trans_time;
    u64 min_rtt_time;
    u64 delivered_start_time;

    u32 delivered_start;

    u32 bdp;
    u32 ss_last_bdp;

    u32 last_rtt;
    u32 min_rtt;
};


static inline u64 davis_current_time(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    return div_u64(tp->tcp_clock_cache, NSEC_PER_USEC);
}


static inline u64 rate_adj(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    return tp->mss_cache*USEC_PER_SEC;
}


static u32 gain_cwnd(struct sock *sk)
{
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    u64 gain = div64_u64(((u64) GAIN_RATE)*davis->min_rtt, rate_adj(sk));

    // NOTE: Clamp is not used since we cannot gaurantee
    // MIN_GAIN_CWND < bdp/MAX_GAIN_FACTOR
    gain = min_t(u32, gain, davis->bdp/MAX_GAIN_FACTOR);
    gain = max_t(u32, gain, MIN_GAIN_CWND);

    return gain;
}


static void davis_enter_slow_start(struct sock *sk, u64 now)
{
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    davis->mode = DAVIS_GAIN_1;
    davis->trans_time = now;

    davis->bdp = MIN_CWND;
    davis->ss_last_bdp = 0;

    tp->snd_cwnd = MIN_CWND;

    davis->min_rtt = davis->last_rtt;
}


void tcp_davis_init(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct davis *davis = inet_csk_ca(sk);
    u64 now = davis_current_time(sk);

    davis->mode = DAVIS_GAIN_1;
    davis->trans_time = now;

    tp->snd_cwnd = MIN_CWND;
    tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;

    davis->delivered_start = tp->delivered;
    davis->delivered_start_time = tp->delivered_mstamp;

    davis->bdp = MIN_CWND;
    davis->ss_last_bdp = 0;

    sk->sk_pacing_rate = 0;

    davis->last_rtt = 0;
    davis->min_rtt = RTT_INF;
    davis->min_rtt_time = now;
}
EXPORT_SYMBOL_GPL(tcp_davis_init);


void tcp_davis_release(struct sock *sk)
{
    struct davis *davis = inet_csk_ca(sk);
}
EXPORT_SYMBOL_GPL(tcp_davis_release);


u32 tcp_davis_ssthresh(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);

    return tp->snd_ssthresh;
}
EXPORT_SYMBOL_GPL(tcp_davis_ssthresh);


void tcp_davis_cwnd_event(struct sock *sk, enum tcp_ca_event ev)
{
    u64 now = davis_current_time(sk);

    if (ev == CA_EVENT_CWND_RESTART)
        davis_enter_slow_start(sk, now);
}


u32 tcp_davis_undo_cwnd(struct sock *sk)
{
    // TODO: Does this get called on ECN CE event?
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    u64 now = davis_current_time(sk);

    if (tcp_in_slow_start(tp)) {
        davis->mode = DAVIS_GAIN_1;
        davis->trans_time = now;

        tp->snd_cwnd = davis->bdp + gain_cwnd(sk);
    }

    return tp->snd_cwnd;
}
EXPORT_SYMBOL_GPL(tcp_davis_undo_cwnd);


static void davis_slow_start(struct sock *sk, u64 now)
{
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    if (davis->mode == DAVIS_GAIN_1) {
        if (now > davis->trans_time + GAIN_1_RTTS*davis->last_rtt) {
            davis->mode = DAVIS_GAIN_2;
            davis->trans_time = now;

            davis->delivered_start = tp->delivered;
            davis->delivered_start_time = tp->delivered_mstamp;
        }
    } else if (davis->mode == DAVIS_GAIN_2) {
        if (now > davis->trans_time + GAIN_2_RTTS*davis->last_rtt) {
            u32 diff_deliv = tp->delivered - davis->delivered_start;
            u32 interval = tp->delivered_mstamp - davis->delivered_start_time;

            if (interval > 0)
                davis->bdp = DIV_ROUND_UP(diff_deliv*davis->min_rtt,
                                          interval);

            if (davis->bdp > davis->ss_last_bdp) {
                davis->mode = DAVIS_GAIN_1;
                davis->trans_time = now;

                tp->snd_cwnd = 3*davis->bdp/2;

                davis->ss_last_bdp = davis->bdp;
            } else {
                davis->mode = DAVIS_DRAIN;
                davis->trans_time = now;

                tp->snd_cwnd = MIN_CWND;
                tp->snd_ssthresh = MIN_CWND;
            }
        }
    } else {
        davis_enter_slow_start(sk, now);
    }
}


void tcp_davis_cong_control(struct sock *sk, const struct rate_sample *rs)
{
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    u64 now = davis_current_time(sk);

    if (rs->rtt_us > 0) {
        davis->last_rtt = rs->rtt_us;

        if (rs->rtt_us < davis->min_rtt) {
            davis->min_rtt = rs->rtt_us;
            davis->min_rtt_time = now;
        }
    }


    if (tcp_in_slow_start(tp)) {
        davis_slow_start(sk, now);
    } else if (davis->mode == DAVIS_DRAIN) {
        if (now > davis->trans_time + DRAIN_RTTS*davis->last_rtt) {
            davis->mode = DAVIS_GAIN_1;
            davis->trans_time = now;

            tp->snd_cwnd = davis->bdp + gain_cwnd(sk);
        }
    } else if (davis->mode == DAVIS_GAIN_1) {
        if (now > davis->trans_time + GAIN_1_RTTS*davis->last_rtt) {
            davis->mode = DAVIS_GAIN_2;
            davis->trans_time = now;

            davis->delivered_start = tp->delivered;
            davis->delivered_start_time = tp->delivered_mstamp;
        }
    } else if (davis->mode == DAVIS_GAIN_2) {
        if (now > davis->trans_time + GAIN_2_RTTS*davis->last_rtt) {
            u32 diff_deliv = tp->delivered - davis->delivered_start;
            u32 interval = tp->delivered_mstamp - davis->delivered_start_time;

            if (interval > 0)
                davis->bdp = DIV_ROUND_UP(diff_deliv*davis->min_rtt,
                                          interval);

            if (now > davis->min_rtt_time + RTT_TIMEOUT_MS*USEC_PER_MSEC) {
                davis->mode = DAVIS_DRAIN;
                davis->trans_time = now;

                tp->snd_cwnd = MIN_CWND;
                davis->min_rtt = davis->last_rtt;
                davis->min_rtt_time = now;
            } else {
                davis->mode = DAVIS_GAIN_1;
                davis->trans_time = now;

                tp->snd_cwnd = davis->bdp + gain_cwnd(sk);
            }
        }
    } else {
        printk(KERN_ERR "tcp_davis: Got to undefined mode %d at time %llu\n",
               davis->mode, now);

        davis->mode = DAVIS_DRAIN;
        davis->trans_time = now;

        tp->snd_cwnd = MIN_CWND;
    }

    tp->snd_cwnd = clamp_t(u32, tp->snd_cwnd, MIN_CWND, MAX_TCP_WINDOW);
}
EXPORT_SYMBOL_GPL(tcp_davis_cong_control);


static struct tcp_congestion_ops tcp_davis __read_mostly = {
    // FIXME: Remove TCP_CONG_NON_RESTRICTED before mainline into kernel.
    .flags        = TCP_CONG_NON_RESTRICTED,
    .init         = tcp_davis_init,
    .release      = tcp_davis_release,
    .ssthresh     = tcp_davis_ssthresh,
    .undo_cwnd    = tcp_davis_undo_cwnd,
    .cong_control = tcp_davis_cong_control,

    .owner        = THIS_MODULE,
    .name         = "davis",
};

static int __init tcp_davis_register(void)
{
    BUILD_BUG_ON(sizeof(struct davis) > ICSK_CA_PRIV_SIZE);
    tcp_register_congestion_control(&tcp_davis);
    return 0;
}

static void __exit tcp_davis_unregister(void)
{
    tcp_unregister_congestion_control(&tcp_davis);
}

module_init(tcp_davis_register);
module_exit(tcp_davis_unregister);

MODULE_AUTHOR("Taran Lynn");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TCP Davis");
