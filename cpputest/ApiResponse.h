
#ifndef NETWORKCS_API_RESPONSE_H
#define NETWORKCS_API_RESPONSE_H

#include <string>
#include <optional>
#include <variant>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

namespace nlohmann {
    template <typename T>
    struct adl_serializer<std::optional<T>> {
        static void to_json(json & json, std::optional<T> const & opt) {
            if (opt.has_value()) {
                json = *opt;
            } else {
                json = nullptr;
            }
        }

        static void from_json(const json & json, std::optional<T> & opt) {
            if (json.is_null()) {
                opt.reset();
            } else {
                opt = json.get<T>();
            }
        }
    };
}

struct ApiError
{
  std::string message;
};
template <typename Data>
using ApiResponse = std::variant<Data, ApiError>;

template <typename InstanceData>
static ApiResponse<InstanceData> response(json const &json, const std::string &key)
{
  auto error = json.find("error");
  if (error != json.end())
  {
    ApiError apiError;
    apiError.message = *error;
    return apiError;
  }
  else
  {
    auto const &data = json.at(key);
    return InstanceData(data);
  }
};

#endif //NETWORKCS_API_RESPONSE_H