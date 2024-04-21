# UDP Client/Server

This is a udp client server application using dpdk. Client generates flows to
different destination and measures throughput and latency.

## Build

Look at `INSTALL` file.

> If you have a Mellanox NIC add `-lmlx5` or `-lmxl4` to the `LDFLAGS`.


## Measure Latency


**machine 1:**

```bash
sudo ./build/app -l 2 -a 41:00.0 -- --server --ip-local 192.168.1.1
```

**machine 2:**

```bash
sudo ./build/app -a 41:00.0 -l 2 -- --latency-client --ip-local 192.168.1.2 --ip-dest 192.168.1.1 --batch 1 &> /tmp/log
bash latency_script.sh /tmp/log
```
