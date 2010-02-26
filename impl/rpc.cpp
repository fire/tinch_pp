#include "tinch_pp/rpc.h"
#include "tinch_pp/node.h"
#include "tinch_pp/mailbox.h"
#include "tinch_pp/erlang_types.h"
#include "erl_cpp_exception.h"

using namespace tinch_pp;
using namespace tinch_pp::erl;
namespace fusion = boost::fusion;

namespace {

// {self, { call, Mod, Fun, Args, user}}
typedef fusion::tuple<atom, atom, atom, rpc_argument_type, atom> call_type;
typedef fusion::tuple<pid, tuple<call_type> > rpc_call_type;

matchable_ptr receive_rpc_reply(mailbox_ptr mbox,
                                const std::string& remote_node,
                                const module_and_function_type& remote_fn);
}

rpc::rpc(node& own_node)
   : mbox(own_node.create_mailbox())
{
}

matchable_ptr rpc::blocking_rpc(const std::string& remote_node,
                                const module_and_function_type& remote_fn,
                                const rpc_argument_type& arguments)
{
   const call_type call(atom("call"), atom(remote_fn.first), atom(remote_fn.second), arguments, atom("user"));

   const rpc_call_type rpc_call(pid(mbox->self()), tuple<call_type>(call));

   mbox->send("rex", remote_node, tuple<rpc_call_type>(rpc_call));

   return receive_rpc_reply(mbox, remote_node, remote_fn);
}

namespace {

matchable_ptr receive_rpc_reply(mailbox_ptr mbox,
                                const std::string& remote_node,
                                const module_and_function_type& remote_fn)
{
   // { rex, Term }
   matchable_ptr result = mbox->receive();

   matchable_ptr reply_part;

   if(!result->match(make_tuple(atom("rex"), any(&reply_part)))) {
      const std::string reason = "RPC: Unexpected result from call to " + remote_node + 
                                 ", function " + remote_fn.first + ":" + remote_fn.second;
      throw erl_cpp_exception(reason);
   }

   return reply_part;
}

}
