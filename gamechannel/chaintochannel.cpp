// Copyright (C) 2018-2022 The XAYA developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chaintochannel.hpp"

#include "protoutils.hpp"

#include "proto/metadata.pb.h"
#include "proto/stateproof.pb.h"

#include <xutil/base64.hpp>

#include <jsonrpccpp/common/errors.h>
#include <jsonrpccpp/common/exception.h>

#include <glog/logging.h>

namespace spacexpanse
{

ChainToChannelFeeder::ChainToChannelFeeder (ChannelGspRpcClient& r,
                                            SynchronisedChannelManager& cm)
  : rpc(r), manager(cm)
{
  auto cmLocked = manager.Read ();
  channelIdHex = cmLocked->GetChannelId ().ToHex ();

  lastBlock.SetNull ();
}

ChainToChannelFeeder::~ChainToChannelFeeder ()
{
  Stop ();
  CHECK (loop == nullptr);
}

namespace
{

/**
 * Decodes a JSON value (which must be a base64-encoded string) into a
 * protocol buffer instance of the given type.
 */
template <typename Proto>
  Proto
  DecodeProto (const Json::Value& val)
{
  CHECK (val.isString ());

  Proto res;
  CHECK (ProtoFromBase64 (val.asString (), res));

  return res;
}

} // anonymous namespace

void
ChainToChannelFeeder::UpdateOnce ()
{
  const auto data = rpc.getchannel (channelIdHex);

  if (data["state"] != "up-to-date")
    {
      LOG (WARNING)
          << "Channel GSP is in state " << data["state"]
          << ", not updating channel";
      return;
    }

  const auto& newBlockVal = data["blockhash"];
  if (newBlockVal.isNull ())
    {
      /* This will typically not happen, since we already check the return
         value of waitforchange.  But there are two situations where we could
         get here:  On the initial update, and (very unlikely) if the
         existing state gets detached between the waitforchange call and when
         we call getchannel.  */
      LOG (WARNING) << "GSP has no current state yet";
      return;
    }
  CHECK (newBlockVal.isString ());
  CHECK (lastBlock.FromHex (newBlockVal.asString ()));

  const auto& heightVal = data["height"];
  CHECK (heightVal.isUInt ());
  const unsigned height = heightVal.asUInt ();

  LOG (INFO)
      << "New on-chain best block: " << lastBlock.ToHex ()
      << " at height " << height;

  const auto& channel = data["channel"];
  if (channel.isNull ())
    {
      LOG (INFO) << "Channel " << channelIdHex << " is not known on-chain";
      auto cmLocked = manager.Access ();
      cmLocked->ProcessOnChainNonExistant (lastBlock, height);
      return;
    }
  CHECK (channel.isObject ());

  CHECK_EQ (channel["id"].asString (), channelIdHex);
  const auto meta
      = DecodeProto<proto::ChannelMetadata> (channel["meta"]["proto"]);
  const auto proof = DecodeProto<proto::StateProof> (channel["state"]["proof"]);

  BoardState reinitState;
  CHECK (DecodeBase64 (channel["reinit"]["base64"].asString (), reinitState));

  unsigned disputeHeight = 0;
  const auto& disputeVal = channel["disputeheight"];
  if (!disputeVal.isNull ())
    {
      CHECK (disputeVal.isUInt ());
      disputeHeight = disputeVal.asUInt ();
    }

  auto cmLocked = manager.Access ();
  cmLocked->ProcessOnChain (lastBlock, height, meta, reinitState,
                            proof, disputeHeight);
  LOG (INFO) << "Updated channel from on-chain state: " << channelIdHex;
}

void
ChainToChannelFeeder::RunLoop ()
{
  UpdateOnce ();

  while (!stopLoop)
    {
      const std::string lastBlockHex = lastBlock.ToHex ();

      std::string newBlockHex;
      try
        {
          newBlockHex = rpc.waitforchange (lastBlockHex);
        }
      catch (const jsonrpc::JsonRpcException& exc)
        {
          /* Especially timeouts are fine, we should just ignore them.
             A relatively small timeout is needed in order to not block
             too long when waiting for shutting down the loop.  */
          VLOG (1) << "Error calling waitforchange: " << exc.what ();
          CHECK_EQ (exc.GetCode (), jsonrpc::Errors::ERROR_CLIENT_CONNECTOR);
          continue;
        }

      if (newBlockHex.empty ())
        {
          VLOG (1) << "GSP does not have any state yet";
          continue;
        }

      if (newBlockHex == lastBlockHex)
        {
          VLOG (1) << "We are already at newest block";
          continue;
        }

      UpdateOnce ();
    }
}

void
ChainToChannelFeeder::Start ()
{
  LOG (INFO) << "Starting chain-to-channel feeder loop...";
  CHECK (loop == nullptr) << "Feeder loop is already running";

  stopLoop = false;
  loop = std::make_unique<std::thread> ([this] ()
    {
      RunLoop ();
    });
}

void
ChainToChannelFeeder::Stop ()
{
  if (loop == nullptr)
    return;

  LOG (INFO) << "Stopping chain-to-channel feeder loop...";

  stopLoop = true;
  loop->join ();
  loop.reset ();
}

} // namespace spacexpanse
