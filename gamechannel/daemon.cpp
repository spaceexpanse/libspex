// Copyright (C) 2019-2022 The Xaya developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "daemon.hpp"

#include <glog/logging.h>

namespace xaya
{

namespace
{

/**
 * Timeout for the GSP RPC connection.  This must not be too long, as otherwise
 * waitforchange calls may block long and prevent the channel daemon from
 * stopping orderly.
 *
 * The default timeout on the server side is 5s, so with 6s here we ensure
 * that typically the calls return ordinarily; but we put up a safety net
 * in case the server is messed up.
 */
constexpr int GSP_RPC_TIMEOUT_MS = 6000;

} // anonymous namespace

ChannelDaemon::XayaBasedInstances::XayaBasedInstances (
    ChannelDaemon& d, const std::string& rpc,
    const jsonrpc::clientVersion_t rpcVersion)
  : xayaClient(rpc),
    xayaRpc(xayaClient, rpcVersion),
    xayaWallet(xayaClient, rpcVersion),
    cm(d.rules, d.channel, xayaRpc, xayaWallet, d.channelId, d.playerName),
    sender(d.gameId, d.channelId, d.playerName, xayaRpc, xayaWallet, d.channel)
{
  cm.SetMoveSender (sender);
}

ChannelDaemon::XayaBasedInstances::~XayaBasedInstances ()
{
  cm.StopUpdates ();
}

ChannelDaemon::GspFeederInstances::GspFeederInstances (ChannelDaemon& d,
                                                       const std::string& rpc)
  : gspClient(rpc), gspRpc(gspClient),
    feeder(gspRpc, d.xayaBased->cm)
{
  gspClient.SetTimeout (GSP_RPC_TIMEOUT_MS);
}

void
ChannelDaemon::ConnectXayaRpc (const std::string& url, const bool legacy)
{
  CHECK (xayaBased == nullptr);
  const auto rpcVersion = (legacy
                            ? jsonrpc::JSONRPC_CLIENT_V1
                            : jsonrpc::JSONRPC_CLIENT_V2);
  xayaBased = std::make_unique<XayaBasedInstances> (*this, url, rpcVersion);
}

void
ChannelDaemon::ConnectGspRpc (const std::string& url)
{
  CHECK (xayaBased != nullptr);
  CHECK (feeder == nullptr);
  feeder = std::make_unique<GspFeederInstances> (*this, url);
}

ChannelManager&
ChannelDaemon::GetChannelManager ()
{
  CHECK (xayaBased != nullptr);
  return xayaBased->cm;
}

void
ChannelDaemon::SetOffChainBroadcast (OffChainBroadcast& b)
{
  CHECK (xayaBased != nullptr);
  CHECK (offChain == nullptr);
  offChain = &b;
  xayaBased->cm.SetOffChainBroadcast (*offChain);
}

void
ChannelDaemon::Start ()
{
  CHECK (xayaBased != nullptr);
  CHECK (feeder != nullptr);
  CHECK (offChain != nullptr);
  CHECK (!startedOnce);
  startedOnce = true;

  feeder->feeder.Start ();
  offChain->Start ();
}

void
ChannelDaemon::Stop ()
{
  CHECK (xayaBased != nullptr);
  CHECK (feeder != nullptr);
  CHECK (offChain != nullptr);
  CHECK (startedOnce);

  feeder->feeder.Stop ();
  offChain->Stop ();
  xayaBased->cm.StopUpdates ();
}

void
ChannelDaemon::Run ()
{
  internal::MainLoop::Functor startAction = [this] () { Start (); };
  internal::MainLoop::Functor stopAction = [this] () { Stop (); };

  mainLoop.Run (startAction, stopAction);
}

} // namespace xaya
