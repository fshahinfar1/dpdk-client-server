# UDP Client/Server

This is a udp client server application using dpdk. Client generates flows to
different destination and measures throughput and latency.

## Build

Look at `INSTALL` file.

> If you have a Mellanox NIC add `-lmlx5` or `-lmxl4` to the `LDFLAGS`.


## Measure Latency


**machine 1:**

```bash
sudo ./build/app -a 41:00.0 -l 4 --  192.168.1.1 1 server 0
```

**machine 2:**

```bash
sudo ./build/app -a 41:00.0 -l 4 -- 192.168.1.2 1 client 1 192.168.1.1 1 5 3000 0 > /tmp/log
bash latency_script.sh /tmp/log
```
