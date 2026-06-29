
#pragma once

#include <nlohmann/json.hpp>

#include "../cpp-msgenv-MQTT.git/IPSME_MsgEnv.h"
#include "../cpp-json-msg.git/json_msg+ack.h"

#define PUBLISH(msg_obj)                                     \
    do {                                                     \
        msg_obj.referer(_referer.PARTICIPANT,                \
                        _referer.INSTANCE);                  \
        std::string msg_str = msg_obj.to_string();           \
        printf("%s: publish(%s): [%s]\n",                    \
               __func__,                                     \
               CLASS_NAME(msg_obj).c_str(),                  \
               msg_str.c_str());                             \
        _kp_IPSME->publish(msg_str.c_str());                 \
    } while (0)

#define PUBLISH3(msg_obj, p_msg_str)                         \
    do {                                                     \
        msg_obj.referer(_referer.PARTICIPANT,                \
                        _referer.INSTANCE);                  \
        *p_msg_str = msg_obj.to_string();                    \
        printf("%s: publish(%s): [%s]\n",                    \
               __func__,                                     \
               CLASS_NAME(msg_obj).c_str(),                  \
               (*p_msg_str).c_str());                        \
        _kp_IPSME->publish((*p_msg_str).c_str());            \
    } while (0)

#include "../Interface_App.hpp"
#include "../cpp-EventLog.git/InMemory_EventLog_ext.hpp"

#include "Responder_Discovery.hpp"

class IPSME_Bridge {
public:
    static std::shared_ptr<IPSME_Bridge> get_instance(Interface_App * const pi_App, IEventLog * const p_IEventLog) {
        std::call_once(_init_flag, [&]() {
            _instance = std::shared_ptr<IPSME_Bridge>(new IPSME_Bridge(pi_App, p_IEventLog));
            });
        return _instance;
    }

    static std::shared_ptr<IPSME_Bridge> get_instance() {
        if (! _instance) {
            throw std::runtime_error("IPSME_Bridge has not been initialized yet.");
        }
        return _instance;
    }

    static void destroy_instance() {
        _instance.reset();
    }

    IPSME_Bridge(Interface_App * const kpi_App, IEventLog * const kp_IEventLog)
        : _uptr_IPSME(std::make_unique<IPSME_MsgEnv>()),
        _kpi_App(kpi_App),
        _kp_IEventLog(kp_IEventLog),
        _responder_Discovery(std::make_unique<Responder_Discovery>(_uptr_IPSME.get(), kpi_App, kp_IEventLog))
    {
        _idx_evt_i= 0;

        bool b_ret = _uptr_IPSME->subscribe(&_handler_static, NULL);
        assert(b_ret);

        _responder_Discovery->announce();
    }

    ~IPSME_Bridge()
    {
        _uptr_IPSME->unsubscribe(&_handler_static);
    }

    IEventLog * const get_EventLog() const { return _kp_IEventLog; }
    IPSME_MsgEnv * const get_IPSME() const { return _uptr_IPSME.get(); }
    Responder_Discovery * const protocol_Discovery() const { return _responder_Discovery.get(); }

public:
    bool handler_json_msgAck(IPSME_MsgEnv::t_MSG msg, JSON::JSON_MsgAck json_msgAck)
    {
        // printf("%s: [%s]\n", __func__, json_msgAck_msg.to_string().c_str());

        //if (_protocol_Discovery->handler_json_msgAck_msg(msg, json_msgAck_msg))
        //    return true;

        return false;
    }

    bool handler_json_msgEffect(IPSME_MsgEnv::t_MSG msg, JSON::JSON_MsgEffect json_msgEffect)
    {
        if (JSON::JSON_MsgAck::validate(json_msgEffect) && handler_json_msgAck(msg, json_msgEffect))
            return true;

        // printf("%s: [%s]\n", __func__, json_msg_msg.to_string().c_str());

        //if (_protocol_Discovery->handler_json_msg_msg(msg, json_msg_msg))
        //    return true;

        return false;
    }

    bool handler_json_msg(IPSME_MsgEnv::t_MSG msg, JSON::JSON_Msg json_msg)
    {
        if (JSON::JSON_MsgEffect::validate(json_msg) && handler_json_msgEffect(msg, json_msg))
            return true;

        // printf("%s: [%s]\n", __func__, json_msg.to_string().c_str());

        if (_responder_Discovery->handler_json_msg(msg, json_msg))
            return true;

        return false;
    }

    bool handler_json(IPSME_MsgEnv::t_MSG msg, nlohmann::json json)
    {
        if (JSON::JSON_Msg::validate(json) && handler_json_msg(msg, json))
            return true;

        // printf("%s: [%s]\n", __func__, json.dump().c_str());

        return false;
    }

    bool handler_string(IPSME_MsgEnv::t_MSG msg, std::string str_msg)
    {
        nlohmann::json json= nlohmann::json::parse(str_msg, nullptr, false);
        if (! json.is_discarded() && handler_json(msg, json))
            return true;

        // -----

        // printf("%s: [%s]\n", __func__, str_msg.c_str());

        // test strings here ...

        return false;
    }

    void handler(IPSME_MsgEnv::t_MSG msg, void*)
    {
        // reverse path (ME -> transport): forward every received ME message to the
        // transport via the App. (on_transport_read = dedup+publish; on_MsgEnv_msg =
        // cache + write to the asio transport.)
        if (_kpi_App)
            _kpi_App->on_MsgEnv_msg(msg, std::string(msg));

        try {
            if (handler_string(msg, std::string(msg)))
                return;
        }
        catch (...) {
            printf("ERR: error is message execution\n");
            return;
        }

        // drop silently ...
        // printf("%s: DROP! [%s]\n", __func__, psz_msg);
    }

public:
    void process_msgs()
    {
        // printf("%s: _uptr_IPSME->process_msgs()\n", __func__);
        _uptr_IPSME->process_msgs();

        for (IEventLog::t_const_iterator it = get_EventLog()->q_begin(); it != get_EventLog()->q_end(); ++it)
        {
            IEventLog::t_idx _idx_evt_i= it->first;

            auto ptr_evt = std::static_pointer_cast<IEvent>(get_EventLog()->get_Event(_idx_evt_i));
            assert(ptr_evt);
            printf("%s: i[%llu] [%s]\n", __func__, _idx_evt_i, ptr_evt->type.c_str());

            try {
                //if (_responder_2PhW->reprocess(ptr_evt))
                //    continue;
            }
            catch (...) {
                std::cout << "process_msgs(): unknown exception" << std::endl;
            }
        }

        get_EventLog()->purge_dequeued();
    }

private:
    inline static std::once_flag _init_flag;
    inline static std::shared_ptr<IPSME_Bridge> _instance;

    std::unique_ptr<IPSME_MsgEnv> _uptr_IPSME;

    Interface_App * const _kpi_App;

    IEventLog * const _kp_IEventLog;
    IEventLog::t_idx _idx_evt_i;

    std::unique_ptr<Responder_Discovery> _responder_Discovery;

    // this is the reason IPSME_Bridge is a singleton
    static void _handler_static(const char* psz_msg, void* p_void) {
        auto bridge = IPSME_Bridge::get_instance();
        bridge->handler(psz_msg, p_void);
    }

};
