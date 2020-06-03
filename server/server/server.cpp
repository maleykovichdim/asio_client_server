// server.cpp : boost asio server (sync server,actions: 
// 1. read int, save in container unique int, calculate int average, write average to client, next turn...  
// 2. dump unique ints
//

////////////  SETTINGS  ///////////////
//for boost asio version
#define _WIN32_WINNT 0x0601
//name file for dump
#define FILE_DUMP "../recieved.binary"
//server port
#define PORT 8001
//time out millisec
#define TIME_OUT  5000   
//time for dump in binary file sec.
#define DUMP_TIME  5  //sec
//range of digits from 0 to 1023
constexpr int num_digits = 1024;

///////////////////////////////////////

#include <iostream>
#include <fstream>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>

////////////// LOG INCLUDE
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

#include <thread>
#include <future>
#include <mutex>

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace sinks = boost::log::sinks;
namespace keywords = boost::log::keywords;
using namespace boost::asio;
using namespace boost::posix_time;
using boost::system::error_code;
namespace pt = boost::posix_time;
using namespace std;

////////////////BOOST TRIVIAL LOG INIT ///////////////////////////////////////////
void init_log()
{
	auto sink_console = logging::add_console_log(std::cout);
	auto sink_file = logging::add_file_log
	(
		keywords::file_name = "../asio_server.log",                                        /*< file name pattern >*/
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
//////////////////////////////////////////////////////////////////////////


///////////////  SERVER   ////////////////////////////////////////////////



//auxiliary class for server
class recieved_client  {//: boost::enable_shared_from_this<recieved_client>
public:
	recieved_client(io_service& service)
		: sock_(ip::tcp::socket(service)), last_ping_(microsec_clock::local_time()), error_(0)	{};

	ip::tcp::socket& sock() { return sock_; }
	ptime& last_ping() { return last_ping_; }
	int& error() { return error_; }

	bool timed_out() const {
		ptime now = microsec_clock::local_time();
		long long ms = (now - last_ping_).total_milliseconds();
		return ms > TIME_OUT;
	}
	bool is_to_remove() const { return (timed_out() | (error_ != 0));  };
	void update()  { 	last_ping_ = microsec_clock::local_time(); 	}

private:
	ip::tcp::socket sock_;
	ptime last_ping_;
	int error_;
};


typedef boost::shared_ptr<recieved_client> client_ptr;


//main class for server
class Server {
public:

	typedef std::vector<client_ptr> array_;

///////////////////////////////////////////////////////////////////////////////////////////////////////

	Server(int port, int logPeriod) :port(port), logPeriod(logPeriod), counter(0), summ(0) {
		timer = new boost::asio::deadline_timer(service_timer, boost::posix_time::seconds(logPeriod));
		memset(values, 0, sizeof(int));
	};

	~Server() { 
		if (timer) delete timer;
		BOOST_LOG_TRIVIAL(info) << "~Server: released ";
	};

///////////////////////////////////////////////////////////////////////////////////////////////////////

	//method for periodic dump into file
	void tick() {
		try {

			timer->expires_at(timer->expires_at() + (boost::posix_time::seconds(logPeriod)));

			//write recieved digits to dump file
			fstream digits(FILE_DUMP, ios::binary | ios::trunc | ios::out);
			//fstream digits(FILE_DUMP,  ios::trunc | ios::out); for check
			for (int i = 0; i < num_digits; i++) {
				if (values[i])
					digits.write((char*)(&i), sizeof(int));
					//digits.write((char*)(std::to_string(i).c_str()), (std::to_string(i)).size()); //for check

			}
			digits.close();

			timer->async_wait(std::bind(&Server::tick, this));
			BOOST_LOG_TRIVIAL(info) << "Server: dump file: "<< FILE_DUMP <<" created ";			
			service_timer.run();
		}
		catch (...) {
			BOOST_LOG_TRIVIAL(info) << "Server: exception during  dump file creating ";
		};
	}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	// main run - method
	void run() {
		results_async_accept = async(std::launch::async, &Server::accept_thread, this);//recieve connection
		results_async_process = async(std::launch::async, &Server::handle_clients_thread, this);//wrapping for process client
		results_async_timer = async(std::launch::async,
			[&] { BOOST_LOG_TRIVIAL(info) << "Server: dump timer turn on;"; timer->async_wait(std::bind(&Server::tick, this)); service_timer.run(); });//timer for periodic dump
	}

	//acception of clients
	void accept_thread() {
		ip::tcp::acceptor acceptor(service, ip::tcp::endpoint(ip::tcp::v4(), port));
		while (true) {
			client_ptr new_(new recieved_client(service));
			acceptor.accept(new_->sock());
			const std::lock_guard<std::mutex> lock(mutex_);
			clients.push_back(new_);
		}
	}

	//processing of all registered cliens
	void handle_clients_thread() {
		while (true) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			const std::lock_guard<std::mutex> lock(mutex_);
			if (clients.size() == 0) continue;
			for (array_::iterator b = clients.begin(), e = clients.end(); b != e; ++b) process_client(*b);
			// erase clients that timed out or have error
			clients.erase(std::remove_if(clients.begin(), clients.end(),
				boost::bind(&recieved_client::is_to_remove, _1)), clients.end());
		}
	}


	//whole process for one client
	void process_client(client_ptr&  rc) {
		int res = 0;
		const int size_message = 5;
		char answer[5];
		answer[4] = '\n';
		if (!rc->sock().available()) {
			return;
		}
		if (rc->error()) {
			BOOST_LOG_TRIVIAL(info) << "Server: clients data has error ";
			return;
		}
		try {
			size_t len = rc->sock().read_some(buffer(buff_read));
			if (len == size_message && buff_read[4] == '\n') {
				int value = *((int*)buff_read); //recieved int value
				if (value >= 0 && value < num_digits)
				{
					if (values[value] == 0) {//save unique int
						values[value] = 1;
						counter++;
						summ += value * value; //sum unique
						average = summ / counter;
					}
				}
				else
				{
					BOOST_LOG_TRIVIAL(warning) << "Server: client wrong message, nimber is not in range ";
					rc->error() = 1;//to remove client
				}

				*((int*)answer) = average;
				rc->sock().write_some(buffer(answer));//write average to client
				rc->update();
			}
			else
			{
				rc->error() = 1;//to remove client
				BOOST_LOG_TRIVIAL(error) << "Server: client wrong message, incorrect length of message";
			}
		}
		catch (...)
		{
			rc->error() = 1;//to remove client
			BOOST_LOG_TRIVIAL(error) << " Server: Exception during answer to client ";
		}
	}


protected:
	std::future<void> results_async_accept;
	std::future<void> results_async_process;
	std::future<void> results_async_timer;

	io_service service;// for read /write
	io_service service_timer; //for timer
	int port;//port for socket

	char buff_read[1024];

	//time period && timer for dump file
	boost::asio::deadline_timer * timer;
	int logPeriod;

	//fast container for recieved digits
	int values[num_digits];
	int counter;
	int summ;
	int average;

	//  cliets:
	array_ clients;
	// thread-safe access to clients array
	std::mutex mutex_;
};




int main(void) {

	init_log();
	logging::add_common_attributes();
	using namespace logging::trivial;
	src::severity_logger< severity_level > lg;

	BOOST_LOG_TRIVIAL(error) << "Server: start";
	Server server(PORT, DUMP_TIME);//PORT - socket port, DUMP_TIME - time sec. for log period
	server.run();
	while (true)
		std::this_thread::sleep_for(std::chrono::seconds(1000));
	return 0;

}

