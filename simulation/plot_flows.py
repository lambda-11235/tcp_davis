#!/usr/bin/env python3

import argparse
import numpy as np
import pandas as pd

import matplotlib as mpl
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description="Plot")
parser.add_argument('data_file', type=str,
        help="The data file to plot from.")
parser.add_argument('--cwnd-limit', type=float, nargs=2,
        help="")
parser.add_argument('--rate-limit', type=float, nargs=2,
        help="")
parser.add_argument('--rtt-limit', type=float, nargs=2,
        help="")
args = parser.parse_args()

mpl.style.use('seaborn-bright')
mpl.rc('figure', dpi=200)


data = pd.read_csv(args.data_file)

flows = []
for i in range(max(data.flow_id) + 1):
    flows.append(data.loc[data.flow_id == i])


### CWND ###
fig = plt.figure()
ax = fig.add_subplot(111)

if args.cwnd_limit is not None:
    ax.set_ylim(args.cwnd_limit[0], args.cwnd_limit[1])

for i, flow in enumerate(flows):
    ax.plot(flow.time, flow.cwnd, label=f"Flow {i}")

ax.set_xlabel("Time (s)")
ax.set_ylabel("CWND (packets)")
ax.legend()


### Losses ###
fig = plt.figure()
ax = fig.add_subplot(111)

for i, flow in enumerate(flows):
    ax.plot(flow.time, flow.losses, label=f"Flow {i}")

ax.set_xlabel("Time (s)")
ax.set_ylabel("Losses (packets)")
ax.legend()


### Rate ###
fig = plt.figure()
ax = fig.add_subplot(111)

if args.rate_limit is not None:
    ax.set_ylim(args.rate_limit[0], args.rate_limit[1])

for i, flow in enumerate(flows):
    ax.plot(flow.time, flow.rate/2**17, label=f"Flow {i}")

ax.set_xlabel("Time (s)")
ax.set_ylabel("Rate (Mbps)")
ax.legend()


### RTT ###
fig = plt.figure()
ax = fig.add_subplot(111)

if args.rtt_limit is not None:
    ax.set_ylim(args.rtt_limit[0], args.rtt_limit[1])

for i, flow in enumerate(flows):
    ax.plot(flow.time, flow.rtt*1000, label=f"Flow {i}")

ax.set_xlabel("Time (s)")
ax.set_ylabel("RTT (ms)")
ax.legend()


### Mode ###
fig = plt.figure()
ax = fig.add_subplot(111)

for i, flow in enumerate(flows):
    ax.plot(flow.time, flow["mode"], label=f"Flow {i}")

ax.set_xlabel("Time (s)")
ax.set_ylabel("Mode")
ax.legend()


plt.show()
