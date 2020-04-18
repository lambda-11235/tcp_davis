// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dumb congestion control
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
 * Under normal circumstances (i.e. no losses) TCP Dumb runs through
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


enum dumb_mode { DUMB_RECOVER, DUMB_STABLE, DUMB_DRAIN,
                 DUMB_GAIN_1, DUMB_GAIN_2 };

struct dumb {
    enum dumb_mode mode;
    u64 trans_time;

    u32 inc_factor;

    u32 bdp;
    u32 ss_last_bdp;

    u32 last_rtt;
    u32 min_rtt;
};


static inline u32 gain_cwnd(struct dumb *dumb)
{
    u32 cwnd = (dumb->inc_factor + 1)*dumb->bdp/dumb->inc_factor;
    return max_t(u32, dumb->bdp + MIN_CWND, cwnd);
}


static inline u32 ss_cwnd(struct dumb *dumb)
{
    u32 cwnd = (SS_INC_FACTOR + 1)*dumb->bdp/SS_INC_FACTOR;
    return max_t(u32, dumb->bdp + MIN_CWND, cwnd);
}


static inline u64 rate_adj(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    return tp->mss_cache*USEC_PER_SEC;
}


static inline u64 dumb_current_time(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    return div_u64(tp->tcp_clock_cache, NSEC_PER_USEC);
}




////////// Enter Routines START //////////
static void dumb_enter_slow_start(struct sock *sk, u64 now)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    dumb->mode = DUMB_GAIN_1;
    dumb->trans_time = now;

    dumb->bdp = MIN_CWND;
    dumb->ss_last_bdp = 0;
    
    tp->snd_cwnd = MIN_CWND;

    dumb->min_rtt = RTT_INF;
}

static void dumb_enter_recovery(struct sock *sk, u64 now)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    dumb->mode = DUMB_RECOVER;
    dumb->trans_time = now;

    tp->snd_cwnd = dumb->bdp;
    tp->snd_ssthresh = dumb->bdp;
}


static void dumb_enter_stable(struct sock *sk, u64 now)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    dumb->mode = DUMB_STABLE;
    dumb->trans_time = now;

    tp->snd_cwnd = dumb->bdp;
    tp->snd_ssthresh = tp->snd_cwnd;

    dumb->inc_factor--;
    dumb->inc_factor = max_t(u32, dumb->inc_factor, MIN_INC_FACTOR);

    dumb->min_rtt = dumb->last_rtt;

    //printk(KERN_DEBUG "tcp_dumb: max_rate = %llu, "
    //       "min_rtt = %u, bdp = %u, gain_cwnd = %u\n",
    //       dumb->max_rate, dumb->min_rtt, dumb->bdp, tp->snd_cwnd);
}


static void dumb_enter_drain(struct sock *sk, u64 now)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    dumb->mode = DUMB_DRAIN;
    dumb->trans_time = now;

    tp->snd_cwnd = MIN_CWND;
    tp->snd_ssthresh = tp->snd_cwnd;
}


static void dumb_enter_gain_1(struct sock *sk, u64 now)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    dumb->mode = DUMB_GAIN_1;
    dumb->trans_time = now;

    tp->snd_cwnd = gain_cwnd(dumb);
}


static void dumb_enter_gain_2(struct sock *sk, u64 now)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    dumb->mode = DUMB_GAIN_2;
    dumb->trans_time = now;

    dumb->bdp = 0;
}
////////// Enter Routines END //////////




void tcp_dumb_init(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct dumb *dumb = inet_csk_ca(sk);

    dumb->mode = DUMB_RECOVER;
    dumb->trans_time = dumb_current_time(sk);

    tp->snd_cwnd = MIN_CWND;
    tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;

    dumb->bdp = MIN_CWND;
    dumb->ss_last_bdp = 0;

    dumb->inc_factor = MIN_INC_FACTOR;

    sk->sk_pacing_rate = 0;

    dumb->last_rtt = 1;
    dumb->min_rtt = RTT_INF;
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


void tcp_dumb_cwnd_event(struct sock *sk, enum tcp_ca_event ev)
{
    u64 now = dumb_current_time(sk);

    if (ev == CA_EVENT_CWND_RESTART)
        dumb_enter_slow_start(sk, now);
}


u32 tcp_dumb_undo_cwnd(struct sock *sk)
{
    // TODO: Does this get called on ECN CE event?
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    u64 now = dumb_current_time(sk);
    bool react = dumb->mode == DUMB_GAIN_1 || dumb->mode == DUMB_GAIN_2;
    react = react && dumb->inc_factor < MAX_INC_FACTOR;
    react = react || tcp_in_slow_start(tp);

    if (react) {
        dumb->inc_factor *= 2;
        dumb->inc_factor = min_t(u32, dumb->inc_factor, MAX_INC_FACTOR);

        dumb_enter_recovery(sk, now);
    }

    return tp->snd_cwnd;
}
EXPORT_SYMBOL_GPL(tcp_dumb_undo_cwnd);


static void dumb_slow_start(struct sock *sk, u64 now)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    if (dumb->mode == DUMB_GAIN_1) {
        if (now > dumb->trans_time + GAIN_1_RTTS*dumb->last_rtt) {
            dumb->mode = DUMB_GAIN_2;
            dumb->trans_time = now;
        }
    } else if (dumb->mode == DUMB_GAIN_2) {
        if (now > dumb->trans_time + GAIN_2_RTTS*dumb->last_rtt) {
            if (dumb->bdp > dumb->ss_last_bdp) {
                dumb->mode = DUMB_GAIN_1;
                dumb->trans_time = now;

                tp->snd_cwnd = ss_cwnd(dumb);

                dumb->ss_last_bdp = dumb->bdp;
            } else {
                dumb_enter_recovery(sk, now);
            }
        }
    } else {
        dumb_enter_slow_start(sk, now);
    }
}


void tcp_dumb_cong_control(struct sock *sk, const struct rate_sample *rs)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);
    u64 now = dumb_current_time(sk);

    if (rs->rtt_us > 0) {
        if (dumb->mode == DUMB_GAIN_2) {
            // Here we are essentially assigning the BDP are median
            // estimate. This is because we only enter a steady-state
            // estimation when we get an equal amount of over and
            // under estimates.
            u32 est_bdp = rs->prior_in_flight*dumb->min_rtt/rs->rtt_us;

            if (dumb->bdp == 0)
                dumb->bdp = est_bdp;
            else if (dumb->bdp < est_bdp)
                dumb->bdp++;
            else
                dumb->bdp--;
        }

        dumb->last_rtt = rs->rtt_us;
        dumb->min_rtt = min_t(u32, dumb->min_rtt, rs->rtt_us);
    }


    if (tcp_in_slow_start(tp)) {
        dumb_slow_start(sk, now);
    } else if (dumb->mode == DUMB_RECOVER || dumb->mode == DUMB_STABLE) {
        unsigned long rtts = dumb->mode == DUMB_RECOVER ? REC_RTTS : STABLE_RTTS;

        if (now > dumb->trans_time + rtts*dumb->last_rtt) {
            dumb_enter_drain(sk, now);
        }
    } else if (dumb->mode == DUMB_DRAIN) {
        if (now > dumb->trans_time + DRAIN_RTTS*dumb->last_rtt) {
            dumb_enter_gain_1(sk, now);
        }
    } else if (dumb->mode == DUMB_GAIN_1) {
        if (now > dumb->trans_time + GAIN_1_RTTS*dumb->last_rtt) {
            dumb_enter_gain_2(sk, now);
        }
    } else if (dumb->mode == DUMB_GAIN_2) {
        if (now > dumb->trans_time + GAIN_2_RTTS*dumb->last_rtt) {
            dumb_enter_stable(sk, now);
        }
    } else {
        printk(KERN_ERR "tcp_dumb: Got to undefined mode %d at time %llu\n",
               dumb->mode, now);

        dumb_enter_drain(sk, now);
    }

    tp->snd_cwnd = clamp_t(u32, tp->snd_cwnd, MIN_CWND, MAX_TCP_WINDOW);

    // If we're in a GAIN mode don't limit throughput, otherwise pace
    // at the predicted throughput.
    if (dumb->mode == DUMB_GAIN_1 || dumb->mode == DUMB_GAIN_2)
        sk->sk_pacing_rate = 0;
    else
        sk->sk_pacing_rate = ((u64) tp->snd_cwnd)*tp->mss_cache*USEC_PER_SEC/dumb->last_rtt;
}
EXPORT_SYMBOL_GPL(tcp_dumb_cong_control);


static struct tcp_congestion_ops tcp_dumb __read_mostly = {
    // FIXME: Remove TCP_CONG_NON_RESTRICTED before mainline into kernel.
    .flags        = TCP_CONG_NON_RESTRICTED,
    .init         = tcp_dumb_init,
    .release      = tcp_dumb_release,
    .ssthresh     = tcp_dumb_ssthresh,
    .undo_cwnd    = tcp_dumb_undo_cwnd,
    .cong_control = tcp_dumb_cong_control,

    .owner        = THIS_MODULE,
    .name         = "dumb",
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
