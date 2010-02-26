#ifndef ERL_LIST_H
#define ERL_LIST_H

#include "impl/list_matcher.h"
#include "impl/string_matcher.h"
#include "erl_object.h"
#include <boost/bind.hpp>
#include <list>
#include <algorithm>
#include <cassert>

namespace tinch_pp {
namespace erl {

template<typename T>
class list : public object
{
public:
  typedef std::list<T> list_type;
  typedef list<T> own_type;

  list(const list_type& contained)
    : val(contained),
      to_assign(0),
      match_fn(boost::bind(&own_type::match_value, this, ::_1, ::_2))
  {
  }

  list(list_type* contained)
    : to_assign(contained),
      match_fn(boost::bind(&own_type::assign_matched, ::_1, ::_2, contained))
  {
  }

  virtual void serialize(msg_seq_out_iter& out) const
  {
    using namespace boost;
    
    list_head_g g;
    karma::generate(out, g, val.size());

    std::for_each(val.begin(), val.end(), bind(&object::serialize, ::_1, boost::ref(out)));
    karma::generate(out, karma::byte_(tinch_pp::type_tag::nil_ext));
  }

  virtual bool match(msg_seq_iter& f, const msg_seq_iter& l) const
  {
    return match_fn(f, l);
  }

private:
  bool match_value(msg_seq_iter& f, const msg_seq_iter& l) const
  {
    return matcher::match(val, f, l);
  }

  static bool assign_matched(msg_seq_iter& f, const msg_seq_iter& l, list_type* dest)
  {
    assert(0 != dest);
    return matcher::assign_match(dest, f, l);
  }

private:
  typedef detail::list_matcher<list_type> matcher;

  list_type val;
  list_type* to_assign;
  match_fn_type match_fn;
};

template<>
class list<erl::int_> : public object
{
public:
  typedef std::list<erl::int_> list_type;
  typedef list<erl::int_> own_type;

  list(const list_type& contained)
    : val(contained),
      to_assign(0),
      match_fn(boost::bind(&own_type::match_value, this, ::_1, ::_2))
  {
  }

  list(list_type* contained)
    : to_assign(contained),
      match_fn(boost::bind(&own_type::assign_matched, ::_1, ::_2, contained))
  {
  }

  virtual void serialize(msg_seq_out_iter& out) const
  {
    using namespace boost;
    
    list_head_g g;
    karma::generate(out, g, val.size());

    std::for_each(val.begin(), val.end(), bind(&erl::int_::serialize, ::_1, boost::ref(out)));
    karma::generate(out, karma::byte_(tinch_pp::type_tag::nil_ext));
  }

  virtual bool match(msg_seq_iter& f, const msg_seq_iter& l) const
  {
    return match_fn(f, l);
  }

private:
  bool match_value(msg_seq_iter& f, const msg_seq_iter& l) const
  {
    // Erlang has an optimization for sending lists of small values (<=255), where 
    // the values are packed into a string.
    const bool is_packed_as_string = (type_tag::string_ext == *f);

    return is_packed_as_string ? matcher_s::match(val, f, l) : matcher_l::match(val, f, l);
  }

  static bool assign_matched(msg_seq_iter& f, const msg_seq_iter& l, list_type* dest)
  {
    // Erlang has an optimization for sending lists of small values (<=255), where 
    // the values are packed into a string.
    const bool is_packed_as_string = (type_tag::string_ext == *f);

    assert(0 != dest);
    return is_packed_as_string ? matcher_s::assign_match(dest, f, l) : matcher_l::assign_match(dest, f, l);
  }

private:
  
  typedef detail::list_matcher<list_type> matcher_l;
  typedef detail::string_matcher matcher_s;

  list_type val;
  list_type* to_assign;
  match_fn_type match_fn;
};

template<typename T>
tinch_pp::erl::list<typename T::value_type> make_list(const T& t) // T is a std::list
{
  tinch_pp::erl::list<typename T::value_type> wrapper(t);

  return wrapper;
}

template<typename T>
tinch_pp::erl::list<typename T::value_type> make_list(T* t) // T is a std::list
{
  tinch_pp::erl::list<typename T::value_type> wrapper(t);

  return wrapper;
}

}
}

#endif
