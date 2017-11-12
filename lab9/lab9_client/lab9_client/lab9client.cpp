#ifdef WIN32
#define _WIN32_WINNT 0x0501
#include <stdio.h>
#endif

#include <iostream>
#include <fstream>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/bind.hpp>
#include <boost/regex.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>
#include <stdexcept>
typedef boost::asio::ip::tcp btcp;
typedef boost::format bfmt;
using namespace boost::asio;
io_service service; //  объект класса, который предоставляет программе связь с нативными объектами ввода/вывода

#define MEM_FN(x)       boost::bind(&self_type::x, shared_from_this())  //  эти функции связывается с переданным адресом
#define MEM_FN1(x,y)    boost::bind(&self_type::x, shared_from_this(),y)
#define MEM_FN2(x,y,z)  boost::bind(&self_type::x, shared_from_this(),y,z)

class talk_to_svr : public boost::enable_shared_from_this<talk_to_svr>
	, boost::noncopyable
{
	typedef talk_to_svr self_type;
	talk_to_svr(const std::string & filepath)
		: sock_(service), started_(true), message_(filepath), istream_(&streambuf_) {}
	void start(ip::tcp::endpoint ep)
	{
		sock_.async_connect(ep, MEM_FN1(on_connect, _1)); // Эта функция асинхронно подключается по данному адресу
	}
public:
	typedef boost::system::error_code error_code;
	typedef boost::shared_ptr<talk_to_svr> ptr;

	static ptr start(ip::tcp::endpoint ep, const std::string & filepath)
	{
		ptr new_(new talk_to_svr(filepath));
		new_->start(ep);
		return new_;
	}
	void stop()
	{
		if (!started_) return;
		started_ = false;
		sock_.close();
	}
	bool started() { return started_; }
private:
	void on_connect(const error_code & err)
	{
		if (!err)
			do_write(message_ + "\n");
		else
			stop();
	}
	void on_write(const error_code & err, size_t bytes)
	{
		do_read();
	}
	void do_read()
	{
		try
		{	
			async_read_until(sock_, streambuf_, "\r\n\r\n", MEM_FN2(handlereaduntil, _1, _2)); // Функция асинхронного чтения из потока до \r\n\r\n
		}
		catch (std::exception const& e)
		{
			std::cout << std::endl << bfmt(e.what()) << std::endl; // Вывод ошибки в формате boost
			system("pause");
			return;
		}
	}
	void do_write(const std::string & msg)
	{
		if (!started()) return;
		try
		{
			std::copy(msg.begin(), msg.end(), write_buffer_);
			sock_.async_write_some(buffer(write_buffer_, msg.size()), MEM_FN2(on_write, _1, _2)); // эта функция запускает операцию асинхронной передачи данных из буфера
		}
		catch (std::exception const& e)
		{
			std::cout << std::endl << bfmt(e.what()) << std::endl;
			system("pause");
			return;
		}
	}

private:
	ip::tcp::socket sock_; // сокет - связь между  сервером и клиентом
	enum { max_msg = 10240 };
	
    char read_buffer_[max_msg]; // буфер для чтения символов из файла, которые передаст сервер
	boost::asio::streambuf streambuf_; // буфер данных для потока, представляет собой интерфейс для непосредственной записи и чтения данных
	std::istream istream_; // объект класса istream, который реализует потоковый ввод
	uintmax_t filesize_; // Размер полученного от сервера файла
	uintmax_t dumped; // Показывает размер захваченных данных
	std::string message_; // переданное сообщение серверу, содержащее путь к копируемому файлу 
	std::ofstream ofs;  // объект класса ostream, который реализует потоковый вывод
	static const boost::regex regFileName, regFileSize; // регулярные выражения для имени и размера файла
	boost::smatch res; // хранит результат поиска по регулярным выражениям

	char write_buffer_[max_msg]; // буфер записи
	bool started_; // переменная для определения начала работы с сервером

	void handlereadend(boost::system::error_code const& errcode, size_t bytes_transferred)
	{
		ofs.close();
		stop();
	}


	void handlereaduntil(boost::system::error_code const& errcode, size_t bytes_transferred)
	{
		using std::cout;
		using std::endl;
		system("chcp 65001");
		try
		{
			if (errcode)
			{
				cout << endl << "[-] Ошибка асинхронного чтения!" << endl;
				stop();
				return;
			}
			std::string headers;
			std::string tmp;
			while (std::getline(istream_, tmp) && tmp != "\r") // Пока запись в tmp из потока istream проходит успешно и при этом tmp != \r
			{
				headers += (tmp + '\n');
			}
			//cout << endl << bfmt("Headers:\n%1%\n") % headers << endl; // для просмотра того, что было прочитано из потока
			if (!boost::regex_search(headers, res, regFileSize)) // поиск среди записанного из потока размера файла и запись результата в res
				throw std::runtime_error("[-] Ошибка определения размера файла.\n");
			filesize_ = boost::lexical_cast<uintmax_t>(res[1]);
			cout << endl << bfmt("Размер файла: %1%") % filesize_ << endl;
			if (!boost::regex_search(headers, res, regFileName))
				throw std::runtime_error("[-] Ошибка определения имени файла.\n");
			message_ = res[1];
			cout << endl << bfmt("Имя файла: %1%") % message_ << endl;
			ofs.open(message_.c_str(), std::ios::binary);
			if (!ofs.is_open())
				throw std::runtime_error((bfmt("[-] Ошибка открытия файла %1%") % message_).str());
			dumped = 0;
			cout << endl << "streambuf.size() = " << streambuf_.size() << endl;
			if (streambuf_.size())
			{
				dumped += streambuf_.size();
				ofs << &streambuf_; // Запись данных в файл
			}
			sock_.async_read_some(boost::asio::buffer(read_buffer_, max_msg), MEM_FN2(handlereadend, _1, _2)); // эта функция запускает асинхронную
																												// операцию получения данных от сокета
		}
		catch (std::exception const& e)
		{
			cout << endl << bfmt(e.what()) << endl;
			stop();
			delete this;
		}
	}
};

boost::regex const talk_to_svr::regFileName("Имя файла: *(.+?)\r\n");
boost::regex const talk_to_svr::regFileSize("Размер файла: *(\\d+?)\r\n");

int main()
{
	// Подключение клиентов
	system("chcp 65001");
	std::cout << std::endl << "*** Лабораторная работа №9: Реализация клиента, использующего утилиту rcp ***" << std::endl;
	std::string filepath;
	ip::tcp::endpoint ep(ip::address::from_string("127.0.0.1"), 8001); // конечная точка, аргументы - адрес и порт (лок.пк "127.0.0.1") (192.168.1.235) (Саня 10.50.1.189)
	std::cout << std::endl << "Введите путь к файлу для копирования: ";
	SetConsoleCP(1251);
	SetConsoleOutputCP(1251);
	getline(std::cin, filepath);
	SetConsoleCP(65001);
	SetConsoleOutputCP(65001);
	int pos = filepath.find(".");
	if (filepath.size() > 10230)
	{
		std::cout << std::endl << "[-] Введено слишком большое сообщение! Ваше соединение закрыто.";
		system("pause");
		return 1;
	}
	else if ( pos == -1 || filepath[pos + 1] == ' ')
	{
		std::cout << std::endl << "[-] Название файла должно содержать его расширение." << std::endl;
		system("pause");
		return 1;
	}
	talk_to_svr::start(ep, filepath);
	boost::this_thread::sleep(boost::posix_time::millisec(100));
	service.run(); // Операция запуска (он ждет все сокеты, контролирует операции чтения/записи и найдя хотя бы одну такую операцию начинает обрабатывать ее)
	std::cout << std::endl;
	system("pause");
	return 0;
}
