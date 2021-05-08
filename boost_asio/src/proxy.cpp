#include <common.h>

#include <iostream>
#include <memory>

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/noncopyable.hpp>

//#define PRINT_STATUS

namespace astrologer {

using boost::asio::ip::tcp;

class Connection : private boost::noncopyable,
                   public std::enable_shared_from_this<Connection> {
public:
  explicit Connection(boost::asio::io_context &io_context)
      : mConnectionSocket{io_context},
        mClientSocket{io_context}, mDeadline{io_context} {}

  tcp::socket &connectionSocket() { return mConnectionSocket; }

  tcp::socket &clientSocket() { return mClientSocket; }

  void start() {
#ifdef PRINT_STATUS
    std::cout << "Connected with: " << mConnectionSocket.remote_endpoint()
              << ", " << mClientSocket.remote_endpoint() << std::endl;
#endif

    startReadRequest();
    startWaitDeadline();
  }

private:
  void stop() {
#ifdef PRINT_STATUS
    std::cerr << "Disconnecting from: " << mConnectionSocket.remote_endpoint()
              << ", " << mClientSocket.remote_endpoint() << std::endl;
#endif

    boost::system::error_code ignoredEc;

    mConnectionSocket.shutdown(tcp::socket::shutdown_both, ignoredEc);
    mConnectionSocket.close(ignoredEc);
    mClientSocket.shutdown(tcp::socket::shutdown_both, ignoredEc);
    mClientSocket.close(ignoredEc);
    mDeadline.cancel(ignoredEc);
  }

  bool stopped() const {
    return !mConnectionSocket.is_open() || !mClientSocket.is_open();
  }

  void startReadRequest() {
    mDeadline.expires_after(durationType{ReadRequestTimeout});
    boost::asio::async_read_until(
        mClientSocket, mClientBuffer, DELIM,
        boost::bind(&Connection::handleReadRequest, shared_from_this(),
                    boost::asio::placeholders::error));
  }

  void handleReadRequest(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec) {
      if (ec == boost::asio::error::eof) {
#ifdef PRINT_STATUS
        std::cout << "Connection was handled\n";
#endif
      } else {
        std::cerr << "Failed to read a request, error: " << ec.message()
                  << std::endl;
      }

      stop();
      return;
    }

    std::istream is{&mClientBuffer};
    std::string request;
    std::getline(is, request, DELIM);

    if (StartsWith(request, STAR_REQUEST_TYPE)) {
#ifdef PRINT_STATUS
      std::cout << "Rejecting request: " << request << std::endl;
#endif

      mClientBuffer.consume(mClientBuffer.size());

      std::ostream os{&mClientBuffer};
      os << STAR_RESPONSE_FAILURE << DELIM;

      mDeadline.expires_after(durationType{WriteResponseTimeout});
      boost::asio::async_write(mClientSocket, mClientBuffer,
                               boost::bind(&Connection::handleWriteResponse,
                                           shared_from_this(),
                                           boost::asio::placeholders::error));
    } else {
#ifdef PRINT_STATUS
      std::cout << "Forwarding a request: " << request << std::endl;
#endif

      std::ostream os{&mConnectionBuffer};
      os << request << DELIM;

      mDeadline.expires_after(durationType{WriteRequestTimeout});
      boost::asio::async_write(mConnectionSocket, mConnectionBuffer,
                               boost::bind(&Connection::handleWriteRequest,
                                           shared_from_this(),
                                           boost::asio::placeholders::error));
    }
  }

  void handleWriteRequest(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec) {
      std::cerr << "Failed to write a request, error: " << ec.message()
                << std::endl;

      stop();
      return;
    }

    mDeadline.expires_after(durationType{ReadResponseTimeout});
    boost::asio::async_read_until(
        mConnectionSocket, mConnectionBuffer, DELIM,
        boost::bind(&Connection::handleReadResponse, shared_from_this(),
                    boost::asio::placeholders::error));
  }

  void handleReadResponse(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec) {
      std::cerr << "Failed to read a response, error: " << ec.message()
                << std::endl;

      stop();
      return;
    }

    std::istream is{&mConnectionBuffer};
    std::string response;
    std::getline(is, response, DELIM);

#ifdef PRINT_STATUS
    std::cout << "Forwarding a response: " << response << std::endl;
#endif

    std::ostream os{&mClientBuffer};
    os << response << DELIM;

    mDeadline.expires_after(durationType{WriteResponseTimeout});
    boost::asio::async_write(mClientSocket, mClientBuffer,
                             boost::bind(&Connection::handleWriteResponse,
                                         shared_from_this(),
                                         boost::asio::placeholders::error));
  }

  void handleWriteResponse(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec) {
      std::cerr << "Failed to write a response, error: " << ec.message()
                << std::endl;

      stop();
      return;
    }

#ifdef PRINT_STATUS
    std::cout << "Response was sent\n";
#endif

    startReadRequest();
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

  tcp::socket mConnectionSocket;
  boost::asio::streambuf mConnectionBuffer;

  tcp::socket mClientSocket;
  boost::asio::streambuf mClientBuffer;

  boost::asio::steady_timer mDeadline;
};

class Proxy : private boost::noncopyable {
public:
  Proxy(const char *host, const char *service, unsigned short listenPort)
      : mResolver{mIo_context}, mAcceptor{mIo_context} {
    mAcceptor.open(tcp::v4());
    mAcceptor.set_option(tcp::acceptor::reuse_address{true});
    mAcceptor.bind(tcp::endpoint{tcp::v4(), listenPort});
    mAcceptor.listen();

    mResolver.async_resolve(host, service,
                            boost::bind(&Proxy::handleResolve, this,
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::endpoint));
  }

  void run() { mIo_context.run(); }

private:
  void handleResolve(const boost::system::error_code &ec,
                     const tcp::resolver::results_type &endpoints) {
    if (ec) {
      std::cerr << "Failed to resolve, error: " << ec.message() << std::endl;
      return;
    }

    mEndpoints = endpoints;

    startConnect();
  }

  void startConnect() {
    const auto newConnection = std::make_shared<Connection>(mIo_context);

    boost::asio::async_connect(newConnection->connectionSocket(), mEndpoints,
                               boost::bind(&Proxy::handleConnect, this,
                                           boost::asio::placeholders::error,
                                           newConnection));
  }

  void handleConnect(const boost::system::error_code &ec,
                     std::shared_ptr<Connection> newConnection) {
    if (ec) {
      std::cerr << "Failed to connect, error: " << ec.message() << std::endl;
      return;
    }

    mAcceptor.async_accept(newConnection->clientSocket(),
                           boost::bind(&Proxy::handleAccept, this,
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

    startConnect();
  }

  boost::asio::io_context mIo_context;

  tcp::resolver mResolver;
  tcp::resolver::results_type mEndpoints;

  tcp::acceptor mAcceptor;
};

} // namespace astrologer

int main(int argc, char *argv[]) {
  enum : int {
    BinaryIndex,
    AddressIndex,
    PortIndex,
    ListenPortIndex,
    ArgsCount
  };

  if (argc != ArgsCount) {
    std::cerr << "Usage: " << argv[BinaryIndex]
              << " <address> <port> <listen_port>\n";
    return EXIT_FAILURE;
  }

  unsigned short listenPort = 0;

  if (!astrologer::Convert(argv[ListenPortIndex], listenPort)) {
    return EXIT_FAILURE;
  }

  try {
    astrologer::Proxy proxy{argv[AddressIndex], argv[PortIndex], listenPort};
    proxy.run();
  } catch (std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
