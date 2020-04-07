#!/usr/bin/env python3

import argparse
import matplotlib

import matplotlib as mpl
import matplotlib.pyplot as plt

from data import Data

bwctlStartTime = 0

parser = argparse.ArgumentParser(description="Plots test results.")
parser.add_argument('test', type=str,
        help="The name of the test to run.")
parser.add_argument('-o', '--output', type=str,
        help="Sets output file base name.")
parser.add_argument('-e', '--output-extension', type=str, default="pdf",
        help="Sets the extension for output files (i.e. pdf, png, etc.).")
parser.add_argument('-s', '--stream', type=int, default=0,
        help="Select the iperf stream to use.")
args = parser.parse_args()


data = Data(args.test)
stream = data.streams[args.stream]

stream.sort_values('start', inplace=True)

mpl.style.use('seaborn-bright')
mpl.rc('figure', dpi=200)


### Rate ###
stream.bits_per_second /= 2**20

fig = plt.figure()
ax = fig.add_subplot(111)

ax.plot('start', 'bits_per_second', data=stream, label="Observed Rate", color="blue")

ax.set_xlabel("Time (s)")
ax.set_ylabel("Rate (Mbps)")
ax.legend()


### cwnd ###
stream.snd_cwnd /= 2**20

fig = plt.figure()
ax = fig.add_subplot(111)

ax.plot('start', 'snd_cwnd', data=stream, label="CWND", color="blue")

ax.set_xlabel("Time (s)")
ax.set_ylabel("CWND (Mbits)")
ax.legend()


### RTT ###
stream.rtt /= 1e3

fig = plt.figure()
ax = fig.add_subplot(111)

ax.plot('start', 'rtt', data=stream, label="Observed RTT", color="blue")

ax.set_xlabel("Time (s)")
ax.set_ylabel("RTT (ms)")
ax.legend()


## Losses
fig = plt.figure()
ax = fig.add_subplot(111)

ax.plot('start', 'retransmits', data=stream, label="Losses", color="red")

ax.set_xlabel("Time (s)")
ax.set_ylabel("Losses")
ax.legend()


plt.show()
