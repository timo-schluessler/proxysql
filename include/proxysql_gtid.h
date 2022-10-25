#ifndef PROXYSQL_GTID
#define PROXYSQL_GTID
// highly inspired by libslave
// https://github.com/vozbu/libslave/
#include <unordered_map>
#include <list>
#include <utility>
#include <queue>

#include "fnv-1a.h"

typedef std::pair<std::string, int64_t> gtid_t;
typedef std::pair<int64_t, int64_t> gtid_interval_t;
typedef std::unordered_map<std::string, std::list<gtid_interval_t>> gtid_set_t;

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

struct CharPtrOrString {
	const char* p_;
	std::string s_;

	explicit CharPtrOrString(const char* p) : p_{p} { }
	CharPtrOrString(std::string s) : p_{nullptr}, s_{std::move(s)} { }

	bool operator==(const CharPtrOrString& x) const {
		return p_ ? x.p_ ? std::strcmp(p_, x.p_) == 0 : p_ == x.s_
		          : x.p_ ? s_ == x.p_ : s_ == x.s_;
	}

	struct Hash {
		size_t operator()(const CharPtrOrString& x) const {
			auto hashfn = fnv1a_t<CHAR_BIT * sizeof(std::size_t)> {};
			if (x.p_)
				hashfn.update(x.p_, std::strlen(x.p_));
			else
				hashfn.update(x.s_.data(), x.s_.size());
			return hashfn.digest();
		}
	};
};

#endif /* PROXYSQL_GTID */
