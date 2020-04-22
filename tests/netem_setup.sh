#!/bin/sh

if [ $# -lt 3 ]
then
    echo "Usage: $0 DEV RATE RTT"
    exit 1
fi

DEV=$1
RATE=$2 # Bytes/sec
RTT=$3  # ms

run() {
    echo $@
    $@
}


MEM=`echo "2^31 - 1" | bc`
echo $MEM
BUFSIZE=`echo "$RATE*$RTT/1000" | bc`

if [ $BUFSIZE -lt $MEM ]
then
    MEM=$BUFSIZE
fi

echo $MEM

sysctl net.core.rmem_default=$MEM
sysctl net.core.rmem_max=$MEM

sysctl net.core.wmem_default=$MEM
sysctl net.core.wmem_max=$MEM

sysctl net.ipv4.tcp_rmem="$MEM $MEM $MEM"
sysctl net.ipv4.tcp_wmem="$MEM $MEM $MEM"

run tc qdisc replace dev $DEV root netem delay ${RTT}ms limit 1G
