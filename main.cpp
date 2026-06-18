// reflector-cpp-MQTT--asio-client : bridges an MQTT message environment with a TCP (asio) reflector server.
//

#include <cassert>
#include <signal.h>
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <chrono>
using namespace std::chrono_literals;
#include <iostream>
#include <thread>
#include <vector>

#pragma comment(lib, "wininet.lib")

#include "msg_cache-dedup.h"

#include "IPSME/IPSME_Bridge.hpp"
#include "cpp-EventLog.git/InMemory_EventLog.h"
#if defined(ROLE_SERVER)
  #include "cpp-asio.git/asio_server.h"
  using transport_t = asio_server;
#else
  #include "cpp-asio.git/asio_client.h"
  using transport_t = asio_client;
#endif

// the asio TCP transport; created in main(), reached by the MQTT-side callback below
transport_t* g_ptr_asio = nullptr;

duplicate g_duplicate;

#ifndef BUILD_NAME
#define BUILD_NAME "reflector"
#endif
#ifndef BUILD_TRANSPORT
#define BUILD_TRANSPORT "transport"
#endif
#ifndef BUILD_MSGENV
#define BUILD_MSGENV "MsgEnv"
#endif
static constexpr const char* kpsz_PARTICIPANT_ = BUILD_NAME;          // == build NAME (-DNAME)
static const std::string     ks_INSTANCE_      = JSON::gen_uuid();    // fresh random GUID per run

// reflector server we bridge MQTT <-> TCP with
static constexpr const char*    kpsz_REFLECTOR_ADDRESS = "127.0.0.1";
static constexpr unsigned short kus_REFLECTOR_PORT     = 4999;

//----------------------------------------------------------------------------------------------------------------

class App : public Interface_App {
public:
	App(const JSON::JSON_Msg::Referer& referer) : Interface_App(referer) {}

	//-------------
	// transport -> MsgEnv : a complete message arrived from the peer; dedup, publish
	void on_transport_read(std::string str_msg)
	{
        auto bridge = IPSME_Bridge::get_instance();

		if (true == g_duplicate.exists(str_msg)) {
			std::cerr << BUILD_TRANSPORT " ->| *DUP -- [" << str_msg << "]" << std::endl;
			return;
		}

		std::cerr << BUILD_TRANSPORT " -> " BUILD_MSGENV " -- [" << str_msg << "]" << std::endl;

        bridge->get_IPSME()->publish(str_msg.c_str());
	}

	//-------------
	// MsgEnv -> transport : hand the message to the transport (it frames it)
	bool on_MsgEnv_msg(const char* psz_msg, std::string str_msg)
	{
		std::cerr << BUILD_MSGENV " -> " BUILD_TRANSPORT " -- [" << str_msg << "]" << std::endl;

		g_duplicate.cache(str_msg, t_entry_context(30s));

		if (g_ptr_asio)
			g_ptr_asio->write(str_msg);

		return true;
	}

};

//----------------------------------------------------------------------------------------------------------------
// mark main()

bool gb_quit_ = false;

void handler_sigint_(int s)
{
    printf("\nCaught SIG[%d]\n", s);

    // if the user presses ^C twice, then just exit.
    if (gb_quit_)
        exit(s);

    gb_quit_ = true;
}

int main()
{
#ifdef POSIX_SIGNAL
    struct sigaction sa;
    sa.sa_handler = handler_sigint_;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
#else
    //signal(SIGINT, handler_sigint_);
#endif

    try {
		std::cerr << __func__ << ": " << "Connecting ..." << std::endl;

		std::unique_ptr<App> uptr_app = std::make_unique<App>(JSON::JSON_Msg::Referer(kpsz_PARTICIPANT_, ks_INSTANCE_));

		std::unique_ptr<InMemoryMsg> _uptr_eventLog = std::make_unique<InMemoryMsg>();
		auto bridge= IPSME_Bridge::get_instance(uptr_app.get(), _uptr_eventLog.get());

        // TCP side
#if defined(ROLE_SERVER)
        transport_t transport(kus_REFLECTOR_PORT,
            [app = uptr_app.get()](std::string m){ app->on_transport_read(std::move(m)); });   // listen + accept
        g_ptr_asio = &transport;
        transport.start();

		std::cout << __func__ << ": " << kpsz_PARTICIPANT_ << " listening on [" << kus_REFLECTOR_PORT << "]" << std::endl;

		std::cerr << __func__ << ": " << "Running ..." << std::endl;
#else
        // client: start with no connections (configured later)
#endif

        while (!gb_quit_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Sleep for 100 milliseconds

            bridge->process_msgs();
        }
    }
    catch (...) {
        std::cout << "main(): unknown exception" << std::endl;
    }

    printf("Exit!\n");
    return 0;
}
