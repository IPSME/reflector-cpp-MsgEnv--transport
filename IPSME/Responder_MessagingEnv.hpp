
#pragma once

#include <cassert>
#include <cstdint>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "../g_.hpp"
#include "../cpp-EventLog.git/IEventLog.hpp"
#include "../generated/interface-MessagingEnv.h"

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
	// generated-interface types, scoped to this responder (no namespace leak into includers)
	using JSON_MsgCtrl = reflector_iface::MessagingEnv::JSON_MsgCtrl;
	using JSON_AckCtrl = reflector_iface::MessagingEnv::JSON_AckCtrl;

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

			// the participant IS the Discovery-svc cxn-pt { protocol, address, port }: dial address:port, and
			// derive the connection/lease key as protocol://address:port (no separate identification on the
			// wire). port is a string|integer union (ConnectionPoint schema) -- normalise it to a string.
			std::string str_host  = json_participant["address"].get<std::string>();
			auto        json_port = json_participant["port"];
			std::string str_port  = json_port.is_string() ? json_port.get<std::string>() : std::to_string(json_port.get<int64_t>());
			std::string str_address = str_host + ":" + str_port;
			std::string str_id      = str_protocol + "://" + str_address;

			printf("%s: touch served [%s] proto[%s] id[%s] ttl_msec[%lld]\n",
			       __func__, str_address.c_str(), str_protocol.c_str(), str_id.c_str(), (long long)i64_ttl_msec);

			// hand the lease to the App, which owns the asio transport: it dials a connection to the store on
			// first touch and keeps the existing one alive on subsequent touches. A NEW dial (touch()==true)
			// parks a pending ack for this store; emit_acks() publishes it once the dial SETTLES, so the
			// orchestrator releases its parked query only when the store is actually reachable. Renewals
			// (touch()==false) draw no ack -- they add no new connection.
			if (_kpi_App && _kpi_App->touch(str_address, str_id, i64_ttl_msec)) {
				// slice to the base nlohmann::json explicitly: constructing a json member from a value DERIVED
				// from json (JSON_MsgCtrl / JSON_) otherwise binds json's serializing ctor, not its copy ctor
				// (which throws type_error.302).
				_map_pending_ack[str_id] = Pending{
					static_cast<const nlohmann::json&>(json_msgCtrl),
					static_cast<const nlohmann::json&>(json_participant)
				};
			}
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

	// Publish the "connections handled" ack for any pending new-dial whose connection has now SETTLED. Driven
	// from the main loop (once per tick). Per store: one JSON_AckCtrl, _cause = the causing touch, and
	// ack.participants = [that store's participant]. The orchestrator releases its parked query per ack and
	// dedups downstream; a store that never connects simply never acks (its query TTL-expires there).
	void emit_acks()
	{
#if !defined(ROLE_SERVER)
		for (auto it = _map_pending_ack.begin(); it != _map_pending_ack.end(); ) {
			if (_kpi_App && _kpi_App->is_connected(it->first)) {
				_publish_ack(it->second.json_touch, it->second.json_participant);
				it = _map_pending_ack.erase(it);
			}
			else
				++it;
		}
#endif
	}

private:
	// build + publish JSON_AckCtrl: _cause = the whole causing touch (standing convention); ack{ok:true} from
	// the base, then add the handled participant + the MessagingEnv version so it validates as a JSON_AckCtrl.
	void _publish_ack(const nlohmann::json& json_touch, const nlohmann::json& json_participant)
	{
		JSON::JSON_Ack json_ack = JSON::JSON_Ack::create_with_cause_Ok(json_touch, true);
		json_ack["ack"]["participants"] = nlohmann::json::array({ json_participant });
		json_ack["MessagingEnv"] = reflector_iface::MessagingEnv::VERSION;

		JSON_AckCtrl json_ackCtrl(json_ack);
		assert(JSON_AckCtrl::validate(json_ackCtrl));
		PUBLISH(json_ackCtrl);
	}

	// a new-dial awaiting its connect to settle before we ack the causing touch.
	struct Pending {
		nlohmann::json json_touch;         // the causing ctrl-msg -> _cause of the ack
		nlohmann::json json_participant;   // this store's participant record -> echoed in ack.participants
	};
	// store id -> its pending ack; added on a new dial, erased once emitted (its connection settled).
	std::map<std::string, Pending> _map_pending_ack;

	IPSME_MsgEnv * const _kp_IPSME;
	Interface_App * const _kpi_App;
	IEventLog * const _kp_IEventLog;
	const JSON::JSON_Msg::Referer _referer;
};
