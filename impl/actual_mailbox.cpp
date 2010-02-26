#include "actual_mailbox.h"
#include "tinch_pp/erl_object.h"
#include "node_access.h"
#include "matchable_seq.h"
#include "erl_cpp_exception.h"
#include <boost/date_time/posix_time/posix_time.hpp>

using namespace tinch_pp;
using namespace boost;

namespace {

  msg_seq serialized(const erl::object& message);

}

actual_mailbox::actual_mailbox(node_access& a_node,
			                            const pid_t& a_own_pid,
                               asio::io_service& a_service)
  : node(a_node),
    own_pid(a_own_pid),
    service(a_service),
    message_ready(false)
{
}

actual_mailbox::actual_mailbox(node_access& a_node,
			                            const pid_t& a_own_pid,
                               asio::io_service& a_service,
			                            const std::string& a_own_name)
  : node(a_node),
    own_pid(a_own_pid),
    service(a_service),
    own_name(a_own_name),
    message_ready(false)
{
}

tinch_pp::pid_t actual_mailbox::self() const
{
  return own_pid;
}

std::string actual_mailbox::name() const
{
  return own_name;
}

void actual_mailbox::send(const pid_t& to_pid, const erl::object& message)
{
  node.deliver(serialized(message), to_pid);
}

void actual_mailbox::send(const std::string& to_name, const erl::object& message)
{
  node.deliver(serialized(message), to_name);
}

void actual_mailbox::send(const std::string& to_name, const std::string& on_given_node, const erl::object& message)
{
  node.deliver(serialized(message), to_name, on_given_node, self());
}

matchable_ptr actual_mailbox::receive()
{
  unique_lock<mutex> lock(received_msgs_mutex);

  wait_for_at_least_one_message(lock);

  const matchable_ptr msg(new matchable_seq(pick_first_msg()));

  return msg;
}

matchable_ptr actual_mailbox::receive(time_type_sec tmo)
{
  asio::deadline_timer timer(service, posix_time::seconds(tmo));

  timer.async_wait(bind(&actual_mailbox::receive_tmo, this, asio::placeholders::error));
  
  matchable_ptr msg = receive();

  timer.cancel();

  return msg;
}

// Always invoked with the mutex locked.
void actual_mailbox::wait_for_at_least_one_message(unique_lock<mutex>& lock)
{
  if(!received_msgs.empty())
    return;

  while(!message_ready)
    message_received_cond.wait(lock);
}

msg_seq actual_mailbox::pick_first_msg()
{
  message_ready = false;

  if(received_msgs.empty())
    throw mailbox_receive_tmo();

  const msg_seq msg(received_msgs.front());

  received_msgs.pop_front();

  return msg;
}

void actual_mailbox::on_incoming(const msg_seq& msg)
{
  {
    lock_guard<mutex> lock(received_msgs_mutex);

    // Design decision: for a selective receive, if we already got messages waiting, its 
    // more likely that this is the one we're waiting for.
    received_msgs.push_front(msg);

    message_ready = true;
  }
  message_received_cond.notify_one();
}

void actual_mailbox::receive_tmo(const boost::system::error_code& error)
{
  const bool is_old_notification = error && (error == boost::asio::error::operation_aborted);

  if(is_old_notification)
    return;

  {
    lock_guard<mutex> lock(received_msgs_mutex);
  
    message_ready = true;
  }
  message_received_cond.notify_one();
}

namespace {

msg_seq serialized(const erl::object& message)
{
  msg_seq s;
  msg_seq_out_iter out(s);

  message.serialize(out);

  return s;
}

}
