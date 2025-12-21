//============================================================================
// Post Rest Server
//============================================================================
#include "apiserver/RestServer.h"
#include <boost/asio/io_context.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/program_options.hpp>
#include <thread>
#include <chrono>
//#include "cookies/parse.h"
#include "routes/Routes.h"
#include "../redisPublish/Publish.h" // RedisPublish class
#include <boost/redis/src.hpp>       // boost redis implementation

namespace net = boost::asio;      // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>
using Rest::RestServer;
namespace po = boost::program_options;
using boost::asio::signal_set;

po::variables_map parse_args(int &argc, char *argv[])
{
  // Initialize the default port with the value from the "PORT" environment
  // variable or with 8888.
  auto port = [&]() -> std::uint16_t
  {
    auto env = std::getenv("PORT");
    if (env == nullptr)
      return 3011;
    auto value = std::stoi(env);
    if (value < std::numeric_limits<std::uint16_t>::min() ||
        value > std::numeric_limits<std::uint16_t>::max())
    {
      std::ostringstream os;
      os << "The PORT environment variable value (" << value
         << ") is out of range.";
      throw std::invalid_argument(std::move(os).str());
    }
    return static_cast<std::uint16_t>(value);
  }();

  // Parse the command-line options.
  po::options_description desc("Server configuration");
  desc.add_options()                                                                           //
      ("help", "produce help message")                                                         //                                                                           //
      ("address", po::value<std::string>()->default_value("0.0.0.0"), "set listening address") //
      ("port", po::value<std::uint16_t>()->default_value(port), "set listening port")          //
      ("threads", po::value<std::uint16_t>()->default_value(2), "set number threads")          //
      ("root", po::value<std::string>()->default_value("latest"), "document root folder");     //

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);
  if (vm.count("help"))
    std::cout << desc << "\n";

  return vm;
}

int main(int argc, char *argv[])
{
  // Check command line arguments.
  try
  {
    auto apidb_name = std::getenv("APIDB_NAME");
    auto apidb_user = std::getenv("APIDB_USER");
    auto apidb_password = std::getenv("APIDB_PASSWORD");
    auto apidb_host = std::getenv("APIDB_HOST");
    auto apidb_port = std::getenv("APIDB_PORT");
    auto jwt_secret_key = std::getenv("JWT_SECRET_KEY");
    auto authorised_user = std::getenv("AUTHORISED_USER");

    if (jwt_secret_key == nullptr || authorised_user == nullptr)
    {
      std::cout << "Require ENV variables set for JWT_SECRET_KEY and AUTHORISED_USER." << std::endl;
      return EXIT_FAILURE;
    }

    if (apidb_name == nullptr || apidb_user == nullptr || apidb_password == nullptr ||
        apidb_host == nullptr || apidb_port == nullptr)
    {
      std::cout << "Require ENV variables set for APIDB_NAME, APIDB_USER, APIDB_PASSWORD, APIDB_HOST and APIDB_PORT." << std::endl;
      return EXIT_FAILURE;
    }
    // check sanity of port string and value
    if (std::atoi(apidb_port) > std::numeric_limits<uint16_t>::max())
    {
      std::cout << "apidb_PORT " << apidb_port << " is out of range." << std::endl;
      return EXIT_FAILURE;
    }
    auto dbport = static_cast<uint16_t>(std::atoi(apidb_port));

    // Check all environment variables
    const char *redis_host = std::getenv("REDIS_HOST");
    const char *redis_port = std::getenv("REDIS_PORT");
    const char *redis_channel = std::getenv("REDIS_CHANNEL");
    const char *redis_password = std::getenv("REDIS_PASSWORD");

    if (!(redis_host && redis_port && redis_password && redis_channel))
    {
      std::cerr << "Environment variables REDIS_CHANNEL, REDIS_HOST, REDIS_PORT or REDIS_PASSWORD are not set." << std::endl;
      exit(1);
    }

    po::variables_map vm = parse_args(argc, argv);
    if (vm.count("help"))
      return 0;

    auto address = net::ip::make_address(vm["address"].as<std::string>());
    auto port = vm["port"].as<std::uint16_t>();
    auto threads = vm["threads"].as<std::uint16_t>();
    auto const doc_root = std::make_shared<std::string>(vm["root"].as<std::string>());
    std::cout << "Listening on " << address << ":" << port << " [Threads:" << threads << "]" << std::endl;
    std::cout << "Document root: " << *doc_root << std::endl;

    // std::unordered_map<std::string, std::string> cookies;
    // // Display parsed cookies
    // cookies = Cookies::cookie_map("username=JohnDoe; sessionid=12345;");
    // std::cout << "Cookie values" << std::endl;
    // for (const auto &[key, val] : cookies)
    // {
    //   std::cout << key << " = " << val << '\n';
    // }

    // The io_context is required for all I/O
    net::io_context ioc{threads};

    // Create and launch a listening port
    std::cout << std::endl;

    RedisPublish::Publish redisPublisher; // starts the redis publisher on a new thread and own ioc
    auto restserver = std::make_shared<RestServer>(
        ioc,
        tcp::endpoint{address, port},
        doc_root,
        std::make_shared<RedisPublish::Sender>(redisPublisher));

    restserver->get("/health", "", Routes::LivePosts::healthCheck);
    restserver->get("/api/v1/liveposts/homepage", "", Routes::LivePosts::homePage);
    restserver->get("/api/v1/liveposts/users", "", Routes::LivePosts::userList);
    restserver->get("/api/v1/liveposts/posts", "", Routes::LivePosts::fetchPosts);

    // User auth req. Create user at liveposts service for the actual logged in user.
    restserver->put("/api/v1/liveposts/posts", "*", Routes::LivePosts::createPost);
    restserver->put("/api/v1/liveposts/users", "*", Routes::LivePosts::createUser);
    restserver->get("/api/v1/liveposts/user/fetchbyauthid/{authId}", "*", Routes::LivePosts::findUserByAuthId);
    restserver->get("/api/v1/liveposts/user/fetchbyid/{id}", "*", Routes::LivePosts::findUserById);

    // NetProcessor calls to LivePost Svc. req NetProc_user authorisation from authenticated NetProc user 
    restserver->get("/api/v1/liveposts/stage/post", "netproc", Routes::LivePosts::allocatePost);
    restserver->put("/api/v1/liveposts/stage/post", "netproc", Routes::LivePosts::stagePost);

    // Begin the rest server at tcp address/port ioc context in a thread pool (no. of threads in cmd arg)
    restserver->run();

    std::cout << "\nRedis Publisher started.\n";
    std::cout << "Api server started.         \n";
    std::cout << " - ready for signal to stop.\n";

    // Setup the signals to cause end to applications
    net::signal_set signals{ioc};
    signals.add(SIGINT);
    signals.add(SIGTERM);
#if defined(SIGQUIT)
    signals.add(SIGQUIT);
#endif // defined(SIGQUIT)
    signals.async_wait(
        [&](beast::error_code const &, int)
        {
          ioc.stop();
        });

    // Run the Terminator(single thread) and I/O service on the requested number of threads
    // for the Api server routes.
    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for (auto i = threads - 1; i > 0; --i)
      v.emplace_back(
          [&ioc]
          {
            ioc.run();
          });

    // This waits until signaled and work is complete
    ioc.run();
    std::cout << "Stopping api server from signal (Signal set to also stop redis publish sender).\n";

    // Block until all the threads exit
    for (auto &t : v)
      t.join();

    std::cout << "Api server stopped.\n";
  }
  catch (const std::exception &e)
  {
    std::cout << "Except: " << e.what() << "\n";
    exit(1);
  }
  catch (const std::string &e)
  {
    std::cout << "Except: " << e << "\n";
    exit(1);
  }

  return EXIT_SUCCESS;
}
