# UDP Client/Server

This is a UDP client server application using dpdk. Client generates flows to
different destination and measures throughput and latency.

## Build

Look at `INSTALL` file.

The `Makefile` tries to detect if the server has Mellanox NICs and adds
`-lmlx5` flag. But you might want to double check that if things are not
working (e.g., if you need `-lmlx4`)

## Generate Load

The `--client` flag runs the app in the load generator mode. It lunches a
thread for sending and a thread for receiving so it requires two cores. But the
cores can not be given simply using `-l 1,2,...` because the app tries to lunch
a worker thread(send/recv) per given core. You should use `--lcores` to group
two physical cores into one logical core.

Alternatively, if you do not need to receive data back, use `--unidir` to only
lunch a sender thread.

```
sudo ./build/app  --lcores '0@(2,4)' -a 03:00.1 -- --client --ip-local 192.168.1.2 --ip-dest 192.168.1.1  --duration 50  --batch 32 --rate 1000000
```

> Feel free to change things in `client.c` to match your expected scenario


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
