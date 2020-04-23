#!/usr/bin/env python3

import argparse
import math
import re
from subprocess import Popen, PIPE

def fraction(s):
    x = float(s)

    if x < 0 or x > 1:
        raise ValueError()

    return x

parser = argparse.ArgumentParser(description="Setup server (NOT client) for NetEm testing.")
parser.add_argument('dev', type=str,
                    help="Network device to attach NetEm QDisc to.")
parser.add_argument('--rate', type=str,
                    help="Rate to limit traffic to in bits/s (default: no limit).")
parser.add_argument('--rtt', type=str, default="30ms",
                    help="Emulated RTT (default: %(default)s).")
parser.add_argument('--limit', type=str,
                    help="NetEm buffer length limit.")
parser.add_argument('--burst-frac', type=fraction, default=0.01,
                    help="Burstiness of TBF as fraction (in range [0, 1]) of buffer size (default: %(default)s).")
args = parser.parse_args()


maxBufSize = int(2**31 - 1)
minMTU = 576

floatRegex = '([0-9]+(\.[0-9]+)?)'
siRegex = "(T|G|M|K|t|g|m|k)"

rateRegex = '{}{}?(bits|bytes)'.format(floatRegex, siRegex)
rttRegex = '{}(s|ms|us)?'.format(floatRegex)
limitRegex = '{}{}?'.format(floatRegex, siRegex)

siScales = {'T': 1000**4, 'G': 1000**3, 'M': 1000**2, 'K': 1000,
              't': 2**40, 'g': 2**30, 'm': 2**20, 'k': 2**10}

rttScales = {'s': 1, 'ms': 1.0e-3, 'us': 1.0e-6}


def run(command, ignoreFailure=False):
    comArgs = command.split()
    
    print(str.join(' ', comArgs))
    p = Popen(comArgs)

    if p.wait() != 0 and not ignoreFailure:
        raise RuntimeError("{} exited with {}".format(comArgs[0], p.returncode))

def getSysctl(attr):
    p = Popen(["sysctl", "-n", attr], stdout=PIPE)

    if p.wait() != 0:
        raise RuntimeError("sysctl exited with {}".format(p.returncode))
    else:
        return (p.stdout.read().decode('utf-8').splitlines()[0], p)

def setSysctl(attr, value):
    p = Popen(["sysctl", "-w", "{}={}".format(attr, value)])

    if p.wait() != 0:
        raise RuntimeError("sysctl exited with {}".format(p.returncode))
    else:
        return p


### Parse Rate ###
rate = None
if args.rate is not None:
    m = re.compile(rateRegex).fullmatch(args.rate)

    if m is None:
        raise RuntimeError("Rate must conform to \"{}\".".format(rateRegex))
    
    (num, _, scale, meas) = m.groups()
    rate = float(num)

    if scale is not None:
        rate *= siScales[scale]

    if meas != 'bytes':
        rate /= 8

### Parse RTT ###
m = re.compile(rttRegex).fullmatch(args.rtt)

if m is None:
    raise RuntimeError("RTT must conform to \"{}\".".format(rttRegex))

(num, _, scale) = m.groups()
rtt = float(num)

if scale is not None:
    rtt *= rttScales[scale]

### Compute MSS ###
if rate is not None:
    bdp = rate*rtt
    
    bufsize = min(maxBufSize, math.ceil(2*bdp))
else:
    bufsize = maxBufSize

### Parse Limit ###
if args.limit is not None:
    m = re.compile(limitRegex).fullmatch(args.limit)

    if m is None:
        raise RuntimeError("Limit must conform to \"{}\".".format(limitRegex))

    (num, _, scale) = m.groups()
    limit = float(num)

    if scale is not None:
        limit *= siScales[scale]
else:
    limit = bufsize


setSysctl("net.core.rmem_default", bufsize)
setSysctl("net.core.rmem_max", bufsize)

setSysctl("net.core.wmem_default", bufsize)
setSysctl("net.core.wmem_max", bufsize)

setSysctl("net.ipv4.tcp_rmem", "{0} {0} {0}".format(bufsize))
setSysctl("net.ipv4.tcp_wmem", "{0} {0} {0}".format(bufsize))

run("tc qdisc del dev ifb0 root", ignoreFailure=True)
run("tc qdisc del dev {} ingress".format(args.dev), ignoreFailure=True)
run("tc qdisc del dev {} root".format(args.dev), ignoreFailure=True)

run("ip link set dev ifb0 up")
run("tc qdisc add dev {} ingress".format(args.dev))
run("tc filter add dev {} parent ffff: ".format(args.dev) +
    "protocol ip u32 match u32 0 0 flowid 1:1 action mirred egress redirect dev ifb0")

if rate is not None:
    run("tc qdisc add dev ifb0 root handle 1: tbf rate {}bit burst {} limit {}".format(
        math.ceil(8*rate), math.ceil(bufsize*args.burst_frac), limit))

    base = "parent 1:"
else:
    base = "root"

run("tc qdisc add dev ifb0 {} netem delay {} limit {}".format(
    base, str(round(rtt*1e6)), str(math.ceil(limit/minMTU))))
