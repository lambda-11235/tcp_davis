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
//#define DAVIS_DEBUG

#define DAVIS_ONE 1024

static const u32 MIN_CWND = 4;

static u32 MIN_GAIN_CWND = 4;
static u32 REACTIVITY = DAVIS_ONE/8;
static u32 SENSITIVITY = DAVIS_ONE/64;

static const u32 DRAIN_RTTS = 2;
static u32 STABLE_RTTS_MIN = 3;
static u32 STABLE_RTTS_MAX = 6;
static const u32 GAIN_1_RTTS = 2;
static const u32 GAIN_2_RTTS = 1;

static const u32 RTT_INF = U32_MAX;
static u32 RTT_TIMEOUT_MS = 10*MSEC_PER_SEC;


// TODO: These parameters should be non-zero. Not sure if it's worth
// writing a custom parameter op for this.

// These are the parameters that really affect performance, and
// therefore should be tuneable. Making the other parameters
// configurable would mainly just be confusing and possibly result in
// bad behavior.

module_param(REACTIVITY, uint, 0644);
MODULE_PARM_DESC(REACTIVITY, "");

module_param(SENSITIVITY, uint, 0644);
MODULE_PARM_DESC(SENSITIVITY, "");

module_param(STABLE_RTTS_MIN, uint, 0644);
MODULE_PARM_DESC(STABLE_RTTS_MIN, "");

module_param(STABLE_RTTS_MAX, uint, 0644);
MODULE_PARM_DESC(STABLE_RTTS_MAX, "");

module_param(MIN_GAIN_CWND, uint, 0644);
MODULE_PARM_DESC(MIN_GAIN_CWND, "Minimum increase in snd_cwnd on each gain (packets)");

module_param(RTT_TIMEOUT_MS, uint, 0644);
MODULE_PARM_DESC(RTT_TIMEOUT_MS, "Timeout to probe for new RTT (milliseconds)");


enum davis_mode { DAVIS_DRAIN, DAVIS_STABLE, DAVIS_GAIN_1, DAVIS_GAIN_2 };

struct davis {
    enum davis_mode mode;
    u64 trans_time;
    u64 min_rtt_time;
    u64 delivered_start_time;

    u32 delivered_start;

    u32 bdp;
    u32 last_bdp;
    u32 gain_cwnd;

    u32 stable_rtts;

    u32 last_rtt;
    u32 min_rtt;

#ifdef DAVIS_DEBUG
    u64 last_debug_time;
#endif
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


static void davis_enter_slow_start(struct sock *sk, u64 now)
{
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    davis->mode = DAVIS_GAIN_1;
    davis->trans_time = now;

    davis->bdp = MIN_CWND;
    davis->last_bdp = 0;

    tp->snd_cwnd = MIN_CWND;

    davis->min_rtt = davis->last_rtt;
}


static void update_gain_cwnd(struct sock *sk)
{
    // Gain cwnd = REACTIVITY  * BDP    given unlimited growth
    // Gain cwnd = SENSITIVITY * BDP    when stable
    // See Lucas sequence.
    //
    // Essentially, start by solving the long term behavior of
    // snd_cwnd[0] = w
    // snd_cwnd[1] = w
    // snd_cwnd[n] = (1 + alpha)*snd_cwnd[n-1] + beta*snd_cwnd[n-2]
    //
    // You will get something like
    // snd_cwnd[n] = C*x^n + D*y^n
    //
    // It is the case that C*x^n + D*y^n = O(max(x,y)^n)
    // So, we can find alpha and beta by solving
    // max(x, y) = REACTIVITY
    // (1 + SENSITIVITY)*snd_cwnd = (1 + alpha)*snd_cwnd + beta*snd_cwnd
    //
    // Hope this helps any unfortunate readers (possibly future me)
    // who are trying to puzzle this out. :)

    struct davis *davis = inet_csk_ca(sk);
    s32 gain;
    s32 alpha, beta;

    if (REACTIVITY <= SENSITIVITY) {
        printk(KERN_ERR DAVIS_PRNT
               "bad reactivity value (%d), must be > %d\n",
                REACTIVITY, SENSITIVITY);

        REACTIVITY = SENSITIVITY + 1;
    }

    alpha = DAVIS_ONE + REACTIVITY - SENSITIVITY*DAVIS_ONE/REACTIVITY;
    beta = SENSITIVITY - alpha;

    gain = alpha*davis->bdp + beta*davis->last_bdp;
    gain = max_t(s32, gain, SENSITIVITY*davis->bdp);
    gain = max_t(s32, gain, MIN_GAIN_CWND*DAVIS_ONE);

    davis->gain_cwnd = gain/DAVIS_ONE;
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
    davis->last_bdp = 0;
    davis->gain_cwnd = MIN_GAIN_CWND;

    davis->stable_rtts = STABLE_RTTS_MIN;

    sk->sk_pacing_rate = 0;

    davis->last_rtt = 0;
    davis->min_rtt = RTT_INF;
    davis->min_rtt_time = now;

#ifdef DAVIS_DEBUG
    davis->last_debug_time = now;
#endif
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
        davis->mode = DAVIS_DRAIN;
        davis->trans_time = now;

        tp->snd_cwnd = MIN_CWND;
        tp->snd_ssthresh = MIN_CWND;
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

            if (davis->bdp > davis->last_bdp) {
                davis->mode = DAVIS_GAIN_1;
                davis->trans_time = now;

                tp->snd_cwnd = 3*davis->bdp/2;

                davis->last_bdp = davis->bdp;
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
    s32 rtt;

    // NOTE: This is a hack. rs->rtt_us is preffered because it will
    // always give the minimum RTT. However it only works well when
    // the RTT is primarily composed of propogation and queueing
    // delays. For very small RTTs the biggest contributer is
    // proceesing delay. This results in min RTTs far smaller than the
    // average base RTT, hurting throughput. Using a smoothed RTT
    // helps avoid these issues.
    //
    // 1 jiffie is choosen because I thought it would be a fair
    // approximation of the order of noise added by kernel processing.
    if (rs->rtt_us > jiffies_to_usecs(1))
        rtt = rs->rtt_us;
    else
        rtt = tp->srtt_us;

    if (rtt > 0) {
        davis->last_rtt = rtt;

        if (rtt < davis->min_rtt) {
            davis->min_rtt = rtt;
            davis->min_rtt_time = now;
        }
    }


    if (tcp_in_slow_start(tp)) {
        davis_slow_start(sk, now);
    } else if (davis->mode == DAVIS_DRAIN) {
        if (now > davis->trans_time + DRAIN_RTTS*davis->last_rtt) {
            davis->mode = DAVIS_STABLE;
            davis->trans_time = now;

            tp->snd_cwnd = davis->bdp;
        }
    } else if (davis->mode == DAVIS_STABLE) {
        if (now > davis->trans_time + davis->stable_rtts*davis->last_rtt) {
            davis->mode = DAVIS_GAIN_1;
            davis->trans_time = now;

            tp->snd_cwnd = davis->bdp + davis->gain_cwnd;
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

            davis->last_bdp = davis->bdp;
            if (interval > 0)
                davis->bdp = DIV_ROUND_UP(diff_deliv*davis->min_rtt,
                                          interval);

            update_gain_cwnd(sk);


#ifdef DAVIS_DEBUG
            if (now > davis->last_debug_time + 250*USEC_PER_MSEC) {
                davis->last_debug_time = now;

                printk(KERN_DEBUG DAVIS_PRNT
                       "bdp = %u, gain_cwnd = %u, min_rtt = %u, stable_rtts = %u\n",
                       davis->bdp, davis->gain_cwnd, davis->min_rtt,
                       davis->stable_rtts);
            }
#endif

            if (now > davis->min_rtt_time + RTT_TIMEOUT_MS*USEC_PER_MSEC) {
                davis->mode = DAVIS_DRAIN;
                davis->trans_time = now;

                tp->snd_cwnd = MIN_CWND;
                davis->min_rtt = davis->last_rtt;
                davis->min_rtt_time = now;
            } else {
                u32 rtt_diff = STABLE_RTTS_MAX - STABLE_RTTS_MIN;

                davis->mode = DAVIS_STABLE;
                davis->trans_time = now;

                davis->stable_rtts = STABLE_RTTS_MIN;
                davis->stable_rtts += prandom_u32_max(rtt_diff + 1);

                tp->snd_cwnd = davis->bdp;
            }
        }
    } else {
        printk(KERN_ERR DAVIS_PRNT
               "Got to undefined mode %d at time %llu\n",
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
