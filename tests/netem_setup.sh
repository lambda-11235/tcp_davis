#!/bin/sh

if [ $# -lt 2 ]
then
    echo "Usage: $0 DEV RTT"
    exit 1
fi

DEV=$1
RTT=$2

run() {
    echo $@
    $@
}

sysctl net.core.rmem_default=2147483647
sysctl net.core.rmem_max=2147483647

sysctl net.core.wmem_default=2147483647
sysctl net.core.wmem_max=2147483647

sysctl net.ipv4.tcp_rmem="2147483647 2147483647 2147483647"
sysctl net.ipv4.tcp_wmem="2147483647 2147483647 2147483647"

run tc qdisc replace dev $DEV root netem delay $RTT limit 1G
