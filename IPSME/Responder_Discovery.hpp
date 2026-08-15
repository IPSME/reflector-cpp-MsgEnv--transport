
#pragma once

#include <memory>
#include <nlohmann/json.hpp>

#include "../g_.hpp"
// #include "IPSME_MsgEnv.h"
#include "../cpp-json-msg.git/json_msg+ack.h"
#include "../cpp-EventLog.git/IEventLog.hpp"
#include "../Interface_App.hpp"

#include "../generated/interface-Echo.h"
#include "../generated/interface-Discovery.h"

// the transport protocol this reflector SERVICES, injected by CMake (two accepted wire spellings
// of the SAME protocol -- see Responder_MessagingEnv). NO fallback -- a missing injection must
// fail the build, never default to another transport's spelling.
#ifndef BUILD_PROTOCOL1
#error "BUILD_PROTOCOL1 not defined -- injected by CMake from TRANSPORT (see CMakeLists PROTOCOL1)"
#endif
#ifndef BUILD_PROTOCOL2
#error "BUILD_PROTOCOL2 not defined -- injected by CMake from TRANSPORT (see CMakeLists PROTOCOL2)"
#endif

class IEvent;   // reprocess() takes a std::shared_ptr<IEvent> (defined in the bridge's event-log header)

class Responder_Discovery {
	// generated-interface types, scoped to this responder (no namespace leak into includers)
	using JSON_MsgDiscover = reflector_iface::Discovery::JSON_MsgDiscover;
	using JSON_EffAnnounce = reflector_iface::Discovery::JSON_EffAnnounce;
	using EchoRequest      = reflector_iface::Echo::EchoRequest;

public:
	Responder_Discovery(IPSME_MsgEnv * const kp_IPSME, Interface_App * const kpi_App, IEventLog * const kp_IEventLog)
		: _kp_IPSME(kp_IPSME), _kpi_App(kpi_App), _kp_IEventLog(kp_IEventLog), _referer(kpi_App->referer())
	{
	}

	// publish_ ...

	void announce()
	{
		JSON_EffAnnounce json_effAnnounce = JSON_EffAnnounce::create_with_cause_merge(nullptr, R"(
{
    "blahblah" : true
}
	    )");
		assert(JSON_EffAnnounce::validate(json_effAnnounce));
		std::string str_effAnnounce;
		PUBLISH3(json_effAnnounce, &str_effAnnounce);

		auto ptr_evt= _kp_IEventLog->add_Event(
			"Discovery[" + CLASS_NAME(json_effAnnounce) + "]",
			json_effAnnounce["id"],
			msg_json_msg(str_effAnnounce.c_str(), json_effAnnounce)
		);
	}

private:
	// -----------------------------------------------------
	// one per message (reverse schema order):

	bool _handler_effAnnounce(IPSME_MsgEnv::t_MSG msg, JSON_EffAnnounce json_effAnnounce)
	{
		printf("%s: [%s]\n", __func__, json_effAnnounce.to_string().c_str());

		// if (! ...)
		//     return false;

		return false;
	}

	bool _handler_msgDiscover(IPSME_MsgEnv::t_MSG msg, JSON_MsgDiscover json_msgDiscover)
	{
		// printf("%s: [%s]\n", __func__, json_msgDiscover.to_string().c_str());
		DebugPrint("%s: [%s]\n", __func__, json_msgDiscover.to_string().c_str());

		// A CAPABILITY discover (discover.protocols -- 'who out there services these transports?'
		// / discover.interfaces -- 'who out there ACCEPTS these message dialects?'): answer with
		// what THIS reflector offers -- the transport protocols it services (both accepted
		// spellings: PROTOCOL1 canonical, PROTOCOL2 the alternate) and the interfaces it accepts
		// (MessagingEnv: the ctrl-msg touches are handled here) -- each answered only when asked,
		// cause-merged onto the discover. A liveness discover (echo-request) continues below.
		const bool b_protocols= json_msgDiscover["discover"]["protocols"].is_array();
		const bool b_interfaces= json_msgDiscover["discover"]["interfaces"].is_object();
		if (b_protocols || b_interfaces) {
			nlohmann::json json_announce;
			if (b_protocols)
				json_announce["protocols"]= { BUILD_PROTOCOL1, BUILD_PROTOCOL2 };
			if (b_interfaces) {
				// interfaces = MAP of name -> sha256 of the accepted SCHEMA FILE (the hash is the
				// identity, the name the label), hand-transcribed from ifacegenconfig.json's pin --
				// iface-gen emitting a SCHEMA_SHA256 const would make this readable from the
				// generated header instead
				json_announce["interfaces"]["MessagingEnv"]= "d6dd578f2f33e6503cbbf3338384aaad2f23ca681a6fb24be3207487e5a949b0";
			}
			JSON_EffAnnounce json_effAnnounce= JSON_EffAnnounce::create_with_cause_merge(json_msgDiscover, json_announce);
			printf("%s: capability discover -> announce [%s]\n", __func__, json_effAnnounce["announce"].dump().c_str());
			PUBLISH(json_effAnnounce);
			return true;
		}

        JSON::JSON_ echoRequest = json_msgDiscover["discover"]["echo-request"];
        if (! EchoRequest::validate(echoRequest))
            return false;

        bool b_refererFound = false;
        for (size_t i = 0; i < echoRequest.size(); i++) {
            // a request entry may carry an empty/absent referer (a product with no participant) --
            // skip it (interest management: drop what we don't understand), never .get<> on null (throws)
            auto json_participant = echoRequest[i]["referer"]["participant"];
            if (! json_participant.is_string())
                continue;

            if (json_participant.get<std::string>() == _referer.PARTICIPANT) {
                b_refererFound = true;
                break;
            }
        }
        if (! b_refererFound)
            return false;

        JSON_EffAnnounce json_effAnnounce= JSON_EffAnnounce::create_with_cause_merge(json_msgDiscover, R"( { "echo-response" : [] } )");
        PUBLISH(json_effAnnounce);

		return true;
	}

public:
	// -----------------------------------------------------
	// base dispatch — the bridge calls these:

	bool handler_json_eff(IPSME_MsgEnv::t_MSG msg, JSON::JSON_Eff json_eff)
	{
		if (JSON_EffAnnounce::validate(json_eff) && _handler_effAnnounce(msg, json_eff))
			return true;

		return false;
	}

	bool handler_json_msg(IPSME_MsgEnv::t_MSG msg, JSON::JSON_Msg json_msg)
	{
		if (JSON_MsgDiscover::validate(json_msg) && _handler_msgDiscover(msg, json_msg))
			return true;

		return false;
	}

	bool reprocess(std::shared_ptr<IEvent> ptr_evt)
	{
		// reprocess parked events ...
		return false;
	}

private:
	IPSME_MsgEnv * const _kp_IPSME;
	Interface_App * const _kpi_App;
	IEventLog * const _kp_IEventLog;
	const JSON::JSON_Msg::Referer _referer;
};
