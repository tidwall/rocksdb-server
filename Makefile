all: rocksdb libuv
	@g++ -O3 -std=c++11 \
		-DROCKSDB_VERSION="\"4.13"\" \
		-DSERVER_VERSION="\"0.1.0"\" \
		-DLIBUV_VERSION="\"1.10.1"\" \
		-Isrc/rocksdb-4.13/include/ \
		-Isrc/libuv-1.10.1/build/include/ \
		-pthread \
		-o rocksdb-server \
		src/server.cc src/match.cc \
		src/rocksdb-4.13/librocksdb.a \
		src/rocksdb-4.13/libbz2.a \
		src/rocksdb-4.13/libz.a \
		src/libuv-1.10.1/build/lib/libuv.a
clean:
	rm -f rocksdb-server
	rm -rf src/libuv-1.10.1/
	rm -rf src/rocksdb-4.13/
install: all
	cp rocksdb-server /usr/local/bin
uninstall: 
	rm -f /usr/local/bin/rocksdb-server

# libuv
libuv: src/libuv-1.10.1/build/lib/libuv.a
src/libuv-1.10.1/build/lib/libuv.a:
	cd src && tar xf libuv-1.10.1.tar.gz
	cd src/libuv-1.10.1 && sh autogen.sh
	mkdir -p src/libuv-1.10.1/build
	cd src/libuv-1.10.1/build && ../configure --prefix=$$(pwd)
	make -C src/libuv-1.10.1/build install


# rocksdb
rocksdb: src/rocksdb-4.13 \
	src/rocksdb-4.13/librocksdb.a \
	src/rocksdb-4.13/libz.a \
	src/rocksdb-4.13/libbz2.a
src/rocksdb-4.13:
	cd src && tar xf rocksdb-4.13.tar.gz
src/rocksdb-4.13/librocksdb.a:
	make -C src/rocksdb-4.13 static_lib
src/rocksdb-4.13/libz.a:
	make -C src/rocksdb-4.13 libz.a
src/rocksdb-4.13/libbz2.a:
	make -C src/rocksdb-4.13 libbz2.a


