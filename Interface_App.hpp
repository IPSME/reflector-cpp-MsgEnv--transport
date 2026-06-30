
#pragma once

#include <optional>
#include <string>
#include <string_view>
using namespace std::literals::string_view_literals;

#include "cpp-json-msg.git/json_msg+ack.h"
#include "cpp-EventLog.git/IEventLog.hpp"

struct DiscoverContext {
    int idx;
    struct {
        std::string data;
    } evt;
};

class Interface_App {
public:
    Interface_App(const JSON::JSON_Msg::Referer& referer)
        : _referer(referer)
    {
    }

    virtual ~Interface_App() = default;

    // Called when a discovery message times out
    virtual bool on_MsgEnv_msg(const char* psz_msg, std::string str_msg) = 0;

    // MessagingEnv touch: ensure a live transport connection to the named server -- dial it if we do
    // not already hold one, otherwise a keep-alive no-op (the connection self-reconnects). i64_ttl_msec
    // is the lease window (for future idle-expiry of the connection).
    virtual void touch(const std::string& str_address, const std::string& str_id, int64_t i64_ttl_msec) = 0;

    const JSON::JSON_Msg::Referer& referer() const { return _referer; }

private:
    const JSON::JSON_Msg::Referer _referer;

};