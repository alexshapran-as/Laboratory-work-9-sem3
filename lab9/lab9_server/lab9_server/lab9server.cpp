#ifdef WIN32
#define _WIN32_WINNT 0x0501
#include <stdio.h>
#endif


#include <iostream>
#include <fstream>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <clocale>
#include <boost/lexical_cast.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/ref.hpp>
using namespace boost::asio;
using namespace boost::posix_time;
typedef boost::asio::ip::tcp btcp;
namespace bfs = boost::filesystem;
typedef boost::format bfmt;
std::ifstream ifs;
io_service service;
static unsigned int index = { 0 }; // Индекс/id подключенного клиента

#define MEM_FN(x)       boost::bind(&self_type::x, shared_from_this())
#define MEM_FN1(x,y)    boost::bind(&self_type::x, shared_from_this(),y)
#define MEM_FN2(x,y,z)  boost::bind(&self_type::x, shared_from_this(),y,z)


class talk_to_client : public boost::enable_shared_from_this<talk_to_client>, boost::noncopyable
{
	typedef talk_to_client self_type;
	talk_to_client() : sock_(service), started_(false) {}
public:
	typedef boost::system::error_code error_code;
	typedef boost::shared_ptr<talk_to_client> ptr;

	void start()
	{
		started_ = true;
		do_read();
	}
	static ptr new_()
	{
		ptr new_(new talk_to_client);
		return new_;
	}
	void stop()
	{
		if (!started_) return;
		started_ = false;
		sock_.close();
	}
	ip::tcp::socket & sock() { return sock_; }
private:
	void on_read(const error_code & err, size_t bytes)
	{
		if (!err)
		{
			index++;
			std::cout << std::endl << "Работа с клиентом id = " << index << std::endl;
			std::string msg(read_buffer_, bytes);
			if (msg.size() > 1023)
			{
				std::cout << "[-] Слишком длинный путь! Ваше соединение прервано." << std::endl;
				stop();
				return;
			}
			do_write(msg + "\n");
		}
		stop();
	}

	void on_write(const error_code & err, size_t bytes)
	{
		do_read();
	}
	void do_read()
	{
		async_read(sock_, buffer(read_buffer_), MEM_FN2(read_complete, _1, _2), MEM_FN2(on_read, _1, _2));
	}
	void do_write(const std::string & msg)
	{
		system("chcp 65001");
		using std::cout;
		using std::endl;
		//std::copy(msg.begin(), msg.end(), write_buffer_); // копирование msg в write_buffer, msg хранит путь к файлу и символы конца сообщения
		std::string temp; // временная строка для получения только строки, содержащей путь
		for (unsigned int i = { 0 }; i < msg.size() - 2; ++i)
		{
			temp.push_back(msg[i]);
		}
		bfs::path filepath = temp; // путь 
		ifs.open(temp, std::ios::binary);  // открытие файла, который пытается получить клиент
		if (!ifs.is_open())
		{
			cout << endl << bfmt("[-] Ошибка открытия файла\n") << endl;
			stop();
			system("pause");
			return;
		}

		uintmax_t fileSize = bfs::file_size(temp); // получение размера файла, путь к которому указан в строке temp
		size_t sentFileName; // отправленное имя файла
		size_t sentFileSize; // отправленный размер файла
		uintmax_t sentFileBody; // размер прочитанных данных
		size_t fileBufSize; // размер буфера
		
		cout << endl << bfmt("Размер файла: %1%") % fileSize << endl;
		sentFileName = boost::asio::write(sock_, boost::asio::buffer("Имя файла: " + temp + "\r\n"));//test1.txt")); //"FileName: " + filepath.filename().string() + "\r\n"));
		sentFileSize = boost::asio::write(sock_, boost::asio::buffer("Размер файла: " + boost::lexical_cast<std::string>(fileSize) + "\r\n\r\n")); //("FileSize: 6\r\n\r\n")); // 
		sentFileBody = 0;
		fileBufSize = sizeof(write_buffer_);
		//sentFileSize = write(sock_, boost::asio::buffer("Размер файла: " + boost::lexical_cast<std::string>(fileSize) + "\r\n\r\n")); //("FileSize: 6\r\n\r\n")); // 
		while (ifs)
		{
			ifs.read(write_buffer_, fileBufSize); // чтение из потока fileBufSize символов и запись их в массив write_buf
			sentFileBody += boost::asio::write(sock_, boost::asio::buffer(write_buffer_, ifs.gcount())); // функция асинхронной записи в поток и подсчет записанных данных
		}
		if (sentFileBody != fileSize) 
		{ 
			std::cerr << "[-] Запись прервалась!\n"; 
			stop();
			system("pause");
			return;
		}
		//cout << endl << bfmt("Имя отправленного файла = %1%, Размер отправленного файла = %2%, Размер записанных данных = %3%") % sentFileName % sentFileSize % sentFileBody << endl;
		ifs.close();
		sock_.shutdown(btcp::socket::shutdown_both);
		sock_.close(); 
		try
		{
			MEM_FN2(on_write, _1, _2);
		}
		catch (std::exception const& e)
		{
			cout << endl << bfmt(e.what()) << endl;
			system("pause");
			return;
		}
		/*ifs.close();
		sock_.shutdown(btcp::socket::shutdown_both);
		sock_.close();*/

	}
	size_t read_complete(const boost::system::error_code & err, size_t bytes)
	{
		if (err) return 0;
		bool found = std::find(read_buffer_, read_buffer_ + bytes, '\n') < read_buffer_ + bytes;
		// Последовательное небуфферизированное чтение до \n
		return found ? 0 : 1;
	}
private:
	ip::tcp::socket sock_;
	enum { max_msg = 1024 };
	char read_buffer_[max_msg]; // буфер, содержащий путь к копируемому файлу
	char write_buffer_[max_msg]; // буфер для записи данных файла и их последующая передача клиенту
	bool started_;
};

ip::tcp::acceptor acceptor(service, ip::tcp::endpoint(ip::tcp::v4(), 8001));
// Серверные TCP функции в asio выполняет объект класса boost::asio::ip::tcp::acceptor
// Собственно этот объект открывает соединение

void handle_accept(talk_to_client::ptr client, const boost::system::error_code & err)
{
	client->start();
	talk_to_client::ptr new_client = talk_to_client::new_();
	acceptor.async_accept(new_client->sock(), boost::bind(handle_accept, new_client, _1));
}


int main()
{
	system("chcp 65001");
	std::cout << std::endl << "*** Лабораторная работа №9: Реализация сервера утилиты rcp ***" << std::endl;
	talk_to_client::ptr client = talk_to_client::new_();
	acceptor.async_accept(client->sock(), boost::bind(handle_accept, client, _1));
	// Метод accept объекта класса acceptor с объектом класса socket переданным в качестве параметра, 
	// начинает ожидание подключения, что при синхронной схеме работы приводит к блокированию потока до тех пор, 
	// пока какой-нибудь клиент не осуществит подключения
	service.run();

	return 0;
}
