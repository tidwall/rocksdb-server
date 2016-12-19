all: src/rocksdb/librocksdb.a src/rocksdb/libz.a src/rocksdb/libbz2.a \
	src/libmill-1.17/build/lib/libmill.a
	@g++ -O2 -std=c++11 \
		-I src/rocksdb/include/ -L src/rocksdb/ -lrocksdb -lz -lbz2 \
		-I src/libmill-1.17/build/include/ -L src/libmill-1.17/build/lib/ -lmill \
		-o rdbp \
		src/main.cc src/util.cc src/client.cc src/match.cc
src/rocksdb/librocksdb.a:
	make -C rocksdb static_lib
src/rocksdb/libz.a:
	make -C rocksdb libz.a
src/rocksdb/libbz2.a:
	make -C rocksdb libbz2.a
src/libmill-1.17/build/lib/libmill.a:
	mkdir -p src/libmill-1.17/build
	cd src/libmill-1.17/build && ../configure --prefix=$$(pwd)
	make -C src/libmill-1.17/build install
clean:
	rm -f rdbp
	make -C rocksdb clean
	rm -rf src/libmill-1.17/build
install: all
	cp rdbp /usr/local/bin
uninstall: 
	rm -f /usr/local/bin/rdbp
