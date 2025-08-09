#include "PQClient.h"
#include <boost/asio/bind_executor.hpp>
#include <iostream>
#include <sstream>

#ifdef NDEBUG
#define D(x)
#else
#define D(x) x
#endif

namespace Rest
{
  PQClient::PQClient(io_context &ioc,
                     strand<io_context::executor_type> &strand,
                     const string &dbname,
                     const string &user,
                     const string &password,
                     const string &host,
                     uint16_t port) : m_ioc(ioc), m_strand(strand),
                                      m_dbname{dbname}, m_user{user}, m_password{password},
                                      m_host{host}, m_port{port},
                                      m_conn{nullptr}, m_sock{0},
                                      m_pgSocket(ioc),
                                      m_rowCount{0}
  {
    // conninfo is a string of keywords and values separated by spaces.
    std::string conninfo = "dbname=" + m_dbname +
                           " user=" + m_user +
                           " password=" + m_password +
                           " host=" + m_host +
                           " port=" + std::to_string(m_port);

    try
    {
      m_conn = PQconnectStart(conninfo.c_str());
      if (!m_conn)
      {
        handleError("Failed to allocate connection object.");
      }

      // Check if the connection is successful
      if (PQstatus(m_conn) == CONNECTION_BAD)
      {
        // If not successful, print the error message and finish the connection
        handleError("Error with connecting starting to the database server", std::string(PQerrorMessage(m_conn)));
      }

      // PQsetnonblocking(m_conn, 1);
      //  Set the connection to non-blocking mode
      if (PQsetnonblocking(m_conn, 1) != 0)
      {
        handleError("Failed to set non-blocking mode: ", std::string(PQerrorMessage(m_conn)));
      }
      // We have successfully established a connect started to the database server
      // Continue polling until the connection is established
      while (true)
      {
        PostgresPollingStatusType pollStatus = PQconnectPoll(m_conn);
        if (pollStatus == PGRES_POLLING_READING || pollStatus == PGRES_POLLING_WRITING)
        {
          // Wait for the socket to be ready for reading/writing
          m_sock = PQsocket(m_conn);
          // std::cout << "Polling read or write on connect conn starting.. " << m_sock << std::endl;
          if (m_sock < 0)
          {
            handleError("Invalid socket", std::string(PQerrorMessage(m_conn)));
          }
        }
        else if (pollStatus == PGRES_POLLING_OK)
        {
          D(std::cout << "PQClient postgres connection established successfully!" << std::endl;)
          break;
        }
        else
        {
          handleError("Connection failed", std::string(PQerrorMessage(m_conn)));
        }
      }

      m_pgSocket.assign(m_sock);

      D(std::cout << "------------------------" << std::endl;
        std::cout << "PQClient Connection info" << std::endl;
        printf("Port: %s\n", PQport(m_conn));
        printf("Host: %s\n", PQhost(m_conn));
        printf("DBName: %s\n", PQdb(m_conn));
        std::cout << "------------------------" << std::endl;)
    }
    catch (std::string error)
    {
      if (m_conn)
      {
        PQfinish(m_conn);
      }
      throw error;
    }
  }

  PQClient::~PQClient()
  {
    D(std::cout << "Destructor PQClient\n";)
    m_pgSocket.cancel();

    if (m_conn)
    {
      PQfinish(m_conn);
    }
  }

  void PQClient::handleError(const string &message, const string &errorMessage)
  {
    std::stringstream ss;
    ss << message << ": " << errorMessage << std::endl;
    throw ss.str();
  }

  void PQClient::handleError(const string &message)
  {
    throw message;
  }

  void PQClient::asyncQuery(const std::string &query, std::function<void(PGresult *)> callback)
  {
    // Post the query execution to the strand
    auto postStrand = [this, query, callback]()
    {
      if (PQsendQuery(m_conn, query.c_str()) == 0)
      {
        D(std::cerr << "Failed to send query: " << query.c_str() << " - " << PQerrorMessage(m_conn) << std::endl;)
        callback(nullptr);
        return;
      }

      // Start monitoring the socket for readiness
      waitForResult(callback);
    };

    boost::asio::post(m_strand, postStrand);
  }

  void PQClient::asyncParamQuery(
      const std::string &query,
      // const std::vector<const char *> &params,
      const std::vector<std::string> &paramStrings,
      const std::vector<int> &paramLengths,
      const std::vector<int> &paramFormats,
      std::function<void(PGresult *)> callback)
  {
    // Post the query execution to the strand
    auto postStrand = [this, query, paramStrings, paramLengths, paramFormats, callback]()
    {
      auto paramValues = std::make_shared<std::vector<const char *>>();
      for (const auto &s : paramStrings)
      {
        strcmp(s.c_str(), "NULL") == 0 ? paramValues->push_back(nullptr) : paramValues->push_back(s.c_str());
      }
      auto paramLengthsCopy = std::make_shared<std::vector<int>>(paramLengths);
      auto paramFormatsCopy = std::make_shared<std::vector<int>>(paramFormats);

      if (PQsendQueryParams(
              m_conn,
              query.c_str(),
              paramValues->size(),
              nullptr,
              paramValues->data(),
              paramLengthsCopy->data(),
              paramFormatsCopy->data(),
              0) == 0)
      {
        D(std::cerr << "Failed to send param query: " << PQerrorMessage(m_conn) << std::endl;)
        callback(nullptr);
        return;
      }

      // Start monitoring the socket for readiness
      waitForResult(callback);
    };

    boost::asio::post(m_strand, postStrand);
  }

  void PQClient::waitForResult(std::function<void(PGresult *)> callback)
  {
    auto resultWaitCallback = [this, callback](const boost::system::error_code &ec)
    {
      if (ec)
      {
        D(std::cerr << "Error waiting for socket: " + ec.message() << std::endl;)
        callback(nullptr);
        return;
      }

      // Pump libpq's input into its internal buffers.
      if (PQconsumeInput(m_conn) == 0)
      {
        D(std::cerr << "Error consuming input: " << PQerrorMessage(m_conn) << std::endl;)
        callback(nullptr);
        return;
      }

      // Check if the query is still busy.
      if (PQisBusy(m_conn))
      {
        D(std::cout << "Query is still busy" << std::endl;)
        waitForResult(callback);
        return;
      }

      while (PGresult *res = PQgetResult(m_conn))
      {
        D(std::cerr << "m_pgSocket.async_wait found results " << std::endl;)
        callback(res);
        PQclear(res);
      }
      // No more results, callback with nullptr to indicate completion
      D(std::cerr << "m_pgSocket.async_wait no more results " << std::endl;)
      callback(nullptr);
    };

    m_pgSocket.async_wait(
        boost::asio::posix::stream_descriptor::wait_read,
        boost::asio::bind_executor(m_strand, resultWaitCallback));
  }
} // namespace Rest
