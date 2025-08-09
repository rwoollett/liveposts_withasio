#ifndef SRC_RESTREQEUSTPARAMETERS_H
#define SRC_RESTREQEUSTPARAMETERS_H

#include <string>
#include <unordered_map>
#include <vector>

using std::string;
using std::unordered_map;

namespace Rest
{

  class Parameters
  {
    using Param_List = unordered_map<string, string>;
    Param_List m_parameters{};

  public:

    void set(const string &name, const string &value);
    string &operator[](const string &name);

    const string get(const string &name) const;
    const string operator[](const string &name) const;

    Param_List::iterator begin() { return m_parameters.begin(); }
    Param_List::iterator end() { return m_parameters.end(); }

    Param_List::const_iterator begin() const { return m_parameters.begin(); }
    Param_List::const_iterator end() const { return m_parameters.end(); }

    size_t size() { return m_parameters.size();  }
  };

}

#endif /* SRC_RESTREQEUSTPARAMETERS_H */