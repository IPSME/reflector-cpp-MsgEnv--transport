// reflector-cpp-MQTT--asio-client : bridges an MQTT message environment with a TCP (asio) reflector server.
//

#include <cassert>
#include <cstdlib>
#include <signal.h>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <chrono>
using namespace std::chrono_literals;
#include <iostream>
#include <thread>
#include <vector>
#include <map>
#include <memory>

#ifdef _WIN32
#pragma comment(lib, "wininet.lib")
#endif

#include "msg_cache-dedup.h"

#include "IPSME/IPSME_Bridge.hpp"
#include "cpp-EventLog.git/Null_EventLog.hpp"
#if defined(ROLE_SERVER)
  #include "cpp-asio.git/asio_broadcast_server.h"
  using transport_t = asio_broadcast_server;
#else
  #include "cpp-asio.git/asio_tcp_client.h"
  using transport_t = asio_tcp_client;
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

#if defined(ROLE_SERVER)
		if (g_ptr_asio)
			g_ptr_asio->write(str_msg);
#else
		// fan out to every store we currently hold a connection to (the touch leases).
		for (auto& pair_conn : _map_connections)
			pair_conn.second.uptr_client->write(str_msg);
#endif

		return true;
	}

	//-------------
	// MessagingEnv touch : dial a connection to the named store on first touch; on a repeat touch the
	// existing connection is already kept alive (asio_tcp_client self-reconnects), so it is a no-op.
	void touch(const std::string& str_address, const std::string& str_id, int64_t i64_ttl_msec) override
	{
#if !defined(ROLE_SERVER)
		auto tp_expires = std::chrono::steady_clock::now() + std::chrono::milliseconds(i64_ttl_msec);

		// already connected? restamp the lease (keep-alive) -- the connection self-reconnects.
		auto it = _map_connections.find(str_id);
		if (it != _map_connections.end()) {
			it->second.tp_expires = tp_expires;
			return;
		}

		// the touch address is "host:port"; asio_tcp_client takes host + port separately.
		auto zt_colon = str_address.find(':');
		if (zt_colon == std::string::npos) {
			std::cerr << "touch: address [" << str_address << "] has no :port -- cannot dial [" << str_id << "]" << std::endl;
			return;
		}
		std::string    str_host = str_address.substr(0, zt_colon);
		unsigned short us_port  = static_cast<unsigned short>(std::atoi(str_address.substr(zt_colon + 1).c_str()));

		std::cerr << "touch: dialing store [" << str_id << "] at [" << str_host << ":" << us_port << "]" << std::endl;

		auto uptr_conn = std::make_unique<asio_tcp_client>(str_host, us_port,
			[this](std::string m){ on_transport_read(std::move(m)); });
		uptr_conn->start();
		_map_connections.emplace(str_id, Connection{ std::move(uptr_conn), tp_expires });
#endif
	}

	//-------------
	// lease sweep : drop any connection whose lease has lapsed (no touch within its ttl_msec). Called
	// from the main loop. Erasing runs the asio_tcp_client dtor, which stops the io thread + closes.
	void sweep_leases()
	{
#if !defined(ROLE_SERVER)
		auto tp_now = std::chrono::steady_clock::now();
		for (auto it = _map_connections.begin(); it != _map_connections.end(); ) {
			if (tp_now >= it->second.tp_expires) {
				std::cerr << "lease expired -> disconnecting store [" << it->first << "]" << std::endl;
				it = _map_connections.erase(it);
			}
			else
				++it;
		}
#endif
	}

#if !defined(ROLE_SERVER)
private:
	struct Connection {
		std::unique_ptr<asio_tcp_client>      uptr_client;
		std::chrono::steady_clock::time_point tp_expires;
	};

	// store_id -> its held connection + lease deadline: dialed on first touch, restamped on each touch,
	// dropped by sweep_leases() once the lease lapses.
	std::map<std::string, Connection> _map_connections;
#endif

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

int main(int argc, char* argv[])
{
    // optional first arg = <tcp-port> (the asio listener); default kus_REFLECTOR_PORT
    unsigned short us_port = kus_REFLECTOR_PORT;
    if (argc > 1)
        us_port = static_cast<unsigned short>(std::atoi(argv[1]));

#ifdef POSIX_SIGNAL
    struct sigaction sa;
    sa.sa_handler = handler_sigint_;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);   // so `docker stop` shuts the server down cleanly
#else
    //signal(SIGINT, handler_sigint_);
#endif

    try {
		std::cerr << __func__ << ": " << "Connecting ..." << std::endl;

		std::unique_ptr<App> uptr_app = std::make_unique<App>(JSON::JSON_Msg::Referer(kpsz_PARTICIPANT_, ks_INSTANCE_));

		// no event log: this reflector's only "memory" need is dedup (cpp-msg_cache-dedup / g_duplicate),
		// so pass a no-op Null_EventLog placeholder rather than an unused InMemory_EventLog.
		std::unique_ptr<Null_EventLog> _uptr_eventLog = std::make_unique<Null_EventLog>();
		auto bridge= IPSME_Bridge::get_instance(uptr_app.get(), _uptr_eventLog.get());

        // TCP side
#if defined(ROLE_SERVER)
        transport_t transport(us_port,
            [app = uptr_app.get()](std::string m){ app->on_transport_read(std::move(m)); });   // listen + accept
        g_ptr_asio = &transport;
        transport.start();

		std::cout << __func__ << ": " << kpsz_PARTICIPANT_ << " listening on [" << us_port << "]" << std::endl;

		std::cerr << __func__ << ": " << "Running ..." << std::endl;
#else
        // client: start with no connections (configured later)
#endif

        while (!gb_quit_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Sleep for 100 milliseconds

            bridge->process_msgs();
            uptr_app->sweep_leases();   // drop connections whose touch-lease has lapsed
        }
    }
    catch (...) {
        std::cout << "main(): unknown exception" << std::endl;
    }

    printf("Exit!\n");
    return 0;
}
