
#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "../g_.hpp"
#include "../cpp-EventLog.git/IEventLog.hpp"
#include "../interface-MessagingEnv.h"

using reflector_iface::MessagingEnv::JSON_MsgCtrl;

//----------------------------------------------------------------------------------------------------------------
// Responder for the MessagingEnv `ctrl-msg` (touch) protocol. The orchestrator publishes a touch naming
// the store participants it wants reflectors to hold connections to. A reflector serves ONLY the
// participants whose protocol matches the transport THIS build was compiled with -- e.g. "tcp+l4end" for
// the asio+l4end build, a websocket protocol for a ws build -- and silently drops the rest (a reflector
// of a different transport serves those: IPSME interest management, applied per participant). The served
// protocol is therefore build-dependent and injected via BUILD_PROTOCOL (set by CMake from TRANSPORT).
//----------------------------------------------------------------------------------------------------------------

// build-injected: the on-wire protocol this reflector's transport speaks. Fallback = the asio+l4end value.
#ifndef BUILD_PROTOCOL
#define BUILD_PROTOCOL "tcp+l4end"
#endif

class Responder_MessagingEnv {
public:
	// ctrl-msg participants whose protocol != kpsz_PROTOCOL_ are not ours.
	static constexpr const char* kpsz_PROTOCOL_ = BUILD_PROTOCOL;

	Responder_MessagingEnv(IPSME_MsgEnv * const kp_IPSME, Interface_App * const kpi_App, IEventLog * const kp_IEventLog)
		: _kp_IPSME(kp_IPSME), _kpi_App(kpi_App), _kp_IEventLog(kp_IEventLog), _referer(kpi_App->referer())
	{
	}

private:
	// a touch keep-alive: (re)stamp a soft-state lease for each STORE the orchestrator named. We act only
	// on participants of THIS reflector's transport ("tcp+l4end"); the rest belong to other reflectors.
	bool _handler_msgCtrl(IPSME_MsgEnv::t_MSG msg, JSON_MsgCtrl json_msgCtrl)
	{
		DebugPrint("%s: [%s]\n", __func__, json_msgCtrl.to_string().c_str());

		JSON::JSON_ json_ctrl = json_msgCtrl["ctrl-msg"];
		int64_t i64_ttl_msec = json_ctrl["ttl_msec"].get<int64_t>();

		JSON::JSON_ json_participants = json_ctrl["participants"];
		for (size_t i = 0; i < json_participants.size(); i++)
		{
			JSON::JSON_ json_participant = json_participants[i];

			std::string str_protocol = json_participant["protocol"].get<std::string>();

			// interest management: serve only this build's transport protocol (kpsz_PROTOCOL_).
			if (str_protocol != kpsz_PROTOCOL_) {
				DebugPrint("%s: skip participant proto[%s] -- not %s\n", __func__, str_protocol.c_str(), kpsz_PROTOCOL_);
				continue;
			}

			std::string str_address = json_participant["address"].get<std::string>();
			std::string str_id      = json_participant["identification"]["id"].get<std::string>();

			printf("%s: touch served [%s] proto[%s] id[%s] ttl_msec[%lld]\n",
			       __func__, str_address.c_str(), str_protocol.c_str(), str_id.c_str(), (long long)i64_ttl_msec);

			// hand the lease to the App, which owns the asio transport: it dials a connection to the
			// store on first touch and keeps the existing one alive on subsequent touches.
			if (_kpi_App)
				_kpi_App->touch(str_address, str_id, i64_ttl_msec);
		}

		return true;
	}

public:
	bool handler_json_msg(IPSME_MsgEnv::t_MSG msg, JSON::JSON_Msg json_msg)
	{
#if defined(ROLE_SERVER)
		// server role: this reflector is dialed-INTO (it accepts connections), it does not dial out, so
		// a touch ctrl-msg is not actionable here -- do nothing on it for now (drop, per interest mgmt).
		(void)msg; (void)json_msg;
		return false;
#else
		if (JSON_MsgCtrl::validate(json_msg) && _handler_msgCtrl(msg, json_msg))
			return true;

		return false;
#endif
	}

private:
	IPSME_MsgEnv * const _kp_IPSME;
	Interface_App * const _kpi_App;
	IEventLog * const _kp_IEventLog;
	const JSON::JSON_Msg::Referer _referer;
};
