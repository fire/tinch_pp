#include "tinch_pp/node.h"
#include "utils.h"
#include "actual_mailbox.h"
#include "ScopeGuard.h"
#include <boost/bind.hpp>
#include <iostream>
#include <cassert>

using namespace tinch_pp;
using namespace boost;

struct erl_cpp_exception;

namespace {

std::string valid_node_name(const std::string& user_provided)
{
  // TODO: verify the name, throw exception!
  // => move to utilities!
  return user_provided;
}

}

namespace tinch_pp {

  bool operator <(const pid_t& p1, const pid_t& p2);

}

// TODO: Implement!
//node(const std::string& node_name);

node::node(const std::string& a_node_name, const std::string& a_cookie)
  : work(io_service),
    epmd(io_service, "127.0.0.1", 4369),
    node_name_(valid_node_name(a_node_name)),
    cookie_(a_cookie),
    connector(*this, io_service),
    async_io_runner(&node::run_async_io, this),
    // variabled used to build pids:
    pid_id(1),
    serial(0),
    creation(0)
{
}

node::~node()
{
  // Terminate all ongoing operations and kill the thread used to dispatch async I/O.
  // As an alternative, we could invoke work.reset() and allow all ongoing operations 
  // to complete => better?
  io_service.stop();
  async_io_runner.join();
}

void node::publish_port(port_number_type incoming_connections_port)
{
  epmd.connect();
  creation = epmd.alive2_request(utils::node_name(node_name_), incoming_connections_port);
  
  connector.start_accept_incoming(incoming_connections_port);
}

bool node::ping_peer(const std::string& peer_node_name)
{
  bool pong = false;

  try {
    pong = !!connector.get_connection_to(peer_node_name);
  } catch(const erl_cpp_exception&) {
    // don't let a failed connection atempt propage through this API.
  }

  return pong;
}

mailbox_ptr node::create_mailbox()
{
  actual_mailbox_ptr mbox(new actual_mailbox(*this, make_pid(), io_service));

  const mutex_guard guard(mailboxes_lock);

  const bool added = mailboxes.insert(std::make_pair(mbox->self(), mbox)).second;
  assert(added);

  return mbox;
}

mailbox_ptr node::create_mailbox(const std::string& registered_name)
{
  actual_mailbox_ptr mbox(new actual_mailbox(*this, make_pid(), io_service, registered_name));

  const mutex_guard guard(mailboxes_lock);

  mailboxes.insert(std::make_pair(mbox->self(), mbox));
  Loki::ScopeGuard insert_guard = Loki::MakeGuard(bind(&node::remove, this, ::_1), mbox);

  registered_mailboxes.insert(std::make_pair(registered_name, mbox));

  insert_guard.Dismiss();

  return mbox;
}

std::vector<std::string> node::connected_nodes() const
{
  return connector.connected_nodes();
}

void node::remove(mailbox_ptr mailbox)
{
  mailboxes.erase(mailbox->self());
  // Not all mailboxes have an registered name, but erase is fine anyway (no throw).
  registered_mailboxes.erase(mailbox->name());
}

void node::deliver(const msg_seq& msg, const pid_t& to_pid)
{
  node_connection_ptr connection = connector.get_connection_to(to_pid.node_name);

  connection->send(msg, to_pid);
}

void node::deliver(const msg_seq& msg, const std::string& to_name)
{
  this->receive_incoming(msg, to_name);
}

void node::deliver(const msg_seq& msg, const std::string& to_name, 
		   const std::string& given_node, const pid_t& from_pid)
{
  
  node_connection_ptr connection = connector.get_connection_to(given_node);

  connection->send(msg, to_name, from_pid);
}

void node::receive_incoming(const msg_seq& msg, const pid_t& to)
{
  const mutex_guard guard(mailboxes_lock);

  mailboxes_type::iterator mbox = mailboxes.find(to);

  if(mbox == mailboxes.end())
    throw erl_cpp_exception("Failed to deliver message - mailbox not known!"); // TODO: more info!

  actual_mailbox_ptr destination = mbox->second;
  destination->on_incoming(msg);
}

void node::receive_incoming(const msg_seq& msg, const std::string& to)
{
  const mutex_guard guard(mailboxes_lock);

  registered_mailboxes_type::iterator mbox = registered_mailboxes.find(to);

  if(mbox == registered_mailboxes.end())
    throw erl_cpp_exception("Failed to deliver message - mailbox not known. Name = " + to);

  actual_mailbox_ptr destination = mbox->second;
  destination->on_incoming(msg);
}

void node::run_async_io()
{
  for(;;) {
    // Error handling: this is our recovery point.
    // All async networking, and the corresponding handlers, execute in this context.
    try {
      io_service.run();
      break; // normal exit
    } catch(const connection_io_error& io_error) {
      std::cerr << "I/O error for " << io_error.node() << " error: "<< io_error.what() << std::endl;

      // TODO: consider linked mailboxes?
      connector.drop_connection_to(io_error.node());
    } catch(const erl_cpp_exception& e) {
      std::cerr << e.what() << std::endl; // TODO: cleaner...
    }
  }
}

tinch_pp::pid_t node::make_pid()
{
  const pid_t new_pid(node_name_, pid_id, serial, creation);

  update_pid_fields();
  
  return new_pid;
}

void node::update_pid_fields()
{
  // The PID semantics aren't particularly well-specified, but 
  // the algorithm is available in OtpLocalNode::createPid() in Jinterface.
  const boost::uint32_t max_pid_id = 0x7fff;
  const boost::uint32_t max_serial = 0x1fff; // 13 bits
  
  ++pid_id;

  if(pid_id > max_pid_id) {
    pid_id = 0;
    
    serial = (serial + 1) > max_serial ? 0 : serial + 1;
  }
}

namespace tinch_pp {

bool operator <(const pid_t& p1, const pid_t& p2)
{
  // Don't care about the name => it's always identical on the receiving node.
  return (p1.id < p2.id) || (p1.serial < p2.serial);
}

}

