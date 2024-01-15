#ifndef PROXYSQL_GTID
#define PROXYSQL_GTID
// highly inspired by libslave
// https://github.com/vozbu/libslave/
#include <unordered_map>
#include <list>
#include <utility>
#include <queue>
#include <cinttypes>

class GTID_UUID {
public:
	GTID_UUID() : uuid{} {}
	static bool from_string(GTID_UUID * uuid, const char * uuidOrGtid);
	static bool from_string(GTID_UUID * uuid, const char * uuidOrGtid, size_t length);
	static bool from_string_without_dashes(GTID_UUID * uuid, const char * in, size_t length);
	static void from_mariadb(GTID_UUID * uuid, uint32_t domain, uint32_t server_id);

	size_t len() const { return 32 + 4; }
	size_t write(void * buf) const; // write including dashes but without leading zero
	char * print() const; // prints this uuid to a thread local buffer and returns that buffer

	bool operator==(const GTID_UUID & uuid) const { return this->uuid == uuid.uuid; }
	#if __cplusplus < 202002L
		bool operator!=(const GTID_UUID & uuid) const { return !(*this == uuid); }
	#endif

	friend struct std::hash<GTID_UUID>;

private:
	std::array<char, 32> uuid; // without dashes
};

inline bool GTID_UUID::from_string(GTID_UUID * uuid, const char * uuidOrGtid) {
	return from_string(uuid, uuidOrGtid, strlen(uuidOrGtid));
}

inline bool GTID_UUID::from_string(GTID_UUID * uuid, const char * uuidOrGtid, size_t length) {
	// UUID format: 3E11FA47-71CA-11E1-9E33-C80AA9429562
	// %08X-%04X-%04X-%04X-%012X
	if (length < 36)
		return false;
	if (uuidOrGtid[36] != '\0' && uuidOrGtid[36] != ':')
		return false;
	if (uuidOrGtid[8] != '-' || uuidOrGtid[13] != '-' || uuidOrGtid[18] != '-' || uuidOrGtid[23] != '-')
		return false;
	memcpy(uuid->uuid.data()     , uuidOrGtid     ,  8);
	memcpy(uuid->uuid.data() +  8, uuidOrGtid +  9,  4);
	memcpy(uuid->uuid.data() + 12, uuidOrGtid + 14,  4);
	memcpy(uuid->uuid.data() + 16, uuidOrGtid + 19,  4);
	memcpy(uuid->uuid.data() + 20, uuidOrGtid + 24, 12);

	return true;
}

inline bool GTID_UUID::from_string_without_dashes(GTID_UUID * uuid, const char * in, size_t length) {
	if (length != 32)
		return false;
	memcpy(uuid->uuid.data(), in, 32);
	return true;
}

inline void GTID_UUID::from_mariadb(GTID_UUID * uuid, uint32_t domain, uint32_t server_id) {
	char buf[uuid->uuid.size() + 1];
	sprintf(buf, "%08" PRIX32 "0000000000000000%08" PRIX32, domain, server_id);
	memcpy(uuid->uuid.data(), buf, uuid->uuid.size());
}

inline size_t GTID_UUID::write(void * buf_void) const {
	char * buf = (char*)buf_void;
	memcpy(buf     , uuid.data()     ,  8);
	buf[8] = '-';
	memcpy(buf +  9, uuid.data() +  8,  4);
	buf[13] = '-';
	memcpy(buf + 14, uuid.data() + 12,  4);
	buf[18] = '-';
	memcpy(buf + 19, uuid.data() + 16,  4);
	buf[23] = '-';
	memcpy(buf + 24, uuid.data() + 20, 12);
	return 36;
}

inline char * GTID_UUID::print() const {
	thread_local static char buf[32 + 4 + 1];
	auto len = write(buf);
	buf[len] = '\0';

	return buf;
}

template<>
struct std::hash<GTID_UUID>
{
	size_t operator()(GTID_UUID const& uuid) const noexcept {
		// TODO benchmark whether it is better to use this "hashing" method
		// TODO or to use fnv1a
		uint64_t tmp[4];
		memcpy(tmp, uuid.uuid.data(), 8);
		memcpy(tmp + 1, uuid.uuid.data() + 8, 8);
		memcpy(tmp + 2, uuid.uuid.data() + 16, 8);
		memcpy(tmp + 3, uuid.uuid.data() + 24, 8);
		uint64_t sum = tmp[0] + tmp[1] + tmp[2] + tmp[3];
		if (sizeof(size_t) == sizeof(uint64_t))
			return sum;
		else {
			uint32_t * u32 = (uint32_t*)&sum;
			return (size_t)(u32[0] + u32[1]);
		}
	}
};


typedef std::pair<GTID_UUID, int64_t> gtid_t;
typedef std::pair<int64_t, int64_t> gtid_interval_t;
typedef std::unordered_map<GTID_UUID, std::list<gtid_interval_t>> gtid_set_t;

/*
class Gtid_Server_Info {
	public:
	gtid_set_t executed_gtid_set;
	char *hostname;
	uint16_t mysql_port;
	uint16_t gtid_port;
	bool active;
	Gtid_Server_Info(char *_h, uint16_t _mp, uint16_t _gp) {
		hostname = strdup(_h);
		mysql_port = _mp;
		gtid_port = _gp;
		active = true;
	};
	~Gtid_Server_Info() {
		free(hostname);
	};
};
*/

struct GTID_Await {
	uint64_t trxid;
	int pipefd; // used to wake corresponding thread
	enum State {
		Waiting,
		Reached,
		Aborted,
	};
	std::atomic<State> state;
};

struct GTID_Await_Compare {
	bool operator() (GTID_Await * left, GTID_Await * right) {
		return left->trxid > right->trxid;
	}
};
typedef std::priority_queue<GTID_Await*, std::deque<GTID_Await*>, GTID_Await_Compare> GTID_Await_Queue;

struct GTID_Awaits_Per_Weight {
	GTID_Await_Queue queue;
	unsigned int min_weight;
	uint64_t latest_trxid;
};

typedef std::vector<GTID_Awaits_Per_Weight> GTID_Awaits;

class Shared_GTID {
	public:
		Shared_GTID(unsigned int hid, const GTID_UUID & uuid, uint64_t trxid) : _hid(hid), trxid(trxid) {
			pthread_rwlock_init(&rwlock, nullptr);
			this->uuid = uuid;
		}
		unsigned int hid() { return _hid; }
		void update(const GTID_UUID & uuid, uint64_t trxid) {
			pthread_rwlock_wrlock(&rwlock);
			// this is somewhat buggy with MySQL: trxid's are not globally strictly monotonic increasing, but every node has its own trxid counter.
			// this means, that after a failover, trxid might jump backwards. with MariaDB this doesn't happen.
			// to support MySQL we allow trxids going backward, if the uuid changes. BUT this might cause us to store an older GTID when two queries race.
			if (uuid != this->uuid) {
				this->uuid = uuid;
				this->trxid = trxid;
			} else if (this->trxid < trxid)
				this->trxid = trxid;
			pthread_rwlock_unlock(&rwlock);
		}
		void get(GTID_UUID & uuid, uint64_t & trxid) {
			pthread_rwlock_rdlock(&rwlock);
			uuid = this->uuid;
			trxid = this->trxid;
			pthread_rwlock_unlock(&rwlock);
		}
	private:
		pthread_rwlock_t rwlock; // TODO I think this would be a better fit for a read/write spinlock?
		unsigned int _hid;
		GTID_UUID uuid;
		uint64_t trxid;
};

#endif /* PROXYSQL_GTID */
