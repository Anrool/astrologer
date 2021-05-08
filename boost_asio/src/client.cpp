#include <common.h>

#include <atomic>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/noncopyable.hpp>

//#define PRINT_STATUS

namespace astrologer {

constexpr const char *CLIENT_TYPE_STAR = "star";
constexpr const char *CLIENT_TYPE_HUMAN = "human";
constexpr const char *CLIENT_TYPE_MULTICLIENT = "multi";

using boost::asio::ip::tcp;

class Star : private boost::noncopyable {
public:
  Star(boost::asio::io_context &io_context,
       const tcp::resolver::results_type &endpoints, const std::string &zodiac,
       const std::string &prognosis, const std::string &remainder,
       std::function<void()> onWorkDoneCb)
      : mStrand{boost::asio::make_strand(io_context)}, mSocket{mStrand},
        mDeadline{mStrand}, mPrognosis{prognosis}, mOnWorkDoneCb{onWorkDoneCb} {
    std::ostream os{&mBuffer};
    os << STAR_REQUEST_TYPE << zodiac << remainder << DELIM;

    boost::asio::async_connect(mSocket, endpoints,
                               boost::bind(&Star::handleConnect, this,
                                           boost::asio::placeholders::error));
  }

private:
  void stop() {
#ifdef PRINT_STATUS
    std::cout << "Disconnecting from: " << mSocket.remote_endpoint()
              << std::endl;
#endif

    boost::system::error_code ignoredEc;

    mSocket.shutdown(tcp::socket::shutdown_both, ignoredEc);
    mSocket.close(ignoredEc);
    mDeadline.cancel(ignoredEc);
  }

  bool stopped() const { return !mSocket.is_open(); }

  void handleConnect(const boost::system::error_code &ec) {
    if (ec) {
      std::cerr << "Failed to connect, error: " << ec.message() << std::endl;
      return;
    }

#ifdef PRINT_STATUS
    std::cout << "Connected with: " << mSocket.remote_endpoint() << std::endl;
#endif

    mDeadline.expires_after(durationType{WriteRequestTimeout});
    boost::asio::async_write(mSocket, mBuffer,
                             boost::bind(&Star::handleWriteRequest, this,
                                         boost::asio::placeholders::error));

    startWaitDeadline();
  }

  void handleWriteRequest(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec) {
      std::cerr << "Failed to send a request, error: " << ec.message()
                << std::endl;

      stop();
      return;
    }

    std::ostream os{&mBuffer};
    os << mPrognosis << DELIM;

    mDeadline.expires_after(durationType{WritePrognosisTimeout});
    boost::asio::async_write(mSocket, mBuffer,
                             boost::bind(&Star::handleWritePrognosis, this,
                                         boost::asio::placeholders::error));
  }

  void handleWritePrognosis(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec) {
      std::cerr << "Failed to send a prognosis, error: " << ec.message()
                << std::endl;

      stop();
      return;
    }

    mDeadline.expires_after(durationType{ReadResponseTimeout});
    boost::asio::async_read_until(
        mSocket, mBuffer, DELIM,
        boost::bind(&Star::handleReadResponse, this,
                    boost::asio::placeholders::error));
  }

  void handleReadResponse(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec) {
      std::cerr << "Failed to read a response, error: " << ec.message()
                << std::endl;
    } else {
      std::istream is{&mBuffer};
      std::string response;
      std::getline(is, response, DELIM);

      if (response == STAR_RESPONSE_SUCCESS) {
        mOnWorkDoneCb();
      } else if (response == STAR_RESPONSE_FAILURE) {
        std::cout << STAR_RESPONSE_FAILURE << std::endl;
      } else {
        std::cerr << "Bad response: " << response << std::endl;
      }
    }

    stop();
  }

  void startWaitDeadline() {
    mDeadline.async_wait(boost::bind(&Star::handleDeadline, this,
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

  boost::asio::strand<boost::asio::io_context::executor_type> mStrand;

  tcp::socket mSocket;
  boost::asio::streambuf mBuffer;

  boost::asio::steady_timer mDeadline;

  const std::string mPrognosis;

  const std::function<void()> mOnWorkDoneCb;
};

class Human : private boost::noncopyable {
public:
  Human(boost::asio::io_context &io_context,
        const tcp::resolver::results_type &endpoints, const std::string &zodiac,
        const std::string &remainder,
        std::function<void(const std::string &)> onWorkDoneCb)
      : mStrand{boost::asio::make_strand(io_context)}, mSocket{mStrand},
        mDeadline{mStrand}, mOnWorkDoneCb{onWorkDoneCb} {
    std::ostream os{&mBuffer};
    os << CLIENT_REQUEST_TYPE << zodiac << remainder << DELIM;

    boost::asio::async_connect(mSocket, endpoints,
                               boost::bind(&Human::handleConnect, this,
                                           boost::asio::placeholders::error));
  }

private:
  void stop() {
#ifdef PRINT_STATUS
    std::cout << "Disconnecting from: " << mSocket.remote_endpoint()
              << std::endl;
#endif

    boost::system::error_code ignoredEc;

    mSocket.shutdown(tcp::socket::shutdown_both, ignoredEc);
    mSocket.close(ignoredEc);
    mDeadline.cancel(ignoredEc);
  }

  bool stopped() const { return !mSocket.is_open(); }

  void handleConnect(const boost::system::error_code &ec) {
    if (ec) {
      std::cerr << "Failed to connect, error: " << ec.message() << std::endl;
      return;
    }

#ifdef PRINT_STATUS
    std::cout << "Connected with: " << mSocket.remote_endpoint() << std::endl;
#endif

    mDeadline.expires_after(durationType{WriteRequestTimeout});
    boost::asio::async_write(mSocket, mBuffer,
                             boost::bind(&Human::handleWriteRequest, this,
                                         boost::asio::placeholders::error));

    startWaitDeadline();
  }

  void handleWriteRequest(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec) {
      std::cerr << "Failed to send a request, error: " << ec.message()
                << std::endl;

      stop();
      return;
    }

    mDeadline.expires_after(durationType{ReadResponseTimeout});
    boost::asio::async_read_until(
        mSocket, mBuffer, DELIM,
        boost::bind(&Human::handleReadResponse, this,
                    boost::asio::placeholders::error));
  }

  void handleReadResponse(const boost::system::error_code &ec) {
    if (stopped()) {
      return;
    }

    if (ec) {
      std::cerr << "Failed to read a response, error: " << ec.message()
                << std::endl;
    } else {
      std::istream is{&mBuffer};
      std::string response;
      std::getline(is, response, DELIM);

      if (response == UNKNOWN_PROGNOSIS) {
        std::cout << UNKNOWN_PROGNOSIS << std::endl;
      } else {
        mOnWorkDoneCb(response);
      }
    }

    stop();
  }

  void startWaitDeadline() {
    mDeadline.async_wait(boost::bind(&Human::handleDeadline, this,
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

  boost::asio::strand<boost::asio::io_context::executor_type> mStrand;

  tcp::socket mSocket;
  boost::asio::streambuf mBuffer;

  boost::asio::steady_timer mDeadline;

  const std::function<void(const std::string &)> mOnWorkDoneCb;
};

class JoinThreads : private boost::noncopyable {
public:
  explicit JoinThreads(std::vector<std::thread> &threads) : mThreads{threads} {}

  ~JoinThreads() {
    for (std::thread &t : mThreads) {
      if (t.joinable()) {
        t.join();
      }
    }
  }

private:
  std::vector<std::thread> &mThreads;
};

void printUsage(const char *binaryPath) {
  std::cerr << "Usage:\n"
            << " " << binaryPath << " " << CLIENT_TYPE_STAR
            << " <address> <port> <zodiac> <prognosis> [<remainder>]\n"
            << " " << binaryPath << " " << CLIENT_TYPE_HUMAN
            << " <address> <port> <zodiac> [<remainder>]\n"
            << " " << binaryPath << " " << CLIENT_TYPE_MULTICLIENT
            << " <address> <port> <human_amount>\n"
            << "Zodiacs:\n";

  for (const std::string &z : ZodiacList()) {
    std::cerr << " " << z << std::endl;
  }
}

int StarMain(int argc, char *argv[]) {
  enum : int {
    BinaryIndex,
    TypeIndex,
    AddressIndex,
    PortIndex,
    ZodiacIndex,
    PrognosisIndex,
    RemainderIndex,
    MinArgsCount = RemainderIndex,
    MaxArgsCount,
  };

  const char *remainder = "";

  if (argc != MinArgsCount) {
    if (argc != MaxArgsCount) {
      printUsage(argv[BinaryIndex]);
      return EXIT_FAILURE;
    }

    remainder = argv[RemainderIndex];
  }

  const char *zodiac = argv[ZodiacIndex];
  const char *prognosis = argv[PrognosisIndex];

  try {
    boost::asio::io_context io_context;

    const tcp::resolver::results_type endpoints =
        tcp::resolver{io_context}.resolve(argv[AddressIndex], argv[PortIndex]);

    const auto cb = [zodiac, prognosis] {
      std::cout << "Set prognosis: \"" << zodiac << "\" - \"" << prognosis
                << "\"\n";
    };

    Star star{io_context, endpoints, zodiac, prognosis, remainder, cb};
    io_context.run();
  } catch (std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int HumanMain(int argc, char *argv[]) {
  enum : int {
    BinaryIndex,
    TypeIndex,
    AddressIndex,
    PortIndex,
    ZodiacIndex,
    RemainderIndex,
    MinArgsCount = RemainderIndex,
    MaxArgsCount,
  };

  const char *remainder = "";

  if (argc != MinArgsCount) {
    if (argc != MaxArgsCount) {
      printUsage(argv[BinaryIndex]);
      return EXIT_FAILURE;
    }

    remainder = argv[RemainderIndex];
  }

  const char *zodiac = argv[ZodiacIndex];

  try {
    boost::asio::io_context io_context;

    const tcp::resolver::results_type endpoints =
        tcp::resolver{io_context}.resolve(argv[AddressIndex], argv[PortIndex]);

    const auto cb = [zodiac](const std::string &response) {
      std::cout << "Got prognosis: \"" << zodiac << "\" - \"" << response
                << "\"\n";
    };

    Human human{io_context, endpoints, zodiac, remainder, cb};
    io_context.run();
  } catch (std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

template <typename... Ts>
bool SetAllZodiacs(boost::asio::io_context &io_context,
                   const tcp::resolver::results_type &endpoints,
                   const std::string &valueSuffix) {
  const std::vector<std::string> &zodiacs = ZodiacList();
  const char *remainder = "";
  std::size_t zodiacCounter = 0;
  const auto starCb = [&zodiacCounter]() { zodiacCounter++; };

  std::vector<std::shared_ptr<Star>> stars;
  stars.reserve(zodiacs.size());

  for (const std::string &z : zodiacs) {
    stars.emplace_back(std::make_shared<Star>(
        io_context, endpoints, z, z + valueSuffix, remainder, starCb));
  }

  io_context.run();
  io_context.restart();

  if (zodiacCounter != stars.size()) {
    std::cerr << "Failed to set all prognosis, only " << zodiacCounter
              << " was sent\n";
    return false;
  }

  std::cout << "Each zodiac has got a prognosis\n";
  return true;
}

std::vector<std::shared_ptr<Human>>
CreateHumans(std::size_t amount, boost::asio::io_context &io_context,
             const tcp::resolver::results_type &endpoints,
             const std::string &valueSuffix,
             std::atomic<std::size_t> &humanCounter) {
  std::vector<std::shared_ptr<Human>> humans;
  humans.reserve(amount);

  const char *remainder = "";
  const std::vector<std::string> &zodiacs = ZodiacList();

  for (std::size_t i = 0; i < amount; i++) {
    const std::string &zodiac = zodiacs[i % zodiacs.size()];

    const auto humanCb = [&zodiac, &valueSuffix,
                          &humanCounter](const std::string &response) {
      if (response == zodiac + valueSuffix) {
        humanCounter++;
      } else {
        std::cerr << "Wrong prognosis: \"" << zodiac << "\" - \"" << response
                  << "\"\n";
      }
    };

    humans.emplace_back(std::make_shared<Human>(
        io_context, endpoints, zodiac.c_str(), remainder, humanCb));
  }

  return humans;
}

void ParallelRun(std::size_t amount, boost::asio::io_context &io_context) {
  std::promise<void> startPromise;
  std::shared_future<void> startFuture = startPromise.get_future().share();

  const auto threadFunc = [](std::shared_future<void> startFuture,
                             boost::asio::io_context *io_context) {
    startFuture.wait();

    try {
      io_context->run();
    } catch (std::exception &e) {
      std::cerr << "Processing exception: " << e.what() << std::endl;
    }
  };

  std::vector<std::thread> contextThreads(amount);
  JoinThreads joiner{contextThreads};

  for (std::thread &t : contextThreads) {
    t = std::thread{threadFunc, startFuture, &io_context};
  }

  startPromise.set_value();
}

int PollAllZodiacs(std::size_t amount, boost::asio::io_context &io_context,
                   const tcp::resolver::results_type &endpoints,
                   const std::string &valueSuffix) {
  std::atomic<std::size_t> humanCounter{0};
  std::vector<std::shared_ptr<Human>> humans =
      CreateHumans(amount, io_context, endpoints, valueSuffix, humanCounter);

  ParallelRun(amount, io_context);

  if (humanCounter != humans.size()) {
    std::cerr << "Only " << humanCounter << "/" << humans.size()
              << " verified prognosis\n";
    return EXIT_FAILURE;
  }

  std::cout << "Prognosis for each zodiac has been verified\n";
  return EXIT_SUCCESS;
}

int MulticlientMain(int argc, char *argv[]) {
  enum : int {
    BinaryIndex,
    TypeIndex,
    AddressIndex,
    PortIndex,
    AmountIndex,
    ArgsCount,
  };

  if (argc != ArgsCount) {
    printUsage(argv[BinaryIndex]);
    return EXIT_FAILURE;
  }

  std::size_t amount = 0;

  if (!Convert(argv[AmountIndex], amount)) {
    return EXIT_FAILURE;
  }

  try {
    boost::asio::io_context io_context;

    const tcp::resolver::results_type endpoints =
        tcp::resolver{io_context}.resolve(argv[AddressIndex], argv[PortIndex]);

    const std::string valueSuffix = " value";

    if (!SetAllZodiacs(io_context, endpoints, valueSuffix)) {
      return EXIT_FAILURE;
    }

    return PollAllZodiacs(amount, io_context, endpoints, valueSuffix);
  } catch (std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

} // namespace astrologer

int main(int argc, char *argv[]) {
  using astrologer::CLIENT_TYPE_HUMAN;
  using astrologer::CLIENT_TYPE_MULTICLIENT;
  using astrologer::CLIENT_TYPE_STAR;
  using astrologer::HumanMain;
  using astrologer::MulticlientMain;
  using astrologer::printUsage;
  using astrologer::StarMain;

  enum : int { BinaryIndex, TypeIndex, MinArgsCount };

  const char *binaryPath = argv[BinaryIndex];

  if (argc < MinArgsCount) {
    printUsage(binaryPath);
    return EXIT_FAILURE;
  }

  const std::string type = argv[TypeIndex];

  if (type == CLIENT_TYPE_STAR) {
    return StarMain(argc, argv);
  }

  if (type == CLIENT_TYPE_HUMAN) {
    return HumanMain(argc, argv);
  }

  if (type == CLIENT_TYPE_MULTICLIENT) {
    return MulticlientMain(argc, argv);
  }

  printUsage(binaryPath);
  return EXIT_FAILURE;
}
