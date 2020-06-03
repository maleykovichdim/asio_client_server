// client.cpp : boost asio client
// 1. write integer from [0,1023] range, read int average from server , next turn....

#define _WIN32_WINNT 0x0601
//server port
#define PORT 8001
#define HOST "127.0.0.1"

#define WAIT_PERION  2000  //milli sec
#define NUM_ATTENTIONS_TO_GET_DATA_FROM_SERVER 5

//range of digits from 0 to 1023
constexpr int num_digits = 1024;

#include <iostream>
#include <string>
#include <thread>
#include <future>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>

#include <locale>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <locale>


namespace pt = boost::posix_time;
namespace logging = boost::log;
namespace src = boost::log::sources;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;
using namespace boost::asio;
using boost::system::error_code;
using namespace boost::posix_time;
using namespace std;

////////////////BOOST TRIVIAL LOG INIT ///////////////////////////////////////////
void init_log()
{
	auto sink_console = logging::add_console_log(std::cout);
	auto sink_file = logging::add_file_log
	(
		keywords::file_name = "../asio_clients.log",                                        /*< file name pattern >*/
		keywords::rotation_size = 10 * 1024 * 1024,                                   /*< rotate files every 10 MiB... >*/
		keywords::auto_flush = true,
		keywords::time_based_rotation = sinks::file::rotation_at_time_point(0, 0, 0), /*< ...or at midnight >*/
		keywords::format = "[%TimeStamp%]: %Message%"                                 /*< log record format >*/
	);
	pt::time_facet *facet = new boost::posix_time::time_facet("%Y--%m--%d %H:%M:%S");
	sink_file->imbue(std::locale(sink_file->getloc(), facet));
	sink_console->imbue(std::locale(sink_console->getloc(), facet));
	logging::core::get()->set_filter
	(
		logging::trivial::severity >= logging::trivial::info
	);
}

//////////////////////// client class ///////////////////////////////////////////////

class Client {
public:

	Client(string ip_adress, int port){
		ep = ip::tcp::endpoint(ip::address::from_string(ip_adress), port);
		memset(buf, 0, sizeof(char) * 1024);
	};

	~Client() {
		BOOST_LOG_TRIVIAL(info) << "~Client: realised ";
	}

	// run client in separate thread
	void run_in_separate_thread_async() {
		results = async(std::launch::async, &Client::sync_echo, this);
	}

	//main method like run()
	void sync_echo() {
		int counter_exception = 0;
		const int size_message = 5;
		char message_to_server[size_message];
		message_to_server[4] = '\n';
		BOOST_LOG_TRIVIAL(info) << "Client start ";

		while (true) {
			ip::tcp::socket sock(service);
			int error = 0;
			sock.connect(ep);
			int wait_step = 3;
			while (true) {
				try
				{

					*((int*)message_to_server) = rand() % num_digits;//generate random int
					sock.write_some(buffer(message_to_server));// 
					size_t len = sock.read_some(buffer(buf));//read average from server
					if (len != size_message || buf[4] != '\n')
					{
						error = 1;
						BOOST_LOG_TRIVIAL(error) << "client: error of answer " << *((int*)buf) << " len = " << len;
					}
				}
				catch (...)
				{
					BOOST_LOG_TRIVIAL(error) << " client: Exception during access to server ";
					if ((counter_exception++) > NUM_ATTENTIONS_TO_GET_DATA_FROM_SERVER) error = 1;
					std::this_thread::sleep_for(std::chrono::milliseconds(WAIT_PERION));
				}				
				if (error) break;

			}//while
			sock.close();
		}//while
	}

protected:
	io_service service;
	ip::tcp::endpoint ep;
	char buf[1024];
	std::future<void> results;
};




int main(int argc, char* argv[]) {

	init_log();
	logging::add_common_attributes();
	using namespace logging::trivial;
	src::severity_logger< severity_level > lg;

	std::vector<shared_ptr<Client>> clients;
	// connect 30 clients in separated threads
	for (int a = 0; a < 30; a++) {
		BOOST_LOG_TRIVIAL(info) << " Clients: " << a <<" created";
		shared_ptr<Client> client = std::make_shared<Client>(HOST, PORT);
		client->run_in_separate_thread_async();
		clients.push_back(client);
	}
	std::this_thread::sleep_for(std::chrono::milliseconds(1000*60*20));//20 min
	BOOST_LOG_TRIVIAL(info) << "---------- Clients finished -------------";
}

