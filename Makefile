all: rocksdb libuv
	@g++ -O2 -std=c++11 $(FLAGS) \
		-DROCKSDB_VERSION="\"5.9.2"\" \
		-DSERVER_VERSION="\"0.1.0"\" \
		-DLIBUV_VERSION="\"1.19.1"\" \
		-Isrc/rocksdb-5.9.2/include/ \
		-Isrc/libuv-1.19.1/build/include/ \
		-pthread \
		-o rocksdb-server \
		src/server.cc src/client.cc src/exec.cc src/match.cc src/util.cc \
		src/rocksdb-5.9.2/librocksdb.a \
		src/rocksdb-5.9.2/libbz2.a \
		src/rocksdb-5.9.2/libz.a \
		src/rocksdb-5.9.2/libsnappy.a \
		src/libuv-1.19.1/build/lib/libuv.a \
		-llz4 -ljemalloc
clean:
	rm -f rocksdb-server
	rm -rf src/libuv-1.19.1/
	rm -f src/libuv-1.19.1.tar.gz
	rm -rf src/rocksdb-5.9.2/
	rm -f src/rocksdb-5.9.2.tar.gz
install: all
	cp rocksdb-server /usr/local/bin
uninstall:
	rm -f /usr/local/bin/rocksdb-server

# libuv
libuv: src/libuv-1.19.1/build/lib/libuv.a
src/libuv-1.19.1/build/lib/libuv.a:
	wget -c https://github.com/libuv/libuv/archive/v1.19.1.tar.gz -O src/libuv-1.19.1.tar.gz
	cd src && tar xf libuv-1.19.1.tar.gz
	cd src/libuv-1.19.1 && sh autogen.sh
	mkdir -p src/libuv-1.19.1/build
	cd src/libuv-1.19.1/build && ../configure --prefix=$$(pwd)
	make -j 2 -C src/libuv-1.19.1/build install


# rocksdb
rocksdb: src/rocksdb-5.9.2 \
	src/rocksdb-5.9.2/librocksdb.a \
	src/rocksdb-5.9.2/libz.a \
	src/rocksdb-5.9.2/libbz2.a \
	src/rocksdb-5.9.2/libsnappy.a
src/rocksdb-5.9.2:
	wget -c https://github.com/facebook/rocksdb/archive/v5.9.2.tar.gz -O src/rocksdb-5.9.2.tar.gz
	cd src && tar xf rocksdb-5.9.2.tar.gz
src/rocksdb-5.9.2/librocksdb.a:
	DEBUG_LEVEL=0 make -j 2 -C src/rocksdb-5.9.2 static_lib
src/rocksdb-5.9.2/libz.a:
	DEBUG_LEVEL=0 make -j 2 -C src/rocksdb-5.9.2 libz.a
src/rocksdb-5.9.2/libbz2.a:
	DEBUG_LEVEL=0 make -j 2 -C src/rocksdb-5.9.2 libbz2.a
src/rocksdb-5.9.2/libsnappy.a:
	DEBUG_LEVEL=0 make -j 2 -C src/rocksdb-5.9.2 libsnappy.a
