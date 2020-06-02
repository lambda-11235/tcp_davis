#!/bin/sh

if [ -z $1 ]
then
    echo "Usage: $0 INTERFACE"
    exit 1
else
    INTERFACE=$1
fi

sysctl -w net.core.rmem_default=16777216
sysctl -w net.core.wmem_default=16777216
sysctl -w net.core.netdev_max_backlog=250000
sysctl -w net.core.rmem_max=536870912
sysctl -w net.core.wmem_max=536870912
sysctl -w net.ipv4.conf.all.arp_announce=2
sysctl -w net.ipv4.conf.all.arp_filter=1
sysctl -w net.ipv4.conf.all.arp_ignore=1
sysctl -w net.ipv4.conf.default.arp_filter=1
sysctl -w net.ipv4.tcp_congestion_control=htcp
sysctl -w net.ipv4.tcp_no_metrics_save=1
sysctl -w net.ipv4.tcp_rmem="4096 87380 268435456"
sysctl -w net.ipv4.tcp_wmem="4096 65536 268435456"
sysctl -w net.ipv4.tcp_mtu_probing=1
sysctl -w net.core.netdev_budget=600
sysctl -w net.ipv4.ip_forward=1

ip link set ${INTERFACE} mtu 9000
ip link set ${INTERFACE} txqueuelen 10000

tc qdisc replace dev ${INTERFACE} root pfifo limit 10000
