
QDEV := eth3

obj-m += tcp_dumb.o
mpc_cc-y += tcp_dumb.o
ccflags-y += -g -O0 -DDEBUG

all:
	#make -C "/lib/modules/$(shell uname -r)/build" M=$(PWD) modules
	#make -C `nix-build -E '(import <nixpkgs> {}).linux.dev' --no-out-link`/lib/modules/*/build M=$(PWD) modules
	make -C `nix-build -E '(import <nixpkgs> {}).linux_latest.dev' --no-out-link`/lib/modules/*/build M=$(PWD) modules

clean:
	#make -C "/lib/modules/$(shell uname -r)/build" M=$(PWD) clean
	#make -C `nix-build -E '(import <nixpkgs> {}).linux.dev' --no-out-link`/lib/modules/*/build M=$(PWD) clean
	make -C `nix-build -E '(import <nixpkgs> {}).linux_latest.dev' --no-out-link`/lib/modules/*/build M=$(PWD) clean

start:
	insmod mpc_cc.ko
	tc qdisc replace dev $(QDEV) root fq
	sysctl -w net.ipv4.tcp_congestion_control=mpc_cc

stop:
	sysctl -w net.ipv4.tcp_congestion_control=cubic
	tc qdisc del dev $(QDEV) root
	rmmod mpc_cc

restart:
	make stop QDEV=$(QDEV)
	make -B
	make start QDEV=$(QDEV)
