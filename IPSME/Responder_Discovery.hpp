
#pragma once

#include <string_view>
#include <nlohmann/json.hpp>

#include "../g_.hpp"
#include "../cpp-EventLog.git/IEventLog.hpp"
//#include "../cpp-msgenv-MQTT.git/IPSME_MsgEnv.h"
#include "../generated/interface-Discovery.h"
#include "../generated/interface-Echo.h"


class IPSME_Bridge;

class Responder_Discovery {
	// generated-interface types, scoped to this responder (no namespace leak into includers)
	using JSON_MsgDiscover  = reflector_iface::Discovery::JSON_MsgDiscover;
	using JSON_EffAnnounce = reflector_iface::Discovery::JSON_EffAnnounce;
	using EchoRequest       = reflector_iface::Echo::EchoRequest;

public:
	Responder_Discovery(IPSME_MsgEnv * const kp_IPSME, Interface_App * const kpi_App, IEventLog * const kp_IEventLog)
		: _kp_IPSME(kp_IPSME), _kpi_App(kpi_App), _kp_IEventLog(kp_IEventLog), _referer(kpi_App->referer())
	{

	}

private:
	bool _handler_msgDiscover(IPSME_MsgEnv::t_MSG msg, JSON_MsgDiscover json_msgDiscover)
	{
		//printf("%s: [%s]\n", __func__, json_msgDiscover.to_string().c_str());
		DebugPrint("%s: [%s]\n", __func__, json_msgDiscover.to_string().c_str());

		JSON::JSON_ json_echoRequest = json_msgDiscover["discover"]["echo-request"];
		if (EchoRequest::validate(json_echoRequest))
		{
			bool b_refererFound = false;
			for (size_t i = 0; i < json_echoRequest.size(); i++) {
				auto json_participant = json_echoRequest[i]["referer"]["participant"];
				//if (! json_participant.is_string())
				//    continue;

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

		return false;
	}

public:
	bool handler_json_msg(IPSME_MsgEnv::t_MSG msg, JSON::JSON_Msg json_msg)
	{
		if (JSON_MsgDiscover::validate(json_msg) && _handler_msgDiscover(msg, json_msg))
			return true;

		return false;
	}

public:
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
	IPSME_MsgEnv * const _kp_IPSME;
	Interface_App * const _kpi_App;
	IEventLog * const _kp_IEventLog;
	const JSON::JSON_Msg::Referer _referer;

};