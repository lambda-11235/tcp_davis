#!/usr/bin/env python3

import argparse
import matplotlib

import matplotlib as mpl
import matplotlib.pyplot as plt

from data import Data

bwctlStartTime = 0

parser = argparse.ArgumentParser(description="Plots test results.")
parser.add_argument('test', type=str, nargs='+',
        help="The name of the test to run.")
parser.add_argument('-o', '--output', type=str,
        help="Sets output file base name.")
parser.add_argument('-e', '--output-extension', type=str, default="pdf",
        help="Sets the extension for output files (i.e. pdf, png, etc.).")
parser.add_argument('-s', '--stream', type=int, default=0,
        help="Select the iperf stream to use.")
parser.add_argument('--ps', action='store_true',
        help="Read a pschedular output.")
args = parser.parse_args()


data = [Data(t, args.ps) for t in args.test]
streams = [d.streams[args.stream] for d in data]

for stream in streams:
    stream.sort_values('start', inplace=True)

mpl.style.use('seaborn-bright')
mpl.rc('figure', dpi=200)


def ecdf(xs):
    return (sorted(xs), [i/len(xs) for i in range(len(xs))])


### Rate ###
fig = plt.figure()
ax = fig.add_subplot(111)

for test, stream in zip(args.test, streams):
    rate = stream.bits_per_second/2**20
    xs, ys = ecdf(rate)
    ax.plot(xs, ys, label=test)

ax.set_xlabel("r (Mbps)")
ax.set_ylabel("P(Rate < r)")
ax.legend()


### cwnd ###
fig = plt.figure()
ax = fig.add_subplot(111)

for test, stream in zip(args.test, streams):
    cwnd = stream.snd_cwnd/2**20
    xs, ys = ecdf(cwnd)
    ax.plot(xs, ys, label=test)

ax.set_xlabel("c (Mbits)")
ax.set_ylabel("P(CWND < c)")
ax.legend()


### RTT ###
rtt = stream.rtt/1e3

fig = plt.figure()
ax = fig.add_subplot(111)

for test, stream in zip(args.test, streams):
    rtt = stream.rtt/1e3
    xs, ys = ecdf(rtt)
    ax.plot(xs, ys, label=test)

ax.set_xlabel("r (ms)")
ax.set_ylabel("P(RTT < r)")
ax.legend()


plt.show()
