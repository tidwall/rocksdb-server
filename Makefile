all:
	@g++ -O2 -std=c++11 $(FLAGS) \
		-DSERVER_VERSION="\"0.1.0"\" \
		-lrocksdb \
		-llibuv \
		-pthread \
		-o rocksdb-server \
		src/server.cc src/client.cc src/exec.cc src/match.cc src/util.cc
clean:
	rm -f rocksdb-server
	rm -rf src/libuv-1.10.1/
	rm -rf src/rocksdb-4.13/
install: all
	cp rocksdb-server /usr/local/bin
uninstall: 
	rm -f /usr/local/bin/rocksdb-server
