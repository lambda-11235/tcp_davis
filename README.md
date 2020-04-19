
# TCP Davis

Welcome to the TCP Davis repository!
The main source code for the Linux module can be found in `tcp_davis.c`.
To install TCP Davis run

```
$ make
$ sudo insmod tcp_davis.ko
```


## Testing

The code for the test rig can be found in `tests/`.
Here's some example usage for emulating a 30ms link on the eth0
network interface.

```
> sudo ./netem_setup.sh eth0 30ms
> sudo ./run.sh iperf3 output_dir example.net
> ./plot output_dir/1-flows/davis.json
> ./plot_ecdf output_dir/1-flows/*
```
