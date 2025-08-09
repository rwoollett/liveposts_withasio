#include "Parameters.h"
#include <algorithm>

void Rest::Parameters::set(const string &name, const string &value)
{
  m_parameters[name] = value;
};

string &Rest::Parameters::operator[](const string &name)
{
  return m_parameters[name];
};

const string Rest::Parameters::get(const string &name) const
{
  auto it = m_parameters.find(name);
  if (it != m_parameters.end())
  {
    return it->second;
  }
  return std::string();
};

const string Rest::Parameters::operator[](const string &name) const
{
  auto it = m_parameters.find(name);
  if (it != m_parameters.end())
  {
    return it->second;
  }
  return std::string();
};
