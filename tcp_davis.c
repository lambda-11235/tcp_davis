// SPDX-License-Identifier: GPL-2.0-only
/*
 * Davis congestion control
 *
 *
 * The core principal behind this algorithm's operation are the
 * equations
 *
 * RTT = max(1, (Inflight Bytes)/BDP)*(Min. RTT)
 * Throughput = (Inflight Bytes)/RTT
 *
 * Using the first equation we first estimate
 * BDP = (Inflight Bytes)*(Min. RTT)/RTT
 *
 * This allows us to set the snd_cwnd = BDP and
 * sk_pacing_rate = BDP/RTT, which is our optimal operating point.
 *
 * Under normal circumstances (i.e. no losses) TCP Davis runs through
 * four modes, with an additional RECOVER mode.
 *
 * STABLE: TODO
 *
 * DRAIN: TODO
 *
 * GAIN 1: TODO
 *
 * GAIN 2: TODO
 *
 * RECOVER: TODO
 *
 * The inc_factor is used to control for small buffers and the use of
 * AQM. If a loss occurs while in a gain cycle, then we assume this
 * loss was due to use overwhelming network buffers, and double the
 * inc_factor. Thus, the next time enter the gain cycle we will
 * increase the snd_cwnd by a slightly lower amount, hopefully so that
 * the buffers are not overwhelmed.
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/inet_diag.h>

#include <net/tcp.h>


#define DAVIS_PRNT "tcp_davis: "

static const u32 MIN_CWND = 4;

static const u32 REC_RTTS = 1;
static u32 STABLE_RTTS = 32;
static const u32 DRAIN_RTTS = 1;
static const u32 GAIN_1_RTTS = 2;
static const u32 GAIN_2_RTTS = 2;

static u32 MIN_INC_FACTOR = 2;
static u32 MAX_INC_FACTOR = 128;
static u32 SS_INC_FACTOR = 2;

static const u32 RTT_INF = U32_MAX;


// TODO: These parameters should be non-zero. Not sure if it's worth
// writing a custom parameter op for this.

// These are the parameters that really affect performance, and
// therefore should be tuneable. Making the other parameters
// configurable would mainly just be confusing and possibly result in
// bad behavior.
module_param(STABLE_RTTS, uint, 0644);
MODULE_PARM_DESC(STABLE_RTTS, "Number of RTTs to stay in STABLE mode for");

module_param(MIN_INC_FACTOR, uint, 0644);
MODULE_PARM_DESC(MIN_INC_FACTOR, "Maximum snd_cwnd gain = BDP/MIN_INC_FACTOR");
module_param(MAX_INC_FACTOR, uint, 0644);
MODULE_PARM_DESC(MAX_INC_FACTOR, "Minimum snd_cwnd gain = BDP/MAX_INC_FACTOR");
module_param(SS_INC_FACTOR, uint, 0644);
MODULE_PARM_DESC(SS_INC_FACTOR, "Slow-start snd_cwnd gain = BDP/SS_INC_FACTOR");


enum davis_mode { DAVIS_RECOVER, DAVIS_STABLE, DAVIS_DRAIN,
                 DAVIS_GAIN_1, DAVIS_GAIN_2 };

struct davis {
    enum davis_mode mode;
    u64 trans_time;

    u32 inc_factor;

    u32 bdp;
    u32 ss_last_bdp;

    u32 last_rtt;
    u32 min_rtt;
};


static u32 gain_cwnd(struct davis *davis)
{
    u32 cwnd;

    if (MIN_INC_FACTOR < 1) {
        MIN_INC_FACTOR = 1;

        printk(KERN_WARNING DAVIS_PRNT
               "MIN_INC_FACTOR must be greater than 0\n");
    }

    if (MAX_INC_FACTOR < MIN_INC_FACTOR) {
        MAX_INC_FACTOR = MIN_INC_FACTOR;

        printk(KERN_WARNING DAVIS_PRNT
               "MAX_INC_FACTOR must be greater than MIN_INC_FACTOR (%u)\n",
               MIN_INC_FACTOR);
    }

    davis->inc_factor = clamp_t(u32, davis->inc_factor, MIN_INC_FACTOR, MAX_INC_FACTOR);
    cwnd = (davis->inc_factor + 1)*davis->bdp/davis->inc_factor;

    return max_t(u32, davis->bdp + MIN_CWND, cwnd);
}


static u32 ss_cwnd(struct davis *davis)
{
    u32 cwnd;

    if (SS_INC_FACTOR < 1) {
        SS_INC_FACTOR = 1;

        printk(KERN_WARNING DAVIS_PRNT
               "SS_INC_FACTOR must be greater than 0\n");
    }

    cwnd = (SS_INC_FACTOR + 1)*davis->bdp/SS_INC_FACTOR;

    return max_t(u32, davis->bdp + MIN_CWND, cwnd);
}


static inline u64 rate_adj(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    return tp->mss_cache*USEC_PER_SEC;
}


static inline u64 davis_current_time(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    return div_u64(tp->tcp_clock_cache, NSEC_PER_USEC);
}




////////// Enter Routines START //////////
static void davis_enter_slow_start(struct sock *sk, u64 now)
{
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    davis->mode = DAVIS_GAIN_1;
    davis->trans_time = now;

    davis->bdp = MIN_CWND;
    davis->ss_last_bdp = 0;

    tp->snd_cwnd = MIN_CWND;

    davis->min_rtt = RTT_INF;
}

static void davis_enter_recovery(struct sock *sk, u64 now)
{
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    davis->mode = DAVIS_RECOVER;
    davis->trans_time = now;

    tp->snd_cwnd = davis->bdp;
    tp->snd_ssthresh = davis->bdp;
}


static void davis_enter_stable(struct sock *sk, u64 now)
{
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    davis->mode = DAVIS_STABLE;
    davis->trans_time = now;

    tp->snd_cwnd = davis->bdp;
    tp->snd_ssthresh = tp->snd_cwnd;

    davis->inc_factor--;

    davis->min_rtt = davis->last_rtt;

    //printk(KERN_DEBUG DAVIS_PRNT
    //       "min_rtt = %u, bdp = %u\n",
    //       davis->min_rtt, davis->bdp);
}


static void davis_enter_drain(struct sock *sk, u64 now)
{
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    davis->mode = DAVIS_DRAIN;
    davis->trans_time = now;

    tp->snd_cwnd = MIN_CWND;
    tp->snd_ssthresh = tp->snd_cwnd;
}


static void davis_enter_gain_1(struct sock *sk, u64 now)
{
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    davis->mode = DAVIS_GAIN_1;
    davis->trans_time = now;

    tp->snd_cwnd = gain_cwnd(davis);
}


static void davis_enter_gain_2(struct sock *sk, u64 now)
{
    struct davis *davis = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    davis->mode = DAVIS_GAIN_2;
    davis->trans_time = now;

    davis->bdp = 0;
}
////////// Enter Routines END //////////




void tcp_davis_init(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct davis *davis = inet_csk_ca(sk);

    davis->mode = DAVIS_RECOVER;
    davis->trans_time = davis_current_time(sk);

    tp->snd_cwnd = MIN_CWND;
    tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;

    davis->bdp = MIN_CWND;
    davis->ss_last_bdp = 0;

    davis->inc_factor = MIN_INC_FACTOR;

    sk->sk_pacing_rate = 0;

    davis->last_rtt = 1;
    davis->min_rtt = RTT_INF;
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
    bool react = davis->mode == DAVIS_GAIN_1 || davis->mode == DAVIS_GAIN_2;
    react = react && davis->inc_factor < MAX_INC_FACTOR;
    react = react || tcp_in_slow_start(tp);

    if (react) {
        davis->inc_factor *= 2;
        davis_enter_recovery(sk, now);
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
        }
    } else if (davis->mode == DAVIS_GAIN_2) {
        if (now > davis->trans_time + GAIN_2_RTTS*davis->last_rtt) {
            if (davis->bdp > davis->ss_last_bdp) {
                davis->mode = DAVIS_GAIN_1;
                davis->trans_time = now;

                tp->snd_cwnd = ss_cwnd(davis);

                davis->ss_last_bdp = davis->bdp;
            } else {
                davis_enter_recovery(sk, now);
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
        if (davis->mode == DAVIS_GAIN_2) {
            // Round up to be improve fairness. Flows with smaller
            // bandwidth shair tend to be more sensitive to
            // rounding. Notably, if we round down, then smaller flows
            // tend to not take bandwidth from larger ones. Rounding
            // up allows these smaller flows to compete with larger
            // ones.
            u32 est_bdp = rs->prior_in_flight*davis->min_rtt;
            est_bdp = DIV_ROUND_UP(est_bdp, rs->rtt_us);

            davis->bdp = max(davis->bdp, est_bdp);
        }

        davis->last_rtt = rs->rtt_us;
        davis->min_rtt = min_t(u32, davis->min_rtt, rs->rtt_us);
    }


    if (tcp_in_slow_start(tp)) {
        davis_slow_start(sk, now);
    } else if (davis->mode == DAVIS_RECOVER || davis->mode == DAVIS_STABLE) {
        unsigned long rtts = davis->mode == DAVIS_RECOVER ? REC_RTTS : STABLE_RTTS;

        if (now > davis->trans_time + rtts*davis->last_rtt) {
            davis_enter_drain(sk, now);
        }
    } else if (davis->mode == DAVIS_DRAIN) {
        if (now > davis->trans_time + DRAIN_RTTS*davis->last_rtt) {
            davis_enter_gain_1(sk, now);
        }
    } else if (davis->mode == DAVIS_GAIN_1) {
        if (now > davis->trans_time + GAIN_1_RTTS*davis->last_rtt) {
            davis_enter_gain_2(sk, now);
        }
    } else if (davis->mode == DAVIS_GAIN_2) {
        if (now > davis->trans_time + GAIN_2_RTTS*davis->last_rtt) {
            davis_enter_stable(sk, now);
        }
    } else {
        printk(KERN_ERR "tcp_davis: Got to undefined mode %d at time %llu\n",
               davis->mode, now);

        davis_enter_drain(sk, now);
    }

    tp->snd_cwnd = clamp_t(u32, tp->snd_cwnd, MIN_CWND, MAX_TCP_WINDOW);

    if (davis->last_rtt > 0)
        sk->sk_pacing_rate = tp->snd_cwnd*rate_adj(sk)/davis->last_rtt;
    else
        sk->sk_pacing_rate = 0;
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
