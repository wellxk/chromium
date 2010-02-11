// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <string>

#include "base/scoped_ptr.h"
#include "chrome/common/extensions/extension_message_bundle.h"
#include "chrome/common/extensions/extension_message_filter_peer.h"
#include "chrome/common/filter_policy.h"
#include "ipc/ipc_message.h"
#include "ipc/ipc_sync_message.h"
#include "net/base/net_errors.h"
#include "net/url_request/url_request_status.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "webkit/glue/resource_loader_bridge.h"

using testing::_;
using testing::DoAll;
using testing::Invoke;
using testing::StrEq;
using testing::Return;

static const char* const kExtensionUrl_1 =
    "chrome-extension://some_id/popup.css";

static const char* const kExtensionUrl_2 =
    "chrome-extension://some_id2/popup.css";

static const char* const kExtensionUrl_3 =
    "chrome-extension://some_id3/popup.css";

void MessageDeleter(IPC::Message* message) {
  delete static_cast<IPC::SyncMessage*>(message)->GetReplyDeserializer();
  delete message;
}

class MockIpcMessageSender : public IPC::Message::Sender {
 public:
  MockIpcMessageSender() {
    ON_CALL(*this, Send(_))
        .WillByDefault(DoAll(Invoke(MessageDeleter), Return(true)));
  }

  virtual ~MockIpcMessageSender() {}

  MOCK_METHOD1(Send, bool(IPC::Message* message));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockIpcMessageSender);
};

class MockResourceLoaderBridgePeer
    : public webkit_glue::ResourceLoaderBridge::Peer {
 public:
  MockResourceLoaderBridgePeer() {}
  virtual ~MockResourceLoaderBridgePeer() {}

  MOCK_METHOD2(OnUploadProgress, void(uint64 position, uint64 size));
  MOCK_METHOD4(OnReceivedRedirect, bool(
      const GURL& new_url,
      const webkit_glue::ResourceLoaderBridge::ResponseInfo& info,
      bool* has_new_first_party_for_cookies,
      GURL* new_first_party_for_cookies));
  MOCK_METHOD2(OnReceivedResponse, void(
      const webkit_glue::ResourceLoaderBridge::ResponseInfo& info,
      bool content_filtered));
  MOCK_METHOD2(OnReceivedData, void(const char* data, int len));
  MOCK_METHOD2(OnCompletedRequest, void(
      const URLRequestStatus& status, const std::string& security_info));
  MOCK_CONST_METHOD0(GetURLForDebugging, GURL());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockResourceLoaderBridgePeer);
};

class ExtensionMessageFilterPeerTest : public testing::Test {
 protected:
  virtual void SetUp() {
    sender_.reset(new MockIpcMessageSender());
    original_peer_.reset(new MockResourceLoaderBridgePeer());
    filter_peer_.reset(
        ExtensionMessageFilterPeer::CreateExtensionMessageFilterPeer(
            original_peer_.get(), sender_.get(), "text/css",
            FilterPolicy::FILTER_EXTENSION_MESSAGES, GURL(kExtensionUrl_1)));
  }

  ExtensionMessageFilterPeer* CreateExtensionMessageFilterPeer(
      const std::string& mime_type,
      FilterPolicy::Type filter_policy,
      const GURL& request_url) {
    return ExtensionMessageFilterPeer::CreateExtensionMessageFilterPeer(
        original_peer_.get(), sender_.get(),
        mime_type, filter_policy, request_url);
  }

  std::string GetData(ExtensionMessageFilterPeer* filter_peer) {
    EXPECT_TRUE(NULL != filter_peer);
    return filter_peer->data_;
  }

  void SetData(ExtensionMessageFilterPeer* filter_peer,
               const std::string& data) {
    EXPECT_TRUE(NULL != filter_peer);
    filter_peer->data_ = data;
  }

  scoped_ptr<MockIpcMessageSender> sender_;
  scoped_ptr<MockResourceLoaderBridgePeer> original_peer_;
  scoped_ptr<ExtensionMessageFilterPeer> filter_peer_;
};

TEST_F(ExtensionMessageFilterPeerTest, CreateWithWrongFilterPolicy) {
  filter_peer_.reset(CreateExtensionMessageFilterPeer(
      "text/css", FilterPolicy::DONT_FILTER, GURL(kExtensionUrl_1)));
  EXPECT_TRUE(NULL == filter_peer_.get());
}

TEST_F(ExtensionMessageFilterPeerTest, CreateWithWrongMimeType) {
  filter_peer_.reset(CreateExtensionMessageFilterPeer(
      "text/html",
      FilterPolicy::FILTER_EXTENSION_MESSAGES,
      GURL(kExtensionUrl_1)));
  EXPECT_TRUE(NULL == filter_peer_.get());
}

TEST_F(ExtensionMessageFilterPeerTest, CreateWithValidInput) {
  EXPECT_TRUE(NULL != filter_peer_.get());
}

TEST_F(ExtensionMessageFilterPeerTest, OnReceivedData) {
  EXPECT_TRUE(GetData(filter_peer_.get()).empty());

  const std::string data_chunk("12345");
  filter_peer_->OnReceivedData(data_chunk.c_str(), data_chunk.length());

  EXPECT_EQ(data_chunk, GetData(filter_peer_.get()));

  filter_peer_->OnReceivedData(data_chunk.c_str(), data_chunk.length());
  EXPECT_EQ(data_chunk + data_chunk, GetData(filter_peer_.get()));
}

MATCHER_P(IsURLRequestEqual, status, "") { return arg.status() == status; }

TEST_F(ExtensionMessageFilterPeerTest, OnCompletedRequestBadURLRequestStatus) {
  // It will self-delete once it exits OnCompletedRequest.
  ExtensionMessageFilterPeer* filter_peer = filter_peer_.release();

  EXPECT_CALL(*original_peer_, OnReceivedResponse(_, true));
  EXPECT_CALL(*original_peer_, OnCompletedRequest(
      IsURLRequestEqual(URLRequestStatus::CANCELED), ""));

  URLRequestStatus status;
  status.set_status(URLRequestStatus::FAILED);
  filter_peer->OnCompletedRequest(status, "");
}

TEST_F(ExtensionMessageFilterPeerTest, OnCompletedRequestEmptyData) {
  // It will self-delete once it exits OnCompletedRequest.
  ExtensionMessageFilterPeer* filter_peer = filter_peer_.release();

  EXPECT_CALL(*original_peer_, GetURLForDebugging()).Times(0);
  EXPECT_CALL(*original_peer_, OnReceivedData(_, _)).Times(0);
  EXPECT_CALL(*sender_, Send(_)).Times(0);

  EXPECT_CALL(*original_peer_, OnReceivedResponse(_, true));
  EXPECT_CALL(*original_peer_, OnCompletedRequest(
      IsURLRequestEqual(URLRequestStatus::SUCCESS), ""));

  URLRequestStatus status;
  status.set_status(URLRequestStatus::SUCCESS);
  filter_peer->OnCompletedRequest(status, "");
}

TEST_F(ExtensionMessageFilterPeerTest, OnCompletedRequestNoCatalogs) {
  // It will self-delete once it exits OnCompletedRequest.
  ExtensionMessageFilterPeer* filter_peer = filter_peer_.release();

  SetData(filter_peer, "some text");

  EXPECT_CALL(*original_peer_, GetURLForDebugging()).Times(0);
  EXPECT_CALL(*sender_, Send(_));

  std::string data = GetData(filter_peer);
  EXPECT_CALL(*original_peer_,
              OnReceivedData(StrEq(data.data()), data.length())).Times(2);

  EXPECT_CALL(*original_peer_, OnReceivedResponse(_, true)).Times(2);
  EXPECT_CALL(*original_peer_, OnCompletedRequest(
      IsURLRequestEqual(URLRequestStatus::SUCCESS), "")).Times(2);

  URLRequestStatus status;
  status.set_status(URLRequestStatus::SUCCESS);
  filter_peer->OnCompletedRequest(status, "");

  // Test if Send gets called again (it shouldn't be) when first call returned
  // an empty dictionary.
  filter_peer = CreateExtensionMessageFilterPeer(
      "text/css",
      FilterPolicy::FILTER_EXTENSION_MESSAGES,
      GURL(kExtensionUrl_1));
  SetData(filter_peer, "some text");
  filter_peer->OnCompletedRequest(status, "");
}

TEST_F(ExtensionMessageFilterPeerTest, OnCompletedRequestWithCatalogs) {
  // It will self-delete once it exits OnCompletedRequest.
  ExtensionMessageFilterPeer* filter_peer = CreateExtensionMessageFilterPeer(
      "text/css",
      FilterPolicy::FILTER_EXTENSION_MESSAGES,
      GURL(kExtensionUrl_2));

  L10nMessagesMap messages;
  messages.insert(std::make_pair("text", "new text"));
  ExtensionToL10nMessagesMap& l10n_messages_map =
      *GetExtensionToL10nMessagesMap();
  l10n_messages_map["some_id2"] = messages;

  SetData(filter_peer, "some __MSG_text__");

  EXPECT_CALL(*original_peer_, GetURLForDebugging()).Times(0);
  // We already have messages in memory, Send will be skipped.
  EXPECT_CALL(*sender_, Send(_)).Times(0);

  // __MSG_text__ gets replaced with "new text".
  std::string data("some new text");
  EXPECT_CALL(*original_peer_,
              OnReceivedData(StrEq(data.data()), data.length()));

  EXPECT_CALL(*original_peer_, OnReceivedResponse(_, true));
  EXPECT_CALL(*original_peer_, OnCompletedRequest(
      IsURLRequestEqual(URLRequestStatus::SUCCESS), ""));

  URLRequestStatus status;
  status.set_status(URLRequestStatus::SUCCESS);
  filter_peer->OnCompletedRequest(status, "");
}

TEST_F(ExtensionMessageFilterPeerTest, OnCompletedRequestReplaceMessagesFails) {
  // It will self-delete once it exits OnCompletedRequest.
  ExtensionMessageFilterPeer* filter_peer = CreateExtensionMessageFilterPeer(
      "text/css",
      FilterPolicy::FILTER_EXTENSION_MESSAGES,
      GURL(kExtensionUrl_3));

  L10nMessagesMap messages;
  messages.insert(std::make_pair("text", "new text"));
  ExtensionToL10nMessagesMap& l10n_messages_map =
      *GetExtensionToL10nMessagesMap();
  l10n_messages_map["some_id3"] = messages;

  std::string message("some __MSG_missing_message__");
  SetData(filter_peer, message);

  EXPECT_CALL(*original_peer_, GetURLForDebugging()).Times(0);
  // We already have messages in memory, Send will be skipped.
  EXPECT_CALL(*sender_, Send(_)).Times(0);

  // __MSG_missing_message__ is missing, so message stays the same.
  EXPECT_CALL(*original_peer_,
              OnReceivedData(StrEq(message.data()), message.length()));

  EXPECT_CALL(*original_peer_, OnReceivedResponse(_, true));
  EXPECT_CALL(*original_peer_, OnCompletedRequest(
      IsURLRequestEqual(URLRequestStatus::SUCCESS), ""));

  URLRequestStatus status;
  status.set_status(URLRequestStatus::SUCCESS);
  filter_peer->OnCompletedRequest(status, "");
}
