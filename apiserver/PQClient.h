#ifndef SRC_REST_PQCLIENT_H
#define SRC_REST_PQCLIENT_H

#include <mutex>
#include <condition_variable>
#include <libpq-fe.h>
#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/posix/stream_descriptor.hpp>
#include <string>
#include <vector>

using boost::asio::io_context;
using boost::asio::strand;
using std::string;
using std::uint16_t;

namespace Rest
{

  class RestServer;

  class PQClient : public std::enable_shared_from_this<PQClient>
  {
    io_context &m_ioc;
    strand<io_context::executor_type> &m_strand;
    string m_dbname, m_user, m_password, m_host;
    uint16_t m_port;
    PGconn *m_conn = nullptr;
    int m_sock = 0;
    boost::asio::posix::stream_descriptor m_pgSocket;
    int m_rowCount = 0;

  public:
    PQClient(io_context &ioc,
             strand<io_context::executor_type> &strand,
             const string &dbname,
             const string &user,
             const string &password,
             const string &host, uint16_t port);
    ~PQClient();

  public:
    void asyncQuery(const std::string &query, std::function<void(PGresult *)> callback);
    void asyncParamQuery(
        const std::string &query,
        // const std::vector<const char *> &params,
        const std::vector<std::string> &paramStrings,
        const std::vector<int> &paramLengths,
        const std::vector<int> &paramFormats,
        std::function<void(PGresult *)> callback);

    void stop()
    {
      m_pgSocket.cancel();
    }

  private:
    void handleError(const string &message, const string &errorMessage);
    void handleError(const string &message);

    void waitForResult(std::function<void(PGresult *)> callback);
  };

}

#endif /* SRC_REST_PQCLIENT_H */