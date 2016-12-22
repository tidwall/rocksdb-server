# RocksDB-Server
[Fast](#benchmarks) and simple [Redis](https://redis.io/) clone written in C using [RocksDB](http://rocksdb.org/) as a backend.

## Supported commands

```
SET key value
GET key
DEL key
KEYS *
SCAN cursor [MATCH pattern] [COUNT count]
FLUSHDB
```

Any [Redis client](https://redis.io/clients) should work.

## Building

Tested on Mac and Linux (Ubuntu), though should work on other platforms.
Please let me know if you run into build problems.

Requires `libtool` and `automake`.

Ubuntu users:
```
$ apt-get install build-esstential libtool automake
```

To build everything simply:

```
$ make
```

## Running

```
usage: ./rocksdb-server [-d data_path] [-p tcp_port] [--sync] [--inmem]
```
- `-d`      -- The database path. Default `./data/`
- `-p`      -- TCP server port. Default 5555.
- `--inmem` -- The active dataset is stored in memory. 
- `--sync`  -- Execute fsync after every SET. More durable, but much slower.

## Benchmarks

**Redis**

```
$ redis-server
```
```
$ redis-benchmark -p 6379 -t set,get -n 10000000 -q -P 256 -c 256
SET: 947867.38 requests per second
GET: 1394700.12 requests per second
```

**RocksDB**

```
$ rocksdb-server
```
```
$ redis-benchmark -p 5555 -t set,get -n 10000000 -q -P 256 -c 256
SET: 419815.28 requests per second
GET: 2132196.00 requests per second
```

*Running on a MacBook Pro 15" 2.8 GHz Intel Core i7 using Go 1.7*


## Contact
Josh Baker [@tidwall](http://twitter.com/tidwall)

## License
RocksDB-Server source code is available under the MIT [License](/LICENSE).
