#include <common.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/core/noncopyable.hpp>

//#define PRINT_STATUS
//#define PRINT_REMAINDER

namespace astrologer {

using boost::asio::ip::tcp;

class PrognosisManager : private boost::noncopyable {
public:
  bool getPrognosis(const std::string &zodiac, std::string &prognosis) const {
    std::lock_guard<decltype(mPrognosisMutex)> lock{mPrognosisMutex};
    const auto zodiacIt = mPrognosisPerZodiac.find(zodiac);

    if (zodiacIt == std::end(mPrognosisPerZodiac)) {
      return false;
    }

    prognosis = zodiacIt->second;
    return true;
  }

  void setPrognosis(const std::string &zodiac, const std::string &prognosis) {
    std::lock_guard<decltype(mPrognosisMutex)> lock{mPrognosisMutex};
    mPrognosisPerZodiac[zodiac] = prognosis;
  }

private:
  mutable std::mutex mPrognosisMutex;
  std::map<std::string, std::string> mPrognosisPerZodiac;
};

class Connection : public std::enable_shared_from_this<Connection>,
                   private boost::noncopyable {
public:
  Connection(boost::asio::io_context &io_context,
             std::shared_ptr<PrognosisManager> prognosisManager)
      : mPrognosisManager{prognosisManager}, mSocket{io_context},
        mDeadline{io_context} {}

  tcp::socket &socket() { return mSocket; }

  void start() {
#ifdef PRINT_STATUS
    std::cout << "Connected with: " << mSocket.remote_endpoint() << std::endl;
#endif

    startReadRequest();
    startWaitDeadline();
  }

private:
  void stop() {
#ifdef PRINT_STATUS
    std::cerr << "Disconnecting from: " << mSocket.remote_endpoint()
              << std::endl;
#endif

    boost::system::error_code ignoredEc;

    mSocket.shutdown(tcp::socket::shutdown_both, ignoredEc);
    mSocket.close(ignoredEc);
    mDeadline.cancel(ignoredEc);
  }

  bool stopped() const { return !mSocket.is_open(); }

  void startReadRequest() {
    mDeadline.expires_after(durationType{ReadRequestTimeout});
    boost::asio::async_read_until(
        mSocket, mBuffer, DELIM,
        boost::bind(&Connection::handleReadRequest, shared_from_this(),
                    boost::asio::placeholders::error));
  }

  void handleReadRequest(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec) {
      if (ec == boost::asio::error::eof) {
#ifdef PRINT_CONNECTION_STATUS
        std::cout << "Connection was handled\n";
#endif
      } else {
        std::cerr << "Failed to read a request, error: " + ec.message()
                  << std::endl;
      }

      stop();
      return;
    }

    std::istream is{&mBuffer};
    std::string request;
    std::getline(is, request, DELIM);

    if (StartsWith(request, STAR_REQUEST_TYPE)) {
      const auto zodiac = std::make_shared<std::string>();

      if (!parseZodiac(request.c_str() + STAR_REQUEST_TYPE.size(), *zodiac)) {
        return;
      }

      mDeadline.expires_after(durationType{ReadPrognosisTimeout});
      boost::asio::async_read_until(
          mSocket, mBuffer, DELIM,
          boost::bind(&Connection::handleReadPrognosis, shared_from_this(),
                      boost::asio::placeholders::error, zodiac));
    } else if (StartsWith(request, CLIENT_REQUEST_TYPE)) {
      std::string zodiac;

      if (!parseZodiac(request.c_str() + CLIENT_REQUEST_TYPE.size(), zodiac)) {
        return;
      }

      std::string response;

      if (mPrognosisManager->getPrognosis(zodiac, response)) {
#ifdef PRINT_STATUS
        std::cout << "Found prognosis: \"" << zodiac << "\" - \"" << response
                  << "\"\n";
#endif
      } else {
#ifdef PRINT_STATUS
        std::cout << "Absent prognosis for \"" << zodiac << "\"\n";
#endif

        response = UNKNOWN_PROGNOSIS;
      }

      std::ostream os{&mBuffer};
      os << response << DELIM;

      mDeadline.expires_after(durationType{WriteResponseTimeout});
      boost::asio::async_write(mSocket, mBuffer,
                               boost::bind(&Connection::handleWriteResponse,
                                           shared_from_this(),
                                           boost::asio::placeholders::error));
    } else {
      std::cerr << "Bad request: " << request << std::endl;

      stop();
    }
  }

  void handleReadPrognosis(const boost::system::error_code &ec,
                           std::shared_ptr<std::string> zodiac) {
    if (stopped()) {
      return;
    }

    if (ec) {
      std::cerr << "Failed to read a prognosis, error: " << ec.message()
                << std::endl;

      stop();
      return;
    }

    std::istream is{&mBuffer};
    std::string prognosis;
    std::getline(is, prognosis, DELIM);

    mPrognosisManager->setPrognosis(*zodiac, prognosis);

#ifdef PRINT_STATUS
    std::cout << "New prognosis: \"" << *zodiac << "\" - \"" << prognosis
              << "\"\n";
#endif

    std::ostream os{&mBuffer};
    os << STAR_RESPONSE_SUCCESS << DELIM;

    mDeadline.expires_after(durationType{WriteResponseTimeout});
    boost::asio::async_write(mSocket, mBuffer,
                             boost::bind(&Connection::handleWriteResponse,
                                         shared_from_this(),
                                         boost::asio::placeholders::error));
  }

  void handleWriteResponse(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec) {
      std::cerr << "Failed to send a response, error: " << ec.message();

      stop();
      return;
    }

#ifdef PRINT_STATUS
    std::cout << "Response was sent\n";
#endif

    startReadRequest();
  }

  bool parseZodiac(const char *zodiacWithRem, std::string &zodiac) {
    if (!FindZodiac(zodiacWithRem, zodiac)) {
      stop();
      return false;
    }

#ifdef PRINT_REMAINDER
    zodiacWithRem += zodiac.size();

    if (*zodiacWithRem) {
      std::cout << "Request remainder: \"" << zodiacWithRem << "\"\n";
    }
#endif

    return true;
  }

  void startWaitDeadline() {
    mDeadline.async_wait(boost::bind(&Connection::handleDeadline,
                                     shared_from_this(),
                                     boost::asio::placeholders::error));
  }

  void handleDeadline(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec == boost::asio::error::operation_aborted) {
      startWaitDeadline();
    } else {
      std::cerr << "Operation timeout\n";

      stop();
    }
  }

  std::shared_ptr<PrognosisManager> mPrognosisManager;

  tcp::socket mSocket;
  boost::asio::streambuf mBuffer;

  boost::asio::steady_timer mDeadline;
};

class Server : private boost::noncopyable {
public:
  explicit Server(unsigned short port)
      : mAcceptor{mIo_context, tcp::endpoint{tcp::v4(), port}},
        mPrognosisManager{std::make_shared<PrognosisManager>()} {
    startAccept();
  }

  void run() { mIo_context.run(); }

private:
  void startAccept() {
    const auto newConnection =
        std::make_shared<Connection>(mIo_context, mPrognosisManager);

    mAcceptor.async_accept(newConnection->socket(),
                           boost::bind(&Server::handleAccept, this,
                                       boost::asio::placeholders::error,
                                       newConnection));
  }

  void handleAccept(const boost::system::error_code &ec,
                    std::shared_ptr<Connection> newConnection) {
    if (ec) {
      std::cerr << "Failed to accept, error: " << ec.message() << std::endl;
    } else {
      newConnection->start();
    }

    startAccept();
  }

  boost::asio::io_context mIo_context;
  tcp::acceptor mAcceptor;

  std::shared_ptr<PrognosisManager> mPrognosisManager;
};

} // namespace astrologer

int main(int argc, char *argv[]) {
  enum : int { BinaryIndex, PortIndex, ArgsCount };

  if (argc != ArgsCount) {
    std::cerr << "Usage: " << argv[BinaryIndex] << " <port>\n";
    return EXIT_FAILURE;
  }

  unsigned short port = 0;

  if (!astrologer::Convert(argv[PortIndex], port)) {
    return EXIT_FAILURE;
  }

  try {
    astrologer::Server server{port};
    server.run();
  } catch (std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
