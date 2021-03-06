// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#pragma once

#include "blobs/protocol/blob_rest_client.hpp"
#include "protocol/datalake_rest_client.hpp"

namespace Azure { namespace Storage { namespace Files { namespace DataLake {

  // ServiceClient models:

  using UserDelegationKey = Blobs::UserDelegationKey;
  using ListFileSystemsResult = ServiceListFileSystemsResponse;

  // FileSystemClient models:

  struct FileSystemProperties
  {
    std::string ETag;
    std::string LastModified;
    std::map<std::string, std::string> Metadata;
  };

  using FileSystemInfo = FileSystemCreateResponse;

  // PathClient models:

  using PathDeleteInfo = PathDeleteResponse;

  struct Acl
  {
    std::string Scope;
    std::string Type;
    std::string Id;
    std::string Permissions;

    /**
     * @brief Creates an Acl based on acl input string.
     * @param aclString the string to be parsed to Acl.
     * @return Acl
     */
    static Acl FromString(const std::string& aclString);

    /**
     * @brief Creates a string from an Acl.
     * @param acl the acl object to be serialized to a string.
     * @return std::string
     */
    static std::string ToString(const Acl& acl);

    /**
     * @brief Creates a vector of Acl from a string that indicates multiple acls.
     * @param dataLakeAclsString the string that contains multiple acls.
     * @return std::vector<Acl>
     */
    static std::vector<Acl> DeserializeAcls(const std::string& dataLakeAclsString);

    /**
     * @brief Creates a string that contains several Acls.
     * @param dataLakeAclsArray the acls to be serialized into a string.
     * @return std::string
     */
    static std::string SerializeAcls(const std::vector<Acl>& dataLakeAclsArray);
  };

  struct PathProperties
  {
    std::string ETag;
    std::string LastModified;
    std::string CreationTime;
    std::map<std::string, std::string> Metadata;
    Azure::Core::Nullable<std::string> LeaseDuration;
    Azure::Core::Nullable<LeaseStateType> LeaseState;
    Azure::Core::Nullable<LeaseStatusType> LeaseStatus;
    DataLakeHttpHeaders HttpHeaders;
    Azure::Core::Nullable<bool> ServerEncrypted;
    Azure::Core::Nullable<std::string> EncryptionKeySHA256;
    Azure::Core::Nullable<bool> AccessTierInferred;
    Azure::Core::Nullable<std::string> AccessTierChangeTime;
    Azure::Core::Nullable<std::string> CopyId;
    Azure::Core::Nullable<std::string> CopySource;
    Azure::Core::Nullable<Blobs::CopyStatus> CopyStatus;
    Azure::Core::Nullable<std::string> CopyProgress;
    Azure::Core::Nullable<std::string> CopyCompletionTime;
  };

  struct PathAccessControl
  {
    std::string ETag;
    std::string LastModified;
    std::vector<Acl> Acls;
  };

  struct SetPathHttpHeadersInfo
  {
    std::string ETag;
    std::string LastModified;
  };

  struct SetPathMetadataInfo
  {
    std::string ETag;
    std::string LastModified;
  };

  struct PathInfo
  {
    Azure::Core::Nullable<std::string> ETag;
    Azure::Core::Nullable<std::string> LastModified;
    Azure::Core::Nullable<int64_t> ContentLength;
  };

  using PathSetAccessControlInfo = PathSetAccessControlResponse;

  // FileClient models:

  using FileContentInfo = Blobs::BlobContentInfo;
  using PathAppendDataInfo = PathAppendDataResponse;
  using PathFlushDataInfo = PathFlushDataResponse;

  struct FileReadInfo
  {
    std::unique_ptr<Azure::Core::Http::BodyStream> Body;
    DataLakeHttpHeaders HttpHeaders;
    Azure::Core::Nullable<int64_t> RangeOffset;
    Azure::Core::Nullable<int64_t> RangeLength;
    Azure::Core::Nullable<std::string> TransactionalMD5;
    std::string ETag;
    std::string LastModified;
    Azure::Core::Nullable<std::string> LeaseDuration;
    LeaseStateType LeaseState;
    LeaseStatusType LeaseStatus;
    Azure::Core::Nullable<std::string> ContentMD5;
    std::map<std::string, std::string> Metadata;
  };

  struct FileRenameInfo
  {
    Azure::Core::Nullable<std::string> ETag;
    Azure::Core::Nullable<std::string> LastModified;
  };

  struct FileDeleteInfo
  {
  };

  struct FileDownloadInfo
  {
    std::string ETag;
    std::string LastModified;
    int64_t ContentLength = 0;
    DataLakeHttpHeaders HttpHeaders;
    std::map<std::string, std::string> Metadata;
    Azure::Core::Nullable<bool> ServerEncrypted;
    Azure::Core::Nullable<std::string> EncryptionKeySHA256;
  };

  using FileInfo = PathInfo;

  // DirectoryClient models:

  struct DirectoryRenameInfo
  {
    Azure::Core::Nullable<std::string> ETag;
    Azure::Core::Nullable<std::string> LastModified;
    Azure::Core::Nullable<std::string> Continuation;
  };

  using DirectorySetAccessControlRecursiveInfo = PathSetAccessControlRecursiveResponse;
  using DirectoryInfo = PathInfo;
  using DirectoryDeleteInfo = PathDeleteResponse;

}}}} // namespace Azure::Storage::Files::DataLake
