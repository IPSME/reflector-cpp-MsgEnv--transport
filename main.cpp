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
#include "cpp-asio.git/asio_client.h"

// the asio TCP transport; created in main(), reached by the MQTT-side callback below
asio_client* g_ptr_asio = nullptr;

// the bridge's IPSME; wired up in main() from the bridge
IPSME_MsgEnv* g_ptr_ipsme = nullptr;

duplicate g_duplicate;

static constexpr const char* kpsz_PARTICIPANT_ = "reflector-MQTT--asio-client";
static constexpr const char* kpsz_INSTANCE_ = "a46aab17-9833-499b-93df-8814a96d2da1";

// reflector server we bridge MQTT <-> TCP with
static constexpr const char*    kpsz_REFLECTOR_ADDRESS = "127.0.0.1";
static constexpr unsigned short kus_REFLECTOR_PORT     = 4999;

//----------------------------------------------------------------------------------------------------------------
// asio -> MQTT : a complete message arrived from the peer; dedup, publish

void on_asio_read(std::string str_msg)
{
	if (true == g_duplicate.exists(str_msg)) {
		std::cerr << "asio ->| *DUP -- [" << str_msg << "]" << std::endl;
		return;
	}

	std::cerr << "asio -> MQTT -- [" << str_msg << "]" << std::endl;

	if (g_ptr_ipsme)
		g_ptr_ipsme->publish(str_msg.c_str());
}

//----------------------------------------------------------------------------------------------------------------
// MQTT -> asio : hand the message to the transport (it frames it)

class App : public Interface_App {
public:
	App(const JSON::JSON_Msg::Referer& referer) : Interface_App(referer) {}

	bool handler_string_(const char* psz_msg, std::string str_msg)
	{
		std::cerr << "MQTT -> asio -- [" << str_msg << "]" << std::endl;

		g_duplicate.cache(str_msg, t_entry_context(30s));

		if (g_ptr_asio)
			g_ptr_asio->write(str_msg);

		return true;
	}
};

//----------------------------------------------------------------------------------------------------------------
// pragma mark asio -> MQTT
/*
enum { max_length = 0xffff };
char ach_recvd_[max_length];

std::vector<char> vch_buffer_;

void do_read(t_ptr_socket ptr_socket)
{
	// std::cerr << "do_read(): \n";

	ptr_socket->async_read_some(asio::buffer(ach_recvd_, max_length), [ptr_socket](std::error_code ec, std::size_t length)
		{
			if (ec) {
				std::cerr << "do_read: ERROR! " << ec << std::endl;

				if (ptr_socket->is_open()) {
					ptr_socket->close();
					g_set_sockets.erase(ptr_socket);
				}
				return;
			}

			// std::cerr << "async_read_some(): " << msg_ << "\n";

			l4end::deframe(&vch_buffer_, ach_recvd_, length, [](std::string str_msg)
				{
					if (true == g_duplicate.exists(str_msg))
						std::cerr << "do_read: asio ->| *DUP -- [" << str_msg << "]" << std::endl;

					else
					{
						std::cerr << "do_read: asio -> MQTT -- [" << str_msg << "]" << std::endl;

						g_ptr_ipsme->publish(str_msg.c_str());
					}
				});

			do_read(ptr_socket);
		});
}

//----------------------------------------------------------------------------------------------------------------
// pragma mark asio <- MQTT

void do_write(t_ptr_socket ptr_socket, std::string str_msg)
{
	// str_out gets deleted before the write callback causing a memory crash in the string lib
	// std::string* str_out= l4end::enframe(str_msg);
	std::string* pstr_out = new std::string(l4end::enframe(str_msg));

	asio::async_write(*ptr_socket, asio::buffer(*pstr_out), [ptr_socket, pstr_out](std::error_code ec, std::size_t / *length* /)
		{
			if (ec) {
				std::cerr << "do_write: ERROR! " << ec << std::endl;

				if (ptr_socket->is_open()) {
					ptr_socket->close();
					g_set_sockets.erase(ptr_socket);
				}
			}
			delete pstr_out;
		});
}

bool handler_string_(const char* psz_msg, std::string str_msg)
{
	std::cerr << "handle_str_: asio[" << g_set_sockets.size() << "] <- MQTT -- [" << str_msg.c_str() << "]" << std::endl;

	g_duplicate.cache(str_msg, t_entry_context(30s));

	for (auto it : g_set_sockets)
		do_write(it, str_msg);

	return true;
}
*/
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
    mosquitto_lib_init();

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
        printf("Running ...\n");

		std::unique_ptr<App> uptr_app = std::make_unique<App>(JSON::JSON_Msg::Referer(kpsz_PARTICIPANT_, kpsz_INSTANCE_));

		printf("Running ...\n");

		std::unique_ptr<InMemoryMsg> _uptr_eventLog = std::make_unique<InMemoryMsg>();
		auto bridge= IPSME_Bridge::get_instance(uptr_app.get(), _uptr_eventLog.get());

        // TCP side: start with no connections (connections configured later)

        while (!gb_quit_)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Sleep for 100 milliseconds

            bridge->process_msgs();
        }
    }
    catch (...) {
        std::cout << "main(): unknown exception" << std::endl;
    }

    mosquitto_lib_cleanup();

    printf("Exit!\n");
    return 0;
}
