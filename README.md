# UDP throughput benchmark

Before attempt the following
[read this article](https://docs.redhat.com/en/documentation/red_hat_enterprise_linux/10/html/network_troubleshooting_and_performance_tuning/tuning-udp-connections)
and run
[`iperf3`](http://software.es.net/iperf/)
to establish a baseline for comparison.

## iperf3

iperf3 needs both a TCP and UDP port in order to run the UDP test.
(TCP used for coordination?)

eg. to temporarily open an arbitrarily selected port 5678 on a host using `firewalld`.

```
sudo firewall-cmd --zone=public --add-port=5678/udp --timeout=4h
sudo firewall-cmd --zone=public --add-port=5678/tcp --timeout=4h
```

Setup the receiver

```
iperf3 --server --port 5678
```

Start testing from the sender.

```
iperf3 --udp --port 5678  --time 60  --length 1450 --bitrate 2G --client 10.139.4.51
```

## Build

```
git clone https://github.com/mdavidsaver/udpbench
cd udpbench
make
```

## Usage

On the receiver run:

```
./blasted 0.0.0.0:5678
```

On the sender run:

```
./blaster 192.168.7.51:5678
```

Interrupt the receiver (`Ctrl+c`) after each "blast" to show statistics.
Timing is based on the first and last packets received in one run.
eg.

```
$ ./blasted 0.0.0.0:5678
listening 0.0.0.0:5678
^C
recv'd 1198967220 bytes in 825735 pkts
#skips 421793
in 11.050 sec
  827.786 Mb/s
```

## Options

```
./blaster [-S] [-m <MTU>] [-b <BATCHSIZE>] [-t <totalMB>] <IP[:port#]>

  -S  - use UDP_SEGMENT
  -m <MTU> - set UDP payload size.  Default 1450
  -b <BATCHSIZE> - Number of packets to push to kernel at once.  Default 1
  -t <totalMB> - Total size to send, in MB.  Default 16MB
  <IP[:port#[> - Destination endpoint address
```

`blaster` provides control over how batches of packets
are enqueued to the socket buffer.
Batches are submitted by `sendmmsg()` with BATCHSIZE individual messages by default,
or as one big "packet" with the UDP_SEGMENT socket option to do segmentation
at a lower level.
