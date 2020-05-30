#!/usr/bin/env python3

import argparse
import numpy as np
import pandas as pd

import matplotlib as mpl
import matplotlib.pyplot as plt

from common import calcRates


parser = argparse.ArgumentParser(description="Plot")
parser.add_argument('data_file', type=str,
        help="The data file to plot from.")
parser.add_argument('--flow', type=int, default=0,
        help="")
parser.add_argument('--cwnd-limit', type=float, nargs=2,
        help="")
parser.add_argument('--rate-limit', type=float, nargs=2,
        help="")
parser.add_argument('--rtt-limit', type=float, nargs=2,
        help="")
parser.add_argument('--rate-interval', type=float,
        help="")
args = parser.parse_args()

mpl.style.use('seaborn-bright')
mpl.rc('figure', dpi=200)


data = pd.read_csv(args.data_file)
data = data.loc[data.flow_id == args.flow]
time = data.time

rate = calcRates(time, data['bytes_sent'], interval=args.rate_interval)


### CWND ###
cwnd = data.cwnd
gain_cwnd = data.gain_cwnd
bdp = data.bdp

fig = plt.figure()
ax = fig.add_subplot(111)

if args.cwnd_limit is not None:
    ax.set_ylim(args.cwnd_limit[0], args.cwnd_limit[1])

ax.plot(time, cwnd, label="CWND", color="blue")
ax.plot(time, gain_cwnd, label="Gain CWND", color="green")
ax.plot(time, bdp, "--", label="BDP", color="red")

ax.set_xlabel("Time (s)")
ax.set_ylabel("CWND (packets)")
ax.legend()


### Losses ###
losses = data.losses

fig = plt.figure()
ax = fig.add_subplot(111)

ax.plot(time, losses, label="Losses", color="red")

ax.set_xlabel("Time (s)")
ax.set_ylabel("Losses (packets)")
ax.legend()


### Rate ###
rate = rate/2**17
pacingRate = data.pacing_rate/2**17

fig = plt.figure()
ax = fig.add_subplot(111)

if args.rate_limit is not None:
    ax.set_ylim(args.rate_limit[0], args.rate_limit[1])

ax.plot(time, rate, label="Rate", color="blue")
ax.plot(time, pacingRate, '--', label="Pacing Rate", color="red")

ax.set_xlabel("Time (s)")
ax.set_ylabel("Rate (Mbps)")
ax.legend()


### RTT ###
rtt = data.rtt*1000
minRTT = data.min_rtt*1000

fig = plt.figure()
ax = fig.add_subplot(111)

if args.rtt_limit is not None:
    ax.set_ylim(args.rtt_limit[0], args.rtt_limit[1])

ax.plot(time, rtt, label="RTT", color="blue")
ax.plot(time, minRTT, label="Min RTT", color="green")

ax.set_xlabel("Time (s)")
ax.set_ylabel("RTT (ms)")
ax.legend()


### Mode ###
mode = data['mode']

fig = plt.figure()
ax = fig.add_subplot(111)

ax.plot(time, mode, label="Mode", color="blue")

ax.set_xlabel("Time (s)")
ax.set_ylabel("Mode")
ax.legend()


plt.show()
