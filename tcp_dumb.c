// SPDX-License-Identifier: GPL-2.0-only
/*
 * Dumb congestion control
 *
 *
 * The core principal behind this algorithms operation is the equation
 * RTT = (Inflight Bytes)*(Min. RTT)/BDP
 *
 * Using this equation we first estimate
 * Rate = (Inflight Bytes)/RTT
 * And then we estimate
 * BDP = (Max. Rate)*(Min. RTT)
 *
 * This allows us to set the snd_cwnd = BDP, which is our optimal operating point.
 *
 * Under normal circumstances (i.e. no losses) TCP Dumb runs through
 * four modes, with an additional RECOVER mode.
 *
 * STABLE: We keep the snd_cwnd set to our estimate of the BDP.  We
 * also set our sk_pacing_rate = Max. Rate, so that buffers will not
 * be overwhelmed by a flood of packets, which could otherwise result
 * in losses.
 *
 * GAIN 1: We set snd_cwnd to BDP*(1 + 1/inc_factor), and wait for the
 * buffers to fill. We also double the pacing rate to allow the
 * snd_cwnd to grow.
 *
 * GAIN 2: After the buffers have been saturated we switch to the
 * second gain mode. This mode is like the first, except that we
 * estimate the maximum rate in **this mode only**. This is important
 * because this is the only mode in which we know that the pipe is
 * saturated. This will thus be the only time the RTT truly reflects
 * congestion.
 *
 * DRAIN: Now we set the snd_cwnd to its minimum value in order
 * to estimate the minimum RTT. After this is done we set our BDP
 * estimate to (max rate measured)*(min. RTT).
 *
 * RECOVER: This is a shorter version of the STABLE mode. It is
 * entered when a loss occurs in slow start or during a GAIN cycle.
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
static const u32 GAIN_1_RTTS = 2;
static const u32 GAIN_2_RTTS = 1;
static const u32 DRAIN_RTTS = 1;

static u32 MIN_INC_FACTOR = 2;
static u32 MAX_INC_FACTOR = 128;

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
MODULE_PARM_DESC(MAX_INC_FACTOR, "Minimum snd_cwnd gain = BDP/MIN_INC_FACTOR");


enum dumb_mode { DUMB_RECOVER, DUMB_STABLE,
                 DUMB_GAIN_1, DUMB_GAIN_2,
                 DUMB_DRAIN };

struct dumb {
    enum dumb_mode mode;
    u64 trans_time;

    // Rates are in bytes/second
    u64 pacing_rate;
    u64 max_rate;

    u32 inc_factor;

    u32 last_rtt;
    u32 min_rtt, max_rtt;

    u32 bdp;
};


static inline u32 gain_cwnd(struct dumb *dumb)
{
    u32 cwnd = (dumb->inc_factor + 1)*dumb->bdp/dumb->inc_factor;
    return max_t(u32, dumb->bdp + MIN_CWND, cwnd);
}


static inline u64 rate_adj(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    return tp->mss_cache*USEC_PER_SEC;
}


static inline u64 dumb_current_time(void)
{
    return ktime_get_ns()/NSEC_PER_USEC;
}




////////// Enter Routines START //////////
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

    dumb->bdp = dumb->max_rate*dumb->min_rtt/rate_adj(sk);
    dumb->bdp = max_t(u32, MIN_CWND, dumb->bdp);

    tp->snd_cwnd = dumb->bdp;
    tp->snd_ssthresh = dumb->bdp;

    sk->sk_pacing_rate = dumb->max_rate;

    dumb->inc_factor--;
    dumb->inc_factor = max_t(u32, dumb->inc_factor, MIN_INC_FACTOR);

    //printk(KERN_DEBUG "tcp_dumb: max_rate = %llu, "
    //       "min_rtt = %u, bdp = %u, gain_cwnd = %u\n",
    //       dumb->max_rate, dumb->min_rtt, dumb->bdp, tp->snd_cwnd);
}


static void dumb_enter_gain_1(struct sock *sk, u64 now)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    dumb->mode = DUMB_GAIN_1;
    dumb->trans_time = now;

    tp->snd_cwnd = gain_cwnd(dumb);
    sk->sk_pacing_rate *= 2;
}


static void dumb_enter_gain_2(struct sock *sk, u64 now)
{
    struct dumb *dumb = inet_csk_ca(sk);
    struct tcp_sock *tp = tcp_sk(sk);

    dumb->mode = DUMB_GAIN_2;
    dumb->trans_time = now;

    dumb->max_rate = 0;

    dumb->min_rtt = RTT_INF;
    dumb->max_rtt = 0;
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
////////// Enter Routines END //////////




void tcp_dumb_init(struct sock *sk)
{
    struct tcp_sock *tp = tcp_sk(sk);
    struct dumb *dumb = inet_csk_ca(sk);

    tp->snd_cwnd = MIN_CWND;
    tp->snd_ssthresh = TCP_INFINITE_SSTHRESH;

    dumb->mode = DUMB_RECOVER;
    dumb->trans_time = dumb_current_time();

    dumb->bdp = MIN_CWND;

    dumb->inc_factor = MIN_INC_FACTOR;

    sk->sk_pacing_rate = 0;
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
        dumb->inc_factor = min_t(u32, dumb->inc_factor, MAX_INC_FACTOR);

        dumb_enter_recovery(sk, now);
    } else if (tcp_in_slow_start(tp)) {
        dumb->bdp = max(MIN_CWND, dumb->bdp/2);
        dumb_enter_recovery(sk, now);
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
            u64 rate = rs->prior_in_flight*rate_adj(sk)/rs->rtt_us;
            dumb->max_rate = max_t(u64, dumb->max_rate, rate);
        }

        dumb->last_rtt = rs->rtt_us;
        dumb->min_rtt = min_t(u32, dumb->min_rtt, rs->rtt_us);
        dumb->max_rtt = max_t(u32, dumb->max_rtt, rs->rtt_us);
    }


    if (tcp_in_slow_start(tp)) {
        if (now > dumb->trans_time + dumb->last_rtt) {
            u32 new_bdp = dumb->max_rate*dumb->min_rtt/rate_adj(sk);
            new_bdp = max_t(u32, MIN_CWND, new_bdp);

            dumb->trans_time = now;

            if (dumb->max_rtt > 3*dumb->min_rtt/2) {
                dumb->bdp = max(MIN_CWND, dumb->bdp/2);
                dumb_enter_recovery(sk, now);
            } else {
                dumb->bdp = new_bdp;
                tp->snd_cwnd = gain_cwnd(dumb);
            }
        }
    } else if (dumb->mode == DUMB_RECOVER || dumb->mode == DUMB_STABLE) {
        unsigned long rtts = dumb->mode == DUMB_RECOVER ? REC_RTTS : STABLE_RTTS;

        if (now > dumb->trans_time + rtts*dumb->last_rtt) {
            dumb_enter_gain_1(sk, now);
        }
    } else if (dumb->mode == DUMB_GAIN_1) {
        if (now > dumb->trans_time + GAIN_1_RTTS*dumb->last_rtt) {
            dumb_enter_gain_2(sk, now);
        }
    } else if (dumb->mode == DUMB_GAIN_2) {
        if (now > dumb->trans_time + GAIN_2_RTTS*dumb->last_rtt) {
            dumb_enter_drain(sk, now);
        }
    } else if (dumb->mode == DUMB_DRAIN) {
        if (now > dumb->trans_time + DRAIN_RTTS*dumb->last_rtt) {
            dumb_enter_stable(sk, now);
        }
    } else {
        printk(KERN_ERR "tcp_dumb: Got to undefined mode %d at time %llu\n",
               dumb->mode, now);

        dumb_enter_drain(sk, now);
    }

    tp->snd_cwnd = clamp_t(u32, tp->snd_cwnd, MIN_CWND, MAX_TCP_WINDOW);
}
EXPORT_SYMBOL_GPL(tcp_dumb_cong_control);


static struct tcp_congestion_ops tcp_dumb __read_mostly = {
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
