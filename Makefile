all: rocksdb libmill
	@g++ -O2 -std=c++11 \
		-DROCKSDB_VERSION="\"4.13"\" \
		-DSERVER_VERSION="\"0.1.0"\" \
		-Isrc/rocksdb-4.13/include/ \
		-Isrc/libmill-1.17/build/include/ \
		-pthread \
		-o rocksdb-server \
		src/server.cc src/util.cc src/client.cc src/match.cc src/exec.cc \
		src/rocksdb-4.13/librocksdb.a \
		src/rocksdb-4.13/libbz2.a \
		src/rocksdb-4.13/libz.a \
		src/libmill-1.17/build/lib/libmill.a
clean:
	rm -f rocksdb-server
	rm -rf src/libmill-1.17/
	rm -rf src/rocksdb-4.13/
install: all
	cp rocksdb-server /usr/local/bin
uninstall: 
	rm -f /usr/local/bin/rocksdb-server


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


# libmill
libmill: src/libmill-1.17/build/lib/libmill.a
src/libmill-1.17/build/lib/libmill.a:
	cd src && tar xf libmill-1.17.tar.gz
	mkdir -p src/libmill-1.17/build
	cd src/libmill-1.17/build && ../configure --prefix=$$(pwd)
	make -C src/libmill-1.17/build install

