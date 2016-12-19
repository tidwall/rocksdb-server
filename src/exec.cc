#include <rocksdb/db.h>
#include "shared.h"

// rocksdb is multithreaded without any extra syncronization.
// https://github.com/facebook/rocksdb/wiki/Basic-Operations#concurrency
extern rocksdb::DB* db;

error exec_command(client *c){
	const char **argv = client_argv(c);
	int *argl = client_argl(c);
	int argc = client_argc(c);
	if (argc==0){
		return NULL;
	}
	if (argl[0] == 3 && 
		(argv[0][0] == 'S' || argv[0][0] == 's') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'T' || argv[0][2] == 't')){
		return exec_set(c);
	}else if (argl[0] == 3 &&
		(argv[0][0] == 'G' || argv[0][0] == 'g') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'T' || argv[0][2] == 't')){
		return exec_get(c);
	}else if (argl[0] == 3 &&
		(argv[0][0] == 'D' || argv[0][0] == 'd') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'L' || argv[0][2] == 'l')){
		return exec_del(c);
	}else if (argl[0] == 4 &&
		(argv[0][0] == 'Q' || argv[0][0] == 'q') &&
		(argv[0][1] == 'U' || argv[0][1] == 'u') &&
		(argv[0][2] == 'I' || argv[0][2] == 'i') &&
		(argv[0][3] == 'T' || argv[0][3] == 't')){
		return exec_quit(c);
	}else if (argl[0] == 4 &&
		(argv[0][0] == 'K' || argv[0][0] == 'k') &&
		(argv[0][1] == 'E' || argv[0][1] == 'e') &&
		(argv[0][2] == 'Y' || argv[0][2] == 'y') &&
		(argv[0][3] == 'S' || argv[0][3] == 's')){
		return exec_keys(c);
	}
	return client_err_unknown_command(c, argv[0], argl[0]);
}

error exec_set(client *c){
	const char **argv = client_argv(c);
	int *argl = client_argl(c);
	int argc = client_argc(c);
	if (argc!=3){
		return "wrong number of arguments for 'set' command";
	}	
	std::string key(argv[1], argl[1]);
	std::string value(argv[2], argl[2]);
	rocksdb::WriteOptions write_options;
	write_options.sync = true;
	rocksdb::Status s = db->Put(write_options, key, value);
	if (!s.ok()){
		panic(s.ToString().c_str());
	}
	client_clear(c);
	client_write(c, "+OK\r\n", 5);
	client_flush(c);
	return NULL;
}

error exec_get(client *c){
	const char **argv = client_argv(c);
	int *argl = client_argl(c);
	int argc = client_argc(c);
	if (argc!=2){
		return "wrong number of arguments for 'get' command";
	}	
	std::string key(argv[1], argl[1]);
	std::string value;
	rocksdb::ReadOptions read_options;
	rocksdb::Status s = db->Get(read_options, key, &value);
	if (!s.ok()){
		if (s.IsNotFound()){
			client_clear(c);
			client_write(c, "$-1\r\n", 5);
			client_flush(c);
			return NULL;
		}
		panic(s.ToString().c_str());
	}
	client_clear(c);
	client_write_bulk(c, value.data(), value.size());
	client_flush(c);
	return NULL;
}

error exec_del(client *c){
	const char **argv = client_argv(c);
	int *argl = client_argl(c);
	int argc = client_argc(c);
	if (argc!=2){
		return "wrong number of arguments for 'del' command";
	}
	std::string key(argv[1], argl[1]);
	std::string value; 
	rocksdb::Status s = db->Get(rocksdb::ReadOptions(), key, &value);
	if (!s.ok()){
		if (s.IsNotFound()){
			client_clear(c);
			client_write(c, ":0\r\n", 4);
			client_flush(c);
			return NULL;
		}
		panic(s.ToString().c_str());
	}
	rocksdb::WriteOptions write_options;
	write_options.sync = true;
	s = db->Delete(write_options, key);
	if (!s.ok()){
		panic(s.ToString().c_str());
	}
	client_clear(c);
	client_write(c, ":1\r\n", 4);
	client_flush(c);
	return NULL;
}

error exec_quit(client *c){
	client_clear(c);
	client_write(c, "+OK\r\n", 5);
	client_flush(c);
	exit(0);
	return NULL;
}

error exec_keys(client *c){
	const char **argv = client_argv(c);
	int *argl = client_argl(c);
	int argc = client_argc(c);
	if (argc!=2){
		return "wrong number of arguments for 'keys' command";
	}

	client_clear(c);
	client_write(c, "012345678901234567890123456789", 30);

	int count = 0;
	rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions());
	for (it->SeekToFirst(); it->Valid(); it->Next()) {
		rocksdb::Slice key = it->key();
		if (stringmatchlen(argv[1], argl[1], key.data(), key.size(), 1)){
			client_write_bulk(c, key.data(), key.size());
			count++;	
		}
	}
	rocksdb::Status s = it->status();
	if (!s.ok()){
		panic(s.ToString().c_str());	
	}
	delete it;
	
	// fill in the header and write from offset.
	char nb[32];
	sprintf(nb, "*%d\r\n", count);
	int nbn = strlen(nb);
	memcpy(client_raw(c)+30-nbn, nb, nbn);
	client_flush_offset(c, 30-nbn);
	return NULL;
}
