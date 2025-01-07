# How to use latency client?

```
Usage: ./app [DPDK EAL arguments] -- --latency-client [Client arguments]
    --ip-local
    --ip-dest
    --port client UDP port number [default: 3000]
    --port-dest server UDP port [default: 8080]
    --batch (packets) [default: 1]
    --payload (UDP payload size) [default: 64 bytes]
    --hdr-encap-sz Let the app know the server will move the offset of timestamp
    --delay (cycles) [default: 0 no dleay]
```

* `--hdr-encap-sze`: If the server changes the offset of timestamp (e.g., by
  encapsulating the payload in a new header) use this field to let the app know
  about this. Pass the number of bytes of encapsulation header.
* `--delay`: Wait for given number of cycles after sending and receiving a
  batch of measurement packets.
