
#pragma once

#include <optional>
#include <string>
#include <string_view>
using namespace std::literals::string_view_literals;

#include "cpp-json-msg.git/json_msg+ack.h"
#include "cpp-EventLog.git/IEventLog.hpp"

// node / electron-main / … C++ participant: the app interface the responders call back into. The
// entrypoint constructs IPSME_Bridge::get_instance(&app, &eventLog) and drives process_msgs().

class Interface_App {
public:
	Interface_App(const JSON::JSON_Msg::Referer& referer)
		: _referer(referer)
	{}

	virtual ~Interface_App() = default;

    // Called when a discovery message times out
    virtual bool on_MsgEnv_msg(const char* psz_msg, std::string str_msg) = 0;

    // MessagingEnv touch: ensure a live transport connection to the named server -- dial it if we do
    // not already hold one, otherwise a keep-alive no-op (the connection self-reconnects). i64_ttl_msec
    // is the lease window (for future idle-expiry of the connection). Returns TRUE iff this touch started a
    // NEW dial (so the caller can await its settling + ack it); FALSE on a keep-alive restamp / no-op.
    virtual bool touch(const std::string& str_address, const std::string& str_id, int64_t i64_ttl_msec) = 0;

    // true once a live transport connection to str_id is established (the asio connect completed); false while
    // still dialing or unknown. Lets the MessagingEnv responder ack a touch only after its new dial has SETTLED.
    virtual bool is_connected(const std::string& str_id) const = 0;

	const JSON::JSON_Msg::Referer& referer() const { return _referer; }

private:
	const JSON::JSON_Msg::Referer _referer;
};
