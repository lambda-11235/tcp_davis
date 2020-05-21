#!/usr/bin/env python3

import argparse
import gzip
import json
import re

parser = argparse.ArgumentParser(
    description="")
parser.add_argument('infile', type=str, nargs='+',
    help="")
args = parser.parse_args()


def avg(xs):
    xs = list(filter(lambda x: x is not None, xs))
    return sum(xs)/len(xs)

pattern = re.compile("TCP upload::[0-9]+")


for f in args.infile:
    with gzip.open(f) as inp:
        data = json.load(inp)

    sumUp = 0
    sumSqrUp = 0
    cnt = 0

    for name, values in data['results'].items():
        if pattern.fullmatch(name):
            x = avg(values)
            sumUp += x
            sumSqrUp += x**2
            cnt += 1

    jain = sumUp**2/(cnt*sumSqrUp)
    print(f"{f}: {jain}")
