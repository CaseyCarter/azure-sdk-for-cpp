// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "blobs/blob_sas_builder.hpp"
#include "common/crypt.hpp"
#include "common/storage_uri_builder.hpp"

namespace Azure { namespace Storage { namespace Blobs {

  namespace {
    std::string BlobSasResourceToString(BlobSasResource resource)
    {
      if (resource == BlobSasResource::Container)
      {
        return "c";
      }
      else if (resource == BlobSasResource::Blob)
      {
        return "b";
      }
      else if (resource == BlobSasResource::BlobSnapshot)
      {
        return "bs";
      }
      else if (resource == BlobSasResource::BlobVersion)
      {
        return "bv";
      }
      else
      {
        throw std::runtime_error("unknown BlobSasResource value");
      }
    }
  } // namespace

  void BlobSasBuilder::SetPermissions(BlobContainerSasPermissions permissions)
  {
    Permissions.clear();
    // The order matters
    if ((permissions & BlobContainerSasPermissions::Read) == BlobContainerSasPermissions::Read)
    {
      Permissions += "r";
    }
    if ((permissions & BlobContainerSasPermissions::Add) == BlobContainerSasPermissions::Add)
    {
      Permissions += "a";
    }
    if ((permissions & BlobContainerSasPermissions::Create) == BlobContainerSasPermissions::Create)
    {
      Permissions += "c";
    }
    if ((permissions & BlobContainerSasPermissions::Write) == BlobContainerSasPermissions::Write)
    {
      Permissions += "w";
    }
    if ((permissions & BlobContainerSasPermissions::Delete) == BlobContainerSasPermissions::Delete)
    {
      Permissions += "d";
    }
    if ((permissions & BlobContainerSasPermissions::DeleteVersion)
        == BlobContainerSasPermissions::DeleteVersion)
    {
      Permissions += "x";
    }
    if ((permissions & BlobContainerSasPermissions::List) == BlobContainerSasPermissions::List)
    {
      Permissions += "l";
    }
    if ((permissions & BlobContainerSasPermissions::Tags) == BlobContainerSasPermissions::Tags)
    {
      Permissions += "t";
    }
  }

  void BlobSasBuilder::SetPermissions(BlobSasPermissions permissions)
  {
    Permissions.clear();
    // The order matters
    if ((permissions & BlobSasPermissions::Read) == BlobSasPermissions::Read)
    {
      Permissions += "r";
    }
    if ((permissions & BlobSasPermissions::Add) == BlobSasPermissions::Add)
    {
      Permissions += "a";
    }
    if ((permissions & BlobSasPermissions::Create) == BlobSasPermissions::Create)
    {
      Permissions += "c";
    }
    if ((permissions & BlobSasPermissions::Write) == BlobSasPermissions::Write)
    {
      Permissions += "w";
    }
    if ((permissions & BlobSasPermissions::Delete) == BlobSasPermissions::Delete)
    {
      Permissions += "d";
    }
    if ((permissions & BlobSasPermissions::DeleteVersion) == BlobSasPermissions::DeleteVersion)
    {
      Permissions += "x";
    }
    if ((permissions & BlobSasPermissions::Tags) == BlobSasPermissions::Tags)
    {
      Permissions += "t";
    }
  }

  std::string BlobSasBuilder::ToSasQueryParameters(const SharedKeyCredential& credential)
  {
    std::string canonicalName = "/blob/" + credential.AccountName + "/" + ContainerName;
    if (Resource == BlobSasResource::Blob || Resource == BlobSasResource::BlobSnapshot)
    {
      canonicalName += "/" + BlobName;
    }
    std::string protocol = SasProtocolToString(Protocol);
    std::string resource = BlobSasResourceToString(Resource);

    std::string stringToSign = Permissions + "\n" + (StartsOn.HasValue() ? StartsOn.GetValue() : "")
        + "\n" + ExpiresOn + "\n" + canonicalName + "\n" + Identifier + "\n"
        + (IPRange.HasValue() ? IPRange.GetValue() : "") + "\n" + protocol + "\n" + Version + "\n"
        + resource + "\n" + Snapshot + "\n" + CacheControl + "\n" + ContentDisposition + "\n"
        + ContentEncoding + "\n" + ContentLanguage + "\n" + ContentType;

    std::string signature
        = Base64Encode(HMAC_SHA256(stringToSign, Base64Decode(credential.GetAccountKey())));

    UriBuilder builder;
    builder.AppendQuery("sv", Version);
    builder.AppendQuery("spr", protocol);
    if (StartsOn.HasValue())
    {
      builder.AppendQuery("st", StartsOn.GetValue());
    }
    builder.AppendQuery("se", ExpiresOn);
    if (IPRange.HasValue())
    {
      builder.AppendQuery("sip", IPRange.GetValue());
    }
    if (!Identifier.empty())
    {
      builder.AppendQuery("si", Identifier);
    }
    builder.AppendQuery("sr", resource);
    builder.AppendQuery("sp", Permissions);
    builder.AppendQuery("sig", signature, true);
    if (!CacheControl.empty())
    {
      builder.AppendQuery("rscc", CacheControl);
    }
    if (!ContentDisposition.empty())
    {
      builder.AppendQuery("rscd", ContentDisposition);
    }
    if (!ContentEncoding.empty())
    {
      builder.AppendQuery("rsce", ContentEncoding);
    }
    if (!ContentLanguage.empty())
    {
      builder.AppendQuery("rscl", ContentLanguage);
    }
    if (!ContentType.empty())
    {
      builder.AppendQuery("rsct", ContentType);
    }

    return builder.ToString();
  }

  std::string BlobSasBuilder::ToSasQueryParameters(
      const UserDelegationKey& userDelegationKey,
      const std::string& accountName)
  {
    std::string canonicalName = "/blob/" + accountName + "/" + ContainerName;
    if (Resource == BlobSasResource::Blob || Resource == BlobSasResource::BlobSnapshot)
    {
      canonicalName += "/" + BlobName;
    }
    std::string protocol = SasProtocolToString(Protocol);
    std::string resource = BlobSasResourceToString(Resource);

    std::string stringToSign = Permissions + "\n" + (StartsOn.HasValue() ? StartsOn.GetValue() : "")
        + "\n" + ExpiresOn + "\n" + canonicalName + "\n" + userDelegationKey.SignedObjectId + "\n"
        + userDelegationKey.SignedTenantId + "\n" + userDelegationKey.SignedStartsOn + "\n"
        + userDelegationKey.SignedExpiresOn + "\n" + userDelegationKey.SignedService + "\n"
        + userDelegationKey.SignedVersion + "\n" + (IPRange.HasValue() ? IPRange.GetValue() : "")
        + "\n" + protocol + "\n" + Version + "\n" + resource + "\n" + Snapshot + "\n" + CacheControl
        + "\n" + ContentDisposition + "\n" + ContentEncoding + "\n" + ContentLanguage + "\n"
        + ContentType;

    std::string signature
        = Base64Encode(HMAC_SHA256(stringToSign, Base64Decode(userDelegationKey.Value)));

    UriBuilder builder;
    builder.AppendQuery("sv", Version);
    builder.AppendQuery("sr", resource);
    if (StartsOn.HasValue())
    {
      builder.AppendQuery("st", StartsOn.GetValue());
    }
    builder.AppendQuery("se", ExpiresOn);
    builder.AppendQuery("sp", Permissions);
    if (IPRange.HasValue())
    {
      builder.AppendQuery("sip", IPRange.GetValue());
    }
    builder.AppendQuery("spr", protocol);
    builder.AppendQuery("skoid", userDelegationKey.SignedObjectId);
    builder.AppendQuery("sktid", userDelegationKey.SignedTenantId);
    builder.AppendQuery("skt", userDelegationKey.SignedStartsOn);
    builder.AppendQuery("ske", userDelegationKey.SignedExpiresOn);
    builder.AppendQuery("sks", userDelegationKey.SignedService);
    builder.AppendQuery("skv", userDelegationKey.SignedVersion);
    if (!CacheControl.empty())
    {
      builder.AppendQuery("rscc", CacheControl);
    }
    if (!ContentDisposition.empty())
    {
      builder.AppendQuery("rscd", ContentDisposition);
    }
    if (!ContentEncoding.empty())
    {
      builder.AppendQuery("rsce", ContentEncoding);
    }
    if (!ContentLanguage.empty())
    {
      builder.AppendQuery("rscl", ContentLanguage);
    }
    if (!ContentType.empty())
    {
      builder.AppendQuery("rsct", ContentType);
    }
    builder.AppendQuery("sig", signature, true);

    return builder.ToString();
  }

}}} // namespace Azure::Storage::Blobs
